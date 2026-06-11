#pragma once

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace FilenameTokenizationSeams
{
/**
 * @brief Canonical filename identity used for fake-file divergence and
 * source-majority rename voting.
 */
struct CanonicalName
{
	std::wstring canonical;
	std::vector<std::wstring> ignoredTokens;
	std::vector<std::wstring> tokens; // kept content tokens (for order-independent set comparison)
	bool hasUsableBaseName = false;
};

/**
 * @brief Selects how aggressively release metadata participates in the
 * canonical name used by a specific caller.
 */
struct CanonicalNameOptions
{
	bool stripBroadReleaseMetadata = false;
	bool includeExtensionInCanonical = true;
};

inline std::wstring ToLower(std::wstring value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](const wchar_t ch) {
		return static_cast<wchar_t>(std::towlower(ch));
	});
	return value;
}

inline bool IsBaseReleaseNoiseToken(const std::wstring &rToken)
{
	static constexpr const wchar_t *kIgnoredTokens[] = {
		L"aac", L"ac3", L"avc", L"bdrip", L"bluray", L"cam", L"divx", L"dvdrip",
		L"extended", L"flac", L"h264", L"h265", L"hdr", L"hdrip", L"hevc",
		L"dl", L"limited", L"mp3", L"proper", L"repack", L"rerip", L"sdr", L"tc", L"ts",
		L"web", L"webdl", L"webrip", L"x264", L"x265", L"xvid",
	};
	for (const wchar_t *pszIgnoredToken : kIgnoredTokens) {
		if (rToken == pszIgnoredToken)
			return true;
	}
	if (rToken == L"4k" || rToken == L"r5")
		return true;
	if (rToken.size() == 4 && rToken[0] == L'7' && rToken[1] == L'2' && rToken[2] == L'0' && rToken[3] == L'p')
		return true;
	if (rToken.size() == 5
		&& ((rToken[0] == L'1' && rToken[1] == L'0' && rToken[2] == L'8' && rToken[3] == L'0')
			|| (rToken[0] == L'2' && rToken[1] == L'1' && rToken[2] == L'6' && rToken[3] == L'0'))
		&& rToken[4] == L'p')
	{
		return true;
	}
	return false;
}

/**
 * @brief Returns high-confidence release metadata tokens inspired by ARR
 * release parsing, used only by trust evidence normalization.
 */
inline bool IsBroadReleaseMetadataToken(const std::wstring &rToken)
{
	static constexpr const wchar_t *kIgnoredTokens[] = {
		L"2.0", L"2.1", L"5.1", L"6.1", L"7.1", L"atmos", L"bgaudio", L"brdisk",
		L"brrip", L"castellano", L"dd", L"ddp", L"director", L"dovi", L"dts", L"dual",
		L"dub", L"dubbed", L"eac3", L"eng", L"english", L"espanol",
		L"fhd", L"fra", L"fre", L"french", L"ger", L"german", L"hdtv", L"internal",
		L"ita", L"italian", L"jap", L"jpn", L"kor", L"limited", L"multi", L"multisub",
		L"multisubs", L"proper", L"real", L"remastered", L"remux", L"repack", L"rerip",
		L"rus", L"russian", L"screener", L"sdr", L"spa", L"spanish", L"sub", L"subbed",
		L"subs", L"truefrench", L"truehd", L"uhd", L"uncut", L"unrated", L"vff", L"vfq",
		L"webhd", L"webrip", L"webmux",
	};
	for (const wchar_t *pszIgnoredToken : kIgnoredTokens) {
		if (rToken == pszIgnoredToken)
			return true;
	}
	if (rToken == L"10bit" || rToken == L"8bit" || rToken == L"5ch" || rToken == L"6ch")
		return true;
	return false;
}

inline bool IsYearLikeToken(const std::wstring &rToken)
{
	if (rToken.size() != 4)
		return false;
	for (const wchar_t ch : rToken) {
		if (std::iswdigit(ch) == 0)
			return false;
	}
	return (rToken[0] == L'1' && (rToken[1] == L'8' || rToken[1] == L'9'))
		|| (rToken[0] == L'2' && rToken[1] == L'0');
}

inline bool IsIgnoredReleaseNoiseToken(const std::wstring &rToken, const bool bBroadReleaseMetadata)
{
	return IsBaseReleaseNoiseToken(rToken) || (bBroadReleaseMetadata && IsBroadReleaseMetadataToken(rToken));
}

inline void AddUnique(std::vector<std::wstring> &rValues, const std::wstring &rValue)
{
	if (rValue.empty())
		return;
	for (const std::wstring &rExisting : rValues) {
		if (rExisting == rValue)
			return;
	}
	rValues.push_back(rValue);
}

inline std::wstring JoinTokens(const std::vector<std::wstring> &rTokens)
{
	std::wstring result;
	for (const std::wstring &rToken : rTokens) {
		if (!result.empty())
			result += L' ';
		result += rToken;
	}
	return result;
}

inline bool IsDefaultFallbackBaseName(const std::vector<std::wstring> &rTokens)
{
	return rTokens.size() == 1 && rTokens[0] == L"download";
}

inline std::vector<std::wstring> TokenizeName(const std::wstring &rName)
{
	std::vector<std::wstring> tokens;
	std::wstring token;
	for (size_t uIndex = 0; uIndex < rName.size(); ++uIndex) {
		const wchar_t ch = rName[uIndex];
		if (std::iswalnum(ch) != 0) {
			token += ch;
			continue;
		}
		if (ch == L'.' && !token.empty() && uIndex + 1 < rName.size()
			&& std::all_of(token.begin(), token.end(), [](const wchar_t tokenCh) { return std::iswdigit(tokenCh) != 0; })
			&& std::iswdigit(rName[uIndex + 1]) != 0)
		{
			token += ch;
			continue;
		}
		if (!token.empty()) {
			tokens.push_back(token);
			token.clear();
		}
	}
	if (!token.empty())
		tokens.push_back(token);
	return tokens;
}

inline bool HasBroadReleaseMetadataSignal(const std::wstring &rName)
{
	for (const std::wstring &rToken : TokenizeName(ToLower(rName))) {
		if (IsIgnoredReleaseNoiseToken(rToken, true) || IsYearLikeToken(rToken))
			return true;
	}
	return false;
}

inline bool IsReleaseGroupCandidate(const std::wstring &rValue)
{
	std::vector<std::wstring> tokens = TokenizeName(ToLower(rValue));
	if (tokens.empty() || tokens.size() > 3)
		return false;

	size_t uLength = 0;
	bool bHasAlpha = false;
	for (const std::wstring &rToken : tokens) {
		uLength += rToken.size();
		for (const wchar_t ch : rToken) {
			if (std::iswalpha(ch) != 0)
				bHasAlpha = true;
		}
	}
	return bHasAlpha && uLength >= 2 && uLength <= 32;
}

inline void AddTokenizedIgnoredTokens(std::vector<std::wstring> &rIgnoredTokens, const std::wstring &rValue)
{
	for (const std::wstring &rToken : TokenizeName(ToLower(rValue)))
		AddUnique(rIgnoredTokens, rToken);
}

inline void StripLeadingBracketedReleaseGroup(std::wstring &rName, std::vector<std::wstring> &rIgnoredTokens)
{
	while (!rName.empty() && (rName.front() == L'[' || rName.front() == L'(')) {
		const wchar_t chClose = rName.front() == L'[' ? L']' : L')';
		const size_t uClose = rName.find(chClose);
		if (uClose == std::wstring::npos || uClose + 1 >= rName.size())
			return;
		const std::wstring group = rName.substr(1, uClose - 1);
		if (!IsReleaseGroupCandidate(group))
			return;
		AddTokenizedIgnoredTokens(rIgnoredTokens, group);
		rName = rName.substr(uClose + 1);
		while (!rName.empty() && std::iswalnum(rName.front()) == 0)
			rName.erase(rName.begin());
	}
}

inline void StripTrailingReleaseGroup(std::wstring &rName, std::vector<std::wstring> &rIgnoredTokens)
{
	const size_t uDash = rName.find_last_of(L'-');
	if (uDash == std::wstring::npos || uDash == 0 || uDash + 1 >= rName.size())
		return;

	const std::wstring prefix = rName.substr(0, uDash);
	const std::wstring group = rName.substr(uDash + 1);
	if (!HasBroadReleaseMetadataSignal(prefix) || !IsReleaseGroupCandidate(group))
		return;

	AddTokenizedIgnoredTokens(rIgnoredTokens, group);
	rName = prefix;
}

/**
 * @brief Builds a comparable filename identity while preserving ignored tokens
 * as evidence for diagnostics and REST/UI reporting.
 */
inline CanonicalName BuildCanonicalName(std::wstring name, const CanonicalNameOptions &rOptions)
{
	name = ToLower(name);
	const size_t uSlash = name.find_last_of(L"\\/");
	if (uSlash != std::wstring::npos)
		name = name.substr(uSlash + 1);
	if (!name.empty() && name.front() == L'.')
		return CanonicalName();

	std::wstring extension;
	const size_t uDot = name.find_last_of(L'.');
	if (uDot != std::wstring::npos && uDot > 0 && uDot + 1 < name.size()) {
		extension = name.substr(uDot + 1);
		name = name.substr(0, uDot);
	}

	CanonicalName result;
	if (rOptions.stripBroadReleaseMetadata) {
		StripLeadingBracketedReleaseGroup(name, result.ignoredTokens);
		StripTrailingReleaseGroup(name, result.ignoredTokens);
	}

	std::vector<std::wstring> keptTokens;
	const std::vector<std::wstring> tokens = TokenizeName(name);
	bool bHasBroadMetadata = false;
	if (rOptions.stripBroadReleaseMetadata) {
		for (const std::wstring &rToken : tokens) {
			if (IsIgnoredReleaseNoiseToken(rToken, true)) {
				bHasBroadMetadata = true;
				break;
			}
		}
	}
	for (const std::wstring &rToken : tokens) {
		if (IsIgnoredReleaseNoiseToken(rToken, rOptions.stripBroadReleaseMetadata)
			|| (rOptions.stripBroadReleaseMetadata && bHasBroadMetadata && IsYearLikeToken(rToken)))
		{
			AddUnique(result.ignoredTokens, rToken);
		}
		else
			keptTokens.push_back(rToken);
	}

	result.hasUsableBaseName = !keptTokens.empty() && !IsDefaultFallbackBaseName(keptTokens);
	if (!result.hasUsableBaseName)
		return result;
	result.tokens = keptTokens;
	result.canonical = JoinTokens(keptTokens);
	if (rOptions.includeExtensionInCanonical && !extension.empty()) {
		if (!result.canonical.empty())
			result.canonical += L" | ";
		result.canonical += L"ext:";
		result.canonical += extension;
	}
	return result;
}

inline CanonicalName BuildCanonicalName(std::wstring name)
{
	return BuildCanonicalName(name, CanonicalNameOptions());
}
}
