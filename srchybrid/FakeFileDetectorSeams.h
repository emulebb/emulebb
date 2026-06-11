#pragma once

#include "FileTypeClassifierSeams.h"
#include "FilenameTokenizationSeams.h"
#include "Opcodes.h"
#include "RegexMatchSeams.h"

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <iterator>
#include <regex>
#include <string>
#include <vector>

namespace FakeFileDetectorSeams
{
/**
 * @brief User-visible severity bucket for a local fake-file analysis report.
 */
enum class Severity
{
	None,
	Low,
	Medium,
	High,
	Critical
};

/**
 * @brief Local bad-signal rules loaded from the editable filter file.
 */
struct RuleSet
{
	std::vector<std::wstring> tokens;
	std::vector<std::wstring> regexes;
};

/**
 * @brief Evidence collected from search rows, downloads, comments, ratings,
 * and local file-type inspection.
 */
struct Evidence
{
	std::vector<std::wstring> names;
	std::vector<std::wstring> comments;
	std::wstring claimedType;
	EFileType extensionType = FILETYPE_UNKNOWN;
	EFileType headerType = FILETYPE_UNKNOWN;
	bool headerAvailable = false;
	bool headerPending = false;
	uint32_t spamRating = 0;
	bool consideredSpam = false;
	bool badRating = false;
	bool fakeRating = false;
	bool multipleAich = false;
	uint64_t fileSizeBytes = 0;
	uint32_t mediaLengthSeconds = 0;
	uint32_t mediaBitrateKbps = 0;
	bool mediaLengthAvailable = false;
	bool mediaBitrateAvailable = false;
	std::wstring mediaArtist;
	std::wstring mediaAlbum;
	std::wstring mediaTitle;
};

/**
 * @brief Decision-neutral report produced by the local fake-file analyzer.
 */
struct Report
{
	uint32_t score = 0;
	Severity severity = Severity::None;
	std::vector<std::string> reasons;
	std::vector<std::wstring> observedNames;
	std::vector<std::wstring> observedExtensions;
	std::vector<std::wstring> canonicalNames;
	std::vector<std::wstring> ignoredNameTokens;
	std::vector<std::wstring> nameDivergenceGroups;
	EFileType extensionType = FILETYPE_UNKNOWN;
	EFileType headerType = FILETYPE_UNKNOWN;
	std::wstring claimedType;
	bool pendingHeaderCheck = false;
	bool cached = false;
};

inline std::wstring ToLower(std::wstring value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](const wchar_t ch) {
		return static_cast<wchar_t>(std::towlower(ch));
	});
	return value;
}

inline std::wstring Trim(std::wstring value)
{
	while (!value.empty() && std::iswspace(value.front()) != 0)
		value.erase(value.begin());
	while (!value.empty() && std::iswspace(value.back()) != 0)
		value.pop_back();
	return value;
}

inline bool IsTokenBoundary(const wchar_t ch)
{
	return ch == L'\0' || std::iswalnum(ch) == 0;
}

inline bool ContainsToken(const std::wstring &rText, const std::wstring &rToken)
{
	if (rText.empty() || rToken.empty())
		return false;

	size_t uPos = 0;
	while ((uPos = rText.find(rToken, uPos)) != std::wstring::npos) {
		const wchar_t chBefore = uPos == 0 ? L'\0' : rText[uPos - 1];
		const size_t uAfter = uPos + rToken.size();
		const wchar_t chAfter = uAfter >= rText.size() ? L'\0' : rText[uAfter];
		if (IsTokenBoundary(chBefore) && IsTokenBoundary(chAfter))
			return true;
		++uPos;
	}
	return false;
}

inline bool ContainsRegex(const std::wstring &rText, const std::wstring &rPattern)
{
	if (rText.empty() || rPattern.empty() || rPattern.size() > 256 || rText.size() > 1024)
		return false;
	return RegexMatchSeams::Match(rText, rPattern, RegexMatchSeams::MatchMode::Search, std::regex_constants::icase | std::regex_constants::ECMAScript);
}

inline bool ContainsRegex(const std::wstring &rText, const std::wregex &rPattern)
{
	if (rText.empty() || rText.size() > 1024)
		return false;
	try {
		return std::regex_search(rText, rPattern);
	} catch (const std::regex_error&) {
		return false;
	}
}

inline void AddReason(Report &rReport, const char *pszReason, const uint32_t uScore)
{
	if (std::find(rReport.reasons.begin(), rReport.reasons.end(), pszReason) == rReport.reasons.end())
		rReport.reasons.push_back(pszReason);
	rReport.score = std::min<uint32_t>(100, rReport.score + uScore);
}

inline std::wstring GetExtension(std::wstring name)
{
	const size_t uSlash = name.find_last_of(L"\\/");
	const size_t uDot = name.find_last_of(L'.');
	if (uDot == std::wstring::npos || (uSlash != std::wstring::npos && uDot < uSlash) || uDot + 1 >= name.size())
		return std::wstring();
	return ToLower(name.substr(uDot + 1));
}

inline void AddUnique(std::vector<std::wstring> &rValues, std::wstring value)
{
	value = Trim(value);
	if (value.empty())
		return;
	const std::wstring lower = ToLower(value);
	for (const std::wstring &rExisting : rValues) {
		if (ToLower(rExisting) == lower)
			return;
	}
	rValues.push_back(value);
}

inline std::vector<std::wstring> UniqueSortedTokens(const std::vector<std::wstring> &rTokens)
{
	std::vector<std::wstring> out(rTokens);
	std::sort(out.begin(), out.end());
	out.erase(std::unique(out.begin(), out.end()), out.end());
	return out;
}

// A hash/UUID fragment: all hex, and either a long run or a short group carrying a
// digit (the hyphen-split pieces of an ed2k/UUID suffix often appended to names).
inline bool IsHashLikeNameToken(const std::wstring &rToken)
{
	if (rToken.size() < 4)
		return false;
	bool bHasDigit = false;
	for (const wchar_t ch : rToken) {
		const bool bHex = (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f');
		if (!bHex)
			return false;
		bHasDigit = bHasDigit || (ch >= L'0' && ch <= L'9');
	}
	return rToken.size() >= 8 || bHasDigit;
}

inline bool IsAllDigitNameToken(const std::wstring &rToken)
{
	if (rToken.empty())
		return false;
	for (const wchar_t ch : rToken)
		if (ch < L'0' || ch > L'9')
			return false;
	return true;
}

// Season/episode marker like 01x05 (the tokenizer keeps it as one token).
inline bool IsEpisodeMarkerNameToken(const std::wstring &rToken)
{
	const size_t uX = rToken.find(L'x');
	if (uX == std::wstring::npos || uX == 0 || uX + 1 >= rToken.size())
		return false;
	for (size_t i = 0; i < rToken.size(); ++i) {
		if (i == uX)
			continue;
		if (rToken[i] < L'0' || rToken[i] > L'9')
			return false;
	}
	return true;
}

// Non-identifying articles/prepositions/conjunctions across the supported release
// languages (English, Spanish, Italian, Portuguese, German). Stripped from the
// divergence comparison only, so connector-word differences do not split a group.
inline bool IsNameStopword(const std::wstring &rToken)
{
	static constexpr const wchar_t *kStopwords[] = {
		// English
		L"a", L"an", L"and", L"as", L"at", L"by", L"for", L"from", L"in", L"of",
		L"on", L"or", L"the", L"to", L"with",
		// Spanish
		L"al", L"con", L"de", L"del", L"el", L"en", L"la", L"las", L"los", L"para",
		L"por", L"su", L"un", L"una", L"unas", L"unos", L"y",
		// Italian
		L"col", L"dei", L"della", L"delle", L"di", L"e", L"gli", L"i", L"il", L"le",
		L"lo", L"per", L"uno",
		// Portuguese
		L"as", L"com", L"da", L"das", L"do", L"dos", L"em", L"na", L"nas", L"no",
		L"nos", L"o", L"os", L"ou", L"uma", L"um",
		// German
		L"auf", L"aus", L"bei", L"das", L"dem", L"den", L"der", L"des", L"die",
		L"ein", L"eine", L"einem", L"einen", L"einer", L"im", L"mit", L"oder",
		L"und", L"von", L"zu", L"zum", L"zur",
	};
	for (const wchar_t *pszStop : kStopwords)
		if (rToken == pszStop)
			return true;
	return false;
}

/**
 * @brief Sorted-unique "significant" tokens used to decide name divergence: the
 * canonical content tokens minus hash/UUID fragments, bare numbers, season/episode
 * markers and common multilingual stopwords, none of which identify a title.
 */
inline std::vector<std::wstring> SignificantNameTokens(const std::vector<std::wstring> &rTokens)
{
	std::vector<std::wstring> significant;
	for (const std::wstring &rToken : rTokens) {
		if (IsHashLikeNameToken(rToken) || IsAllDigitNameToken(rToken)
			|| IsEpisodeMarkerNameToken(rToken) || IsNameStopword(rToken))
		{
			continue;
		}
		significant.push_back(rToken);
	}
	return UniqueSortedTokens(significant);
}

/**
 * @brief True when two sorted-unique significant-token sets describe the same file:
 * they share a real core (>= 2 tokens) covering at least 60% of the smaller set.
 * Order-independent and tolerant of extra descriptive words (cast/crew, genre, an
 * episode subtitle), so naming variations of the same hash are not divergence; only
 * largely disjoint names (genuine mislabeling) stay distinct.
 */
inline bool IsSameNameContent(const std::vector<std::wstring> &rA, const std::vector<std::wstring> &rB)
{
	if (rA.empty() || rB.empty())
		return false;
	std::vector<std::wstring> shared;
	std::set_intersection(rA.begin(), rA.end(), rB.begin(), rB.end(), std::back_inserter(shared));
	if (shared.size() < 2)
		return false;
	const size_t uSmaller = rA.size() < rB.size() ? rA.size() : rB.size();
	return shared.size() * 5 >= uSmaller * 3; // >= 60% of the smaller significant set
}

inline bool IsMediaType(const EFileType eType)
{
	return eType == AUDIO_MPEG || eType == VIDEO_AVI || eType == VIDEO_MPG || eType == VIDEO_MP4
		|| eType == VIDEO_MKV || eType == VIDEO_OGG || eType == WM || eType == AUDIO_FLAC
		|| eType == AUDIO_WAV || eType == AUDIO_AAC;
}

inline bool IsVideoType(const EFileType eType)
{
	return eType == VIDEO_AVI || eType == VIDEO_MPG || eType == VIDEO_MP4 || eType == VIDEO_MKV
		|| eType == VIDEO_OGG || eType == WM;
}

inline bool IsAudioType(const EFileType eType)
{
	return eType == AUDIO_MPEG || eType == AUDIO_FLAC || eType == AUDIO_WAV || eType == AUDIO_AAC;
}

inline bool IsArchiveType(const EFileType eType)
{
	return eType == ARCHIVE_ZIP || eType == ARCHIVE_RAR || eType == ARCHIVE_ACE || eType == ARCHIVE_7Z
		|| eType == ARCHIVE_GZ;
}

/**
 * @brief Reports whether observed header evidence is compatible with the
 * extension classifier.
 */
inline bool HeaderMatchesExtensionType(const EFileType eHeaderType, const EFileType eExtensionType)
{
	return eHeaderType == eExtensionType;
}

inline bool ClaimedTypeConflicts(const std::wstring &rClaimedType, const EFileType eObservedType)
{
	const std::wstring claimed = ToLower(rClaimedType);
	if (claimed.empty() || eObservedType == FILETYPE_UNKNOWN)
		return false;
	if ((claimed.find(L"video") != std::wstring::npos || claimed.find(L"audio") != std::wstring::npos)
		&& (eObservedType == FILETYPE_EXECUTABLE || IsArchiveType(eObservedType)))
		return true;
	if ((claimed.find(L"program") != std::wstring::npos || claimed.find(L"pro") != std::wstring::npos)
		&& IsMediaType(eObservedType))
		return true;
	if ((claimed.find(L"archive") != std::wstring::npos || claimed.find(L"arc") != std::wstring::npos)
		&& IsMediaType(eObservedType))
		return true;
	return false;
}

inline bool ClaimedAsVideo(const std::wstring &rClaimedType)
{
	return ToLower(rClaimedType).find(L"video") != std::wstring::npos;
}

inline bool ClaimedAsAudio(const std::wstring &rClaimedType)
{
	return ToLower(rClaimedType).find(L"audio") != std::wstring::npos;
}

inline bool HasVideoSignal(const Evidence &rEvidence)
{
	return IsVideoType(rEvidence.extensionType) || IsVideoType(rEvidence.headerType) || ClaimedAsVideo(rEvidence.claimedType);
}

inline bool HasAudioSignal(const Evidence &rEvidence)
{
	return IsAudioType(rEvidence.extensionType) || IsAudioType(rEvidence.headerType) || ClaimedAsAudio(rEvidence.claimedType);
}

inline bool HasImplausibleMediaLength(const Evidence &rEvidence)
{
	if (!rEvidence.mediaLengthAvailable || rEvidence.mediaLengthSeconds == 0)
		return false;
	constexpr uint64_t kLargeVideoBytes = 50ull * 1024ull * 1024ull;
	if (HasVideoSignal(rEvidence))
		return (rEvidence.fileSizeBytes >= kLargeVideoBytes && rEvidence.mediaLengthSeconds < 60)
			|| rEvidence.mediaLengthSeconds > 12u * 60u * 60u;
	if (HasAudioSignal(rEvidence))
		return (rEvidence.fileSizeBytes >= 1024ull * 1024ull && rEvidence.mediaLengthSeconds < 2)
			|| rEvidence.mediaLengthSeconds > 24u * 60u * 60u;
	return false;
}

inline bool HasImplausibleMediaBitrate(const Evidence &rEvidence)
{
	if (!rEvidence.mediaBitrateAvailable || rEvidence.mediaBitrateKbps == 0)
		return false;
	if (HasVideoSignal(rEvidence))
		return rEvidence.mediaBitrateKbps < 40 || rEvidence.mediaBitrateKbps > 500000;
	if (HasAudioSignal(rEvidence))
		return rEvidence.mediaBitrateKbps < 16 || rEvidence.mediaBitrateKbps > 2000;
	return false;
}

inline Severity SeverityFromScore(const uint32_t uScore)
{
	if (uScore == 0)
		return Severity::None;
	if (uScore < 25)
		return Severity::Low;
	if (uScore < 50)
		return Severity::Medium;
	if (uScore < 75)
		return Severity::High;
	return Severity::Critical;
}

/**
	 * @brief Combines existing local evidence into a warning-only fake-file report.
	 */
inline Report Analyze(const Evidence &rEvidence, const RuleSet &rRules, const std::vector<std::wregex> *pCompiledRegexes)
{
	Report report;
	report.observedNames = rEvidence.names;
	report.extensionType = rEvidence.extensionType;
	report.headerType = rEvidence.headerType;
	report.claimedType = rEvidence.claimedType;
	report.pendingHeaderCheck = rEvidence.headerPending;

	std::vector<std::wstring> uniqueNames;
	struct NameContentGroup
	{
		std::vector<std::wstring> tokens; // sorted-unique content tokens
		std::wstring canonical;           // representative (most descriptive) name
	};
	std::vector<NameContentGroup> nameGroups;
	const FilenameTokenizationSeams::CanonicalNameOptions trustNameOptions{ true, false };
	for (const std::wstring &rName : rEvidence.names) {
		AddUnique(uniqueNames, rName);
		AddUnique(report.observedExtensions, GetExtension(rName));
		const FilenameTokenizationSeams::CanonicalName canonicalName = FilenameTokenizationSeams::BuildCanonicalName(rName, trustNameOptions);
		for (const std::wstring &rIgnoredToken : canonicalName.ignoredTokens)
			AddUnique(report.ignoredNameTokens, rIgnoredToken);
		if (canonicalName.hasUsableBaseName && !canonicalName.canonical.empty()) {
			AddUnique(report.canonicalNames, canonicalName.canonical);
			std::vector<std::wstring> significant = SignificantNameTokens(canonicalName.tokens);
			if (!significant.empty())
				nameGroups.push_back({ significant, canonicalName.canonical });
		}
	}

	// Collapse names that describe the same content (shared significant-token core,
	// order-independent) to a fixpoint, keeping the most descriptive name per group.
	for (bool bMerged = true; bMerged;) {
		bMerged = false;
		for (size_t i = 0; i < nameGroups.size() && !bMerged; ++i) {
			for (size_t j = i + 1; j < nameGroups.size(); ++j) {
				if (!IsSameNameContent(nameGroups[i].tokens, nameGroups[j].tokens))
					continue;
				if (nameGroups[j].tokens.size() > nameGroups[i].tokens.size())
					nameGroups[i] = nameGroups[j];
				nameGroups.erase(nameGroups.begin() + static_cast<std::ptrdiff_t>(j));
				bMerged = true;
				break;
			}
		}
	}

	if (nameGroups.size() >= 2) {
		for (const NameContentGroup &rGroup : nameGroups)
			report.nameDivergenceGroups.push_back(rGroup.canonical);
		AddReason(report, "multiple_names", nameGroups.size() >= 3 ? 25 : 15);
	}

	bool bBadNameSignal = false;
	bool bBadCommentSignal = false;
	for (const std::wstring &rName : rEvidence.names) {
		const std::wstring text = ToLower(rName);
		for (const std::wstring &rToken : rRules.tokens)
			bBadNameSignal = bBadNameSignal || ContainsToken(text, ToLower(rToken));
		if (pCompiledRegexes != nullptr && pCompiledRegexes->size() == rRules.regexes.size()) {
			for (const std::wregex &rRegex : *pCompiledRegexes)
				bBadNameSignal = bBadNameSignal || ContainsRegex(rName, rRegex);
		} else {
			for (const std::wstring &rRegex : rRules.regexes)
				bBadNameSignal = bBadNameSignal || ContainsRegex(rName, rRegex);
		}
	}
	for (const std::wstring &rComment : rEvidence.comments) {
		const std::wstring text = ToLower(rComment);
		for (const std::wstring &rToken : rRules.tokens)
			bBadCommentSignal = bBadCommentSignal || ContainsToken(text, ToLower(rToken));
		if (pCompiledRegexes != nullptr && pCompiledRegexes->size() == rRules.regexes.size()) {
			for (const std::wregex &rRegex : *pCompiledRegexes)
				bBadCommentSignal = bBadCommentSignal || ContainsRegex(rComment, rRegex);
		} else {
			for (const std::wstring &rRegex : rRules.regexes)
				bBadCommentSignal = bBadCommentSignal || ContainsRegex(rComment, rRegex);
		}
	}
	if (bBadNameSignal)
		AddReason(report, "bad_signal_name", 25);
	if (bBadCommentSignal)
		AddReason(report, "bad_signal_comment", 15);

	if (rEvidence.headerAvailable && rEvidence.headerType != FILETYPE_UNKNOWN && rEvidence.extensionType != FILETYPE_UNKNOWN
		&& !HeaderMatchesExtensionType(rEvidence.headerType, rEvidence.extensionType))
	{
		AddReason(report, "header_extension_mismatch", 45);
		if (rEvidence.headerType == FILETYPE_EXECUTABLE && IsMediaType(rEvidence.extensionType))
			AddReason(report, "executable_masquerade", 25);
		if (IsArchiveType(rEvidence.headerType) && IsMediaType(rEvidence.extensionType))
			AddReason(report, "archive_masquerade", 20);
	} else if (rEvidence.headerPending) {
		report.reasons.push_back("pending_header_check");
	}

	const EFileType eObservedType = rEvidence.headerType != FILETYPE_UNKNOWN ? rEvidence.headerType : rEvidence.extensionType;
	if (ClaimedTypeConflicts(rEvidence.claimedType, eObservedType))
		AddReason(report, "claimed_type_mismatch", 15);

	if (rEvidence.spamRating >= SEARCH_SPAM_THRESHOLD)
		AddReason(report, "spam_score", 25);
	else if (rEvidence.spamRating >= 30)
		AddReason(report, "spam_score", 15);
	if (rEvidence.consideredSpam)
		AddReason(report, "spam_status", 15);
	if (rEvidence.badRating)
		AddReason(report, "bad_rating", 20);
	if (rEvidence.fakeRating)
		AddReason(report, "fake_rating", 30);
	if (rEvidence.multipleAich)
		AddReason(report, "multiple_aich", 35);
	if (HasImplausibleMediaLength(rEvidence))
		AddReason(report, "implausible_media_length", 10);
	if (HasImplausibleMediaBitrate(rEvidence))
		AddReason(report, "implausible_media_bitrate", 10);

	// Borrowed (beba AntiFake): an audio/video file's name should reflect its own embedded
	// media tags. When meaningful tags exist and none appear in any observed filename,
	// contribute a soft (Low) fake signal. Self-consistency, distinct from multiple_names.
	if (HasAudioSignal(rEvidence) || HasVideoSignal(rEvidence)) {
		const std::wstring artist = ToLower(Trim(rEvidence.mediaArtist));
		const std::wstring album = ToLower(Trim(rEvidence.mediaAlbum));
		const std::wstring title = ToLower(Trim(rEvidence.mediaTitle));
		const bool bHasMeaningfulTag = artist.size() >= 4 || album.size() >= 4 || title.size() >= 4;
		if (bHasMeaningfulTag) {
			bool bTagInName = false;
			for (const std::wstring &rName : rEvidence.names) {
				const std::wstring lname = ToLower(rName);
				if ((artist.size() >= 4 && lname.find(artist) != std::wstring::npos)
					|| (album.size() >= 4 && lname.find(album) != std::wstring::npos)
					|| (title.size() >= 4 && lname.find(title) != std::wstring::npos)) {
					bTagInName = true;
					break;
				}
			}
			if (!bTagInName)
				AddReason(report, "name_media_tag_mismatch", 10);
		}
	}

	report.severity = SeverityFromScore(report.score);
	return report;
}

inline Report Analyze(const Evidence &rEvidence, const RuleSet &rRules)
{
	return Analyze(rEvidence, rRules, nullptr);
}
}

#define EMULEBB_TEST_HAVE_FAKE_FILE_DETECTOR_SEAMS 1
