#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "WebServerJsonSeams.h"

/**
 * @brief Pure helper surface for the eMuleBB *arr compatibility bridge.
 */
namespace WebServerArrCompatSeams
{
static const size_t kMaxTorznabQueryLength = WebServerJsonSeams::kMaxSearchQueryLength;
static const unsigned kMaxTorznabSeason = 9999;
static const unsigned kMaxTorznabEpisode = 9999;
static const unsigned kMaxTorznabYear = 9999;
static const int kTorznabParseErrorHttpStatus = 400;
static const int kTorznabBusyHttpStatus = 503;
static const unsigned kDefaultTorznabLimit = 100;
static const unsigned kMaxTorznabLimit = 100;
static const unsigned kMaxTorznabOffset = 1000000;
static const unsigned long long kTorznabDefaultSearchTimeoutMs = 12ULL * 1000ULL;
static const unsigned long long kTorznabMediaSearchTimeoutMs = 45ULL * 1000ULL;
static const char kTorznabXmlContentTypeHeader[] = "Content-Type: application/xml; charset=utf-8\r\n";
static const char kTorznabTorrentContentMimeType[] = "application/x-bittorrent";

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
	unsigned uOffset;
	unsigned uLimit;
	ETorznabFamily eFamily;

	STorznabRequest()
		: uOffset(0)
		, uLimit(kDefaultTorznabLimit)
		, eFamily(ETorznabFamily::Any)
	{
	}
};

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
	static const char *const pszEndpoint = "/indexer/emulebb/api";
	return strPathLower == pszEndpoint
		|| strPathLower == std::string(pszEndpoint) + "/"
		|| strPathLower.rfind(std::string(pszEndpoint) + "%", 0) == 0;
}

/**
 * @brief Validates the Torznab compatibility request path before auth and
 * query parsing.
 */
inline bool TryGetArrCompatRequestPathLower(
	const std::string &rRequestTarget,
	std::string &rPathLower,
	std::string &rErrorMessage)
{
	rPathLower.clear();
	if (!WebServerJsonSeams::TryValidateRequestPathEscapes(rRequestTarget, rErrorMessage))
		return false;

	rPathLower = WebServerJsonSeams::ToLowerAscii(WebServerJsonSeams::GetRequestPath(rRequestTarget));
	return true;
}

inline std::vector<std::string> SplitCommaList(const std::string &rValue)
{
	std::vector<std::string> tokens;
	size_t uPos = 0;
	while (uPos <= rValue.size()) {
		const std::string::size_type uComma = rValue.find(',', uPos);
		const std::string token = WebServerJsonSeams::TrimAsciiWhitespace(rValue.substr(
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

/**
 * @brief Parses a Torznab comma-separated category list with strict unsigned
 * decimal tokens.
 */
inline bool TryParseTorznabCategoryList(const std::string &rCategories, std::vector<int> &rCategoriesOut, std::string &rErrorMessage)
{
	rCategoriesOut.clear();
	if (rCategories.empty())
		return true;

	size_t uPos = 0;
	while (uPos <= rCategories.size()) {
		const std::string::size_type uComma = rCategories.find(',', uPos);
		const std::string token = rCategories.substr(
			uPos,
			uComma == std::string::npos ? std::string::npos : (uComma - uPos));
		uint64_t ullCategory = 0;
		if (token.empty()
			|| !WebServerJsonSeams::TryParseUnsignedDecimalValue(token, ullCategory)
			|| ullCategory > static_cast<uint64_t>((std::numeric_limits<int>::max)()))
		{
			rErrorMessage = "cat must contain unsigned decimal Torznab category IDs";
			rCategoriesOut.clear();
			return false;
		}
		rCategoriesOut.push_back(static_cast<int>(ullCategory));
		if (uComma == std::string::npos)
			break;
		uPos = uComma + 1;
	}
	return true;
}

inline bool TryParseBoundedUnsigned(const std::string &rValue, const unsigned uMaxValue, unsigned &ruValue)
{
	uint64_t ullValue = 0;
	if (!WebServerJsonSeams::TryParseUnsignedDecimalValue(rValue, ullValue))
		return false;
	if (ullValue > uMaxValue)
		return false;
	ruValue = static_cast<unsigned>(ullValue);
	return true;
}

/**
 * @brief Maps Torznab categories onto the native eMule search file families.
 */
inline bool TryResolveFamily(const std::string &rType, const std::string &rCategories, ETorznabFamily &reFamily, std::string &rErrorMessage)
{
	reFamily = ETorznabFamily::Any;
	std::vector<int> categories;
	if (!TryParseTorznabCategoryList(rCategories, categories, rErrorMessage))
		return false;

	const std::string strType(WebServerJsonSeams::ToLowerAscii(rType));
	if (strType == "movie") {
		reFamily = ETorznabFamily::Movie;
		return true;
	}
	if (strType == "tvsearch") {
		reFamily = ETorznabFamily::Tv;
		return true;
	}

	bool bSawKnown = false;
	ETorznabFamily eFamily = ETorznabFamily::Unknown;
	for (const int iCategory : categories) {
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
			reFamily = ETorznabFamily::Unknown;
			return true;
		}
		if (eFamily == ETorznabFamily::Unknown)
			eFamily = eTokenFamily;
		else if (eFamily != eTokenFamily)
			eFamily = ETorznabFamily::Any;
	}

	reFamily = bSawKnown ? eFamily : ETorznabFamily::Any;
	return true;
}

/**
 * @brief Parses Torznab query parameters with shared lower-case key normalization.
 */
inline bool TryParseTorznabQueryParameters(
	const std::string &rRequestTarget,
	std::map<std::string, std::string> &rNormalized,
	std::string &rErrorMessage)
{
	rNormalized.clear();
	std::map<std::string, std::string> query;
	if (!WebServerJsonSeams::TryParseQueryString(rRequestTarget, query, rErrorMessage))
		return false;

	for (const auto &rPair : query) {
		const std::string strName(WebServerJsonSeams::ToLowerAscii(rPair.first));
		if (rNormalized.find(strName) != rNormalized.end()) {
			rErrorMessage = "duplicate query parameter: " + strName;
			rNormalized.clear();
			return false;
		}
		rNormalized[strName] = rPair.second;
	}
	return true;
}

/**
 * @brief Parses and normalizes the Torznab query parameters accepted by Prowlarr.
 */
inline bool TryParseTorznabRequest(const std::string &rRequestTarget, STorznabRequest &rRequest, std::string &rErrorMessage)
{
	rRequest = STorznabRequest();
	STorznabRequest parsed;
	std::map<std::string, std::string> normalized;
	if (!TryParseTorznabQueryParameters(rRequestTarget, normalized, rErrorMessage))
		return false;

	const auto typeIt = normalized.find("t");
	parsed.strType = typeIt == normalized.end() ? "search" : WebServerJsonSeams::ToLowerAscii(WebServerJsonSeams::TrimAsciiWhitespace(typeIt->second));
	if (parsed.strType.empty())
		parsed.strType = "search";
	if (parsed.strType != "caps" && parsed.strType != "search" && parsed.strType != "tvsearch" && parsed.strType != "movie") {
		rErrorMessage = "unsupported Torznab request type";
		return false;
	}

	const auto queryIt = normalized.find("q");
	const auto seasonIt = normalized.find("season");
	const auto episodeIt = normalized.find("ep");
	const auto yearIt = normalized.find("year");
	const auto catIt = normalized.find("cat");
	const auto offsetIt = normalized.find("offset");
	const auto limitIt = normalized.find("limit");
	if (!WebServerJsonSeams::TryNormalizeSearchText(
			queryIt == normalized.end() ? std::string() : queryIt->second,
			"q",
			true,
			parsed.strQuery,
			rErrorMessage))
	{
		return false;
	}
	parsed.strSeason = seasonIt == normalized.end() ? std::string() : seasonIt->second;
	parsed.strEpisode = episodeIt == normalized.end() ? std::string() : episodeIt->second;
	parsed.strYear = yearIt == normalized.end() ? std::string() : yearIt->second;
	parsed.strCategories = catIt == normalized.end() ? std::string() : catIt->second;
	unsigned uIgnored = 0;
	if (offsetIt != normalized.end() && !TryParseBoundedUnsigned(offsetIt->second, kMaxTorznabOffset, parsed.uOffset)) {
		rErrorMessage = "offset must be an unsigned decimal value in the range 0..1000000";
		return false;
	}
	if (limitIt != normalized.end()) {
		unsigned uParsedLimit = 0;
		if (!TryParseBoundedUnsigned(limitIt->second, kMaxTorznabLimit, uParsedLimit)) {
			rErrorMessage = "limit must be an unsigned decimal value in the range 0..100";
			return false;
		}
		parsed.uLimit = uParsedLimit == 0 ? kDefaultTorznabLimit : uParsedLimit;
	}
	if (!parsed.strSeason.empty() && !TryParseBoundedUnsigned(parsed.strSeason, kMaxTorznabSeason, uIgnored)) {
		rErrorMessage = "season must be an unsigned decimal value in the range 0..9999";
		return false;
	}
	if (!parsed.strEpisode.empty() && !TryParseBoundedUnsigned(parsed.strEpisode, kMaxTorznabEpisode, uIgnored)) {
		rErrorMessage = "ep must be an unsigned decimal value in the range 0..9999";
		return false;
	}
	if (!parsed.strYear.empty() && !TryParseBoundedUnsigned(parsed.strYear, kMaxTorznabYear, uIgnored)) {
		rErrorMessage = "year must be an unsigned decimal value in the range 0..9999";
		return false;
	}
	if (!TryResolveFamily(parsed.strType, parsed.strCategories, parsed.eFamily, rErrorMessage))
		return false;
	rRequest = parsed;
	return true;
}

inline bool IsMd4HexString(const std::string &rValue)
{
	if (rValue.size() != 32)
		return false;
	for (const char ch : rValue) {
		if (!std::isxdigit(static_cast<unsigned char>(ch)))
			return false;
	}
	return true;
}

/**
 * @brief Builds the native eD2K download link exposed by the Torznab adapter.
 */
inline std::string BuildEd2kDownloadLink(const std::string &rEd2kHash, const std::string &rName, const uint64_t ullSize)
{
	const std::string strHash(WebServerJsonSeams::ToLowerAscii(rEd2kHash));
	if (!IsMd4HexString(strHash))
		return std::string();
	if (ullSize == 0)
		return std::string();
	std::string strError;
	if (!WebServerJsonSeams::TryValidatePublicFileNameText(rName, "name", strError))
		return std::string();
	std::ostringstream link;
	link << "ed2k://|file|" << WebServerJsonSeams::UrlEncodeUtf8(rName) << '|' << ullSize << '|' << strHash << "|/";
	return link.str();
}

/**
 * @brief Builds the controlled BTIH-shaped magnet URL that lets Servarr route
 * a result through torrent-indexer code while eMuleBB converts it back to eD2K.
 */
inline std::string BuildEd2kMagnetDownloadLink(const std::string &rEd2kHash, const std::string &rName, const uint64_t ullSize)
{
	const std::string strHash(WebServerJsonSeams::ToLowerAscii(rEd2kHash));
	if (!IsMd4HexString(strHash))
		return std::string();
	if (ullSize == 0)
		return std::string();
	std::string strError;
	if (!WebServerJsonSeams::TryValidatePublicFileNameText(rName, "name", strError))
		return std::string();
	std::ostringstream link;
	link << "magnet:?xt=urn:btih:" << strHash << "00000000&dn=" << WebServerJsonSeams::UrlEncodeUtf8(rName) << "&xl=" << ullSize << "&x.emulebb-ed2k=" << strHash;
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
 * @brief Reports whether a Torznab result set is stable enough to cache.
 */
inline bool ShouldCacheTorznabResults(const size_t uResultCount)
{
	return uResultCount > 0;
}

/**
 * @brief Reports whether an Arr app is probing an indexer's configured category
 * without a real title query.
 */
inline bool IsArrIndexerValidationProbe(const STorznabRequest &rRequest)
{
	return rRequest.strQuery.empty()
		&& !rRequest.strCategories.empty()
		&& rRequest.uOffset == 0
		&& rRequest.eFamily != ETorznabFamily::Unknown;
}

/**
 * @brief Converts a Torznab media family to the REST search type token passed
 * through the native command bridge.
 */
inline const char *GetRestSearchType(const ETorznabFamily eFamily)
{
	switch (eFamily) {
	case ETorznabFamily::Movie:
	case ETorznabFamily::Tv:
		return "video";
	case ETorznabFamily::Audio:
		return "audio";
	case ETorznabFamily::Book:
		return "doc";
	case ETorznabFamily::Other:
	case ETorznabFamily::Any:
	case ETorznabFamily::Unknown:
	default:
		return "";
	}
}

/**
 * @brief Returns REST search type probes for one Torznab family.
 */
inline std::vector<std::string> BuildRestSearchTypeNames(const ETorznabFamily eFamily)
{
	std::vector<std::string> types;
	types.push_back(GetRestSearchType(eFamily));
	return types;
}

/**
 * @brief Returns the native eMule search method order for one Torznab family.
 */
inline std::vector<std::string> BuildNativeSearchMethodNames(const ETorznabFamily eFamily)
{
	if (eFamily == ETorznabFamily::Movie || eFamily == ETorznabFamily::Tv) {
		std::vector<std::string> methods;
		methods.push_back("global");
		methods.push_back("kad");
		return methods;
	}
	std::vector<std::string> methods;
	methods.push_back("automatic");
	return methods;
}

/**
 * @brief Reports whether one native search method is network-specific.
 */
inline bool IsConnectedNetworkSearchMethod(const std::string &rMethod)
{
	const std::string strMethod(WebServerJsonSeams::ToLowerAscii(rMethod));
	return strMethod == "global" || strMethod == "kad";
}

/**
 * @brief Filters native search methods to the currently connected networks.
 */
inline std::vector<std::string> BuildAvailableNativeSearchMethodNames(
	const ETorznabFamily eFamily,
	const bool bGlobalConnected,
	const bool bKadConnected)
{
	std::vector<std::string> methods;
	const std::vector<std::string> candidates(BuildNativeSearchMethodNames(eFamily));
	for (const std::string &rMethod : candidates) {
		const std::string strMethod(WebServerJsonSeams::ToLowerAscii(rMethod));
		if (strMethod == "global") {
			if (bGlobalConnected)
				methods.push_back(rMethod);
		} else if (strMethod == "kad") {
			if (bKadConnected)
				methods.push_back(rMethod);
		} else {
			methods.push_back(rMethod);
		}
	}
	return methods;
}

/**
 * @brief Returns the native search observation window for one Torznab family.
 */
inline unsigned long long GetNativeSearchTimeoutMilliseconds(const ETorznabFamily eFamily)
{
	return (eFamily == ETorznabFamily::Movie || eFamily == ETorznabFamily::Tv)
		? kTorznabMediaSearchTimeoutMs
		: kTorznabDefaultSearchTimeoutMs;
}

/**
 * @brief Returns the bounded observation window for one native search method.
 */
inline unsigned long long GetNativeSearchMethodProbeTimeoutMilliseconds(const ETorznabFamily eFamily, const size_t uMethodCount)
{
	const unsigned long long ullTotal = GetNativeSearchTimeoutMilliseconds(eFamily);
	if ((eFamily != ETorznabFamily::Movie && eFamily != ETorznabFamily::Tv) || uMethodCount <= 1)
		return ullTotal;
	return ullTotal / static_cast<unsigned long long>(uMethodCount);
}

/**
 * @brief Builds expanded native query strings for common Prowlarr media requests.
 */
inline std::vector<std::string> BuildNativeQueries(const STorznabRequest &rRequest)
{
	std::vector<std::string> queries;
	const auto appendUnique = [&queries](const std::string &rQuery) {
		if (std::find(queries.begin(), queries.end(), rQuery) == queries.end())
			queries.push_back(rQuery);
	};
	const std::string strType(WebServerJsonSeams::ToLowerAscii(rRequest.strType));
	if (rRequest.strQuery.empty())
		return queries;

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
			appendUnique(WebServerJsonSeams::NormalizeAsciiWhitespace(sxxeyy.str()));

			std::ostringstream xFormat;
			xFormat << rRequest.strQuery << ' ' << rRequest.strSeason << 'x';
			if (rRequest.strEpisode.size() == 1)
				xFormat << '0';
			xFormat << rRequest.strEpisode;
			appendUnique(WebServerJsonSeams::NormalizeAsciiWhitespace(xFormat.str()));
		} else if (!rRequest.strSeason.empty()) {
			std::ostringstream season;
			season << rRequest.strQuery << " S";
			if (rRequest.strSeason.size() == 1)
				season << '0';
			season << rRequest.strSeason;
			appendUnique(WebServerJsonSeams::NormalizeAsciiWhitespace(season.str()));
		}
	}

	if (strType == "movie" && !rRequest.strYear.empty()) {
		appendUnique(WebServerJsonSeams::NormalizeAsciiWhitespace(rRequest.strQuery + " " + rRequest.strYear));
		appendUnique(rRequest.strQuery);
	}

	appendUnique(rRequest.strQuery);
	return queries;
}

/**
 * @brief Builds the normalized cache token for a concrete native method set.
 */
inline std::string BuildNativeSearchMethodsCacheToken(const std::vector<std::string> &rMethods)
{
	if (rMethods.empty())
		return "none";

	std::ostringstream token;
	for (size_t i = 0; i < rMethods.size(); ++i) {
		if (i != 0)
			token << ',';
		token << WebServerJsonSeams::ToLowerAscii(rMethods[i]);
	}
	return token.str();
}

/**
 * @brief Builds the normalized cache key for one Torznab search request.
 */
inline std::string BuildCacheKey(const STorznabRequest &rRequest, const std::vector<std::string> &rMethods)
{
	std::ostringstream key;
	key << WebServerJsonSeams::ToLowerAscii(rRequest.strType)
		<< "|q=" << WebServerJsonSeams::ToLowerAscii(rRequest.strQuery)
		<< "|cat=" << WebServerJsonSeams::ToLowerAscii(rRequest.strCategories)
		<< "|season=" << WebServerJsonSeams::ToLowerAscii(rRequest.strSeason)
		<< "|ep=" << WebServerJsonSeams::ToLowerAscii(rRequest.strEpisode)
		<< "|year=" << WebServerJsonSeams::ToLowerAscii(rRequest.strYear)
		<< "|family=" << static_cast<int>(rRequest.eFamily)
		<< "|type=" << GetRestSearchType(rRequest.eFamily)
		<< "|methods=" << BuildNativeSearchMethodsCacheToken(rMethods);
	return key.str();
}

inline std::string BuildCacheKey(const STorznabRequest &rRequest)
{
	return BuildCacheKey(rRequest, BuildNativeSearchMethodNames(rRequest.eFamily));
}
}
