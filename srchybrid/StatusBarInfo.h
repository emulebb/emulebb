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
	}

	inline CString FormatNetworkAddressPaneText(const CString &strBindAddress
		, uint32 dwPublicIp
		, const CString &strBindIpLabel
		, const CString &strPublicIpLabel
		, const CString &strAnyBindAddressLabel
		, const CString &strUnknownPublicIpLabel
		, const CString &strFormat)
	{
		CString strPaneText;
		strPaneText.Format(strFormat
			, (LPCTSTR)strBindIpLabel
			, (LPCTSTR)Detail::GetBindAddressDisplayValue(strBindAddress, strAnyBindAddressLabel)
			, (LPCTSTR)strPublicIpLabel
			, dwPublicIp != 0 ? (LPCTSTR)Detail::FormatStoredIPv4Address(dwPublicIp) : (LPCTSTR)strUnknownPublicIpLabel);
		return strPaneText;
	}

	inline CString FormatNetworkAddressPaneToolTip(const CString &strBindAddress
		, uint32 dwPublicIp
		, const CString &strBindIpLabel
		, const CString &strPublicIpLabel
		, const CString &strAnyInterfaceLabel
		, const CString &strUnknownLabel
		, const CString &strFormat)
	{
		CString strToolTip;
		strToolTip.Format(strFormat
			, (LPCTSTR)strBindIpLabel
			, strBindAddress.IsEmpty() ? (LPCTSTR)strAnyInterfaceLabel : (LPCTSTR)strBindAddress
			, (LPCTSTR)strPublicIpLabel
			, dwPublicIp != 0 ? (LPCTSTR)Detail::FormatStoredIPv4Address(dwPublicIp) : (LPCTSTR)strUnknownLabel);
		return strToolTip;
	}
}
