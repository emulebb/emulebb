//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
#include "stdafx.h"
#include "WindowsFirewallRepair.h"

bool WindowsFirewallRepair::RunElevatedRepair(const CString &rstrProgramPath, const std::vector<WindowsFirewallRepairSeams::CFirewallRuleSpec> &rules, CRepairLaunchResult &rResult)
{
	rResult = CRepairLaunchResult{};
	rResult.rules = rules;
	if (rules.empty()) {
		rResult.bSucceeded = true;
		return true;
	}

	if (!ElevatedPowerShellAction::PrepareBundledScript(_T("eMuleBB-FirewallRepair"), _T("repair-firewall.ps1"), _T("repair-result.json"), rResult))
		return false;

	CString strArguments;
	strArguments.Format(
		_T("-ProgramPath %s -ResultPath %s"),
		(LPCTSTR)ElevatedPowerShellAction::QuotePowerShellArgument(rstrProgramPath),
		(LPCTSTR)ElevatedPowerShellAction::QuotePowerShellArgument(rResult.strResultPath));
	return ElevatedPowerShellAction::RunBundledScript(strArguments, true, true, rResult);
}
