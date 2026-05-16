//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
#pragma once

#include "ElevatedPowerShellAction.h"
#include "WindowsFirewallRepairSeams.h"

#include <vector>

/**
 * @brief Runtime launcher for the explicit elevated Windows Firewall repair action.
 */
namespace WindowsFirewallRepair
{
	struct CRepairLaunchResult : ElevatedPowerShellAction::CLaunchResult
	{
		std::vector<WindowsFirewallRepairSeams::CFirewallRuleSpec> rules;
	};

	/** Launches one elevated temp PowerShell repair script and captures its JSON result. */
	bool RunElevatedRepair(const CString &rstrProgramPath, const std::vector<WindowsFirewallRepairSeams::CFirewallRuleSpec> &rules, CRepairLaunchResult &rResult);
}
