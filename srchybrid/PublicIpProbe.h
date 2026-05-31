#pragma once

#include <atlstr.h>

namespace PublicIpProbe
{
	struct SBoundPublicIpv4ProbeResult
	{
		bool bAttempted = false;
		bool bSucceeded = false;
		uint32 uGeneration = 0;
		CStringA strPublicAddress;
		uint32 uPublicAddress = 0;
		CString strProviderUrl;
		CString strPurpose;
		CString strBindInterfaceName;
		CStringA strBindAddress;
		DWORD dwBindInterfaceIndex = 0;
		CString strAttempts;
		CString strError;
	};

	void StartBoundPublicIpv4Probe();
	bool StartBoundPublicIpv4Probe(HWND hNotifyWnd, UINT uNotifyMessage, uint32 uGeneration, const CString& strPurpose, CString& rstrError);
}
