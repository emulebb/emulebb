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

	/** Creates the locked-down temp action directory and assigns script/result paths. */
	bool PrepareTempScript(const CString &rstrTempPrefix, const CString &rstrScriptLeafName, const CString &rstrResultLeafName, CLaunchResult &rResult);

	/** Writes the prepared script, runs it elevated, reads the JSON result, and cleans up temp files. */
	bool RunPreparedScript(const CString &rstrScript, CLaunchResult &rResult);
}
