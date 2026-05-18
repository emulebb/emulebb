#pragma once

#include <atlstr.h>
#include <cstdint>

namespace IPv4AddressSeams
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
		unsigned uPart = 0;
		if (!TryParseUnsignedDecimal(strText.Mid(iStart, iEnd - iStart), uPart) || uPart > 255u)
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
 * @brief Formats a network-order IPv4 scalar as dotted decimal text.
 */
inline CString FormatIPv4Address(uint32_t uNetworkOrderAddress)
{
	CString strAddress;
	strAddress.Format(_T("%u.%u.%u.%u"),
		static_cast<unsigned>(uNetworkOrderAddress & 0xffu),
		static_cast<unsigned>((uNetworkOrderAddress >> 8u) & 0xffu),
		static_cast<unsigned>((uNetworkOrderAddress >> 16u) & 0xffu),
		static_cast<unsigned>((uNetworkOrderAddress >> 24u) & 0xffu));
	return strAddress;
}
}
