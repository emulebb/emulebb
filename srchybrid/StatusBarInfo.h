#pragma once

#include <atlstr.h>
#include "types.h"

#define EMULEBB_STATUS_BAR_INFO_USES_EXTERNAL_TEXT 1

namespace StatusBarInfo
{
	namespace Detail
	{
		inline CString FormatStoredIPv4Address(const uint32 dwIp)
		{
			const BYTE *pucIp = reinterpret_cast<const BYTE*>(&dwIp);
			CString strIp;
			strIp.Format(_T("%u.%u.%u.%u")
				, pucIp[0]
				, pucIp[1]
				, pucIp[2]
				, pucIp[3]);
			return strIp;
		}

		inline CString GetBindAddressDisplayValue(const CString &strBindAddress, const CString &strAnyBindAddressLabel)
		{
			return strBindAddress.IsEmpty() ? strAnyBindAddressLabel : strBindAddress;
		}

		inline CString GetPublicEndpointDisplayValue(const uint32 dwPublicIp, uint16 uInboundTcpPort, const CString &strUnknownPublicIpLabel)
		{
			if (dwPublicIp == 0)
				return strUnknownPublicIpLabel;

			CString strPublicEndpoint(FormatStoredIPv4Address(dwPublicIp));
			if (uInboundTcpPort != 0)
				strPublicEndpoint.AppendFormat(_T(":%u"), uInboundTcpPort);
			return strPublicEndpoint;
		}
	}

	inline CString FormatNetworkAddressPaneText(const CString &strBindAddress
		, uint32 dwPublicIp
		, const CString &strBindIpLabel
		, const CString &strPublicIpLabel
		, const CString &strAnyBindAddressLabel
		, const CString &strUnknownPublicIpLabel
		, const CString &strFormat
		, uint16 uInboundTcpPort = 0
		, bool bNetworkAddressBlocked = false
		, const CString &strBlockedAddressLabel = CString(_T("-")))
	{
		const CString strBindDisplay = bNetworkAddressBlocked
			? strBlockedAddressLabel
			: Detail::GetBindAddressDisplayValue(strBindAddress, strAnyBindAddressLabel);
		const CString strPublicDisplay = bNetworkAddressBlocked
			? strBlockedAddressLabel
			: Detail::GetPublicEndpointDisplayValue(dwPublicIp, uInboundTcpPort, strUnknownPublicIpLabel);
		CString strPaneText;
		strPaneText.Format(strFormat
			, (LPCTSTR)strBindIpLabel
			, (LPCTSTR)strBindDisplay
			, (LPCTSTR)strPublicIpLabel
			, (LPCTSTR)strPublicDisplay);
		return strPaneText;
	}

	inline CString FormatNetworkAddressPaneToolTip(const CString &strBindAddress
		, uint32 dwPublicIp
		, const CString &strBindIpLabel
		, const CString &strPublicIpLabel
		, const CString &strAnyInterfaceLabel
		, const CString &strUnknownLabel
		, const CString &strFormat
		, uint16 uInboundTcpPort = 0
		, bool bNetworkAddressBlocked = false
		, const CString &strBlockedAddressLabel = CString(_T("-")))
	{
		const CString strBindDisplay = bNetworkAddressBlocked
			? strBlockedAddressLabel
			: (strBindAddress.IsEmpty() ? strAnyInterfaceLabel : strBindAddress);
		const CString strPublicDisplay = bNetworkAddressBlocked
			? strBlockedAddressLabel
			: Detail::GetPublicEndpointDisplayValue(dwPublicIp, uInboundTcpPort, strUnknownLabel);
		CString strToolTip;
		strToolTip.Format(strFormat
			, (LPCTSTR)strBindIpLabel
			, (LPCTSTR)strBindDisplay
			, (LPCTSTR)strPublicIpLabel
			, (LPCTSTR)strPublicDisplay);
		return strToolTip;
	}
}
