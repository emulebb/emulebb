#pragma once

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "WebServerJsonSeams.h"

/**
 * @brief Pure helper surface for the eMule BB *arr compatibility bridge.
 */
namespace WebServerArrCompatSeams
{
/**
 * @brief Identifies the coarse media family used by the Torznab bridge.
 */
enum class ETorznabFamily
{
	Unknown,
	Any,
	Movie,
	Tv,
	Audio,
	Book,
	Other
};

/**
 * @brief Carries normalized Torznab query input before native search dispatch.
 */
struct STorznabRequest
{
	std::string strType;
	std::string strQuery;
	std::string strSeason;
	std::string strEpisode;
	std::string strYear;
	std::string strCategories;
	ETorznabFamily eFamily;

	STorznabRequest()
		: eFamily(ETorznabFamily::Any)
	{
	}
};

inline std::string TrimAscii(const std::string &rValue)
{
	size_t uBegin = 0;
	while (uBegin < rValue.size() && std::isspace(static_cast<unsigned char>(rValue[uBegin])) != 0)
		++uBegin;

	size_t uEnd = rValue.size();
	while (uEnd > uBegin && std::isspace(static_cast<unsigned char>(rValue[uEnd - 1])) != 0)
		--uEnd;

	return rValue.substr(uBegin, uEnd - uBegin);
}

inline std::string NormalizeSpace(const std::string &rValue)
{
	std::string result;
	result.reserve(rValue.size());
	bool bPreviousSpace = true;
	for (const char ch : rValue) {
		if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
			if (!bPreviousSpace)
				result.push_back(' ');
			bPreviousSpace = true;
		} else {
			result.push_back(ch);
			bPreviousSpace = false;
		}
	}
	if (!result.empty() && result[result.size() - 1] == ' ')
		result.erase(result.size() - 1);
	return result;
}

/**
 * @brief Escapes text for XML element and attribute content.
 */
inline std::string XmlEscape(const std::string &rValue)
{
	std::string escaped;
	escaped.reserve(rValue.size());
	for (const char ch : rValue) {
		switch (ch) {
		case '&':
			escaped += "&amp;";
			break;
		case '<':
			escaped += "&lt;";
			break;
		case '>':
			escaped += "&gt;";
			break;
		case '"':
			escaped += "&quot;";
			break;
		case '\'':
			escaped += "&apos;";
			break;
		default:
			escaped.push_back(ch);
			break;
		}
	}
	return escaped;
}

/**
 * @brief Reports whether one request target belongs to the Prowlarr Torznab bridge.
 */
inline bool IsArrCompatRequestTarget(const std::string &rRequestTarget)
{
	const std::string strPathLower(WebServerJsonSeams::ToLowerAscii(WebServerJsonSeams::GetRequestPath(rRequestTarget)));
	return strPathLower == "/indexer/emulebb/api" || strPathLower == "/indexer/emulebb/api/";
}

inline std::vector<std::string> SplitCommaList(const std::string &rValue)
{
	std::vector<std::string> tokens;
	size_t uPos = 0;
	while (uPos <= rValue.size()) {
		const std::string::size_type uComma = rValue.find(',', uPos);
		const std::string token = TrimAscii(rValue.substr(
			uPos,
			uComma == std::string::npos ? std::string::npos : (uComma - uPos)));
		if (!token.empty())
			tokens.push_back(token);
		if (uComma == std::string::npos)
			break;
		uPos = uComma + 1;
	}
	return tokens;
}

inline bool IsTorznabCategoryInRange(const int iCategory, const int iBase)
{
	return iCategory == iBase || (iCategory > iBase && iCategory < iBase + 1000);
}

inline char HexDigit(const unsigned char value)
{
	return static_cast<char>(value < 10 ? ('0' + value) : ('A' + (value - 10)));
}

/**
 * @brief URL-encodes UTF-8 query parameter content for generated magnet links.
 */
inline std::string UrlEncodeUtf8(const std::string &rValue)
{
	std::string encoded;
	encoded.reserve(rValue.size());
	for (const unsigned char ch : rValue) {
		if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
			encoded.push_back(static_cast<char>(ch));
		} else {
			encoded.push_back('%');
			encoded.push_back(HexDigit(static_cast<unsigned char>(ch >> 4)));
			encoded.push_back(HexDigit(static_cast<unsigned char>(ch & 0x0F)));
		}
	}
	return encoded;
}

/**
 * @brief Maps Torznab categories onto the native eMule search file families.
 */
inline ETorznabFamily ResolveFamily(const std::string &rType, const std::string &rCategories)
{
	const std::string strType(WebServerJsonSeams::ToLowerAscii(rType));
	if (strType == "movie")
		return ETorznabFamily::Movie;
	if (strType == "tvsearch")
		return ETorznabFamily::Tv;

	bool bSawKnown = false;
	ETorznabFamily eFamily = ETorznabFamily::Unknown;
	for (const std::string &rToken : SplitCommaList(rCategories)) {
		if (!WebServerJsonSeams::IsValidUnsignedDecimal(rToken))
			return ETorznabFamily::Unknown;
		const int iCategory = std::atoi(rToken.c_str());
		ETorznabFamily eTokenFamily = ETorznabFamily::Unknown;
		if (IsTorznabCategoryInRange(iCategory, 2000)) {
			bSawKnown = true;
			eTokenFamily = ETorznabFamily::Movie;
		} else if (IsTorznabCategoryInRange(iCategory, 5000)) {
			bSawKnown = true;
			eTokenFamily = ETorznabFamily::Tv;
		} else if (IsTorznabCategoryInRange(iCategory, 3000)) {
			bSawKnown = true;
			eTokenFamily = ETorznabFamily::Audio;
		} else if (IsTorznabCategoryInRange(iCategory, 7000)) {
			bSawKnown = true;
			eTokenFamily = ETorznabFamily::Book;
		} else if (IsTorznabCategoryInRange(iCategory, 8000) || IsTorznabCategoryInRange(iCategory, 4000)) {
			bSawKnown = true;
			eTokenFamily = ETorznabFamily::Other;
		} else {
			return ETorznabFamily::Unknown;
		}
		if (eFamily == ETorznabFamily::Unknown)
			eFamily = eTokenFamily;
		else if (eFamily != eTokenFamily)
			eFamily = ETorznabFamily::Any;
	}

	if (!bSawKnown)
		return ETorznabFamily::Any;
	return eFamily;
}

/**
 * @brief Parses and normalizes the Torznab query parameters accepted by Prowlarr.
 */
inline bool TryParseTorznabRequest(const std::string &rRequestTarget, STorznabRequest &rRequest, std::string &rErrorMessage)
{
	std::map<std::string, std::string> query;
	if (!WebServerJsonSeams::TryParseQueryString(rRequestTarget, query, rErrorMessage))
		return false;

	std::map<std::string, std::string> normalized;
	for (const auto &rPair : query) {
		const std::string strName(WebServerJsonSeams::ToLowerAscii(rPair.first));
		if (normalized.find(strName) != normalized.end()) {
			rErrorMessage = "duplicate query parameter: " + strName;
			return false;
		}
		normalized[strName] = rPair.second;
	}

	const auto typeIt = normalized.find("t");
	rRequest.strType = typeIt == normalized.end() ? "search" : WebServerJsonSeams::ToLowerAscii(TrimAscii(typeIt->second));
	if (rRequest.strType.empty())
		rRequest.strType = "search";
	if (rRequest.strType != "caps" && rRequest.strType != "search" && rRequest.strType != "tvsearch" && rRequest.strType != "movie") {
		rErrorMessage = "unsupported Torznab request type";
		return false;
	}

	const auto queryIt = normalized.find("q");
	const auto seasonIt = normalized.find("season");
	const auto episodeIt = normalized.find("ep");
	const auto yearIt = normalized.find("year");
	const auto catIt = normalized.find("cat");
	rRequest.strQuery = queryIt == normalized.end() ? std::string() : NormalizeSpace(TrimAscii(queryIt->second));
	rRequest.strSeason = seasonIt == normalized.end() ? std::string() : TrimAscii(seasonIt->second);
	rRequest.strEpisode = episodeIt == normalized.end() ? std::string() : TrimAscii(episodeIt->second);
	rRequest.strYear = yearIt == normalized.end() ? std::string() : TrimAscii(yearIt->second);
	rRequest.strCategories = catIt == normalized.end() ? std::string() : TrimAscii(catIt->second);
	if ((!rRequest.strSeason.empty() && !WebServerJsonSeams::IsValidUnsignedDecimal(rRequest.strSeason))
		|| (!rRequest.strEpisode.empty() && !WebServerJsonSeams::IsValidUnsignedDecimal(rRequest.strEpisode))
		|| (!rRequest.strYear.empty() && !WebServerJsonSeams::IsValidUnsignedDecimal(rRequest.strYear)))
	{
		rErrorMessage = "season, ep, and year must be unsigned decimal values";
		return false;
	}
	rRequest.eFamily = ResolveFamily(rRequest.strType, rRequest.strCategories);
	return true;
}

/**
 * @brief Builds a deterministic fake BTIH from a 32-character eD2K hash.
 */
inline std::string BuildFakeBtihHash(const std::string &rEd2kHash)
{
	std::string hash(WebServerJsonSeams::ToLowerAscii(rEd2kHash));
	if (hash.size() != 32)
		return std::string();
	for (const char ch : hash) {
		if (!std::isxdigit(static_cast<unsigned char>(ch)))
			return std::string();
	}
	return hash + "00000000";
}

/**
 * @brief Builds the qBittorrent-style magnet link Prowlarr can hand to *arr.
 */
inline std::string BuildMagnetFromEd2k(const std::string &rEd2kHash, const std::string &rName, const uint64_t ullSize)
{
	const std::string strBtih(BuildFakeBtihHash(rEd2kHash));
	if (strBtih.empty())
		return std::string();
	std::ostringstream link;
	link << "magnet:?xt=urn:btih:" << strBtih << "&dn=" << UrlEncodeUtf8(rName) << "&xl=" << ullSize;
	return link.str();
}

inline std::string GetLowerExtension(const std::string &rName)
{
	const std::string::size_type uDot = rName.find_last_of('.');
	if (uDot == std::string::npos || uDot + 1 >= rName.size())
		return std::string();
	return WebServerJsonSeams::ToLowerAscii(rName.substr(uDot + 1));
}

inline bool IsExtensionInList(const std::string &rExtension, const char *const *ppszValues, const size_t uCount)
{
	for (size_t i = 0; i < uCount; ++i) {
		if (rExtension == ppszValues[i])
			return true;
	}
	return false;
}

/**
 * @brief Applies the minimal Torznab category filter to one native search result.
 */
inline bool DoesResultMatchFamily(const ETorznabFamily eFamily, const std::string &rName, const uint64_t ullSize)
{
	if (eFamily == ETorznabFamily::Unknown)
		return false;
	if (eFamily == ETorznabFamily::Any || eFamily == ETorznabFamily::Other)
		return true;

	static const char *const s_video[] = {"avi", "mkv", "mp4", "m4v", "mov", "mpg", "mpeg", "ts", "wmv", "webm", "iso"};
	static const char *const s_audio[] = {"mp3", "flac", "m4a", "aac", "ogg", "opus", "wav", "wma"};
	static const char *const s_book[] = {"epub", "mobi", "azw3", "pdf", "cbz", "cbr", "txt", "rtf", "doc", "docx", "zip", "rar", "7z"};
	const std::string strExtension(GetLowerExtension(rName));

	if (eFamily == ETorznabFamily::Movie || eFamily == ETorznabFamily::Tv)
		return IsExtensionInList(strExtension, s_video, sizeof(s_video) / sizeof(s_video[0])) || (strExtension.empty() && ullSize >= 100ULL * 1024ULL * 1024ULL);
	if (eFamily == ETorznabFamily::Audio)
		return IsExtensionInList(strExtension, s_audio, sizeof(s_audio) / sizeof(s_audio[0]));
	if (eFamily == ETorznabFamily::Book)
		return IsExtensionInList(strExtension, s_book, sizeof(s_book) / sizeof(s_book[0]));
	return false;
}

/**
 * @brief Converts a Torznab media family to the native search type token.
 */
inline const char *GetNativeSearchType(const ETorznabFamily eFamily)
{
	switch (eFamily) {
	case ETorznabFamily::Movie:
	case ETorznabFamily::Tv:
		return "video";
	case ETorznabFamily::Audio:
		return "audio";
	case ETorznabFamily::Book:
		return "document";
	case ETorznabFamily::Other:
	case ETorznabFamily::Any:
	case ETorznabFamily::Unknown:
	default:
		return "any";
	}
}

/**
 * @brief Builds expanded native query strings for common Prowlarr media requests.
 */
inline std::vector<std::string> BuildNativeQueries(const STorznabRequest &rRequest)
{
	std::vector<std::string> queries;
	const std::string strType(WebServerJsonSeams::ToLowerAscii(rRequest.strType));
	if (rRequest.strQuery.empty()) {
		queries.push_back("linux");
		return queries;
	}

	if (strType == "tvsearch") {
		if (!rRequest.strSeason.empty() && !rRequest.strEpisode.empty()) {
			std::ostringstream sxxeyy;
			sxxeyy << rRequest.strQuery << " S";
			if (rRequest.strSeason.size() == 1)
				sxxeyy << '0';
			sxxeyy << rRequest.strSeason << 'E';
			if (rRequest.strEpisode.size() == 1)
				sxxeyy << '0';
			sxxeyy << rRequest.strEpisode;
			queries.push_back(NormalizeSpace(sxxeyy.str()));

			std::ostringstream xFormat;
			xFormat << rRequest.strQuery << ' ' << rRequest.strSeason << 'x';
			if (rRequest.strEpisode.size() == 1)
				xFormat << '0';
			xFormat << rRequest.strEpisode;
			queries.push_back(NormalizeSpace(xFormat.str()));
		} else if (!rRequest.strSeason.empty()) {
			std::ostringstream season;
			season << rRequest.strQuery << " S";
			if (rRequest.strSeason.size() == 1)
				season << '0';
			season << rRequest.strSeason;
			queries.push_back(NormalizeSpace(season.str()));
		}
	}

	if (strType == "movie" && !rRequest.strYear.empty()) {
		queries.push_back(NormalizeSpace(rRequest.strQuery + " " + rRequest.strYear));
		queries.push_back(rRequest.strQuery);
	}

	queries.push_back(rRequest.strQuery);
	std::sort(queries.begin(), queries.end());
	queries.erase(std::unique(queries.begin(), queries.end()), queries.end());
	return queries;
}

/**
 * @brief Builds the normalized cache key for one Torznab search request.
 */
inline std::string BuildCacheKey(const STorznabRequest &rRequest)
{
	std::ostringstream key;
	key << WebServerJsonSeams::ToLowerAscii(rRequest.strType)
		<< "|q=" << WebServerJsonSeams::ToLowerAscii(rRequest.strQuery)
		<< "|cat=" << WebServerJsonSeams::ToLowerAscii(rRequest.strCategories)
		<< "|season=" << WebServerJsonSeams::ToLowerAscii(rRequest.strSeason)
		<< "|ep=" << WebServerJsonSeams::ToLowerAscii(rRequest.strEpisode)
		<< "|year=" << WebServerJsonSeams::ToLowerAscii(rRequest.strYear)
		<< "|family=" << static_cast<int>(rRequest.eFamily)
		<< "|type=" << GetNativeSearchType(rRequest.eFamily);
	return key.str();
}
}
