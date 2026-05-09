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
#include "DirectDownloadSeams.h"
#include "emule.h"
#include "LongPathSeams.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace DirectDownload
{
/**
 * @brief Owns one WinInet handle while coordinating close ownership with an optional cancellation token.
 */
class CRegisteredInternetHandle
{
public:
	CRegisteredInternetHandle(CDownloadCancellation::InternetHandleSlot eSlot, const std::shared_ptr<CDownloadCancellation>& pCancellation) noexcept
		: m_eSlot(eSlot)
		, m_pCancellation(pCancellation)
		, m_hInternet(NULL)
	{
	}

	CRegisteredInternetHandle(const CRegisteredInternetHandle&) = delete;
	CRegisteredInternetHandle& operator=(const CRegisteredInternetHandle&) = delete;

	~CRegisteredInternetHandle()
	{
		Reset();
	}

	/**
	 * @brief Returns true when this object currently owns a non-null handle.
	 */
	explicit operator bool() const noexcept
	{
		return m_hInternet != NULL;
	}

	/**
	 * @brief Returns the wrapped raw WinInet handle for API calls.
	 */
	HINTERNET Get() const noexcept
	{
		return m_hInternet;
	}

	/**
	 * @brief Replaces the wrapped handle and closes the previous handle unless cancellation already consumed it.
	 */
	void Reset(HINTERNET hInternet = NULL) noexcept
	{
		if (m_hInternet != NULL && m_hInternet != hInternet) {
			bool bCloseHandle = true;
			if (m_pCancellation)
				bCloseHandle = m_pCancellation->ReleaseHandle(m_eSlot, m_hInternet);
			if (bCloseHandle)
				::InternetCloseHandle(m_hInternet);
		}

		m_hInternet = NULL;
		if (hInternet == NULL)
			return;

		if (m_pCancellation) {
			if (!m_pCancellation->RegisterHandle(m_eSlot, hInternet)) {
				::InternetCloseHandle(hInternet);
				return;
			}

			m_hInternet = hInternet;
			return;
		}
		m_hInternet = hInternet;
	}

private:
	CDownloadCancellation::InternetHandleSlot m_eSlot;
	std::shared_ptr<CDownloadCancellation> m_pCancellation;
	HINTERNET m_hInternet;
};

namespace
{
	const DWORD DIRECT_DOWNLOAD_CONNECT_TIMEOUT_MS = 30000;
	const DWORD DIRECT_DOWNLOAD_SEND_TIMEOUT_MS = 30000;
	const DWORD DIRECT_DOWNLOAD_RECEIVE_TIMEOUT_MS = 30000;
	const ULONGLONG DIRECT_DOWNLOAD_TOTAL_TIMEOUT_MS = 5ull * 60ull * 1000ull;

	/**
	 * @brief Applies one WinInet timeout and reports failure through the download error string.
	 */
	bool SetInternetTimeout(HINTERNET hInternet, DWORD dwOption, DWORD dwTimeoutMs, CString& strError)
	{
		if (::InternetSetOption(hInternet, dwOption, &dwTimeoutMs, sizeof(dwTimeoutMs)))
			return true;

		strError.Format(_T("InternetSetOption(%u) failed (%u)"), static_cast<unsigned>(dwOption), ::GetLastError());
		return false;
	}

	/**
	 * @brief Checks the total background download deadline across repeated reads.
	 */
	bool HasDownloadDeadlineExpired(ULONGLONG ullStartTick)
	{
		return ::GetTickCount64() - ullStartTick >= DIRECT_DOWNLOAD_TOTAL_TIMEOUT_MS;
	}

	/**
	 * @brief Reports whether an owner has requested cancellation.
	 */
	bool IsDownloadCancelled(const std::shared_ptr<CDownloadCancellation>& pCancellation, CString& strError)
	{
		if (!pCancellation || !pCancellation->IsCancelled())
			return false;

		strError = _T("Download cancelled");
		return true;
	}
}

CDownloadCancellation::CDownloadCancellation() noexcept
	: m_hInternet{}
	, m_bCancelled(false)
{
}

void CDownloadCancellation::Cancel() noexcept
{
	HINTERNET ahInternet[static_cast<size_t>(InternetHandleSlot::Count)] = {};

	{
		CSingleLock lock(&m_lock, TRUE);
		m_bCancelled = true;
		for (size_t uIndex = 0; uIndex < static_cast<size_t>(InternetHandleSlot::Count); ++uIndex) {
			ahInternet[uIndex] = m_hInternet[uIndex];
			m_hInternet[uIndex] = NULL;
		}
	}

	for (size_t uIndex = static_cast<size_t>(InternetHandleSlot::Count); uIndex > 0; --uIndex) {
		HINTERNET hInternet = ahInternet[uIndex - 1];
		if (hInternet != NULL)
			::InternetCloseHandle(hInternet);
	}
}

bool CDownloadCancellation::IsCancelled() const noexcept
{
	CSingleLock lock(&m_lock, TRUE);
	return m_bCancelled;
}

bool CDownloadCancellation::RegisterHandle(InternetHandleSlot eSlot, HINTERNET hInternet) noexcept
{
	if (hInternet == NULL)
		return false;

	CSingleLock lock(&m_lock, TRUE);
	if (!DirectDownloadSeams::ShouldRegisterInternetHandleForCancellationState(m_bCancelled))
		return false;

	const size_t uSlot = static_cast<size_t>(eSlot);
	ASSERT(uSlot < static_cast<size_t>(InternetHandleSlot::Count));
	ASSERT(m_hInternet[uSlot] == NULL);
	m_hInternet[uSlot] = hInternet;
	return true;
}

bool CDownloadCancellation::ReleaseHandle(InternetHandleSlot eSlot, HINTERNET hInternet) noexcept
{
	if (hInternet == NULL)
		return false;

	CSingleLock lock(&m_lock, TRUE);
	const size_t uSlot = static_cast<size_t>(eSlot);
	ASSERT(uSlot < static_cast<size_t>(InternetHandleSlot::Count));
	if (m_hInternet[uSlot] != hInternet)
		return false;

	m_hInternet[uSlot] = NULL;
	return true;
}

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
	return DownloadUrlToFile(strUrl, strTargetPath, strError, std::shared_ptr<CDownloadCancellation>());
}

bool DownloadUrlToFile(const CString& strUrl, const CString& strTargetPath, CString& strError, const std::shared_ptr<CDownloadCancellation>& pCancellation)
{
	strError.Empty();

	if (IsDownloadCancelled(pCancellation, strError))
		return false;

	CRegisteredInternetHandle hInternetSession(CDownloadCancellation::InternetHandleSlot::Session, pCancellation);
	hInternetSession.Reset(::InternetOpen(AfxGetAppName(), INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0));
	if (!hInternetSession) {
		if (!IsDownloadCancelled(pCancellation, strError))
			strError.Format(_T("InternetOpen failed (%u)"), ::GetLastError());
		return false;
	}
	if (IsDownloadCancelled(pCancellation, strError))
		return false;
	if (!SetInternetTimeout(hInternetSession.Get(), INTERNET_OPTION_CONNECT_TIMEOUT, DIRECT_DOWNLOAD_CONNECT_TIMEOUT_MS, strError))
		return false;

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
	if (IsDownloadCancelled(pCancellation, strError))
		return false;
	CRegisteredInternetHandle hHttpConnection(CDownloadCancellation::InternetHandleSlot::Connection, pCancellation);
	hHttpConnection.Reset(::InternetConnect(hInternetSession.Get(),
		CString(components.lpszHostName, components.dwHostNameLength),
		components.nPort,
		NULL,
		NULL,
		dwServiceType,
		0,
		0));
	if (!hHttpConnection) {
		if (!IsDownloadCancelled(pCancellation, strError))
			strError.Format(_T("InternetConnect failed (%u)"), ::GetLastError());
		return false;
	}
	if (IsDownloadCancelled(pCancellation, strError))
		return false;

	LPCTSTR pszAcceptTypes[] = { _T("*/*"), NULL };
	DWORD dwFlags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_DONT_CACHE | INTERNET_FLAG_KEEP_CONNECTION;
	if (components.nScheme == INTERNET_SCHEME_HTTPS)
		dwFlags |= INTERNET_FLAG_SECURE;

	CRegisteredInternetHandle hHttpFile(CDownloadCancellation::InternetHandleSlot::Request, pCancellation);
	hHttpFile.Reset(::HttpOpenRequest(hHttpConnection.Get(), _T("GET"), strObject, NULL, NULL, pszAcceptTypes, dwFlags, 0));
	if (!hHttpFile) {
		if (!IsDownloadCancelled(pCancellation, strError))
			strError.Format(_T("HttpOpenRequest failed (%u)"), ::GetLastError());
		return false;
	}
	if (IsDownloadCancelled(pCancellation, strError))
		return false;
	if (!SetInternetTimeout(hHttpFile.Get(), INTERNET_OPTION_SEND_TIMEOUT, DIRECT_DOWNLOAD_SEND_TIMEOUT_MS, strError)
		|| !SetInternetTimeout(hHttpFile.Get(), INTERNET_OPTION_RECEIVE_TIMEOUT, DIRECT_DOWNLOAD_RECEIVE_TIMEOUT_MS, strError))
		return false;

	::HttpAddRequestHeaders(hHttpFile.Get(), _T("Accept-Encoding: identity\r\n"), _UI32_MAX, HTTP_ADDREQ_FLAG_ADD);
	const ULONGLONG ullDownloadStartTick = ::GetTickCount64();
	if (IsDownloadCancelled(pCancellation, strError))
		return false;
	if (!::HttpSendRequest(hHttpFile.Get(), NULL, 0, NULL, 0)) {
		if (!IsDownloadCancelled(pCancellation, strError))
			strError.Format(_T("HttpSendRequest failed (%u)"), ::GetLastError());
		return false;
	}
	if (IsDownloadCancelled(pCancellation, strError))
		return false;

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
		if (IsDownloadCancelled(pCancellation, strError)) {
			bSuccess = false;
			break;
		}
		if (HasDownloadDeadlineExpired(ullDownloadStartTick)) {
			strError.Format(_T("Download timed out after %u seconds"), static_cast<unsigned>(DIRECT_DOWNLOAD_TOTAL_TIMEOUT_MS / 1000ull));
			bSuccess = false;
			break;
		}
		if (!::InternetReadFile(hHttpFile.Get(), buffer, sizeof(buffer), &dwBytesRead)) {
			if (!IsDownloadCancelled(pCancellation, strError))
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
