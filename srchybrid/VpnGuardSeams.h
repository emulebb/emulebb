#pragma once

#include "IPv4AddressSeams.h"

#include <vector>

namespace VpnGuardSeams
{
enum class EMode
{
	Off,
	Block
};

struct SAllowedPublicIpv4Range
{
	uint32_t uNetworkOrderAddress;
	uint8_t uPrefixLength;
};

inline LPCTSTR GetModePreferenceText(EMode eMode)
{
	return eMode == EMode::Block ? _T("Block") : _T("Off");
}

inline EMode ParseModePreferenceText(CString strText)
{
	strText.Trim();
	return strText.CompareNoCase(_T("Block")) == 0 ? EMode::Block : EMode::Off;
}

inline uint32_t ToComparableIpv4(uint32_t uNetworkOrderAddress)
{
	return ((uNetworkOrderAddress & 0x000000ffu) << 24u)
		| ((uNetworkOrderAddress & 0x0000ff00u) << 8u)
		| ((uNetworkOrderAddress & 0x00ff0000u) >> 8u)
		| ((uNetworkOrderAddress & 0xff000000u) >> 24u);
}

inline bool Ipv4RangesOverlap(uint32_t uFirstBase, uint8_t uFirstPrefix, uint32_t uSecondBase, uint8_t uSecondPrefix)
{
	const uint8_t uSharedPrefix = uFirstPrefix < uSecondPrefix ? uFirstPrefix : uSecondPrefix;
	const uint32_t uMask = uSharedPrefix == 0 ? 0u : (0xffffffffu << (32u - uSharedPrefix));
	return (uFirstBase & uMask) == (uSecondBase & uMask);
}

inline bool IsPublicIpv4Address(uint32_t uNetworkOrderAddress)
{
	const uint32_t uAddress = ToComparableIpv4(uNetworkOrderAddress);
	const auto inRange = [uAddress](uint32_t uBase, uint8_t uPrefix) {
		const uint32_t uMask = uPrefix == 0 ? 0u : (0xffffffffu << (32u - uPrefix));
		return (uAddress & uMask) == (uBase & uMask);
	};

	if (uAddress == 0xffffffffu)
		return false;
	if (inRange(0x00000000u, 8))
		return false;
	if (inRange(0x0a000000u, 8))
		return false;
	if (inRange(0x64400000u, 10))
		return false;
	if (inRange(0x7f000000u, 8))
		return false;
	if (inRange(0xa9fe0000u, 16))
		return false;
	if (inRange(0xac100000u, 12))
		return false;
	if (inRange(0xc0000000u, 24))
		return false;
	if (inRange(0xc0000200u, 24))
		return false;
	if (inRange(0xc0a80000u, 16))
		return false;
	if (inRange(0xc6120000u, 15))
		return false;
	if (inRange(0xc6336400u, 24))
		return false;
	if (inRange(0xcb007100u, 24))
		return false;
	if (inRange(0xe0000000u, 4))
		return false;
	return true;
}

inline bool IsPublicIpv4RangeOnly(uint32_t uNetworkOrderAddress, uint8_t uPrefixLength)
{
	const uint32_t uBase = ToComparableIpv4(uNetworkOrderAddress);
	static const struct
	{
		uint32_t uBase;
		uint8_t uPrefix;
	} kNonPublicRanges[] = {
		{0x00000000u, 8},
		{0x0a000000u, 8},
		{0x64400000u, 10},
		{0x7f000000u, 8},
		{0xa9fe0000u, 16},
		{0xac100000u, 12},
		{0xc0000000u, 24},
		{0xc0000200u, 24},
		{0xc0a80000u, 16},
		{0xc6120000u, 15},
		{0xc6336400u, 24},
		{0xcb007100u, 24},
		{0xe0000000u, 4},
		{0xffffffffu, 32},
	};
	for (const auto& range : kNonPublicRanges) {
		if (Ipv4RangesOverlap(uBase, uPrefixLength, range.uBase, range.uPrefix))
			return false;
	}
	return true;
}

inline bool TryParseAllowedPublicIpv4Range(CString strText, SAllowedPublicIpv4Range& rRange, CString& rstrError)
{
	strText.Trim();
	rstrError.Empty();
	if (strText.IsEmpty()) {
		rstrError = _T("empty range");
		return false;
	}

	CString strAddress(strText);
	unsigned uPrefix = 32;
	const int iSlash = strText.Find(_T('/'));
	if (iSlash >= 0) {
		strAddress = strText.Left(iSlash);
		CString strPrefix(strText.Mid(iSlash + 1));
		if (!IPv4AddressSeams::TryParseUnsignedDecimal(strPrefix, uPrefix) || uPrefix > 32u) {
			rstrError.Format(_T("invalid prefix length in %s"), (LPCTSTR)strText);
			return false;
		}
	}

	uint32_t uAddress = 0;
	if (!IPv4AddressSeams::TryParseIPv4Address(strAddress, uAddress)) {
		rstrError.Format(_T("invalid IPv4 address in %s"), (LPCTSTR)strText);
		return false;
	}
	if (!IsPublicIpv4Address(uAddress) || !IsPublicIpv4RangeOnly(uAddress, static_cast<uint8_t>(uPrefix))) {
		rstrError.Format(_T("range is not public IPv4: %s"), (LPCTSTR)strText);
		return false;
	}

	rRange.uNetworkOrderAddress = uAddress;
	rRange.uPrefixLength = static_cast<uint8_t>(uPrefix);
	return true;
}

inline bool TryParseAllowedPublicIpv4Ranges(CString strText, std::vector<SAllowedPublicIpv4Range>& rRanges, CString& rstrError)
{
	rRanges.clear();
	rstrError.Empty();

	CString strToken;
	for (int i = 0; i <= strText.GetLength(); ++i) {
		const TCHAR ch = i < strText.GetLength() ? strText[i] : _T(',');
		if (ch == _T(',') || ch == _T(';') || ch == _T('\r') || ch == _T('\n') || ch == _T('\t') || ch == _T(' ')) {
			strToken.Trim();
			if (!strToken.IsEmpty()) {
				SAllowedPublicIpv4Range range = {};
				if (!TryParseAllowedPublicIpv4Range(strToken, range, rstrError))
					return false;
				rRanges.push_back(range);
				strToken.Empty();
			}
		} else {
			strToken.AppendChar(ch);
		}
	}

	if (rRanges.empty()) {
		rstrError = _T("at least one public IPv4 CIDR or address is required");
		return false;
	}
	return true;
}

inline bool IsPublicIpv4Allowed(uint32_t uNetworkOrderAddress, const std::vector<SAllowedPublicIpv4Range>& ranges)
{
	const uint32_t uAddress = ToComparableIpv4(uNetworkOrderAddress);
	for (const SAllowedPublicIpv4Range& range : ranges) {
		const uint32_t uBase = ToComparableIpv4(range.uNetworkOrderAddress);
		const uint32_t uMask = range.uPrefixLength == 0 ? 0u : (0xffffffffu << (32u - range.uPrefixLength));
		if ((uAddress & uMask) == (uBase & uMask))
			return true;
	}
	return false;
}
}
