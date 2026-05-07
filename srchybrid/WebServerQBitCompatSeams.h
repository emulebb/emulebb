#pragma once

#include <cctype>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "WebServerJsonSeams.h"

/**
 * @brief Pure helpers for the qBittorrent compatibility bridge.
 */
namespace WebServerQBitCompatSeams
{
static const char kQBitTextContentTypeHeader[] = "Content-Type: text/plain; charset=utf-8\r\n";
static const char kQBitJsonContentTypeHeader[] = "Content-Type: application/json; charset=utf-8\r\n";
static const char kQBitSuccessBody[] = "Ok.";
static const char kQBitFailureBody[] = "Fails.";
static const char kQBitNotFoundBody[] = "Not found";

/**
 * @brief Carries the parsed qBittorrent torrent-add request fields.
 */
struct SQBitTorrentAddRequest
{
	std::string strUrl;
	std::string strCategory;
	bool bPaused;

	SQBitTorrentAddRequest()
		: bPaused(false)
	{
	}
};

/**
 * @brief Carries a qBittorrent hash-list mutation request.
 */
struct SQBitHashMutationRequest
{
	std::vector<std::string> hashes;
	bool bDeleteFiles;
	std::string strCategory;

	SQBitHashMutationRequest()
		: bDeleteFiles(false)
	{
	}
};

static const size_t kMaxHashMutationCount = 100;

/**
 * @brief Declares the intentionally supported qBittorrent-compatible REST
 * surface used by Radarr and Sonarr.
 */
struct SQBitRouteSpec
{
	const char *pszMethod;
	const char *pszPath;
	bool bRequiresAuth;
};

inline bool IsQBitRequestTarget(const std::string &rRequestTarget)
{
	const std::string strPathLower(WebServerJsonSeams::ToLowerAscii(WebServerJsonSeams::GetRequestPath(rRequestTarget)));
	return strPathLower == "/api/v2" || strPathLower.rfind("/api/v2/", 0) == 0;
}

inline bool TryGetQBitRequestPathLower(const std::string &rRequestTarget, std::string &rPathLower, std::string &rErrorMessage)
{
	rPathLower.clear();
	if (!WebServerJsonSeams::TryValidateRequestPathEscapes(rRequestTarget, rErrorMessage))
		return false;

	rPathLower = WebServerJsonSeams::ToLowerAscii(WebServerJsonSeams::GetRequestPath(rRequestTarget));
	return true;
}

/**
 * @brief Parses the optional qBittorrent category filter without widening
 * malformed requests to an unfiltered transfer list.
 */
inline bool TryGetOptionalCategoryQueryParam(
	const std::string &rRequestTarget,
	std::string &rCategory,
	std::string &rErrorMessage)
{
	rCategory.clear();
	std::map<std::string, std::string> query;
	if (!WebServerJsonSeams::TryParseQueryString(rRequestTarget, query, rErrorMessage))
		return false;
	const auto it = query.find("category");
	if (it == query.end())
		return true;
	return WebServerJsonSeams::TryNormalizeCategoryNameText(it->second, "category", true, rCategory, rErrorMessage);
}

inline const std::vector<SQBitRouteSpec> &GetQBitRouteSpecs()
{
	static const std::vector<SQBitRouteSpec> specs = {
		{"GET", "/api/v2/app/webapiversion", false},
		{"POST", "/api/v2/auth/login", false},
		{"GET", "/api/v2/app/version", true},
		{"GET", "/api/v2/app/preferences", true},
		{"GET", "/api/v2/torrents/categories", true},
		{"POST", "/api/v2/torrents/createcategory", true},
		{"GET", "/api/v2/torrents/info", true},
		{"GET", "/api/v2/torrents/properties", true},
		{"GET", "/api/v2/torrents/files", true},
		{"POST", "/api/v2/torrents/add", true},
		{"POST", "/api/v2/torrents/delete", true},
		{"POST", "/api/v2/torrents/setcategory", true},
		{"POST", "/api/v2/torrents/pause", true},
		{"POST", "/api/v2/torrents/stop", true},
		{"POST", "/api/v2/torrents/resume", true},
		{"POST", "/api/v2/torrents/start", true},
		{"POST", "/api/v2/torrents/setsharelimits", true},
		{"POST", "/api/v2/torrents/topprio", true},
		{"POST", "/api/v2/torrents/setforcestart", true},
	};
	return specs;
}

inline const SQBitRouteSpec *FindQBitRouteSpec(const std::string &rMethod, const std::string &rPathLower)
{
	const std::vector<SQBitRouteSpec> &specs = GetQBitRouteSpecs();
	for (size_t i = 0; i < specs.size(); ++i) {
		if (rMethod == specs[i].pszMethod && rPathLower == specs[i].pszPath)
			return &specs[i];
	}
	return NULL;
}

/**
 * @brief Parses an application/x-www-form-urlencoded body into unique decoded
 * field names.
 */
inline bool TryParseFormBody(const std::string &rBody, std::map<std::string, std::string> &rForm, std::string &rErrorMessage)
{
	return WebServerJsonSeams::TryParseUrlEncodedFields(
		rBody,
		rForm,
		rErrorMessage,
		"duplicate form field: ",
		"form field name must not be empty");
}

/**
 * @brief Reports whether a qBittorrent-compatible POST body is declared as
 * application/x-www-form-urlencoded.
 */
inline bool IsFormContentType(const std::string &rContentType)
{
	const std::string strContentType(WebServerJsonSeams::ToLowerAscii(WebServerJsonSeams::TrimAsciiWhitespace(rContentType)));
	if (strContentType.empty())
		return false;

	const std::string::size_type uSemicolon = strContentType.find(';');
	const std::string strMediaType(WebServerJsonSeams::TrimAsciiWhitespace(strContentType.substr(0, uSemicolon)));
	return strMediaType == "application/x-www-form-urlencoded";
}

/**
 * @brief Validates qBittorrent-compatible request metadata before form parsing.
 */
inline bool TryValidateFormRequestMetadata(const std::string &rRequestBody, const std::string &rContentType, std::string &rErrorMessage)
{
	if (!rRequestBody.empty() && !IsFormContentType(rContentType)) {
		rErrorMessage = "Content-Type must be application/x-www-form-urlencoded for form request bodies";
		return false;
	}
	return true;
}

inline bool TryGetRequiredNonEmptyFormField(const std::map<std::string, std::string> &rForm, const char *pszFieldName, std::string &rValue, std::string &rErrorMessage)
{
	const auto it = rForm.find(pszFieldName);
	if (it == rForm.end() || it->second.empty()) {
		rErrorMessage = std::string(pszFieldName) + " form field is required";
		return false;
	}
	rValue = it->second;
	return true;
}

/**
 * @brief Normalizes one qBittorrent category form field using the same
 * category-name rules as native REST.
 */
inline bool TryNormalizeCategoryFormField(
	const std::map<std::string, std::string> &rForm,
	const char *pszFieldName,
	const bool bRequired,
	std::string &rCategory,
	std::string &rErrorMessage)
{
	rCategory.clear();
	const auto it = rForm.find(pszFieldName);
	if (it == rForm.end()) {
		if (!bRequired)
			return true;
		rErrorMessage = std::string(pszFieldName) + " form field is required";
		return false;
	}

	return WebServerJsonSeams::TryNormalizeCategoryNameText(it->second, pszFieldName, !bRequired, rCategory, rErrorMessage);
}

/**
 * @brief Parses the qBittorrent category-create body through the native
 * category-name policy.
 */
inline bool TryParseCreateCategoryRequest(const std::string &rBody, std::string &rCategory, std::string &rErrorMessage)
{
	rCategory.clear();
	std::map<std::string, std::string> form;
	return TryParseFormBody(rBody, form, rErrorMessage)
		&& TryNormalizeCategoryFormField(form, "category", true, rCategory, rErrorMessage);
}

/**
 * @brief Validates qBittorrent-compatible login form credentials.
 */
inline bool IsValidLoginForm(
	const std::map<std::string, std::string> &rForm,
	const std::string &rExpectedUsername,
	const std::string &rExpectedPassword)
{
	const auto usernameIt = rForm.find("username");
	const auto passwordIt = rForm.find("password");
	return usernameIt != rForm.end()
		&& passwordIt != rForm.end()
		&& usernameIt->second == rExpectedUsername
		&& passwordIt->second == rExpectedPassword;
}

inline std::string TrimCookieToken(const std::string &rValue)
{
	size_t uBegin = 0;
	while (uBegin < rValue.size() && std::isspace(static_cast<unsigned char>(rValue[uBegin])) != 0)
		++uBegin;

	size_t uEnd = rValue.size();
	while (uEnd > uBegin && std::isspace(static_cast<unsigned char>(rValue[uEnd - 1])) != 0)
		--uEnd;

	return rValue.substr(uBegin, uEnd - uBegin);
}

/**
 * @brief Finds one exact cookie name/value pair in a Cookie header.
 */
inline bool HasCookiePair(const std::string &rCookieHeader, const std::string &rName, const std::string &rValue)
{
	if (rName.empty())
		return false;

	size_t uPos = 0;
	while (uPos <= rCookieHeader.size()) {
		const std::string::size_type uSemi = rCookieHeader.find(';', uPos);
		const std::string token = TrimCookieToken(rCookieHeader.substr(
			uPos,
			uSemi == std::string::npos ? std::string::npos : (uSemi - uPos)));
		const std::string::size_type uEquals = token.find('=');
		if (uEquals != std::string::npos
			&& token.substr(0, uEquals) == rName
			&& token.substr(uEquals + 1) == rValue)
		{
			return true;
		}
		if (uSemi == std::string::npos)
			break;
		uPos = uSemi + 1;
	}
	return false;
}

inline bool IsTruthyFormValue(const std::string &rValue)
{
	const std::string strValue(WebServerJsonSeams::ToLowerAscii(rValue));
	return strValue == "1" || strValue == "true" || strValue == "yes";
}

inline bool IsMd4Hex(const std::string &rValue)
{
	if (rValue.size() != 32)
		return false;
	for (const char ch : rValue) {
		if (!std::isxdigit(static_cast<unsigned char>(ch)))
			return false;
	}
	return true;
}

inline bool IsNativeMd4Hash(const std::string &rValue)
{
	return rValue.size() == 32 && WebServerJsonSeams::IsLowercaseMd4HexString(rValue);
}

inline bool IsQBitWrappedEd2kBtih(const std::string &rBtih)
{
	const std::string strBtih(WebServerJsonSeams::ToLowerAscii(rBtih));
	return strBtih.size() == 40 && IsMd4Hex(strBtih.substr(0, 32)) && strBtih.substr(32) == "00000000";
}

/**
 * @brief Converts one qBittorrent magnet emitted by the Torznab bridge back to
 * a native eD2K file link.
 */
inline bool TryBuildEd2kLinkFromMagnet(const std::string &rMagnet, std::string &rEd2kLink, std::string &rErrorMessage)
{
	rEd2kLink.clear();
	const std::string strPrefix("magnet:?");
	if (rMagnet.rfind(strPrefix, 0) != 0) {
		rErrorMessage = "only magnet URLs are supported";
		return false;
	}

	std::map<std::string, std::string> query;
	if (!WebServerJsonSeams::TryParseQueryString("?" + rMagnet.substr(strPrefix.size()), query, rErrorMessage))
		return false;

	const auto xtIt = query.find("xt");
	const auto nameIt = query.find("dn");
	const auto sizeIt = query.find("xl");
	if (xtIt == query.end() || nameIt == query.end() || sizeIt == query.end()) {
		rErrorMessage = "magnet must contain xt, dn, and xl";
		return false;
	}
	if (!WebServerJsonSeams::TryValidatePublicFileNameText(nameIt->second, "magnet display name", rErrorMessage))
		return false;

	const std::string strXtLower(WebServerJsonSeams::ToLowerAscii(xtIt->second));
	const std::string strBtihPrefix("urn:btih:");
	if (strXtLower.rfind(strBtihPrefix, 0) != 0 || !IsQBitWrappedEd2kBtih(strXtLower.substr(strBtihPrefix.size()))) {
		rErrorMessage = "magnet btih does not carry an eD2K hash";
		return false;
	}
	uint64_t ullSize = 0;
	if (!WebServerJsonSeams::TryParseUnsignedDecimalValue(sizeIt->second, ullSize)) {
		rErrorMessage = "magnet size must be an unsigned decimal value";
		return false;
	}

	if (ullSize == 0) {
		rErrorMessage = "magnet size must be positive";
		return false;
	}

	std::ostringstream ed2k;
	ed2k << "ed2k://|file|"
		<< WebServerJsonSeams::UrlEncodeUtf8(nameIt->second)
		<< '|'
		<< sizeIt->second
		<< '|'
		<< strXtLower.substr(strBtihPrefix.size(), 32)
		<< "|/";
	rEd2kLink = ed2k.str();
	return true;
}

/**
 * @brief Parses the qBittorrent torrent-add form and converts its URL to eD2K
 * when possible.
 */
inline bool TryParseTorrentAddRequest(const std::string &rBody, SQBitTorrentAddRequest &rRequest, std::string &rErrorMessage)
{
	std::map<std::string, std::string> form;
	if (!TryParseFormBody(rBody, form, rErrorMessage))
		return false;

	std::string strMagnet;
	if (!TryGetRequiredNonEmptyFormField(form, "urls", strMagnet, rErrorMessage))
		return false;

	if (!TryBuildEd2kLinkFromMagnet(strMagnet, rRequest.strUrl, rErrorMessage))
		return false;

	if (!TryNormalizeCategoryFormField(form, "category", false, rRequest.strCategory, rErrorMessage))
		return false;

	const auto stoppedIt = form.find("stopped");
	const auto pausedIt = form.find("paused");
	rRequest.bPaused = (stoppedIt != form.end() && IsTruthyFormValue(stoppedIt->second))
		|| (pausedIt != form.end() && IsTruthyFormValue(pausedIt->second));
	return true;
}

/**
 * @brief Parses qBittorrent's pipe-delimited hashes form field into native MD4
 * hashes.
 */
inline bool TryParseHashesFormField(const std::map<std::string, std::string> &rForm, std::vector<std::string> &rHashes, std::string &rErrorMessage)
{
	rHashes.clear();
	const auto hashesIt = rForm.find("hashes");
	if (hashesIt == rForm.end() || hashesIt->second.empty()) {
		rErrorMessage = "hashes form field is required";
		return false;
	}
	if (hashesIt->second == "all") {
		rErrorMessage = "hashes=all is not supported";
		return false;
	}

	size_t uPos = 0;
	while (uPos <= hashesIt->second.size()) {
		const std::string::size_type uPipe = hashesIt->second.find('|', uPos);
		const std::string token = WebServerJsonSeams::ToLowerAscii(hashesIt->second.substr(
			uPos,
			uPipe == std::string::npos ? std::string::npos : (uPipe - uPos)));
		if (token.empty() || !IsNativeMd4Hash(token)) {
			rErrorMessage = "hashes must contain only 32-character eD2K hashes";
			return false;
		}
		for (const std::string &rHash : rHashes) {
			if (rHash == token) {
				rErrorMessage = "hashes must not contain duplicates";
				return false;
			}
		}
		if (rHashes.size() >= kMaxHashMutationCount) {
			rErrorMessage = "hashes form field exceeds the supported item limit";
			return false;
		}
		rHashes.push_back(token);
		if (uPipe == std::string::npos)
			break;
		uPos = uPipe + 1;
	}

	if (rHashes.empty()) {
		rErrorMessage = "hashes form field is required";
		return false;
	}
	return true;
}

/**
 * @brief Parses a qBittorrent delete request body.
 */
inline bool TryParseDeleteRequest(const std::string &rBody, SQBitHashMutationRequest &rRequest, std::string &rErrorMessage)
{
	rRequest = SQBitHashMutationRequest();
	std::map<std::string, std::string> form;
	if (!TryParseFormBody(rBody, form, rErrorMessage))
		return false;
	if (!TryParseHashesFormField(form, rRequest.hashes, rErrorMessage))
		return false;
	// Native eMule transfer cancel cannot preserve partial .part state; qBit
	// compatibility adapts delete requests to the native destructive contract.
	rRequest.bDeleteFiles = true;
	return true;
}

/**
 * @brief Parses a qBittorrent category-assignment request body.
 */
inline bool TryParseSetCategoryRequest(const std::string &rBody, SQBitHashMutationRequest &rRequest, std::string &rErrorMessage)
{
	rRequest = SQBitHashMutationRequest();
	std::map<std::string, std::string> form;
	if (!TryParseFormBody(rBody, form, rErrorMessage))
		return false;
	if (!TryParseHashesFormField(form, rRequest.hashes, rErrorMessage))
		return false;
	if (!TryNormalizeCategoryFormField(form, "category", true, rRequest.strCategory, rErrorMessage))
		return false;
	return true;
}

/**
 * @brief Parses a qBittorrent pause/resume/stop/start request body.
 */
inline bool TryParseHashesOnlyRequest(const std::string &rBody, SQBitHashMutationRequest &rRequest, std::string &rErrorMessage)
{
	rRequest = SQBitHashMutationRequest();
	std::map<std::string, std::string> form;
	if (!TryParseFormBody(rBody, form, rErrorMessage))
		return false;
	return TryParseHashesFormField(form, rRequest.hashes, rErrorMessage);
}
}
