//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
#pragma once

#include <atlstr.h>
#include <vector>

/**
 * @brief Testable rule helpers for Windows Firewall repair.
 */
namespace WindowsFirewallRepairSeams
{
	struct CFirewallRuleSpec
	{
		CString strName;
		CString strDirection;
		CString strProtocol;
	};

	/** Returns true when the WebServer bind address is loopback-only and does not need an inbound OS firewall rule. */
	inline bool IsLoopbackWebBindAddress(const CString &rstrBindAddress)
	{
		CString strBindAddress(rstrBindAddress);
		strBindAddress.Trim();
		strBindAddress.MakeLower();
		return strBindAddress == _T("localhost")
			|| strBindAddress == _T("::1")
			|| strBindAddress == _T("[::1]")
			|| strBindAddress == _T("127.0.0.1")
			|| strBindAddress.Left(4) == _T("127.");
	}

	/** Builds the desired persistent broad rules for the eMuleBB executable. */
	inline std::vector<CFirewallRuleSpec> BuildDesiredRules(const UINT uTcpPort, const UINT uUdpPort, const bool bWebServerEnabled, const UINT uWebPort, const CString &rstrWebBindAddress)
	{
		(void)uTcpPort;
		(void)uUdpPort;
		(void)bWebServerEnabled;
		(void)uWebPort;
		(void)rstrWebBindAddress;

		std::vector<CFirewallRuleSpec> rules;
		rules.push_back(CFirewallRuleSpec{_T("eMuleBB Inbound TCP"), _T("Inbound"), _T("TCP")});
		rules.push_back(CFirewallRuleSpec{_T("eMuleBB Inbound UDP"), _T("Inbound"), _T("UDP")});
		rules.push_back(CFirewallRuleSpec{_T("eMuleBB Outbound TCP"), _T("Outbound"), _T("TCP")});
		rules.push_back(CFirewallRuleSpec{_T("eMuleBB Outbound UDP"), _T("Outbound"), _T("UDP")});
		return rules;
	}

}
