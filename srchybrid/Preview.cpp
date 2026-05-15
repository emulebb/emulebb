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
#include <io.h>
#include <sys/stat.h>
#include <share.h>
#include "Preferences.h"
#include "PartFile.h"
#include "Preview.h"
#include "MenuCmds.h"
#include "FileCompletionCommandSeams.h"
#include "LongPathSeams.h"
#include "OtherFunctions.h"
#include "PartFilePreviewSeams.h"
#include "SafeFile.h"
#include "emule.h"
#include "emuledlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


CPreviewApps thePreviewApps;

namespace
{
constexpr ULONGLONG kMaxThumbnailBytes = 16ull * 1024ull * 1024ull;
volatile LONG g_lVideoThumbnailProcessActive = 0;

class CScopedVideoThumbnailProcessSlot
{
public:
	CScopedVideoThumbnailProcessSlot()
		: m_bAcquired(::InterlockedCompareExchange(&g_lVideoThumbnailProcessActive, 1, 0) == 0)
	{
	}

	~CScopedVideoThumbnailProcessSlot()
	{
		if (m_bAcquired)
			(void)::InterlockedExchange(&g_lVideoThumbnailProcessActive, 0);
	}

	CScopedVideoThumbnailProcessSlot(const CScopedVideoThumbnailProcessSlot&) = delete;
	CScopedVideoThumbnailProcessSlot& operator=(const CScopedVideoThumbnailProcessSlot&) = delete;

	bool IsAcquired() const
	{
		return m_bAcquired;
	}

private:
	bool m_bAcquired;
};

bool RunVideoThumbnailCommand(const CString &rstrCommand, const CString &rstrWorkingDirectory, DWORD &rdwExitCode)
{
	CScopedVideoThumbnailProcessSlot slot;
	if (!slot.IsAcquired()) {
		rdwExitCode = ERROR_BUSY;
		return false;
	}

	STARTUPINFO startupInfo = {};
	startupInfo.cb = sizeof(startupInfo);
	startupInfo.dwFlags = STARTF_USESHOWWINDOW;
	startupInfo.wShowWindow = SW_HIDE;

	PROCESS_INFORMATION processInfo = {};
	CString strCommand(rstrCommand);
	LPTSTR pszCommand = strCommand.GetBuffer();
	const BOOL bStarted = ::CreateProcess(NULL, pszCommand, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL
		, rstrWorkingDirectory.IsEmpty() ? NULL : (LPCTSTR)rstrWorkingDirectory, &startupInfo, &processInfo);
	const DWORD dwError = bStarted ? ERROR_SUCCESS : ::GetLastError();
	strCommand.ReleaseBuffer();

	if (!bStarted) {
		rdwExitCode = dwError;
		return false;
	}

	const DWORD dwWait = ::WaitForSingleObject(processInfo.hProcess, PartFilePreviewSeams::kFfmpegThumbnailTimeoutMs);
	if (dwWait == WAIT_TIMEOUT) {
		(void)::TerminateProcess(processInfo.hProcess, WAIT_TIMEOUT);
		(void)::WaitForSingleObject(processInfo.hProcess, SEC2MS(2));
		rdwExitCode = WAIT_TIMEOUT;
	} else if (!::GetExitCodeProcess(processInfo.hProcess, &rdwExitCode))
		rdwExitCode = ::GetLastError();

	VERIFY(::CloseHandle(processInfo.hThread));
	VERIFY(::CloseHandle(processInfo.hProcess));
	return dwWait != WAIT_TIMEOUT;
}
}

bool ReadVideoThumbnailBitmapFile(const CString &rstrPath, HBITMAP &rhBitmap)
{
	CSafeFile file;
	if (!LongPathSeams::OpenFile(file, rstrPath, CFile::modeRead | CFile::shareDenyWrite | CFile::osSequentialScan))
		return false;
	const ULONGLONG ullLength = static_cast<ULONGLONG>(file.GetLength());
	if (ullLength == 0 || ullLength > kMaxThumbnailBytes)
		return false;
	byte *pBuffer = new byte[static_cast<size_t>(ullLength)];
	try {
		if (file.Read(pBuffer, static_cast<UINT>(ullLength)) != ullLength) {
			delete[] pBuffer;
			return false;
		}
		rhBitmap = mem2bmp(pBuffer, static_cast<size_t>(ullLength));
		delete[] pBuffer;
		return rhBitmap != NULL;
	} catch (...) {
		delete[] pBuffer;
		throw;
	}
}

///////////////////////////////////////////////////////////////////////////////
// CVideoThumbnailThread

IMPLEMENT_DYNCREATE(CVideoThumbnailThread, CWinThread)

CVideoThumbnailThread::CVideoThumbnailThread()
	: m_pPartfile()
	, m_strFfmpegPath()
	, m_strTitle()
	, m_strInputPath()
	, m_strWorkingDirectory()
	, m_strFileHash()
	, m_strCachePath()
	, m_hNotifyWnd()
	, m_ullFileSize()
	, m_ullCompletedSize()
{
}

BOOL CVideoThumbnailThread::InitInstance()
{
	DbgSetThreadName("VideoThumbnail");
	InitThreadLocale();
	return TRUE;
}

BOOL CVideoThumbnailThread::Run()
{
	VideoThumbnailResult_Struct *pResult = new VideoThumbnailResult_Struct;
	pResult->pPartFile = m_pPartfile;
	pResult->strFileHash = m_strFileHash;
	pResult->strCachePath = m_strCachePath;
	pResult->ullCompletedSize = m_ullCompletedSize;

	try {
		if (m_strInputPath.IsEmpty() || LongPathSeams::GetFileAttributes(m_strInputPath) == INVALID_FILE_ATTRIBUTES) {
			pResult->eResult = PartFilePreviewSeams::VTAR_COPY_FAILED;
			pResult->dwErrorCode = ::GetLastError();
		} else {
			DWORD dwExitCode = 0;
			const uint32 uStartSecond = PartFilePreviewSeams::GetVideoThumbnailCaptureStartSecond(m_ullFileSize, m_ullCompletedSize);
			if (LongPathSeams::GetFileAttributes(m_strCachePath) != INVALID_FILE_ATTRIBUTES && !LongPathSeams::DeleteFile(m_strCachePath)) {
				pResult->eResult = PartFilePreviewSeams::VTAR_CACHE_WRITE_FAILED;
				pResult->dwErrorCode = ::GetLastError();
			} else {
				const CString strCommandLine(PartFilePreviewSeams::BuildFfmpegThumbnailCommandLine(m_strFfmpegPath, m_strInputPath, m_strCachePath, uStartSecond));
				if (!RunVideoThumbnailCommand(strCommandLine, m_strWorkingDirectory, dwExitCode)) {
					pResult->dwErrorCode = dwExitCode;
					if (dwExitCode == ERROR_BUSY)
						pResult->eResult = PartFilePreviewSeams::VTAR_FFMPEG_BUSY;
					else if (dwExitCode == WAIT_TIMEOUT)
						pResult->eResult = PartFilePreviewSeams::VTAR_FFMPEG_TIMEOUT;
					else
						pResult->eResult = PartFilePreviewSeams::VTAR_FFMPEG_START_FAILED;
				} else {
					pResult->dwProcessExitCode = dwExitCode;
					HBITMAP hBitmap = NULL;
					if (dwExitCode != 0)
						pResult->eResult = PartFilePreviewSeams::VTAR_FFMPEG_FAILED;
					else if (LongPathSeams::GetFileAttributes(m_strCachePath) == INVALID_FILE_ATTRIBUTES)
						pResult->eResult = PartFilePreviewSeams::VTAR_NO_THUMBNAIL;
					else if (!ReadVideoThumbnailBitmapFile(m_strCachePath, hBitmap))
						pResult->eResult = PartFilePreviewSeams::VTAR_DECODE_FAILED;
					else {
						pResult->hBitmap = hBitmap;
						hBitmap = NULL;
						pResult->eResult = PartFilePreviewSeams::VTAR_SUCCESS;
					}
					if (hBitmap != NULL)
						::DeleteObject(hBitmap);
				}
			}
		}
	} catch (CFileException *ex) {
		pResult->eResult = PartFilePreviewSeams::VTAR_EXCEPTION;
		pResult->dwErrorCode = static_cast<DWORD>(ex->m_lOsError);
		ex->Delete();
	} catch (...) {
		pResult->eResult = PartFilePreviewSeams::VTAR_EXCEPTION;
	}

	if (!::IsWindow(m_hNotifyWnd) || !::PostMessage(m_hNotifyWnd, TM_VIDEOTHUMBNAILFINISHED, 0, reinterpret_cast<LPARAM>(pResult)))
		delete pResult;
	return TRUE;
}

void CVideoThumbnailThread::SetValues(CPartFile *pPartFile, LPCTSTR pszFfmpegPath, HWND hNotifyWnd, LPCTSTR pszCachePath)
{
	m_pPartfile = pPartFile;
	m_strFfmpegPath = pszFfmpegPath;
	m_strTitle = pPartFile->GetFileName();
	m_strInputPath = pPartFile->GetFilePath();
	m_strWorkingDirectory = pPartFile->GetTmpPath();
	m_strFileHash = md4str(pPartFile->GetFileHash());
	m_strCachePath = pszCachePath;
	m_hNotifyWnd = hNotifyWnd;
	m_ullFileSize = static_cast<uint64>(pPartFile->GetFileSize());
	m_ullCompletedSize = static_cast<uint64>(pPartFile->GetCompletedSize());
}

///////////////////////////////////////////////////////////////////////////////
// CPreviewThread

IMPLEMENT_DYNCREATE(CPreviewThread, CWinThread)

//BEGIN_MESSAGE_MAP(CPreviewThread, CWinThread)
//END_MESSAGE_MAP()

CPreviewThread::CPreviewThread()
	: m_pPartfile()
	, m_aFilled()
{
}

BOOL CPreviewThread::InitInstance()
{
	DbgSetThreadName("PartFilePreview");
	InitThreadLocale();
	return TRUE;
}

BOOL CPreviewThread::Run()
{
	try {
		const CString srcName(m_pPartfile->GetFileName());
		CString strPreviewName(m_pPartfile->GetTmpPath());
		strPreviewName.AppendFormat(_T("%s_preview%s"), (LPCTSTR)srcName.Left(5), ::PathFindExtension(srcName));

		bool bRet = m_pPartfile->CopyPartFile(m_aFilled, strPreviewName);
		m_pPartfile->m_bPreviewing = false;
		m_aFilled.RemoveAll();
		if (!bRet)
			return FALSE;

		SHELLEXECUTEINFO SE = {};
		SE.cbSize = (DWORD)sizeof SE;
		SE.fMask = SEE_MASK_NOCLOSEPROCESS;
		SE.nShow = SW_SHOW;

		if (m_strCommand.IsEmpty()) {
			// use the default verb for the document
			SE.lpFile = strPreviewName;
			ShellExecuteEx(&SE);
		} else {
			SE.lpVerb = _T("open");	// "open" the specified video player

			// get directory of video player application
			CString strCommandDir(m_strCommand);
			int iPos = strCommandDir.ReverseFind(_T('\\'));
			strCommandDir.Truncate(iPos + 1); //may be empty

			CString strArgs(m_strCommandArgs);
			if (!strArgs.IsEmpty())
				strArgs += _T(' ');
			if (strPreviewName.Find(_T(' ')) >= 0)
				strArgs.AppendFormat(_T("\"%s\""), (LPCTSTR)strPreviewName);
			else
				strArgs += strPreviewName;

			CString strCommand(m_strCommand);
			ExpandEnvironmentStrings(strCommand);
			ExpandEnvironmentStrings(strArgs);
			ExpandEnvironmentStrings(strCommandDir);
			SE.lpFile = strCommand;
			SE.lpParameters = strArgs;
			SE.lpDirectory = strCommandDir;
			ShellExecuteEx(&SE);
		}
		if (SE.hProcess) {
			::WaitForSingleObject(SE.hProcess, INFINITE);
			::CloseHandle(SE.hProcess);
		}
		(void)LongPathSeams::DeleteFileIfExists(strPreviewName);
	} catch (CFileException *ex) {
		m_pPartfile->m_bPreviewing = false;
		ex->Delete();
	}
	return TRUE;
}

void CPreviewThread::SetValues(CPartFile *pPartFile, LPCTSTR pszCommand, LPCTSTR pszCommandArgs)
{
	m_pPartfile = pPartFile;
	pPartFile->GetFilledArray(m_aFilled);
	m_strCommand = pszCommand;
	m_strCommandArgs = pszCommandArgs;
}


///////////////////////////////////////////////////////////////////////////////
// CPreviewApps

CPreviewApps::CPreviewApps()
	: m_tDefAppsFileLastModified()
	, m_pLastCheckedPartFile()
	, m_pLastPartFileApp()
{
}

CString CPreviewApps::GetDefaultAppsFile()
{
	return thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + _T("PreviewApps.dat");
}

void CPreviewApps::RemoveAllApps()
{
	m_aApps.RemoveAll();
	m_tDefAppsFileLastModified = 0;
}

INT_PTR CPreviewApps::ReadAllApps()
{
	RemoveAllApps();

	const CString &strFilePath(GetDefaultAppsFile());
	FILE *readFile = LongPathSeams::OpenFileStreamDenyWriteLongPath(strFilePath, _T("r"));
	if (readFile != NULL) {
		CString sbuffer;
		while (!feof(readFile)) {
			TCHAR buffer[1024];
			if (_fgetts(buffer, _countof(buffer), readFile) == NULL)
				break;
			sbuffer = buffer;
			sbuffer.TrimRight(_T("\r\n\t"));

			// ignore comments & too short lines
			if (buffer[0] == _T('#') || buffer[0] == _T('/') || sbuffer.GetLength() < 5)
				continue;

			int iPos = 0;
			CString strTitle(sbuffer.Tokenize(_T("="), iPos));
			if (!strTitle.Trim().IsEmpty()) {
				CString strCommand(sbuffer.Tokenize(_T(";"), iPos));
				if (strCommand.Trim().IsEmpty())
					continue;

				LPCTSTR pszCommandArgs = ::PathGetArgs((LPCTSTR)strCommand);
				if (pszCommandArgs)
					strCommand.Truncate((int)(pszCommandArgs - (LPCTSTR)strCommand));
				if (strCommand.Trim(_T(" \t\"")).IsEmpty())
					continue;

				uint64 ullMinCompletedSize = 0;
				uint64 ullMinStartOfFile = 0;
				CStringArray astrExtensions;
				for (; iPos >= 0;) {
					const CString &strParams(sbuffer.Tokenize(_T(";"), iPos));
					if (strParams.IsEmpty())
						break;
					int iPosParam = 0;
					CString strId(strParams.Tokenize(_T("="), iPosParam));
					if (strId.IsEmpty())
						continue;
					const CString &strValue(strParams.Tokenize(_T("="), iPosParam));
					if (strValue.IsEmpty())
						continue;
					strId.MakeLower();
					if (strId == _T("ext"))
						astrExtensions.Add((strValue[0] == _T('.')) ? strValue : _T('.') + strValue);
					else if (strId == _T("minsize"))
						(void)_stscanf(strValue, _T("%I64u"), &ullMinCompletedSize);
					else if (strId == _T("minstart"))
						(void)_stscanf(strValue, _T("%I64u"), &ullMinStartOfFile);
				}

				SPreviewApp svc;
				svc.strTitle = strTitle;
				svc.strCommand = strCommand;
				svc.strCommandArgs = CString(pszCommandArgs).Trim();
				svc.astrExtensions.Append(astrExtensions);
				svc.ullMinCompletedSize = ullMinCompletedSize;
				svc.ullMinStartOfFile = ullMinStartOfFile;
				m_aApps.Add(svc);
			}
		}
		struct _stat64 st;
		if (statUTC((HANDLE)_get_osfhandle(_fileno(readFile)), st) == 0)
			m_tDefAppsFileLastModified = (time_t)st.st_mtime;

		fclose(readFile);

	}

	return m_aApps.GetCount();
}

void CPreviewApps::UpdateApps()
{
	struct _stat64 st;
	if (m_aApps.IsEmpty() || (statUTC(GetDefaultAppsFile(), st) == 0 && st.st_mtime > m_tDefAppsFileLastModified))
		ReadAllApps();
}

int CPreviewApps::GetAllMenuEntries(CMenu &rMenu, const CPartFile *file)
{
	UpdateApps();
	int count = min((int)m_aApps.GetCount(), MP_PREVIEW_APP_MAX - MP_PREVIEW_APP_MIN + 1);
	bool bEnabled = (file && (uint64)file->GetCompletedSize() >= 16ull * 1024);
	for (int i = 0; i < count; ++i)
		rMenu.AppendMenu(MF_STRING | (bEnabled ? MF_ENABLED : MF_GRAYED), MP_PREVIEW_APP_MIN + i, m_aApps[i].strTitle);
	return count;
}

void CPreviewApps::RunApp(CPartFile *file, UINT uMenuID) const
{
	const SPreviewApp &svc(m_aApps[uMenuID - MP_PREVIEW_APP_MIN]);
	ExecutePartFile(file, svc.strCommand, svc.strCommandArgs);
}

void ExecutePartFile(CPartFile *file, LPCTSTR pszCommand, LPCTSTR pszCommandArgs)
{
	if (!CheckFileOpen(file->GetFilePath(), file->GetFileName()))
		return;

	// get directory of video player application
	CString strCommandDir(pszCommand);
	int iPos = strCommandDir.ReverseFind(_T('\\'));
	strCommandDir.Truncate(iPos + 1); //may be empty

	CString strArgs(pszCommandArgs);
	if (!strArgs.IsEmpty())
		strArgs += _T(' ');

	const CString &strQuotedPartFilePath(file->GetFilePath());
	// if the path contains spaces, quote the entire path
	if (strQuotedPartFilePath.Find(_T(' ')) >= 0)
		strArgs.AppendFormat(_T("\"%s\""), (LPCTSTR)strQuotedPartFilePath);
	else
		strArgs += strQuotedPartFilePath;

	file->FlushBuffer(true);

	CString strCommand(pszCommand);
	ExpandEnvironmentStrings(strCommand);
	ExpandEnvironmentStrings(strArgs);
	ExpandEnvironmentStrings(strCommandDir);

	LPCTSTR pszVerb;

	// Backward compatibility with old 'preview' command (when no preview application is specified):
	//	"ShellExecute(NULL, NULL, strPartFilePath, NULL, NULL, SW_SHOWNORMAL);"
	if (strCommand.IsEmpty()) {
		strCommand = strArgs;
		strArgs.Empty();
		strCommandDir.Empty();
		pszVerb = NULL;
	} else
		pszVerb = _T("open");

	TRACE(_T("Starting preview application:\n"));
	TRACE(_T("  Command =%s\n"), (LPCTSTR)strCommand);
	TRACE(_T("  Args    =%s\n"), (LPCTSTR)strArgs);
	TRACE(_T("  Dir     =%s\n"), (LPCTSTR)strCommandDir);
	DWORD dwError = (DWORD)ShellExecute(NULL, pszVerb, strCommand, strArgs.IsEmpty() ? NULL : (LPCTSTR)strArgs, strCommandDir.IsEmpty() ? NULL : (LPCTSTR)strCommandDir, SW_SHOWNORMAL);
	if (dwError <= 32) {
		//
		// Unfortunately, Windows may already have shown an error dialog which tells
		// the user about the failed 'ShellExecute' call. *BUT* that error dialog is not
		// shown in each case!
		//
		// Examples:
		//	 -	Specifying an executable which does not exist (e.g. APP.EXE)
		//		-> (Error 2) -> No error is shown.
		//
		//   -	Executing a document (verb "open") which has an unregistered extension
		//		-> (Error 31) -> Error is shown.
		//
		// I'm not sure whether this behaviour (showing an error dialog in cases of some
		// specific errors) is handled the same way in all Windows version -> therefore I
		// decide to always show an application specific error dialog!
		//
		CString strMsg;
		strMsg.Format(_T("Failed to execute: %s %s"), (LPCTSTR)strCommand, (LPCTSTR)strArgs);

		LPCTSTR strSysErrMsg = GetShellExecuteErrMsg(dwError);
		if (*strSysErrMsg)
			strMsg.AppendFormat(_T("\r\n\r\n%s"), strSysErrMsg);

		AfxMessageBox(strMsg, MB_ICONSTOP);
	}
}

int CPreviewApps::GetPreviewApp(const CPartFile *file)
{
	LPCTSTR pszExt = ::PathFindExtension(file->GetFileName());
	if (*pszExt) {
		UpdateApps();
		for (INT_PTR i = m_aApps.GetCount(); --i >= 0;) {
			const SPreviewApp &rApp(m_aApps[i]);
			for (INT_PTR j = rApp.astrExtensions.GetCount(); --j >= 0;)
				if (rApp.astrExtensions[j].CompareNoCase(pszExt) == 0)
					return (int)i;
		}
	}
	return -1;
}

CPreviewApps::ECanPreviewRes CPreviewApps::CanPreview(const CPartFile *file)
{
	int iApp = GetPreviewApp(file);
	if (iApp == -1)
		return NotHandled;

	const SPreviewApp &rApp = m_aApps[iApp];
	if ((uint64)file->GetCompletedSize() < rApp.ullMinCompletedSize)
		return No;

	if (rApp.ullMinStartOfFile &&!file->IsComplete(0, min(rApp.ullMinStartOfFile, (uint64)file->GetFileSize()) - 1))
		return No;

	return Yes;
}

bool CPreviewApps::Preview(CPartFile *file)
{
	int iApp = GetPreviewApp(file);
	if (iApp < 0)
		return false;
	RunApp(file, MP_PREVIEW_APP_MIN + iApp);
	return true;
}
