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
#include "emule.h"
#include "FakeFileDetector.h"
#include "DownloadProgressBarSeams.h"
#include "DownloadListKeyboardShortcutsSeams.h"
#include "DownloadPriorityShortcutsSeams.h"
#include "DownloadListCtrl.h"
#include "FilenameNormalizationPolicy.h"
#include "updownclient.h"
#include "MenuCmds.h"
#include "ClientDetailDialog.h"
#include "FileDetailDialog.h"
#include "InputBox.h"
#include "emuledlg.h"
#include "DownloadQueue.h"
#include "FriendList.h"
#include "PartFile.h"
#include "ClientCredits.h"
#include "MemDC.h"
#include "ChatWnd.h"
#include "TransferDlg.h"
#include "Kademlia/Kademlia/Kademlia.h"
#include "WebServices.h"
#include "Preview.h"
#include "StringConversion.h"
#include "AddSourceDlg.h"
#include "SearchDlg.h"
#include "SharedFileList.h"
#include "KnownFileList.h"
#include "GeoLocation.h"
#include "ClientList.h"
#include "OtherFunctions.h"
#include "LongPathSeams.h"
#include "HelperThreadLaunchSeams.h"
#include "PartFilePreviewSeams.h"
#include "ProUserMenuCopySeams.h"
#include "MenuShortcutLabels.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


// CDownloadListCtrl

#define DLC_BARUPDATE (SEC2MS(1)/2)

#define RATING_ICON_WIDTH	16

namespace
{
	constexpr int DOWNLOAD_COLUMN_TRUST = 9;
	const UINT_PTR kVideoThumbnailTimerId = 0xE072;
	const TCHAR kVideoThumbnailCacheFolder[] = _T("VideoThumbnails\\");

	uint32 GetClientGeoIP(const CUpDownClient* client)
	{
		if (client == NULL)
			return 0;
		return client->GetIP() != 0 ? client->GetIP() : client->GetConnectIP();
	}

	bool IsSourceCtrlItem(const CtrlItem_Struct *item)
	{
		return item != NULL && (item->type == AVAILABLE_SOURCE || item->type == UNAVAILABLE_SOURCE);
	}

	SearchTrustHintSeams::TrustHint BuildDownloadTrustHint(CPartFile &, const SFakeFileReport &rFakeReport)
	{
		return SearchTrustHintSeams::BuildTrustHint(false, rFakeReport.nScore, rFakeReport.eSeverity);
	}

	SearchTrustHintSeams::TrustHint BuildDownloadTrustHint(CPartFile &rPartFile)
	{
		return BuildDownloadTrustHint(rPartFile, FakeFileDetector::GetPartFileReportSnapshot(rPartFile));
	}

	bool IsLiveKnownFilePointer(const CKnownFile *file)
	{
		if (file == NULL)
			return false;
		return (theApp.downloadqueue != NULL && theApp.downloadqueue->IsPartFile(file))
			|| (theApp.knownfiles != NULL && theApp.knownfiles->IsFilePtrInList(file))
			|| (theApp.sharedfiles != NULL && theApp.sharedfiles->IsFilePtrInList(file));
	}

	bool IsLiveFileCtrlItem(const CtrlItem_Struct *item)
	{
		return item != NULL
			&& item->type == FILE_TYPE
			&& item->value != NULL
			&& IsLiveKnownFilePointer(static_cast<const CPartFile*>(item->value));
	}

	bool IsLiveSourceCtrlItem(const CtrlItem_Struct *item)
	{
		if (!IsSourceCtrlItem(item) || item->value == NULL || theApp.clientlist == NULL)
			return false;
		return (item->owner == NULL || IsLiveKnownFilePointer(item->owner))
			&& theApp.clientlist->ContainsClientPointer(static_cast<CUpDownClient*>(item->value));
	}

	bool IsLiveDownloadClient(const CUpDownClient *client)
	{
		return theApp.clientlist != NULL && theApp.clientlist->ContainsClientPointer(client);
	}

	LPCTSTR GetVideoThumbnailAttemptResultText(PartFilePreviewSeams::EVideoThumbnailAttemptResult eResult)
	{
		switch (eResult) {
		case PartFilePreviewSeams::VTAR_COPY_FAILED:
			return _T("copy failed");
		case PartFilePreviewSeams::VTAR_FFMPEG_BUSY:
			return _T("FFmpeg busy");
		case PartFilePreviewSeams::VTAR_FFMPEG_TIMEOUT:
			return _T("FFmpeg timeout");
		case PartFilePreviewSeams::VTAR_FFMPEG_START_FAILED:
			return _T("FFmpeg start failed");
		case PartFilePreviewSeams::VTAR_FFMPEG_FAILED:
			return _T("FFmpeg failed");
		case PartFilePreviewSeams::VTAR_NO_THUMBNAIL:
			return _T("no thumbnail output");
		case PartFilePreviewSeams::VTAR_DECODE_FAILED:
			return _T("bitmap decode failed");
		case PartFilePreviewSeams::VTAR_CACHE_WRITE_FAILED:
			return _T("cache write failed");
		case PartFilePreviewSeams::VTAR_SUCCESS:
			return _T("success");
		case PartFilePreviewSeams::VTAR_EXCEPTION:
			return _T("exception");
		default:
			return _T("none");
		}
	}

	void LogVideoThumbnailVerbose(LPCTSTR pszFormat, ...)
	{
		if (!thePrefs.GetVerbose())
			return;

		va_list argptr;
		va_start(argptr, pszFormat);
		CString strMessage;
		strMessage.FormatV(pszFormat, argptr);
		va_end(argptr);
		theApp.QueueDebugLogLine(false, _T("Video thumbnail: %s"), (LPCTSTR)strMessage);
	}

	int FindSubMenuItemPosition(CMenu &menu, HMENU hSubMenu)
	{
		const int itemCount = menu.GetMenuItemCount();
		for (int i = 0; i < itemCount; ++i) {
			MENUITEMINFO mii = {};
			mii.cbSize = sizeof(mii);
			mii.fMask = MIIM_SUBMENU;
			if (menu.GetMenuItemInfo(static_cast<UINT>(i), &mii, TRUE) && mii.hSubMenu == hSubMenu)
				return i;
		}
		return -1;
	}

	bool EnableSubMenuItem(CMenu &menu, HMENU hSubMenu, UINT state)
	{
		const int position = FindSubMenuItemPosition(menu, hSubMenu);
		if (position < 0)
			return false;
		return menu.EnableMenuItem(static_cast<UINT>(position), MF_BYPOSITION | state) != static_cast<UINT>(-1);
	}

	bool RemoveSubMenuItem(CMenu &menu, HMENU hSubMenu)
	{
		const int position = FindSubMenuItemPosition(menu, hSubMenu);
		if (position < 0)
			return false;
		return menu.RemoveMenu(static_cast<UINT>(position), MF_BYPOSITION) != FALSE;
	}

}


IMPLEMENT_DYNAMIC(CtrlItem_Struct, CObject)

IMPLEMENT_DYNAMIC(CDownloadListCtrl, CMuleListCtrl)

BEGIN_MESSAGE_MAP(CDownloadListCtrl, CMuleListCtrl)
	ON_NOTIFY_REFLECT(LVN_COLUMNCLICK, OnLvnColumnClick)
	ON_NOTIFY_REFLECT(LVN_DELETEITEM, OnListModified)
	ON_NOTIFY_REFLECT(LVN_GETDISPINFO, OnLvnGetDispInfo)
	ON_NOTIFY_REFLECT(LVN_GETINFOTIP, OnLvnGetInfoTip)
	ON_NOTIFY_REFLECT(LVN_INSERTITEM, OnListModified)
	ON_NOTIFY_REFLECT(LVN_ITEMACTIVATE, OnLvnItemActivate)
	ON_NOTIFY_REFLECT(LVN_ITEMCHANGED, OnListModified)
	ON_NOTIFY_REFLECT(NM_DBLCLK, OnNmDblClk)
	ON_MESSAGE(TM_VIDEOTHUMBNAILFINISHED, OnVideoThumbnailFinished)
	ON_WM_CONTEXTMENU()
	ON_WM_DESTROY()
	ON_WM_KEYDOWN()
	ON_WM_MOUSEMOVE()
	ON_WM_SYSCOLORCHANGE()
	ON_WM_TIMER()
END_MESSAGE_MAP()

CDownloadListCtrl::CDownloadListCtrl()
	: CDownloadListListCtrlItemWalk(this)
	, m_curTab()
	, m_pFontBold()
	, m_pHoveredVideoThumbnailFile()
	, m_hHoveredVideoThumbnailBitmap()
	, m_bVideoThumbnailWorkerActive()
	, m_bVideoThumbnailCacheInitialized()
	, m_dwLastAvailableCommandsCheck()
	, m_bRemainSort()
	, m_availableCommandsDirty(true)
{
	SetGeneralPurposeFind(true);
	SetSkinKey(_T("DownloadsLv"));
}

CDownloadListCtrl::~CDownloadListCtrl()
{
	if (m_PreviewMenu)
		VERIFY(m_PreviewMenu.DestroyMenu());
	if (m_PrioMenu)
		VERIFY(m_PrioMenu.DestroyMenu());
	if (m_SourcesMenu)
		VERIFY(m_SourcesMenu.DestroyMenu());
	if (m_FileMenu)
		VERIFY(m_FileMenu.DestroyMenu());

	while (!m_ListItems.empty()) {
		delete m_ListItems.begin()->second; // second = CtrlItem_Struct*
		m_ListItems.erase(m_ListItems.begin());
	}
	ClearVideoThumbnailCache();
}

void CDownloadListCtrl::Init()
{
	SetPrefsKey(_T("DownloadListCtrl"));
	SetStyle();
	ASSERT((GetStyle() & LVS_SINGLESEL) == 0);

	CToolTipCtrl *tooltip = GetToolTips();
	if (tooltip) {
		m_tooltip.SetFileIconToolTip(true);
		m_tooltip.SubclassWindow(*tooltip);
		tooltip->ModifyStyle(0, TTS_NOPREFIX);
		tooltip->SetDelayTime(TTDT_AUTOPOP, SEC2MS(20));
		tooltip->SetDelayTime(TTDT_INITIAL, SEC2MS(thePrefs.GetToolTipDelay()));
	}

	InsertColumn(0,		_T(""),	LVCFMT_LEFT,	DFLT_FILENAME_COL_WIDTH);		//IDS_DL_FILENAME
	InsertColumn(1,		_T(""),	LVCFMT_RIGHT,	DFLT_SIZE_COL_WIDTH);			//IDS_DL_SIZE
	InsertColumn(2,		_T(""),	LVCFMT_RIGHT,	DFLT_SIZE_COL_WIDTH);			//IDS_DL_TRANSF
	InsertColumn(3,		_T(""),	LVCFMT_RIGHT,	DFLT_SIZE_COL_WIDTH);			//IDS_DL_TRANSFCOMPL
	InsertColumn(4,		_T(""),	LVCFMT_RIGHT,	DFLT_DATARATE_COL_WIDTH);		//IDS_DL_SPEED
	InsertColumn(5,		_T(""),	LVCFMT_LEFT,	DFLT_PARTSTATUS_COL_WIDTH);		//IDS_DL_PROGRESS
	InsertColumn(6,		_T(""),	LVCFMT_RIGHT,	60);							//IDS_DL_SOURCES
	InsertColumn(7,		_T(""),	LVCFMT_LEFT,	DFLT_PRIORITY_COL_WIDTH);		//IDS_PRIORITY
	InsertColumn(8,		_T(""),	LVCFMT_LEFT,	70);							//IDS_STATUS
	InsertColumn(DOWNLOAD_COLUMN_TRUST,	_T(""),	LVCFMT_LEFT,	90);			//IDS_SEARCH_TRUST
	InsertColumn(10,	_T(""),	LVCFMT_RIGHT,	110);							//IDS_DL_REMAINS
	InsertColumn(11,	_T(""),	LVCFMT_LEFT,	150);							//IDS_LASTSEENCOMPL
	InsertColumn(12,	_T(""),	LVCFMT_LEFT,	120);							//IDS_FD_LASTCHANGE
	InsertColumn(13,	_T(""),	LVCFMT_LEFT,	100);							//IDS_CAT
	InsertColumn(14,	_T(""),	LVCFMT_LEFT,	120);							//IDS_ADDEDON
	InsertColumn(15,	_T(""),	LVCFMT_LEFT,	140);							//IDS_GEOLOCATION
	InsertColumn(16,	_T(""),	LVCFMT_RIGHT,	50);							//IDS_PERCENTAGE
	InsertColumn(17,	_T(""),	LVCFMT_LEFT,	100);							//IDS_IP
	InsertColumn(18,	_T(""),	LVCFMT_LEFT,	70);							//IDS_IDLOW

	if (const auto *pProfile = MuleListCtrlViewPresets::FindProfile(_T("DownloadListCtrl")))
		SetViewPresetProfile(*pProfile);
	SetAllIcons();
	Localize();
	LoadSettings();
	m_curTab = 0;

	if (thePrefs.GetShowActiveDownloadsBold()) {
		if (thePrefs.GetUseSystemFontForMainControls()) {
			CFont *pFont = GetFont();
			LOGFONT lfFont;
			pFont->GetLogFont(&lfFont);
			lfFont.lfWeight = FW_BOLD;
			m_fontBold.CreateFontIndirect(&lfFont);
			m_pFontBold = &m_fontBold;
		} else
			m_pFontBold = &theApp.m_fontDefaultBold;
	}

	// Barry - Use preferred sort order from preferences
	m_bRemainSort = thePrefs.TransferlistRemainSortStyle();
	int adder;
	if (GetSortItem() != 9 || !m_bRemainSort) {
		SetSortArrow();
		adder = 0;
	} else {
		SetSortArrow(GetSortItem(), GetSortAscending() ? arrowDoubleUp : arrowDoubleDown);
		adder = 81; //9+81=90 - used in Compare(,,)
	}
	SortItems(SortProc, MAKELONG(GetSortItem() + adder, !GetSortAscending()));
	InitializeVideoThumbnailCache();
	UpdateVideoThumbnailTimer();
}

void CDownloadListCtrl::OnSysColorChange()
{
	CMuleListCtrl::OnSysColorChange();
	SetAllIcons();
	CreateMenus();
}

void CDownloadListCtrl::InitializeVideoThumbnailCache()
{
	if (m_bVideoThumbnailCacheInitialized)
		return;
	m_bVideoThumbnailCacheInitialized = true;

	const CString strCacheDirectory(GetVideoThumbnailCacheDirectory());
	if (!LongPathSeams::PathExists(strCacheDirectory))
		(void)LongPathSeams::CreateDirectory(strCacheDirectory, NULL);

	CString strPattern(strCacheDirectory);
	strPattern += _T("*.png");
	WIN32_FIND_DATA findData = {};
	HANDLE hFind = LongPathSeams::FindFirstFile(strPattern, &findData);
	if (hFind == INVALID_HANDLE_VALUE)
		return;
	do {
		if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
			(void)LongPathSeams::DeleteFile(strCacheDirectory + findData.cFileName);
	} while (::FindNextFile(hFind, &findData));
	VERIFY(::FindClose(hFind));
}

void CDownloadListCtrl::ClearVideoThumbnailCache()
{
	m_tooltip.SetPreviewBitmap(NULL, 0);
	for (POSITION pos = m_videoThumbnailCache.GetStartPosition(); pos != NULL;) {
		CString strKey;
		void *pEntry = NULL;
		m_videoThumbnailCache.GetNextAssoc(pos, strKey, pEntry);
		delete static_cast<VideoThumbnailCacheEntry*>(pEntry);
	}
	m_videoThumbnailCache.RemoveAll();
	m_videoThumbnailQueue.RemoveAll();
	m_pHoveredVideoThumbnailFile = NULL;
	m_hHoveredVideoThumbnailBitmap = NULL;
}

CString CDownloadListCtrl::GetVideoThumbnailCacheDirectory() const
{
	CString strDirectory(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	strDirectory += kVideoThumbnailCacheFolder;
	return strDirectory;
}

CString CDownloadListCtrl::GetVideoThumbnailCacheKey(const CPartFile *pPartFile) const
{
	return pPartFile != NULL ? CString(md4str(pPartFile->GetFileHash())) : CString();
}

CString CDownloadListCtrl::GetVideoThumbnailCachePath(const CString &rstrKey) const
{
	CString strPath(GetVideoThumbnailCacheDirectory());
	strPath.AppendFormat(_T("thumb_%s.png"), (LPCTSTR)rstrKey);
	return strPath;
}

CDownloadListCtrl::VideoThumbnailCacheEntry* CDownloadListCtrl::GetVideoThumbnailCacheEntry(const CString &rstrKey)
{
	if (rstrKey.IsEmpty())
		return NULL;
	InitializeVideoThumbnailCache();

	void *pEntry = NULL;
	if (m_videoThumbnailCache.Lookup(rstrKey, pEntry))
		return static_cast<VideoThumbnailCacheEntry*>(pEntry);

	VideoThumbnailCacheEntry *pNewEntry = new VideoThumbnailCacheEntry;
	pNewEntry->strCachePath = GetVideoThumbnailCachePath(rstrKey);
	m_videoThumbnailCache.SetAt(rstrKey, pNewEntry);
	return pNewEntry;
}

CPartFile* CDownloadListCtrl::FindVideoThumbnailFileByKey(const CString &rstrKey) const
{
	if (theApp.downloadqueue == NULL || rstrKey.IsEmpty())
		return NULL;
	POSITION pos = NULL;
	for (INT_PTR i = 0, iCount = theApp.downloadqueue->GetFileCount(); i < iCount; ++i) {
		CPartFile *pPartFile = theApp.downloadqueue->GetFileNext(pos);
		if (pPartFile == NULL)
			break;
		if (rstrKey.CompareNoCase(md4str(pPartFile->GetFileHash())) == 0)
			return pPartFile;
	}
	return NULL;
}

HBITMAP CDownloadListCtrl::GetCachedVideoThumbnail(CPartFile *pPartFile)
{
	VideoThumbnailCacheEntry *pEntry = GetVideoThumbnailCacheEntry(GetVideoThumbnailCacheKey(pPartFile));
	if (pEntry == NULL)
		return NULL;
	if (pEntry->hBitmap == NULL && LongPathSeams::PathExists(pEntry->strCachePath)) {
		HBITMAP hBitmap = NULL;
		if (ReadVideoThumbnailBitmapFile(pEntry->strCachePath, hBitmap))
			pEntry->hBitmap = hBitmap;
	}
	return pEntry->hBitmap;
}

bool CDownloadListCtrl::IsVideoThumbnailCandidate(const CPartFile *pPartFile) const
{
	return pPartFile != NULL && pPartFile->IsReadyForVideoThumbnail();
}

bool CDownloadListCtrl::IsVideoThumbnailAttemptDue(const VideoThumbnailCacheEntry *pEntry, const CPartFile *pPartFile, ULONGLONG ullCurrentTick, bool bForceAttempt) const
{
	if (pEntry == NULL || pPartFile == NULL)
		return false;

	const uint64 ullCompletedSize = static_cast<uint64>(pPartFile->GetCompletedSize());
	const uint64 ullFileSize = static_cast<uint64>(pPartFile->GetFileSize());
	if (pEntry->hBitmap == NULL) {
		if (bForceAttempt)
			return true;
		return PartFilePreviewSeams::IsVideoThumbnailAttemptDue(ullCurrentTick, pEntry->ullLastAttemptTick, thePrefs.GetVideoThumbnailIntervalMs());
	}
	if (!PartFilePreviewSeams::ShouldRefreshVideoThumbnail(pEntry->ullCompletedSize, ullCompletedSize, ullFileSize))
		return false;
	if (pEntry->ullLastAttemptCompletedSize <= pEntry->ullCompletedSize)
		return true;
	if (PartFilePreviewSeams::ShouldRefreshVideoThumbnail(pEntry->ullLastAttemptCompletedSize, ullCompletedSize, ullFileSize))
		return true;
	return PartFilePreviewSeams::IsVideoThumbnailAttemptDue(ullCurrentTick, pEntry->ullLastAttemptTick, thePrefs.GetVideoThumbnailIntervalMs());
}

void CDownloadListCtrl::QueueVideoThumbnail(CPartFile *pPartFile, bool bHighPriority)
{
	if (pPartFile == NULL)
		return;
	if (!pPartFile->IsMovie() || !thePrefs.IsVideoThumbnailGenerationEnabled() || !PartFilePreviewSeams::IsValidConfiguredFfmpegPath(thePrefs.GetVideoThumbnailFfmpegPath())) {
		if (bHighPriority)
			LogVideoThumbnailVerbose(_T("skip queue for \"%s\" because thumbnail prerequisites are not met"), (LPCTSTR)pPartFile->GetFileName());
		return;
	}

	const CString strKey(GetVideoThumbnailCacheKey(pPartFile));
	VideoThumbnailCacheEntry *pEntry = GetVideoThumbnailCacheEntry(strKey);
	if (pEntry == NULL)
		return;

	const bool bForceAttempt = PartFilePreviewSeams::ShouldForceVideoThumbnailAttempt(bHighPriority, pEntry->hBitmap != NULL);
	if (pEntry->bInFlight) {
		if (bForceAttempt) {
			pEntry->bForceAttempt = true;
			if (!pEntry->bQueued) {
				pEntry->bQueued = true;
				m_videoThumbnailQueue.AddHead(strKey);
				LogVideoThumbnailVerbose(_T("queued forced retry behind active worker for \"%s\""), pPartFile != NULL ? (LPCTSTR)pPartFile->GetFileName() : _T(""));
			}
		}
		return;
	}

	if (!IsVideoThumbnailCandidate(pPartFile)) {
		if (bHighPriority)
			LogVideoThumbnailVerbose(_T("skip queue for \"%s\" because it is not eligible"), pPartFile != NULL ? (LPCTSTR)pPartFile->GetFileName() : _T(""));
		return;
	}

	const ULONGLONG ullCurrentTick = ::GetTickCount64();
	if (!IsVideoThumbnailAttemptDue(pEntry, pPartFile, ullCurrentTick, bForceAttempt))
		return;

	if (bForceAttempt)
		pEntry->bForceAttempt = true;

	if (pEntry->bQueued) {
		if (bHighPriority) {
			for (POSITION pos = m_videoThumbnailQueue.GetHeadPosition(); pos != NULL;) {
				POSITION posCurrent = pos;
				const CString strQueuedKey(m_videoThumbnailQueue.GetNext(pos));
				if (strQueuedKey.CompareNoCase(strKey) == 0) {
					m_videoThumbnailQueue.RemoveAt(posCurrent);
					m_videoThumbnailQueue.AddHead(strKey);
					break;
				}
			}
		}
	} else {
		pEntry->bQueued = true;
		if (bHighPriority)
			m_videoThumbnailQueue.AddHead(strKey);
		else
			m_videoThumbnailQueue.AddTail(strKey);
		LogVideoThumbnailVerbose(_T("queued %s request for \"%s\""), bHighPriority ? _T("hover") : _T("background"), (LPCTSTR)pPartFile->GetFileName());
	}
	StartNextVideoThumbnailWorker();
}

void CDownloadListCtrl::QueueVideoThumbnailScan()
{
	if (theApp.downloadqueue == NULL)
		return;
	POSITION pos = NULL;
	for (INT_PTR i = 0, iCount = theApp.downloadqueue->GetFileCount(); i < iCount; ++i) {
		CPartFile *pPartFile = theApp.downloadqueue->GetFileNext(pos);
		if (pPartFile == NULL)
			break;
		QueueVideoThumbnail(pPartFile, false);
	}
}

void CDownloadListCtrl::StartNextVideoThumbnailWorker()
{
	if (!PartFilePreviewSeams::ShouldStartVideoThumbnailWorker(m_bVideoThumbnailWorkerActive, ::IsWindow(m_hWnd) != FALSE, theApp.IsClosing()))
		return;

	while (!m_videoThumbnailQueue.IsEmpty()) {
		const CString strKey(m_videoThumbnailQueue.RemoveHead());
		VideoThumbnailCacheEntry *pEntry = GetVideoThumbnailCacheEntry(strKey);
		if (pEntry == NULL)
			continue;
		pEntry->bQueued = false;
		const bool bForceAttempt = pEntry->bForceAttempt;
		pEntry->bForceAttempt = false;

		CPartFile *pPartFile = FindVideoThumbnailFileByKey(strKey);
		if (!IsVideoThumbnailCandidate(pPartFile)) {
			if (bForceAttempt)
				LogVideoThumbnailVerbose(_T("skip forced worker start for %s because it is no longer eligible"), (LPCTSTR)strKey);
			continue;
		}

		const ULONGLONG ullCurrentTick = ::GetTickCount64();
		if (!IsVideoThumbnailAttemptDue(pEntry, pPartFile, ullCurrentTick, bForceAttempt))
			continue;

		pEntry->ullLastAttemptTick = ullCurrentTick;
		pEntry->ullLastAttemptCompletedSize = static_cast<uint64>(pPartFile->GetCompletedSize());
		pEntry->bInFlight = true;
		m_bVideoThumbnailWorkerActive = true;
		pPartFile->m_bPreviewing = true;
		LogVideoThumbnailVerbose(_T("starting %s worker for \"%s\" at %I64u/%I64u bytes")
			, bForceAttempt ? _T("forced") : _T("background")
			, (LPCTSTR)pPartFile->GetFileName()
			, pEntry->ullLastAttemptCompletedSize
			, static_cast<uint64>(pPartFile->GetFileSize()));

		CVideoThumbnailThread *pThread = static_cast<CVideoThumbnailThread*>(AfxBeginThread(RUNTIME_CLASS(CVideoThumbnailThread), PartFilePreviewSeams::kVideoThumbnailWorkerThreadPriority, 0, CREATE_SUSPENDED));
		if (pThread == NULL) {
			pPartFile->m_bPreviewing = false;
			pEntry->bInFlight = false;
			m_bVideoThumbnailWorkerActive = false;
			pEntry->eLastResult = PartFilePreviewSeams::VTAR_EXCEPTION;
			pEntry->dwLastErrorCode = ERROR_NOT_ENOUGH_MEMORY;
			LogVideoThumbnailVerbose(_T("failed to start worker thread for \"%s\""), (LPCTSTR)pPartFile->GetFileName());
			continue;
		}
		pThread->SetValues(pPartFile, thePrefs.GetVideoThumbnailFfmpegPath(), m_hWnd, pEntry->strCachePath);
		DWORD dwResumeError = ERROR_SUCCESS;
		if (!HelperThreadLaunchSeams::ResumeAutoDeleteSuspendedThread(pThread, dwResumeError)) {
			pPartFile->m_bPreviewing = false;
			pEntry->bInFlight = false;
			m_bVideoThumbnailWorkerActive = false;
			pEntry->eLastResult = PartFilePreviewSeams::VTAR_EXCEPTION;
			pEntry->dwLastErrorCode = dwResumeError;
			LogVideoThumbnailVerbose(_T("failed to resume worker thread for \"%s\" (error %lu)"), (LPCTSTR)pPartFile->GetFileName(), dwResumeError);
			continue;
		}
		return;
	}
}

void CDownloadListCtrl::UpdateVideoThumbnailTimer()
{
	KillTimer(kVideoThumbnailTimerId);
	if (!thePrefs.IsVideoThumbnailGenerationEnabled() || !PartFilePreviewSeams::IsValidConfiguredFfmpegPath(thePrefs.GetVideoThumbnailFfmpegPath())) {
		m_videoThumbnailQueue.RemoveAll();
		ClearVideoThumbnailCache();
		m_hHoveredVideoThumbnailBitmap = NULL;
		m_tooltip.SetPreviewBitmap(NULL, 0);
		return;
	}

	SetTimer(kVideoThumbnailTimerId, static_cast<UINT>(thePrefs.GetVideoThumbnailIntervalMs()), NULL);
	QueueVideoThumbnailScan();
}

void CDownloadListCtrl::RemoveVideoThumbnailCache(const CPartFile *pPartFile)
{
	const CString strKey(GetVideoThumbnailCacheKey(pPartFile));
	if (strKey.IsEmpty())
		return;

	for (POSITION pos = m_videoThumbnailQueue.GetHeadPosition(); pos != NULL;) {
		POSITION posCurrent = pos;
		const CString strQueuedKey(m_videoThumbnailQueue.GetNext(pos));
		if (strQueuedKey.CompareNoCase(strKey) == 0)
			m_videoThumbnailQueue.RemoveAt(posCurrent);
	}

	void *pEntry = NULL;
	if (m_videoThumbnailCache.Lookup(strKey, pEntry)) {
		VideoThumbnailCacheEntry *pCacheEntry = static_cast<VideoThumbnailCacheEntry*>(pEntry);
		(void)LongPathSeams::DeleteFile(pCacheEntry->strCachePath);
		delete pCacheEntry;
		m_videoThumbnailCache.RemoveKey(strKey);
	}
	if (m_pHoveredVideoThumbnailFile == pPartFile) {
		m_pHoveredVideoThumbnailFile = NULL;
		m_hHoveredVideoThumbnailBitmap = NULL;
		m_tooltip.SetPreviewBitmap(NULL, 0);
	}
}

void CDownloadListCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	CPartFile *pHoveredFile = NULL;
	bool bHoveringFileName = false;
	LVHITTESTINFO hitTest = {};
	hitTest.pt = point;
	if (SubItemHitTest(&hitTest) >= 0) {
		const CtrlItem_Struct *pCtrlItem = reinterpret_cast<CtrlItem_Struct*>(GetItemData(hitTest.iItem));
		if (IsLiveFileItem(pCtrlItem)) {
			pHoveredFile = static_cast<CPartFile*>(pCtrlItem->value);
			bHoveringFileName = hitTest.iSubItem == 0;
		}
		else if (pCtrlItem != NULL && pCtrlItem->type == FILE_TYPE)
			PruneStaleFileItems();
	}

	if (pHoveredFile != m_pHoveredVideoThumbnailFile) {
		m_pHoveredVideoThumbnailFile = pHoveredFile;
		m_hHoveredVideoThumbnailBitmap = NULL;
		if (pHoveredFile == NULL)
			m_tooltip.SetPreviewBitmap(NULL, 0);
	}
	if (pHoveredFile != NULL && bHoveringFileName && pHoveredFile->IsMovie() && thePrefs.IsVideoThumbnailGenerationEnabled()
		&& PartFilePreviewSeams::IsValidConfiguredFfmpegPath(thePrefs.GetVideoThumbnailFfmpegPath())) {
		HBITMAP hBitmap = GetCachedVideoThumbnail(pHoveredFile);
		const bool bBitmapChanged = hBitmap != m_hHoveredVideoThumbnailBitmap;
		m_hHoveredVideoThumbnailBitmap = hBitmap;
		m_tooltip.SetPreviewBitmap(hBitmap, hBitmap != NULL ? PartFilePreviewSeams::kVideoThumbnailDisplayMaxWidth : 0);
		if (hBitmap != NULL && bBitmapChanged && ::IsWindowVisible(m_tooltip.GetSafeHwnd()))
			m_tooltip.RefreshCurrentTool();
		QueueVideoThumbnail(pHoveredFile, true);
	} else {
		m_hHoveredVideoThumbnailBitmap = NULL;
		m_tooltip.SetPreviewBitmap(NULL, 0);
	}

	CMuleListCtrl::OnMouseMove(nFlags, point);
}

void CDownloadListCtrl::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == kVideoThumbnailTimerId) {
		QueueVideoThumbnailScan();
		return;
	}
	CMuleListCtrl::OnTimer(nIDEvent);
}

void CDownloadListCtrl::OnDestroy()
{
	KillTimer(kVideoThumbnailTimerId);
	ClearVideoThumbnailCache();
	CMuleListCtrl::OnDestroy();
}

LRESULT CDownloadListCtrl::OnVideoThumbnailFinished(WPARAM, LPARAM lParam)
{
	VideoThumbnailResult_Struct *pResult = reinterpret_cast<VideoThumbnailResult_Struct*>(lParam);
	if (pResult == NULL)
		return 0;

	VideoThumbnailCacheEntry *pEntry = GetVideoThumbnailCacheEntry(pResult->strFileHash);
	if (pEntry != NULL) {
		pEntry->bInFlight = false;
		pEntry->strCachePath = pResult->strCachePath;
		pEntry->eLastResult = pResult->eResult;
		pEntry->dwLastErrorCode = pResult->dwErrorCode;
		pEntry->dwLastProcessExitCode = pResult->dwProcessExitCode;
	}
	m_bVideoThumbnailWorkerActive = false;

	const bool bFileStillTracked = theApp.downloadqueue != NULL
		&& pResult->pPartFile != NULL
		&& theApp.downloadqueue->IsPartFile(pResult->pPartFile);
	if (bFileStillTracked)
		pResult->pPartFile->m_bPreviewing = false;

	const bool bFileStillCurrent = bFileStillTracked
		&& pResult->strFileHash.CompareNoCase(GetVideoThumbnailCacheKey(pResult->pPartFile)) == 0;
	if (thePrefs.GetVerbose()) {
		const CString strFileName(bFileStillCurrent ? pResult->pPartFile->GetFileName() : pResult->strFileHash);
		if (pResult->eResult == PartFilePreviewSeams::VTAR_SUCCESS)
			LogVideoThumbnailVerbose(_T("finished for \"%s\": success at %I64u bytes"), (LPCTSTR)strFileName, pResult->ullCompletedSize);
		else
			LogVideoThumbnailVerbose(_T("finished for \"%s\": %s (error=%lu, processExit=%lu, completed=%I64u)")
				, (LPCTSTR)strFileName
				, GetVideoThumbnailAttemptResultText(pResult->eResult)
				, pResult->dwErrorCode
				, pResult->dwProcessExitCode
				, pResult->ullCompletedSize);
	}
	if (pEntry != NULL && bFileStillCurrent && pResult->hBitmap != NULL) {
		if (pEntry->hBitmap != NULL)
			::DeleteObject(pEntry->hBitmap);
		pEntry->hBitmap = pResult->hBitmap;
		pResult->hBitmap = NULL;
		pEntry->ullCompletedSize = pResult->ullCompletedSize;
		if (m_pHoveredVideoThumbnailFile == pResult->pPartFile) {
			m_hHoveredVideoThumbnailBitmap = pEntry->hBitmap;
			m_tooltip.SetPreviewBitmap(pEntry->hBitmap, PartFilePreviewSeams::kVideoThumbnailDisplayMaxWidth);
			m_tooltip.RefreshCurrentTool();
		}
	}

	delete pResult;
	StartNextVideoThumbnailWorker();
	return 0;
}

void CDownloadListCtrl::SetAllIcons()
{
	ApplyImageList(NULL);
	m_ImageList.DeleteImageList();
	m_ImageList.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	m_ImageList.Add(CTempIconLoader(_T("SrcDownloading")));	//0
	m_ImageList.Add(CTempIconLoader(_T("SrcOnQueue")));		//1
	m_ImageList.Add(CTempIconLoader(_T("SrcConnecting")));	//2
	m_ImageList.Add(CTempIconLoader(_T("SrcNNPQF")));		//3
	m_ImageList.Add(CTempIconLoader(_T("SrcUnknown")));		//4
	m_ImageList.Add(CTempIconLoader(_T("ClientCompatible")));//5
	m_ImageList.Add(CTempIconLoader(_T("Friend")));			//6
	m_ImageList.Add(CTempIconLoader(_T("ClientEDonkey")));	//7
	m_ImageList.Add(CTempIconLoader(_T("ClientMLDonkey")));	//8
	m_ImageList.Add(CTempIconLoader(_T("ClientEDonkeyHybrid")));//9
	m_ImageList.Add(CTempIconLoader(_T("ClientShareaza")));	//10
	m_ImageList.Add(CTempIconLoader(_T("Server")));			//11
	m_ImageList.Add(CTempIconLoader(_T("ClientAMule")));	//12
	m_ImageList.Add(CTempIconLoader(_T("ClientLPhant")));	//13
	m_ImageList.Add(CTempIconLoader(_T("Rating_NotRated")));//14
	m_ImageList.Add(CTempIconLoader(_T("Rating_Fake")));	//15
	m_ImageList.Add(CTempIconLoader(_T("Rating_Poor")));	//16
	m_ImageList.Add(CTempIconLoader(_T("Rating_Fair")));	//17
	m_ImageList.Add(CTempIconLoader(_T("Rating_Good")));	//18
	m_ImageList.Add(CTempIconLoader(_T("Rating_Excellent")));//19
	m_ImageList.Add(CTempIconLoader(_T("Collection_Search"))); //20 rating for comments are searched on kad
	m_ImageList.SetOverlayImage(m_ImageList.Add(CTempIconLoader(_T("ClientSecureOvl"))), 1);
	m_ImageList.SetOverlayImage(m_ImageList.Add(CTempIconLoader(_T("OverlayObfu"))), 2);
	m_ImageList.SetOverlayImage(m_ImageList.Add(CTempIconLoader(_T("OverlaySecureObfu"))), 3);
	// Apply the image list also to the listview control, even if we use our own 'DrawItem'.
	// This is needed to give the listview control a chance to initialize the row height.
	ASSERT((GetStyle() & LVS_SHAREIMAGELISTS) != 0);
	VERIFY(ApplyImageList(m_ImageList) == NULL);
}

void CDownloadListCtrl::Localize()
{
	static const UINT uids[19] =
	{
		IDS_DL_FILENAME, IDS_DL_SIZE, IDS_DL_TRANSF, IDS_DL_TRANSFCOMPL, IDS_DL_SPEED
		, IDS_DL_PROGRESS, IDS_DL_SOURCES, IDS_PRIORITY, IDS_STATUS, IDS_SEARCH_TRUST
		, IDS_DL_REMAINS, 0/*IDS_LASTSEENCOMPL*/, 0/*IDS_FD_LASTCHANGE*/, IDS_CAT, IDS_ADDEDON
		, IDS_GEOLOCATION, IDS_PERCENTAGE, IDS_IP, IDS_IDLOW
	};

	LocaliseHeaderCtrl(uids, _countof(uids));

	CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();
	HDITEM hdi;
	hdi.mask = HDI_TEXT;

	CString strRes(GetResString(IDS_LASTSEENCOMPL));
	strRes.Remove(_T(':'));
	hdi.pszText = const_cast<LPTSTR>((LPCTSTR)strRes);
	pHeaderCtrl->SetItem(11, &hdi);

	strRes = GetResString(IDS_FD_LASTCHANGE);
	strRes.Remove(_T(':'));
	hdi.pszText = const_cast<LPTSTR>((LPCTSTR)strRes);
	pHeaderCtrl->SetItem(12, &hdi);

	CreateMenus();
	ShowFilesCount();
}

void CDownloadListCtrl::AddFile(CPartFile *toadd)
{
	// Create new Item
	CtrlItem_Struct *newitem = new CtrlItem_Struct;
	newitem->owner = NULL;
	newitem->value = toadd;
	newitem->parent = NULL;
	newitem->dwUpdated = 0;
	newitem->type = FILE_TYPE;

	// The same file shall be added only once
	ASSERT(m_ListItems.find(toadd) == m_ListItems.end());
	m_ListItems.emplace(toadd, newitem);

	if (toadd->CheckShowItemInGivenCat(m_curTab))
		InsertItem(LVIF_TEXT | LVIF_PARAM, GetItemCount(), LPSTR_TEXTCALLBACK, 0, 0, 0, (LPARAM)newitem);

	ShowFilesCount();
}

void CDownloadListCtrl::AddSource(CPartFile *owner, CUpDownClient *source, bool notavailable)
{
	if (!IsLiveDownloadClient(source))
		return;

	ItemType itemtype = notavailable ? UNAVAILABLE_SOURCE : AVAILABLE_SOURCE;
	// Update the other instances of this source
	bool bFound = false;
	for (ListItems::const_iterator it = m_ListItems.lower_bound(source); it != m_ListItems.end() && it->first == source; ++it) {
		CtrlItem_Struct *cur_item = it->second;

		// Check if this source has been already added to this file => to be sure
		if (cur_item->owner == owner) {
			// Update this instance with its new setting
			cur_item->type = itemtype;
			cur_item->dwUpdated = 0;
			bFound = true;
		} else if (!notavailable) {
			// The state 'Available' is exclusive
			cur_item->type = UNAVAILABLE_SOURCE;
			cur_item->dwUpdated = 0;
		}
	}

	// The same source could be added a few times but only once per file
	if (bFound)
		return;

	ListItems::const_iterator ownerIt = m_ListItems.find(owner);
	ASSERT(ownerIt != m_ListItems.end());
	CtrlItem_Struct *ownerItem = ownerIt->second;
	ASSERT(ownerItem->value == owner);

	// Create new Item
	CtrlItem_Struct *newitem = new CtrlItem_Struct;
	newitem->owner = owner;
	newitem->value = source;
	newitem->parent = ownerItem;	// cross link to the owner
	newitem->dwUpdated = 0;
	newitem->type = itemtype;

	m_ListItems.emplace(source, newitem);

	if (owner->srcarevisible) {
		// find parent from the CListCtrl to add source
		LVFINDINFO find;
		find.flags = LVFI_PARAM;
		find.lParam = (LPARAM)ownerItem;
		int iItem = FindItem(&find);
		if (iItem >= 0)
			InsertItem(LVIF_TEXT | LVIF_PARAM, iItem + 1, LPSTR_TEXTCALLBACK, 0, 0, 0, (LPARAM)newitem);
	}
}

bool CDownloadListCtrl::IsLiveFileItem(const CtrlItem_Struct *pCtrlItem) const
{
	return IsLiveFileCtrlItem(pCtrlItem);
}

bool CDownloadListCtrl::IsLiveSourceItem(const CtrlItem_Struct *pCtrlItem) const
{
	return IsLiveSourceCtrlItem(pCtrlItem);
}

bool CDownloadListCtrl::PruneStaleFileItems()
{
	bool bRemoved = false;
	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end();) {
		CtrlItem_Struct *delItem = it->second;
		if ((delItem->type == FILE_TYPE && !IsLiveFileItem(delItem)) || (IsSourceCtrlItem(delItem) && !IsLiveSourceItem(delItem))) {
			it = m_ListItems.erase(it);

			LVFINDINFO find;
			find.flags = LVFI_PARAM;
			find.lParam = (LPARAM)delItem;
			for (;;) {
				int iItem = FindItem(&find);
				if (iItem < 0)
					break;
				DeleteItem(iItem);
			}

			delete delItem;
			bRemoved = true;
		} else
			++it;
	}

	if (bRemoved) {
		m_availableCommandsDirty = true;
		ShowFilesCount();
	}

	return bRemoved;
}

bool CDownloadListCtrl::PruneStaleSourceItems()
{
	bool bRemoved = false;
	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end();) {
		CtrlItem_Struct *delItem = it->second;
		if (IsSourceCtrlItem(delItem) && !IsLiveSourceItem(delItem)) {
			it = m_ListItems.erase(it);

			LVFINDINFO find;
			find.flags = LVFI_PARAM;
			find.lParam = (LPARAM)delItem;
			for (;;) {
				int iItem = FindItem(&find);
				if (iItem < 0)
					break;
				DeleteItem(iItem);
			}

			delete delItem;
			bRemoved = true;
		} else
			++it;
	}

	if (bRemoved)
		m_availableCommandsDirty = true;

	return bRemoved;
}

void CDownloadListCtrl::RemoveSource(CUpDownClient *source, CPartFile *owner)
{
	if (theApp.IsClosing())
		return;

	// Retrieve all entries matching the source
	for (ListItems::const_iterator it = m_ListItems.lower_bound(source); it != m_ListItems.end() && it->first == source;) {
		const CtrlItem_Struct *delItem = it->second;
		if (owner == NULL || owner == delItem->owner) {
			// Remove it from the m_ListItems
			it = m_ListItems.erase(it);

			// Remove it from the CListCtrl
			LVFINDINFO find;
			find.flags = LVFI_PARAM;
			find.lParam = (LPARAM)delItem;
			int iItem = FindItem(&find);
			if (iItem >= 0)
				DeleteItem(iItem);

			// finally it could be deleted
			delete delItem;
		} else
			++it;
	}
}

bool CDownloadListCtrl::RemoveFile(const CPartFile *toremove)
{
	bool bResult = false;
	if (theApp.IsClosing())
		return bResult;
	RemoveVideoThumbnailCache(toremove);
	// Retrieve all entries matching the File or linked to the file
	// Remark: The 'asked another files' clients must be removed from here
	ASSERT(toremove != NULL);
	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end();) {
		const CtrlItem_Struct *delItem = it->second;
		if (delItem->owner == toremove || delItem->value == (void*)toremove) {
			// Remove it from the m_ListItems
			it = m_ListItems.erase(it);

			// Remove it from the CListCtrl
			LVFINDINFO find;
			find.flags = LVFI_PARAM;
			find.lParam = (LPARAM)delItem;
			int iItem = FindItem(&find);
			if (iItem >= 0)
				DeleteItem(iItem);

			// finally, it could be deleted
			delete delItem;
			bResult = true;
		} else
			++it;
	}
	ShowFilesCount();
	return bResult;
}

void CDownloadListCtrl::UpdateItem(void *toupdate)
{
	if (theApp.IsClosing())
		return;

	// Retrieve all entries matching the source
	for (ListItems::const_iterator it = m_ListItems.lower_bound(toupdate); it != m_ListItems.end() && it->first == toupdate; ++it) {
		CtrlItem_Struct *updateItem = it->second;

		// Find entry in CListCtrl and update object
		LVFINDINFO find;
		find.flags = LVFI_PARAM;
		find.lParam = (LPARAM)updateItem;
		int iItem = FindItem(&find);
		if (iItem >= 0) {
			updateItem->dwUpdated = 0;
			Update(iItem);
		}
	}
	m_availableCommandsDirty = true;
}

void CDownloadListCtrl::DrawFileItem(CDC &dc, int nColumn, LPCRECT lpRect, UINT uDrawTextAlignment, CtrlItem_Struct *pCtrlItem)
{
	/*const*/ CPartFile *pPartFile = static_cast<CPartFile*>(pCtrlItem->value);
	const CString &sItem(GetFileItemDisplayText(pPartFile, nColumn));
	CRect rcDraw(lpRect);
	switch (nColumn) {
	case 0: // file name
		{
			LONG iIconPosY = max((rcDraw.Height() - theApp.GetSmallSytemIconSize().cy) / 2,  0);
			int iImage = theApp.GetFileTypeSystemImageIdx(pPartFile->GetFileName());
			if (theApp.GetSystemImageList() != NULL)
				::ImageList_Draw(theApp.GetSystemImageList(), iImage, dc.GetSafeHdc(), rcDraw.left, rcDraw.top + iIconPosY, ILD_TRANSPARENT);
			rcDraw.left += theApp.GetSmallSytemIconSize().cx;

			if (thePrefs.ShowRatingIndicator() && (pPartFile->HasComment() || pPartFile->HasRating() || pPartFile->IsKadCommentSearchRunning())) {
				m_ImageList.Draw(&dc, 14 + pPartFile->UserRating(true), CPoint(rcDraw.left + 2, rcDraw.top + iIconPosY), ILD_NORMAL);
				rcDraw.left += 2 + RATING_ICON_WIDTH;
			}

			rcDraw.left += sm_iLabelOffset;
			dc.DrawText(sItem, rcDraw, MLC_DT_TEXT | uDrawTextAlignment);
		}
		break;
	case 5: // progress
		{
			--rcDraw.bottom;
			++rcDraw.top;

			int iWidth = rcDraw.Width();
			int iHeight = rcDraw.Height();
			if (!DownloadProgressBarSeams::HasDrawableExtent(iWidth, iHeight))
				break;
			HGDIOBJ hOldBitmap;
			CDC cdcStatus;
			cdcStatus.CreateCompatibleDC(&dc);
			const ULONGLONG curTick = ::GetTickCount64();
			if (!(HBITMAP)pCtrlItem->status || curTick >= pCtrlItem->dwUpdated
				|| pCtrlItem->status.GetBitmapDimension() != CSize(iWidth, iHeight))
			{
				pCtrlItem->status.DeleteObject();
				pCtrlItem->status.CreateCompatibleBitmap(&dc, iWidth, iHeight);
				hOldBitmap = cdcStatus.SelectObject(pCtrlItem->status);

				CRect rec_status(0, 0, iWidth, iHeight);
				const bool bUseFlatBar = thePrefs.UseFlatBar();
				const int iSavedDC = DownloadProgressBarSeams::ShouldIsolateFlatBarDcState(bUseFlatBar) ? cdcStatus.SaveDC() : 0;
				pPartFile->DrawStatusBar(cdcStatus, rec_status, bUseFlatBar);
				if (iSavedDC != 0)
					cdcStatus.RestoreDC(iSavedDC);
				pCtrlItem->dwUpdated = curTick + DLC_BARUPDATE + (rand() & 0x7f);
			} else
				hOldBitmap = cdcStatus.SelectObject(pCtrlItem->status);
			dc.BitBlt(rcDraw.left, rcDraw.top, iWidth, iHeight, &cdcStatus, 0, 0, SRCCOPY);
			cdcStatus.SelectObject(hOldBitmap);

			if (thePrefs.GetUseDwlPercentage()) {
				COLORREF oldclr = dc.SetTextColor(RGB(255, 255, 255));
				int iOldBkMode = dc.SetBkMode(TRANSPARENT);
				dc.DrawText(CPTR(sItem, sItem.ReverseFind(_T(' ')) + 1), -1, rcDraw, (MLC_DT_TEXT & ~DT_LEFT) | DT_CENTER);
				dc.SetBkMode(iOldBkMode);
				dc.SetTextColor(oldclr);
			}
		}
		break;
	default:
		dc.DrawText(sItem, rcDraw, MLC_DT_TEXT | uDrawTextAlignment);
	}
}

CString CDownloadListCtrl::GetSourceItemDisplayText(const CtrlItem_Struct *pCtrlItem, int iSubItem)
{
	CString sText;
	if (!IsLiveSourceItem(pCtrlItem))
		return sText;

	const CUpDownClient *pClient = static_cast<CUpDownClient*>(pCtrlItem->value);
	switch (iSubItem) {
	case 0: //icon, name, status
		if (pClient->GetUserName())
			return CString(pClient->GetUserName());
		sText.Format(_T("(%s)"), (LPCTSTR)GetResString(IDS_UNKNOWN));
		break;
	case 1: //source from
		{
			UINT uid;
			switch (pClient->GetSourceFrom()) {
			case SF_SERVER:
				uid = IDS_ED2KSERVER;
				break;
			case SF_KADEMLIA:
				uid = IDS_KADEMLIA;
				break;
			case SF_SOURCE_EXCHANGE:
				uid = IDS_SE;
				break;
			case SF_PASSIVE:
				uid = IDS_PASSIVE;
				break;
			case SF_LINK:
				uid = IDS_SW_LINK;
				break;
			default:
				uid = 0;
			}
			if (uid)
				return GetResString(uid);
		}
		break;
	case 2: //transferred
	case 3: //completed
		// - 'Transferred' column: Show transferred data
		// - 'Completed' column: If 'Transferred' column is hidden, show the amount of transferred data
		//	  in 'Completed' column. This is plain wrong (at least when receiving compressed data), but
		//	  users seem to got used to it.
		if (iSubItem == 2 || IsColumnHidden(2)) {
			if (pCtrlItem->type == AVAILABLE_SOURCE && pClient->GetTransferredDown())
				return CastItoXBytes(pClient->GetTransferredDown());
		}
		break;
	case 4: //speed
		if (pCtrlItem->type == AVAILABLE_SOURCE && pClient->GetDownloadDatarate())
			return CastItoXBytes(pClient->GetDownloadDatarate(), false, true);
		break;
	case 5: //file info
		return GetResString(IDS_DL_PROGRESS);
	case 6: //sources
		return pClient->DbgGetFullClientSoftVer();
	case 7: //prio
		if (pClient->GetDownloadState() == DS_ONQUEUE) {
			if (pClient->IsRemoteQueueFull())
				return GetResString(IDS_QUEUEFULL);
			if (pClient->GetRemoteQueueRank())
				sText.Format(_T("QR: %u"), pClient->GetRemoteQueueRank());
		}
		break;
	case 8: //status
		{
			if (pCtrlItem->type == AVAILABLE_SOURCE)
				sText = pClient->GetDownloadStateDisplayString();
			else {
				sText = GetResString(IDS_ASKED4ANOTHERFILE);
// ZZ:DownloadManager -->
				UINT uid;
				if (pClient->IsInNoNeededList(pCtrlItem->owner))
					uid = IDS_NONEEDEDPARTS;
				else if (pClient->GetDownloadState() == DS_DOWNLOADING)
					uid = IDS_TRANSFERRING;
				else if (const_cast<CUpDownClient*>(pClient)->IsSwapSuspended(pClient->GetRequestFile()))
					uid = IDS_SOURCESWAPBLOCKED;
				else
					uid = 0;
				if (uid)
					sText.AppendFormat(_T(" (%s)"), (LPCTSTR)GetResString(uid));
				if (pClient->GetRequestFile() && !pClient->GetRequestFile()->GetFileName().IsEmpty())
					sText.AppendFormat(_T(": \"%s\""), (LPCTSTR)pClient->GetRequestFile()->GetFileName());
			}

			if (!pClient->m_OtherRequests_list.IsEmpty())
				sText += _T('*');
// ZZ:DownloadManager <--
		}
		break;
	case 15: // geo location
		if (theApp.geolocation != NULL)
			sText = theApp.geolocation->GetDisplayText(GetClientGeoIP(pClient));
		break;
	case 17: // IP
		sText = ipstr(GetClientGeoIP(pClient));
		break;
	case 18: // LowID
		sText = GetResString(pClient->HasLowID() ? IDS_IDLOW : IDS_IDHIGH);
		break;
	//	break;
	//case 9: //trust
	//case 10: //remaining time & size
	//case 11: //last seen complete
	//case 12: //last received
	//case 13: //category
	//case 14: //added on
	}
	return sText;
}

void CDownloadListCtrl::DrawSourceItem(CDC &dc, int nColumn, LPCRECT lpRect, UINT uDrawTextAlignment, CtrlItem_Struct *pCtrlItem)
{
	if (!IsLiveSourceItem(pCtrlItem))
		return;

	const CUpDownClient *pClient = static_cast<CUpDownClient*>(pCtrlItem->value);
	const CString &sItem(GetSourceItemDisplayText(pCtrlItem, nColumn));
	switch (nColumn) {
	case 0: // icon, name, status
		{
			CRect rcItem(*lpRect);
			int iIconPosY = (rcItem.Height() > 16) ? ((rcItem.Height() - 15) / 2) : 0;
			POINT point{rcItem.left, rcItem.top + iIconPosY};
			int iImage;
			if (pCtrlItem->type == AVAILABLE_SOURCE) {
				switch (pClient->GetDownloadState()) {
				case DS_CONNECTED:
				case DS_CONNECTING:
				case DS_WAITCALLBACK:
				case DS_WAITCALLBACKKAD:
				case DS_TOOMANYCONNS:
				case DS_TOOMANYCONNSKAD:
					iImage = 2;
					break;
				case DS_ONQUEUE:
					iImage = pClient->IsRemoteQueueFull() ? 3 : 1;
					break;
				case DS_DOWNLOADING:
				case DS_REQHASHSET:
					iImage = 0;
					break;
				case DS_NONEEDEDPARTS:
				case DS_ERROR:
					iImage = 3;
					break;
				default:
					iImage = 4;
				}
			} else
				iImage = 3;
			m_ImageList.Draw(&dc, iImage, point, ILD_NORMAL);
			rcItem.left += 20;

			UINT uOvlImg = static_cast<UINT>((pClient->Credits() && pClient->Credits()->GetCurrentIdentState(pClient->GetIP()) == IS_IDENTIFIED));
			uOvlImg |= (static_cast<UINT>(pClient->IsObfuscatedConnectionEstablished()) << 1);

			if (pClient->IsFriend())
				iImage = 6;
			else
				switch (pClient->GetClientSoft()) {
				case SO_EDONKEYHYBRID:
					iImage = 9;
					break;
				case SO_MLDONKEY:
					iImage = 8;
					break;
				case SO_SHAREAZA:
					iImage = 10;
					break;
				case SO_AMULE:
					iImage = 12;
					break;
				case SO_LPHANT:
					iImage = 13;
					break;
				default:
					iImage = pClient->ExtProtocolAvailable() ? 5 : 7;
				}
			const POINT point2{rcItem.left, rcItem.top + iIconPosY};
			m_ImageList.Draw(&dc, iImage, point2, ILD_NORMAL | INDEXTOOVERLAYMASK(uOvlImg));
			rcItem.left += 20;

			dc.DrawText(sItem, &rcItem, MLC_DT_TEXT | uDrawTextAlignment);
		}
		break;
	case 5: // file info
		{
			int iWidth = lpRect->right - lpRect->left;
			int iHeight = lpRect->bottom - lpRect->top - 2;
			if (!DownloadProgressBarSeams::HasDrawableExtent(iWidth, iHeight))
				break;
			HGDIOBJ hOldBitmap;
			CDC cdcStatus;
			cdcStatus.CreateCompatibleDC(&dc);
			const ULONGLONG curTick = ::GetTickCount64();
			if (!(HBITMAP)pCtrlItem->status || curTick >= pCtrlItem->dwUpdated
				|| pCtrlItem->status.GetBitmapDimension() != CSize(iWidth, iHeight))
			{
				pCtrlItem->status.DeleteObject();
				pCtrlItem->status.CreateCompatibleBitmap(&dc, iWidth, iHeight);
				hOldBitmap = cdcStatus.SelectObject(pCtrlItem->status);

				CRect rec_status(0, 0, iWidth, iHeight);
				const bool bUseFlatBar = thePrefs.UseFlatBar();
				const int iSavedDC = DownloadProgressBarSeams::ShouldIsolateFlatBarDcState(bUseFlatBar) ? cdcStatus.SaveDC() : 0;
				pClient->DrawStatusBar(cdcStatus, rec_status, (pCtrlItem->type == UNAVAILABLE_SOURCE), bUseFlatBar);
				if (iSavedDC != 0)
					cdcStatus.RestoreDC(iSavedDC);
				pCtrlItem->dwUpdated = curTick + DLC_BARUPDATE + (rand() & 0x7f);
			} else
				hOldBitmap = cdcStatus.SelectObject(pCtrlItem->status);
			dc.BitBlt(lpRect->left, lpRect->top + 1, iWidth, iHeight, &cdcStatus, 0, 0, SRCCOPY);
			cdcStatus.SelectObject(hOldBitmap);
		}
		break;
	case 15: // geo location
		{
			CRect rcItem(*lpRect);
			int iIconPosY = (rcItem.Height() > 16) ? ((rcItem.Height() - 15) / 2) : 0;
			if (theApp.geolocation != NULL) {
				const POINT point{rcItem.left, rcItem.top + iIconPosY};
				if (theApp.geolocation->DrawFlag(dc, GetClientGeoIP(pClient), point))
					rcItem.left += 18 + sm_iLabelOffset;
			}
			dc.DrawText(sItem, &rcItem, MLC_DT_TEXT | uDrawTextAlignment);
		}
		break;
	case 16: // percentage
		break;
	//case 9: // trust
	//case 10: // remaining time & size
	//case 11: // last seen complete
	//case 12: // last received
	//case 13: // category
	//case 14: // added on
	//	break;
	default:
		dc.DrawText(sItem, const_cast<LPRECT>(lpRect), MLC_DT_TEXT | uDrawTextAlignment);
	}
}

void CDownloadListCtrl::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	if (!lpDrawItemStruct->itemData || theApp.IsClosing())
		return;

	RECT rcItem(lpDrawItemStruct->rcItem);
	CMemoryDC dc(CDC::FromHandle(lpDrawItemStruct->hDC), &rcItem);
	BOOL bCtrlFocused;
	InitItemMemDC(dc, lpDrawItemStruct, bCtrlFocused);
	RECT rcClient;
	GetClientRect(&rcClient);
	CtrlItem_Struct *content = reinterpret_cast<CtrlItem_Struct*>(lpDrawItemStruct->itemData);
	if (IsSourceCtrlItem(content) && !IsLiveSourceItem(content))
		return;
	if (content->type == FILE_TYPE && !IsLiveFileItem(content))
		return;

	if (m_pFontBold)
		if (content->type == FILE_TYPE && static_cast<CPartFile*>(content->value)->GetTransferringSrcCount()
			|| ((content->type == UNAVAILABLE_SOURCE || content->type == AVAILABLE_SOURCE)
				&& static_cast<CUpDownClient*>(content->value)->GetDownloadState() == DS_DOWNLOADING))
		{
			dc.SelectObject(m_pFontBold);
		}

	bool isChild = content->type != FILE_TYPE;
	bool notLast = lpDrawItemStruct->itemID + 1 < (UINT)GetItemCount();
	bool notFirst = lpDrawItemStruct->itemID > 0;
	int tree_start = 0;
	int tree_end = 0;

	int iTreeOffset = 8 - sm_iLabelOffset; //6
	const CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();
	int iCount = pHeaderCtrl->GetItemCount();
	LONG itemLeft = rcItem.left;
	rcItem.right = rcItem.left - sm_iLabelOffset;
	rcItem.left += sm_iIconOffset;

	if (!isChild && !g_bLowColorDesktop && (lpDrawItemStruct->itemState & ODS_SELECTED) == 0) {
		DWORD dwCatColor = thePrefs.GetCatColor(static_cast<CPartFile*>(content->value)->GetCategory(), COLOR_WINDOWTEXT);
		if (dwCatColor > 0)
			dc.SetTextColor(dwCatColor);
	}
	for (int iCurrent = 0; iCurrent < iCount; ++iCurrent) {
		int iColumn = pHeaderCtrl->OrderToIndex(iCurrent);
		if (IsColumnHidden(iColumn))
			continue;

		UINT uDrawTextAlignment;
		int iColumnWidth = GetColumnWidth(iColumn, uDrawTextAlignment);
		rcItem.left = itemLeft;
		rcItem.right = itemLeft + iColumnWidth;
		switch (iColumn) {
		case 5: //progress
			//set up tree vars
			tree_start = rcItem.left + 1;
			rcItem.left += iTreeOffset;
			tree_end = rcItem.left;
			rcItem.right -= iTreeOffset - sm_iLabelOffset;
			[[fallthrough]];
		default:
			rcItem.left += sm_iLabelOffset;
			rcItem.right -= sm_iLabelOffset;
			if (rcItem.left < rcItem.right && HaveIntersection(rcClient, rcItem))
				if (isChild)
					DrawSourceItem(dc, iColumn, &rcItem, uDrawTextAlignment, content);
				else
					DrawFileItem(dc, iColumn, &rcItem, uDrawTextAlignment, content);
		}
		itemLeft += iColumnWidth;
	}

	DrawFocusRect(dc, &lpDrawItemStruct->rcItem, lpDrawItemStruct->itemState & ODS_FOCUS, bCtrlFocused, lpDrawItemStruct->itemState & ODS_SELECTED);

	//draw tree last so it draws over selected and focus (looks better)
	if (tree_start < tree_end) {
		//set new bounds
		RECT tree_rect{tree_start, lpDrawItemStruct->rcItem.top, tree_end, lpDrawItemStruct->rcItem.bottom};
		dc.SetBoundsRect(&tree_rect, DCB_DISABLE);

		//gather some information
		bool hasNext = notLast
			&& reinterpret_cast<CtrlItem_Struct*>(GetItemData(lpDrawItemStruct->itemID + 1))->type != FILE_TYPE;
		bool isOpenRoot = hasNext && content->type == FILE_TYPE;
		//bool isExpandable = !isChild && static_cast<CPartFile*>(content->value)->GetSourceCount() > 0;
		//might as well calculate these now
		int treeCenter = tree_start + 3;
		int middle = (rcItem.top + rcItem.bottom + 1) / 2;

		//set up a new pen for drawing the tree
		CPen pn, *oldpn;
		pn.CreatePen(PS_SOLID, 1, m_crWindowText);
		oldpn = dc.SelectObject(&pn);

		if (isChild) {
			//draw the line to the status bar
			dc.MoveTo(tree_end, middle);
			dc.LineTo(tree_start + 3, middle);

			//draw the line to the child node
			if (hasNext) {
				dc.MoveTo(treeCenter, middle);
				dc.LineTo(treeCenter, rcItem.bottom + 1);
			}
		} else if (isOpenRoot) {
			//draw circle
			RECT circle_rec{treeCenter - 2, middle - 2, treeCenter + 3, middle + 3};
			COLORREF crBk = dc.GetBkColor();
			CBrush brush(m_crWindowText);
			dc.FrameRect(&circle_rec, &brush);
			dc.SetPixelV(circle_rec.left, circle_rec.top, crBk);
			dc.SetPixelV(circle_rec.right - 1, circle_rec.top, crBk);
			dc.SetPixelV(circle_rec.left, circle_rec.bottom - 1, crBk);
			dc.SetPixelV(circle_rec.right - 1, circle_rec.bottom - 1, crBk);
			//draw the line to the child node (hasNext is true here)
			dc.MoveTo(treeCenter, middle + 3);
			dc.LineTo(treeCenter, rcItem.bottom + 1);
		} /*else if(isExpandable) {
			//draw a + sign
			dc.MoveTo(treeCenter, middle - 2);
			dc.LineTo(treeCenter, middle + 3);
			dc.MoveTo(treeCenter - 2, middle);
			dc.LineTo(treeCenter + 3, middle);
		}*/

		//draw the line back up to parent node
		if (notFirst && isChild) {
			dc.MoveTo(treeCenter, middle);
			dc.LineTo(treeCenter, rcItem.top - 1);
		}

		//put the old pen back
		dc.SelectObject(oldpn);
		pn.DeleteObject();
	}
}

void CDownloadListCtrl::HideSources(CPartFile *toCollapse)
{
	if (!toCollapse->srcarevisible)
		return;
	SetRedraw(false);
	for (int i = GetItemCount(); --i >= 0;) {
		CtrlItem_Struct *item = reinterpret_cast<CtrlItem_Struct*>(GetItemData(i));
		if (item != NULL && item->owner == toCollapse) {
			item->dwUpdated = 0;
			item->status.DeleteObject();
			DeleteItem(i);
		}
	}
	toCollapse->srcarevisible = false;
	SetRedraw(true);
}

void CDownloadListCtrl::ExpandCollapseItem(int iItem, int iAction, bool bCollapseSource)
{
	if (iItem == -1)
		return;
	CtrlItem_Struct *content = reinterpret_cast<CtrlItem_Struct*>(GetItemData(iItem));
	if (IsSourceCtrlItem(content) && !IsLiveSourceItem(content)) {
		PruneStaleSourceItems();
		return;
	}

	PruneStaleSourceItems();
	if (iItem >= GetItemCount())
		return;
	content = reinterpret_cast<CtrlItem_Struct*>(GetItemData(iItem));

	// to collapse/expand files when one of its sources is selected
	if (content != NULL && bCollapseSource && content->parent != NULL) {
		content = content->parent;

		LVFINDINFO find;
		find.flags = LVFI_PARAM;
		find.lParam = (LPARAM)content;
		iItem = FindItem(&find);
		if (iItem == -1)
			return;
	}

	if (!content || content->type != FILE_TYPE)
		return;
	if (!IsLiveFileItem(content)) {
		PruneStaleFileItems();
		return;
	}

	CPartFile *partfile = static_cast<CPartFile*>(content->value);
	if (!partfile)
		return;

	if (partfile->CanOpenFile()) {
		partfile->OpenFile();
		return;
	}

	// Check if the sources branch is visible
	if (!partfile->srcarevisible) {
		if (iAction > COLLAPSE_ONLY) {
			SetRedraw(false);

			// Go through the whole list to find out the sources for this file
			// Remark: don't use GetSourceCount() => UNAVAILABLE_SOURCE
			for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
				const CtrlItem_Struct *cur_item = it->second;
				if (cur_item->owner == partfile && (!IsSourceCtrlItem(cur_item) || IsLiveSourceItem(cur_item))) {
					partfile->srcarevisible = true;
					InsertItem(LVIF_TEXT | LVIF_PARAM, iItem + 1, LPSTR_TEXTCALLBACK, 0, 0, 0, (LPARAM)cur_item);
				}
			}

			SetRedraw(true);
		}
	} else if (iAction == EXPAND_COLLAPSE || iAction == COLLAPSE_ONLY) {
		if (GetItemState(iItem, LVIS_SELECTED | LVIS_FOCUSED) != (LVIS_SELECTED | LVIS_FOCUSED)) {
			SetItemState(iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			SetSelectionMark(iItem);
		}
		HideSources(partfile);
	}
}

void CDownloadListCtrl::OnLvnItemActivate(LPNMHDR pNMHDR, LRESULT *pResult)
{
	LPNMITEMACTIVATE pNMIA = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);

	if (thePrefs.IsDoubleClickEnabled() || pNMIA->iSubItem > 0)
		ExpandCollapseItem(pNMIA->iItem, EXPAND_COLLAPSE);
	*pResult = 0;
}

void CDownloadListCtrl::OnContextMenu(CWnd*, CPoint point)
{
	int iSel = GetNextItem(-1, LVIS_SELECTED);
	if (iSel >= 0) {
		const CtrlItem_Struct *content = reinterpret_cast<CtrlItem_Struct*>(GetItemData(iSel));
		if (IsSourceCtrlItem(content) && !IsLiveSourceItem(content)) {
			PruneStaleSourceItems();
			return;
		}
		if (content != NULL && content->type == FILE_TYPE) {
			if (!IsLiveFileItem(content)) {
				PruneStaleFileItems();
				return;
			}
			// get merged settings
			int iSelectedItems = 0;
			int iFilesNotDone = 0;
			int iFilesToPause = 0;
			int iFilesToStop = 0;
			int iFilesToResume = 0;
			int iFilesToOpen = 0;
			int iFilesGetPreviewParts = 0;
			int iFilesPreviewType = 0;
			int iFilesToPreview = 0;
			int iFilesToCancel = 0;
			int iFilesCanPauseOnPreview = 0;
			int iFilesDoPauseOnPreview = 0;
			int iFilesInCats = 0;
			int iFilesFollowMajority = 0;
			int iFilesFollowMajorityApplicable = 0;
			UINT uPrioMenuItem = 0;
			const CPartFile *file1 = NULL;

			bool bFirstItem = true;
			for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
				const CtrlItem_Struct *pItemData = reinterpret_cast<CtrlItem_Struct*>(GetItemData(GetNextSelectedItem(pos)));
				if (!IsLiveFileItem(pItemData))
					continue;
				const CPartFile *pFile = static_cast<CPartFile*>(pItemData->value);
				++iSelectedItems;

				iFilesToCancel += static_cast<int>(pFile->GetStatus() != PS_COMPLETING);
				iFilesNotDone += static_cast<int>((pFile->GetStatus() != PS_COMPLETE && pFile->GetStatus() != PS_COMPLETING));
				iFilesToStop += static_cast<int>(pFile->CanStopFile());
				iFilesToPause += static_cast<int>(pFile->CanPauseFile());
				iFilesToResume += static_cast<int>(pFile->CanResumeFile());
				iFilesToOpen += static_cast<int>(pFile->CanOpenFile());
				iFilesGetPreviewParts += static_cast<int>(pFile->GetPreviewPrio());
				iFilesPreviewType += static_cast<int>(pFile->IsPreviewableFileType());
				iFilesToPreview += static_cast<int>(pFile->IsReadyForPreview());
				iFilesCanPauseOnPreview += static_cast<int>(pFile->IsPreviewableFileType() && !pFile->IsReadyForPreview() && pFile->CanPauseFile());
				iFilesDoPauseOnPreview += static_cast<int>(pFile->IsPausingOnPreview());
				iFilesInCats += static_cast<int>(!pFile->HasDefaultCategory());
				if (pFile->GetStatus() != PS_COMPLETE && pFile->GetStatus() != PS_COMPLETING) {
					++iFilesFollowMajorityApplicable;
					iFilesFollowMajority += static_cast<int>(pFile->IsFollowMajorityFilenameEnabled());
				}
				UINT uCurPrioMenuItem;
				if (pFile->IsAutoDownPriority())
					uCurPrioMenuItem = MP_PRIOAUTO;
				else
					switch (pFile->GetDownPriority()) {
					case PR_HIGH:
						uCurPrioMenuItem = MP_PRIOHIGH;
						break;
					case PR_NORMAL:
						uCurPrioMenuItem = MP_PRIONORMAL;
						break;
					case PR_LOW:
						uCurPrioMenuItem = MP_PRIOLOW;
						break;
					default:
						uCurPrioMenuItem = 0;
						ASSERT(0);
					}

				if (bFirstItem) {
					bFirstItem = false;
					file1 = pFile;
					uPrioMenuItem = uCurPrioMenuItem;
				} else if (uPrioMenuItem != uCurPrioMenuItem)
					uPrioMenuItem = 0;
			}

			EnableSubMenuItem(m_FileMenu, m_PrioMenu.m_hMenu, iFilesNotDone > 0 ? MF_ENABLED : MF_GRAYED);
			m_PrioMenu.CheckMenuRadioItem(MP_PRIOLOW, MP_PRIOAUTO, uPrioMenuItem, 0);
			int iBatchFilesToPause = 0;
			int iBatchFilesToResume = 0;
			int iBatchFilesToStop = 0;
			CountBatchCommandsInCurCat(iBatchFilesToPause, iBatchFilesToResume, iBatchFilesToStop);
			EnableSubMenuItem(m_FileMenu, m_BatchMenu.m_hMenu, (iBatchFilesToPause > 0 || iBatchFilesToResume > 0 || iBatchFilesToStop > 0) ? MF_ENABLED : MF_GRAYED);
			m_BatchMenu.EnableMenuItem(MP_PAUSE_CATEGORY, iBatchFilesToPause > 0 ? MF_ENABLED : MF_GRAYED);
			m_BatchMenu.EnableMenuItem(MP_STOP_CATEGORY, iBatchFilesToStop > 0 ? MF_ENABLED : MF_GRAYED);
			m_BatchMenu.EnableMenuItem(MP_RESUME_CATEGORY, iBatchFilesToResume > 0 ? MF_ENABLED : MF_GRAYED);

			// enable commands if there is at least one item which can be used for the action
			m_FileMenu.EnableMenuItem(MP_CANCEL, iFilesToCancel > 0 ? MF_ENABLED : MF_GRAYED);
			m_FileMenu.EnableMenuItem(MP_STOP, iFilesToStop > 0 ? MF_ENABLED : MF_GRAYED);
			m_FileMenu.EnableMenuItem(MP_PAUSE, iFilesToPause > 0 ? MF_ENABLED : MF_GRAYED);
			m_FileMenu.EnableMenuItem(MP_RESUME, iFilesToResume > 0 ? MF_ENABLED : MF_GRAYED);

			bool bOpenEnabled = (iSelectedItems == 1 && iFilesToOpen == 1);
			m_FileMenu.EnableMenuItem(MP_OPEN, bOpenEnabled ? MF_ENABLED : MF_GRAYED);
			bool bOpenFolderEnabled = (iSelectedItems == 1 && file1 != NULL && !file1->GetPath().IsEmpty());
			m_FileMenu.EnableMenuItem(MP_OPENFOLDER, bOpenFolderEnabled ? MF_ENABLED : MF_GRAYED);

			CMenu PreviewWithMenu;
			PreviewWithMenu.CreateMenu();
			int iPreviewMenuEntries = thePreviewApps.GetAllMenuEntries(PreviewWithMenu, (iSelectedItems == 1) ? file1 : NULL);
			if (!thePrefs.GetPreviewPrio()) {
				m_PreviewMenu.EnableMenuItem(MP_TRY_TO_GET_PREVIEW_PARTS, (iSelectedItems == 1 && iFilesPreviewType == 1 && iFilesToPreview == 0 && iFilesNotDone == 1) ? MF_ENABLED : MF_GRAYED);
				m_PreviewMenu.CheckMenuItem(MP_TRY_TO_GET_PREVIEW_PARTS, (iSelectedItems == 1 && iFilesGetPreviewParts == 1) ? MF_CHECKED : MF_UNCHECKED);
			}
			m_PreviewMenu.EnableMenuItem(MP_PREVIEW, (iSelectedItems == 1 && iFilesToPreview == 1) ? MF_ENABLED : MF_GRAYED);
			m_PreviewMenu.EnableMenuItem(MP_PAUSEONPREVIEW, iFilesCanPauseOnPreview > 0 ? MF_ENABLED : MF_GRAYED);
			m_PreviewMenu.CheckMenuItem(MP_PAUSEONPREVIEW, (iSelectedItems > 0 && iFilesDoPauseOnPreview == iSelectedItems) ? MF_CHECKED : MF_UNCHECKED);
			EnableSubMenuItem(m_FileMenu, m_PreviewMenu.m_hMenu, m_PreviewMenu.HasEnabledItems() ? MF_ENABLED : MF_GRAYED);

			if (iPreviewMenuEntries > 0) {
				if (thePrefs.GetExtraPreviewWithMenu())
					m_FileMenu.InsertMenu(MP_METINFO, MF_POPUP | MF_BYCOMMAND | (iSelectedItems == 1 ? MF_ENABLED : MF_GRAYED), (UINT_PTR)PreviewWithMenu.m_hMenu, GetResString(IDS_PREVIEWWITH));
				else
					m_PreviewMenu.InsertMenu(1, MF_POPUP | MF_BYPOSITION | (iSelectedItems == 1 ? MF_ENABLED : MF_GRAYED), (UINT_PTR)PreviewWithMenu.m_hMenu, GetResString(IDS_PREVIEWWITH));
			}

			bool bDetailsEnabled = (iSelectedItems > 0);
			m_FileMenu.EnableMenuItem(MP_METINFO, bDetailsEnabled ? MF_ENABLED : MF_GRAYED);
			if (thePrefs.IsDoubleClickEnabled() && bOpenEnabled)
				m_FileMenu.SetDefaultItem(MP_OPEN);
			else if (!thePrefs.IsDoubleClickEnabled() && bDetailsEnabled)
				m_FileMenu.SetDefaultItem(MP_METINFO);
			else
				m_FileMenu.SetDefaultItem(UINT_MAX);
			m_FileMenu.EnableMenuItem(MP_VIEWFILECOMMENTS, (iSelectedItems >= 1 /*&& iFilesNotDone == 1*/) ? MF_ENABLED : MF_GRAYED);
			m_FileMenu.EnableMenuItem(MP_FOLLOWMAJORITYFILENAME, iFilesFollowMajorityApplicable > 0 ? MF_ENABLED : MF_GRAYED);
			m_FileMenu.CheckMenuItem(MP_FOLLOWMAJORITYFILENAME, MF_BYCOMMAND | ((iFilesFollowMajorityApplicable > 0 && iFilesFollowMajority == iFilesFollowMajorityApplicable) ? MF_CHECKED : MF_UNCHECKED));
			int total;
			m_FileMenu.EnableMenuItem(MP_CLEARCOMPLETED, GetCompleteDownloads(m_curTab, total) > 0 ? MF_ENABLED : MF_GRAYED);
			EnableSubMenuItem(m_FileMenu, m_SourcesMenu.m_hMenu, MF_ENABLED);
			m_SourcesMenu.EnableMenuItem(MP_ADDSOURCE, (iSelectedItems == 1 && iFilesToStop == 1) ? MF_ENABLED : MF_GRAYED);
			m_SourcesMenu.EnableMenuItem(MP_SETSOURCELIMIT, (iFilesNotDone == iSelectedItems) ? MF_ENABLED : MF_GRAYED);

			m_FileMenu.EnableMenuItem(thePrefs.GetShowCopyEd2kLinkCmd() ? MP_GETED2KLINK : MP_SHOWED2KLINK, iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED);
			m_FileMenu.EnableMenuItem(MP_PASTE, theApp.IsEd2kFileLinkInClipboard() ? MF_ENABLED : MF_GRAYED);
			m_FileMenu.EnableMenuItem(MP_FIND, GetItemCount() > 0 ? MF_ENABLED : MF_GRAYED);
			m_FileMenu.EnableMenuItem(MP_SEARCHRELATED, theApp.emuledlg->searchwnd->CanSearchRelatedFiles() ? MF_ENABLED : MF_GRAYED);

			CTitledMenu CopyMenu;
			CopyMenu.CreateMenu();
			CopyMenu.AddMenuTitle(NULL, true);
			CopyMenu.AppendMenu(MF_STRING | (iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED), MP_COPY_FILE_NAME, GetResString(IDS_COPY_FILE_NAME));
			CopyMenu.AppendMenu(MF_STRING | (iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED), MP_COPY_FILE_HASH, GetResString(IDS_COPY_HASH));
			CopyMenu.AppendMenu(MF_STRING | (iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED), MP_COPY_FILE_SIZE, GetResString(IDS_COPY_FILE_SIZE));
			CopyMenu.AppendMenu(MF_STRING | (iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED), MP_COPY_FILE_STATUS, GetResString(IDS_COPY_FILE_STATUS));
			CopyMenu.AppendMenu(MF_STRING | (iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED), MP_COPY_FILE_PROGRESS, GetResString(IDS_COPY_FILE_PROGRESS));
			CopyMenu.AppendMenu(MF_STRING | (iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED), MP_COPY_FILE_PATH, GetResString(IDS_COPY_FILE_PATH));
			CopyMenu.AppendMenu(MF_STRING | (iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED), MP_COPY_FOLDER_PATH, GetResString(IDS_COPY_FOLDER_PATH));
			CopyMenu.AppendMenu(MF_STRING | (iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED), MP_GETED2KLINK, GetResString(IDS_DL_LINK1), _T("ED2KLINK"));
			CopyMenu.AppendMenu(MF_STRING | (iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED), MP_GETHTMLED2KLINK, GetResString(IDS_DL_LINK2), _T("ED2KLINK"));
			CopyMenu.AppendMenu(MF_STRING | (iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED), MP_COPY_FILE_SUMMARY, AddMenuShortcutLabel(GetResString(IDS_COPY_FILE_SUMMARY), _T("Ctrl+Shift+C")));
			m_FileMenu.AppendMenu(MF_POPUP | (iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED), (UINT_PTR)CopyMenu.m_hMenu, GetResString(IDS_COPY));

			CTitledMenu WebMenu;
			WebMenu.CreateMenu();
			WebMenu.AddMenuTitle(NULL, true);
			int iWebMenuEntries = theWebServices.GetFileMenuEntries(&WebMenu);
			UINT flag = (iWebMenuEntries == 0 || iSelectedItems != 1) ? MF_GRAYED : MF_ENABLED;
			m_FileMenu.AppendMenu(MF_POPUP | flag, (UINT_PTR)WebMenu.m_hMenu, GetResString(IDS_WEBSERVICES), _T("WEB"));

			// create cat-submenu
			CMenu CatsMenu;
			CatsMenu.CreateMenu();
			FillCatsMenu(CatsMenu, iFilesInCats);
			m_FileMenu.AppendMenu(MF_POPUP, (UINT_PTR)CatsMenu.m_hMenu, GetResString(IDS_TOCAT), _T("CATEGORY"));

			bool bToolbarItem = !thePrefs.IsDownloadToolbarEnabled();
			if (bToolbarItem) {
				m_FileMenu.AppendMenu(MF_SEPARATOR);
				m_FileMenu.AppendMenu(MF_STRING, MP_TOGGLEDTOOLBAR, GetResString(IDS_SHOWTOOLBAR));
			}

			GetPopupMenuPos(*this, point);
			m_FileMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
			if (bToolbarItem) {
				VERIFY(m_FileMenu.RemoveMenu(m_FileMenu.GetMenuItemCount() - 1, MF_BYPOSITION));
				VERIFY(m_FileMenu.RemoveMenu(m_FileMenu.GetMenuItemCount() - 1, MF_BYPOSITION));
			}
			VERIFY(m_FileMenu.RemoveMenu(m_FileMenu.GetMenuItemCount() - 1, MF_BYPOSITION));
			VERIFY(m_FileMenu.RemoveMenu(m_FileMenu.GetMenuItemCount() - 1, MF_BYPOSITION));
			VERIFY(m_FileMenu.RemoveMenu(m_FileMenu.GetMenuItemCount() - 1, MF_BYPOSITION));
			if (iPreviewMenuEntries)
				if (!thePrefs.GetExtraPreviewWithMenu())
					VERIFY(RemoveSubMenuItem(m_PreviewMenu, PreviewWithMenu.m_hMenu));
				else
					VERIFY(RemoveSubMenuItem(m_FileMenu, PreviewWithMenu.m_hMenu));

			VERIFY(WebMenu.DestroyMenu());
			VERIFY(CopyMenu.DestroyMenu());
			VERIFY(CatsMenu.DestroyMenu());
			VERIFY(PreviewWithMenu.DestroyMenu());
		} else {
			const CUpDownClient *client = (content != NULL && IsLiveSourceItem(content)) ? static_cast<CUpDownClient*>(content->value) : NULL;
			const bool is_ed2k = client && client->IsEd2kClient();
			CTitledMenu ClientMenu;
			ClientMenu.CreatePopupMenu();
			ClientMenu.AddMenuTitle(GetResString(IDS_CLIENTS), true);
			ClientMenu.AppendMenu(MF_STRING | (client ? MF_ENABLED : MF_GRAYED), MP_DETAIL, GetResString(IDS_SHOWDETAILS), _T("CLIENTDETAILS"));
			ClientMenu.SetDefaultItem(MP_DETAIL);
			ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && !client->IsFriend()) ? MF_ENABLED : MF_GRAYED), MP_ADDFRIEND, GetResString(IDS_ADDFRIEND), _T("ADDFRIEND"));
			ClientMenu.AppendMenu(MF_STRING | (is_ed2k ? MF_ENABLED : MF_GRAYED), MP_MESSAGE, GetResString(IDS_SEND_MSG), _T("SENDMESSAGE"));
			ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && client->GetViewSharedFilesSupport()) ? MF_ENABLED : MF_GRAYED), MP_SHOWLIST, GetResString(IDS_VIEWFILES), _T("VIEWFILES"));
			if (Kademlia::CKademlia::IsRunning() && !Kademlia::CKademlia::IsConnected())
				ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && client->GetKadPort() && client->GetKadVersion() >= KADEMLIA_VERSION2_47a) ? MF_ENABLED : MF_GRAYED), MP_BOOT, GetResString(IDS_BOOTSTRAP));
			CTitledMenu CopyMenu;
			CopyMenu.CreateMenu();
			CopyMenu.AddMenuTitle(NULL, true);
			CopyMenu.AppendMenu(MF_STRING | (client ? MF_ENABLED : MF_GRAYED), MP_COPY_CLIENT_IP, GetResString(IDS_COPY_CLIENT_IP));
			CopyMenu.AppendMenu(MF_STRING | (client ? MF_ENABLED : MF_GRAYED), MP_COPY_CLIENT_USERHASH, GetResString(IDS_COPY_USER_HASH));
			CopyMenu.AppendMenu(MF_STRING | (client ? MF_ENABLED : MF_GRAYED), MP_COPY_CLIENT_SUMMARY, GetResString(IDS_COPY_CLIENT_SUMMARY));
			if (client != NULL && client->GetRequestFile() != NULL) {
				CopyMenu.AppendMenu(MF_STRING, MP_COPY_FILE_NAME, GetResString(IDS_COPY_FILE_NAME));
				CopyMenu.AppendMenu(MF_STRING, MP_COPY_FILE_HASH, GetResString(IDS_COPY_HASH));
			}
			ClientMenu.AppendMenu(MF_POPUP | (client ? MF_ENABLED : MF_GRAYED), (UINT_PTR)CopyMenu.m_hMenu, GetResString(IDS_COPY));
			ClientMenu.AppendMenu(MF_SEPARATOR);
			ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && !client->IsBanned()) ? MF_ENABLED : MF_GRAYED), MP_BAN, GetResString(IDS_BAN));
			ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && client->IsBanned()) ? MF_ENABLED : MF_GRAYED), MP_UNBAN, GetResString(IDS_UNBAN));
			ClientMenu.AppendMenu(MF_STRING | (GetItemCount() > 0 ? MF_ENABLED : MF_GRAYED), MP_FIND, GetResString(IDS_FIND), _T("Search"));

			CMenu A4AFMenu;
			A4AFMenu.CreateMenu();
// ZZ:DownloadManager -->
#ifdef _DEBUG
			if (content && content->type == UNAVAILABLE_SOURCE)
				A4AFMenu.AppendMenu(MF_STRING, MP_A4AF_CHECK_THIS_NOW, GetResString(IDS_A4AF_CHECK_THIS_NOW));
# endif
// <-- ZZ:DownloadManager
			if (A4AFMenu.GetMenuItemCount() > 0)
				ClientMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)A4AFMenu.m_hMenu, GetResString(IDS_A4AF));

			GetPopupMenuPos(*this, point);
			ClientMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);

			VERIFY(A4AFMenu.DestroyMenu());
			VERIFY(CopyMenu.DestroyMenu());
			VERIFY(ClientMenu.DestroyMenu());
		}
	} else { // nothing selected
		int total;
		EnableSubMenuItem(m_FileMenu, m_PrioMenu.m_hMenu, MF_GRAYED);
		int iBatchFilesToPause = 0;
		int iBatchFilesToResume = 0;
		int iBatchFilesToStop = 0;
		CountBatchCommandsInCurCat(iBatchFilesToPause, iBatchFilesToResume, iBatchFilesToStop);
		EnableSubMenuItem(m_FileMenu, m_BatchMenu.m_hMenu, (iBatchFilesToPause > 0 || iBatchFilesToResume > 0 || iBatchFilesToStop > 0) ? MF_ENABLED : MF_GRAYED);
		m_BatchMenu.EnableMenuItem(MP_PAUSE_CATEGORY, iBatchFilesToPause > 0 ? MF_ENABLED : MF_GRAYED);
		m_BatchMenu.EnableMenuItem(MP_STOP_CATEGORY, iBatchFilesToStop > 0 ? MF_ENABLED : MF_GRAYED);
		m_BatchMenu.EnableMenuItem(MP_RESUME_CATEGORY, iBatchFilesToResume > 0 ? MF_ENABLED : MF_GRAYED);
		m_FileMenu.EnableMenuItem(MP_CANCEL, MF_GRAYED);
		m_FileMenu.EnableMenuItem(MP_PAUSE, MF_GRAYED);
		m_FileMenu.EnableMenuItem(MP_STOP, MF_GRAYED);
		m_FileMenu.EnableMenuItem(MP_RESUME, MF_GRAYED);
		m_FileMenu.EnableMenuItem(MP_OPEN, MF_GRAYED);
		m_FileMenu.EnableMenuItem(MP_OPENFOLDER, MF_GRAYED);

		EnableSubMenuItem(m_FileMenu, m_PreviewMenu.m_hMenu, MF_GRAYED);
		if (!thePrefs.GetPreviewPrio()) {
			m_PreviewMenu.EnableMenuItem(MP_TRY_TO_GET_PREVIEW_PARTS, MF_GRAYED);
			m_PreviewMenu.CheckMenuItem(MP_TRY_TO_GET_PREVIEW_PARTS, MF_UNCHECKED);
		}
		m_PreviewMenu.EnableMenuItem(MP_PREVIEW, MF_GRAYED);
		m_PreviewMenu.EnableMenuItem(MP_PAUSEONPREVIEW, MF_GRAYED);

		m_FileMenu.EnableMenuItem(MP_METINFO, MF_GRAYED);
		m_FileMenu.EnableMenuItem(MP_VIEWFILECOMMENTS, MF_GRAYED);
		m_FileMenu.EnableMenuItem(MP_FOLLOWMAJORITYFILENAME, MF_GRAYED);
		m_FileMenu.CheckMenuItem(MP_FOLLOWMAJORITYFILENAME, MF_BYCOMMAND | MF_UNCHECKED);
		m_FileMenu.EnableMenuItem(MP_CLEARCOMPLETED, GetCompleteDownloads(m_curTab, total) > 0 ? MF_ENABLED : MF_GRAYED);
		m_FileMenu.EnableMenuItem(thePrefs.GetShowCopyEd2kLinkCmd() ? MP_GETED2KLINK : MP_SHOWED2KLINK, MF_GRAYED);
		m_FileMenu.EnableMenuItem(MP_PASTE, theApp.IsEd2kFileLinkInClipboard() ? MF_ENABLED : MF_GRAYED);
		m_FileMenu.SetDefaultItem(UINT_MAX);
		if (m_SourcesMenu)
			EnableSubMenuItem(m_FileMenu, m_SourcesMenu.m_hMenu, MF_GRAYED);
		m_FileMenu.EnableMenuItem(MP_SEARCHRELATED, MF_GRAYED);
		m_FileMenu.EnableMenuItem(MP_FIND, GetItemCount() > 0 ? MF_ENABLED : MF_GRAYED);

		// also show the "Web Services" entry, even if its disabled and therefore not usable, it though looks a little
		// less confusing this way.
		CTitledMenu WebMenu;
		WebMenu.CreateMenu();
		WebMenu.AddMenuTitle(NULL, true);
		theWebServices.GetFileMenuEntries(&WebMenu);
		m_FileMenu.AppendMenu(MF_POPUP | MF_GRAYED, (UINT_PTR)WebMenu.m_hMenu, GetResString(IDS_WEBSERVICES), _T("WEB"));

		bool bToolbarItem = !thePrefs.IsDownloadToolbarEnabled();
		if (bToolbarItem) {
			m_FileMenu.AppendMenu(MF_SEPARATOR);
			m_FileMenu.AppendMenu(MF_STRING, MP_TOGGLEDTOOLBAR, GetResString(IDS_SHOWTOOLBAR));
		}

		GetPopupMenuPos(*this, point);
		m_FileMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
		if (bToolbarItem) {
			VERIFY(m_FileMenu.RemoveMenu(m_FileMenu.GetMenuItemCount() - 1, MF_BYPOSITION));
			VERIFY(m_FileMenu.RemoveMenu(m_FileMenu.GetMenuItemCount() - 1, MF_BYPOSITION));
		}
		m_FileMenu.RemoveMenu(m_FileMenu.GetMenuItemCount() - 1, MF_BYPOSITION);
		VERIFY(WebMenu.DestroyMenu());
	}
}

void CDownloadListCtrl::FillCatsMenu(CMenu &rCatsMenu, int iFilesInCats)
{
	ASSERT(rCatsMenu.m_hMenu);
	if (iFilesInCats == -1) {
		iFilesInCats = 0;
		int iSel = GetNextItem(-1, LVIS_SELECTED);
		if (iSel >= 0) {
			const CtrlItem_Struct *content = reinterpret_cast<CtrlItem_Struct*>(GetItemData(iSel));
			if (content != NULL && content->type == FILE_TYPE)
				for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
					const CtrlItem_Struct *pItemData = reinterpret_cast<CtrlItem_Struct*>(GetItemData(GetNextSelectedItem(pos)));
					if (IsLiveFileItem(pItemData)) {
						const CPartFile *pFile = static_cast<CPartFile*>(pItemData->value);
						iFilesInCats += static_cast<int>((!pFile->HasDefaultCategory()));
					}
				}
		}
	}
	rCatsMenu.AppendMenu(MF_STRING, MP_NEWCAT, GetResString(IDS_NEW) + _T("..."));
	CString label(GetResString(IDS_CAT_UNASSIGN));
	label.Remove('(');
	label.Remove(')'); // Remove braces without having to put a new/changed resource string in
	rCatsMenu.AppendMenu(MF_STRING | ((iFilesInCats == 0) ? MF_GRAYED : MF_ENABLED), MP_ASSIGNCAT, label);
	if (thePrefs.GetCatCount() > 1) {
		rCatsMenu.AppendMenu(MF_SEPARATOR);
		for (INT_PTR i = 1; i < thePrefs.GetCatCount(); ++i) {
			label = thePrefs.GetCategory(i)->strTitle;
			DupAmpersand(label);
			rCatsMenu.AppendMenu(MF_STRING, MP_ASSIGNCAT + i, label);
		}
	}
}

CTitledMenu* CDownloadListCtrl::GetPrioMenu()
{
	UINT uPrioMenuItem = 0;
	int iSel = GetNextItem(-1, LVIS_SELECTED);
	if (iSel >= 0) {
		const CtrlItem_Struct *content = reinterpret_cast<CtrlItem_Struct*>(GetItemData(iSel));
		if (content != NULL && content->type == FILE_TYPE) {
			bool bFirstItem = true;
			for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
				const CtrlItem_Struct *pItemData = reinterpret_cast<CtrlItem_Struct*>(GetItemData(GetNextSelectedItem(pos)));
				if (!IsLiveFileItem(pItemData))
					continue;
				const CPartFile *pFile = static_cast<CPartFile*>(pItemData->value);
				UINT uCurPrioMenuItem;
				if (pFile->IsAutoDownPriority())
					uCurPrioMenuItem = MP_PRIOAUTO;
				else if (pFile->GetDownPriority() == PR_HIGH)
					uCurPrioMenuItem = MP_PRIOHIGH;
				else if (pFile->GetDownPriority() == PR_NORMAL)
					uCurPrioMenuItem = MP_PRIONORMAL;
				else if (pFile->GetDownPriority() == PR_LOW)
					uCurPrioMenuItem = MP_PRIOLOW;
				else {
					uCurPrioMenuItem = 0;
					ASSERT(0);
				}
				if (bFirstItem)
					uPrioMenuItem = uCurPrioMenuItem;
				else if (uPrioMenuItem != uCurPrioMenuItem) {
					uPrioMenuItem = 0;
					break;
				}
				bFirstItem = false;
			}
		}
	}
	m_PrioMenu.CheckMenuRadioItem(MP_PRIOLOW, MP_PRIOAUTO, uPrioMenuItem, 0);
	return &m_PrioMenu;
}

BOOL CDownloadListCtrl::OnCommand(WPARAM wParam, LPARAM)
{
	wParam = LOWORD(wParam);

	switch (wParam) {
	case MP_PASTE:
		if (theApp.IsEd2kFileLinkInClipboard())
			theApp.PasteClipboard(m_curTab);
		return TRUE;
	case MP_FIND:
		OnFindStart();
		return TRUE;
	case MP_PAUSE_CATEGORY:
	case MP_RESUME_CATEGORY:
	case MP_STOP_CATEGORY:
		theApp.downloadqueue->SetCatStatus(m_curTab, wParam == MP_PAUSE_CATEGORY ? MP_PAUSE : (wParam == MP_RESUME_CATEGORY ? MP_RESUME : MP_STOP));
		theApp.emuledlg->transferwnd->UpdateCatTabTitles();
		m_availableCommandsDirty = true;
		Invalidate();
		return TRUE;
	case MP_TOGGLEDTOOLBAR:
		thePrefs.SetDownloadToolbar(true);
		theApp.emuledlg->transferwnd->ShowToolbar(true);
		return TRUE;
	}

	int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
	if (iSel < 0)
		iSel = GetNextItem(-1, LVIS_SELECTED);
	if (iSel >= 0) {
		const CtrlItem_Struct *content = reinterpret_cast<CtrlItem_Struct*>(GetItemData(iSel));
		if (content != NULL && content->type == FILE_TYPE) {
			if (!IsLiveFileItem(content)) {
				PruneStaleFileItems();
				return TRUE;
			}
			//for multiple selections
			unsigned selectedCount = 0;
			CTypedPtrList<CPtrList, CPartFile*> selectedList;
			for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
				int index = GetNextSelectedItem(pos);
				const CtrlItem_Struct *pSelectedItem = index > -1 ? reinterpret_cast<CtrlItem_Struct*>(GetItemData(index)) : NULL;
				if (IsLiveFileItem(pSelectedItem)) {
					++selectedCount;
					selectedList.AddTail(static_cast<CPartFile*>(pSelectedItem->value));
				}
			}

			CPartFile *file = static_cast<CPartFile*>(content->value);
			switch (wParam) {
			case MP_CANCEL:
			case MP_CANCEL_NO_CONFIRM:
			case MPG_DELETE: // keyboard del will continue to remove completed files from the screen while cancel will now also be available for complete files
				if (selectedCount > 0) {
					const bool bCancelCommand = wParam == MP_CANCEL || wParam == MP_CANCEL_NO_CONFIRM;
					const bool bConfirmCancel = wParam != MP_CANCEL_NO_CONFIRM;
					SetRedraw(false);
					CString fileList(GetResString(selectedCount == 1 ? IDS_Q_CANCELDL2 : IDS_Q_CANCELDL));
					bool validdelete = false;
					bool removecompl = false;
					int cFiles = 0;
					const int iMaxDisplayFiles = 10;
					for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
						const CPartFile *cur_file = selectedList.GetNext(pos);
						if (cur_file->GetStatus() != PS_COMPLETING && (cur_file->GetStatus() != PS_COMPLETE || bCancelCommand)) {
							validdelete = true;
							if (++cFiles < iMaxDisplayFiles)
								fileList.AppendFormat(_T("\n%s"), (LPCTSTR)cur_file->GetFileName());
							else if (cFiles == iMaxDisplayFiles && pos != NULL)
								fileList += _T("\n...");
						} else if (cur_file->GetStatus() == PS_COMPLETE)
							removecompl = true;
					}

					if ((removecompl && !validdelete)
						|| (validdelete && (!bConfirmCancel || AfxMessageBox(fileList, MB_DEFBUTTON2 | MB_ICONQUESTION | MB_YESNO) == IDYES))) {
						bool bRemovedItems = !selectedList.IsEmpty();
						while (!selectedList.IsEmpty()) {
							CPartFile *partfile = selectedList.RemoveHead();
							HideSources(partfile);
							switch (partfile->GetStatus()) {
							case PS_WAITINGFORHASH:
							case PS_HASHING:
							case PS_COMPLETING:
								break;
							case PS_COMPLETE:
								if (bCancelCommand) {
									bool delsucc = ShellDeleteFile(partfile->GetFilePath());
									if (delsucc)
										theApp.sharedfiles->RemoveFile(partfile, true);
									else {
										CString strError;
										strError.Format(GetResString(IDS_ERR_DELFILE) + _T("\r\n\r\n%s"), (LPCTSTR)partfile->GetFilePath(), (LPCTSTR)GetErrorMessage(::GetLastError()));
										AfxMessageBox(strError);
									}
								}
								RemoveFile(partfile);
								break;
							default:
								if (partfile->GetCategory())
									theApp.downloadqueue->StartNextFileIfPrefs(partfile->GetCategory());
								[[fallthrough]];
							case PS_PAUSED:
								partfile->DeletePartFile();
							}
						}
						if (bRemovedItems) {
							AutoSelectItem();
							theApp.emuledlg->transferwnd->UpdateCatTabTitles();
						}
					}
					SetRedraw(true);
				}
				break;
			case MP_PRIOLOW:
			case MP_PRIONORMAL:
			case MP_PRIOHIGH:
			case MP_PRIOAUTO:
			case MP_PRIOUP:
			case MP_PRIODOWN:
				{
					bool bAuto = (wParam == MP_PRIOAUTO);
					uint8 pr;
					switch (wParam){
					case MP_PRIOLOW:
						pr = PR_LOW;
						break;
					case MP_PRIONORMAL:
						pr = PR_NORMAL;
						break;
					default:
						pr = PR_HIGH;
					}
					SetRedraw(false);
					while (!selectedList.IsEmpty()) {
						CPartFile *partfile = selectedList.RemoveHead();
						partfile->SetAutoDownPriority(bAuto);
						if (wParam == MP_PRIOUP || wParam == MP_PRIODOWN)
							pr = DownloadPriorityShortcutsSeams::StepManualDownloadPriority(partfile->GetDownPriority(), wParam == MP_PRIOUP);
						partfile->SetDownPriority(pr);
					}
					SetRedraw(true);
				}
				break;
			case MP_PAUSE:
				SetRedraw(false);
				while (!selectedList.IsEmpty()) {
					CPartFile *partfile = selectedList.RemoveHead();
					if (partfile->CanPauseFile())
						partfile->PauseFile();
				}
				SetRedraw(true);
				break;
			case MP_RESUME:
				SetRedraw(false);
				while (!selectedList.IsEmpty()) {
					CPartFile *partfile = selectedList.RemoveHead();
					if (partfile->CanResumeFile())
						if (partfile->GetStatus() == PS_INSUFFICIENT)
							partfile->ResumeFileInsufficient();
						else
							partfile->ResumeFile();
				}
				SetRedraw(true);
				break;
			case MP_STOP:
				SetRedraw(false);
				while (!selectedList.IsEmpty()) {
					CPartFile *partfile = selectedList.RemoveHead();
					if (partfile->CanStopFile()) {
						HideSources(partfile);
						partfile->StopFile();
					}
				}
				SetRedraw(true);
				theApp.emuledlg->transferwnd->UpdateCatTabTitles();
				break;
			case MP_CLEARCOMPLETED:
				SetRedraw(false);
				ClearCompleted();
				SetRedraw(true);
				break;
			case MPG_F2:
				if (GetKeyState(VK_CONTROL) < 0 || selectedCount > 1) {
					// when ctrl is pressed -> filename cleanup
					if (IDYES == LocMessageBox(IDS_MANUAL_FILENAMECLEANUP, MB_YESNO, 0))
						while (!selectedList.IsEmpty()) {
							CPartFile *partfile = selectedList.RemoveHead();
							if (partfile->IsPartFile()) {
								HideSources(partfile);
								partfile->DisableFollowMajorityFilenameForManualRename();
								CString strCleanedFileName;
								if (FilenameNormalizationPolicy::TryNormalizeDownloadFilenameCandidate(CleanupFilename(partfile->GetFileName()), strCleanedFileName)) {
									partfile->SetFileName(strCleanedFileName);
									partfile->SavePartFile();
								}
							}
						}
				} else {
					if (file->GetStatus() != PS_COMPLETE && file->GetStatus() != PS_COMPLETING) {
						InputBox inputbox;
						inputbox.SetLabels(GetResNoAmp(IDS_RENAME), GetResString(IDS_DL_FILENAME), file->GetFileName());
						inputbox.SetEditFilenameMode();
						if (inputbox.DoModal() == IDOK && !inputbox.GetInput().IsEmpty() && IsValidEd2kString(inputbox.GetInput())) {
							HideSources(file);
							file->DisableFollowMajorityFilenameForManualRename();
							file->SetFileName(inputbox.GetInput(), true);
							file->UpdateDisplayedInfo();
							file->SavePartFile();
						}
					} else
						MessageBeep(MB_OK);
				}
				break;
			case MP_METINFO:
			case MPG_ALTENTER:
				ShowFileDialog(0);
				break;
			case MP_COPYSELECTED:
			case MP_GETED2KLINK:
			case MP_GETHTMLED2KLINK:
				{
					CString str;
					while (!selectedList.IsEmpty()) {
						const CAbstractFile *af = static_cast<CAbstractFile*>(selectedList.RemoveHead());
						if (af) {
							if (!str.IsEmpty())
								str += (wParam == MP_GETHTMLED2KLINK) ? _T("<br>\r\n") : _T("\r\n");
							str += af->GetED2kLink(false, wParam == MP_GETHTMLED2KLINK);
						}
					}
					theApp.CopyTextToClipboard(str);
				}
				break;
			case MP_COPY_FILE_NAME:
			case MP_COPY_FILE_HASH:
			case MP_COPY_FILE_SIZE:
			case MP_COPY_FILE_STATUS:
			case MP_COPY_FILE_PROGRESS:
			case MP_COPY_FILE_PATH:
			case MP_COPY_FOLDER_PATH:
			case MP_COPY_FILE_SUMMARY:
				{
					std::vector<CString> lines;
					for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
						const CPartFile *partfile = selectedList.GetNext(pos);
						if (partfile == NULL)
							continue;
						CString size;
						CString progress;
						switch (wParam) {
						case MP_COPY_FILE_NAME:
							lines.push_back(partfile->GetFileName());
							break;
						case MP_COPY_FILE_HASH:
							lines.push_back(md4str(partfile->GetFileHash()));
							break;
						case MP_COPY_FILE_SIZE:
							lines.push_back(ProUserMenuCopySeams::FormatUInt64(static_cast<uint64>(partfile->GetFileSize())));
							break;
						case MP_COPY_FILE_STATUS:
							lines.push_back(partfile->getPartfileStatus());
							break;
						case MP_COPY_FILE_PROGRESS:
							lines.push_back(ProUserMenuCopySeams::FormatPercent(partfile->GetPercentCompleted()));
							break;
						case MP_COPY_FILE_PATH:
							lines.push_back(partfile->GetFullName());
							break;
						case MP_COPY_FOLDER_PATH:
							lines.push_back(partfile->GetPath());
							break;
						case MP_COPY_FILE_SUMMARY:
							size = ProUserMenuCopySeams::FormatUInt64(static_cast<uint64>(partfile->GetFileSize()));
							progress = ProUserMenuCopySeams::FormatPercent(partfile->GetPercentCompleted());
							lines.push_back(ProUserMenuCopySeams::FormatFileSummary(
								partfile->GetFileName(),
								md4str(partfile->GetFileHash()),
								size,
								partfile->getPartfileStatus(),
								progress,
								partfile->GetFullName(),
								partfile->GetED2kLink()));
							break;
						}
					}
					const CString text = ProUserMenuCopySeams::JoinLines(lines);
					if (!text.IsEmpty())
						theApp.CopyTextToClipboard(text);
				}
				break;
			case MP_SEARCHRELATED:
				theApp.emuledlg->searchwnd->SearchRelatedFiles(selectedList);
				theApp.emuledlg->SetActiveDialog(theApp.emuledlg->searchwnd);
				break;
			case MP_OPEN:
			case IDA_ENTER:
				if (selectedCount == 1 && file->CanOpenFile())
					file->OpenFile();
				break;
			case MP_OPENFOLDER:
				if (selectedCount == 1)
					ShellOpenFile(file->GetPath());
				break;
			case MP_TRY_TO_GET_PREVIEW_PARTS:
				if (selectedCount == 1)
					file->SetPreviewPrio(!file->GetPreviewPrio());
				break;
			case MP_PREVIEW:
				if (selectedCount == 1)
					file->PreviewFile();
				break;
			case MP_PAUSEONPREVIEW:
				{
					bool bAllPausedOnPreview = true;
					for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL && bAllPausedOnPreview;)
						bAllPausedOnPreview = static_cast<CPartFile*>(selectedList.GetNext(pos))->IsPausingOnPreview();
					while (!selectedList.IsEmpty()) {
						CPartFile *pPartFile = selectedList.RemoveHead();
						if (pPartFile->IsPreviewableFileType() && !pPartFile->IsReadyForPreview())
							pPartFile->SetPauseOnPreview(!bAllPausedOnPreview);
					}
				}
				break;
			case MP_VIEWFILECOMMENTS:
				ShowFileDialog(IDD_COMMENTLST);
				break;
			case MP_FOLLOWMAJORITYFILENAME:
				{
					bool bEnable = false;
					for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
						const CPartFile *partfile = selectedList.GetNext(pos);
						if (partfile->GetStatus() != PS_COMPLETE && partfile->GetStatus() != PS_COMPLETING && !partfile->IsFollowMajorityFilenameEnabled()) {
							bEnable = true;
							break;
						}
					}
					while (!selectedList.IsEmpty()) {
						CPartFile *partfile = selectedList.RemoveHead();
						if (partfile->GetStatus() != PS_COMPLETE && partfile->GetStatus() != PS_COMPLETING) {
							partfile->SetFollowMajorityFilename(bEnable);
							partfile->SavePartFile();
							partfile->UpdateDisplayedInfo();
						}
					}
				}
				break;
			case MP_SHOWED2KLINK:
				ShowFileDialog(IDD_ED2KLINK);
				break;
			case MP_SETSOURCELIMIT:
				{
					CString temp;
					temp.Format(_T("%u"), file->GetPrivateMaxSources());
					InputBox inputbox;
					const CString &title(GetResString(IDS_SETPFSLIMIT));
					inputbox.SetLabels(title, GetResString(IDS_SETPFSLIMITEXPLAINED), temp);

					if (inputbox.DoModal() == IDOK) {
						int newlimit = _tstoi(inputbox.GetInput());
						while (!selectedList.IsEmpty()) {
							CPartFile *partfile = selectedList.RemoveHead();
							partfile->SetPrivateMaxSources(newlimit);
							partfile->UpdateDisplayedInfo(true);
						}
					}
				}
				break;
			case MP_ADDSOURCE:
				if (selectedCount == 1) {
					CAddSourceDlg as;
					as.SetFile(file);
					as.DoModal();
				}
				break;
			default:
				if (wParam >= MP_WEBURL && wParam <= MP_WEBURL + 99)
					theWebServices.RunURL(file, (UINT)wParam);
				else if ((wParam >= MP_ASSIGNCAT && wParam <= MP_ASSIGNCAT + 99) || wParam == MP_NEWCAT) {
					int nCatNumber;
					if (wParam == MP_NEWCAT) {
						nCatNumber = theApp.emuledlg->transferwnd->AddCategoryInteractive();
						if (nCatNumber == 0) // Creation canceled
							break;
					} else
						nCatNumber = (int)(wParam - MP_ASSIGNCAT);
					SetRedraw(false);
					while (!selectedList.IsEmpty()) {
						CPartFile *partfile = selectedList.RemoveHead();
						partfile->SetCategory(nCatNumber);
						partfile->UpdateDisplayedInfo(true);
					}
					SetRedraw(true);
					UpdateCurrentCategoryView();
					if (thePrefs.ShowCatTabInfos())
						theApp.emuledlg->transferwnd->UpdateCatTabTitles();
				} else if (wParam >= MP_PREVIEW_APP_MIN && wParam <= MP_PREVIEW_APP_MAX)
					thePreviewApps.RunApp(file, (UINT)wParam);
			}
		} else if (content != NULL && IsLiveSourceItem(content)) {
			CUpDownClient *client = static_cast<CUpDownClient*>(content->value);

			switch (wParam) {
			case MP_SHOWLIST:
				client->RequestSharedFileList();
				break;
			case MP_MESSAGE:
				theApp.emuledlg->chatwnd->StartSession(client);
				break;
			case MP_ADDFRIEND:
				if (theApp.friendlist->AddFriend(client))
					UpdateItem(client);
				break;
			case MP_BAN:
				if (!client->IsBanned()) {
					client->Ban(_T("Manual download-source ban"));
					UpdateItem(client);
				}
				break;
			case MP_UNBAN:
				if (client->IsBanned()) {
					client->UnBan();
					UpdateItem(client);
				}
				break;
			case MP_COPY_CLIENT_IP:
			case MP_COPY_CLIENT_USERHASH:
			case MP_COPY_CLIENT_SUMMARY:
			case MP_COPY_FILE_NAME:
			case MP_COPY_FILE_HASH:
				{
					CString text;
					if (wParam == MP_COPY_CLIENT_IP) {
						text = ipstr(GetClientGeoIP(client));
					} else if (wParam == MP_COPY_CLIENT_USERHASH) {
						text = md4str(client->GetUserHash());
					} else if (wParam == MP_COPY_FILE_NAME && client->GetRequestFile() != NULL) {
						text = client->GetRequestFile()->GetFileName();
					} else if (wParam == MP_COPY_FILE_HASH && client->GetRequestFile() != NULL) {
						text = md4str(client->GetRequestFile()->GetFileHash());
					} else if (wParam == MP_COPY_CLIENT_SUMMARY) {
						std::vector<ProUserMenuCopySeams::NamedField> fields;
						ProUserMenuCopySeams::AppendField(fields, _T("username"), client->GetUserName());
						ProUserMenuCopySeams::AppendField(fields, _T("client"), client->GetClientSoftVer());
						ProUserMenuCopySeams::AppendField(fields, _T("ip"), ipstr(GetClientGeoIP(client)));
						ProUserMenuCopySeams::AppendField(fields, _T("userhash"), md4str(client->GetUserHash()));
						ProUserMenuCopySeams::AppendField(fields, _T("download_state"), client->GetDownloadStateDisplayString());
						ProUserMenuCopySeams::AppendField(fields, _T("upload_state"), client->GetUploadStateDisplayString());
						if (client->GetRequestFile() != NULL) {
							ProUserMenuCopySeams::AppendField(fields, _T("file"), client->GetRequestFile()->GetFileName());
							ProUserMenuCopySeams::AppendField(fields, _T("filehash"), md4str(client->GetRequestFile()->GetFileHash()));
						}
						text = ProUserMenuCopySeams::FormatSummary(fields);
					}
					if (!text.IsEmpty())
						theApp.CopyTextToClipboard(text);
				}
				break;
			case MP_DETAIL:
			case MPG_ALTENTER:
				ShowClientDialog(client);
				break;
			case MP_BOOT:
				if (client->GetKadPort() && client->GetKadVersion() >= KADEMLIA_VERSION2_47a)
					Kademlia::CKademlia::Bootstrap(ntohl(client->GetIP()), client->GetKadPort());
// ZZ:DownloadManager -->
#ifdef _DEBUG
				break;
			case MP_A4AF_CHECK_THIS_NOW:
				{
					CPartFile *file = static_cast<CPartFile*>(content->owner);
					if (file->GetStatus(false) == PS_READY || file->GetStatus(false) == PS_EMPTY) {
						if (client->GetDownloadState() != DS_DOWNLOADING) {
							client->SwapToAnotherFile(_T("Manual init of source check. Test to be like ProcessA4AFClients(). CDownloadListCtrl::OnCommand() MP_SWAP_A4AF_DEBUG_THIS"), false, false, false, NULL, true, true, true); // ZZ:DownloadManager
							UpdateItem(file);
						}
					}
				}
#endif
// <-- ZZ:DownloadManager
			}
		}
	} else if (wParam == MP_CLEARCOMPLETED) //nothing selected
		ClearCompleted();

	m_availableCommandsDirty = true;
	return TRUE;
}

void CDownloadListCtrl::OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult)
{
	const LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	bool sortAscending;
	if (GetSortItem() != pNMLV->iSubItem)
		switch (pNMLV->iSubItem) {
		case 2: // Transferred
		case 3: // Completed
		case 4: // Download rate
		case 5: // Progress
		case 16: // Percentage
		case 6: // Sources / Client Software
			sortAscending = false;
			break;
		case 10:
			// Keep the current 'm_bRemainSort' for that column, but reset to 'ascending'
		default:
			sortAscending = true;
		}
	else
		sortAscending = !GetSortAscending();

	// Ornis 4-way-sorting
	int adder = 0;
	if (pNMLV->iSubItem == 10) {
		if (GetSortItem() == 10 && sortAscending) // check for 'ascending' because the initial sort order is also 'ascending'
			m_bRemainSort = !m_bRemainSort;
		if (m_bRemainSort)
			adder = 81;
	}
	// Sort table
	if (adder == 0)
		SetSortArrow(pNMLV->iSubItem, sortAscending);
	else
		SetSortArrow(pNMLV->iSubItem, sortAscending ? arrowDoubleUp : arrowDoubleDown);
	UpdateSortHistory(MAKELONG(pNMLV->iSubItem + adder, !sortAscending));
	PruneStaleFileItems();
	SortItems(SortProc, MAKELONG(pNMLV->iSubItem + adder, !sortAscending));
	// Save new preferences
	thePrefs.TransferlistRemainSortStyle(m_bRemainSort);
	*pResult = 0;
}

int CALLBACK CDownloadListCtrl::SortProc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	const CtrlItem_Struct *item1 = reinterpret_cast<CtrlItem_Struct*>(lParam1);
	const CtrlItem_Struct *item2 = reinterpret_cast<CtrlItem_Struct*>(lParam2);
	const bool bLiveItem1 = item1 != NULL && (item1->type == FILE_TYPE ? IsLiveFileCtrlItem(item1) : (!IsSourceCtrlItem(item1) || IsLiveSourceCtrlItem(item1)));
	const bool bLiveItem2 = item2 != NULL && (item2->type == FILE_TYPE ? IsLiveFileCtrlItem(item2) : (!IsSourceCtrlItem(item2) || IsLiveSourceCtrlItem(item2)));
	if (!bLiveItem1 || !bLiveItem2)
		return bLiveItem1 ? -1 : (bLiveItem2 ? 1 : 0);

	int iResult;
	if (item1->type == FILE_TYPE && item2->type != FILE_TYPE) {
		if (item1->value == item2->owner)
			return -1;
		iResult = Compare(static_cast<CPartFile*>(item1->value), item2->owner, lParamSort);
	} else if (item1->type != FILE_TYPE && item2->type == FILE_TYPE) {
		if (item1->owner == item2->value)
			return 1;
		iResult = Compare(item1->owner, static_cast<CPartFile*>(item2->value), lParamSort);
	} else if (item1->type == FILE_TYPE)
		iResult = Compare(static_cast<CPartFile*>(item1->value), static_cast<CPartFile*>(item2->value), lParamSort);
	else {
		if (item1->owner != item2->owner) {
			iResult = Compare(item1->owner, item2->owner, lParamSort);
			return HIWORD(lParamSort) ? -iResult : iResult;
		}
		if (item1->type != item2->type)
			return item1->type - item2->type;

		iResult = Compare(static_cast<CUpDownClient*>(item1->value), static_cast<CUpDownClient*>(item2->value), lParamSort);
	}

	//call secondary sort order, if the first one resulted as equal
	if (iResult == 0) {
		LPARAM iNextSort = theApp.emuledlg->transferwnd->GetDownloadList()->GetNextSortOrder(lParamSort);
		if (iNextSort != -1)
			return SortProc(lParam1, lParam2, iNextSort);
	}

	return HIWORD(lParamSort) ? -iResult : iResult;
}

void CDownloadListCtrl::ClearCompleted(int incat)
{
	if (incat == -2)
		incat = m_curTab;

	// Search for completed file(s)
	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end();) {
		const CtrlItem_Struct *cur_item = it->second;
		++it; // Already point to the next iterator.
		if (IsLiveFileItem(cur_item)) {
			CPartFile *file = static_cast<CPartFile*>(cur_item->value);
			if (!file->IsPartFile() && (file->CheckShowItemInGivenCat(incat) || incat == -1))
				if (RemoveFile(file))
					it = m_ListItems.begin();
		}
	}
	if (thePrefs.ShowCatTabInfos())
		theApp.emuledlg->transferwnd->UpdateCatTabTitles();
}

void CDownloadListCtrl::ClearCompleted(const CPartFile *pFile)
{
	if (!pFile->IsPartFile())
		for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
			const CtrlItem_Struct *cur_item = it->second;
			if (IsLiveFileItem(cur_item)) {
				const CPartFile *pCurFile = static_cast<CPartFile*>(cur_item->value);
				if (pCurFile == pFile) {
					RemoveFile(pCurFile);
					return;
				}
			}
		}
}

void CDownloadListCtrl::SetStyle()
{
	if (thePrefs.IsDoubleClickEnabled())
		SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	else
		SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_ONECLICKACTIVATE);
}

void CDownloadListCtrl::OnListModified(LPNMHDR pNMHDR, LRESULT* /*pResult*/)
{
	NMLISTVIEW *pNMListView = reinterpret_cast<NMLISTVIEW*>(pNMHDR);

	//this works because true is equal to 1 and false equal to 0
	int notLast = static_cast<int>(pNMListView->iItem + 1 != GetItemCount());
	int notFirst = static_cast<int>(pNMListView->iItem != 0);
	RedrawItems(pNMListView->iItem - notFirst, pNMListView->iItem + notLast);
	m_availableCommandsDirty = true;
}

int CDownloadListCtrl::Compare(const CPartFile *file1, const CPartFile *file2, LPARAM lParamSort)
{
	switch (LOWORD(lParamSort)) {
	case 0: //filename
		return CompareLocaleStringNoCase(file1->GetFileName(), file2->GetFileName());
	case 1: //size
		return CompareUnsigned(file1->GetFileSize(), file2->GetFileSize());
	case 2: //transferred
		return CompareUnsigned(file1->GetTransferred(), file2->GetTransferred());
	case 3: //completed
		return CompareUnsigned(file1->GetCompletedSize(), file2->GetCompletedSize());
	case 4: //speed
		return CompareUnsigned(file1->GetDatarate(), file2->GetDatarate());
	case 5: //progress
		return sgn((float)file1->GetCompletedSize() / (float)file1->GetFileSize() - (float)file2->GetCompletedSize() / (float)file2->GetFileSize()); //compare exact ratio instead of rounded percents
	case 6: //sources
		return CompareUnsigned(file1->GetSourceCount(), file2->GetSourceCount());
	case 7: //priority
		return CompareUnsigned(file1->GetDownPriority(), file2->GetDownPriority());
	case 8: //Status
		return file1->getPartfileStatusRank() - file2->getPartfileStatusRank();
	case DOWNLOAD_COLUMN_TRUST:
		return SearchTrustHintSeams::CompareTrustHints(
			BuildDownloadTrustHint(*const_cast<CPartFile*>(file1)),
			BuildDownloadTrustHint(*const_cast<CPartFile*>(file2)));
	case 10: //Remaining Time
		{
			//Make ascending sort so we can have the smaller remaining time on the top
			//instead of unknowns so we can see which files are about to finish better.
			time_t f1 = file1->getTimeRemaining();
			time_t f2 = file2->getTimeRemaining();
			//Same, do nothing.
			if (f1 == f2)
				break;

			//If descending, put first on top as it is unknown
			//If ascending, put first on bottom as it is unknown
			if (f1 == -1)
				return 1;

			//If descending, put second on top as it is unknown
			//If ascending, put second on bottom as it is unknown
			if (f2 == -1)
				return -1;

			//If descending, put first on top as it is bigger.
			//If ascending, put first on bottom as it is bigger.
			return CompareUnsigned(f1, f2);
		}

	case 90: //Remaining SIZE
		return CompareUnsigned(file1->GetFileSize() - file1->GetCompletedSize(), file2->GetFileSize() - file2->GetCompletedSize());
	case 11: //last seen complete
		return sgn(file1->lastseencomplete - file2->lastseencomplete);
	case 12: //last received Time
		return sgn(file1->GetLastReceptionDate() - file2->GetLastReceptionDate());
	case 13: //category
		return CompareLocaleStringNoCase(
					(file1->GetCategory() != 0) ? thePrefs.GetCategory(file1->GetCategory())->strTitle : GetResString(IDS_ALL)
				  , (file2->GetCategory() != 0) ? thePrefs.GetCategory(file2->GetCategory())->strTitle : GetResString(IDS_ALL));
	case 14: // added on
		return sgn(file1->GetCrFileDate() - file2->GetCrFileDate());
	case 15: // geo location
		return 0;
	case 16: // percentage
		return sgn(file1->GetPercentCompleted() - file2->GetPercentCompleted());
	}
	return 0;
}

int CDownloadListCtrl::Compare(const CUpDownClient *client1, const CUpDownClient *client2, LPARAM lParamSort)
{
	switch (LOWORD(lParamSort)) {
	case 0: //name
		if (client1->GetUserName() && client2->GetUserName())
			return CompareLocaleStringNoCase(client1->GetUserName(), client2->GetUserName());
		if (client1->GetUserName() == NULL)
			return 1; // place clients with no user names at bottom
		if (client2->GetUserName() == NULL)
			return -1; // place clients with no user names at bottom
		return 0;
	case 1: //size but we use status
		return client1->GetSourceFrom() - client2->GetSourceFrom();
	case 2: //transferred
	case 3: //completed
		return CompareUnsigned(client1->GetTransferredDown(), client2->GetTransferredDown());
	case 4: //speed
		return CompareUnsigned(client1->GetDownloadDatarate(), client2->GetDownloadDatarate());
	case 5: //progress
		return CompareUnsigned(client1->GetAvailablePartCount(), client2->GetAvailablePartCount());
	case 6:
		if (client1->GetClientSoft() == client2->GetClientSoft())
			return client1->GetVersion() - client2->GetVersion();
		return -(client1->GetClientSoft() - client2->GetClientSoft()); // invert result to place eMule's at top
	case 7: //qr
		if (client1->GetDownloadState() == DS_DOWNLOADING)
			return (client2->GetDownloadState() == DS_DOWNLOADING) ? 0 : -1;
		if (client2->GetDownloadState() == DS_DOWNLOADING)
			return 1;
		if (client1->GetRemoteQueueRank() == 0 && client1->GetDownloadState() == DS_ONQUEUE && client1->IsRemoteQueueFull())
			return 1;
		if (client2->GetRemoteQueueRank() == 0 && client2->GetDownloadState() == DS_ONQUEUE && client2->IsRemoteQueueFull())
			return -1;
		if (client1->GetRemoteQueueRank() == 0)
			return 1;
		if (client2->GetRemoteQueueRank() == 0)
			return -1;
		return CompareUnsigned(client1->GetRemoteQueueRank(), client2->GetRemoteQueueRank());
	case 8: //state
		if (client1->GetDownloadState() == client2->GetDownloadState()) {
			if (client1->IsRemoteQueueFull() && client2->IsRemoteQueueFull())
				return 0;
			if (client1->IsRemoteQueueFull())
				return 1;
			if (client2->IsRemoteQueueFull())
				return -1;
		}
		return client1->GetDownloadState() - client2->GetDownloadState();
	case 15: // geo location
		if (theApp.geolocation != NULL)
			return CompareLocaleStringNoCase(theApp.geolocation->GetDisplayText(GetClientGeoIP(client1)), theApp.geolocation->GetDisplayText(GetClientGeoIP(client2)));
		return 0;
	case 17: // IP
		return CompareLocaleStringNoCase(ipstr(GetClientGeoIP(client1)), ipstr(GetClientGeoIP(client2)));
	case 18: // LowID
		return CompareUnsigned(client1->HasLowID(), client2->HasLowID());
	}
	return 0;
}

void CDownloadListCtrl::OnNmDblClk(LPNMHDR, LRESULT *pResult)
{
	int iSel = GetSelectionMark();
	if (iSel >= 0) {
		const CtrlItem_Struct *content = reinterpret_cast<CtrlItem_Struct*>(GetItemData(iSel));
		if (IsSourceCtrlItem(content) && !IsLiveSourceItem(content)) {
			PruneStaleSourceItems();
			*pResult = 0;
			return;
		}
		if (content && content->value) {
			if (content->type == FILE_TYPE) {
				if (!IsLiveFileItem(content)) {
					PruneStaleFileItems();
					*pResult = 0;
					return;
				}
				CPoint pt;
				if (!thePrefs.IsDoubleClickEnabled() && ::GetCursorPos(&pt)) {
					ScreenToClient(&pt);
					LVHITTESTINFO hit;
					hit.pt = pt;
					if (HitTest(&hit) >= 0 && (hit.flags & LVHT_ONITEM)) {
						LVHITTESTINFO subhit;
						subhit.pt = pt;
						if (SubItemHitTest(&subhit) >= 0 && subhit.iSubItem == 0) {
							CPartFile *file = static_cast<CPartFile*>(content->value);
							const LONG iconcx = theApp.GetSmallSytemIconSize().cx;
							if (thePrefs.ShowRatingIndicator()
								&& (file->HasComment() || file->HasRating() || file->IsKadCommentSearchRunning())
								&& pt.x >= sm_iIconOffset + iconcx
								&& pt.x <= sm_iIconOffset + iconcx + RATING_ICON_WIDTH)
							{
								ShowFileDialog(IDD_COMMENTLST);
							} else if (thePrefs.GetPreviewOnIconDblClk()
										&& pt.x >= sm_iIconOffset
										&& pt.x < sm_iIconOffset + iconcx)
							{
								if (file->IsReadyForPreview())
									file->PreviewFile();
								else
									MessageBeep(MB_OK);
							} else
								ShowFileDialog(0);
						}
					}
				}
			} else if (IsLiveSourceItem(content))
				ShowClientDialog(static_cast<CUpDownClient*>(content->value));
		}
	}

	*pResult = 0;
}

void CDownloadListCtrl::CreateMenus()
{
	if (m_PreviewMenu)
		VERIFY(m_PreviewMenu.DestroyMenu());
	if (m_PrioMenu)
		VERIFY(m_PrioMenu.DestroyMenu());
	if (m_SourcesMenu)
		VERIFY(m_SourcesMenu.DestroyMenu());
	if (m_BatchMenu)
		VERIFY(m_BatchMenu.DestroyMenu());
	if (m_FileMenu)
		VERIFY(m_FileMenu.DestroyMenu());

	m_FileMenu.CreatePopupMenu();
	m_FileMenu.AddMenuTitle(GetResString(IDS_DOWNLOADMENUTITLE), true);

	// Add 'Download Priority' sub menu
	//
	m_PrioMenu.CreateMenu();
	m_PrioMenu.AddMenuTitle(NULL, true);
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOUP, AddMenuShortcutLabel(GetResString(IDS_PRIO_INCREASE), _T("Ctrl++")));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIODOWN, AddMenuShortcutLabel(GetResString(IDS_PRIO_DECREASE), _T("Ctrl+-")));
	m_PrioMenu.AppendMenu(MF_SEPARATOR);
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOLOW, AddMenuShortcutLabel(GetResString(IDS_PRIOLOW), _T("Ctrl+Shift+-")));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIONORMAL, GetResString(IDS_PRIONORMAL));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOHIGH, AddMenuShortcutLabel(GetResString(IDS_PRIOHIGH), _T("Ctrl+Shift++")));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOAUTO, GetResString(IDS_PRIOAUTO));

	CString sPrio;
	sPrio.Format(_T("%s (%s)"), (LPCTSTR)GetResString(IDS_PRIORITY), (LPCTSTR)GetResString(IDS_DOWNLOAD));
	m_FileMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_PrioMenu.m_hMenu, sPrio, _T("FILEPRIORITY"));

	// Add file commands
	//
	m_FileMenu.AppendMenu(MF_STRING, MP_PAUSE, AddMenuShortcutLabel(GetResString(IDS_DL_PAUSE), _T("Ctrl+P")), _T("PAUSE"));
	m_FileMenu.AppendMenu(MF_STRING, MP_STOP, AddMenuShortcutLabel(GetResString(IDS_DL_STOP), _T("Ctrl+T")), _T("STOP"));
	m_FileMenu.AppendMenu(MF_STRING, MP_RESUME, AddMenuShortcutLabel(GetResString(IDS_DL_RESUME), _T("Ctrl+S")), _T("RESUME"));
	m_FileMenu.AppendMenu(MF_STRING, MP_CANCEL, AddMenuShortcutLabel(GetResString(IDS_MAIN_BTN_CANCEL), _T("Delete")), _T("DELETE"));
	m_BatchMenu.CreateMenu();
	m_BatchMenu.AddMenuTitle(NULL, true);
	m_BatchMenu.AppendMenu(MF_STRING, MP_PAUSE_CATEGORY, AddMenuShortcutLabel(GetResString(IDS_DL_PAUSE_CATEGORY), _T("Ctrl+Shift+P")));
	m_BatchMenu.AppendMenu(MF_STRING, MP_STOP_CATEGORY, AddMenuShortcutLabel(GetResString(IDS_DL_STOP_CATEGORY), _T("Ctrl+Shift+T")));
	m_BatchMenu.AppendMenu(MF_STRING, MP_RESUME_CATEGORY, AddMenuShortcutLabel(GetResString(IDS_DL_RESUME_CATEGORY), _T("Ctrl+Shift+S")));
	m_FileMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_BatchMenu.m_hMenu, GetResString(IDS_DL_BATCH));
	m_FileMenu.AppendMenu(MF_SEPARATOR);

	m_FileMenu.AppendMenu(MF_STRING, MP_OPEN, AddMenuShortcutLabel(GetResString(IDS_DL_OPEN), _T("Ctrl+O")), _T("OPENFILE"));
	m_FileMenu.AppendMenu(MF_STRING, MP_OPENFOLDER, AddMenuShortcutLabel(GetResString(IDS_OPENFOLDER), _T("Ctrl+Shift+O")), _T("OPENFOLDER"));
	// Extended: Submenu with Preview options, Normal: Preview and possibly 'Preview with' item
	m_PreviewMenu.CreateMenu();
	m_PreviewMenu.AddMenuTitle(NULL, true);
	m_PreviewMenu.AppendMenu(MF_STRING, MP_PREVIEW, GetResString(IDS_DL_PREVIEW), _T("PREVIEW"));
	m_PreviewMenu.AppendMenu(MF_STRING, MP_PAUSEONPREVIEW, GetResString(IDS_PAUSEONPREVIEW));
	if (!thePrefs.GetPreviewPrio())
		m_PreviewMenu.AppendMenu(MF_STRING, MP_TRY_TO_GET_PREVIEW_PARTS, GetResString(IDS_DL_TRY_TO_GET_PREVIEW_PARTS));
	m_FileMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_PreviewMenu.m_hMenu, GetResString(IDS_DL_PREVIEW), _T("PREVIEW"));

	m_FileMenu.AppendMenu(MF_STRING, MP_METINFO, AddMenuShortcutLabel(GetResString(IDS_DL_INFO), _T("Ctrl+I")), _T("FILEINFO"));
	m_FileMenu.AppendMenu(MF_STRING, MP_VIEWFILECOMMENTS, GetResString(IDS_CMT_SHOWALL), _T("FILECOMMENTS"));
	m_FileMenu.AppendMenu(MF_STRING, MP_FOLLOWMAJORITYFILENAME, GetResString(IDS_FOLLOW_MAJORITY_FILENAME), _T("FILERENAME"));
	m_FileMenu.AppendMenu(MF_SEPARATOR);

	m_FileMenu.AppendMenu(MF_STRING, MP_CLEARCOMPLETED, GetResString(IDS_DL_CLEAR), _T("CLEARCOMPLETE"));

	// Add (extended user mode) 'Source Handling' sub menu
	//
	m_SourcesMenu.CreateMenu();
	m_SourcesMenu.AppendMenu(MF_STRING, MP_ADDSOURCE, GetResString(IDS_ADDSRCMANUALLY));
	m_SourcesMenu.AppendMenu(MF_STRING, MP_SETSOURCELIMIT, GetResString(IDS_SETPFSLIMIT));
	m_FileMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_SourcesMenu.m_hMenu, GetResString(IDS_A4AF));
	m_FileMenu.AppendMenu(MF_SEPARATOR);

	// Add 'Copy & Paste' commands
	//
	if (thePrefs.GetShowCopyEd2kLinkCmd())
		m_FileMenu.AppendMenu(MF_STRING, MP_GETED2KLINK, AddMenuShortcutLabel(GetResString(IDS_DL_LINK1), _T("Ctrl+L")), _T("ED2KLINK"));
	else
		m_FileMenu.AppendMenu(MF_STRING, MP_SHOWED2KLINK, GetResString(IDS_DL_SHOWED2KLINK), _T("ED2KLINK"));
	m_FileMenu.AppendMenu(MF_STRING, MP_PASTE, GetResString(IDS_SW_DIRECTDOWNLOAD), _T("PASTELINK"));
	m_FileMenu.AppendMenu(MF_SEPARATOR);

	// Search commands
	//
	m_FileMenu.AppendMenu(MF_STRING, MP_FIND, AddMenuShortcutLabel(GetResString(IDS_FIND), _T("Ctrl+F")), _T("Search"));
	m_FileMenu.AppendMenu(MF_STRING, MP_SEARCHRELATED, GetResString(IDS_SEARCHRELATED), _T("KadFileSearch"));
	// Web-services and categories will be added on-the-fly.
}

CString CDownloadListCtrl::getTextList()
{
	CString out;

	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
		const CtrlItem_Struct *cur_item = it->second;
		if (IsLiveFileItem(cur_item)) {
			const CPartFile *file = static_cast<CPartFile*>(cur_item->value);
			out.AppendFormat(_T("\n%s\t [%.1f%%] %u/%u - %s")
				, (LPCTSTR)file->GetFileName()
				, file->GetPercentCompleted()
				, file->GetTransferringSrcCount()
				, file->GetSourceCount()
				, (LPCTSTR)file->getPartfileStatus());
		}
	}
	return out;
}

float CDownloadListCtrl::GetFinishedSize()
{
	float fsize = 0;

	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
		const CtrlItem_Struct *cur_item = it->second;
		if (IsLiveFileItem(cur_item)) {
			const CPartFile *file = static_cast<CPartFile*>(cur_item->value);
			if (file->GetStatus() == PS_COMPLETE)
				fsize += static_cast<float>(static_cast<uint64>(file->GetFileSize()));
		}
	}
	return fsize;
}

int CDownloadListCtrl::GetFilesCountInCurCat()
{
	int iCount = 0;
	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
		const CtrlItem_Struct *cur_item = it->second;
		if (IsLiveFileItem(cur_item)) {
			CPartFile *file = static_cast<CPartFile*>(cur_item->value);
			iCount += static_cast<int>(file->CheckShowItemInGivenCat(m_curTab));
		}
	}
	return iCount;
}

void CDownloadListCtrl::CountBatchCommandsInCurCat(int &riFilesToPause, int &riFilesToResume, int &riFilesToStop) const
{
	riFilesToPause = 0;
	riFilesToResume = 0;
	riFilesToStop = 0;

	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
		const CtrlItem_Struct *cur_item = it->second;
		if (IsLiveFileItem(cur_item)) {
			CPartFile *file = static_cast<CPartFile*>(cur_item->value);
			if (file->CheckShowItemInGivenCat(m_curTab)) {
				riFilesToPause += static_cast<int>(file->CanPauseFile());
				riFilesToResume += static_cast<int>(file->CanResumeFile());
				riFilesToStop += static_cast<int>(file->CanStopFile());
			}
		}
	}
}

CString CDownloadListCtrl::GetFileItemDisplayText(const CPartFile *lpPartFile, int iSubItem)
{
	CString sText;
	switch (iSubItem) {
	case 0: //file name
		sText = lpPartFile->GetFileName();
		break;
	case 1: //size
		sText = CastItoXBytes(lpPartFile->GetFileSize());
		break;
	case 2: //transferred
		sText = CastItoXBytes(lpPartFile->GetTransferred());
		break;
	case 3: //transferred complete
		sText = CastItoXBytes(lpPartFile->GetCompletedSize());
		break;
	case 4: //speed
		if (lpPartFile->GetTransferringSrcCount())
			sText = CastItoXBytes(lpPartFile->GetDatarate(), false, true);
		break;
	case 5: //progress
		sText.Format(_T("%s: %.1f%%"), (LPCTSTR)GetResString(IDS_DL_PROGRESS), lpPartFile->GetPercentCompleted());
		break;
	case 6: //sources
		{
// ZZ:DownloadManager -->
			const UINT sc = lpPartFile->GetSourceCount();
			if ((lpPartFile->GetStatus() != PS_PAUSED || sc) && lpPartFile->GetStatus() != PS_COMPLETE) {
				UINT ncsc = lpPartFile->GetNotCurrentSourcesCount();
				sText.Format(_T("%u"), sc - ncsc);
				if (ncsc > 0)
					sText.AppendFormat(_T("/%u"), sc);
				if (lpPartFile->GetSrcA4AFCount() > 0)
					sText.AppendFormat(_T("+%u"), lpPartFile->GetSrcA4AFCount());
				if (lpPartFile->GetTransferringSrcCount() > 0)
					sText.AppendFormat(_T(" (%u)"), lpPartFile->GetTransferringSrcCount());
			}
// <-- ZZ:DownloadManager
			if (lpPartFile->GetPrivateMaxSources() > 0)
				sText.AppendFormat(_T(" [%u]"), lpPartFile->GetPrivateMaxSources());
		}
		break;
	case 7: //prio
		{
			UINT uid;
			switch (lpPartFile->GetDownPriority()) {
			case PR_LOW:
				uid = lpPartFile->IsAutoDownPriority() ? IDS_PRIOAUTOLOW : IDS_PRIOLOW;
				break;
			case PR_NORMAL:
				uid = lpPartFile->IsAutoDownPriority() ? IDS_PRIOAUTONORMAL : IDS_PRIONORMAL;
				break;
			case PR_HIGH:
				uid = lpPartFile->IsAutoDownPriority() ? IDS_PRIOAUTOHIGH : IDS_PRIOHIGH;
				break;
			default:
				uid = 0;
			}
			if (uid)
				sText = GetResString(uid);
		}
		break;
	case 8: //state
		sText = lpPartFile->getPartfileStatus();
		break;
	case DOWNLOAD_COLUMN_TRUST:
		{
			CPartFile *pPartFile = const_cast<CPartFile*>(lpPartFile);
			sText = FakeFileDetector::FormatTrustHint(BuildDownloadTrustHint(*pPartFile));
		}
		break;
	case 10: //remaining time & size
		if (lpPartFile->GetStatus() != PS_COMPLETING && lpPartFile->GetStatus() != PS_COMPLETE) {
			time_t restTime = lpPartFile->getTimeRemaining();
			sText.Format(_T("%s (%s)"), (LPCTSTR)CastSecondsToHM(restTime), (LPCTSTR)CastItoXBytes((uint64)(lpPartFile->GetFileSize() - lpPartFile->GetCompletedSize())));
		}
		break;
	case 11: //last seen complete
		if (lpPartFile->lastseencomplete == 0)
			sText = GetResString(IDS_NEVER);
		else
			sText = lpPartFile->lastseencomplete.Format(thePrefs.GetDateTimeFormat4Lists());
		if (lpPartFile->m_nCompleteSourcesCountLo == 0)
			sText.AppendFormat(_T(" (< %u)"), lpPartFile->m_nCompleteSourcesCountHi);
		else if (lpPartFile->m_nCompleteSourcesCountLo == lpPartFile->m_nCompleteSourcesCountHi)
			sText.AppendFormat(_T(" (%u)"), lpPartFile->m_nCompleteSourcesCountLo);
		else
			sText.AppendFormat(_T(" (%u - %u)"), lpPartFile->m_nCompleteSourcesCountLo, lpPartFile->m_nCompleteSourcesCountHi);
		break;
	case 12: //last receive
		if (lpPartFile->GetLastReceptionDate() == time_t(-1))
			sText = GetResString(IDS_NEVER);
		else
			sText = lpPartFile->GetCFileDate().Format(thePrefs.GetDateTimeFormat4Lists());
		break;
	case 13: //cat
		{
			UINT cat = lpPartFile->GetCategory();
			if (cat)
				sText = thePrefs.GetCategory(cat)->strTitle;
		}
		break;
	case 14: //added on
		if (lpPartFile->GetCrFileDate())
			sText = lpPartFile->GetCrCFileDate().Format(thePrefs.GetDateTimeFormat4Lists());
		else
			sText += _T('?');
		break;
	case 16: // percentage
		sText.Format(_T("%.1f%%"), lpPartFile->GetPercentCompleted());
	}
	return sText;
}

void CDownloadListCtrl::ShowFilesCount()
{
	theApp.emuledlg->transferwnd->UpdateFilesCount(GetFilesCountInCurCat());
}

void CDownloadListCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	const UINT uCommand = DownloadListKeyboardShortcutsSeams::ClassifyKeyMessage(
		WM_KEYDOWN,
		nChar,
		GetKeyState(VK_CONTROL) < 0,
		GetKeyState(VK_MENU) < 0,
		GetKeyState(VK_SHIFT) < 0);
	if (uCommand != 0) {
		SendMessage(WM_COMMAND, uCommand);
		return;
	}

	CMuleListCtrl::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CDownloadListCtrl::ShowSelectedFileDetails()
{
	CPoint point;
	if (!::GetCursorPos(&point))
		return;
	ScreenToClient(&point);
	int it = HitTest(point);
	if (it == -1)
		return;

	SetItemState(-1, 0, LVIS_SELECTED);
	SetItemState(it, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	SetSelectionMark(it);   // display selection mark correctly!

	CtrlItem_Struct *content = reinterpret_cast<CtrlItem_Struct*>(GetItemData(GetSelectionMark()));
	if (content != NULL)
		if (content->type == FILE_TYPE) {
			if (!IsLiveFileItem(content)) {
				PruneStaleFileItems();
				return;
			}
			const CPartFile *file = static_cast<CPartFile*>(content->value);
			bool b = (thePrefs.ShowRatingIndicator()
				&& (file->HasComment() || file->HasRating() || file->IsKadCommentSearchRunning())
				&& point.x >= sm_iIconOffset + theApp.GetSmallSytemIconSize().cx
				&& point.x <= sm_iIconOffset + theApp.GetSmallSytemIconSize().cx + RATING_ICON_WIDTH);
			ShowFileDialog(b ? IDD_COMMENTLST : 0);
		} else if (IsLiveSourceItem(content))
			ShowClientDialog(static_cast<CUpDownClient*>(content->value));
		else if (IsSourceCtrlItem(content))
			PruneStaleSourceItems();
}

int CDownloadListCtrl::GetCompleteDownloads(int cat, int &total)
{
	total = 0;
	int count = 0;
	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
		const CtrlItem_Struct *cur_item = it->second;
		if (cur_item->type == FILE_TYPE) {
			if (!IsLiveFileItem(cur_item))
				continue;
			/*const*/ CPartFile *file = static_cast<CPartFile*>(cur_item->value);
			if (file->CheckShowItemInGivenCat(cat) || cat == -1) {
				++total;
				count += static_cast<int>(file->GetStatus() == PS_COMPLETE);
			}
		}
	}
	return count;
}

void CDownloadListCtrl::UpdateCurrentCategoryView()
{
	ChangeCategory(m_curTab);
}

void CDownloadListCtrl::UpdateCurrentCategoryView(CPartFile *thisfile)
{
	ListItems::const_iterator it = m_ListItems.find(thisfile);
	if (it != m_ListItems.end()) {
		const CtrlItem_Struct *cur_item = it->second;
		if (IsLiveFileItem(cur_item)) {
			CPartFile *file = static_cast<CPartFile*>(cur_item->value);
			if (file->CheckShowItemInGivenCat(m_curTab))
				ShowFile(it);
			else
				HideFile(it);
		}
	}
}

void CDownloadListCtrl::ChangeCategory(int newsel)
{
	PruneStaleFileItems();
	SetRedraw(false);

	// show the files of the selected category, remove all others
	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
		const CtrlItem_Struct *cur_item = it->second;
		if (IsLiveFileItem(cur_item))
			if (static_cast<CPartFile*>(cur_item->value)->CheckShowItemInGivenCat(newsel))
				ShowFile(it);
			else
				HideFile(it);
	}

	SetRedraw(true);
	m_curTab = newsel;
	ShowFilesCount();
}

void CDownloadListCtrl::HideFile(ListItems::const_iterator ihide)
{
	if (!IsLiveFileItem(ihide->second))
		return;
	CPartFile *tohide = static_cast<CPartFile*>(ihide->second->value);
	HideSources(tohide);

	// Retrieve all entries matching the source
	for (ListItems::const_iterator it = ihide; it != m_ListItems.end() && it->first == tohide; ++it) {
		// Find entry in CListCtrl and update object
		LVFINDINFO find;
		find.flags = LVFI_PARAM;
		find.lParam = (LPARAM)it->second;
		int iItem = FindItem(&find);
		if (iItem >= 0) {
			DeleteItem(iItem);
			return;
		}
	}
}

void CDownloadListCtrl::ShowFile(ListItems::const_iterator ishow)
{
	CtrlItem_Struct *updateItem = ishow->second;

	// Check if entry is already in the List
	LVFINDINFO find;
	find.flags = LVFI_PARAM;
	find.lParam = (LPARAM)updateItem;
	if (FindItem(&find) == -1)
		InsertItem(LVIF_TEXT | LVIF_PARAM, GetItemCount(), LPSTR_TEXTCALLBACK, 0, 0, 0, (LPARAM)updateItem);
}

void CDownloadListCtrl::GetDisplayedFiles(CArray<CPartFile*, CPartFile*> *list)
{
	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
		const CtrlItem_Struct *cur_item = it->second;
		if (IsLiveFileItem(cur_item))
			list->Add(static_cast<CPartFile*>(cur_item->value));
	}
}

void CDownloadListCtrl::MoveCompletedfilesCat(UINT from, UINT to)
{
	const UINT cmin = min(from, to);
	const UINT cmax = max(from, to);
	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
		const CtrlItem_Struct *cur_item = it->second;
		if (IsLiveFileItem(cur_item)) {
			CPartFile *file = static_cast<CPartFile*>(cur_item->value);
			if (!file->IsPartFile()) {
				UINT mycat = file->GetCategory();
				if (mycat >= cmin && mycat <= cmax)
					if (mycat == from)
						mycat = to;
					else
						mycat += (from < to ? -1 : 1);
				file->SetCategory(mycat);
			}
		}
	}
}

int CDownloadListCtrl::CountFilesAssignedToCategory(UINT cat) const
{
	int count = 0;
	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
		const CtrlItem_Struct *cur_item = it->second;
		if (IsLiveFileItem(cur_item) && static_cast<const CPartFile*>(cur_item->value)->GetCategory() == cat)
			++count;
	}
	return count;
}

void CDownloadListCtrl::OnLvnGetDispInfo(LPNMHDR pNMHDR, LRESULT *pResult)
{
	if (!theApp.IsClosing()) {
		// Although we have an owner drawn listview control we store the text for the primary item in the
		// listview, to be capable of quick searching those items via the keyboard. Because our listview
		// items may change their contents, we do this via a text callback function. The listview control
		// will send us the LVN_DISPINFO notification if it needs to know the contents of the primary item.
		//
		// But, the listview control sends this notification all the time, even if we do not search for an item.
		// At least this notification is only sent for the visible items and not for all items in the list.
		// Though, because this function is invoked *very* often, do *NOT* put any time consuming code in here.
		//
		// Vista: That callback is used to get the strings for the label tips for the sub(!)-items.
		//
		const LVITEM &rItem = reinterpret_cast<NMLVDISPINFO*>(pNMHDR)->item;
		/*TRACE("CDownloadListCtrl::OnLvnGetDispInfo iItem=%d iSubItem=%d", rItem.iItem, rItem.iSubItem);
		if (rItem.mask & LVIF_TEXT)
			TRACE(" LVIF_TEXT");
		if (rItem.mask & LVIF_IMAGE)
			TRACE(" LVIF_IMAGE");
		if (rItem.mask & LVIF_STATE)
			TRACE(" LVIF_STATE");
		TRACE("\n");*/
		if (rItem.mask & LVIF_TEXT) {
			const CtrlItem_Struct *pCtrlItem = reinterpret_cast<CtrlItem_Struct*>(rItem.lParam);
			if (pCtrlItem != NULL && pCtrlItem->value != NULL)
				switch (pCtrlItem->type) {
				case FILE_TYPE:
					if (IsLiveFileItem(pCtrlItem))
						_tcsncpy_s(rItem.pszText, rItem.cchTextMax, GetFileItemDisplayText(static_cast<CPartFile*>(pCtrlItem->value), rItem.iSubItem), _TRUNCATE);
					else if (rItem.pszText != NULL && rItem.cchTextMax > 0)
						rItem.pszText[0] = _T('\0');
					break;
				case UNAVAILABLE_SOURCE:
				case AVAILABLE_SOURCE:
					if (IsLiveSourceItem(pCtrlItem))
						_tcsncpy_s(rItem.pszText, rItem.cchTextMax, GetSourceItemDisplayText(pCtrlItem, rItem.iSubItem), _TRUNCATE);
					else if (rItem.pszText != NULL && rItem.cchTextMax > 0)
						rItem.pszText[0] = _T('\0');
					break;
				default:
					ASSERT(0);
				}
		}
	}
	*pResult = 0;
}

void CDownloadListCtrl::OnLvnGetInfoTip(LPNMHDR pNMHDR, LRESULT *pResult)
{
	LPNMLVGETINFOTIP pGetInfoTip = reinterpret_cast<LPNMLVGETINFOTIP>(pNMHDR);
	LVHITTESTINFO hti;
	if (pGetInfoTip && pGetInfoTip->iSubItem == 0 && ::GetCursorPos(&hti.pt)) {
		ScreenToClient(&hti.pt);
		if (SubItemHitTest(&hti) == -1 || hti.iItem != pGetInfoTip->iItem || hti.iSubItem != 0) {
			// don't show the default label tip for the main item, if the mouse is not over the main item
			if ((pGetInfoTip->dwFlags & LVGIT_UNFOLDED) == 0 && pGetInfoTip->cchTextMax > 0 && pGetInfoTip->pszText[0] != _T('\0'))
				pGetInfoTip->pszText[0] = _T('\0');
			return;
		}

		const CtrlItem_Struct *content = reinterpret_cast<CtrlItem_Struct*>(GetItemData(pGetInfoTip->iItem));
		if (content && pGetInfoTip->pszText && pGetInfoTip->cchTextMax > 0) {
			CString info;
			bool bPreviewBitmapHandled = false;

			// build info text and display it
			if (content->type == 1) { // for downloading files
				if (!IsLiveFileItem(content)) {
					PruneStaleFileItems();
					pGetInfoTip->pszText[0] = _T('\0');
					*pResult = 0;
					return;
				}
				CPartFile *pPartFile = static_cast<CPartFile*>(content->value);
				info = pPartFile->GetInfoSummary();
				const SFakeFileReport fakeReport = FakeFileDetector::GetPartFileReportSnapshot(*pPartFile);
				const CString strTrustText = FakeFileDetector::FormatTrustHint(BuildDownloadTrustHint(*pPartFile, fakeReport));
				CString strTrustLine;
				strTrustLine.Format(GetResString(IDS_SEARCH_TRUST_INFOTIP), (LPCTSTR)strTrustText);
				if (!info.IsEmpty())
					info += _T('\n');
				info += strTrustLine;
				if (fakeReport.nScore > 0 || fakeReport.bPendingHeaderCheck) {
					if (!info.IsEmpty())
						info += _T('\n');
					info += FakeFileDetector::FormatReportDetails(fakeReport);
				}
				if (pPartFile->IsMovie() && thePrefs.IsVideoThumbnailGenerationEnabled()
					&& PartFilePreviewSeams::IsValidConfiguredFfmpegPath(thePrefs.GetVideoThumbnailFfmpegPath())) {
					HBITMAP hBitmap = GetCachedVideoThumbnail(pPartFile);
					if (hBitmap != NULL)
						m_tooltip.SetPreviewBitmap(hBitmap, PartFilePreviewSeams::kVideoThumbnailDisplayMaxWidth);
					else
						m_tooltip.SetPreviewBitmap(NULL, 0);
					bPreviewBitmapHandled = true;
				}
				QueueVideoThumbnail(pPartFile, true);
			}
			else if ((content->type == 3 || content->type == 2) && IsLiveSourceItem(content)) { // for sources
				const CUpDownClient *client = static_cast<CUpDownClient*>(content->value);
				if (client->IsEd2kClient()) {
					in_addr server;
					server.s_addr = client->GetServerIP();
					info.Format(GetResString(IDS_USERINFO) + _T("%s:%s:%u\n\n")
						, client->GetUserName() ? client->GetUserName() : (LPCTSTR)(_T('(') + GetResString(IDS_UNKNOWN) + _T(')'))
						, (LPCTSTR)GetResString(IDS_SERVER)
						, (LPCTSTR)ipstr(server)
						, client->GetServerPort());
					if (client->GetDownloadState() != DS_CONNECTING && client->GetDownloadState() != DS_DOWNLOADING) { //do not display inappropriate 'next re-ask'
						info.AppendFormat(GetResString(IDS_NEXT_REASK) + _T(":%s"), (LPCTSTR)CastSecondsToHM(client->GetTimeUntilReask(client->GetRequestFile()) / SEC2MS(1)));
						info.AppendFormat(_T(" (%s)"), (LPCTSTR)CastSecondsToHM(client->GetTimeUntilReask(content->owner) / SEC2MS(1)));
						info += _T('\n');
					}
					info.AppendFormat(GetResString(IDS_SOURCEINFO), client->GetAskedCountDown(), client->GetAvailablePartCount());
					info += _T('\n');

					if (content->type == 2) {
						info.AppendFormat(_T("%s%s"), (LPCTSTR)GetResString(IDS_CLIENTSOURCENAME), client->GetClientFilename().IsEmpty() ? _T("-") : (LPCTSTR)client->GetClientFilename());
						if (!client->GetFileComment().IsEmpty())
							info.AppendFormat(_T("\n%s %s"), (LPCTSTR)GetResString(IDS_CMT_READ), (LPCTSTR)client->GetFileComment());
						if (client->GetFileRating())
							info.AppendFormat(_T("\n%s:%s"), (LPCTSTR)GetResString(IDS_QL_RATING), (LPCTSTR)GetRateString(client->GetFileRating()));
					} else { // client asked twice
						info += GetResString(IDS_ASKEDFAF);
						if (client->GetRequestFile() && !client->GetRequestFile()->GetFileName().IsEmpty())
							info.AppendFormat(_T(": %s"), (LPCTSTR)client->GetRequestFile()->GetFileName());
					}

					if (!client->m_OtherRequests_list.IsEmpty()) {
						CSimpleArray<const CString*> apstrFileNames;
						for (POSITION pos = client->m_OtherRequests_list.GetHeadPosition(); pos != NULL;)
							apstrFileNames.Add(&client->m_OtherRequests_list.GetNext(pos)->GetFileName());
						Sort(apstrFileNames);
						if (content->type == 2)
							info += _T('\n');
						info.AppendFormat(_T("\n%s:"), (LPCTSTR)GetResString(IDS_A4AF_FILES));

						for (int i = 0; i < apstrFileNames.GetSize(); ++i) {
							const CString *pstrFileName = apstrFileNames[i];
							if (info.GetLength() + (i > 0 ? 2 : 0) + pstrFileName->GetLength() >= pGetInfoTip->cchTextMax) {
								static TCHAR const szEllipsis[] = _T("\n:...");
								if (info.GetLength() + (int)_countof(szEllipsis) - 1 < pGetInfoTip->cchTextMax)
									info += szEllipsis;
								break;
							}
							if (i > 0)
								info += _T("\n:");
							info += *pstrFileName;
						}
					}
				} else
					info.Format(_T("URL: %s\nAvailable parts: %u"), client->GetUserName(), client->GetAvailablePartCount());
			} else if (IsSourceCtrlItem(content))
				PruneStaleSourceItems();
			if (!bPreviewBitmapHandled)
				m_tooltip.SetPreviewBitmap(NULL, 0);

			info += TOOLTIP_AUTOFORMAT_SUFFIX_CH;
			_tcsncpy(pGetInfoTip->pszText, info, pGetInfoTip->cchTextMax);
			pGetInfoTip->pszText[pGetInfoTip->cchTextMax - 1] = _T('\0');
		}
	}
	*pResult = 0;
}

void CDownloadListCtrl::ShowFileDialog(UINT uInvokePage)
{
	CSimpleArray<CPartFile*> aFiles;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		int iItem = GetNextSelectedItem(pos);
		if (iItem >= 0) {
			const CtrlItem_Struct *pCtrlItem = reinterpret_cast<CtrlItem_Struct*>(GetItemData(iItem));
			if (IsLiveFileItem(pCtrlItem))
				aFiles.Add(static_cast<CPartFile*>(pCtrlItem->value));
		}
	}

	if (aFiles.GetSize() > 0) {
		CDownloadListListCtrlItemWalk::SetItemType(FILE_TYPE);
		CFileDetailDialog dialog(&aFiles, uInvokePage, this);
		dialog.DoModal();
	}
}

CDownloadListListCtrlItemWalk::CDownloadListListCtrlItemWalk(CDownloadListCtrl *pListCtrl)
	: CListCtrlItemWalk(pListCtrl)
	, m_pDownloadListCtrl(pListCtrl)
	, m_eItemType(INVALID_TYPE)
{
}

CObject* CDownloadListListCtrlItemWalk::GetPrevSelectableItem()
{
	if (m_pDownloadListCtrl == NULL) {
		ASSERT(0);
		return NULL;
	}
	ASSERT(m_eItemType != INVALID_TYPE);

	int iItemCount = m_pDownloadListCtrl->GetItemCount();
	if (iItemCount >= 2) {
		POSITION pos = m_pDownloadListCtrl->GetFirstSelectedItemPosition();
		if (pos) {
			int iItem = m_pDownloadListCtrl->GetNextSelectedItem(pos);
			int iCurSelItem = iItem;
			while (--iItem >= 0) {
				const CtrlItem_Struct *ctrl_item = reinterpret_cast<CtrlItem_Struct*>(m_pDownloadListCtrl->GetItemData(iItem));
				if (ctrl_item != NULL
					&& (ctrl_item->type == m_eItemType || (m_eItemType != FILE_TYPE && ctrl_item->type != FILE_TYPE))
					&& (ctrl_item->type == FILE_TYPE ? m_pDownloadListCtrl->IsLiveFileItem(ctrl_item) : m_pDownloadListCtrl->IsLiveSourceItem(ctrl_item)))
				{
					m_pDownloadListCtrl->SetItemState(iCurSelItem, 0, LVIS_SELECTED | LVIS_FOCUSED);
					m_pDownloadListCtrl->SetItemState(iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
					m_pDownloadListCtrl->SetSelectionMark(iItem);
					m_pDownloadListCtrl->EnsureVisible(iItem, FALSE);
					return reinterpret_cast<CObject*>(ctrl_item->value);
				}
			}
		}
	}
	return NULL;
}

CObject* CDownloadListListCtrlItemWalk::GetNextSelectableItem()
{
	ASSERT(m_pDownloadListCtrl != NULL);
	if (m_pDownloadListCtrl == NULL)
		return NULL;
	ASSERT(m_eItemType != (ItemType)-1);

	int iItemCount = m_pDownloadListCtrl->GetItemCount();
	if (iItemCount >= 2) {
		POSITION pos = m_pDownloadListCtrl->GetFirstSelectedItemPosition();
		if (pos) {
			int iItem = m_pDownloadListCtrl->GetNextSelectedItem(pos);
			int iCurSelItem = iItem;
			while (++iItem < iItemCount) {
				const CtrlItem_Struct *ctrl_item = reinterpret_cast<CtrlItem_Struct*>(m_pDownloadListCtrl->GetItemData(iItem));
				if (ctrl_item != NULL
					&& (ctrl_item->type == m_eItemType || (m_eItemType != FILE_TYPE && ctrl_item->type != FILE_TYPE))
					&& (ctrl_item->type == FILE_TYPE ? m_pDownloadListCtrl->IsLiveFileItem(ctrl_item) : m_pDownloadListCtrl->IsLiveSourceItem(ctrl_item)))
				{
					m_pDownloadListCtrl->SetItemState(iCurSelItem, 0, LVIS_SELECTED | LVIS_FOCUSED);
					m_pDownloadListCtrl->SetItemState(iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
					m_pDownloadListCtrl->SetSelectionMark(iItem);
					m_pDownloadListCtrl->EnsureVisible(iItem, FALSE);
					return reinterpret_cast<CObject*>(ctrl_item->value);
				}
			}
		}
	}
	return NULL;
}

void CDownloadListCtrl::ShowClientDialog(CUpDownClient *pClient)
{
	if (!IsLiveDownloadClient(pClient))
		return;

	CDownloadListListCtrlItemWalk::SetItemType(AVAILABLE_SOURCE); // just set to something !=FILE_TYPE
	CClientDetailDialog dialog(pClient, this);
	dialog.DoModal();
}

CImageList* CDownloadListCtrl::CreateDragImage(int /*iItem*/, LPPOINT lpPoint)
{
	static const int iMaxSelectedItems = 30;
	int iSelectedItems = 0;
	CRect rcSelectedItems, rcLabel;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos && iSelectedItems < iMaxSelectedItems;) {
		int iItem = GetNextSelectedItem(pos);
		const CtrlItem_Struct *pCtrlItem = reinterpret_cast<CtrlItem_Struct*>(GetItemData(iItem));
		if (IsLiveFileItem(pCtrlItem) && GetItemRect(iItem, rcLabel, LVIR_LABEL)) {
			if (iSelectedItems <= 0) {
				rcSelectedItems.left = sm_iIconOffset;
				rcSelectedItems.top = rcLabel.top;
				rcSelectedItems.right = rcLabel.right;
				rcSelectedItems.bottom = rcLabel.bottom;
			}
			rcSelectedItems.UnionRect(rcSelectedItems, rcLabel);
			++iSelectedItems;
		}
	}
	if (iSelectedItems <= 0)
		return NULL;

	CClientDC dc(this);
	CDC dcMem;
	if (!dcMem.CreateCompatibleDC(&dc))
		return NULL;

	CBitmap bmpMem;
	if (!bmpMem.CreateCompatibleBitmap(&dc, rcSelectedItems.Width(), rcSelectedItems.Height()))
		return NULL;

	CBitmap *pOldBmp = dcMem.SelectObject(&bmpMem);
	CFont *pOldFont = dcMem.SelectObject(GetFont());

	COLORREF crBackground = ::GetSysColor(COLOR_WINDOW);
	dcMem.FillSolidRect(0, 0, rcSelectedItems.Width(), rcSelectedItems.Height(), crBackground);
	dcMem.SetTextColor(::GetSysColor(COLOR_WINDOWTEXT));

	iSelectedItems = 0;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos && iSelectedItems < iMaxSelectedItems;) {
		int iItem = GetNextSelectedItem(pos);
		const CtrlItem_Struct *pCtrlItem = reinterpret_cast<CtrlItem_Struct*>(GetItemData(iItem));
		if (IsLiveFileItem(pCtrlItem)) {
			const CPartFile *pPartFile = static_cast<CPartFile*>(pCtrlItem->value);
			GetItemRect(iItem, rcLabel, LVIR_LABEL);

			RECT rcItem;
			rcItem.left = 16 + sm_iLabelOffset;
			rcItem.top = rcLabel.top - rcSelectedItems.top;
			rcItem.right = rcLabel.right;
			rcItem.bottom = rcItem.top + rcLabel.Height();

			if (theApp.GetSystemImageList()) {
				int iImage = theApp.GetFileTypeSystemImageIdx(pPartFile->GetFileName());
				::ImageList_Draw(theApp.GetSystemImageList(), iImage, dcMem, 0, rcItem.top, ILD_TRANSPARENT);
			}

			dcMem.DrawText(pPartFile->GetFileName(), &rcItem, MLC_DT_TEXT);

			++iSelectedItems;
		}
	}
	dcMem.SelectObject(pOldBmp);
	dcMem.SelectObject(pOldFont);

	// At this point the bitmap in 'bmpMem' may or may not contain alpha data and we have to take special
	// care about passing such a bitmap further into Windows (GDI). Strange things can happen due to that
	// not all GDI functions can deal with RGBA bitmaps. Thus, create an image list with ILC_COLORDDB.
	CImageList *pimlDrag = new CImageList();
	pimlDrag->Create(rcSelectedItems.Width(), rcSelectedItems.Height(), ILC_COLORDDB | ILC_MASK, 1, 0);
	pimlDrag->Add(&bmpMem, crBackground);
	bmpMem.DeleteObject();

	if (lpPoint) {
		CPoint ptCursor;
		::GetCursorPos(&ptCursor);
		ScreenToClient(&ptCursor);
		lpPoint->x = ptCursor.x - rcSelectedItems.left;
		lpPoint->y = ptCursor.y - rcSelectedItems.top;
	}

	return pimlDrag;
}

bool CDownloadListCtrl::ReportAvailableCommands(CList<int> &liAvailableCommands)
{
	const ULONGLONG curTick = ::GetTickCount64();

	if (curTick < m_dwLastAvailableCommandsCheck + SEC2MS(3) && !m_availableCommandsDirty)
		return false;
	m_dwLastAvailableCommandsCheck = curTick;
	m_availableCommandsDirty = false;

	int iSel = GetNextItem(-1, LVIS_SELECTED);
	if (iSel >= 0) {
		const CtrlItem_Struct *content = reinterpret_cast<CtrlItem_Struct*>(GetItemData(iSel));
		if (content != NULL && content->type == FILE_TYPE) {
			if (!IsLiveFileItem(content)) {
				PruneStaleFileItems();
				return true;
			}
			// get merged settings
			int iSelectedItems = 0;
			//int iFilesNotDone = 0;
			int iFilesToPause = 0;
			int iFilesToStop = 0;
			int iFilesToResume = 0;
			int iFilesToOpen = 0;
			//int iFilesGetPreviewParts = 0;
			//int iFilesPreviewType = 0;
			int iFilesToPreview = 0;
			int iFilesToCancel = 0;
			for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
				const CtrlItem_Struct *pItemData = reinterpret_cast<CtrlItem_Struct*>(GetItemData(GetNextSelectedItem(pos)));
				if (!IsLiveFileItem(pItemData))
					continue;
				const CPartFile *pFile = static_cast<CPartFile*>(pItemData->value);
				++iSelectedItems;

				iFilesToCancel += static_cast<int>(pFile->GetStatus() != PS_COMPLETING);
				//iFilesNotDone += static_cast<int>(pFile->GetStatus()!=PS_COMPLETE && pFile->GetStatus()!=PS_COMPLETING);
				iFilesToStop += static_cast<int>(pFile->CanStopFile());
				iFilesToPause += static_cast<int>(pFile->CanPauseFile());
				iFilesToResume += static_cast<int>(pFile->CanResumeFile());
				iFilesToOpen += static_cast<int>(pFile->CanOpenFile());
				//iFilesGetPreviewParts += static_cast<int>(pFile->GetPreviewPrio());
				//iFilesPreviewType += static_cast<int>(pFile->IsPreviewableFileType());
				iFilesToPreview += static_cast<int>(pFile->IsReadyForPreview());
			}

			// enable commands if there is at least one item which can be used for the action
			if (iFilesToCancel > 0)
				liAvailableCommands.AddTail(MP_CANCEL);
			if (iFilesToStop > 0)
				liAvailableCommands.AddTail(MP_STOP);
			if (iFilesToPause > 0)
				liAvailableCommands.AddTail(MP_PAUSE);
			if (iFilesToResume > 0)
				liAvailableCommands.AddTail(MP_RESUME);
			if (iSelectedItems == 1 && iFilesToOpen == 1)
				liAvailableCommands.AddTail(MP_OPEN);
			if (iSelectedItems == 1 && iFilesToPreview == 1)
				liAvailableCommands.AddTail(MP_PREVIEW);
			if (iSelectedItems == 1)
				liAvailableCommands.AddTail(MP_OPENFOLDER);
			if (iSelectedItems > 0) {
				liAvailableCommands.AddTail(MP_METINFO);
				liAvailableCommands.AddTail(MP_VIEWFILECOMMENTS);
				liAvailableCommands.AddTail(MP_SHOWED2KLINK);
				liAvailableCommands.AddTail(MP_NEWCAT);
				liAvailableCommands.AddTail(MP_PRIOLOW);
				if (theApp.emuledlg->searchwnd->CanSearchRelatedFiles())
					liAvailableCommands.AddTail(MP_SEARCHRELATED);
			}
		}
	}
	int total;
	if (GetCompleteDownloads(m_curTab, total) > 0)
		liAvailableCommands.AddTail(MP_CLEARCOMPLETED);
	int iBatchFilesToPause = 0;
	int iBatchFilesToResume = 0;
	int iBatchFilesToStop = 0;
	CountBatchCommandsInCurCat(iBatchFilesToPause, iBatchFilesToResume, iBatchFilesToStop);
	if (iBatchFilesToPause > 0)
		liAvailableCommands.AddTail(MP_PAUSE_CATEGORY);
	if (iBatchFilesToResume > 0)
		liAvailableCommands.AddTail(MP_RESUME_CATEGORY);
	if (iBatchFilesToStop > 0)
		liAvailableCommands.AddTail(MP_STOP_CATEGORY);
	if (GetItemCount() > 0)
		liAvailableCommands.AddTail(MP_FIND);
	return true;
}
