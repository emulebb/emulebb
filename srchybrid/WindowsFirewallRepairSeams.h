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

	/** Quotes a PowerShell single-quoted string literal. */
	inline CString QuotePowerShellSingleQuotedLiteral(const CString &rstrValue)
	{
		CString strEscaped(rstrValue);
		strEscaped.Replace(_T("'"), _T("''"));
		return _T("'") + strEscaped + _T("'");
	}

	/** Builds the elevated PowerShell script body that repairs eMuleBB-owned firewall rules. */
	inline CString BuildRepairScript(const CString &rstrProgramPath, const std::vector<CFirewallRuleSpec> &rules, const CString &rstrResultPath)
	{
		CString strScript;
		strScript += _T("$ErrorActionPreference = 'Stop'\r\n");
		strScript += _T("$Host.UI.RawUI.WindowTitle = 'eMuleBB Windows Firewall Repair'\r\n");
		strScript.AppendFormat(_T("$ProgramPath = %s\r\n"), (LPCTSTR)QuotePowerShellSingleQuotedLiteral(rstrProgramPath));
		strScript.AppendFormat(_T("$ResultPath = %s\r\n"), (LPCTSTR)QuotePowerShellSingleQuotedLiteral(rstrResultPath));
		strScript += _T("$Rules = @(\r\n");
		for (const CFirewallRuleSpec &rule : rules) {
			strScript.AppendFormat(
				_T("    [pscustomobject]@{ Name = %s; Direction = %s; Protocol = %s }\r\n"),
				(LPCTSTR)QuotePowerShellSingleQuotedLiteral(rule.strName),
				(LPCTSTR)QuotePowerShellSingleQuotedLiteral(rule.strDirection),
				(LPCTSTR)QuotePowerShellSingleQuotedLiteral(rule.strProtocol));
		}
		strScript += _T(")\r\n");
		strScript += _T("Write-Host ''\r\n");
		strScript += _T("Write-Host 'eMuleBB Windows Firewall Repair' -ForegroundColor Cyan\r\n");
		strScript += _T("Write-Host ('Program: {0}' -f $ProgramPath)\r\n");
		strScript += _T("Write-Host 'Profiles: Domain, Private, Public'\r\n");
		strScript += _T("Write-Host 'Scope: inbound and outbound TCP/UDP, all ports, all hosts, all interfaces'\r\n");
		strScript += _T("Write-Host ('Rules requested: {0}' -f $Rules.Count)\r\n");
		strScript += _T("Write-Host ''\r\n");
		strScript += _T("function Repair-EmuleFirewallRule([string]$Name, [string]$Direction, [string]$Protocol) {\r\n");
		strScript += _T("    Write-Host ('Repairing {0}: {1} {2}, all ports' -f $Name, $Direction, $Protocol)\r\n");
		strScript += _T("    $existing = @(Get-NetFirewallRule -DisplayName $Name -ErrorAction SilentlyContinue)\r\n");
		strScript += _T("    $action = 'created'\r\n");
		strScript += _T("    if ($existing.Count -gt 0) {\r\n");
		strScript += _T("        Write-Host ('  Existing exact-name rule count: {0}; deleting before recreation.' -f $existing.Count) -ForegroundColor Yellow\r\n");
		strScript += _T("        $existing | Remove-NetFirewallRule -ErrorAction Stop\r\n");
		strScript += _T("        $action = 'replaced'\r\n");
		strScript += _T("    }\r\n");
		strScript += _T("    New-NetFirewallRule -DisplayName $Name -Direction $Direction -Action Allow -Enabled True -Profile Domain,Private,Public -Program $ProgramPath -Protocol $Protocol | Out-Null\r\n");
		strScript += _T("    Write-Host ('  OK: {0} {1} {2}, all ports ({3})' -f $Name, $Direction, $Protocol, $action) -ForegroundColor Green\r\n");
		strScript += _T("    [ordered]@{ name = $Name; direction = $Direction; protocol = $Protocol; programPath = $ProgramPath; localPort = 'Any'; remotePort = 'Any'; localAddress = 'Any'; remoteAddress = 'Any'; profiles = @('Domain', 'Private', 'Public'); action = $action }\r\n");
		strScript += _T("}\r\n");
		strScript += _T("$result = [ordered]@{ schema = 'emulebb.firewallRepairResult.v1'; success = $false; programPath = $ProgramPath; rules = @(); errors = @() }\r\n");
		strScript += _T("foreach ($rule in $Rules) {\r\n");
		strScript += _T("    try {\r\n");
		strScript += _T("        $result.rules += Repair-EmuleFirewallRule -Name $rule.Name -Direction $rule.Direction -Protocol $rule.Protocol\r\n");
		strScript += _T("    } catch {\r\n");
		strScript += _T("        Write-Host ('  FAILED: {0}' -f $_.Exception.Message) -ForegroundColor Red\r\n");
		strScript += _T("        $result.errors += [ordered]@{ name = $rule.Name; direction = $rule.Direction; protocol = $rule.Protocol; message = $_.Exception.Message }\r\n");
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
