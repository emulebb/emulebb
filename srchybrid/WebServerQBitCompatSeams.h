#pragma once

#include <cctype>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>

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

inline bool IsQBitRequestTarget(const std::string &rRequestTarget)
{
	const std::string strPathLower(WebServerJsonSeams::ToLowerAscii(WebServerJsonSeams::GetRequestPath(rRequestTarget)));
	return strPathLower == "/api/v2" || strPathLower.rfind("/api/v2/", 0) == 0;
}

inline char HexDigit(const unsigned char value)
{
	return static_cast<char>(value < 10 ? ('0' + value) : ('A' + (value - 10)));
}

/**
 * @brief URL-encodes UTF-8 text for generated eD2K file links.
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
		<< UrlEncodeUtf8(nameIt->second)
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

	const auto urlIt = form.find("urls");
	if (urlIt == form.end() || urlIt->second.empty()) {
		rErrorMessage = "urls form field is required";
		return false;
	}

	if (!TryBuildEd2kLinkFromMagnet(urlIt->second, rRequest.strUrl, rErrorMessage))
		return false;

	const auto categoryIt = form.find("category");
	rRequest.strCategory = categoryIt == form.end() ? std::string() : categoryIt->second;

	const auto stoppedIt = form.find("stopped");
	const auto pausedIt = form.find("paused");
	rRequest.bPaused = (stoppedIt != form.end() && IsTruthyFormValue(stoppedIt->second))
		|| (pausedIt != form.end() && IsTruthyFormValue(pausedIt->second));
	return true;
}
}
