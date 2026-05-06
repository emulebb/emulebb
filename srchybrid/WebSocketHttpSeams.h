#pragma once

#include <cstdint>
#include <string>

#include "WebServerJsonSeams.h"

/**
 * @brief Pure HTTP request parsing helpers used before REST and compatibility
 * dispatch.
 */
namespace WebSocketHttpSeams
{
static const uint64_t kMaxHttpContentLength = 16ui64 * 1024ui64 * 1024ui64;

enum class EContentLengthHeader
{
	NotContentLength,
	Valid,
	Invalid
};

/**
 * @brief Reports whether one HTTP method token is supported by the web
 * dispatcher.
 */
inline bool IsSupportedDispatchMethod(const std::string &rMethod)
{
	return rMethod == "GET" || rMethod == "POST" || rMethod == "PATCH" || rMethod == "DELETE";
}

/**
 * @brief Parses the method and request target from one HTTP request header.
 */
inline bool TryParseRequestLine(const std::string &rHeader, std::string &rMethod, std::string &rRequestTarget)
{
	rMethod.clear();
	rRequestTarget.clear();

	const std::string::size_type uLineEnd = rHeader.find('\n');
	std::string strLine(rHeader.substr(0, uLineEnd == std::string::npos ? std::string::npos : uLineEnd));
	while (!strLine.empty() && strLine[strLine.size() - 1] == '\r')
		strLine.erase(strLine.size() - 1);

	const std::string::size_type uFirstSpace = strLine.find(' ');
	if (uFirstSpace == std::string::npos || uFirstSpace == 0)
		return false;
	const std::string::size_type uSecondSpace = strLine.find(' ', uFirstSpace + 1);
	if (uSecondSpace == std::string::npos || uSecondSpace <= uFirstSpace + 1)
		return false;

	rMethod = strLine.substr(0, uFirstSpace);
	rRequestTarget = strLine.substr(uFirstSpace + 1, uSecondSpace - uFirstSpace - 1);
	return !rMethod.empty() && !rRequestTarget.empty();
}

/**
 * @brief Strictly parses one Content-Length field value.
 */
inline bool TryParseContentLengthValue(const std::string &rValue, uint32_t &ruContentLength)
{
	ruContentLength = 0;
	uint64_t ullContentLength = 0;
	if (!WebServerJsonSeams::TryParseUnsignedDecimalValue(WebServerJsonSeams::TrimAsciiWhitespace(rValue), ullContentLength))
		return false;
	if (ullContentLength > kMaxHttpContentLength)
		return false;
	ruContentLength = static_cast<uint32_t>(ullContentLength);
	return true;
}

/**
 * @brief Classifies and parses one raw HTTP header line.
 */
inline EContentLengthHeader ParseContentLengthHeaderLine(const std::string &rLine, uint32_t &ruContentLength)
{
	ruContentLength = 0;
	std::string strLine(rLine);
	while (!strLine.empty() && (strLine[strLine.size() - 1] == '\r' || strLine[strLine.size() - 1] == '\n'))
		strLine.erase(strLine.size() - 1);

	const std::string::size_type uColon = strLine.find(':');
	if (uColon == std::string::npos)
		return EContentLengthHeader::NotContentLength;

	const std::string strName(WebServerJsonSeams::ToLowerAscii(WebServerJsonSeams::TrimAsciiWhitespace(strLine.substr(0, uColon))));
	if (strName != "content-length")
		return EContentLengthHeader::NotContentLength;

	return TryParseContentLengthValue(strLine.substr(uColon + 1), ruContentLength)
		? EContentLengthHeader::Valid
		: EContentLengthHeader::Invalid;
}

/**
 * @brief Scans one complete HTTP header block for a single valid
 * Content-Length field.
 */
inline bool TryParseContentLengthHeaders(
	const std::string &rHeader,
	bool &rbHasContentLength,
	uint32_t &ruContentLength)
{
	rbHasContentLength = false;
	ruContentLength = 0;

	for (std::string::size_type uPos = 0; uPos < rHeader.size();) {
		const std::string::size_type uLineEnd = rHeader.find('\n', uPos);
		const std::string strLine(rHeader.substr(uPos, uLineEnd == std::string::npos ? std::string::npos : uLineEnd - uPos));

		uint32_t uParsedContentLength = 0;
		const EContentLengthHeader eContentLength = ParseContentLengthHeaderLine(strLine, uParsedContentLength);
		if (eContentLength == EContentLengthHeader::Invalid)
			return false;
		if (eContentLength == EContentLengthHeader::Valid) {
			if (rbHasContentLength)
				return false;
			rbHasContentLength = true;
			ruContentLength = uParsedContentLength;
		}

		if (uLineEnd == std::string::npos)
			break;
		uPos = uLineEnd + 1;
	}

	return true;
}
}
