#pragma once

#include <atlstr.h>
#include "BindAddressResolver.h"

#define EMULEBB_BIND_STARTUP_POLICY_USES_EXTERNAL_TEXT 1

namespace BindStartupPolicy
{
	/**
	 * User-visible text used when formatting bind startup policy messages.
	 */
	struct CBindStartupPolicyText
	{
		CString strAnyInterface;
		CString strInterfaceNotFoundFormat;
		CString strInterfaceNameAmbiguousFormat;
		CString strInterfaceHasNoAddressFormat;
		CString strAddressNotFoundOnInterfaceFormat;
		CString strAddressNotFoundFormat;
	};

	/**
	 * Reports whether the user explicitly selected a bind interface or bind IP.
	 */
	inline bool HasExplicitBindSelection(const CString &strInterfaceId, const CString &strConfiguredAddress)
	{
		return !strInterfaceId.IsEmpty() || !strConfiguredAddress.IsEmpty();
	}

	/**
	 * Decides whether startup networking must be blocked for the current session.
	 */
	inline bool ShouldBlockSessionNetworking(bool bEnabled
		, const CString &strInterfaceId
		, const CString &strConfiguredAddress
		, EBindAddressResolveResult eResult)
	{
		return bEnabled && HasExplicitBindSelection(strInterfaceId, strConfiguredAddress) && eResult != BARR_Resolved;
	}

	/**
	 * Formats the configured bind target for logs and diagnostics.
	 */
	inline CString FormatConfiguredBindTarget(const CString &strInterfaceName
		, const CString &strInterfaceId
		, const CString &strConfiguredAddress
		, const CString &strAnyInterface)
	{
		CString strTarget;
		if (!strInterfaceName.IsEmpty())
			strTarget = strInterfaceName;
		else if (!strInterfaceId.IsEmpty())
			strTarget = strInterfaceId;
		else
			strTarget = strAnyInterface;

		if (!strConfiguredAddress.IsEmpty()) {
			if (!strTarget.IsEmpty())
				strTarget.Append(_T(" / "));
			strTarget.Append(strConfiguredAddress);
		}
		return strTarget;
	}

	/**
	 * Formats one bind-policy message with the already formatted bind target.
	 */
	inline CString FormatMessage(const CString &strFormat, const CString &strTarget)
	{
		CString strMessage;
		strMessage.Format(strFormat, (LPCTSTR)strTarget);
		return strMessage;
	}

	/**
	 * Explains why startup networking was blocked for the current bind selection.
	 */
	inline CString FormatStartupBlockReason(const CString &strInterfaceName
		, const CString &strInterfaceId
		, const CString &strConfiguredAddress
		, EBindAddressResolveResult eResult
		, const CBindStartupPolicyText &text)
	{
		const CString strTarget = FormatConfiguredBindTarget(strInterfaceName, strInterfaceId, strConfiguredAddress, text.strAnyInterface);

		switch (eResult) {
		case BARR_InterfaceNotFound:
			return FormatMessage(text.strInterfaceNotFoundFormat, strTarget);
		case BARR_InterfaceNameAmbiguous:
			return FormatMessage(text.strInterfaceNameAmbiguousFormat, strTarget);
		case BARR_InterfaceHasNoAddress:
			return FormatMessage(text.strInterfaceHasNoAddressFormat, strTarget);
		case BARR_AddressNotFoundOnInterface:
			return FormatMessage(text.strAddressNotFoundOnInterfaceFormat, strTarget);
		case BARR_AddressNotFound:
			return FormatMessage(text.strAddressNotFoundFormat, strTarget);
		case BARR_Default:
		case BARR_Resolved:
		default:
			return CString();
		}
	}
}
