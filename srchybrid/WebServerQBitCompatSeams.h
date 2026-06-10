#pragma once

#include <cctype>
#include <cstdint>
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
 * @brief Maps native REST transfer states to qBittorrent queue states.
 *
 * Arr clients do not treat qBit's `state` as cosmetic text. They use it to
 * distinguish paused, failed, queued, missing, and complete rows in their queue
 * logic, so the compatibility bridge must preserve native REST meaning instead
 * of collapsing everything into `downloading`.
 */
inline const char* GetQBitStateForNativeTransferState(const std::string &rNativeState, const bool bStopped)
{
	// State vocabulary matches qBittorrent 5.0 (Web API 2.11.0), which renamed
	// the paused* states to stopped*.
	const std::string strState(WebServerJsonSeams::ToLowerAscii(rNativeState));
	if (strState == "completed")
		return "stoppedUP";
	if (strState == "checking" || strState == "completing")
		return "checkingDL";
	if (strState == "error")
		return "error";
	if (strState == "missingfiles")
		return "missingFiles";
	if (strState == "paused" || bStopped)
		return "stoppedDL";
	if (strState == "queued")
		return "queuedDL";
	return "downloading";
}

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
	std::string strCategory;
	if (!WebServerJsonSeams::TryNormalizeCategoryNameText(it->second, "category", true, strCategory, rErrorMessage))
		return false;
	rCategory = strCategory;
	return true;
}

/**
 * @brief Parses the required qBittorrent hash query field used by properties
 * and files endpoints.
 */
inline bool TryGetRequiredHashQueryParam(
	const std::string &rRequestTarget,
	std::string &rHash,
	std::string &rErrorMessage)
{
	rHash.clear();
	std::map<std::string, std::string> query;
	if (!WebServerJsonSeams::TryParseQueryString(rRequestTarget, query, rErrorMessage))
		return false;
	const auto it = query.find("hash");
	if (it == query.end() || it->second.empty()) {
		rErrorMessage = "hash query parameter is required";
		return false;
	}
	rHash = WebServerJsonSeams::ToLowerAscii(it->second);
	if (!WebServerJsonSeams::IsLowercaseMd4HexString(rHash)) {
		rErrorMessage = "hash must be a 32-character eD2K hash";
		rHash.clear();
		return false;
	}
	return true;
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
	// The password is the configured REST API key; compare it in constant time
	// to avoid leaking it through response timing. The username is the fixed,
	// non-secret "emule" account so a plain comparison is fine.
	return usernameIt != rForm.end()
		&& passwordIt != rForm.end()
		&& usernameIt->second == rExpectedUsername
		&& WebServerJsonSeams::ConstantTimeSecretEquals(rExpectedPassword, passwordIt->second);
}

/**
 * @brief Extracts the credential from a "Bearer <token>" Authorization header.
 *
 * The scheme name is matched case-insensitively, as permitted by RFC 7235.
 * Returns false when the header is absent or is not a non-empty bearer
 * credential.
 */
inline bool TryParseBearerToken(const std::string &rAuthorizationHeader, std::string &rToken)
{
	rToken.clear();
	const std::string strHeader(WebServerJsonSeams::TrimAsciiWhitespace(rAuthorizationHeader));
	static const char kBearerPrefix[] = "bearer ";
	const size_t uPrefixLen = sizeof(kBearerPrefix) - 1;
	if (strHeader.size() <= uPrefixLen)
		return false;
	for (size_t i = 0; i < uPrefixLen; ++i) {
		if (static_cast<char>(std::tolower(static_cast<unsigned char>(strHeader[i]))) != kBearerPrefix[i])
			return false;
	}
	rToken = WebServerJsonSeams::TrimAsciiWhitespace(strHeader.substr(uPrefixLen));
	return !rToken.empty();
}

/**
 * @brief Reports whether an Authorization header carries a valid bearer API key.
 *
 * Mirrors qBittorrent, which lets clients authenticate with
 * "Authorization: Bearer <key>" instead of the login/cookie flow. The token is
 * compared against the configured REST API key in constant time; an empty
 * configured key never authorizes.
 */
inline bool IsAuthorizedByBearerApiKey(const std::string &rAuthorizationHeader, const std::string &rConfiguredApiKey)
{
	if (rConfiguredApiKey.empty())
		return false;
	std::string strToken;
	if (!TryParseBearerToken(rAuthorizationHeader, strToken))
		return false;
	return WebServerJsonSeams::ConstantTimeSecretEquals(rConfiguredApiKey, strToken);
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
	if (rName.empty() || rValue.empty())
		return false;

	bool bFound = false;
	size_t uPos = 0;
	while (uPos <= rCookieHeader.size()) {
		const std::string::size_type uSemi = rCookieHeader.find(';', uPos);
		const std::string token = TrimCookieToken(rCookieHeader.substr(
			uPos,
			uSemi == std::string::npos ? std::string::npos : (uSemi - uPos)));
		const std::string::size_type uEquals = token.find('=');
		if (uEquals != std::string::npos
			&& token.substr(0, uEquals) == rName)
		{
			if (bFound)
				return false;
			if (token.substr(uEquals + 1) != rValue)
				return false;
			bFound = true;
		}
		if (uSemi == std::string::npos)
			break;
		uPos = uSemi + 1;
	}
	return bFound;
}

/**
 * @brief Parses one optional qBittorrent-compatible boolean form value.
 */
inline bool TryParseOptionalBooleanFormField(
	const std::map<std::string, std::string> &rForm,
	const char *pszFieldName,
	bool &rbValue,
	std::string &rErrorMessage)
{
	rbValue = false;
	const auto it = rForm.find(pszFieldName);
	if (it == rForm.end())
		return true;

	const std::string strValue(WebServerJsonSeams::ToLowerAscii(WebServerJsonSeams::TrimAsciiWhitespace(it->second)));
	if (strValue == "1" || strValue == "true" || strValue == "yes") {
		rbValue = true;
		return true;
	}
	if (strValue == "0" || strValue == "false" || strValue == "no") {
		rbValue = false;
		return true;
	}

	rErrorMessage = std::string(pszFieldName) + " must be a boolean form value";
	return false;
}

/**
 * Validates an optional signed decimal qBittorrent form field.
 */
inline bool TryParseOptionalSignedDecimalFormField(
	const std::map<std::string, std::string> &rForm,
	const char *pszFieldName,
	std::string &rErrorMessage)
{
	const auto it = rForm.find(pszFieldName);
	if (it == rForm.end())
		return true;

	const std::string strValue(WebServerJsonSeams::TrimAsciiWhitespace(it->second));
	if (strValue.empty()) {
		rErrorMessage = std::string(pszFieldName) + " must be a signed decimal value";
		return false;
	}

	size_t uPos = 0;
	if (strValue[uPos] == '+' || strValue[uPos] == '-')
		++uPos;
	if (uPos >= strValue.size()) {
		rErrorMessage = std::string(pszFieldName) + " must be a signed decimal value";
		return false;
	}

	bool bSawDigit = false;
	bool bSawDot = false;
	for (; uPos < strValue.size(); ++uPos) {
		const char ch = strValue[uPos];
		if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
			bSawDigit = true;
			continue;
		}
		if (ch == '.' && !bSawDot) {
			bSawDot = true;
			continue;
		}
		rErrorMessage = std::string(pszFieldName) + " must be a signed decimal value";
		return false;
	}
	if (!bSawDigit) {
		rErrorMessage = std::string(pszFieldName) + " must be a signed decimal value";
		return false;
	}
	return true;
}

/**
 * Validates an optional signed integer qBittorrent form field.
 */
inline bool TryParseOptionalSignedIntegerFormField(
	const std::map<std::string, std::string> &rForm,
	const char *pszFieldName,
	std::string &rErrorMessage)
{
	const auto it = rForm.find(pszFieldName);
	if (it == rForm.end())
		return true;

	const std::string strValue(WebServerJsonSeams::TrimAsciiWhitespace(it->second));
	if (strValue.empty()) {
		rErrorMessage = std::string(pszFieldName) + " must be a signed integer value";
		return false;
	}

	size_t uPos = 0;
	if (strValue[uPos] == '+' || strValue[uPos] == '-')
		++uPos;
	if (uPos >= strValue.size()) {
		rErrorMessage = std::string(pszFieldName) + " must be a signed integer value";
		return false;
	}
	for (; uPos < strValue.size(); ++uPos) {
		if (std::isdigit(static_cast<unsigned char>(strValue[uPos])) == 0) {
			rErrorMessage = std::string(pszFieldName) + " must be a signed integer value";
			return false;
		}
	}
	return true;
}

/**
 * Validates the qBittorrent share-limit fields sent by Arr clients.
 */
inline bool TryValidateShareLimitFormFields(const std::map<std::string, std::string> &rForm, std::string &rErrorMessage)
{
	return TryParseOptionalSignedDecimalFormField(rForm, "ratioLimit", rErrorMessage)
		&& TryParseOptionalSignedIntegerFormField(rForm, "seedingTimeLimit", rErrorMessage)
		&& TryParseOptionalSignedIntegerFormField(rForm, "inactiveSeedingTimeLimit", rErrorMessage);
}

inline bool IsNativeMd4Hash(const std::string &rValue)
{
	return rValue.size() == 32 && WebServerJsonSeams::IsLowercaseMd4HexString(rValue);
}

inline bool IsBtihHash(const std::string &rValue)
{
	if (rValue.size() != 40)
		return false;
	for (const char ch : rValue) {
		if (!std::isxdigit(static_cast<unsigned char>(ch)))
			return false;
	}
	return true;
}

inline bool StartsWithNoCase(const std::string &rValue, const char *pszPrefix)
{
	const std::string strPrefix(pszPrefix != NULL ? pszPrefix : "");
	if (rValue.size() < strPrefix.size())
		return false;
	return WebServerJsonSeams::ToLowerAscii(rValue.substr(0, strPrefix.size())) == WebServerJsonSeams::ToLowerAscii(strPrefix);
}

inline std::string BuildNativeEd2kUrl(const std::string &rHash, const std::string &rName, const uint64_t ullSize)
{
	std::ostringstream link;
	link << "ed2k://|file|" << WebServerJsonSeams::UrlEncodeUtf8(rName) << '|' << ullSize << '|' << rHash << "|/";
	return link.str();
}

inline bool TryNormalizeControlledEd2kMagnetUrl(const std::string &rUrl, std::string &rNormalizedUrl, std::string &rErrorMessage)
{
	static const std::string strPrefix("magnet:?");
	rNormalizedUrl.clear();
	if (!StartsWithNoCase(rUrl, strPrefix.c_str())) {
		rErrorMessage = "magnet URLs are not supported";
		return false;
	}

	std::map<std::string, std::string> fields;
	if (!WebServerJsonSeams::TryParseUrlEncodedFields(rUrl.substr(strPrefix.size()), fields, rErrorMessage, "duplicate magnet parameter: "))
		return false;

	std::map<std::string, std::string> normalized;
	for (const auto &rField : fields) {
		const std::string strName(WebServerJsonSeams::ToLowerAscii(rField.first));
		if (normalized.find(strName) != normalized.end()) {
			rErrorMessage = "duplicate magnet parameter: " + strName;
			return false;
		}
		normalized[strName] = rField.second;
	}

	const auto xtIt = normalized.find("xt");
	const auto dnIt = normalized.find("dn");
	const auto xlIt = normalized.find("xl");
	if (xtIt == normalized.end() || dnIt == normalized.end() || xlIt == normalized.end()) {
		rErrorMessage = "eMuleBB ED2K magnet requires xt, dn, and xl parameters";
		return false;
	}

	static const std::string strEd2kUrnPrefix("urn:ed2k:");
	static const std::string strBtihUrnPrefix("urn:btih:");
	std::string strHash;
	if (StartsWithNoCase(xtIt->second, strEd2kUrnPrefix.c_str())) {
		strHash = WebServerJsonSeams::ToLowerAscii(xtIt->second.substr(strEd2kUrnPrefix.size()));
	} else if (StartsWithNoCase(xtIt->second, strBtihUrnPrefix.c_str())) {
		const auto emulebbEd2kIt = normalized.find("x.emulebb-ed2k");
		if (emulebbEd2kIt == normalized.end()) {
			rErrorMessage = "eMuleBB BTIH magnets require x.emulebb-ed2k";
			return false;
		}
		const std::string strBtihHash(WebServerJsonSeams::ToLowerAscii(xtIt->second.substr(strBtihUrnPrefix.size())));
		const std::string strEd2kHash(WebServerJsonSeams::ToLowerAscii(emulebbEd2kIt->second));
		if (!IsBtihHash(strBtihHash) || !IsNativeMd4Hash(strEd2kHash) || strBtihHash != strEd2kHash + "00000000") {
			rErrorMessage = "eMuleBB BTIH magnet does not match its eD2K hash";
			return false;
		}
		strHash = strEd2kHash;
	} else {
		rErrorMessage = "only eMuleBB ED2K magnets are supported";
		return false;
	}
	if (!IsNativeMd4Hash(strHash)) {
		rErrorMessage = "xt must contain a 32-character eD2K hash";
		return false;
	}

	uint64_t ullSize = 0;
	if (!WebServerJsonSeams::TryParseUnsignedDecimalValue(xlIt->second, ullSize) || ullSize == 0) {
		rErrorMessage = "xl must be a positive unsigned decimal size";
		return false;
	}

	std::string strNameError;
	if (!WebServerJsonSeams::TryValidatePublicFileNameText(dnIt->second, "dn", strNameError)) {
		rErrorMessage = strNameError;
		return false;
	}

	rNormalizedUrl = BuildNativeEd2kUrl(strHash, dnIt->second, ullSize);
	return true;
}

inline bool TryValidateAddRequestUrl(const std::string &rUrl, std::string &rNormalizedUrl, std::string &rErrorMessage)
{
	rNormalizedUrl.clear();
	if (StartsWithNoCase(rUrl, "magnet:")) {
		return TryNormalizeControlledEd2kMagnetUrl(rUrl, rNormalizedUrl, rErrorMessage);
	}
	if (!StartsWithNoCase(rUrl, "ed2k://")) {
		rErrorMessage = "only eD2K URLs are supported";
		return false;
	}
	rNormalizedUrl = rUrl;
	return true;
}

/**
 * @brief Parses the qBittorrent torrent-add form for native eD2K URLs.
 */
inline bool TryParseTorrentAddRequest(const std::string &rBody, SQBitTorrentAddRequest &rRequest, std::string &rErrorMessage)
{
	std::map<std::string, std::string> form;
	if (!TryParseFormBody(rBody, form, rErrorMessage))
		return false;

	std::string strUrl;
	if (!TryGetRequiredNonEmptyFormField(form, "urls", strUrl, rErrorMessage))
		return false;

	if (!TryValidateAddRequestUrl(strUrl, rRequest.strUrl, rErrorMessage))
		return false;

	if (!TryNormalizeCategoryFormField(form, "category", false, rRequest.strCategory, rErrorMessage))
		return false;

	bool bStopped = false;
	bool bPaused = false;
	if (!TryParseOptionalBooleanFormField(form, "stopped", bStopped, rErrorMessage))
		return false;
	if (!TryParseOptionalBooleanFormField(form, "paused", bPaused, rErrorMessage))
		return false;
	if (!TryValidateShareLimitFormFields(form, rErrorMessage))
		return false;
	rRequest.bPaused = bStopped || bPaused;
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
	bool bIgnoredDeleteFiles = false;
	if (!TryParseOptionalBooleanFormField(form, "deleteFiles", bIgnoredDeleteFiles, rErrorMessage))
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

/**
 * @brief Parses qBittorrent share-limit no-op requests while validating the
 * public limit controls that Arr may send.
 */
inline bool TryParseShareLimitsRequest(const std::string &rBody, SQBitHashMutationRequest &rRequest, std::string &rErrorMessage)
{
	rRequest = SQBitHashMutationRequest();
	std::map<std::string, std::string> form;
	if (!TryParseFormBody(rBody, form, rErrorMessage))
		return false;
	if (!TryParseHashesFormField(form, rRequest.hashes, rErrorMessage))
		return false;
	return TryValidateShareLimitFormFields(form, rErrorMessage);
}

/**
 * @brief Parses a qBittorrent force-start no-op request while validating its
 * public boolean control field.
 */
inline bool TryParseForceStartRequest(const std::string &rBody, SQBitHashMutationRequest &rRequest, std::string &rErrorMessage)
{
	rRequest = SQBitHashMutationRequest();
	std::map<std::string, std::string> form;
	if (!TryParseFormBody(rBody, form, rErrorMessage))
		return false;
	if (!TryParseHashesFormField(form, rRequest.hashes, rErrorMessage))
		return false;
	bool bIgnoredValue = false;
	return TryParseOptionalBooleanFormField(form, "value", bIgnoredValue, rErrorMessage);
}
}
