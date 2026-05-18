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
#include "HttpTransfer.h"
#include "HttpTransferSeams.h"
#include "emule.h"
#include "LongPathSeams.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace HttpTransfer
{
class CRegisteredInternetHandle
{
public:
	CRegisteredInternetHandle(CTransferCancellation::InternetHandleSlot eSlot, const std::shared_ptr<CTransferCancellation>& pCancellation) noexcept
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

	explicit operator bool() const noexcept
	{
		return m_hInternet != NULL;
	}

	HINTERNET Get() const noexcept
	{
		return m_hInternet;
	}

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
		}
		m_hInternet = hInternet;
	}

private:
	CTransferCancellation::InternetHandleSlot m_eSlot;
	std::shared_ptr<CTransferCancellation> m_pCancellation;
	HINTERNET m_hInternet;
};

namespace
{
	bool SetInternetTimeout(HINTERNET hInternet, DWORD dwOption, DWORD dwTimeoutMs, CString& strError)
	{
		if (::InternetSetOption(hInternet, dwOption, &dwTimeoutMs, sizeof(dwTimeoutMs)))
			return true;

		strError.Format(_T("InternetSetOption(%u) failed (%u)"), static_cast<unsigned>(dwOption), ::GetLastError());
		return false;
	}

	bool HasTransferDeadlineExpired(ULONGLONG ullStartTick, ULONGLONG ullTotalTimeoutMs)
	{
		return ullTotalTimeoutMs != 0 && ::GetTickCount64() - ullStartTick >= ullTotalTimeoutMs;
	}

	bool IsTransferCancelled(const std::shared_ptr<CTransferCancellation>& pCancellation, CString& strError)
	{
		if (!pCancellation || !pCancellation->IsCancelled())
			return false;

		strError = _T("HTTP transfer cancelled");
		return true;
	}

	bool CrackHttpUrl(const CString& strUrl, URL_COMPONENTS& components, TCHAR (&szHostName)[INTERNET_MAX_HOST_NAME_LENGTH], TCHAR (&szUrlPath)[2048], TCHAR (&szExtraInfo)[2048], CString& strError)
	{
		ZeroMemory(&components, sizeof(components));
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

		if (components.nScheme != INTERNET_SCHEME_HTTP && components.nScheme != INTERNET_SCHEME_HTTPS) {
			strError = _T("Only HTTP and HTTPS URLs are supported");
			return false;
		}
		return true;
	}

	bool OpenGetRequest(const SRequest& request, CString& strError, const std::shared_ptr<CTransferCancellation>& pCancellation, CRegisteredInternetHandle& hInternetSession, CRegisteredInternetHandle& hHttpConnection, CRegisteredInternetHandle& hHttpFile)
	{
		if (IsTransferCancelled(pCancellation, strError))
			return false;

		const CString strUserAgent = request.strUserAgent.IsEmpty() ? CString(AfxGetAppName()) : request.strUserAgent;
		hInternetSession.Reset(::InternetOpen(strUserAgent, HttpTransferSeams::GetInternetOpenTypeForSystemProxyMode(request.bUseWindowsSystemProxy), NULL, NULL, 0));
		if (!hInternetSession) {
			if (!IsTransferCancelled(pCancellation, strError))
				strError.Format(_T("InternetOpen failed (%u)"), ::GetLastError());
			return false;
		}
		if (IsTransferCancelled(pCancellation, strError))
			return false;
		if (!SetInternetTimeout(hInternetSession.Get(), INTERNET_OPTION_CONNECT_TIMEOUT, request.dwConnectTimeoutMs, strError))
			return false;

		TCHAR szHostName[INTERNET_MAX_HOST_NAME_LENGTH] = {};
		TCHAR szUrlPath[2048] = {};
		TCHAR szExtraInfo[2048] = {};
		URL_COMPONENTS components = {};
		if (!CrackHttpUrl(request.strUrl, components, szHostName, szUrlPath, szExtraInfo, strError))
			return false;

		CString strObject(components.lpszUrlPath, components.dwUrlPathLength);
		strObject.Append(CString(components.lpszExtraInfo, components.dwExtraInfoLength));
		if (strObject.IsEmpty())
			strObject = _T("/");

		hHttpConnection.Reset(::InternetConnect(hInternetSession.Get(),
			CString(components.lpszHostName, components.dwHostNameLength),
			components.nPort,
			NULL,
			NULL,
			INTERNET_SERVICE_HTTP,
			0,
			0));
		if (!hHttpConnection) {
			if (!IsTransferCancelled(pCancellation, strError))
				strError.Format(_T("InternetConnect failed (%u)"), ::GetLastError());
			return false;
		}
		if (IsTransferCancelled(pCancellation, strError))
			return false;

		LPCTSTR pszAcceptTypes[] = { _T("*/*"), NULL };
		DWORD dwFlags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_DONT_CACHE | INTERNET_FLAG_KEEP_CONNECTION | INTERNET_FLAG_NO_COOKIES;
		if (components.nScheme == INTERNET_SCHEME_HTTPS)
			dwFlags |= INTERNET_FLAG_SECURE;

		hHttpFile.Reset(::HttpOpenRequest(hHttpConnection.Get(), _T("GET"), strObject, NULL, NULL, pszAcceptTypes, dwFlags, 0));
		if (!hHttpFile) {
			if (!IsTransferCancelled(pCancellation, strError))
				strError.Format(_T("HttpOpenRequest failed (%u)"), ::GetLastError());
			return false;
		}
		if (IsTransferCancelled(pCancellation, strError))
			return false;
		if (!SetInternetTimeout(hHttpFile.Get(), INTERNET_OPTION_SEND_TIMEOUT, request.dwSendTimeoutMs, strError)
			|| !SetInternetTimeout(hHttpFile.Get(), INTERNET_OPTION_RECEIVE_TIMEOUT, request.dwReceiveTimeoutMs, strError))
			return false;

		CString strHeaders(request.strHeaders);
		if (strHeaders.Find(_T("Accept-Encoding:")) < 0)
			strHeaders.Append(_T("Accept-Encoding: identity\r\n"));

		if (IsTransferCancelled(pCancellation, strError))
			return false;
		if (!::HttpSendRequest(hHttpFile.Get(), strHeaders.IsEmpty() ? NULL : (LPCTSTR)strHeaders, strHeaders.GetLength(), NULL, 0)) {
			if (!IsTransferCancelled(pCancellation, strError))
				strError.Format(_T("HttpSendRequest failed (%u)"), ::GetLastError());
			return false;
		}
		if (IsTransferCancelled(pCancellation, strError))
			return false;

		DWORD dwStatusCode = 0;
		DWORD dwStatusLength = sizeof(dwStatusCode);
		if (!::HttpQueryInfo(hHttpFile.Get(), HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &dwStatusCode, &dwStatusLength, NULL) || dwStatusCode != HTTP_STATUS_OK) {
			strError.Format(_T("Unexpected HTTP status %u"), static_cast<unsigned>(dwStatusCode));
			return false;
		}
		return true;
	}

	ULONGLONG QueryContentLength(HINTERNET hHttpFile)
	{
		DWORD dwContentLength = 0;
		DWORD dwContentLengthSize = sizeof(dwContentLength);
		if (!::HttpQueryInfo(hHttpFile, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &dwContentLength, &dwContentLengthSize, NULL))
			return 0;
		return static_cast<ULONGLONG>(dwContentLength);
	}

	bool CheckKnownContentLengthLimit(ULONGLONG ullContentLength, ULONGLONG ullMaxResponseBytes, CString& strError)
	{
		if (HttpTransferSeams::IsKnownContentLengthAllowed(ullContentLength, ullMaxResponseBytes))
			return true;

		strError.Format(_T("HTTP response is too large (%I64u bytes, limit %I64u bytes)."), ullContentLength, ullMaxResponseBytes);
		return false;
	}

	bool CheckStreamingResponseLimit(ULONGLONG ullCurrentBytes, DWORD dwNextBytes, ULONGLONG ullMaxResponseBytes, CString& strError)
	{
		if (!HttpTransferSeams::WouldExceedResponseLimit(ullCurrentBytes, dwNextBytes, ullMaxResponseBytes))
			return true;

		strError.Format(_T("HTTP response is too large (limit %I64u bytes)."), ullMaxResponseBytes);
		return false;
	}
}

CTransferCancellation::CTransferCancellation() noexcept
	: m_hInternet{}
	, m_bCancelled(false)
{
}

void CTransferCancellation::Cancel() noexcept
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

bool CTransferCancellation::IsCancelled() const noexcept
{
	CSingleLock lock(&m_lock, TRUE);
	return m_bCancelled;
}

bool CTransferCancellation::RegisterHandle(InternetHandleSlot eSlot, HINTERNET hInternet) noexcept
{
	if (hInternet == NULL)
		return false;

	CSingleLock lock(&m_lock, TRUE);
	if (!HttpTransferSeams::ShouldRegisterInternetHandleForCancellationState(m_bCancelled))
		return false;

	const size_t uSlot = static_cast<size_t>(eSlot);
	ASSERT(uSlot < static_cast<size_t>(InternetHandleSlot::Count));
	ASSERT(m_hInternet[uSlot] == NULL);
	m_hInternet[uSlot] = hInternet;
	return true;
}

bool CTransferCancellation::ReleaseHandle(InternetHandleSlot eSlot, HINTERNET hInternet) noexcept
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

SRequest MakeRequest(HttpTransferSeams::ERequestProfile eProfile, const CString& strUrl)
{
	const HttpTransferSeams::SRequestLimits limits = HttpTransferSeams::GetRequestLimitsForProfile(eProfile);
	SRequest request;
	request.strUrl = strUrl;
	request.dwConnectTimeoutMs = limits.dwConnectTimeoutMs;
	request.dwSendTimeoutMs = limits.dwSendTimeoutMs;
	request.dwReceiveTimeoutMs = limits.dwReceiveTimeoutMs;
	request.ullTotalTimeoutMs = limits.ullTotalTimeoutMs;
	request.ullMaxResponseBytes = limits.ullMaxResponseBytes;
	return request;
}

bool DownloadToFile(const SRequest& request, const CString& strTargetPath, CString& strError, const std::shared_ptr<CTransferCancellation>& pCancellation, const ProgressCallback& progressCallback)
{
	strError.Empty();

	CRegisteredInternetHandle hInternetSession(CTransferCancellation::InternetHandleSlot::Session, pCancellation);
	CRegisteredInternetHandle hHttpConnection(CTransferCancellation::InternetHandleSlot::Connection, pCancellation);
	CRegisteredInternetHandle hHttpFile(CTransferCancellation::InternetHandleSlot::Request, pCancellation);
	if (!OpenGetRequest(request, strError, pCancellation, hInternetSession, hHttpConnection, hHttpFile))
		return false;

	const ULONGLONG ullContentLength = QueryContentLength(hHttpFile.Get());
	if (!CheckKnownContentLengthLimit(ullContentLength, request.ullMaxResponseBytes, strError))
		return false;

	const int fdOut = LongPathSeams::OpenCrtWriteOnlyLongPath(strTargetPath, CREATE_ALWAYS, FILE_SHARE_READ);
	if (fdOut == -1) {
		strError.Format(_T("Could not open %s for writing"), (LPCTSTR)strTargetPath);
		return false;
	}

	const ULONGLONG ullTransferStartTick = ::GetTickCount64();
	BYTE buffer[16 * 1024] = {};
	DWORD dwBytesRead = 0;
	ULONGLONG ullTotalBytesRead = 0;
	bool bSuccess = true;
	do {
		if (IsTransferCancelled(pCancellation, strError)) {
			bSuccess = false;
			break;
		}
		if (HasTransferDeadlineExpired(ullTransferStartTick, request.ullTotalTimeoutMs)) {
			strError.Format(_T("HTTP transfer timed out after %u seconds"), static_cast<unsigned>(request.ullTotalTimeoutMs / 1000ull));
			bSuccess = false;
			break;
		}
		if (!::InternetReadFile(hHttpFile.Get(), buffer, sizeof(buffer), &dwBytesRead)) {
			if (!IsTransferCancelled(pCancellation, strError))
				strError.Format(_T("InternetReadFile failed (%u)"), ::GetLastError());
			bSuccess = false;
			break;
		}

		if (dwBytesRead > 0) {
			if (!CheckStreamingResponseLimit(ullTotalBytesRead, dwBytesRead, request.ullMaxResponseBytes, strError)) {
				bSuccess = false;
				break;
			}
			if (_write(fdOut, buffer, dwBytesRead) != static_cast<int>(dwBytesRead)) {
				strError.Format(_T("Write failed for %s (%u)"), (LPCTSTR)strTargetPath, errno);
				bSuccess = false;
				break;
			}
			ullTotalBytesRead += dwBytesRead;
			if (progressCallback && !progressCallback(ullTotalBytesRead, ullContentLength)) {
				strError = _T("HTTP transfer cancelled");
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

bool FetchToMemory(const SRequest& request, std::string& strResponse, CString& strError, const std::shared_ptr<CTransferCancellation>& pCancellation, const ProgressCallback& progressCallback)
{
	strResponse.clear();
	strError.Empty();

	CRegisteredInternetHandle hInternetSession(CTransferCancellation::InternetHandleSlot::Session, pCancellation);
	CRegisteredInternetHandle hHttpConnection(CTransferCancellation::InternetHandleSlot::Connection, pCancellation);
	CRegisteredInternetHandle hHttpFile(CTransferCancellation::InternetHandleSlot::Request, pCancellation);
	if (!OpenGetRequest(request, strError, pCancellation, hInternetSession, hHttpConnection, hHttpFile))
		return false;

	const ULONGLONG ullContentLength = QueryContentLength(hHttpFile.Get());
	if (!CheckKnownContentLengthLimit(ullContentLength, request.ullMaxResponseBytes, strError))
		return false;

	const ULONGLONG ullTransferStartTick = ::GetTickCount64();
	BYTE buffer[16 * 1024] = {};
	DWORD dwBytesRead = 0;
	do {
		if (IsTransferCancelled(pCancellation, strError))
			return false;
		if (HasTransferDeadlineExpired(ullTransferStartTick, request.ullTotalTimeoutMs)) {
			strError.Format(_T("HTTP transfer timed out after %u seconds"), static_cast<unsigned>(request.ullTotalTimeoutMs / 1000ull));
			return false;
		}
		if (!::InternetReadFile(hHttpFile.Get(), buffer, sizeof(buffer), &dwBytesRead)) {
			if (!IsTransferCancelled(pCancellation, strError))
				strError.Format(_T("InternetReadFile failed (%u)"), ::GetLastError());
			return false;
		}

		if (dwBytesRead != 0) {
			if (!CheckStreamingResponseLimit(static_cast<ULONGLONG>(strResponse.size()), dwBytesRead, request.ullMaxResponseBytes, strError))
				return false;
			strResponse.append(reinterpret_cast<const char*>(buffer), dwBytesRead);
			if (progressCallback && !progressCallback(static_cast<ULONGLONG>(strResponse.size()), ullContentLength)) {
				strError = _T("HTTP transfer cancelled");
				return false;
			}
		}
	} while (dwBytesRead != 0);

	return true;
}
}
