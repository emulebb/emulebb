#pragma once

#include <atlstr.h>
#include "BindStartupPolicy.h"

#define EMULEBB_BIND_RUNTIME_LOSS_POLICY_USES_EXTERNAL_TEXT 1

namespace BindRuntimeLossPolicy
{
	/**
	 * User-visible text used when formatting runtime bind-loss policy messages.
	 */
	struct CBindRuntimeLossPolicyText
	{
		BindStartupPolicy::CBindStartupPolicyText startupText;
		CString strInterfaceChangedFormat;
		CString strInterfaceUnavailable;
		CString strStartupDisabledPrefix;
		CString strRuntimeExitPrefix;
	};

	/**
	 * Reports whether a live re-resolution still matches the address selected at startup.
	 */
	inline bool IsActiveBindAddressStillCurrent(EBindAddressResolveResult eResult
		, const CString &strResolvedAddress
		, const CString &strActiveBindAddress)
	{
		return eResult == BARR_Resolved && !strResolvedAddress.CompareNoCase(strActiveBindAddress);
	}

	/**
	 * Decides whether runtime bind-loss protection must shut down the application.
	 */
	inline bool ShouldExitForRuntimeBindLoss(bool bMonitorActive
		, EBindAddressResolveResult eResult
		, const CString &strResolvedAddress
		, const CString &strActiveBindAddress)
	{
		return bMonitorActive && !IsActiveBindAddressStillCurrent(eResult, strResolvedAddress, strActiveBindAddress);
	}

	/**
	 * Formats the operator-facing reason for a runtime bind-loss shutdown.
	 */
	inline CString FormatRuntimeBindLossReason(const CString &strResolvedInterfaceName
		, const CString &strActiveInterfaceName
		, const CString &strActiveInterfaceId
		, const CString &strActiveConfiguredAddress
		, EBindAddressResolveResult eResult
		, const CString &strResolvedAddress
		, const CString &strActiveBindAddress
		, const CBindRuntimeLossPolicyText &text)
	{
		if (eResult == BARR_Resolved) {
			CString strReason;
			strReason.Format(text.strInterfaceChangedFormat
				, (LPCTSTR)strActiveBindAddress
				, (LPCTSTR)strResolvedAddress
				, (LPCTSTR)BindStartupPolicy::FormatConfiguredBindTarget(strActiveInterfaceName
					, strActiveInterfaceId
					, strActiveConfiguredAddress
					, text.startupText.strAnyInterface));
			return strReason;
		}

		CString strReason = BindStartupPolicy::FormatStartupBlockReason(strResolvedInterfaceName
			, strActiveInterfaceId
			, strActiveConfiguredAddress
			, eResult
			, text.startupText);
		if (strReason.IsEmpty())
			return text.strInterfaceUnavailable;

		strReason.Replace(text.strStartupDisabledPrefix, text.strRuntimeExitPrefix);
		return strReason;
	}
}
