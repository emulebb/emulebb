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
#include <afxinet.h>
#include <io.h>
#include "DirectDownload.h"
#include "emule.h"
#include "LongPathSeams.h"
#include "WinInetHandle.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace DirectDownload
{
bool CreateTempPathInDirectory(const CString& strDirectory, LPCTSTR pszPrefix, CString& strTempPath, CString& strError)
{
	LongPathSeams::PathString strTempPathLong;
	if (!LongPathSeams::CreateUniqueTempFilePath(strDirectory, pszPrefix, strTempPathLong)) {
		strError.Format(_T("CreateUniqueTempFilePath failed for %s (%u)"), (LPCTSTR)strDirectory, ::GetLastError());
		return false;
	}

	strTempPath = strTempPathLong.c_str();
	return true;
}

bool DownloadUrlToFile(const CString& strUrl, const CString& strTargetPath, CString& strError)
{
	strError.Empty();

	WinInetUtil::CInternetHandle hInternetSession(::InternetOpen(AfxGetAppName(), INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0));
	if (!hInternetSession) {
		strError.Format(_T("InternetOpen failed (%u)"), ::GetLastError());
		return false;
	}

	TCHAR szHostName[INTERNET_MAX_HOST_NAME_LENGTH] = {};
	TCHAR szUrlPath[2048] = {};
	TCHAR szExtraInfo[2048] = {};
	URL_COMPONENTS components = {};
	components.dwStructSize = sizeof(components);
	components.lpszHostName = szHostName;
	components.dwHostNameLength = _countof(szHostName);
	components.lpszUrlPath = szUrlPath;
	components.dwUrlPathLength = _countof(szUrlPath);
	components.lpszExtraInfo = szExtraInfo;
	components.dwExtraInfoLength = _countof(szExtraInfo);
	if (!::InternetCrackUrl(strUrl, 0, 0, &components)) {
		strError.Format(_T("InternetCrackUrl failed (%u)"), ::GetLastError());
		return false;
	}

	CString strObject(components.lpszUrlPath, components.dwUrlPathLength);
	strObject.Append(CString(components.lpszExtraInfo, components.dwExtraInfoLength));
	const DWORD dwServiceType = INTERNET_SERVICE_HTTP;
	WinInetUtil::CInternetHandle hHttpConnection(::InternetConnect(hInternetSession.Get(),
		CString(components.lpszHostName, components.dwHostNameLength),
		components.nPort,
		NULL,
		NULL,
		dwServiceType,
		0,
		0));
	if (!hHttpConnection) {
		strError.Format(_T("InternetConnect failed (%u)"), ::GetLastError());
		return false;
	}

	LPCTSTR pszAcceptTypes[] = { _T("*/*"), NULL };
	DWORD dwFlags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_DONT_CACHE | INTERNET_FLAG_KEEP_CONNECTION;
	if (components.nScheme == INTERNET_SCHEME_HTTPS)
		dwFlags |= INTERNET_FLAG_SECURE;

	WinInetUtil::CInternetHandle hHttpFile(::HttpOpenRequest(hHttpConnection.Get(), _T("GET"), strObject, NULL, NULL, pszAcceptTypes, dwFlags, 0));
	if (!hHttpFile) {
		strError.Format(_T("HttpOpenRequest failed (%u)"), ::GetLastError());
		return false;
	}

	::HttpAddRequestHeaders(hHttpFile.Get(), _T("Accept-Encoding: identity\r\n"), _UI32_MAX, HTTP_ADDREQ_FLAG_ADD);
	if (!::HttpSendRequest(hHttpFile.Get(), NULL, 0, NULL, 0)) {
		strError.Format(_T("HttpSendRequest failed (%u)"), ::GetLastError());
		return false;
	}

	DWORD dwStatusCode = 0;
	DWORD dwStatusLength = sizeof(dwStatusCode);
	if (!::HttpQueryInfo(hHttpFile.Get(), HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &dwStatusCode, &dwStatusLength, NULL) || dwStatusCode != HTTP_STATUS_OK) {
		strError.Format(_T("Unexpected HTTP status %u"), static_cast<unsigned>(dwStatusCode));
		return false;
	}

	const int fdOut = LongPathSeams::OpenCrtWriteOnlyLongPath(strTargetPath, CREATE_ALWAYS, FILE_SHARE_READ);
	if (fdOut == -1) {
		strError.Format(_T("Could not open %s for writing"), (LPCTSTR)strTargetPath);
		return false;
	}

	BYTE buffer[16 * 1024] = {};
	DWORD dwBytesRead = 0;
	bool bSuccess = true;
	do {
		if (!::InternetReadFile(hHttpFile.Get(), buffer, sizeof(buffer), &dwBytesRead)) {
			strError.Format(_T("InternetReadFile failed (%u)"), ::GetLastError());
			bSuccess = false;
			break;
		}

		if (dwBytesRead > 0) {
			if (_write(fdOut, buffer, dwBytesRead) != static_cast<int>(dwBytesRead)) {
				strError.Format(_T("Write failed for %s (%u)"), (LPCTSTR)strTargetPath, errno);
				bSuccess = false;
				break;
			}
		}
	} while (dwBytesRead != 0);

	if (_close(fdOut) != 0 && bSuccess) {
		strError.Format(_T("Close failed for %s (%u)"), (LPCTSTR)strTargetPath, errno);
		bSuccess = false;
	}

	if (!bSuccess)
		(void)LongPathSeams::DeleteFileIfExists(strTargetPath);
	return bSuccess;
}
}
