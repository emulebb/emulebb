#pragma once

#include "DialogInputParsingSeams.h"

namespace ServerInputSeams
{
struct ManualServerInput
{
	bool Valid = false;
	CString Address;
	uint16_t Port = 0;
	CString Name;
};

struct ServerMetUrlInput
{
	bool Valid = false;
	CString Url;
	CString Scheme;
	CString HostName;
};

/**
 * @brief Normalizes a server address resolved from either manual text or an eD2K server link.
 */
inline ManualServerInput BuildManualServerInput(CString strAddress, const uint16_t uPort, CString strName)
{
	ManualServerInput input;
	strAddress.Trim();
	strName.Trim();
	if (strAddress.IsEmpty() || uPort == 0)
		return input;

	input.Valid = true;
	input.Address = strAddress;
	input.Port = uPort;
	input.Name = strName;
	return input;
}

/**
 * @brief Parses manual server dialog text into a validated add-server request.
 */
inline ManualServerInput ParseManualServerInput(const CString& strAddressText, const CString& strPortText, const CString& strNameText)
{
	uint16_t uPort = 0;
	if (!DialogInputParsingSeams::TryParseTcpPort(strPortText, uPort))
		return ManualServerInput();

	return BuildManualServerInput(strAddressText, uPort, strNameText);
}

/**
 * @brief Validates a server.met update URL before it enters download history or network code.
 */
inline ServerMetUrlInput ParseServerMetUrlInput(CString strUrl)
{
	ServerMetUrlInput input;
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
