#pragma once

#include <Windows.h>
#include <atlstr.h>
#include <tchar.h>

#include <cstdint>
#include <vector>

#include "IPv4AddressSeams.h"

namespace DialogInputParsingSeams
{
/**
 * @brief Parses a trimmed unsigned decimal value without accepting partial input.
 */
inline bool TryParseUnsignedDecimal(CString strText, unsigned& uValue)
{
	return IPv4AddressSeams::TryParseUnsignedDecimal(strText, uValue);
}

/**
 * @brief Parses a user-entered TCP port and rejects zero, overflow, and partial text.
 */
inline bool TryParseTcpPort(const CString& strText, uint16_t& uPort)
{
	return IPv4AddressSeams::TryParseTcpPort(strText, uPort);
}

/**
 * @brief Parses a dotted IPv4 address into a network-order scalar.
 */
inline bool TryParseIPv4Address(CString strText, uint32_t& uNetworkOrderAddress)
{
	return IPv4AddressSeams::TryParseIPv4Address(strText, uNetworkOrderAddress);
}

/**
 * @brief Parses an IPv4 address that may carry a trailing :port suffix.
 */
inline bool TryParseIPv4AddressAndOptionalPort(CString strText, uint32_t& uNetworkOrderAddress, uint16_t& uPort, bool& bHasPort)
{
	return IPv4AddressSeams::TryParseIPv4AddressAndOptionalPort(strText, uNetworkOrderAddress, uPort, bHasPort);
}

/**
 * @brief Splits a whitespace-delimited multiline edit value into non-empty tokens.
 */
inline std::vector<CString> TokenizeWhitespaceSeparatedText(CString strText)
{
	std::vector<CString> tokens;
	for (int iPos = 0; iPos >= 0;) {
		CString strToken(strText.Tokenize(_T(" \t\r\n"), iPos));
		if (strToken.IsEmpty())
			break;
		tokens.push_back(strToken);
	}
	return tokens;
}

/**
 * @brief Extracts the scheme and host from an absolute URL without accepting empty authorities.
 */
inline bool TryExtractAbsoluteUrlHost(CString strUrl, CString& strScheme, CString& strHost)
{
	strUrl.Trim();
	strScheme.Empty();
	strHost.Empty();

	const int iSchemeEnd = strUrl.Find(_T("://"));
	if (iSchemeEnd <= 0)
		return false;

	strScheme = strUrl.Left(iSchemeEnd);
	for (int i = 0; i < strScheme.GetLength(); ++i) {
		const TCHAR ch = strScheme[i];
		const bool bAlpha = (ch >= _T('a') && ch <= _T('z')) || (ch >= _T('A') && ch <= _T('Z'));
		const bool bRest = i > 0 && ((ch >= _T('0') && ch <= _T('9')) || ch == _T('+') || ch == _T('-') || ch == _T('.'));
		if (!bAlpha && !bRest)
			return false;
	}

	const int iAuthorityStart = iSchemeEnd + 3;
	int iAuthorityEnd = strUrl.GetLength();
	for (int i = iAuthorityStart; i < strUrl.GetLength(); ++i) {
		const TCHAR ch = strUrl[i];
		if (ch == _T('/') || ch == _T('\\') || ch == _T('?') || ch == _T('#')) {
			iAuthorityEnd = i;
			break;
		}
	}

	CString strAuthority(strUrl.Mid(iAuthorityStart, iAuthorityEnd - iAuthorityStart));
	strAuthority.Trim();
	const int iAt = strAuthority.ReverseFind(_T('@'));
	if (iAt >= 0)
		strAuthority = strAuthority.Mid(iAt + 1);

	if (strAuthority.IsEmpty())
		return false;

	if (strAuthority[0] == _T('[')) {
		const int iClose = strAuthority.Find(_T(']'));
		if (iClose <= 1)
			return false;
		strHost = strAuthority.Mid(1, iClose - 1);
	} else {
		const int iPort = strAuthority.Find(_T(':'));
		strHost = iPort >= 0 ? strAuthority.Left(iPort) : strAuthority;
	}

	strHost.Trim();
	return !strHost.IsEmpty();
}

/**
 * @brief Returns whether the scheme is a supported download/update transport.
 */
inline bool IsSupportedDownloadUrlScheme(const CString& strScheme)
{
	return strScheme.CompareNoCase(_T("http")) == 0
		|| strScheme.CompareNoCase(_T("https")) == 0
		|| strScheme.CompareNoCase(_T("ftp")) == 0;
}
}
