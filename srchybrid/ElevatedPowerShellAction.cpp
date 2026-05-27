//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
#include "stdafx.h"
#include "ElevatedPowerShellAction.h"
#include "LongPathSeams.h"
#include "OtherFunctions.h"
#include "PathHelpers.h"
#include "ProcessLaunchSeams.h"

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

	bool CreateActionTempDirectory(const CString &rstrTempPrefix, CString &rstrTempDir, CString &rstrError)
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
			strCandidate.Format(_T("%s%s-%s"), (LPCTSTR)strTempRoot, (LPCTSTR)rstrTempPrefix, (LPCTSTR)strGuid);
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

	CString GetPowerShellPath()
	{
		const CString strSystemDir(PathHelpers::GetSystemDirectoryPath());
		if (strSystemDir.IsEmpty())
			return CString();

		const CString strPath(PathHelpers::AppendPathComponent(strSystemDir, _T("WindowsPowerShell\\v1.0\\powershell.exe")));
		if (LongPathSeams::PathExists(strPath))
			return strPath;

		::SetLastError(ERROR_FILE_NOT_FOUND);
		return CString();
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

	CString GetBundledScriptPath(const CString &rstrScriptLeafName)
	{
		const CString strModulePath(PathHelpers::GetModuleFilePath(NULL));
		if (strModulePath.IsEmpty())
			return CString();
		return PathHelpers::AppendPathComponent(
			PathHelpers::AppendPathComponent(PathHelpers::GetDirectoryPath(strModulePath), _T("scripts")),
			rstrScriptLeafName);
	}

	void CleanupActionTemp(const CString &rstrResultPath, const CString &rstrTempDir)
	{
		(void)LongPathSeams::DeleteFileIfExists(rstrResultPath);
		if (!rstrTempDir.IsEmpty())
			(void)LongPathSeams::RemoveDirectory(rstrTempDir);
	}
}

CString ElevatedPowerShellAction::QuotePowerShellArgument(const CString &rstrValue)
{
	CString strQuoted(_T("'"));
	for (int i = 0; i < rstrValue.GetLength(); ++i) {
		if (rstrValue[i] == _T('\''))
			strQuoted += _T("''");
		else
			strQuoted += rstrValue[i];
	}
	strQuoted += _T("'");
	return strQuoted;
}

bool ElevatedPowerShellAction::PrepareBundledScript(const CString &rstrTempPrefix, const CString &rstrScriptLeafName, const CString &rstrResultLeafName, CLaunchResult &rResult)
{
	rResult = CLaunchResult{};

	rResult.strScriptPath = GetBundledScriptPath(rstrScriptLeafName);
	if (rResult.strScriptPath.IsEmpty() || !LongPathSeams::PathExists(rResult.strScriptPath)) {
		rResult.dwLastError = ERROR_FILE_NOT_FOUND;
		rResult.strErrorText.Format(_T("Bundled eMuleBB script is missing: %s"), (LPCTSTR)rResult.strScriptPath);
		return false;
	}
	if (rstrResultLeafName.IsEmpty())
		return true;

	CString strError;
	if (!CreateActionTempDirectory(rstrTempPrefix, rResult.strTempDir, strError)) {
		rResult.dwLastError = ::GetLastError();
		rResult.strErrorText = strError;
		return false;
	}

	rResult.strResultPath = rResult.strTempDir + _T("\\") + rstrResultLeafName;
	return true;
}

bool ElevatedPowerShellAction::RunBundledScript(const CString &rstrArguments, bool bElevated, bool bWaitForExit, CLaunchResult &rResult)
{
	if (rResult.strScriptPath.IsEmpty() || !LongPathSeams::PathExists(rResult.strScriptPath)) {
		rResult.dwLastError = ERROR_FILE_NOT_FOUND;
		rResult.strErrorText.Format(_T("Bundled eMuleBB script is missing: %s"), (LPCTSTR)rResult.strScriptPath);
		CleanupActionTemp(rResult.strResultPath, rResult.strTempDir);
		return false;
	}

	CString strParameters;
	strParameters.Format(_T("-NoProfile -ExecutionPolicy Bypass -File \"%s\""), (LPCTSTR)rResult.strScriptPath);
	if (!rstrArguments.IsEmpty()) {
		strParameters += _T(" ");
		strParameters += rstrArguments;
	}

	const CString strWorkingDirectory(PathHelpers::GetDirectoryPath(rResult.strScriptPath));
	const CString strPowerShellPath(GetPowerShellPath());
	if (strPowerShellPath.IsEmpty()) {
		rResult.dwLastError = ::GetLastError();
		rResult.strErrorText = GetErrorMessage(rResult.dwLastError);
		CleanupActionTemp(rResult.strResultPath, rResult.strTempDir);
		return false;
	}

	SHELLEXECUTEINFO sei = {};
	sei.cbSize = sizeof(sei);
	sei.fMask = bWaitForExit ? SEE_MASK_NOCLOSEPROCESS : 0;
	sei.hwnd = NULL;
	sei.lpVerb = bElevated ? _T("runas") : NULL;
	sei.lpFile = strPowerShellPath;
	sei.lpParameters = strParameters;
	sei.lpDirectory = strWorkingDirectory;
	sei.nShow = SW_SHOWNORMAL;
	if (ShellExecuteExNormalized(sei) == FALSE) {
		rResult.dwLastError = ::GetLastError();
		rResult.bCancelled = rResult.dwLastError == ERROR_CANCELLED;
		rResult.strErrorText = GetErrorMessage(rResult.dwLastError);
		CleanupActionTemp(rResult.strResultPath, rResult.strTempDir);
		return false;
	}

	rResult.bStarted = true;
	if (!bWaitForExit) {
		rResult.bSucceeded = true;
		return true;
	}

	const DWORD dwWaitResult = ::WaitForSingleObject(sei.hProcess, ProcessLaunchSeams::kElevatedPowerShellActionTimeoutMs);
	const ProcessLaunchSeams::EProcessWaitResult eWaitResult = ProcessLaunchSeams::ClassifyProcessWaitResult(dwWaitResult);
	if (eWaitResult == ProcessLaunchSeams::EProcessWaitResult::TimedOut) {
		rResult.dwLastError = WAIT_TIMEOUT;
		rResult.dwExitCode = WAIT_TIMEOUT;
		rResult.strErrorText = _T("Elevated PowerShell action timed out.");
		(void)::TerminateProcess(sei.hProcess, WAIT_TIMEOUT);
		(void)::WaitForSingleObject(sei.hProcess, ProcessLaunchSeams::kTimedOutProcessTerminateWaitMs);
		::CloseHandle(sei.hProcess);
		CleanupActionTemp(rResult.strResultPath, rResult.strTempDir);
		return false;
	}
	if (eWaitResult != ProcessLaunchSeams::EProcessWaitResult::Completed) {
		rResult.dwLastError = eWaitResult == ProcessLaunchSeams::EProcessWaitResult::Failed ? ::GetLastError() : dwWaitResult;
		rResult.strErrorText = GetErrorMessage(rResult.dwLastError);
		::CloseHandle(sei.hProcess);
		CleanupActionTemp(rResult.strResultPath, rResult.strTempDir);
		return false;
	}
	if (::GetExitCodeProcess(sei.hProcess, &rResult.dwExitCode) == FALSE) {
		rResult.dwLastError = ::GetLastError();
		rResult.strErrorText = GetErrorMessage(rResult.dwLastError);
	}
	::CloseHandle(sei.hProcess);

	ReadResultJson(rResult.strResultPath, rResult.strResultJson);
	rResult.bSucceeded = rResult.dwExitCode == ERROR_SUCCESS;
	CleanupActionTemp(rResult.strResultPath, rResult.strTempDir);
	return rResult.bSucceeded;
}
