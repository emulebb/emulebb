#pragma once

#include "DialogInputParsingSeams.h"

namespace AddSourceInputSeams
{
struct SourceClientInput
{
	bool Valid = false;
	uint32_t NetworkOrderAddress = 0;
	uint16_t Port = 0;
	bool AddressContainedPort = false;
};

struct UrlSourceInput
{
	bool Valid = false;
	CString Url;
	CString Scheme;
	CString HostName;
};

/**
 * @brief Builds a validated source-client endpoint from IP and port dialog fields.
 */
inline SourceClientInput ParseSourceClientInput(const CString& strAddressText, const CString& strPortText)
{
	SourceClientInput input;
	uint16_t uEmbeddedPort = 0;
	if (!DialogInputParsingSeams::TryParseIPv4AddressAndOptionalPort(strAddressText, input.NetworkOrderAddress, uEmbeddedPort, input.AddressContainedPort))
		return input;

	if (input.AddressContainedPort)
		input.Port = uEmbeddedPort;
	else if (!DialogInputParsingSeams::TryParseTcpPort(strPortText, input.Port))
		return input;

	input.Valid = true;
	return input;
}

/**
 * @brief Builds a validated unresolved URL source request from dialog text.
 */
inline UrlSourceInput ParseUrlSourceInput(CString strUrl)
{
	UrlSourceInput input;
	strUrl.Trim();
	input.Url = strUrl;
	if (!DialogInputParsingSeams::TryExtractAbsoluteUrlHost(strUrl, input.Scheme, input.HostName))
		return input;
	if (!DialogInputParsingSeams::IsSupportedDownloadUrlScheme(input.Scheme))
		return input;

	input.Valid = true;
	return input;
}
}
