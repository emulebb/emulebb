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
 * @brief Testable rule and script-building helpers for Windows Firewall repair.
 */
namespace WindowsFirewallRepairSeams
{
	struct CFirewallRuleSpec
	{
		CString strName;
		CString strProtocol;
		UINT uPort = 0;
	};

	/** Returns true when a TCP or UDP local port can be represented in a firewall rule. */
	inline bool IsValidFirewallPort(const UINT uPort)
	{
		return uPort >= 1u && uPort <= 65535u;
	}

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

	/** Returns true when the REST/WebServer listener is configured for non-loopback reachability. */
	inline bool ShouldIncludeRestFirewallRule(const bool bWebServerEnabled, const UINT uWebPort, const CString &rstrWebBindAddress)
	{
		return bWebServerEnabled && IsValidFirewallPort(uWebPort) && !IsLoopbackWebBindAddress(rstrWebBindAddress);
	}

	/** Builds the desired persistent inbound rules for the current eMule BB ports. */
	inline std::vector<CFirewallRuleSpec> BuildDesiredRules(const UINT uTcpPort, const UINT uUdpPort, const bool bWebServerEnabled, const UINT uWebPort, const CString &rstrWebBindAddress)
	{
		std::vector<CFirewallRuleSpec> rules;
		if (IsValidFirewallPort(uTcpPort))
			rules.push_back(CFirewallRuleSpec{_T("eMule BB TCP"), _T("TCP"), uTcpPort});
		if (IsValidFirewallPort(uUdpPort))
			rules.push_back(CFirewallRuleSpec{_T("eMule BB UDP"), _T("UDP"), uUdpPort});
		if (ShouldIncludeRestFirewallRule(bWebServerEnabled, uWebPort, rstrWebBindAddress))
			rules.push_back(CFirewallRuleSpec{_T("eMule BB REST"), _T("TCP"), uWebPort});
		return rules;
	}

	/** Quotes a PowerShell single-quoted string literal. */
	inline CString QuotePowerShellSingleQuotedLiteral(const CString &rstrValue)
	{
		CString strEscaped(rstrValue);
		strEscaped.Replace(_T("'"), _T("''"));
		return _T("'") + strEscaped + _T("'");
	}

	/** Builds the elevated PowerShell script body that repairs eMule BB-owned firewall rules. */
	inline CString BuildRepairScript(const CString &rstrProgramPath, const std::vector<CFirewallRuleSpec> &rules, const CString &rstrResultPath)
	{
		CString strScript;
		strScript += _T("$ErrorActionPreference = 'Stop'\r\n");
		strScript += _T("$Host.UI.RawUI.WindowTitle = 'eMule BB Windows Firewall Repair'\r\n");
		strScript.AppendFormat(_T("$ProgramPath = %s\r\n"), (LPCTSTR)QuotePowerShellSingleQuotedLiteral(rstrProgramPath));
		strScript.AppendFormat(_T("$ResultPath = %s\r\n"), (LPCTSTR)QuotePowerShellSingleQuotedLiteral(rstrResultPath));
		strScript += _T("$Rules = @(\r\n");
		for (const CFirewallRuleSpec &rule : rules) {
			strScript.AppendFormat(
				_T("    [pscustomobject]@{ Name = %s; Protocol = %s; Port = %u }\r\n"),
				(LPCTSTR)QuotePowerShellSingleQuotedLiteral(rule.strName),
				(LPCTSTR)QuotePowerShellSingleQuotedLiteral(rule.strProtocol),
				rule.uPort);
		}
		strScript += _T(")\r\n");
		strScript += _T("Write-Host ''\r\n");
		strScript += _T("Write-Host 'eMule BB Windows Firewall Repair' -ForegroundColor Cyan\r\n");
		strScript += _T("Write-Host ('Program: {0}' -f $ProgramPath)\r\n");
		strScript += _T("Write-Host 'Profiles: Domain, Private, Public'\r\n");
		strScript += _T("Write-Host ('Rules requested: {0}' -f $Rules.Count)\r\n");
		strScript += _T("Write-Host ''\r\n");
		strScript += _T("function Repair-EmuleFirewallRule([string]$Name, [string]$Protocol, [int]$Port) {\r\n");
		strScript += _T("    Write-Host ('Repairing {0}: {1}/{2}' -f $Name, $Protocol, $Port)\r\n");
		strScript += _T("    $existing = @(Get-NetFirewallRule -DisplayName $Name -ErrorAction SilentlyContinue)\r\n");
		strScript += _T("    $action = 'created'\r\n");
		strScript += _T("    if ($existing.Count -gt 0) {\r\n");
		strScript += _T("        Write-Host ('  Existing rule count: {0}; replacing eMule BB-owned rule.' -f $existing.Count) -ForegroundColor Yellow\r\n");
		strScript += _T("        $existing | Remove-NetFirewallRule -ErrorAction Stop\r\n");
		strScript += _T("        $action = 'updated'\r\n");
		strScript += _T("    }\r\n");
		strScript += _T("    New-NetFirewallRule -DisplayName $Name -Direction Inbound -Action Allow -Enabled True -Profile Domain,Private,Public -Program $ProgramPath -Protocol $Protocol -LocalPort $Port | Out-Null\r\n");
		strScript += _T("    Write-Host ('  OK: {0} {1}/{2} ({3})' -f $Name, $Protocol, $Port, $action) -ForegroundColor Green\r\n");
		strScript += _T("    [ordered]@{ name = $Name; protocol = $Protocol; port = $Port; action = $action }\r\n");
		strScript += _T("}\r\n");
		strScript += _T("$result = [ordered]@{ schema = 'emulebb.firewallRepairResult.v1'; success = $false; programPath = $ProgramPath; rules = @(); errors = @() }\r\n");
		strScript += _T("foreach ($rule in $Rules) {\r\n");
		strScript += _T("    try {\r\n");
		strScript += _T("        $result.rules += Repair-EmuleFirewallRule -Name $rule.Name -Protocol $rule.Protocol -Port $rule.Port\r\n");
		strScript += _T("    } catch {\r\n");
		strScript += _T("        Write-Host ('  FAILED: {0}' -f $_.Exception.Message) -ForegroundColor Red\r\n");
		strScript += _T("        $result.errors += [ordered]@{ name = $rule.Name; protocol = $rule.Protocol; port = $rule.Port; message = $_.Exception.Message }\r\n");
		strScript += _T("    }\r\n");
		strScript += _T("}\r\n");
		strScript += _T("$result.success = ($result.errors.Count -eq 0)\r\n");
		strScript += _T("$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ResultPath -Encoding UTF8\r\n");
		strScript += _T("Write-Host ''\r\n");
		strScript += _T("Write-Host ('Result JSON: {0}' -f $ResultPath)\r\n");
		strScript += _T("if ($result.success) {\r\n");
		strScript += _T("    Write-Host 'Windows Firewall repair completed successfully.' -ForegroundColor Green\r\n");
		strScript += _T("    Read-Host 'Press Enter to close this window' | Out-Null\r\n");
		strScript += _T("    exit 0\r\n");
		strScript += _T("}\r\n");
		strScript += _T("Write-Host 'Windows Firewall repair completed with errors.' -ForegroundColor Red\r\n");
		strScript += _T("Read-Host 'Press Enter to close this window' | Out-Null\r\n");
		strScript += _T("exit 1\r\n");
		return strScript;
	}
}
