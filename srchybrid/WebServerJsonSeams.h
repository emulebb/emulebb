#pragma once

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include <Windows.h>

#include <nlohmann/json.hpp>

#include "AppStateSeams.h"
#include "WebApiSurfaceSeams.h"

namespace WebServerJsonSeams
{
using json = nlohmann::json;

static const size_t kMaxSearchQueryLength = 160;
static const size_t kMaxCategoryNameLength = 128;
static const size_t kMaxPublicFileNameLength = 255;
static const size_t kMaxUrlImportLength = 2048;
static const size_t kMaxTransferAddLinks = 100;
static const size_t kMaxQBitTorrentInfoRows = 2000;
static const UINT kRestUiDispatchTimeoutMs = 15000u;
static const char *const kRestRouteExecutionDirect = "direct";
static const char *const kRestRouteExecutionUiThread = "ui-thread";
static const char *const kTransferPriorityError = "priority must be one of auto, verylow, low, normal, high, veryhigh";
static const char *const kSharedUploadPriorityError = "priority must be one of auto, verylow, low, normal, high, release";
static const char *const kCategoryPriorityError = "priority must be one of verylow, low, normal, high, veryhigh";
static const char *const kServerPriorityError = "priority must be one of low, normal, high";
static const char *const kSearchMethodError = "method must be one of automatic, server, global, kad";
static const char *const kTransferStateError = "state must be one of downloading, paused, queued, checking, completing, completed, error, missingfiles";

/**
 * @brief Lifecycle safety category for one native REST command.
 */
enum class ERestLifecyclePolicy : uint8_t
{
	Read,
	Mutation,
	Shutdown,
	DiagnosticUnsafe
};

/**
 * @brief Carries one parsed REST route command together with the normalized
 * request parameters that feed the existing UI-command handler.
 */
struct SApiRoute
{
	std::string strCommand;
	std::string strMethod;
	json params;
	std::string strPathTemplate;

	SApiRoute()
		: params(json::object())
	{
	}
};

/**
 * @brief Describes one public REST route and its strict request surface.
 */
struct SApiRouteSpec
{
	const char *pszMethod;
	const char *pszPathTemplate;
	const char *pszBodyFields;
	const char *pszQueryFields;
	const char *pszExecutionModel = kRestRouteExecutionUiThread;
};

/**
 * @brief Carries the native REST API-key authorization decision and stable
 * error payload fields when authorization fails.
 */
struct SApiAuthResult
{
	bool bAllowed;
	std::string strErrorCode;
	std::string strErrorMessage;

	SApiAuthResult()
		: bAllowed(false)
	{
	}
};

/**
 * @brief Converts the native directory-rule lookup into the public shared-file
 * metadata flag. Single-file shares do not count as rule-backed shares.
 */
inline bool BuildSharedByRuleFlag(const bool bDirectoryRuleMatched)
{
	return bDirectoryRuleMatched;
}

/**
 * @brief Normalizes the Win32 SendMessageTimeout result used by REST dispatch.
 */
inline bool DidRestUiDispatchComplete(const LRESULT lSendMessageTimeoutResult)
{
	return lSendMessageTimeoutResult != 0;
}

/**
 * @brief Reports whether REST command execution must stop because app teardown
 * has begun and UI-owned subsystems may already be partially destroyed.
 */
inline bool ShouldRejectRestCommandDuringShutdown(const bool bAppClosing)
{
	return bAppClosing;
}

/**
 * @brief Reports whether an HTTP method mutates native state for lifecycle
 * gating.
 */
inline bool IsRestMutationMethod(const std::string &rMethod)
{
	return rMethod == "POST" || rMethod == "PATCH" || rMethod == "DELETE";
}

/**
 * @brief Returns the lifecycle policy for one internal REST command token.
 */
inline ERestLifecyclePolicy GetLifecyclePolicyForCommand(const std::string &rCommand)
{
	if (rCommand == "app/shutdown")
		return ERestLifecyclePolicy::Shutdown;
	if (rCommand == "app/capture_dump" || rCommand == "app/crash_test")
		return ERestLifecyclePolicy::DiagnosticUnsafe;

	if (rCommand == "app/version"
		|| rCommand == "app/preferences/get"
		|| rCommand == "stats/global"
		|| rCommand == "status/get"
		|| rCommand == "snapshot/get"
		|| rCommand == "categories/list"
		|| rCommand == "categories/get"
		|| rCommand == "shared_directories/get"
		|| rCommand == "qbit/transfers/info"
		|| rCommand == "transfers/list"
		|| rCommand == "transfers/get"
		|| rCommand == "transfers/sources"
		|| rCommand == "transfers/source"
		|| rCommand == "transfers/details"
		|| rCommand == "uploads/list"
		|| rCommand == "uploads/get"
		|| rCommand == "uploads/queue"
		|| rCommand == "uploads/queue_get"
		|| rCommand == "servers/list"
		|| rCommand == "servers/status"
		|| rCommand == "servers/get"
		|| rCommand == "kad/status"
		|| rCommand == "shared/list"
		|| rCommand == "shared/get"
		|| rCommand == "shared/ed2k_link"
		|| rCommand == "shared/comments"
		|| rCommand == "search/list"
		|| rCommand == "search/results"
		|| rCommand == "friends/list"
		|| rCommand == "log/get")
	{
		return ERestLifecyclePolicy::Read;
	}

	return ERestLifecyclePolicy::Mutation;
}

/**
 * @brief Reports whether the command policy needs mutation-level lifecycle readiness.
 */
inline bool IsLifecycleMutationPolicy(const ERestLifecyclePolicy ePolicy)
{
	return ePolicy != ERestLifecyclePolicy::Read;
}

/**
 * @brief Reports whether one REST command should be rejected for the current lifecycle.
 */
inline bool ShouldRejectRestCommandForLifecycle(
	const SAppLifecycleStatus &rLifecycle,
	const ERestLifecyclePolicy ePolicy)
{
	return !rLifecycle.bAcceptingRest || (IsLifecycleMutationPolicy(ePolicy) && !rLifecycle.bAcceptingMutations);
}

inline std::string ToLowerAscii(const std::string &rValue)
{
	std::string result(rValue);
	for (char &rCh : result)
		rCh = static_cast<char>(std::tolower(static_cast<unsigned char>(rCh)));
	return result;
}

inline std::string TrimAsciiWhitespace(const std::string &rValue)
{
	std::string::size_type uBegin = 0;
	while (uBegin < rValue.size() && std::isspace(static_cast<unsigned char>(rValue[uBegin])) != 0)
		++uBegin;

	std::string::size_type uEnd = rValue.size();
	while (uEnd > uBegin && std::isspace(static_cast<unsigned char>(rValue[uEnd - 1])) != 0)
		--uEnd;

	return rValue.substr(uBegin, uEnd - uBegin);
}

/**
 * @brief Returns the native search method token adapters should use when the
 * caller has not requested a transport-specific search.
 */
inline const char *GetDefaultSearchMethodName()
{
	return "automatic";
}

/**
 * @brief Reports whether a token is in the public lowercase compact
 * search-method vocabulary.
 */
inline bool IsSearchMethodName(const std::string &rValue)
{
	return rValue == GetDefaultSearchMethodName()
		|| rValue == "server"
		|| rValue == "global"
		|| rValue == "kad";
}

/**
 * @brief Returns the exact lowercase compact search file-type tokens exposed
 * by REST v1.
 */
inline const std::vector<std::string>& GetRestSearchFileTypeNames()
{
	static const std::vector<std::string> s_names = {
		"",
		"arc",
		"audio",
		"iso",
		"image",
		"pro",
		"video",
		"doc",
		"emulecollection"
	};
	return s_names;
}

/**
 * @brief Reports whether a token is in the REST search-file-type vocabulary.
 */
inline bool IsSearchFileTypeName(const std::string &rValue)
{
	const std::vector<std::string> &rNames = GetRestSearchFileTypeNames();
	return std::find(rNames.begin(), rNames.end(), rValue) != rNames.end();
}

/**
 * @brief Converts one REST search type token to the native eMule token used by
 * the search engine.
 */
inline const char* GetNativeSearchFileTypeName(const std::string &rValue)
{
	if (rValue.empty())
		return "";
	if (rValue == "arc")
		return "Arc";
	if (rValue == "audio")
		return "Audio";
	if (rValue == "iso")
		return "Iso";
	if (rValue == "image")
		return "Image";
	if (rValue == "pro")
		return "Pro";
	if (rValue == "video")
		return "Video";
	if (rValue == "doc")
		return "Doc";
	if (rValue == "emulecollection")
		return "EmuleCollection";
	return "";
}

/**
 * @brief Converts one native eMule search type token to the REST v1 token.
 */
inline std::string GetRestSearchFileTypeName(const std::string &rValue)
{
	if (rValue.empty())
		return "";
	if (rValue == "Arc")
		return "arc";
	if (rValue == "Audio")
		return "audio";
	if (rValue == "Iso")
		return "iso";
	if (rValue == "Image")
		return "image";
	if (rValue == "Pro")
		return "pro";
	if (rValue == "Video")
		return "video";
	if (rValue == "Doc")
		return "doc";
	if (rValue == "EmuleCollection")
		return "emulecollection";
	return "";
}

/**
 * @brief Reports whether a token is in the REST transfer-state vocabulary.
 */
inline bool IsTransferStateName(const std::string &rValue)
{
	return rValue == "downloading"
		|| rValue == "paused"
		|| rValue == "queued"
		|| rValue == "checking"
		|| rValue == "completing"
		|| rValue == "completed"
		|| rValue == "error"
		|| rValue == "missingfiles";
}

/**
 * @brief Collapses ASCII whitespace runs without changing UTF-8 payload bytes.
 */
inline std::string NormalizeAsciiWhitespace(const std::string &rValue)
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
 * @brief Validates UTF-8 with the platform decoder and returns the UTF-16
 * length used by the app's native UI layer.
 */
inline bool TryMeasureStrictUtf8AsUtf16(const std::string &rValue, size_t &ruWideCharacters)
{
	ruWideCharacters = 0;
	if (rValue.empty())
		return true;
	if (rValue.size() > static_cast<size_t>(INT_MAX))
		return false;

	const int iWideChars = ::MultiByteToWideChar(
		CP_UTF8,
		MB_ERR_INVALID_CHARS,
		rValue.data(),
		static_cast<int>(rValue.size()),
		NULL,
		0);
	if (iWideChars <= 0)
		return false;

	std::vector<WCHAR> wide(static_cast<size_t>(iWideChars));
	const int iConverted = ::MultiByteToWideChar(
		CP_UTF8,
		MB_ERR_INVALID_CHARS,
		rValue.data(),
		static_cast<int>(rValue.size()),
		wide.data(),
		iWideChars);
	if (iConverted != iWideChars)
		return false;

	for (const WCHAR ch : wide) {
		if (ch < 0x20 && ch != L' ')
			return false;
	}
	ruWideCharacters = static_cast<size_t>(iWideChars);
	return true;
}

/**
 * @brief Applies the shared public search-text rules used by native REST and
 * *arr compatibility search requests.
 */
inline bool TryNormalizeSearchText(
	const std::string &rValue,
	const char *pszFieldName,
	const bool bAllowEmpty,
	std::string &rNormalized,
	std::string &rErrorMessage)
{
	const std::string strFieldName(pszFieldName != NULL && pszFieldName[0] != '\0' ? pszFieldName : "query");
	rNormalized = NormalizeAsciiWhitespace(TrimAsciiWhitespace(rValue));
	if (rNormalized.empty()) {
		if (bAllowEmpty)
			return true;
		rErrorMessage = strFieldName + " must not be empty";
		return false;
	}

	size_t uWideCharacters = 0;
	if (!TryMeasureStrictUtf8AsUtf16(rNormalized, uWideCharacters)) {
		rErrorMessage = strFieldName + " must be valid UTF-8 without control characters";
		return false;
	}
	if (uWideCharacters > kMaxSearchQueryLength) {
		rErrorMessage = strFieldName + " must be at most 160 characters";
		return false;
	}
	return true;
}

/**
 * @brief Applies shared public category-name rules used by native REST and
 * qBittorrent-compatible category selectors.
 */
inline bool TryNormalizeCategoryNameText(
	const std::string &rValue,
	const char *pszFieldName,
	const bool bAllowEmpty,
	std::string &rNormalized,
	std::string &rErrorMessage)
{
	const std::string strFieldName(pszFieldName != NULL && pszFieldName[0] != '\0' ? pszFieldName : "categoryName");
	rNormalized = TrimAsciiWhitespace(rValue);
	if (rNormalized.empty()) {
		if (bAllowEmpty)
			return true;
		rErrorMessage = strFieldName + " must not be empty";
		return false;
	}

	size_t uWideCharacters = 0;
	if (!TryMeasureStrictUtf8AsUtf16(rNormalized, uWideCharacters)) {
		rErrorMessage = strFieldName + " must be valid UTF-8 without control characters";
		return false;
	}
	if (uWideCharacters > kMaxCategoryNameLength) {
		rErrorMessage = strFieldName + " must be at most 128 characters";
		return false;
	}
	return true;
}

/**
 * @brief Validates a public file/display name before REST-adjacent link
 * conversion emits it into an eD2K URL.
 */
inline bool TryValidatePublicFileNameText(
	const std::string &rValue,
	const char *pszFieldName,
	std::string &rErrorMessage)
{
	const std::string strFieldName(pszFieldName != NULL && pszFieldName[0] != '\0' ? pszFieldName : "name");
	if (rValue.empty()) {
		rErrorMessage = strFieldName + " must not be empty";
		return false;
	}

	size_t uWideCharacters = 0;
	if (!TryMeasureStrictUtf8AsUtf16(rValue, uWideCharacters)) {
		rErrorMessage = strFieldName + " must be valid UTF-8 without control characters";
		return false;
	}
	if (uWideCharacters > kMaxPublicFileNameLength) {
		rErrorMessage = strFieldName + " must be at most 255 characters";
		return false;
	}
	return true;
}

/**
 * @brief Validates and trims one public URL-import token before it reaches the
 * modal downloader used by server.met and nodes.dat updates.
 */
inline bool TryValidateUrlImportText(
	const std::string &rValue,
	const char *pszFieldName,
	std::string &rNormalized,
	std::string &rErrorMessage)
{
	const std::string strFieldName(pszFieldName != NULL && pszFieldName[0] != '\0' ? pszFieldName : "url");
	rNormalized = TrimAsciiWhitespace(rValue);
	if (rNormalized.empty()) {
		rErrorMessage = strFieldName + " must not be empty";
		return false;
	}

	size_t uWideCharacters = 0;
	if (!TryMeasureStrictUtf8AsUtf16(rNormalized, uWideCharacters)) {
		rErrorMessage = strFieldName + " must be valid UTF-8 without control characters";
		return false;
	}
	if (uWideCharacters > kMaxUrlImportLength) {
		rErrorMessage = strFieldName + " must be at most 2048 characters";
		return false;
	}
	for (const char ch : rNormalized) {
		if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
			rErrorMessage = strFieldName + " must not contain whitespace";
			return false;
		}
	}

	const std::string strLower(ToLowerAscii(rNormalized));
	const bool bHttp = strLower.find("http://") == 0;
	const bool bHttps = strLower.find("https://") == 0;
	if (!bHttp && !bHttps) {
		rErrorMessage = strFieldName + " must start with http:// or https://";
		return false;
	}

	const std::string::size_type uHostBegin = strLower.find("://") + 3;
	if (uHostBegin >= rNormalized.size()
		|| rNormalized[uHostBegin] == '/'
		|| rNormalized[uHostBegin] == '?'
		|| rNormalized[uHostBegin] == '#') {
		rErrorMessage = strFieldName + " must include a host";
		return false;
	}
	return true;
}

inline int HexNibble(const char ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	if (ch >= 'a' && ch <= 'f')
		return 10 + (ch - 'a');
	if (ch >= 'A' && ch <= 'F')
		return 10 + (ch - 'A');
	return -1;
}

inline char HexDigit(const unsigned char value)
{
	return static_cast<char>(value < 10 ? ('0' + value) : ('A' + (value - 10)));
}

/**
 * @brief URL-encodes UTF-8 text for REST-adjacent compatibility links.
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

inline bool TryUrlDecodeUtf8WithPlusPolicy(
	const std::string &rValue,
	const bool bDecodePlusAsSpace,
	std::string &rDecoded,
	std::string &rErrorMessage)
{
	rDecoded.clear();
	rDecoded.reserve(rValue.size());
	for (size_t i = 0; i < rValue.size(); ++i) {
		const char ch = rValue[i];
		if (ch == '+' && bDecodePlusAsSpace) {
			rDecoded.push_back(' ');
			continue;
		}

		if (ch == '%') {
			if (i + 2 >= rValue.size()) {
				rErrorMessage = "malformed percent escape";
				return false;
			}
			const int high = HexNibble(rValue[i + 1]);
			const int low = HexNibble(rValue[i + 2]);
			if (high < 0 || low < 0) {
				rErrorMessage = "malformed percent escape";
				return false;
			}
			rDecoded.push_back(static_cast<char>((high << 4) | low));
			i += 2;
			continue;
		}

		rDecoded.push_back(ch);
	}
	return true;
}

/**
 * @brief Decodes one URL-encoded UTF-8 token for form/query parsing, where
 * plus signs are spaces by application/x-www-form-urlencoded convention.
 */
inline bool TryUrlDecodeUtf8(const std::string &rValue, std::string &rDecoded, std::string &rErrorMessage)
{
	return TryUrlDecodeUtf8WithPlusPolicy(rValue, true, rDecoded, rErrorMessage);
}

/**
 * @brief Decodes one URL path segment. A raw plus is a literal path byte, not
 * a space; callers still reject malformed escapes and encoded separators.
 */
inline bool TryUrlDecodePathSegmentUtf8(const std::string &rValue, std::string &rDecoded, std::string &rErrorMessage)
{
	return TryUrlDecodeUtf8WithPlusPolicy(rValue, false, rDecoded, rErrorMessage);
}

inline std::string UrlDecodeUtf8(const std::string &rValue)
{
	std::string decoded;
	std::string ignored;
	if (!TryUrlDecodeUtf8(rValue, decoded, ignored))
		return std::string();
	return decoded;
}

/**
 * @brief Removes the query suffix from one request target.
 */
inline std::string GetRequestPath(const std::string &rRequestTarget)
{
	const std::string::size_type uQuery = rRequestTarget.find('?');
	return uQuery == std::string::npos ? rRequestTarget : rRequestTarget.substr(0, uQuery);
}

/**
 * @brief Reports whether one request target belongs to the in-process REST
 * surface.
 */
inline bool IsApiRequestTarget(const std::string &rRequestTarget)
{
	const std::string strPathLower(ToLowerAscii(GetRequestPath(rRequestTarget)));
	return strPathLower == "/api/v1"
		|| strPathLower.rfind("/api/v1/", 0) == 0
		|| strPathLower.rfind("/api/v1%", 0) == 0;
}

/**
 * @brief Parses URL-encoded key/value pairs into unique decoded parameters.
 */
inline bool TryParseUrlEncodedFields(
	const std::string &rFields,
	std::map<std::string, std::string> &rFieldsOut,
	std::string &rErrorMessage,
	const char *pszDuplicatePrefix,
	const char *pszEmptyNameMessage = NULL)
{
	rFieldsOut.clear();

	size_t uPos = 0;
	while (uPos <= rFields.size()) {
		const std::string::size_type uAmp = rFields.find('&', uPos);
		const std::string token = rFields.substr(
			uPos,
			uAmp == std::string::npos ? std::string::npos : (uAmp - uPos));
		if (!token.empty()) {
			const std::string::size_type uEquals = token.find('=');
			std::string strName;
			std::string strValue;
			if (!TryUrlDecodeUtf8(token.substr(0, uEquals), strName, rErrorMessage))
				return false;
			if (uEquals != std::string::npos && !TryUrlDecodeUtf8(token.substr(uEquals + 1), strValue, rErrorMessage))
				return false;
			if (strName.empty() && pszEmptyNameMessage != NULL) {
				rErrorMessage = pszEmptyNameMessage;
				return false;
			}
			if (rFieldsOut.find(strName) != rFieldsOut.end()) {
				rErrorMessage = std::string(pszDuplicatePrefix != NULL ? pszDuplicatePrefix : "duplicate field: ") + strName;
				return false;
			}
			rFieldsOut[strName] = strValue;
		}

		if (uAmp == std::string::npos)
			break;
		uPos = uAmp + 1;
	}

	return true;
}

/**
 * @brief Parses a query string into unique decoded parameters.
 */
inline bool TryParseQueryString(const std::string &rRequestTarget, std::map<std::string, std::string> &rQuery, std::string &rErrorMessage)
{
	rQuery.clear();
	const std::string::size_type uQuery = rRequestTarget.find('?');
	if (uQuery == std::string::npos || uQuery + 1 >= rRequestTarget.size())
		return true;

	return TryParseUrlEncodedFields(
		rRequestTarget.substr(uQuery + 1),
		rQuery,
		rErrorMessage,
		"duplicate query parameter: ");
}

/**
 * @brief Reports whether a comma-separated token list contains one exact name.
 */
inline bool HasToken(const char *pszTokens, const std::string &rName)
{
	if (pszTokens == NULL || *pszTokens == '\0')
		return false;

	const std::string tokens(pszTokens);
	size_t uPos = 0;
	while (uPos <= tokens.size()) {
		const std::string::size_type uComma = tokens.find(',', uPos);
		const std::string token = tokens.substr(
			uPos,
			uComma == std::string::npos ? std::string::npos : (uComma - uPos));
		if (token == rName)
			return true;
		if (uComma == std::string::npos)
			break;
		uPos = uComma + 1;
	}
	return false;
}

inline std::vector<std::string> SplitPathSegments(const std::string &rPath)
{
	std::vector<std::string> segments;
	size_t uPos = 0;
	while (uPos <= rPath.size()) {
		const std::string::size_type uSlash = rPath.find('/', uPos);
		const std::string token = rPath.substr(
			uPos,
			uSlash == std::string::npos ? std::string::npos : (uSlash - uPos));
		if (!token.empty()) {
			std::string decoded;
			std::string ignored;
			if (!TryUrlDecodePathSegmentUtf8(token, decoded, ignored))
				decoded.clear();
			segments.push_back(decoded);
		}

		if (uSlash == std::string::npos)
			break;
		uPos = uSlash + 1;
	}

	return segments;
}

inline bool TrySplitPathSegments(const std::string &rPath, std::vector<std::string> &rSegments, std::string &rErrorMessage)
{
	rSegments.clear();
	size_t uPos = 0;
	while (uPos <= rPath.size()) {
		const std::string::size_type uSlash = rPath.find('/', uPos);
		const std::string token = rPath.substr(
			uPos,
			uSlash == std::string::npos ? std::string::npos : (uSlash - uPos));
		if (!token.empty()) {
			std::string decoded;
			if (!TryUrlDecodePathSegmentUtf8(token, decoded, rErrorMessage))
				return false;
			if (decoded.find('/') != std::string::npos || decoded.find('\\') != std::string::npos) {
				rErrorMessage = "path segment must not contain encoded slash";
				return false;
			}
			rSegments.push_back(decoded);
		}

		if (uSlash == std::string::npos)
			break;
		uPos = uSlash + 1;
	}

	return true;
}

inline bool TryValidateRequestPathEscapes(const std::string &rRequestTarget, std::string &rErrorMessage)
{
	std::vector<std::string> ignored;
	return TrySplitPathSegments(GetRequestPath(rRequestTarget), ignored, rErrorMessage);
}

inline bool IsValidUnsignedDecimal(const std::string &rValue)
{
	if (rValue.empty())
		return false;
	for (const char ch : rValue) {
		if (std::isdigit(static_cast<unsigned char>(ch)) == 0)
			return false;
	}
	char *pEnd = NULL;
	errno = 0;
	(void)std::strtoull(rValue.c_str(), &pEnd, 10);
	return errno == 0 && pEnd != NULL && *pEnd == '\0';
}

/**
 * @brief Parses a strict unsigned decimal token without accepting signs,
 * whitespace, or partial values.
 */
inline bool TryParseUnsignedDecimalValue(const std::string &rValue, uint64_t &ruValue)
{
	if (!IsValidUnsignedDecimal(rValue))
		return false;

	ruValue = static_cast<uint64_t>(std::strtoull(rValue.c_str(), NULL, 10));
	return true;
}

/**
 * @brief Parses one JSON value as a non-negative uint64 using the same overflow
 * and sign rules as strict REST decimal tokens.
 */
inline bool TryParseJsonUInt64(const json &rValue, uint64_t &ruValue, const bool bAllowString = false)
{
	if (rValue.is_number_unsigned()) {
		ruValue = rValue.get<uint64_t>();
		return true;
	}
	if (rValue.is_number_integer()) {
		const int64_t iValue = rValue.get<int64_t>();
		if (iValue < 0)
			return false;
		ruValue = static_cast<uint64_t>(iValue);
		return true;
	}
	if (bAllowString && rValue.is_string())
		return TryParseUnsignedDecimalValue(rValue.get_ref<const std::string&>(), ruValue);
	return false;
}

inline bool TryParseUnsignedQueryValue(const std::map<std::string, std::string> &rQuery, const char *pszName, uint64_t &ruValue)
{
	const auto it = rQuery.find(pszName);
	if (it == rQuery.end())
		return false;

	return TryParseUnsignedDecimalValue(it->second, ruValue);
}

inline bool IsLowercaseMd4HexString(const std::string &rValue)
{
	if (rValue.size() != 32)
		return false;
	for (const char ch : rValue) {
		if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')))
			return false;
	}
	return true;
}

inline void SetInvalidArgument(std::string &rErrorCode, std::string &rErrorMessage, const std::string &rMessage)
{
	rErrorCode = "INVALID_ARGUMENT";
	rErrorMessage = rMessage;
}

/**
 * @brief Reports whether a native REST request body is declared as JSON.
 */
inline bool IsJsonContentType(const std::string &rContentType)
{
	const std::string strContentType(ToLowerAscii(TrimAsciiWhitespace(rContentType)));
	if (strContentType.empty())
		return false;

	const std::string::size_type uSemicolon = strContentType.find(';');
	const std::string strMediaType(TrimAsciiWhitespace(strContentType.substr(0, uSemicolon)));
	return strMediaType == "application/json";
}

/**
 * @brief Validates HTTP metadata before the body is parsed as native REST JSON.
 */
inline bool TryValidateRequestMetadata(
	const std::string &rRequestBody,
	const std::string &rContentType,
	std::string &rErrorCode,
	std::string &rErrorMessage)
{
	if (!rRequestBody.empty() && !IsJsonContentType(rContentType)) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "Content-Type must be application/json for JSON request bodies");
		return false;
	}
	return true;
}

/**
 * @brief Returns the strict route table used before legacy command dispatch.
 */
inline const std::vector<SApiRouteSpec> &GetApiRouteSpecs()
{
	static const std::vector<SApiRouteSpec> specs = {
		{"GET", "/app", "", "", kRestRouteExecutionDirect},
		{"GET", "/app/preferences", "", ""},
		{"PATCH", "/app/preferences", WebApiSurfaceSeams::kMutablePreferenceFieldListCsv, ""},
		{"POST", "/app/shutdown", "confirmShutdown", ""},
		{"POST", "/diagnostics/dumps", "confirmDump,fullMemory", ""},
		{"POST", "/diagnostics/crash-tests", "confirmCrash", ""},
		{"GET", "/status", "", ""},
		{"GET", "/stats", "", ""},
		{"GET", "/snapshot", "", "limit"},
		{"GET", "/categories", "", ""},
		{"POST", "/categories", "name,path,comment,color,priority", ""},
		{"GET", "/categories/{categoryId}", "", ""},
		{"PATCH", "/categories/{categoryId}", "name,path,comment,color,priority", ""},
		{"DELETE", "/categories/{categoryId}", "", ""},
		{"GET", "/transfers", "", "state,categoryId,offset,limit"},
		{"POST", "/transfers", "link,links,categoryId,categoryName,paused", ""},
		{"POST", "/transfers/operations/clear-completed", "confirmClearCompleted", ""},
		{"GET", "/transfers/{hash}", "", ""},
		{"PATCH", "/transfers/{hash}", "name,priority,categoryId,categoryName", ""},
		{"DELETE", "/transfers/{hash}", "", ""},
		{"DELETE", "/transfers/{hash}/files", "", "confirm"},
		{"GET", "/transfers/{hash}/details", "", ""},
		{"GET", "/transfers/{hash}/sources", "", ""},
		{"GET", "/transfers/{hash}/sources/{clientId}", "", ""},
		{"POST", "/transfers/{hash}/sources/{clientId}/operations/browse", "", ""},
		{"POST", "/transfers/{hash}/sources/{clientId}/operations/add-friend", "", ""},
		{"POST", "/transfers/{hash}/sources/{clientId}/operations/remove-friend", "", ""},
		{"POST", "/transfers/{hash}/sources/{clientId}/operations/remove", "", ""},
		{"POST", "/transfers/{hash}/sources/{clientId}/operations/ban", "", ""},
		{"POST", "/transfers/{hash}/sources/{clientId}/operations/unban", "", ""},
		{"POST", "/transfers/{hash}/sources/{clientId}/operations/release-slot", "", ""},
		{"POST", "/transfers/{hash}/operations/pause", "", ""},
		{"POST", "/transfers/{hash}/operations/resume", "", ""},
		{"POST", "/transfers/{hash}/operations/stop", "", ""},
		{"POST", "/transfers/{hash}/operations/recheck", "", ""},
		{"POST", "/transfers/{hash}/operations/preview", "", ""},
		{"GET", "/shared-files", "", "offset,limit"},
		{"POST", "/shared-files", "path", ""},
		{"POST", "/shared-files/operations/reload", "", ""},
		{"GET", "/shared-files/{hash}", "", ""},
		{"PATCH", "/shared-files/{hash}", "priority,rating,comment", ""},
		{"DELETE", "/shared-files/{hash}", "", ""},
		{"DELETE", "/shared-files/{hash}/file", "", "confirm"},
		{"GET", "/shared-files/{hash}/ed2k-link", "", ""},
		{"GET", "/shared-files/{hash}/comments", "", ""},
		{"GET", "/shared-directories", "", ""},
		{"PATCH", "/shared-directories", "roots,confirmReplaceRoots", ""},
		{"POST", "/shared-directories/operations/reload", "", ""},
		{"GET", "/uploads", "", ""},
		{"GET", "/uploads/{clientId}", "", ""},
		{"POST", "/uploads/{clientId}/operations/remove", "", ""},
		{"POST", "/uploads/{clientId}/operations/release-slot", "", ""},
		{"POST", "/uploads/{clientId}/operations/add-friend", "", ""},
		{"POST", "/uploads/{clientId}/operations/remove-friend", "", ""},
		{"POST", "/uploads/{clientId}/operations/ban", "", ""},
		{"POST", "/uploads/{clientId}/operations/unban", "", ""},
		{"GET", "/upload-queue", "", "offset,limit"},
		{"GET", "/upload-queue/{clientId}", "", ""},
		{"POST", "/upload-queue/{clientId}/operations/remove", "", ""},
		{"POST", "/upload-queue/{clientId}/operations/release-slot", "", ""},
		{"POST", "/upload-queue/{clientId}/operations/add-friend", "", ""},
		{"POST", "/upload-queue/{clientId}/operations/remove-friend", "", ""},
		{"POST", "/upload-queue/{clientId}/operations/ban", "", ""},
		{"POST", "/upload-queue/{clientId}/operations/unban", "", ""},
		{"GET", "/servers", "", ""},
		{"POST", "/servers", "address,port,name,priority,static,connect", ""},
		{"POST", "/servers/operations/connect", "", ""},
		{"POST", "/servers/operations/disconnect", "", ""},
		{"POST", "/servers/operations/import-met-url", "url", ""},
		{"GET", "/servers/{serverId}", "", ""},
		{"PATCH", "/servers/{serverId}", "name,priority,static", ""},
		{"DELETE", "/servers/{serverId}", "", ""},
		{"POST", "/servers/{serverId}/operations/connect", "", ""},
		{"GET", "/kad", "", ""},
		{"POST", "/kad/operations/import-nodes-url", "url", ""},
		{"POST", "/kad/operations/start", "", ""},
		{"POST", "/kad/operations/stop", "", ""},
		{"POST", "/kad/operations/bootstrap", "address,port", ""},
		{"POST", "/kad/operations/recheck-firewall", "", ""},
		{"GET", "/searches", "", ""},
		{"POST", "/searches", "query,method,type,minSizeBytes,maxSizeBytes,minAvailability,extension", ""},
		{"DELETE", "/searches", "", "confirm"},
		{"GET", "/searches/{searchId}", "", "offset,limit"},
		{"DELETE", "/searches/{searchId}", "", ""},
		{"POST", "/searches/{searchId}/results/{hash}/operations/download", "categoryId,categoryName,paused", ""},
		{"GET", "/friends", "", ""},
		{"POST", "/friends", "userHash,name", ""},
		{"DELETE", "/friends/{userHash}", "", ""},
		{"GET", "/logs", "", "limit"},
		{"POST", "/logs/operations/clear", "confirmClearLogs", ""},
	};
	return specs;
}

inline std::string BuildRoutePathForSpec(const std::vector<std::string> &rRoute)
{
	std::string path;
	for (const std::string &segment : rRoute) {
		path += "/";
		path += segment;
	}
	return path.empty() ? "/" : path;
}

inline bool IsTemplateParameter(const std::string &rSegment)
{
	return rSegment.size() >= 3 && rSegment.front() == '{' && rSegment.back() == '}';
}

inline bool DoesPathMatchTemplate(const std::string &rPath, const char *pszPathTemplate)
{
	const std::vector<std::string> pathSegments = SplitPathSegments(rPath);
	const std::vector<std::string> templateSegments = SplitPathSegments(pszPathTemplate != NULL ? pszPathTemplate : "");
	if (pathSegments.size() != templateSegments.size())
		return false;
	for (size_t i = 0; i < pathSegments.size(); ++i) {
		if (IsTemplateParameter(templateSegments[i])) {
			if (pathSegments[i].empty())
				return false;
			continue;
		}
		if (pathSegments[i] != templateSegments[i])
			return false;
	}
	return true;
}

inline const SApiRouteSpec *FindRouteSpec(const std::string &rMethodUpper, const std::string &rApiPath)
{
	const std::vector<SApiRouteSpec> &specs = GetApiRouteSpecs();
	for (size_t i = 0; i < specs.size(); ++i) {
		if (rMethodUpper == specs[i].pszMethod && DoesPathMatchTemplate(rApiPath, specs[i].pszPathTemplate))
			return &specs[i];
	}
	return NULL;
}

/**
 * @brief Finds a route by path without considering its HTTP method.
 */
inline const SApiRouteSpec *FindRouteSpecForAnyMethod(const std::string &rApiPath)
{
	const std::vector<SApiRouteSpec> &specs = GetApiRouteSpecs();
	for (size_t i = 0; i < specs.size(); ++i) {
		if (DoesPathMatchTemplate(rApiPath, specs[i].pszPathTemplate))
			return &specs[i];
	}
	return NULL;
}

inline std::string ToUpperAscii(const std::string &rValue)
{
	std::string result(rValue);
	for (char &rCh : result)
		rCh = static_cast<char>(std::toupper(static_cast<unsigned char>(rCh)));
	return result;
}

/**
 * @brief Parses an endpoint route token in the public "address:port" form.
 */
inline bool TryCopyEndpointToken(const std::string &rValue, json &rParams)
{
	const std::string::size_type uColon = rValue.rfind(':');
	if (uColon == std::string::npos || uColon == 0 || uColon + 1 >= rValue.size())
		return false;

	const std::string strPort = rValue.substr(uColon + 1);
	uint64_t ullPort = 0;
	if (!TryParseUnsignedDecimalValue(strPort, ullPort))
		return false;

	if (ullPort == 0 || ullPort > static_cast<uint64_t>(0xFFFF))
		return false;

	rParams["addr"] = rValue.substr(0, uColon);
	rParams["port"] = static_cast<unsigned>(ullPort);
	return true;
}

inline bool TryValidateBoundedUnsignedDecimal(
	const std::string &rValue,
	const uint64_t uMaxValue,
	const char *pszErrorFieldName,
	std::string &rErrorCode,
	std::string &rErrorMessage)
{
	uint64_t uValue = 0;
	if (!TryParseUnsignedDecimalValue(rValue, uValue)) {
		SetInvalidArgument(rErrorCode, rErrorMessage, std::string(pszErrorFieldName) + " must be an unsigned decimal string");
		return false;
	}

	if (uValue > uMaxValue) {
		SetInvalidArgument(rErrorCode, rErrorMessage, std::string(pszErrorFieldName) + " is out of range");
		return false;
	}
	return true;
}

/**
 * @brief Validates one unsigned query parameter against the published bounds.
 */
inline bool TryParseBoundedQueryUInt(
	const std::string &rValue,
	const uint64_t uMinValue,
	const uint64_t uMaxValue,
	const char *pszFieldName,
	std::string &rErrorCode,
	std::string &rErrorMessage)
{
	uint64_t uValue = 0;
	if (!TryParseUnsignedDecimalValue(rValue, uValue)) {
		SetInvalidArgument(rErrorCode, rErrorMessage, std::string(pszFieldName) + " must be an unsigned number");
		return false;
	}
	if (uValue < uMinValue || uValue > uMaxValue) {
		SetInvalidArgument(rErrorCode, rErrorMessage, std::string(pszFieldName) + " is out of range");
		return false;
	}
	return true;
}

/**
 * @brief Rejects ambiguous category selectors before command dispatch.
 */
inline bool ValidateCategorySelectorBody(json &rBody, std::string &rErrorCode, std::string &rErrorMessage)
{
	if (rBody.contains("categoryId") && rBody.contains("categoryName")) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "categoryId and categoryName are mutually exclusive");
		return false;
	}
	if (rBody.contains("categoryId")) {
		uint64_t uCategoryId = 0;
		if (!TryParseJsonUInt64(rBody["categoryId"], uCategoryId)) {
			SetInvalidArgument(rErrorCode, rErrorMessage, "categoryId must be an unsigned number");
			return false;
		}
		if (uCategoryId > UINT_MAX) {
			SetInvalidArgument(rErrorCode, rErrorMessage, "categoryId is out of range");
			return false;
		}
		rBody["categoryId"] = uCategoryId;
	}
	if (rBody.contains("categoryName")) {
		if (!rBody["categoryName"].is_string()) {
			SetInvalidArgument(rErrorCode, rErrorMessage, "categoryName must be a string");
			return false;
		}
		std::string strCategoryName;
		std::string strError;
		if (!TryNormalizeCategoryNameText(rBody["categoryName"].get<std::string>(), "categoryName", false, strCategoryName, strError)) {
			SetInvalidArgument(rErrorCode, rErrorMessage, strError);
			return false;
		}
		rBody["categoryName"] = strCategoryName;
	}
	return true;
}

/**
 * @brief Validates and trims one public transfer-add eD2K link token.
 */
inline bool TryParseTransferAddLink(const json &rParams, std::string &rLink, std::string &rError)
{
	if (!rParams.contains("link") || !rParams["link"].is_string()) {
		rError = "link must be a string";
		return false;
	}

	rLink = TrimAsciiWhitespace(rParams["link"].get<std::string>());
	std::string strNormalized;
	if (!TryValidateUrlImportText(rLink, "link", strNormalized, rError))
		return false;
	rLink = strNormalized;
	return true;
}

inline bool ValidateOptionalPausedField(const json &rBody, std::string &rErrorCode, std::string &rErrorMessage)
{
	if (rBody.contains("paused") && !rBody["paused"].is_boolean()) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "paused must be a boolean");
		return false;
	}
	return true;
}

/**
 * @brief Validates native transfer-add body shape before command dispatch.
 */
inline bool ValidateTransferAddBody(json &rBody, std::string &rErrorCode, std::string &rErrorMessage)
{
	if (rBody.contains("link") && rBody.contains("links")) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "link and links are mutually exclusive");
		return false;
	}
	if (!rBody.contains("link") && !rBody.contains("links")) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "link or links is required");
		return false;
	}
	if (!ValidateOptionalPausedField(rBody, rErrorCode, rErrorMessage))
		return false;
	if (rBody.contains("link")) {
		std::string strLink;
		std::string strError;
		if (!TryParseTransferAddLink(rBody, strLink, strError)) {
			SetInvalidArgument(rErrorCode, rErrorMessage, strError);
			return false;
		}
		rBody["link"] = strLink;
	}
	if (rBody.contains("links")) {
		if (!rBody["links"].is_array()) {
			SetInvalidArgument(rErrorCode, rErrorMessage, "links must be a string array");
			return false;
		}
		if (rBody["links"].empty()) {
			SetInvalidArgument(rErrorCode, rErrorMessage, "links must not be empty");
			return false;
		}
		if (rBody["links"].size() > kMaxTransferAddLinks) {
			SetInvalidArgument(rErrorCode, rErrorMessage, "links contains too many items");
			return false;
		}
		for (json &rLinkValue : rBody["links"]) {
			std::string strLink;
			std::string strError;
			if (!TryParseTransferAddLink(json{{"link", rLinkValue}}, strLink, strError)) {
				SetInvalidArgument(rErrorCode, rErrorMessage, "links must be a non-empty string array");
				return false;
			}
			rLinkValue = strLink;
		}
	}
	return true;
}

/**
 * @brief Validates and trims one public transfer rename token.
 */
inline bool TryParseTransferRenameText(const json &rParams, std::string &rName, std::string &rError)
{
	if (!rParams.contains("name") || !rParams["name"].is_string()) {
		rError = "name must be a string";
		return false;
	}

	rName = TrimAsciiWhitespace(rParams["name"].get<std::string>());
	if (rName.empty()) {
		rError = "name must not be empty";
		return false;
	}

	return TryValidatePublicFileNameText(rName, "name", rError);
}

/**
 * @brief Validates native transfer PATCH body shape before command dispatch.
 */
inline bool ValidateTransferPatchBody(json &rBody, std::string &rErrorCode, std::string &rErrorMessage)
{
	int iMutationFamilyCount = 0;
	if (rBody.contains("priority"))
		++iMutationFamilyCount;
	if (rBody.contains("categoryId") || rBody.contains("categoryName"))
		++iMutationFamilyCount;
	if (rBody.contains("name"))
		++iMutationFamilyCount;

	if (iMutationFamilyCount == 0) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "transfer PATCH requires priority, categoryId, categoryName, or name");
		return false;
	}
	if (iMutationFamilyCount > 1) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "transfer PATCH accepts only one mutation family");
		return false;
	}
	if (rBody.contains("priority") && !rBody["priority"].is_string()) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "priority must be a string");
		return false;
	}
	if (rBody.contains("priority")
		&& !WebApiSurfaceSeams::IsTransferPriorityName(rBody["priority"].get_ref<const std::string&>().c_str())) {
		SetInvalidArgument(rErrorCode, rErrorMessage, kTransferPriorityError);
		return false;
	}
	if (rBody.contains("name")) {
		std::string strName;
		std::string strError;
		if (!TryParseTransferRenameText(rBody, strName, strError)) {
			SetInvalidArgument(rErrorCode, rErrorMessage, strError);
			return false;
		}
		rBody["name"] = strName;
	}
	return true;
}

/**
 * @brief Validates public shared-file comment/rating fields.
 */
inline bool TryParseSharedFileRatingCommentFields(const json &rParams, std::string &rComment, int &riRating, std::string &rError)
{
	if (!rParams.contains("comment") || !rParams["comment"].is_string()) {
		rError = "comment must be a string";
		return false;
	}
	if (!rParams.contains("rating") || !rParams["rating"].is_number_integer()) {
		rError = "rating must be an integer between 0 and 5";
		return false;
	}

	const int iRating = rParams["rating"].get<int>();
	if (iRating < 0 || iRating > 5) {
		rError = "rating must be an integer between 0 and 5";
		return false;
	}

	rComment = rParams["comment"].get<std::string>();
	riRating = iRating;
	return true;
}

/**
 * @brief Validates native shared-file PATCH body shape before command dispatch.
 */
inline bool ValidateSharedFilePatchBody(json &rBody, std::string &rErrorCode, std::string &rErrorMessage)
{
	if (!rBody.contains("priority") && !rBody.contains("comment") && !rBody.contains("rating")) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "shared-file PATCH requires priority, comment, or rating");
		return false;
	}
	if (rBody.contains("priority") && !rBody["priority"].is_string()) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "priority must be a string");
		return false;
	}
	if (rBody.contains("priority")
		&& !WebApiSurfaceSeams::IsSharedUploadPriorityName(rBody["priority"].get_ref<const std::string&>().c_str())) {
		SetInvalidArgument(rErrorCode, rErrorMessage, kSharedUploadPriorityError);
		return false;
	}
	if (rBody.contains("comment") || rBody.contains("rating")) {
		std::string strComment;
		int iRating = 0;
		std::string strError;
		if (!TryParseSharedFileRatingCommentFields(rBody, strComment, iRating, strError)) {
			SetInvalidArgument(rErrorCode, rErrorMessage, strError);
			return false;
		}
	}
	return true;
}

/**
 * @brief Validates and trims one public filesystem path token.
 */
inline bool TryParsePathText(const json &rValue, const char *pszFieldName, std::string &rPath, std::string &rError)
{
	if (!rValue.is_string()) {
		rError = std::string(pszFieldName) + " must be a non-empty string path";
		return false;
	}

	rPath = TrimAsciiWhitespace(rValue.get<std::string>());
	if (rPath.empty()) {
		rError = std::string(pszFieldName) + " must not be empty";
		return false;
	}
	return true;
}

/**
 * @brief Validates native shared-file add body shape before command dispatch.
 */
inline bool ValidateSharedFileAddBody(json &rBody, std::string &rErrorCode, std::string &rErrorMessage)
{
	std::string strPath;
	std::string strError;
	if (!TryParsePathText(rBody.contains("path") ? rBody["path"] : json(), "path", strPath, strError)) {
		SetInvalidArgument(rErrorCode, rErrorMessage, strError);
		return false;
	}
	rBody["path"] = strPath;
	return true;
}

/**
 * @brief Validates one shared-directory root descriptor shape.
 */
inline bool ValidateSharedDirectoryRootBody(json &rRoot, std::string &rErrorCode, std::string &rErrorMessage)
{
	json *pPathValue = &rRoot;
	if (rRoot.is_object()) {
		for (json::iterator it = rRoot.begin(); it != rRoot.end(); ++it) {
			if (it.key() != "path" && it.key() != "recursive") {
				SetInvalidArgument(rErrorCode, rErrorMessage, "unknown shared-directory root field: " + it.key());
				return false;
			}
		}
		if (!rRoot.contains("path")) {
			SetInvalidArgument(rErrorCode, rErrorMessage, "path must be a non-empty string path");
			return false;
		}
		if (rRoot.contains("recursive") && !rRoot["recursive"].is_boolean()) {
			SetInvalidArgument(rErrorCode, rErrorMessage, "recursive must be a boolean");
			return false;
		}
		pPathValue = &rRoot["path"];
	}

	std::string strPath;
	std::string strError;
	if (!TryParsePathText(*pPathValue, "path", strPath, strError)) {
		SetInvalidArgument(rErrorCode, rErrorMessage, strError);
		return false;
	}
	if (rRoot.is_object())
		rRoot["path"] = strPath;
	else
		rRoot = strPath;
	return true;
}

/**
 * @brief Validates native shared-directory replacement body shape.
 */
inline bool ValidateSharedDirectoriesPatchBody(json &rBody, std::string &rErrorCode, std::string &rErrorMessage)
{
	if (!rBody.contains("roots") || !rBody["roots"].is_array()) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "roots must be an array");
		return false;
	}
	for (json &rRoot : rBody["roots"]) {
		if (!ValidateSharedDirectoryRootBody(rRoot, rErrorCode, rErrorMessage))
			return false;
	}
	return true;
}

/**
 * @brief Validates and trims one public non-empty string token.
 */
inline bool TryParseNonEmptyTextField(json &rBody, const char *pszFieldName, std::string &rErrorCode, std::string &rErrorMessage)
{
	if (!rBody.contains(pszFieldName) || !rBody[pszFieldName].is_string()) {
		SetInvalidArgument(rErrorCode, rErrorMessage, std::string(pszFieldName) + " must be a non-empty string");
		return false;
	}
	const std::string strValue(TrimAsciiWhitespace(rBody[pszFieldName].get<std::string>()));
	if (strValue.empty()) {
		SetInvalidArgument(rErrorCode, rErrorMessage, std::string(pszFieldName) + " must not be empty");
		return false;
	}
	rBody[pszFieldName] = strValue;
	return true;
}

/**
 * @brief Validates one public TCP port body field.
 */
inline bool ValidatePortBodyField(json &rBody, const char *pszFieldName, std::string &rErrorCode, std::string &rErrorMessage)
{
	uint64_t uPort = 0;
	if (!rBody.contains(pszFieldName) || !TryParseJsonUInt64(rBody[pszFieldName], uPort) || uPort == 0 || uPort > 0xFFFFui64) {
		SetInvalidArgument(rErrorCode, rErrorMessage, std::string(pszFieldName) + " must be in the range 1..65535");
		return false;
	}
	rBody[pszFieldName] = uPort;
	return true;
}

/**
 * @brief Validates native server-create body shape before command dispatch.
 */
inline bool ValidateServerCreateBody(json &rBody, std::string &rErrorCode, std::string &rErrorMessage)
{
	if (rBody.contains("addr")) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "server create uses address, not addr");
		return false;
	}
	if (!TryParseNonEmptyTextField(rBody, "address", rErrorCode, rErrorMessage))
		return false;
	if (!ValidatePortBodyField(rBody, "port", rErrorCode, rErrorMessage))
		return false;
	if (rBody.contains("name") && !rBody["name"].is_string()) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "name must be a string when provided");
		return false;
	}
	if (rBody.contains("priority") && !rBody["priority"].is_string()) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "priority must be a string");
		return false;
	}
	if (rBody.contains("priority")
		&& !WebApiSurfaceSeams::IsServerPriorityName(rBody["priority"].get_ref<const std::string&>().c_str())) {
		SetInvalidArgument(rErrorCode, rErrorMessage, kServerPriorityError);
		return false;
	}
	if (rBody.contains("static") && !rBody["static"].is_boolean()) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "static must be a boolean");
		return false;
	}
	if (rBody.contains("connect") && !rBody["connect"].is_boolean()) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "connect must be a boolean");
		return false;
	}
	return true;
}

/**
 * @brief Validates native server PATCH body shape before command dispatch.
 */
inline bool ValidateServerPatchBody(json &rBody, std::string &rErrorCode, std::string &rErrorMessage)
{
	if (!rBody.contains("name") && !rBody.contains("priority") && !rBody.contains("static")) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "server PATCH requires name, priority, or static");
		return false;
	}
	if (rBody.contains("name") && !rBody["name"].is_string()) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "name must be a string when provided");
		return false;
	}
	if (rBody.contains("priority") && !rBody["priority"].is_string()) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "priority must be a string");
		return false;
	}
	if (rBody.contains("priority")
		&& !WebApiSurfaceSeams::IsServerPriorityName(rBody["priority"].get_ref<const std::string&>().c_str())) {
		SetInvalidArgument(rErrorCode, rErrorMessage, kServerPriorityError);
		return false;
	}
	if (rBody.contains("static") && !rBody["static"].is_boolean()) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "static must be a boolean");
		return false;
	}
	return true;
}

/**
 * @brief Validates native URL-import body shape before command dispatch.
 */
inline bool ValidateUrlImportBody(json &rBody, std::string &rErrorCode, std::string &rErrorMessage)
{
	if (!rBody.contains("url") || !rBody["url"].is_string()) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "url must be a non-empty string");
		return false;
	}

	std::string strUrl;
	std::string strError;
	if (!TryValidateUrlImportText(rBody["url"].get<std::string>(), "url", strUrl, strError)) {
		SetInvalidArgument(rErrorCode, rErrorMessage, strError);
		return false;
	}
	rBody["url"] = strUrl;
	return true;
}

/**
 * @brief Validates native Kad bootstrap body shape before command dispatch.
 */
inline bool ValidateKadBootstrapBody(json &rBody, std::string &rErrorCode, std::string &rErrorMessage)
{
	return TryParseNonEmptyTextField(rBody, "address", rErrorCode, rErrorMessage)
		&& ValidatePortBodyField(rBody, "port", rErrorCode, rErrorMessage);
}

/**
 * @brief Validates the category priority payload shape accepted by native
 * category create/update requests.
 */
inline bool ValidateCategoryPriorityField(json &rBody, std::string &rErrorCode, std::string &rErrorMessage)
{
	if (!rBody.contains("priority"))
		return true;

	if (rBody["priority"].is_number_unsigned() || rBody["priority"].is_number_integer()) {
		uint64_t ullPriority = 0;
		if (!TryParseJsonUInt64(rBody["priority"], ullPriority) || ullPriority > UINT_MAX) {
			SetInvalidArgument(rErrorCode, rErrorMessage, "priority must be a supported priority value");
			return false;
		}
		rBody["priority"] = ullPriority;
		return true;
	}

	if (!rBody["priority"].is_string()) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "priority must be a string or number");
		return false;
	}

	if (WebApiSurfaceSeams::IsCategoryPriorityName(rBody["priority"].get_ref<const std::string&>().c_str()))
		return true;

	SetInvalidArgument(rErrorCode, rErrorMessage, kCategoryPriorityError);
	return false;
}

/**
 * @brief Validates the shared category create/update body shape before live
 * category command handling applies filesystem policy.
 */
inline bool ValidateCategoryCoreBody(json &rBody, const bool bRequireName, std::string &rErrorCode, std::string &rErrorMessage)
{
	if (bRequireName || rBody.contains("name")) {
		if (!TryParseNonEmptyTextField(rBody, "name", rErrorCode, rErrorMessage))
			return false;
	}

	if (rBody.contains("path") && !rBody["path"].is_null()) {
		std::string strPath;
		std::string strError;
		if (!TryParsePathText(rBody["path"], "path", strPath, strError)) {
			SetInvalidArgument(rErrorCode, rErrorMessage, strError);
			return false;
		}
		rBody["path"] = strPath;
	}

	if (rBody.contains("comment") && !rBody["comment"].is_string()) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "comment must be a string");
		return false;
	}

	if (rBody.contains("color") && !rBody["color"].is_null()) {
		uint64_t ullColor = 0;
		if (!TryParseJsonUInt64(rBody["color"], ullColor) || ullColor > 0x00FFFFFFui64) {
			SetInvalidArgument(rErrorCode, rErrorMessage, "color must be null or an RGB integer");
			return false;
		}
		rBody["color"] = ullColor;
	}

	return ValidateCategoryPriorityField(rBody, rErrorCode, rErrorMessage);
}

/**
 * @brief Validates native category-create body shape before command dispatch.
 */
inline bool ValidateCategoryCreateBody(json &rBody, std::string &rErrorCode, std::string &rErrorMessage)
{
	return ValidateCategoryCoreBody(rBody, true, rErrorCode, rErrorMessage);
}

/**
 * @brief Validates native category-update body shape before command dispatch.
 */
inline bool ValidateCategoryPatchBody(json &rBody, std::string &rErrorCode, std::string &rErrorMessage)
{
	if (rBody.empty()) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "category PATCH requires at least one field");
		return false;
	}
	return ValidateCategoryCoreBody(rBody, false, rErrorCode, rErrorMessage);
}

/**
 * @brief Validates native friend-create body shape before command dispatch.
 */
inline bool ValidateFriendCreateBody(json &rBody, std::string &rErrorCode, std::string &rErrorMessage)
{
	if (!rBody.contains("userHash") || !rBody["userHash"].is_string() || !IsLowercaseMd4HexString(rBody["userHash"].get_ref<const std::string&>())) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "userHash must be a 32-character lowercase hex string");
		return false;
	}

	if (rBody.contains("name")) {
		if (!rBody["name"].is_string()) {
			SetInvalidArgument(rErrorCode, rErrorMessage, "name must be a string");
			return false;
		}
		size_t uWideCharacters = 0;
		if (!TryMeasureStrictUtf8AsUtf16(rBody["name"].get_ref<const std::string&>(), uWideCharacters)) {
			SetInvalidArgument(rErrorCode, rErrorMessage, "name must be valid UTF-8 without control characters");
			return false;
		}
		if (uWideCharacters > kMaxCategoryNameLength) {
			SetInvalidArgument(rErrorCode, rErrorMessage, "name must be at most 128 characters");
			return false;
		}
	}

	return true;
}

/**
 * @brief Validates native search-start body shape before command dispatch.
 */
inline bool ValidateSearchStartBody(json &rBody, std::string &rErrorCode, std::string &rErrorMessage)
{
	if (!rBody.contains("query") || !rBody["query"].is_string()) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "query must be a string");
		return false;
	}

	std::string strQuery;
	std::string strError;
	if (!TryNormalizeSearchText(rBody["query"].get<std::string>(), "query", false, strQuery, strError)) {
		SetInvalidArgument(rErrorCode, rErrorMessage, strError);
		return false;
	}
	rBody["query"] = strQuery;

	if (rBody.contains("method")) {
		if (!rBody["method"].is_string()) {
			SetInvalidArgument(rErrorCode, rErrorMessage, "method must be a string");
			return false;
		}
		if (!IsSearchMethodName(rBody["method"].get_ref<const std::string&>())) {
			SetInvalidArgument(rErrorCode, rErrorMessage, kSearchMethodError);
			return false;
		}
	}

	if (rBody.contains("type")) {
		if (!rBody["type"].is_string()) {
			SetInvalidArgument(rErrorCode, rErrorMessage, "type must be a string");
			return false;
		}
		if (!IsSearchFileTypeName(rBody["type"].get_ref<const std::string&>())) {
			SetInvalidArgument(rErrorCode, rErrorMessage, "type is not supported");
			return false;
		}
	}

	if (rBody.contains("extension") && !rBody["extension"].is_string()) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "extension must be a string");
		return false;
	}

	uint64_t ullMinSize = 0;
	const bool bHasMinSize = rBody.contains("minSizeBytes");
	if (bHasMinSize) {
		if (!TryParseJsonUInt64(rBody["minSizeBytes"], ullMinSize)) {
			SetInvalidArgument(rErrorCode, rErrorMessage, "minSizeBytes must be an unsigned number");
			return false;
		}
		rBody["minSizeBytes"] = ullMinSize;
	}

	uint64_t ullMaxSize = 0;
	const bool bHasMaxSize = rBody.contains("maxSizeBytes");
	if (bHasMaxSize) {
		if (!TryParseJsonUInt64(rBody["maxSizeBytes"], ullMaxSize)) {
			SetInvalidArgument(rErrorCode, rErrorMessage, "maxSizeBytes must be an unsigned number");
			return false;
		}
		rBody["maxSizeBytes"] = ullMaxSize;
	}

	if (bHasMinSize && bHasMaxSize && ullMaxSize < ullMinSize) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "maxSizeBytes must be greater than or equal to minSizeBytes");
		return false;
	}

	if (rBody.contains("minAvailability")) {
		uint64_t ullMinAvailability = 0;
		if (!TryParseJsonUInt64(rBody["minAvailability"], ullMinAvailability) || ullMinAvailability > 1000000ui64) {
			SetInvalidArgument(rErrorCode, rErrorMessage, "minAvailability must be an unsigned number in the range 0..1000000");
			return false;
		}
		rBody["minAvailability"] = ullMinAvailability;
	}

	return true;
}

inline bool ValidateUnsignedPreferenceField(
	json &rBody,
	const char *pszFieldName,
	const char *pszErrorMessage,
	bool (*pfnIsValidValue)(uint64_t),
	std::string &rErrorCode,
	std::string &rErrorMessage)
{
	if (!rBody.contains(pszFieldName))
		return true;

	uint64_t ullValue = 0;
	if (!TryParseJsonUInt64(rBody[pszFieldName], ullValue) || (pfnIsValidValue != NULL && !pfnIsValidValue(ullValue))) {
		SetInvalidArgument(rErrorCode, rErrorMessage, pszErrorMessage);
		return false;
	}
	rBody[pszFieldName] = ullValue;
	return true;
}

inline bool ValidateBooleanPreferenceField(json &rBody, const char *pszFieldName, std::string &rErrorCode, std::string &rErrorMessage)
{
	if (rBody.contains(pszFieldName) && !rBody[pszFieldName].is_boolean()) {
		SetInvalidArgument(rErrorCode, rErrorMessage, std::string(pszFieldName) + " must be a boolean");
		return false;
	}
	return true;
}

/**
 * @brief Validates native app preferences PATCH body shape before dispatch.
 */
inline bool ValidatePreferencesPatchBody(json &rBody, std::string &rErrorCode, std::string &rErrorMessage)
{
	if (rBody.empty()) {
		SetInvalidArgument(rErrorCode, rErrorMessage, "preferences PATCH requires at least one preference");
		return false;
	}

	for (json::const_iterator it = rBody.begin(); it != rBody.end(); ++it) {
		if (WebApiSurfaceSeams::ParseMutablePreferenceName(it.key().c_str()) == WebApiSurfaceSeams::EMutablePreference::Invalid) {
			SetInvalidArgument(rErrorCode, rErrorMessage, "unsupported preference key: " + it.key());
			return false;
		}
	}

	for (const WebApiSurfaceSeams::SMutablePreferenceSpec &spec : WebApiSurfaceSeams::GetMutablePreferenceSpecs()) {
		if (spec.eKind == WebApiSurfaceSeams::EMutablePreferenceValueKind::Unsigned) {
			if (!ValidateUnsignedPreferenceField(rBody, spec.pszName, spec.pszErrorMessage, spec.pfnIsValidValue, rErrorCode, rErrorMessage))
				return false;
		} else if (!ValidateBooleanPreferenceField(rBody, spec.pszName, rErrorCode, rErrorMessage))
			return false;
	}

	return true;
}

inline bool RequireBooleanField(
	const json &rBody,
	const char *pszFieldName,
	const char *pszExpectedMessage,
	std::string &rErrorCode,
	std::string &rErrorMessage)
{
	if (pszFieldName == NULL || pszFieldName[0] == '\0')
		return true;
	if (!rBody.contains(pszFieldName) || !rBody[pszFieldName].is_boolean()) {
		SetInvalidArgument(rErrorCode, rErrorMessage, pszExpectedMessage != NULL ? pszExpectedMessage : std::string(pszFieldName) + " must be a boolean");
		return false;
	}
	return true;
}

inline bool RequireBooleanFieldTrue(
	const json &rBody,
	const char *pszFieldName,
	const char *pszExpectedMessage,
	std::string &rErrorCode,
	std::string &rErrorMessage)
{
	if (!RequireBooleanField(rBody, pszFieldName, pszExpectedMessage, rErrorCode, rErrorMessage))
		return false;
	if (!rBody[pszFieldName].get<bool>()) {
		SetInvalidArgument(rErrorCode, rErrorMessage, pszExpectedMessage != NULL ? pszExpectedMessage : std::string(pszFieldName) + " must be true");
		return false;
	}
	return true;
}

inline bool TryParseBooleanQueryValue(
	const std::string &rValue,
	const char *pszFieldName,
	bool &rValueOut,
	std::string &rErrorCode,
	std::string &rErrorMessage)
{
	if (rValue == "true") {
		rValueOut = true;
		return true;
	}
	if (rValue == "false") {
		rValueOut = false;
		return true;
	}
	SetInvalidArgument(rErrorCode, rErrorMessage, std::string(pszFieldName) + " must be true or false");
	return false;
}

inline bool RequireQueryBooleanTrue(
	const std::map<std::string, std::string> &rQuery,
	const char *pszFieldName,
	const char *pszExpectedMessage,
	std::string &rErrorCode,
	std::string &rErrorMessage)
{
	const std::map<std::string, std::string>::const_iterator it = rQuery.find(pszFieldName);
	if (it == rQuery.end()) {
		SetInvalidArgument(rErrorCode, rErrorMessage, pszExpectedMessage != NULL ? pszExpectedMessage : std::string(pszFieldName) + " must be true");
		return false;
	}
	bool bValue = false;
	if (!TryParseBooleanQueryValue(it->second, pszFieldName, bValue, rErrorCode, rErrorMessage))
		return false;
	if (!bValue) {
		SetInvalidArgument(rErrorCode, rErrorMessage, pszExpectedMessage != NULL ? pszExpectedMessage : std::string(pszFieldName) + " must be true");
		return false;
	}
	return true;
}

inline bool IsRouteSpec(const SApiRouteSpec &rSpec, const char *pszMethod, const char *pszPath)
{
	return rSpec.pszMethod != NULL
		&& rSpec.pszPathTemplate != NULL
		&& std::string(rSpec.pszMethod) == pszMethod
		&& std::string(rSpec.pszPathTemplate) == pszPath;
}

/**
 * @brief Applies explicit confirmation rules for destructive native REST routes.
 */
inline bool ValidateDestructiveConfirmationBody(const json &rBody, const SApiRouteSpec &rSpec, std::string &rErrorCode, std::string &rErrorMessage)
{
	if (IsRouteSpec(rSpec, "POST", "/app/shutdown"))
		return RequireBooleanFieldTrue(rBody, "confirmShutdown", "confirmShutdown must be true", rErrorCode, rErrorMessage);
	if (IsRouteSpec(rSpec, "POST", "/diagnostics/dumps"))
		return RequireBooleanFieldTrue(rBody, "confirmDump", "confirmDump must be true", rErrorCode, rErrorMessage);
	if (IsRouteSpec(rSpec, "POST", "/diagnostics/crash-tests"))
		return RequireBooleanFieldTrue(rBody, "confirmCrash", "confirmCrash must be true", rErrorCode, rErrorMessage);
	if (IsRouteSpec(rSpec, "POST", "/transfers/operations/clear-completed"))
		return RequireBooleanFieldTrue(rBody, "confirmClearCompleted", "confirmClearCompleted must be true", rErrorCode, rErrorMessage);
	if (IsRouteSpec(rSpec, "POST", "/logs/operations/clear"))
		return RequireBooleanFieldTrue(rBody, "confirmClearLogs", "confirmClearLogs must be true", rErrorCode, rErrorMessage);
	if (IsRouteSpec(rSpec, "PATCH", "/shared-directories"))
		return RequireBooleanFieldTrue(rBody, "confirmReplaceRoots", "confirmReplaceRoots must be true", rErrorCode, rErrorMessage);
	return true;
}

inline bool ValidateTemplateParameter(
	const std::string &rTemplateSegment,
	const std::string &rValue,
	std::string &rErrorCode,
	std::string &rErrorMessage)
{
	if (rTemplateSegment == "{hash}" || rTemplateSegment == "{userHash}") {
		if (!IsLowercaseMd4HexString(rValue)) {
			SetInvalidArgument(rErrorCode, rErrorMessage, rTemplateSegment.substr(1, rTemplateSegment.size() - 2) + " must be a 32-character lowercase hex string");
			return false;
		}
		return true;
	}

	if (rTemplateSegment == "{categoryId}")
		return TryValidateBoundedUnsignedDecimal(rValue, UINT_MAX, "categoryId", rErrorCode, rErrorMessage);

	if (rTemplateSegment == "{searchId}")
		return TryValidateBoundedUnsignedDecimal(rValue, UINT32_MAX, "searchId", rErrorCode, rErrorMessage);

	if (rTemplateSegment == "{serverId}") {
		json endpoint = json::object();
		if (!TryCopyEndpointToken(rValue, endpoint)) {
			SetInvalidArgument(rErrorCode, rErrorMessage, "serverId must use address:port with a port in the range 1..65535");
			return false;
		}
		return true;
	}

	if (rTemplateSegment == "{clientId}") {
		json endpoint = json::object();
		if (!IsLowercaseMd4HexString(rValue) && !TryCopyEndpointToken(rValue, endpoint)) {
			SetInvalidArgument(rErrorCode, rErrorMessage, "clientId must be a 32-character lowercase hex string or address:port");
			return false;
		}
		return true;
	}

	return true;
}

inline bool ValidatePathParameters(const std::string &rApiPath, const SApiRouteSpec &rSpec, std::string &rErrorCode, std::string &rErrorMessage)
{
	const std::vector<std::string> pathSegments = SplitPathSegments(rApiPath);
	const std::vector<std::string> templateSegments = SplitPathSegments(rSpec.pszPathTemplate != NULL ? rSpec.pszPathTemplate : "");
	if (pathSegments.size() != templateSegments.size())
		return true;

	for (size_t i = 0; i < pathSegments.size(); ++i) {
		if (IsTemplateParameter(templateSegments[i]) && !ValidateTemplateParameter(templateSegments[i], pathSegments[i], rErrorCode, rErrorMessage))
			return false;
	}
	return true;
}

inline bool ValidateRequestBodyFields(json &rBody, const SApiRouteSpec &rSpec, std::string &rErrorCode, std::string &rErrorMessage)
{
	for (json::const_iterator it = rBody.begin(); it != rBody.end(); ++it) {
		if (!HasToken(rSpec.pszBodyFields, it.key())) {
			SetInvalidArgument(rErrorCode, rErrorMessage, "unknown JSON field: " + it.key());
			return false;
		}
	}
	if ((rSpec.pszPathTemplate != NULL && (
		std::string(rSpec.pszPathTemplate) == "/transfers"
		|| std::string(rSpec.pszPathTemplate) == "/transfers/{hash}"
		|| std::string(rSpec.pszPathTemplate) == "/searches/{searchId}/results/{hash}/operations/download"))
		&& !ValidateCategorySelectorBody(rBody, rErrorCode, rErrorMessage))
		return false;
	if (rSpec.pszMethod != NULL
		&& rSpec.pszPathTemplate != NULL
		&& std::string(rSpec.pszMethod) == "POST"
		&& std::string(rSpec.pszPathTemplate) == "/categories"
		&& !ValidateCategoryCreateBody(rBody, rErrorCode, rErrorMessage))
	{
		return false;
	}
	if (rSpec.pszMethod != NULL
		&& rSpec.pszPathTemplate != NULL
		&& std::string(rSpec.pszMethod) == "PATCH"
		&& std::string(rSpec.pszPathTemplate) == "/categories/{categoryId}"
		&& !ValidateCategoryPatchBody(rBody, rErrorCode, rErrorMessage))
	{
		return false;
	}
	if (rSpec.pszMethod != NULL
		&& rSpec.pszPathTemplate != NULL
		&& std::string(rSpec.pszMethod) == "POST"
		&& std::string(rSpec.pszPathTemplate) == "/transfers"
		&& !ValidateTransferAddBody(rBody, rErrorCode, rErrorMessage))
	{
		return false;
	}
	if (rSpec.pszPathTemplate != NULL
		&& std::string(rSpec.pszPathTemplate) == "/searches/{searchId}/results/{hash}/operations/download"
		&& !ValidateOptionalPausedField(rBody, rErrorCode, rErrorMessage))
	{
		return false;
	}
	if (rSpec.pszMethod != NULL
		&& rSpec.pszPathTemplate != NULL
		&& std::string(rSpec.pszMethod) == "PATCH"
		&& std::string(rSpec.pszPathTemplate) == "/transfers/{hash}"
		&& !ValidateTransferPatchBody(rBody, rErrorCode, rErrorMessage))
	{
		return false;
	}
	if (rSpec.pszMethod != NULL
		&& rSpec.pszPathTemplate != NULL
		&& std::string(rSpec.pszMethod) == "PATCH"
		&& std::string(rSpec.pszPathTemplate) == "/shared-files/{hash}"
		&& !ValidateSharedFilePatchBody(rBody, rErrorCode, rErrorMessage))
	{
		return false;
	}
	if (rSpec.pszMethod != NULL
		&& rSpec.pszPathTemplate != NULL
		&& std::string(rSpec.pszMethod) == "POST"
		&& std::string(rSpec.pszPathTemplate) == "/shared-files"
		&& !ValidateSharedFileAddBody(rBody, rErrorCode, rErrorMessage))
	{
		return false;
	}
	if (rSpec.pszMethod != NULL
		&& rSpec.pszPathTemplate != NULL
		&& std::string(rSpec.pszMethod) == "PATCH"
		&& std::string(rSpec.pszPathTemplate) == "/shared-directories"
		&& !ValidateSharedDirectoriesPatchBody(rBody, rErrorCode, rErrorMessage))
	{
		return false;
	}
	if (rSpec.pszMethod != NULL
		&& rSpec.pszPathTemplate != NULL
		&& std::string(rSpec.pszMethod) == "POST"
		&& std::string(rSpec.pszPathTemplate) == "/servers"
		&& !ValidateServerCreateBody(rBody, rErrorCode, rErrorMessage))
	{
		return false;
	}
	if (rSpec.pszMethod != NULL
		&& rSpec.pszPathTemplate != NULL
		&& std::string(rSpec.pszMethod) == "PATCH"
		&& std::string(rSpec.pszPathTemplate) == "/servers/{serverId}"
		&& !ValidateServerPatchBody(rBody, rErrorCode, rErrorMessage))
	{
		return false;
	}
	if (rSpec.pszMethod != NULL
		&& rSpec.pszPathTemplate != NULL
		&& std::string(rSpec.pszMethod) == "POST"
		&& (std::string(rSpec.pszPathTemplate) == "/servers/operations/import-met-url"
			|| std::string(rSpec.pszPathTemplate) == "/kad/operations/import-nodes-url")
		&& !ValidateUrlImportBody(rBody, rErrorCode, rErrorMessage))
	{
		return false;
	}
	if (rSpec.pszMethod != NULL
		&& rSpec.pszPathTemplate != NULL
		&& std::string(rSpec.pszMethod) == "POST"
		&& std::string(rSpec.pszPathTemplate) == "/searches"
		&& !ValidateSearchStartBody(rBody, rErrorCode, rErrorMessage))
	{
		return false;
	}
	if (rSpec.pszMethod != NULL
		&& rSpec.pszPathTemplate != NULL
		&& std::string(rSpec.pszMethod) == "POST"
		&& std::string(rSpec.pszPathTemplate) == "/kad/operations/bootstrap"
		&& !ValidateKadBootstrapBody(rBody, rErrorCode, rErrorMessage))
	{
		return false;
	}
	if (rSpec.pszMethod != NULL
		&& rSpec.pszPathTemplate != NULL
		&& std::string(rSpec.pszMethod) == "PATCH"
		&& std::string(rSpec.pszPathTemplate) == "/app/preferences"
		&& !ValidatePreferencesPatchBody(rBody, rErrorCode, rErrorMessage))
	{
		return false;
	}
	if (rSpec.pszMethod != NULL
		&& rSpec.pszPathTemplate != NULL
		&& std::string(rSpec.pszMethod) == "POST"
		&& std::string(rSpec.pszPathTemplate) == "/friends"
		&& !ValidateFriendCreateBody(rBody, rErrorCode, rErrorMessage))
	{
		return false;
	}
	if (!ValidateDestructiveConfirmationBody(rBody, rSpec, rErrorCode, rErrorMessage))
		return false;
	return true;
}

inline bool ValidateQueryFields(const std::map<std::string, std::string> &rQuery, const SApiRouteSpec &rSpec, std::string &rErrorCode, std::string &rErrorMessage)
{
	for (std::map<std::string, std::string>::const_iterator it = rQuery.begin(); it != rQuery.end(); ++it) {
		if (!HasToken(rSpec.pszQueryFields, it->first)) {
			SetInvalidArgument(rErrorCode, rErrorMessage, "unknown query parameter: " + it->first);
			return false;
		}
		if (it->first == "limit" && !TryParseBoundedQueryUInt(it->second, 1, 1000, "limit", rErrorCode, rErrorMessage))
			return false;
		if (it->first == "offset" && !TryParseBoundedQueryUInt(it->second, 0, static_cast<uint64_t>(INT_MAX), "offset", rErrorCode, rErrorMessage))
			return false;
		if (it->first == "categoryId" && !TryParseBoundedQueryUInt(it->second, 0, static_cast<uint64_t>(UINT_MAX), "categoryId", rErrorCode, rErrorMessage))
			return false;
		if (it->first == "state" && !IsTransferStateName(it->second)) {
			SetInvalidArgument(rErrorCode, rErrorMessage, kTransferStateError);
			return false;
		}
		if (it->first == "confirm") {
			bool bIgnored = false;
			if (!TryParseBooleanQueryValue(it->second, "confirm", bIgnored, rErrorCode, rErrorMessage))
				return false;
		}
	}
	if (IsRouteSpec(rSpec, "DELETE", "/transfers/{hash}/files")
		|| IsRouteSpec(rSpec, "DELETE", "/shared-files/{hash}/file")
		|| IsRouteSpec(rSpec, "DELETE", "/searches"))
	{
		return RequireQueryBooleanTrue(rQuery, "confirm", "confirm must be true", rErrorCode, rErrorMessage);
	}
	return true;
}

/**
 * @brief Copies common collection query parameters into one command payload.
 */
inline void CopyTransferListQueryParams(const std::map<std::string, std::string> &rQuery, json &rParams)
{
	const auto itState = rQuery.find("state");
	if (itState != rQuery.end())
		rParams["state"] = itState->second;
	uint64_t ullCategory = 0;
	if (TryParseUnsignedQueryValue(rQuery, "categoryId", ullCategory))
		rParams["categoryId"] = ullCategory;
}

/**
 * @brief Copies the bounded log-tail query parameter into one command payload.
 */
inline void CopyLogQueryParams(const std::map<std::string, std::string> &rQuery, json &rParams)
{
	uint64_t ullLimit = 0;
	if (TryParseUnsignedQueryValue(rQuery, "limit", ullLimit))
		rParams["limit"] = ullLimit > INT_MAX ? INT_MAX : static_cast<int>(ullLimit);
}

inline void CopyPagingQueryParams(const std::map<std::string, std::string> &rQuery, json &rParams)
{
	uint64_t ullLimit = 0;
	if (TryParseUnsignedQueryValue(rQuery, "limit", ullLimit))
		rParams["_limit"] = ullLimit > 1000 ? 1000 : static_cast<int>(ullLimit);
	uint64_t ullOffset = 0;
	if (TryParseUnsignedQueryValue(rQuery, "offset", ullOffset))
		rParams["_offset"] = ullOffset > INT_MAX ? INT_MAX : static_cast<int>(ullOffset);
}

/**
 * @brief Copies a public client id token into the legacy selector payload.
 */
inline void CopyClientIdToken(const std::string &rClientId, json &rParams)
{
	if (rClientId.size() == 32)
		rParams["userHash"] = rClientId;
	else {
		json endpoint = json::object();
		if (TryCopyEndpointToken(rClientId, endpoint)) {
			rParams["ip"] = endpoint["addr"];
			rParams["port"] = endpoint["port"];
		}
	}
}

/**
 * @brief Marks list responses that should use the redesigned resource envelope.
 */
inline void RequestItemsEnvelope(json &rParams)
{
	rParams["_items_envelope"] = true;
}

/**
 * @brief Marks list responses that should expose public offset/limit metadata.
 */
inline void RequestPagedItemsEnvelope(json &rParams)
{
	RequestItemsEnvelope(rParams);
	rParams["_paged_items_envelope"] = true;
}

/**
 * @brief Parses one JSON request body and reports the stable REST error text
 * when parsing fails.
 */
inline bool TryParseRequestBody(const std::string &rRequestBody, json &rBody, std::string &rErrorMessage)
{
	rBody = json::object();
	if (rRequestBody.empty())
		return true;

	try {
		rBody = json::parse(rRequestBody);
		return true;
	} catch (const json::exception &rJsonError) {
		rErrorMessage = "invalid JSON body: ";
		rErrorMessage += rJsonError.what();
		return false;
	}
}

/**
 * @brief Maps the stable REST error codes onto HTTP status codes.
 */
inline int GetHttpStatusForError(const std::string &rCode)
{
	if (rCode == "INVALID_ARGUMENT")
		return 400;
	if (rCode == "UNAUTHORIZED")
		return 401;
	if (rCode == "METHOD_NOT_ALLOWED")
		return 405;
	if (rCode == "NOT_FOUND")
		return 404;
	if (rCode == "INVALID_STATE")
		return 409;
	if (rCode == "SERVICE_BUSY")
		return 503;
	if (rCode == "EMULE_UNAVAILABLE")
		return 503;
	return 500;
}

/**
 * @brief Validates the native REST API key without exposing whether a supplied
 * non-empty key was merely wrong.
 */
inline SApiAuthResult ValidateApiKey(const std::string &rConfiguredApiKey, const std::string &rPresentedApiKey)
{
	SApiAuthResult result;
	if (rConfiguredApiKey.empty()) {
		result.strErrorCode = "EMULE_UNAVAILABLE";
		result.strErrorMessage = "REST API key is not configured";
		return result;
	}

	if (rPresentedApiKey.empty() || rPresentedApiKey != rConfiguredApiKey) {
		result.strErrorCode = "UNAUTHORIZED";
		result.strErrorMessage = "missing or invalid X-API-Key";
		return result;
	}

	result.bAllowed = true;
	return result;
}

/**
 * @brief Builds the stable native REST JSON error envelope.
 */
inline json BuildErrorEnvelopeJson(
	const std::string &rCode,
	const std::string &rMessage,
	const json &rDetails = json::object())
{
	return json{
		{"error", json{
			{"code", rCode.empty() ? "EMULE_ERROR" : rCode},
			{"message", rMessage},
			{"details", rDetails.is_object() ? rDetails : json::object()}
		}}
	};
}

/**
 * @brief Builds one normalized REST route command from the raw HTTP method,
 * target, and body.
 */
inline bool TryBuildRoute(
	const std::string &rMethod,
	const std::string &rRequestTarget,
	const std::string &rRequestBody,
	SApiRoute &rRoute,
	std::string &rErrorCode,
	std::string &rErrorMessage,
	const std::string &rContentType = "application/json")
{
	rRoute.params = json::object();
	rErrorCode.clear();
	rErrorMessage.clear();

	const std::string strPath(GetRequestPath(rRequestTarget));
	std::vector<std::string> segments;
	if (!TrySplitPathSegments(strPath, segments, rErrorMessage)) {
		rErrorCode = "INVALID_ARGUMENT";
		return false;
	}
	if (segments.size() < 2 || ToLowerAscii(segments[0]) != "api" || ToLowerAscii(segments[1]) != "v1") {
		rErrorCode = "NOT_FOUND";
		rErrorMessage = "API route not found";
		return false;
	}
	const bool bGet = rMethod == "GET";
	const bool bPost = rMethod == "POST";
	const bool bPatch = rMethod == "PATCH";
	const bool bDelete = rMethod == "DELETE";
	if (!bGet && !bPost && !bPatch && !bDelete) {
		rErrorCode = "INVALID_ARGUMENT";
		rErrorMessage = "only GET, POST, PATCH, and DELETE are supported";
		return false;
	}

	const std::vector<std::string> route(segments.begin() + 2, segments.end());
	std::map<std::string, std::string> query;
	if (!TryParseQueryString(rRequestTarget, query, rErrorMessage)) {
		rErrorCode = "INVALID_ARGUMENT";
		return false;
	}

	rRoute.strMethod = rMethod;

	if (route.empty()) {
		rErrorCode = "NOT_FOUND";
		rErrorMessage = "API route not found";
		return false;
	}

	const std::string strApiPath(BuildRoutePathForSpec(route));
	const SApiRouteSpec *const pRouteSpec = FindRouteSpec(rMethod, strApiPath);
	if (pRouteSpec == NULL) {
		if (FindRouteSpecForAnyMethod(strApiPath) != NULL) {
			rErrorCode = "METHOD_NOT_ALLOWED";
			rErrorMessage = "HTTP method is not allowed for this API route";
			return false;
		}
		rErrorCode = "NOT_FOUND";
		rErrorMessage = "API route not found";
		return false;
	}
	rRoute.strPathTemplate = pRouteSpec->pszPathTemplate;
	if (!ValidatePathParameters(strApiPath, *pRouteSpec, rErrorCode, rErrorMessage))
		return false;
	if (!ValidateQueryFields(query, *pRouteSpec, rErrorCode, rErrorMessage))
		return false;
	if (bDelete && !TrimAsciiWhitespace(rRequestBody).empty()) {
		rErrorCode = "INVALID_ARGUMENT";
		rErrorMessage = "DELETE request bodies are not supported";
		return false;
	}
	if (!TryValidateRequestMetadata(rRequestBody, rContentType, rErrorCode, rErrorMessage))
		return false;
	json body;
	if (!TryParseRequestBody(rRequestBody, body, rErrorMessage)) {
		rErrorCode = "INVALID_ARGUMENT";
		return false;
	}
	if (!body.is_object()) {
		if (!rRequestBody.empty()) {
			rErrorCode = "INVALID_ARGUMENT";
			rErrorMessage = "JSON body must be an object";
			return false;
		}
		body = json::object();
	}
	if (!ValidateRequestBodyFields(body, *pRouteSpec, rErrorCode, rErrorMessage))
		return false;

	if (route.size() == 1 && route[0] == "app" && bGet) {
		rRoute.strCommand = "app/version";
		return true;
	}
	if (route.size() == 2 && route[0] == "app" && route[1] == "preferences") {
		if (bGet) {
			rRoute.strCommand = "app/preferences/get";
			return true;
		}
		if (bPatch) {
			rRoute.strCommand = "app/preferences/set";
			rRoute.params["prefs"] = body;
			return true;
		}
	}
	if (route.size() == 2 && route[0] == "app" && route[1] == "shutdown" && bPost) {
		rRoute.strCommand = "app/shutdown";
		return true;
	}
	if (route.size() == 2 && route[0] == "diagnostics" && route[1] == "dumps" && bPost) {
		rRoute.strCommand = "app/capture_dump";
		if (body.contains("fullMemory"))
			rRoute.params["fullMemory"] = body["fullMemory"];
		return true;
	}
	if (route.size() == 2 && route[0] == "diagnostics" && route[1] == "crash-tests" && bPost) {
		rRoute.strCommand = "app/crash_test";
		return true;
	}
	if (route.size() == 1 && route[0] == "status" && bGet) {
		rRoute.strCommand = "status/get";
		return true;
	}
	if (route.size() == 1 && route[0] == "stats" && bGet) {
		rRoute.strCommand = "stats/global";
		return true;
	}
	if (route.size() == 1 && route[0] == "snapshot" && bGet) {
		rRoute.strCommand = "snapshot/get";
		CopyLogQueryParams(query, rRoute.params);
		return true;
	}
	if (route.size() == 1 && route[0] == "categories" && bGet) {
		rRoute.strCommand = "categories/list";
		RequestItemsEnvelope(rRoute.params);
		return true;
	}
	if (route.size() == 1 && route[0] == "categories" && bPost) {
		rRoute.strCommand = "categories/create";
		rRoute.params = body;
		return true;
	}
	if (route.size() == 2 && route[0] == "categories" && bGet) {
		rRoute.strCommand = "categories/get";
		rRoute.params["id"] = route[1];
		return true;
	}
	if (route.size() == 2 && route[0] == "categories" && bPatch) {
		rRoute.strCommand = "categories/update";
		rRoute.params = body;
		rRoute.params["id"] = route[1];
		return true;
	}
	if (route.size() == 2 && route[0] == "categories" && bDelete) {
		rRoute.strCommand = "categories/delete";
		rRoute.params["id"] = route[1];
		return true;
	}
	if (route.size() == 1 && route[0] == "transfers" && bGet) {
		rRoute.strCommand = "transfers/list";
		CopyTransferListQueryParams(query, rRoute.params);
		CopyPagingQueryParams(query, rRoute.params);
		RequestPagedItemsEnvelope(rRoute.params);
		return true;
	}
	if (route.size() == 1 && route[0] == "transfers" && bPost) {
		rRoute.strCommand = "transfers/add";
		rRoute.params = body;
		return true;
	}
	if (route.size() == 3 && route[0] == "transfers" && route[1] == "operations" && route[2] == "clear-completed" && bPost) {
		rRoute.strCommand = "transfers/clear_completed";
		rRoute.params = body;
		return true;
	}
	if (route.size() == 2 && route[0] == "transfers" && bGet) {
		rRoute.strCommand = "transfers/get";
		rRoute.params["hash"] = route[1];
		return true;
	}
	if (route.size() == 4 && route[0] == "transfers" && route[2] == "operations" && bPost) {
		const std::string strOperation = route[3];
		if (strOperation == "pause" || strOperation == "resume" || strOperation == "stop") {
			rRoute.strCommand = "transfers/" + strOperation;
			rRoute.params = body;
			rRoute.params["hashes"] = json::array({route[1]});
			return true;
		}
		if (strOperation == "recheck" || strOperation == "preview") {
			rRoute.strCommand = "transfers/" + strOperation;
			rRoute.params = body;
			rRoute.params["hash"] = route[1];
			return true;
		}
		rErrorCode = "NOT_FOUND";
		rErrorMessage = "API route not found";
		return false;
	}
	if (route.size() == 2 && route[0] == "transfers" && bPatch) {
		if (body.contains("priority")) {
			rRoute.strCommand = "transfers/set_priority";
			rRoute.params = body;
			rRoute.params["hash"] = route[1];
			return true;
		}
		if (body.contains("categoryId") || body.contains("categoryName")) {
			rRoute.strCommand = "transfers/set_category";
			rRoute.params = body;
			rRoute.params["hash"] = route[1];
			return true;
		}
		if (body.contains("name")) {
			rRoute.strCommand = "transfers/rename";
			rRoute.params = body;
			rRoute.params["hash"] = route[1];
			return true;
		}
		rErrorCode = "INVALID_ARGUMENT";
		rErrorMessage = "transfer PATCH requires priority, categoryId, categoryName, or name";
		return false;
	}
	if (route.size() == 2 && route[0] == "transfers" && bDelete) {
		rRoute.strCommand = "transfers/delete";
		rRoute.params["deleteFiles"] = false;
		rRoute.params["hashes"] = json::array({route[1]});
		return true;
	}
	if (route.size() == 3 && route[0] == "transfers" && route[2] == "files" && bDelete) {
		rRoute.strCommand = "transfers/delete";
		rRoute.params["deleteFiles"] = true;
		rRoute.params["hashes"] = json::array({route[1]});
		return true;
	}
	if (route.size() == 3 && route[0] == "transfers" && route[2] == "sources" && bGet) {
		rRoute.strCommand = "transfers/sources";
		rRoute.params["hash"] = route[1];
		RequestItemsEnvelope(rRoute.params);
		return true;
	}
	if (route.size() == 4 && route[0] == "transfers" && route[2] == "sources" && bGet) {
		rRoute.strCommand = "transfers/source";
		rRoute.params["hash"] = route[1];
		CopyClientIdToken(route[3], rRoute.params);
		return true;
	}
	if (route.size() == 3 && route[0] == "transfers" && route[2] == "details" && bGet) {
		rRoute.strCommand = "transfers/details";
		rRoute.params["hash"] = route[1];
		return true;
	}
	if (route.size() == 6 && route[0] == "transfers" && route[2] == "sources" && route[4] == "operations" && route[5] == "browse" && bPost) {
		rRoute.strCommand = "transfers/source_browse";
		rRoute.params = body;
		rRoute.params["hash"] = route[1];
		CopyClientIdToken(route[3], rRoute.params);
		return true;
	}
	if (route.size() == 1 && route[0] == "uploads" && bGet) {
		rRoute.strCommand = "uploads/list";
		RequestItemsEnvelope(rRoute.params);
		return true;
	}
	if (route.size() == 2 && route[0] == "uploads" && bGet) {
		rRoute.strCommand = "uploads/get";
		CopyClientIdToken(route[1], rRoute.params);
		return true;
	}
	if (route.size() == 1 && route[0] == "upload-queue" && bGet) {
		rRoute.strCommand = "uploads/queue";
		CopyPagingQueryParams(query, rRoute.params);
		RequestPagedItemsEnvelope(rRoute.params);
		return true;
	}
	if (route.size() == 2 && route[0] == "upload-queue" && bGet) {
		rRoute.strCommand = "uploads/queue_get";
		CopyClientIdToken(route[1], rRoute.params);
		return true;
	}
	if (route.size() == 4 && route[0] == "uploads" && route[2] == "operations" && route[3] == "release-slot" && bPost) {
		rRoute.strCommand = "uploads/release_slot";
		rRoute.params = body;
		CopyClientIdToken(route[1], rRoute.params);
		return true;
	}
	if (route.size() == 4 && route[0] == "uploads" && route[2] == "operations" && bPost) {
		if (route[3] == "add-friend" || route[3] == "remove-friend" || route[3] == "ban" || route[3] == "unban") {
			rRoute.strCommand = "peers/" + route[3];
			rRoute.params = body;
			CopyClientIdToken(route[1], rRoute.params);
			return true;
		}
		if (route[3] == "remove") {
			rRoute.strCommand = "uploads/remove";
			rRoute.params = body;
			CopyClientIdToken(route[1], rRoute.params);
			return true;
		}
		rErrorCode = "NOT_FOUND";
		rErrorMessage = "API route not found";
		return false;
	}
	if (route.size() == 4 && route[0] == "upload-queue" && route[2] == "operations" && route[3] == "release-slot" && bPost) {
		rRoute.strCommand = "uploads/release_slot";
		rRoute.params = body;
		CopyClientIdToken(route[1], rRoute.params);
		return true;
	}
	if (route.size() == 4 && route[0] == "upload-queue" && route[2] == "operations" && bPost) {
		if (route[3] == "add-friend" || route[3] == "remove-friend" || route[3] == "ban" || route[3] == "unban") {
			rRoute.strCommand = "peers/" + route[3];
			rRoute.params = body;
			CopyClientIdToken(route[1], rRoute.params);
			return true;
		}
		if (route[3] == "remove") {
			rRoute.strCommand = "uploads/remove";
			rRoute.params = body;
			CopyClientIdToken(route[1], rRoute.params);
			return true;
		}
		rErrorCode = "NOT_FOUND";
		rErrorMessage = "API route not found";
		return false;
	}
	if (route.size() == 6 && route[0] == "transfers" && route[2] == "sources" && route[4] == "operations" && bPost) {
		if (route[5] == "add-friend" || route[5] == "remove-friend" || route[5] == "ban" || route[5] == "unban") {
			rRoute.strCommand = "peers/" + route[5];
			rRoute.params = body;
			rRoute.params["hash"] = route[1];
			CopyClientIdToken(route[3], rRoute.params);
			return true;
		}
		if (route[5] == "remove") {
			rRoute.strCommand = "peers/remove";
			rRoute.params = body;
			rRoute.params["hash"] = route[1];
			CopyClientIdToken(route[3], rRoute.params);
			return true;
		}
		if (route[5] == "release-slot") {
			rRoute.strCommand = "uploads/release_slot";
			rRoute.params = body;
			CopyClientIdToken(route[3], rRoute.params);
			return true;
		}
		rErrorCode = "NOT_FOUND";
		rErrorMessage = "API route not found";
		return false;
	}
	if (route.size() == 1 && route[0] == "servers" && bGet) {
		rRoute.strCommand = "servers/list";
		RequestItemsEnvelope(rRoute.params);
		return true;
	}
	if (route.size() == 1 && route[0] == "servers" && bPost) {
		rRoute.strCommand = "servers/add";
		rRoute.params = body;
		if (body.contains("addr")) {
			rErrorCode = "INVALID_ARGUMENT";
			rErrorMessage = "server create uses address, not addr";
			return false;
		}
		if (body.contains("address"))
			rRoute.params["addr"] = body["address"];
		return true;
	}
	if (route.size() == 3 && route[0] == "servers" && route[1] == "operations" && bPost) {
		if (route[2] == "import-met-url") {
			rRoute.strCommand = "servers/import_met_url";
			rRoute.params = body;
			return true;
		}
		if (route[2] == "connect" || route[2] == "disconnect") {
			rRoute.strCommand = "servers/" + route[2];
			rRoute.params = body;
			return true;
		}
		rErrorCode = "NOT_FOUND";
		rErrorMessage = "API route not found";
		return false;
	}
	if (route.size() == 4 && route[0] == "servers" && route[2] == "operations" && route[3] == "connect" && bPost) {
		rRoute.strCommand = "servers/connect";
		rRoute.params = body;
		if (!TryCopyEndpointToken(route[1], rRoute.params)) {
			rErrorCode = "INVALID_ARGUMENT";
			rErrorMessage = "server id must use address:port";
			return false;
		}
		return true;
	}
	if (route.size() == 2 && route[0] == "servers" && bGet) {
		rRoute.strCommand = "servers/get";
		if (!TryCopyEndpointToken(route[1], rRoute.params)) {
			rErrorCode = "INVALID_ARGUMENT";
			rErrorMessage = "server id must use address:port";
			return false;
		}
		return true;
	}
	if (route.size() == 2 && route[0] == "servers" && bPatch) {
		rRoute.params = body;
		if (!TryCopyEndpointToken(route[1], rRoute.params)) {
			rErrorCode = "INVALID_ARGUMENT";
			rErrorMessage = "server id must use address:port";
			return false;
		}
		rRoute.strCommand = "servers/update";
		return true;
	}
	if (route.size() == 2 && route[0] == "servers" && bDelete) {
		rRoute.strCommand = "servers/remove";
		rRoute.params = body;
		if (!TryCopyEndpointToken(route[1], rRoute.params)) {
			rErrorCode = "INVALID_ARGUMENT";
			rErrorMessage = "server id must use address:port";
			return false;
		}
		return true;
	}
	if (route.size() == 1 && route[0] == "kad" && bGet) {
		rRoute.strCommand = "kad/status";
		return true;
	}
	if (route.size() == 3 && route[0] == "kad" && route[1] == "operations" && bPost) {
		if (route[2] == "import-nodes-url") {
			rRoute.strCommand = "kad/import_nodes_url";
			rRoute.params = body;
			return true;
		}
		if (route[2] == "start") {
			rRoute.strCommand = "kad/connect";
			rRoute.params = body;
			return true;
		}
		if (route[2] == "bootstrap") {
			rRoute.strCommand = "kad/bootstrap";
			rRoute.params = body;
			return true;
		}
		if (route[2] == "stop") {
			rRoute.strCommand = "kad/disconnect";
			rRoute.params = body;
			return true;
		}
		if (route[2] == "recheck-firewall") {
			rRoute.strCommand = "kad/recheck_firewall";
			rRoute.params = body;
			return true;
		}
		rErrorCode = "NOT_FOUND";
		rErrorMessage = "API route not found";
		return false;
	}
	if (route.size() == 1 && route[0] == "shared-directories" && bGet) {
		rRoute.strCommand = "shared_directories/get";
		return true;
	}
	if (route.size() == 1 && route[0] == "shared-directories" && bPatch) {
		rRoute.strCommand = "shared_directories/set";
		rRoute.params = body;
		return true;
	}
	if (route.size() == 3 && route[0] == "shared-directories" && route[1] == "operations" && route[2] == "reload" && bPost) {
		rRoute.strCommand = "shared_directories/reload";
		return true;
	}
	if (route.size() == 1 && route[0] == "shared-files" && bGet) {
		rRoute.strCommand = "shared/list";
		CopyPagingQueryParams(query, rRoute.params);
		RequestPagedItemsEnvelope(rRoute.params);
		return true;
	}
	if (route.size() == 1 && route[0] == "shared-files" && bPost) {
		rRoute.strCommand = "shared/add";
		rRoute.params = body;
		return true;
	}
	if (route.size() == 3 && route[0] == "shared-files" && route[1] == "operations" && route[2] == "reload" && bPost) {
		rRoute.strCommand = "shared_directories/reload";
		return true;
	}
	if (route.size() == 2 && route[0] == "shared-files" && bPatch) {
		rRoute.strCommand = "shared/set_rating_comment";
		rRoute.params = body;
		rRoute.params["hash"] = route[1];
		return true;
	}
	if (route.size() == 3 && route[0] == "shared-files" && route[2] == "ed2k-link" && bGet) {
		rRoute.strCommand = "shared/ed2k_link";
		rRoute.params["hash"] = route[1];
		return true;
	}
	if (route.size() == 3 && route[0] == "shared-files" && route[2] == "comments" && bGet) {
		rRoute.strCommand = "shared/comments";
		rRoute.params["hash"] = route[1];
		RequestItemsEnvelope(rRoute.params);
		return true;
	}
	if (route.size() == 2 && route[0] == "shared-files" && bGet) {
		rRoute.strCommand = "shared/get";
		rRoute.params["hash"] = route[1];
		return true;
	}
	if (route.size() == 2 && route[0] == "shared-files" && bDelete) {
		rRoute.strCommand = "shared/remove";
		rRoute.params["deleteFiles"] = false;
		rRoute.params["hash"] = route[1];
		return true;
	}
	if (route.size() == 3 && route[0] == "shared-files" && route[2] == "file" && bDelete) {
		rRoute.strCommand = "shared/remove";
		rRoute.params["deleteFiles"] = true;
		rRoute.params["hash"] = route[1];
		return true;
	}
	if (route.size() == 1 && route[0] == "searches" && bPost) {
		rRoute.strCommand = "search/start";
		rRoute.params = body;
		return true;
	}
	if (route.size() == 1 && route[0] == "searches" && bGet) {
		rRoute.strCommand = "search/list";
		RequestItemsEnvelope(rRoute.params);
		return true;
	}
	if (route.size() == 1 && route[0] == "searches" && bDelete) {
		rRoute.strCommand = "search/clear";
		return true;
	}
	if (route.size() == 6 && route[0] == "searches" && route[2] == "results" && route[4] == "operations" && route[5] == "download" && bPost) {
		rRoute.strCommand = "search/download_result";
		rRoute.params = body;
		rRoute.params["searchId"] = route[1];
		rRoute.params["hash"] = route[3];
		return true;
	}
	if (route.size() == 2 && route[0] == "searches" && bGet) {
		rRoute.strCommand = "search/results";
		rRoute.params["searchId"] = route[1];
		CopyPagingQueryParams(query, rRoute.params);
		return true;
	}
	if (route.size() == 2 && route[0] == "searches" && bDelete) {
		rRoute.strCommand = "search/stop";
		rRoute.params = body;
		rRoute.params["searchId"] = route[1];
		return true;
	}
	if (route.size() == 1 && route[0] == "friends" && bGet) {
		rRoute.strCommand = "friends/list";
		RequestItemsEnvelope(rRoute.params);
		return true;
	}
	if (route.size() == 1 && route[0] == "friends" && bPost) {
		rRoute.strCommand = "friends/add";
		rRoute.params = body;
		return true;
	}
	if (route.size() == 2 && route[0] == "friends" && bDelete) {
		rRoute.strCommand = "friends/remove";
		rRoute.params["userHash"] = route[1];
		return true;
	}
	if (route.size() == 1 && route[0] == "logs" && bGet) {
		rRoute.strCommand = "log/get";
		CopyLogQueryParams(query, rRoute.params);
		RequestItemsEnvelope(rRoute.params);
		return true;
	}
	if (route.size() == 3 && route[0] == "logs" && route[1] == "operations" && route[2] == "clear" && bPost) {
		rRoute.strCommand = "log/clear";
		rRoute.params = body;
		return true;
	}

	rErrorCode = "NOT_FOUND";
	rErrorMessage = "API route not found";
	return false;
}
}
