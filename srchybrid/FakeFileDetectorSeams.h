#pragma once

#include "FileTypeClassifierSeams.h"
#include "FilenameTokenizationSeams.h"
#include "Opcodes.h"
#include "RegexMatchSeams.h"

#include <algorithm>
#include <cstdint>
#include <cwctype>
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

inline bool IsMediaType(const EFileType eType)
{
	return eType == AUDIO_MPEG || eType == VIDEO_AVI || eType == VIDEO_MPG || eType == VIDEO_MP4
		|| eType == VIDEO_MKV || eType == VIDEO_OGG || eType == WM || eType == AUDIO_FLAC
		|| eType == AUDIO_WAV || eType == AUDIO_AAC;
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
	std::vector<std::wstring> uniqueCanonicalNames;
	for (const std::wstring &rName : rEvidence.names) {
		AddUnique(uniqueNames, rName);
		AddUnique(report.observedExtensions, GetExtension(rName));
		const FilenameTokenizationSeams::CanonicalName canonicalName = FilenameTokenizationSeams::BuildCanonicalName(rName);
		for (const std::wstring &rIgnoredToken : canonicalName.ignoredTokens)
			AddUnique(report.ignoredNameTokens, rIgnoredToken);
		if (canonicalName.hasUsableBaseName && !canonicalName.canonical.empty()) {
			AddUnique(uniqueCanonicalNames, canonicalName.canonical);
			AddUnique(report.canonicalNames, canonicalName.canonical);
		}
	}
	report.nameDivergenceGroups = uniqueCanonicalNames;
	if (uniqueCanonicalNames.size() >= 3)
		AddReason(report, "multiple_names", 25);
	else if (uniqueCanonicalNames.size() == 2)
		AddReason(report, "multiple_names", 15);

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

	report.severity = SeverityFromScore(report.score);
	return report;
}

inline Report Analyze(const Evidence &rEvidence, const RuleSet &rRules)
{
	return Analyze(rEvidence, rRules, nullptr);
}
}

#define EMULE_TEST_HAVE_FAKE_FILE_DETECTOR_SEAMS 1
