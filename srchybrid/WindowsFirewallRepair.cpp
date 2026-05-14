//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
#include "stdafx.h"
#include "WindowsFirewallRepair.h"
#include "LongPathSeams.h"
#include "OtherFunctions.h"

#include <AclAPI.h>
#include <vector>

namespace
{
	struct CSecurityAttributesStorage
	{
		SECURITY_ATTRIBUTES securityAttributes = {};
		std::vector<BYTE> securityDescriptor;
		std::vector<BYTE> acl;
	};

	bool AddFullAccessAce(PACL pAcl, PSID pSid)
	{
		return ::AddAccessAllowedAceEx(
			pAcl,
			ACL_REVISION,
			OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE,
			GENERIC_ALL,
			pSid) != FALSE;
	}

	bool BuildRestrictedSecurityAttributes(CSecurityAttributesStorage &rStorage, CString &rstrError)
	{
		HANDLE hToken = NULL;
		if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &hToken) == FALSE) {
			rstrError = GetErrorMessage(::GetLastError());
			return false;
		}

		DWORD dwTokenUserBytes = 0;
		(void)::GetTokenInformation(hToken, TokenUser, NULL, 0, &dwTokenUserBytes);
		std::vector<BYTE> tokenUserBytes(dwTokenUserBytes);
		if (dwTokenUserBytes == 0 || ::GetTokenInformation(hToken, TokenUser, tokenUserBytes.data(), dwTokenUserBytes, &dwTokenUserBytes) == FALSE) {
			rstrError = GetErrorMessage(::GetLastError());
			::CloseHandle(hToken);
			return false;
		}
		::CloseHandle(hToken);

		const PTOKEN_USER pTokenUser = reinterpret_cast<PTOKEN_USER>(tokenUserBytes.data());
		PSID pAdministratorsSid = NULL;
		PSID pSystemSid = NULL;
		SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
		if (::AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &pAdministratorsSid) == FALSE
			|| ::AllocateAndInitializeSid(&ntAuthority, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &pSystemSid) == FALSE) {
			rstrError = GetErrorMessage(::GetLastError());
			if (pAdministratorsSid != NULL)
				::FreeSid(pAdministratorsSid);
			if (pSystemSid != NULL)
				::FreeSid(pSystemSid);
			return false;
		}

		const DWORD dwAclBytes = sizeof(ACL)
			+ (sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD)) * 3
			+ ::GetLengthSid(pTokenUser->User.Sid)
			+ ::GetLengthSid(pAdministratorsSid)
			+ ::GetLengthSid(pSystemSid);
		rStorage.acl.assign(dwAclBytes, 0);
		PACL pAcl = reinterpret_cast<PACL>(rStorage.acl.data());
		const bool bAclOk = ::InitializeAcl(pAcl, dwAclBytes, ACL_REVISION) != FALSE
			&& AddFullAccessAce(pAcl, pTokenUser->User.Sid)
			&& AddFullAccessAce(pAcl, pAdministratorsSid)
			&& AddFullAccessAce(pAcl, pSystemSid);
		::FreeSid(pAdministratorsSid);
		::FreeSid(pSystemSid);
		if (!bAclOk) {
			rstrError = GetErrorMessage(::GetLastError());
			return false;
		}

		rStorage.securityDescriptor.assign(SECURITY_DESCRIPTOR_MIN_LENGTH, 0);
		PSECURITY_DESCRIPTOR pSecurityDescriptor = reinterpret_cast<PSECURITY_DESCRIPTOR>(rStorage.securityDescriptor.data());
		if (::InitializeSecurityDescriptor(pSecurityDescriptor, SECURITY_DESCRIPTOR_REVISION) == FALSE
			|| ::SetSecurityDescriptorDacl(pSecurityDescriptor, TRUE, pAcl, FALSE) == FALSE) {
			rstrError = GetErrorMessage(::GetLastError());
			return false;
		}

		rStorage.securityAttributes.nLength = sizeof(rStorage.securityAttributes);
		rStorage.securityAttributes.lpSecurityDescriptor = pSecurityDescriptor;
		rStorage.securityAttributes.bInheritHandle = FALSE;
		return true;
	}

	CString GetTempRootPath()
	{
		DWORD dwLength = ::GetTempPath(0, NULL);
		CString strPath;
		LPTSTR pszPath = strPath.GetBuffer(dwLength + 1);
		dwLength = ::GetTempPath(dwLength + 1, pszPath);
		strPath.ReleaseBuffer(dwLength);
		return strPath;
	}

	CString GuidToLeaf()
	{
		GUID guid = {};
		if (FAILED(::CoCreateGuid(&guid)))
			return CString();

		TCHAR szGuid[64] = {};
		if (::StringFromGUID2(guid, szGuid, _countof(szGuid)) <= 0)
			return CString();

		CString strGuid(szGuid);
		strGuid.Trim(_T("{}"));
		return strGuid;
	}

	bool CreateRepairTempDirectory(CString &rstrTempDir, CString &rstrError)
	{
		CSecurityAttributesStorage security;
		if (!BuildRestrictedSecurityAttributes(security, rstrError))
			return false;

		const CString strTempRoot(GetTempRootPath());
		if (strTempRoot.IsEmpty()) {
			rstrError = GetErrorMessage(::GetLastError());
			return false;
		}

		for (UINT uAttempt = 0; uAttempt < 16; ++uAttempt) {
			const CString strGuid(GuidToLeaf());
			if (strGuid.IsEmpty()) {
				rstrError = GetErrorMessage(::GetLastError());
				return false;
			}

			CString strCandidate;
			strCandidate.Format(_T("%seMuleBB-FirewallRepair-%s"), (LPCTSTR)strTempRoot, (LPCTSTR)strGuid);
			if (LongPathSeams::CreateDirectory(strCandidate, &security.securityAttributes) != FALSE) {
				rstrTempDir = strCandidate;
				return true;
			}

			const DWORD dwLastError = ::GetLastError();
			if (dwLastError != ERROR_ALREADY_EXISTS && dwLastError != ERROR_FILE_EXISTS) {
				rstrError = GetErrorMessage(dwLastError);
				return false;
			}
		}

		rstrError = GetErrorMessage(ERROR_ALREADY_EXISTS);
		return false;
	}

	bool WriteUtf16PowerShellScript(const CString &rstrPath, const CString &rstrScript, CString &rstrError)
	{
		CStringW strScriptW(rstrScript);
		std::vector<unsigned char> bytes(sizeof(WCHAR) + static_cast<size_t>(strScriptW.GetLength()) * sizeof(WCHAR));
		const WCHAR wBom = 0xFEFF;
		memcpy(bytes.data(), &wBom, sizeof wBom);
		if (strScriptW.GetLength() > 0)
			memcpy(bytes.data() + sizeof(WCHAR), static_cast<LPCWSTR>(strScriptW), static_cast<size_t>(strScriptW.GetLength()) * sizeof(WCHAR));

		if (!LongPathSeams::WriteAllBytes(rstrPath, bytes, CREATE_NEW, 0)) {
			rstrError = GetErrorMessage(::GetLastError());
			return false;
		}
		return true;
	}

	CString GetPowerShellPath()
	{
		CString strPath;
		LPTSTR pszWindowsDir = strPath.GetBuffer(MAX_PATH);
		const UINT uLength = ::GetWindowsDirectory(pszWindowsDir, MAX_PATH);
		strPath.ReleaseBuffer(uLength);
		if (uLength != 0 && uLength < MAX_PATH) {
			if (strPath.Right(1) != _T("\\"))
				strPath += _T("\\");
			strPath += _T("System32\\WindowsPowerShell\\v1.0\\powershell.exe");
			if (LongPathSeams::PathExists(strPath))
				return strPath;
		}
		return _T("powershell.exe");
	}

	void ReadResultJson(const CString &rstrResultPath, CString &rstrResultJson)
	{
		std::vector<unsigned char> bytes;
		if (!LongPathSeams::ReadAllBytes(rstrResultPath, bytes) || bytes.empty())
			return;

		if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
			const CStringA strUtf8(reinterpret_cast<LPCSTR>(bytes.data() + 3), static_cast<int>(bytes.size() - 3));
			rstrResultJson = CString(CA2T(strUtf8, CP_UTF8));
		} else if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE)
			rstrResultJson = CString(reinterpret_cast<LPCWSTR>(bytes.data() + 2), static_cast<int>((bytes.size() - 2) / sizeof(WCHAR)));
		else {
			const CStringA strUtf8(reinterpret_cast<LPCSTR>(bytes.data()), static_cast<int>(bytes.size()));
			rstrResultJson = CString(CA2T(strUtf8, CP_UTF8));
		}
	}

	void CleanupRepairTemp(const CString &rstrScriptPath, const CString &rstrResultPath, const CString &rstrTempDir)
	{
		(void)LongPathSeams::DeleteFileIfExists(rstrScriptPath);
		(void)LongPathSeams::DeleteFileIfExists(rstrResultPath);
		if (!rstrTempDir.IsEmpty())
			(void)LongPathSeams::RemoveDirectory(rstrTempDir);
	}
}

bool WindowsFirewallRepair::RunElevatedRepair(const CString &rstrProgramPath, const std::vector<WindowsFirewallRepairSeams::CFirewallRuleSpec> &rules, CRepairLaunchResult &rResult)
{
	rResult = CRepairLaunchResult{};
	rResult.rules = rules;
	if (rules.empty()) {
		rResult.bSucceeded = true;
		return true;
	}

	CString strError;
	if (!CreateRepairTempDirectory(rResult.strTempDir, strError)) {
		rResult.dwLastError = ::GetLastError();
		rResult.strErrorText = strError;
		return false;
	}

	rResult.strScriptPath = rResult.strTempDir + _T("\\repair-firewall.ps1");
	rResult.strResultPath = rResult.strTempDir + _T("\\repair-result.json");
	const CString strScript(WindowsFirewallRepairSeams::BuildRepairScript(rstrProgramPath, rules, rResult.strResultPath));
	if (!WriteUtf16PowerShellScript(rResult.strScriptPath, strScript, strError)) {
		rResult.dwLastError = ::GetLastError();
		rResult.strErrorText = strError;
		CleanupRepairTemp(rResult.strScriptPath, rResult.strResultPath, rResult.strTempDir);
		return false;
	}

	CString strParameters;
	strParameters.Format(_T("-NoProfile -ExecutionPolicy Bypass -File \"%s\""), (LPCTSTR)rResult.strScriptPath);

	SHELLEXECUTEINFO sei = {};
	sei.cbSize = sizeof(sei);
	sei.fMask = SEE_MASK_NOCLOSEPROCESS;
	sei.hwnd = NULL;
	sei.lpVerb = _T("runas");
	sei.lpFile = GetPowerShellPath();
	sei.lpParameters = strParameters;
	sei.lpDirectory = rResult.strTempDir;
	sei.nShow = SW_HIDE;
	if (::ShellExecuteEx(&sei) == FALSE) {
		rResult.dwLastError = ::GetLastError();
		rResult.bCancelled = rResult.dwLastError == ERROR_CANCELLED;
		rResult.strErrorText = GetErrorMessage(rResult.dwLastError);
		CleanupRepairTemp(rResult.strScriptPath, rResult.strResultPath, rResult.strTempDir);
		return false;
	}

	rResult.bStarted = true;
	(void)::WaitForSingleObject(sei.hProcess, INFINITE);
	if (::GetExitCodeProcess(sei.hProcess, &rResult.dwExitCode) == FALSE) {
		rResult.dwLastError = ::GetLastError();
		rResult.strErrorText = GetErrorMessage(rResult.dwLastError);
	}
	::CloseHandle(sei.hProcess);

	ReadResultJson(rResult.strResultPath, rResult.strResultJson);
	rResult.bSucceeded = rResult.dwExitCode == ERROR_SUCCESS;
	CleanupRepairTemp(rResult.strScriptPath, rResult.strResultPath, rResult.strTempDir);
	return rResult.bSucceeded;
}
