#pragma once

#include <Windows.h>
#include <atlstr.h>
#include <tchar.h>

#include <cstdint>
#include <vector>

namespace DialogInputParsingSeams
{
/**
 * @brief Parses a trimmed unsigned decimal value without accepting partial input.
 */
inline bool TryParseUnsignedDecimal(CString strText, unsigned& uValue)
{
	strText.Trim();
	if (strText.IsEmpty())
		return false;

	unsigned uParsed = 0;
	for (int i = 0; i < strText.GetLength(); ++i) {
		const TCHAR ch = strText[i];
		if (ch < _T('0') || ch > _T('9'))
			return false;

		const unsigned uDigit = static_cast<unsigned>(ch - _T('0'));
		if (uParsed > (static_cast<unsigned>(-1) - uDigit) / 10u)
			return false;
		uParsed = uParsed * 10u + uDigit;
	}

	uValue = uParsed;
	return true;
}

/**
 * @brief Parses a user-entered TCP port and rejects zero, overflow, and partial text.
 */
inline bool TryParseTcpPort(const CString& strText, uint16_t& uPort)
{
	unsigned uParsed = 0;
	if (!TryParseUnsignedDecimal(strText, uParsed) || uParsed == 0 || uParsed > 65535u)
		return false;

	uPort = static_cast<uint16_t>(uParsed);
	return true;
}

/**
 * @brief Parses a dotted IPv4 address into the same network-order value returned by inet_addr.
 */
inline bool TryParseIPv4Address(CString strText, uint32_t& uNetworkOrderAddress)
{
	strText.Trim();
	if (strText.IsEmpty())
		return false;

	unsigned uParts[4] = {};
	int iStart = 0;
	for (int iPart = 0; iPart < 4; ++iPart) {
		int iDot = strText.Find(_T('.'), iStart);
		if (iPart < 3) {
			if (iDot < 0)
				return false;
		} else if (iDot >= 0)
			return false;

		const int iEnd = iDot >= 0 ? iDot : strText.GetLength();
		CString strPart(strText.Mid(iStart, iEnd - iStart));
		unsigned uPart = 0;
		if (!TryParseUnsignedDecimal(strPart, uPart) || uPart > 255u)
			return false;

		uParts[iPart] = uPart;
		iStart = iEnd + 1;
	}

	uNetworkOrderAddress = uParts[0] | (uParts[1] << 8u) | (uParts[2] << 16u) | (uParts[3] << 24u);
	return true;
}

/**
 * @brief Parses an IPv4 address that may carry a trailing :port suffix.
 */
inline bool TryParseIPv4AddressAndOptionalPort(CString strText, uint32_t& uNetworkOrderAddress, uint16_t& uPort, bool& bHasPort)
{
	strText.Trim();
	bHasPort = false;
	uPort = 0;

	const int iColon = strText.ReverseFind(_T(':'));
	if (iColon >= 0) {
		CString strPort(strText.Mid(iColon + 1));
		if (!TryParseTcpPort(strPort, uPort))
			return false;
		strText.Truncate(iColon);
		strText.TrimRight();
		bHasPort = true;
	}

	return TryParseIPv4Address(strText, uNetworkOrderAddress);
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
