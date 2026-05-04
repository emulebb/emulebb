#pragma once

#include <cctype>
#include <cstdlib>
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

inline const std::vector<SQBitRouteSpec> &GetQBitRouteSpecs()
{
	static const std::vector<SQBitRouteSpec> specs = {
		{"get", "/api/v2/app/webapiversion", false},
		{"post", "/api/v2/auth/login", false},
		{"get", "/api/v2/app/version", true},
		{"get", "/api/v2/app/preferences", true},
		{"get", "/api/v2/torrents/categories", true},
		{"post", "/api/v2/torrents/createcategory", true},
		{"get", "/api/v2/torrents/info", true},
		{"get", "/api/v2/torrents/properties", true},
		{"get", "/api/v2/torrents/files", true},
		{"post", "/api/v2/torrents/add", true},
		{"post", "/api/v2/torrents/delete", true},
		{"post", "/api/v2/torrents/setcategory", true},
		{"post", "/api/v2/torrents/pause", true},
		{"post", "/api/v2/torrents/stop", true},
		{"post", "/api/v2/torrents/resume", true},
		{"post", "/api/v2/torrents/start", true},
		{"post", "/api/v2/torrents/setsharelimits", true},
		{"post", "/api/v2/torrents/topprio", true},
		{"post", "/api/v2/torrents/setforcestart", true},
	};
	return specs;
}

inline const SQBitRouteSpec *FindQBitRouteSpec(const std::string &rMethodLower, const std::string &rPathLower)
{
	const std::vector<SQBitRouteSpec> &specs = GetQBitRouteSpecs();
	for (size_t i = 0; i < specs.size(); ++i) {
		if (rMethodLower == specs[i].pszMethod && rPathLower == specs[i].pszPath)
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
	rForm.clear();
	size_t uPos = 0;
	while (uPos <= rBody.size()) {
		const std::string::size_type uAmp = rBody.find('&', uPos);
		const std::string token = rBody.substr(
			uPos,
			uAmp == std::string::npos ? std::string::npos : (uAmp - uPos));
		if (!token.empty()) {
			const std::string::size_type uEquals = token.find('=');
			const std::string strName(WebServerJsonSeams::UrlDecodeUtf8(token.substr(0, uEquals)));
			const std::string strValue(uEquals == std::string::npos ? std::string() : WebServerJsonSeams::UrlDecodeUtf8(token.substr(uEquals + 1)));
			if (strName.empty()) {
				rErrorMessage = "form field name must not be empty";
				return false;
			}
			if (rForm.find(strName) != rForm.end()) {
				rErrorMessage = "duplicate form field: " + strName;
				return false;
			}
			rForm[strName] = strValue;
		}

		if (uAmp == std::string::npos)
			break;
		uPos = uAmp + 1;
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

	const std::string strXtLower(WebServerJsonSeams::ToLowerAscii(xtIt->second));
	const std::string strBtihPrefix("urn:btih:");
	if (strXtLower.rfind(strBtihPrefix, 0) != 0 || !IsQBitWrappedEd2kBtih(strXtLower.substr(strBtihPrefix.size()))) {
		rErrorMessage = "magnet btih does not carry an eD2K hash";
		return false;
	}
	if (nameIt->second.empty()) {
		rErrorMessage = "magnet display name must not be empty";
		return false;
	}
	if (!WebServerJsonSeams::IsValidUnsignedDecimal(sizeIt->second)) {
		rErrorMessage = "magnet size must be an unsigned decimal value";
		return false;
	}

	const unsigned long long ullSize = std::strtoull(sizeIt->second.c_str(), NULL, 10);
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

	const auto categoryIt = form.find("category");
	rRequest.strCategory = categoryIt == form.end() ? std::string() : categoryIt->second;

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
	const auto deleteIt = form.find("deleteFiles");
	rRequest.bDeleteFiles = deleteIt != form.end() && IsTruthyFormValue(deleteIt->second);
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
	const auto categoryIt = form.find("category");
	if (categoryIt == form.end()) {
		rErrorMessage = "category form field is required";
		return false;
	}
	rRequest.strCategory = categoryIt->second;
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
