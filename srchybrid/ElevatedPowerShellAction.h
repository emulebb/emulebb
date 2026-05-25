//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
#pragma once

/**
 * @brief Common runtime launcher for explicit elevated one-shot PowerShell actions.
 */
namespace ElevatedPowerShellAction
{
	struct CLaunchResult
	{
		bool bStarted = false;
		bool bSucceeded = false;
		bool bCancelled = false;
		DWORD dwLastError = ERROR_SUCCESS;
		DWORD dwExitCode = STILL_ACTIVE;
		CString strTempDir;
		CString strScriptPath;
		CString strResultPath;
		CString strErrorText;
		CString strResultJson;
	};

	/** Quotes one value for PowerShell command-line parameter passing. */
	CString QuotePowerShellArgument(const CString &rstrValue);

	/** Prepares a bundled script launch with a locked-down temp result file. */
	bool PrepareBundledScript(const CString &rstrTempPrefix, const CString &rstrScriptLeafName, const CString &rstrResultLeafName, CLaunchResult &rResult);

	/** Runs the prepared bundled script, optionally elevated, and reads the JSON result. */
	bool RunBundledScript(const CString &rstrArguments, bool bElevated, bool bWaitForExit, CLaunchResult &rResult);
}
