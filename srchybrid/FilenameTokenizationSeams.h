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
	bool hasUsableBaseName = false;
};

inline std::wstring ToLower(std::wstring value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](const wchar_t ch) {
		return static_cast<wchar_t>(std::towlower(ch));
	});
	return value;
}

inline bool IsIgnoredReleaseNoiseToken(const std::wstring &rToken)
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

inline CanonicalName BuildCanonicalName(std::wstring name)
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
	std::vector<std::wstring> keptTokens;
	std::wstring token;
	for (const wchar_t ch : name) {
		if (std::iswalnum(ch) != 0) {
			token += ch;
			continue;
		}
		if (!token.empty()) {
			if (IsIgnoredReleaseNoiseToken(token))
				AddUnique(result.ignoredTokens, token);
			else
				keptTokens.push_back(token);
			token.clear();
		}
	}
	if (!token.empty()) {
		if (IsIgnoredReleaseNoiseToken(token))
			AddUnique(result.ignoredTokens, token);
		else
			keptTokens.push_back(token);
	}

	result.hasUsableBaseName = !keptTokens.empty();
	result.canonical = JoinTokens(keptTokens);
	if (!extension.empty()) {
		if (!result.canonical.empty())
			result.canonical += L" | ";
		result.canonical += L"ext:";
		result.canonical += extension;
	}
	return result;
}
}
