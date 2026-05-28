//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#include "stdafx.h"
#include <dbghelp.h>
#include "LogArtifactNames.h"
#include "LongPathSeams.h"
#include "PathHelpers.h"
#include "mdump.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


CMiniDumper theCrashDumper;
CString CMiniDumper::m_strAppName;
CString CMiniDumper::m_strDumpDir;

namespace
{
	MINIDUMP_TYPE GetManualDumpType(bool bFullMemoryDump)
	{
		DWORD dwDumpType = static_cast<DWORD>(MiniDumpNormal)
			| static_cast<DWORD>(MiniDumpWithThreadInfo)
			| static_cast<DWORD>(MiniDumpWithUnloadedModules)
			| static_cast<DWORD>(MiniDumpWithProcessThreadData);

		if (bFullMemoryDump) {
			dwDumpType |= static_cast<DWORD>(MiniDumpWithFullMemory)
				| static_cast<DWORD>(MiniDumpWithHandleData)
				| static_cast<DWORD>(MiniDumpWithFullMemoryInfo);
		}

		return static_cast<MINIDUMP_TYPE>(dwDumpType);
	}

	MINIDUMP_TYPE GetCrashDumpType(bool bFullMemoryDump)
	{
		return bFullMemoryDump ? GetManualDumpType(true) : MiniDumpNormal;
	}

}

void CMiniDumper::Enable(LPCTSTR pszAppName, bool bShowErrors, LPCTSTR pszDumpDir)
{
	// This assert fires if you have two instances of CMiniDumper which is not allowed
	ASSERT(m_strAppName.IsEmpty());
	m_strAppName = (pszAppName != NULL) ? pszAppName : _T("");

	// eMule may not have the permission to create a DMP file in the directory where the "emulebb.exe" is located.
	// Need to pre-determine a valid directory.
	m_strDumpDir = PathHelpers::EnsureTrailingSeparator((pszDumpDir != NULL) ? CString(pszDumpDir) : CString());

	UNREFERENCED_PARAMETER(bShowErrors);
	::SetUnhandledExceptionFilter(TopLevelFilter);
}

CMiniDumper::SManualDumpResult CMiniDumper::CreateManualDump(LPCTSTR, LPCTSTR pszDumpDir, bool bFullMemoryDump)
{
	SManualDumpResult result = {false, ERROR_SUCCESS, CString()};

	const CString strRootDumpDir(PathHelpers::EnsureTrailingSeparator((pszDumpDir != NULL) ? CString(pszDumpDir) : CString()));
	const CString strDumpDir(PathHelpers::AppendPathComponent(strRootDumpDir, _T("dumps")));
	if (!LongPathSeams::PathExists(strDumpDir) && !LongPathSeams::CreateDirectory(strDumpDir, NULL) && ::GetLastError() != ERROR_ALREADY_EXISTS) {
		result.dwError = ::GetLastError();
		return result;
	}

	SYSTEMTIME timeNow;
	::GetLocalTime(&timeNow);
	const DWORD dwProcessId = ::GetCurrentProcessId();
	const MINIDUMP_TYPE eDumpType = GetManualDumpType(bFullMemoryDump);

	for (int iAttempt = 0; iAttempt < 100; ++iAttempt) {
		const CString strDumpPath(PathHelpers::AppendPathComponent(strDumpDir, LogArtifactNames::BuildManualDumpFileName(timeNow, dwProcessId, bFullMemoryDump, iAttempt)));
		HANDLE hFile = LongPathSeams::CreateFile(strDumpPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile == INVALID_HANDLE_VALUE) {
			const DWORD dwError = ::GetLastError();
			if (dwError == ERROR_FILE_EXISTS)
				continue;
			result.dwError = dwError;
			return result;
		}

		const BOOL bWritten = ::MiniDumpWriteDump(GetCurrentProcess(), dwProcessId, hFile, eDumpType, NULL, NULL, NULL);
		const DWORD dwError = bWritten ? ERROR_SUCCESS : ::GetLastError();
		::CloseHandle(hFile);
		// MiniDumpWriteDump can leave a partial file on failure; do not advertise or retain it as evidence.
		if (!bWritten)
			(void)LongPathSeams::DeleteFileIfExists(strDumpPath);

		result.strDumpPath = strDumpPath;
		result.dwError = dwError;
		result.bSuccess = bWritten != FALSE;
		return result;
	}

	result.dwError = ERROR_ALREADY_EXISTS;
	return result;
}

#define CRASHTEXT _T("eMule crashed :-(\r\n\r\n") \
	_T("A diagnostic file can be created which will help the author to resolve this problem.\r\n") \
	_T("This file will be saved on your Disk (and not sent).\r\n\r\n") \
	_T("Do you want to create this file now?")

LONG WINAPI CMiniDumper::TopLevelFilter(struct _EXCEPTION_POINTERS *pExceptionInfo) noexcept
{
#ifdef _DEBUG
	LONG lRetValue = EXCEPTION_CONTINUE_SEARCH;
#endif
	const bool bAutoCreateDump = (theCrashDumper.uCreateCrashDump == 2);
	SYSTEMTIME t;
	::GetLocalTime(&t); //time of this crash
	// Ask user to confirm writing a dump file
	// Do *NOT* localize that string (in fact, do not use MFC to load it)!
	if (bAutoCreateDump || MessageBox(NULL, CRASHTEXT, m_strAppName, MB_ICONSTOP | MB_YESNO) == IDYES) {
		// Create full path for the dump file
		const CString strDumpPath(PathHelpers::AppendPathComponent(m_strDumpDir, LogArtifactNames::BuildCrashDumpFileName(t, GetCurrentProcessId())));

		CString strResult;
		HANDLE hFile = LongPathSeams::CreateFile(strDumpPath, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile != INVALID_HANDLE_VALUE) {
			_MINIDUMP_EXCEPTION_INFORMATION ExInfo = _MINIDUMP_EXCEPTION_INFORMATION{GetCurrentThreadId(), pExceptionInfo, FALSE};
			const MINIDUMP_TYPE eDumpType = GetCrashDumpType(theCrashDumper.bCaptureFullCrashDump);
			BOOL bOK = ::MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, eDumpType, &ExInfo, NULL, NULL);
			const DWORD dwDumpError = bOK ? ERROR_SUCCESS : ::GetLastError();
			if (bOK) {
				// Do *NOT* localize this string (in fact, do not use MFC to load it)!
				strResult.Format(
					_T("Saved dump file to \"%s\".\r\n\r\n")
					_T("Please attach this file to a detailed bug report at forum.emule-project.net\r\n\r\n")
					_T("Thank you for helping to improve eMule!"),
					(LPCTSTR)strDumpPath);
#ifdef _DEBUG
				lRetValue = EXCEPTION_EXECUTE_HANDLER;
#endif
			} else {
				// Do *NOT* localize this string (in fact, do not use MFC to load it)!
				strResult.Format(_T("Failed to save dump file to \"%s\".\r\n\r\nError: %lu")
					, (LPCTSTR)strDumpPath, dwDumpError);
			}
			::CloseHandle(hFile);
			// Keep crash folders clean when dbghelp creates the file but cannot complete the stream.
			if (!bOK)
				(void)LongPathSeams::DeleteFileIfExists(strDumpPath);
		} else {
			// Do *NOT* localize this string (in fact, do not use MFC to load it)!
			strResult.Format(_T("Failed to create dump file \"%s\".\r\n\r\nError: %lu")
				, (LPCTSTR)strDumpPath, ::GetLastError());
		}
		if (!bAutoCreateDump && !strResult.IsEmpty())
			::MessageBox(NULL, strResult, m_strAppName, MB_ICONINFORMATION | MB_OK);
	}

#ifndef _DEBUG
	// Exit the process only in release builds, so that in debug builds the exception
	// is passed to an installed debugger
	const DWORD dwExitCode = (pExceptionInfo != NULL && pExceptionInfo->ExceptionRecord != NULL)
		? pExceptionInfo->ExceptionRecord->ExceptionCode
		: 1;
	ExitProcess(dwExitCode);
#else
	return lRetValue;
#endif
}
