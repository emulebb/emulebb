//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
#pragma once

#include "WindowsFirewallRepairSeams.h"

#include <atlstr.h>
#include <vector>

namespace WindowsMaintenanceActionsSeams
{
	inline CString QuotePowerShellSingleQuotedLiteral(const CString &rstrValue)
	{
		return WindowsFirewallRepairSeams::QuotePowerShellSingleQuotedLiteral(rstrValue);
	}

	inline CString BuildEnableLongPathsScript(const CString &rstrResultPath)
	{
		CString strScript;
		strScript += _T("$ErrorActionPreference = 'Stop'\r\n");
		strScript += _T("$Host.UI.RawUI.WindowTitle = 'eMuleBB Long Path Policy'\r\n");
		strScript.AppendFormat(_T("$ResultPath = %s\r\n"), (LPCTSTR)QuotePowerShellSingleQuotedLiteral(rstrResultPath));
		strScript += _T("$RegistryPath = 'HKLM:\\SYSTEM\\CurrentControlSet\\Control\\FileSystem'\r\n");
		strScript += _T("Write-Host ''\r\n");
		strScript += _T("Write-Host 'eMuleBB Long Path Policy' -ForegroundColor Cyan\r\n");
		strScript += _T("Write-Host 'Target: HKLM\\SYSTEM\\CurrentControlSet\\Control\\FileSystem\\LongPathsEnabled = 1'\r\n");
		strScript += _T("$result = [ordered]@{ schema = 'emulebb.longPathPolicyResult.v1'; success = $false; alreadyEnabled = $false; changed = $false; errors = @() }\r\n");
		strScript += _T("try {\r\n");
		strScript += _T("    $current = (Get-ItemProperty -LiteralPath $RegistryPath -Name 'LongPathsEnabled' -ErrorAction SilentlyContinue).LongPathsEnabled\r\n");
		strScript += _T("    if ($current -eq 1) {\r\n");
		strScript += _T("        $result.alreadyEnabled = $true\r\n");
		strScript += _T("        Write-Host 'Long path support is already enabled.' -ForegroundColor Green\r\n");
		strScript += _T("    } else {\r\n");
		strScript += _T("        New-ItemProperty -LiteralPath $RegistryPath -Name 'LongPathsEnabled' -PropertyType DWord -Value 1 -Force | Out-Null\r\n");
		strScript += _T("        $result.changed = $true\r\n");
		strScript += _T("        Write-Host 'Long path support has been enabled.' -ForegroundColor Green\r\n");
		strScript += _T("    }\r\n");
		strScript += _T("    $result.success = $true\r\n");
		strScript += _T("} catch {\r\n");
		strScript += _T("    Write-Host ('FAILED: {0}' -f $_.Exception.Message) -ForegroundColor Red\r\n");
		strScript += _T("    $result.errors += $_.Exception.Message\r\n");
		strScript += _T("}\r\n");
		strScript += _T("$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ResultPath -Encoding UTF8\r\n");
		strScript += _T("Write-Host ''\r\n");
		strScript += _T("Write-Host ('Result JSON: {0}' -f $ResultPath)\r\n");
		strScript += _T("if ($result.success) { Read-Host 'Press Enter to close this window' | Out-Null; exit 0 }\r\n");
		strScript += _T("Read-Host 'Press Enter to close this window' | Out-Null\r\n");
		strScript += _T("exit 1\r\n");
		return strScript;
	}

	inline CString BuildDefenderExclusionScript(const std::vector<CString> &paths, const CString &rstrResultPath)
	{
		CString strScript;
		strScript += _T("$ErrorActionPreference = 'Stop'\r\n");
		strScript += _T("$Host.UI.RawUI.WindowTitle = 'eMuleBB Microsoft Defender Exclusions'\r\n");
		strScript.AppendFormat(_T("$ResultPath = %s\r\n"), (LPCTSTR)QuotePowerShellSingleQuotedLiteral(rstrResultPath));
		strScript += _T("$Paths = @(\r\n");
		for (const CString &strPath : paths)
			strScript.AppendFormat(_T("    %s\r\n"), (LPCTSTR)QuotePowerShellSingleQuotedLiteral(strPath));
		strScript += _T(")\r\n");
		strScript += _T("Write-Host ''\r\n");
		strScript += _T("Write-Host 'eMuleBB Microsoft Defender Exclusions' -ForegroundColor Cyan\r\n");
		strScript += _T("Write-Host 'Scope: active Incoming, Temp, and category incoming folders'\r\n");
		strScript += _T("Write-Host ('Folders requested: {0}' -f $Paths.Count)\r\n");
		strScript += _T("$result = [ordered]@{ schema = 'emulebb.defenderExclusionResult.v1'; success = $false; added = @(); skipped = @(); errors = @() }\r\n");
		strScript += _T("try { $existing = @((Get-MpPreference).ExclusionPath) } catch { $existing = @(); Write-Host ('Could not read existing exclusions: {0}' -f $_.Exception.Message) -ForegroundColor Yellow }\r\n");
		strScript += _T("foreach ($path in $Paths) {\r\n");
		strScript += _T("    if ([string]::IsNullOrWhiteSpace($path)) { continue }\r\n");
		strScript += _T("    $alreadyExcluded = $false\r\n");
		strScript += _T("    foreach ($existingPath in $existing) { if ([string]::Equals($existingPath, $path, [System.StringComparison]::OrdinalIgnoreCase)) { $alreadyExcluded = $true; break } }\r\n");
		strScript += _T("    if ($alreadyExcluded) {\r\n");
		strScript += _T("        Write-Host ('Already excluded: {0}' -f $path) -ForegroundColor Yellow\r\n");
		strScript += _T("        $result.skipped += [ordered]@{ path = $path; reason = 'alreadyExcluded' }\r\n");
		strScript += _T("        continue\r\n");
		strScript += _T("    }\r\n");
		strScript += _T("    try {\r\n");
		strScript += _T("        Add-MpPreference -ExclusionPath $path -ErrorAction Stop\r\n");
		strScript += _T("        Write-Host ('Added exclusion: {0}' -f $path) -ForegroundColor Green\r\n");
		strScript += _T("        $result.added += $path\r\n");
		strScript += _T("    } catch {\r\n");
		strScript += _T("        Write-Host ('FAILED: {0}: {1}' -f $path, $_.Exception.Message) -ForegroundColor Red\r\n");
		strScript += _T("        $result.errors += [ordered]@{ path = $path; message = $_.Exception.Message }\r\n");
		strScript += _T("    }\r\n");
		strScript += _T("}\r\n");
		strScript += _T("$result.success = ($result.errors.Count -eq 0)\r\n");
		strScript += _T("$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ResultPath -Encoding UTF8\r\n");
		strScript += _T("Write-Host ''\r\n");
		strScript += _T("Write-Host ('Added: {0}; skipped: {1}; errors: {2}' -f $result.added.Count, $result.skipped.Count, $result.errors.Count)\r\n");
		strScript += _T("Write-Host ('Result JSON: {0}' -f $ResultPath)\r\n");
		strScript += _T("if ($result.success) { Read-Host 'Press Enter to close this window' | Out-Null; exit 0 }\r\n");
		strScript += _T("Read-Host 'Press Enter to close this window' | Out-Null\r\n");
		strScript += _T("exit 1\r\n");
		return strScript;
	}
}
