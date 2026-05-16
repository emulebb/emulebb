//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
#include "stdafx.h"
#include "WindowsMaintenanceActions.h"
#include "WindowsMaintenanceActionsSeams.h"

bool WindowsMaintenanceActions::RunElevatedEnableLongPaths(CMaintenanceLaunchResult &rResult)
{
	if (!ElevatedPowerShellAction::PrepareTempScript(_T("eMuleBB-LongPaths"), _T("enable-long-paths.ps1"), _T("long-paths-result.json"), rResult))
		return false;

	return ElevatedPowerShellAction::RunPreparedScript(
		WindowsMaintenanceActionsSeams::BuildEnableLongPathsScript(rResult.strResultPath),
		rResult);
}

bool WindowsMaintenanceActions::RunElevatedDefenderExclusions(const std::vector<CString> &paths, CMaintenanceLaunchResult &rResult)
{
	rResult = CMaintenanceLaunchResult{};
	rResult.paths = paths;
	if (paths.empty()) {
		rResult.bSucceeded = true;
		return true;
	}

	if (!ElevatedPowerShellAction::PrepareTempScript(_T("eMuleBB-DefenderExclusions"), _T("defender-exclusions.ps1"), _T("defender-exclusions-result.json"), rResult))
		return false;

	return ElevatedPowerShellAction::RunPreparedScript(
		WindowsMaintenanceActionsSeams::BuildDefenderExclusionScript(paths, rResult.strResultPath),
		rResult);
}
