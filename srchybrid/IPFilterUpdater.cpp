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
#include <memory>
#include <time.h>
#include "IPFilterUpdater.h"
#include "DirectDownload.h"
#include "emule.h"
#include "IPFilter.h"
#include "IPFilterUpdateSeams.h"
#include "Preferences.h"
#include "HttpDownloadDlg.h"
#include "emuledlg.h"
#include "OtherFunctions.h"
#include "ZipFile.h"
#include "GZipFile.h"
#include "RarFile.h"
#include "ServerWnd.h"
#include "ServerListCtrl.h"
#include "UserMsgs.h"
#include "Log.h"
#include "LongPathSeams.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/**
 * @brief Heap-owned refresh state shared between the owner and worker context.
 */
struct SIPFilterBackgroundRefreshState
{
	SIPFilterBackgroundRefreshState()
		: lQueued(0)
	{
	}

	volatile LONG lQueued;
};

bool GetMimeType(LPCTSTR pszFilePath, CString &rstrMimeType);

struct CIPFilterUpdater::SBackgroundRefreshContext
{
	CString strDownloadUrl;
	CString strArchiveTempPath;
	CString strInstallPath;
	HWND hNotifyWnd;
	std::shared_ptr<SIPFilterBackgroundRefreshState> pRefreshState;
	bool bProxyEnabled;
};

namespace
{
	/**
	 * @brief Reloads the live IP-filter list and reapplies server-list pruning when configured.
	 */
	INT_PTR ReloadCurrentIPFilter(bool bShowResponse)
	{
		CWaitCursor curHourglass;
		const INT_PTR nLoaded = theApp.ipfilter != NULL ? theApp.ipfilter->LoadFromDefaultFile(bShowResponse) : 0;
		if (thePrefs.GetFilterServerByIP() && theApp.emuledlg != NULL && theApp.emuledlg->serverwnd != NULL)
			theApp.emuledlg->serverwnd->serverlistctrl.RemoveAllFilteredServers();
		return nLoaded;
	}

	/**
	 * @brief Reports IP-filter update failures either through a modal message or noninteractive logging.
	 */
	void ReportIPFilterUpdateError(const CString& strError, bool bInteractive)
	{
		if (bInteractive)
			AfxMessageBox(strError, MB_ICONERROR);
		else
			theApp.QueueLogLine(false, GetResString(IDS_IPFILTER_AUTO_UPDATE_FAILED), (LPCTSTR)strError);
	}

	/**
	 * @brief Creates one temporary path in the config directory for IP-filter updater work.
	 */
	bool CreateIPFilterTempPath(LPCTSTR pszPrefix, CString& strTempPath, CString& strError)
	{
		return DirectDownload::CreateTempPathInDirectory(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR), pszPrefix, strTempPath, strError);
	}

	void DeliverBackgroundRefreshCompletion(HWND hNotifyWnd, UINT uMessage, bool bUpdated, const std::shared_ptr<SIPFilterBackgroundRefreshState>& pRefreshState, LPCTSTR pszComponentName)
	{
		if (hNotifyWnd != NULL && ::PostMessage(hNotifyWnd, uMessage, bUpdated ? 1u : 0u, 0))
			return;

		const DWORD dwPostError = ::GetLastError();
		if (hNotifyWnd != NULL && ::IsWindow(hNotifyWnd)) {
			(void)::SendMessage(hNotifyWnd, uMessage, bUpdated ? 1u : 0u, 0);
			AddDebugLogLine(false, _T("%s: delivered background refresh completion synchronously after PostMessage failed (%u)."), pszComponentName, dwPostError);
			return;
		}

		if (pRefreshState)
			(void)::InterlockedExchange(&pRefreshState->lQueued, 0);
		AddDebugLogLine(false, _T("%s: dropped background refresh completion because the notify window is unavailable (%u)."), pszComponentName, dwPostError);
	}

	/**
	 * @brief Promotes a prepared IP-filter file through the app's atomic replacement helper.
	 */
	bool PromoteIPFilterFile(const CString& strSourcePath, DWORD* pdwLastError)
	{
		const CString strTargetPath = CIPFilter::GetDefaultFilePath();
		DWORD dwReplaceError = ERROR_SUCCESS;
		const bool bPromoted = ReplaceFileAtomically(strSourcePath, strTargetPath, &dwReplaceError);
		if (!bPromoted)
			TRACE(_T("*** Error: Failed to replace default IP filter file \"%s\" with \"%s\" - %s\n"), (LPCTSTR)strTargetPath, (LPCTSTR)strSourcePath, (LPCTSTR)GetErrorMessage(dwReplaceError));
		if (pdwLastError != NULL)
			*pdwLastError = dwReplaceError;
		return bPromoted;
	}

	/**
	 * @brief Rejects empty or obvious markup payloads before promotion.
	 */
	bool IsPlainIPFilterPayloadAcceptable(const CString& strFilePath, CString& strError)
	{
		FILE* fp = LongPathSeams::OpenFileStreamDenyWriteLongPath(strFilePath, _T("rb"));
		if (fp == NULL) {
			strError.Format(_T("Failed to open downloaded IP filter file \"%s\"."), (LPCTSTR)strFilePath);
			return false;
		}

		char szBuffer[16384] = {};
		const size_t uRead = fread(szBuffer, 1, sizeof szBuffer, fp);
		fclose(fp);
		if (uRead == 0) {
			strError = GetResString(IDS_DWLIPFILTERFAILED);
			return false;
		}
		if (IPFilterUpdateSeams::LooksLikeMarkupPayload(szBuffer, uRead)) {
			strError = GetResString(IDS_DWLIPFILTERFAILED);
			return false;
		}
		return true;
	}

	/**
	 * @brief Extracts and promotes one downloaded IP-filter archive or plain filter file.
	 */
	bool InstallDownloadedIPFilter(const CString& strDownloadedPath, bool bInteractive, CString& strError)
	{
		CString strMimeType;
		GetMimeType(strDownloadedPath, strMimeType);

		bool bIsArchiveFile = false;
		bool bUncompressed = false;
		CZIPFile zip;
		if (zip.Open(strDownloadedPath)) {
			bIsArchiveFile = true;

			CZIPFile::File* zfile = zip.GetFile(_T("ipfilter.dat"));
			if (zfile == NULL) {
				zfile = zip.GetFile(_T("guarding.p2p"));
				if (zfile == NULL)
					zfile = zip.GetFile(_T("guardian.p2p"));
			}
			if (zfile != NULL) {
				CString strTempUnzipFilePath;
				if (!CreateIPFilterTempPath(_T("ipf"), strTempUnzipFilePath, strError)) {
					zip.Close();
					return false;
				}

				if (zfile->Extract(strTempUnzipFilePath)) {
					zip.Close();

					DWORD dwPromoteError = ERROR_SUCCESS;
					if (PromoteIPFilterFile(strTempUnzipFilePath, &dwPromoteError)) {
						bUncompressed = true;
						return true;
					}

					strError.Format(_T("%s\r\n\r\nFailed to replace \"%s\" with \"%s\".\r\n\r\n%s"),
						(LPCTSTR)GetResString(IDS_DWLIPFILTERFAILED),
						(LPCTSTR)CIPFilter::GetDefaultFilePath(),
						(LPCTSTR)strTempUnzipFilePath,
						(LPCTSTR)GetErrorMessage(dwPromoteError));
				} else
					strError.Format(GetResString(IDS_ERR_IPFILTERZIPEXTR), (LPCTSTR)strDownloadedPath);
				(void)LongPathSeams::DeleteFileIfExists(strTempUnzipFilePath);
			} else
				strError.Format(GetResString(IDS_ERR_IPFILTERCONTENTERR), (LPCTSTR)strDownloadedPath);

			zip.Close();
		} else if (strMimeType.CompareNoCase(_T("application/x-rar-compressed")) == 0) {
			bIsArchiveFile = true;

			CRARFile rar;
			if (rar.Open(strDownloadedPath)) {
				CString strFile;
				if (rar.GetNextFile(strFile)
					&& (strFile.CompareNoCase(_T("ipfilter.dat")) == 0
						|| strFile.CompareNoCase(_T("guarding.p2p")) == 0
						|| strFile.CompareNoCase(_T("guardian.p2p")) == 0))
				{
					CString strTempUnzipFilePath;
					if (!CreateIPFilterTempPath(_T("ipf"), strTempUnzipFilePath, strError)) {
						rar.Close();
						return false;
					}
					if (rar.Extract(strTempUnzipFilePath)) {
						rar.Close();

						DWORD dwPromoteError = ERROR_SUCCESS;
						if (PromoteIPFilterFile(strTempUnzipFilePath, &dwPromoteError)) {
							bUncompressed = true;
							return true;
						}

						strError.Format(_T("%s\r\n\r\nFailed to replace \"%s\" with \"%s\".\r\n\r\n%s"),
							(LPCTSTR)GetResString(IDS_DWLIPFILTERFAILED),
							(LPCTSTR)CIPFilter::GetDefaultFilePath(),
							(LPCTSTR)strTempUnzipFilePath,
							(LPCTSTR)GetErrorMessage(dwPromoteError));
					} else
						strError.Format(_T("Failed to extract IP filter file from RAR file \"%s\"."), (LPCTSTR)strDownloadedPath);
					(void)LongPathSeams::DeleteFileIfExists(strTempUnzipFilePath);
				} else
					strError.Format(_T("Failed to find IP filter file \"guarding.p2p\" or \"ipfilter.dat\" in RAR file \"%s\"."), (LPCTSTR)strDownloadedPath);
				rar.Close();
			} else
				strError.Format(_T("Failed to open file \"%s\".\r\n\r\nInvalid file format?\r\n\r\n%s"), (LPCTSTR)strDownloadedPath, CRARFile::sUnrar_download);
		} else {
			CGZIPFile gz;
			if (gz.Open(strDownloadedPath)) {
				bIsArchiveFile = true;

				CString strTempUnzipFilePath;
				if (!CreateIPFilterTempPath(_T("ipf"), strTempUnzipFilePath, strError)) {
					gz.Close();
					return false;
				}

				const CString& strUncompressedFileName(gz.GetUncompressedFileName());
				if (!strUncompressedFileName.IsEmpty())
					strTempUnzipFilePath.AppendFormat(_T(".%s"), (LPCTSTR)strUncompressedFileName);

				if (gz.Extract(strTempUnzipFilePath)) {
					gz.Close();

					DWORD dwPromoteError = ERROR_SUCCESS;
					if (PromoteIPFilterFile(strTempUnzipFilePath, &dwPromoteError)) {
						bUncompressed = true;
						return true;
					}

					strError.Format(_T("%s\r\n\r\nFailed to replace \"%s\" with \"%s\".\r\n\r\n%s"),
						(LPCTSTR)GetResString(IDS_DWLIPFILTERFAILED),
						(LPCTSTR)CIPFilter::GetDefaultFilePath(),
						(LPCTSTR)strTempUnzipFilePath,
						(LPCTSTR)GetErrorMessage(dwPromoteError));
				} else
					strError.Format(GetResString(IDS_ERR_IPFILTERZIPEXTR), (LPCTSTR)strDownloadedPath);
				(void)LongPathSeams::DeleteFileIfExists(strTempUnzipFilePath);
			}
			gz.Close();
		}

		if (!bIsArchiveFile && !bUncompressed) {
			if (!IsPlainIPFilterPayloadAcceptable(strDownloadedPath, strError))
				return false;

			DWORD dwPromoteError = ERROR_SUCCESS;
			if (PromoteIPFilterFile(strDownloadedPath, &dwPromoteError))
				return true;

			strError.Format(_T("%s\r\n\r\nFailed to replace \"%s\" with \"%s\".\r\n\r\n%s"),
				(LPCTSTR)GetResString(IDS_DWLIPFILTERFAILED),
				(LPCTSTR)CIPFilter::GetDefaultFilePath(),
				(LPCTSTR)strDownloadedPath,
				(LPCTSTR)GetErrorMessage(dwPromoteError));
		}

		if (strError.IsEmpty())
			strError = GetResString(IDS_DWLIPFILTERFAILED);
		if (!bInteractive)
			AddDebugLogLine(false, _T("IPFilter: automatic update rejected %s (%s)"), (LPCTSTR)strDownloadedPath, (LPCTSTR)strError);
		return false;
	}
}

CIPFilterUpdater::CIPFilterUpdater()
	: m_pBackgroundRefreshState(std::make_shared<SIPFilterBackgroundRefreshState>())
{
}

CIPFilterUpdater::~CIPFilterUpdater()
{
	(void)::InterlockedExchange(&m_pBackgroundRefreshState->lQueued, 0);
}

bool CIPFilterUpdater::UpdateFromUrlInteractive(const CString& strUrl)
{
	if (strUrl.IsEmpty()) {
		ReloadCurrentIPFilter(true);
		return false;
	}

	CString strTempFilePath;
	CString strError;
	if (!CreateIPFilterTempPath(_T("ipf"), strTempFilePath, strError)) {
		ReportIPFilterUpdateError(strError, true);
		return false;
	}

	CHttpDownloadDlg dlgDownload;
	dlgDownload.m_strTitle = GetResString(IDS_DWL_IPFILTERFILE);
	dlgDownload.m_sURLToDownload = strUrl;
	dlgDownload.m_sFileToDownloadInto = strTempFilePath;
	if (dlgDownload.DoModal() != IDOK) {
		(void)LongPathSeams::DeleteFileIfExists(strTempFilePath);
		strError = GetResString(IDS_DWLIPFILTERFAILED);
		if (!dlgDownload.GetError().IsEmpty())
			strError.AppendFormat(_T("\r\n\r\n%s"), (LPCTSTR)dlgDownload.GetError());
		ReportIPFilterUpdateError(strError, true);
		return false;
	}

	const bool bUpdated = InstallDownloadedIPFilter(strTempFilePath, true, strError);
	(void)LongPathSeams::DeleteFileIfExists(strTempFilePath);
	if (!bUpdated) {
		ReportIPFilterUpdateError(strError, true);
		return false;
	}

	const INT_PTR nLoaded = ReloadCurrentIPFilter(true);
	if (nLoaded == 0) {
		CString strLoaded;
		strLoaded.Format(GetResString(IDS_IPFILTERLOADED), static_cast<UINT>(nLoaded));
		CString strWarning(GetResString(IDS_DWLIPFILTERFAILED));
		strWarning.AppendFormat(_T("\r\n\r\n%s"), (LPCTSTR)strLoaded);
		ReportIPFilterUpdateError(strWarning, true);
		return false;
	}
	return true;
}

bool CIPFilterUpdater::IsAutomaticRefreshDue(const __time64_t tNow)
{
	return IPFilterUpdateSeams::IsAutomaticRefreshDue(tNow, thePrefs.GetIPFilterLastUpdateTime(), thePrefs.GetIPFilterUpdatePeriodDays());
}

bool CIPFilterUpdater::QueueBackgroundRefresh()
{
	if (!thePrefs.GetAutoIPFilterUpdate() || ::InterlockedCompareExchange(&m_pBackgroundRefreshState->lQueued, 0, 0) != 0)
		return false;

	__time64_t tNow = 0;
	_time64(&tNow);
	if (!IsAutomaticRefreshDue(tNow))
		return false;

	const CString strUpdateUrl(thePrefs.GetIPFilterUpdateUrl());
	if (strUpdateUrl.IsEmpty()) {
		AddLogLine(false, GetResString(IDS_IPFILTER_AUTO_UPDATE_NO_URL));
		return false;
	}

	const HWND hNotifyWnd = theApp.emuledlg != NULL ? theApp.emuledlg->m_hWnd : NULL;
	if (hNotifyWnd == NULL)
		return false;

	CString strArchiveTempPath;
	CString strError;
	if (!CreateIPFilterTempPath(_T("ipf"), strArchiveTempPath, strError)) {
		AddDebugLogLine(false, _T("%s"), (LPCTSTR)strError);
		return false;
	}

	std::unique_ptr<SBackgroundRefreshContext> pContext(new SBackgroundRefreshContext);
	pContext->strDownloadUrl = strUpdateUrl;
	pContext->strArchiveTempPath = strArchiveTempPath;
	pContext->strInstallPath = CIPFilter::GetDefaultFilePath();
	pContext->hNotifyWnd = hNotifyWnd;
	pContext->pRefreshState = m_pBackgroundRefreshState;
	pContext->bProxyEnabled = thePrefs.GetProxySettings().bUseProxy;

	if (::InterlockedCompareExchange(&m_pBackgroundRefreshState->lQueued, 1, 0) != 0) {
		(void)LongPathSeams::DeleteFileIfExists(strArchiveTempPath);
		return false;
	}
	CWinThread* pThread = AfxBeginThread(BackgroundRefreshThread, pContext.get(), THREAD_PRIORITY_BELOW_NORMAL, 0, CREATE_SUSPENDED, NULL);
	if (pThread == NULL) {
		(void)::InterlockedExchange(&m_pBackgroundRefreshState->lQueued, 0);
		(void)LongPathSeams::DeleteFileIfExists(strArchiveTempPath);
		AddDebugLogLine(false, _T("IPFilter: failed to start background update thread."));
		return false;
	}

	pThread->m_bAutoDelete = TRUE;
	thePrefs.SetIPFilterLastUpdateTime(tNow, true);
	AddLogLine(false, GetResString(IDS_IPFILTER_AUTO_UPDATE_STARTED), (LPCTSTR)strUpdateUrl);
	SBackgroundRefreshContext *pThreadContext = pContext.release();
	if (pThread->ResumeThread() == static_cast<DWORD>(-1)) {
		std::unique_ptr<SBackgroundRefreshContext> pCleanupContext(pThreadContext);
		(void)::InterlockedExchange(&m_pBackgroundRefreshState->lQueued, 0);
		(void)LongPathSeams::DeleteFileIfExists(pCleanupContext->strArchiveTempPath);
		AddDebugLogLine(false, _T("IPFilter: failed to resume background update thread (%u)."), ::GetLastError());
		return false;
	}
	return true;
}

void CIPFilterUpdater::HandleBackgroundRefreshResult(bool bUpdated)
{
	(void)::InterlockedExchange(&m_pBackgroundRefreshState->lQueued, 0);
	if (!bUpdated)
		return;

	const INT_PTR nLoaded = ReloadCurrentIPFilter(false);
	AddLogLine(false, GetResString(IDS_IPFILTER_AUTO_UPDATE_DONE), static_cast<UINT>(nLoaded));
	if (nLoaded == 0)
		AddLogLine(false, GetResString(IDS_DWLIPFILTERFAILED));
}

bool CIPFilterUpdater::IsRefreshQueued() const
{
	return ::InterlockedCompareExchange(&m_pBackgroundRefreshState->lQueued, 0, 0) != 0;
}

UINT AFX_CDECL CIPFilterUpdater::BackgroundRefreshThread(LPVOID pParam)
{
	std::unique_ptr<SBackgroundRefreshContext> pContext(reinterpret_cast<SBackgroundRefreshContext*>(pParam));
	if (pContext.get() == NULL)
		return 0;

	bool bUpdated = false;
	if (pContext->bProxyEnabled)
		theApp.QueueLogLine(false, _T("IPFilter: proxy-backed automatic refresh is ignored; use the manual updater or a VPN for network privacy."));

	CString strError;
	if (!DirectDownload::DownloadUrlToFile(pContext->strDownloadUrl, pContext->strArchiveTempPath, strError)) {
		CString strDownloadError;
		strDownloadError.Format(_T("%s (%s)"), (LPCTSTR)pContext->strDownloadUrl, (LPCTSTR)strError);
		ReportIPFilterUpdateError(strDownloadError, false);
		goto cleanup;
	}

	if (!InstallDownloadedIPFilter(pContext->strArchiveTempPath, false, strError)) {
		ReportIPFilterUpdateError(strError, false);
		goto cleanup;
	}

	bUpdated = true;

cleanup:
	(void)LongPathSeams::DeleteFileIfExists(pContext->strArchiveTempPath);
	DeliverBackgroundRefreshCompletion(pContext->hNotifyWnd, UM_IPFILTER_UPDATED, bUpdated, pContext->pRefreshState, _T("IPFilter"));
	return 0;
}
