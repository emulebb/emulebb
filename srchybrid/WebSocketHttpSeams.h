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
}
