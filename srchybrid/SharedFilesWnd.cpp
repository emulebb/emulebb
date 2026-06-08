//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( devs@emule-project.net / https://www.emule-project.net )
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
#include "emuleDlg.h"
#include "SharedFilesWnd.h"
#include "KnownFilePointerValidation.h"
#include "SharedFilesWndSeams.h"
#include "WorkerUiMessageSeams.h"
#include "OtherFunctions.h"
#include "SharedFileList.h"
#include "SharedDirectoryOps.h"
#include "KnownFile.h"
#include "UserMsgs.h"
#include "HelpIDs.h"
#include "HighColorTab.hpp"
#include "Log.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define	SPLITTER_MARGIN			0

namespace
{
int GetSharedFilesClientWidth(CWnd *pWnd)
{
	if (pWnd == NULL || pWnd->GetSafeHwnd() == NULL)
		return 0;

	CRect rectClient;
	pWnd->GetClientRect(&rectClient);
	return rectClient.Width();
}
}


// CSharedFilesWnd dialog

IMPLEMENT_DYNAMIC(CSharedFilesWnd, CDialog)

BEGIN_MESSAGE_MAP(CSharedFilesWnd, CResizableDialog)
	ON_BN_CLICKED(IDC_RELOADSHAREDFILES, OnBnClickedReloadSharedFiles)
	ON_MESSAGE(UM_DELAYED_EVALUATE, OnChangeFilter)
	ON_NOTIFY(LVN_ITEMACTIVATE, IDC_SFLIST, OnLvnItemActivateSharedFiles)
	ON_NOTIFY(NM_CLICK, IDC_SFLIST, OnNmClickSharedFiles)
	ON_NOTIFY(TVN_SELCHANGED, IDC_SHAREDDIRSTREE, OnTvnSelChangedSharedDirsTree)
	ON_STN_DBLCLK(IDC_FILES_ICO, OnStnDblClickFilesIco)
	ON_WM_CTLCOLOR()
	ON_WM_HELPINFO()
	ON_WM_SYSCOLORCHANGE()
	ON_BN_CLICKED(IDC_SF_HIDESHOWDETAILS, OnBnClickedSfHideshowdetails)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_SFLIST, OnLvnItemchangedSflist)
	ON_WM_SHOWWINDOW()
	ON_WM_DESTROY()
	ON_MESSAGE(UM_AICH_HASHING_COUNT_CHANGED, OnAICHHashingCountChanged)
	ON_MESSAGE(UM_MONITORED_SHARED_DIR_UPDATE, OnMonitoredSharedDirectoryUpdate)
END_MESSAGE_MAP()

CSharedFilesWnd::CSharedFilesWnd(CWnd *pParent /*=NULL*/)
	: CResizableDialog(CSharedFilesWnd::IDD, pParent)
	, icon_files()
	, m_nFilterColumn()
	, m_bWorkerUiClosing(false)
	, m_bDetailsVisible(true)
	, m_bSharedTreeInitialized(false)
#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
	, m_bStartupSharedTreePopulatedReported(false)
	, m_bStartupSharedModelPopulatedReported(false)
	, m_bStartupSharedFilesReadyReported(false)
	, m_bStartupSharedFilesHashingDoneReported(false)
#endif
	, m_bReloadToolTipCreated(false)
	, m_bDeferredFullReloadAfterHash(false)
	, m_bDeferredSharedFilesReloadAfterHash(false)
{
}

CSharedFilesWnd::~CSharedFilesWnd()
{
	DiscardPostedWorkerUiPayloadsForOwner(GetWorkerUiPayloadOwnerKey());
	m_ctlSharedListHeader.Detach();
	if (icon_files)
		VERIFY(::DestroyIcon(icon_files));
}

void CSharedFilesWnd::DoDataExchange(CDataExchange *pDX)
{
	CResizableDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SFLIST, sharedfilesctrl);
	DDX_Control(pDX, IDC_SHAREDDIRSTREE, m_ctlSharedDirTree);
	DDX_Control(pDX, IDC_SHAREDFILES_FILTER, m_ctlFilter);
}

BOOL CSharedFilesWnd::OnInitDialog()
{
#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
	const ULONGLONG ullInitStart = theApp.GetStartupDiagnosticsTimestampUs();
#endif
	CResizableDialog::OnInitDialog();
	InitWindowStyles(this);
	SetAllIcons();
#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
	ULONGLONG ullPhaseStart = theApp.GetStartupDiagnosticsTimestampUs();
#endif
	sharedfilesctrl.Init();
#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
	theApp.AppendStartupDiagnosticsLine(_T("CSharedFilesWnd::OnInitDialog sharedfilesctrl.Init"), theApp.GetStartupDiagnosticsElapsedUs(ullPhaseStart));
#endif

#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
	ullPhaseStart = theApp.GetStartupDiagnosticsTimestampUs();
#endif
	sharedfilesctrl.EnsureModelBound();
#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
	{
		CString strPhase;
		strPhase.Format(_T("CSharedFilesWnd::OnInitDialog eager shared-files bind (%d visible rows)"), sharedfilesctrl.GetItemCount());
		theApp.AppendStartupDiagnosticsLine(strPhase, theApp.GetStartupDiagnosticsElapsedUs(ullPhaseStart));
		theApp.AppendStartupDiagnosticsCounter(_T("shared.visible_rows"), static_cast<ULONGLONG>(sharedfilesctrl.GetItemCount()), _T("rows"));
	}
#endif

	m_ctlSharedListHeader.Attach(sharedfilesctrl.GetHeaderCtrl()->Detach());
	CArray<int, int> aIgnore; // ignored no-text columns for filter edit
	aIgnore.Add(8); // shared parts
	aIgnore.Add(11); // shared ed2k/kad
	m_ctlFilter.OnInit(&m_ctlSharedListHeader, &aIgnore);

	RECT rcSpl;
	m_ctlSharedDirTree.GetWindowRect(&rcSpl);
	ScreenToClient(&rcSpl);

	CRect rcFiles;
	sharedfilesctrl.GetWindowRect(rcFiles);
	ScreenToClient(rcFiles);
	VERIFY(m_dlgDetails.Create(this, DS_CONTROL | DS_SETFONT | WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, WS_EX_CONTROLPARENT));
	m_dlgDetails.SetWindowPos(NULL, rcFiles.left, rcFiles.bottom + 4, rcFiles.Width() + 2, rcSpl.bottom - (rcFiles.bottom + 3), 0);
	AddAnchor(m_dlgDetails, BOTTOM_LEFT, BOTTOM_RIGHT);

	rcSpl.left = rcSpl.right + SPLITTER_MARGIN;
	rcSpl.right = rcSpl.left + SharedFilesWndSeams::kSplitterWidth;
	m_wndSplitter.CreateWnd(WS_CHILD | WS_VISIBLE, rcSpl, this, IDC_SPLITTER_SHAREDFILES);

	AddAnchor(m_ctlSharedDirTree, TOP_LEFT, BOTTOM_LEFT);
	AddAnchor(IDC_RELOADSHAREDFILES, TOP_RIGHT);
	AddAllOtherAnchors();

	const int iClientWidth = GetSharedFilesClientWidth(this);
	const int iPosStatInit = rcSpl.left;
	const int iPosStatNew = SharedFilesWndSeams::ClampSplitterPosition(thePrefs.GetSplitterbarPositionShared(), iClientWidth);
	rcSpl.left = iPosStatNew;
	rcSpl.right = iPosStatNew + SharedFilesWndSeams::kSplitterWidth;
	m_wndSplitter.MoveWindow(&rcSpl);
	DoResize(iPosStatNew - iPosStatInit);

	GetDlgItem(IDC_SF_HIDESHOWDETAILS)->SetFont(&theApp.m_fontSymbol);
	GetDlgItem(IDC_SF_HIDESHOWDETAILS)->BringWindowToTop();
	ShowDetailsPanel(thePrefs.GetShowSharedFilesDetails());
	if (m_reloadToolTip.Create(this)) {
		m_bReloadToolTipCreated = true;
		m_reloadToolTip.AddTool(GetDlgItem(IDC_RELOADSHAREDFILES), GetResString(IDS_SF_RELOADTIP));
		m_reloadToolTip.SetDelayTime(TTDT_AUTOPOP, SEC2MS(20));
		m_reloadToolTip.SetDelayTime(TTDT_INITIAL, SEC2MS(thePrefs.GetToolTipDelay()));
	}

#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
	ullPhaseStart = theApp.GetStartupDiagnosticsTimestampUs();
#endif
	EnsureSharedTreeInitialized();
#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
	{
		CString strPhase;
		strPhase.Format(_T("CSharedFilesWnd::OnInitDialog eager shared tree init (%d visible rows)"), sharedfilesctrl.GetItemCount());
		theApp.AppendStartupDiagnosticsLine(strPhase, theApp.GetStartupDiagnosticsElapsedUs(ullPhaseStart));
		theApp.AppendStartupDiagnosticsCounter(_T("shared.visible_rows_after_tree_init"), static_cast<ULONGLONG>(sharedfilesctrl.GetItemCount()), _T("rows"));
	}
#endif

	Localize();
	m_bWorkerUiClosing.store(false, std::memory_order_release);
#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
	theApp.AppendStartupDiagnosticsLine(_T("CSharedFilesWnd::OnInitDialog total"), theApp.GetStartupDiagnosticsElapsedUs(ullInitStart));
#endif
	return TRUE;
}

void CSharedFilesWnd::EnsureSharedTreeInitialized()
{
	if (m_bSharedTreeInitialized)
		return;

#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
	const ULONGLONG ullPhaseStart = theApp.GetStartupDiagnosticsTimestampUs();
#endif
	m_ctlSharedDirTree.Initialize(&sharedfilesctrl);
	if (thePrefs.GetUseSystemFontForMainControls())
		m_ctlSharedDirTree.SendMessage(WM_SETFONT, NULL, FALSE);
	m_bSharedTreeInitialized = true;
	sharedfilesctrl.SetDirectoryFilter(m_ctlSharedDirTree.GetSelectedFilter(), !m_ctlSharedDirTree.IsCreatingTree());
#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
	{
		CString strPhase;
		const CDirectoryItem *pSelectedFilter = m_ctlSharedDirTree.GetSelectedFilter();
		strPhase.Format(_T("CSharedFilesWnd shared dir tree Initialize (filter=%d rows=%d)"),
			pSelectedFilter != NULL ? static_cast<int>(pSelectedFilter->m_eItemType) : -1,
			sharedfilesctrl.GetItemCount());
		theApp.AppendStartupDiagnosticsLine(strPhase, theApp.GetStartupDiagnosticsElapsedUs(ullPhaseStart));
		theApp.AppendStartupDiagnosticsCounter(
			_T("shared.tree.selected_filter"),
			pSelectedFilter != NULL ? static_cast<ULONGLONG>(static_cast<unsigned>(pSelectedFilter->m_eItemType)) : 0ui64,
			_T("filter"));
	}
#endif

#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
	if (!m_bStartupSharedTreePopulatedReported) {
		theApp.AppendStartupDiagnosticsLine(_T("shared.tree.populated"), 0);
		theApp.AppendStartupDiagnosticsCounter(_T("shared.tree.visible_rows"), static_cast<ULONGLONG>(sharedfilesctrl.GetItemCount()), _T("rows"));
		m_bStartupSharedTreePopulatedReported = true;
	}
#endif
#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
	ReportStartupSharedFilesReadinessIfReady();
#endif
}

#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
void CSharedFilesWnd::OnStartupSharedFilesModelChanged()
{
	ReportStartupSharedFilesReadinessIfReady();
}

void CSharedFilesWnd::OnStartupDiagnosticsStartupComplete()
{
	ReportStartupSharedFilesReadinessIfReady();
}

void CSharedFilesWnd::ReportStartupSharedFilesReadinessIfReady()
{
	if (m_bStartupSharedFilesReadyReported && m_bStartupSharedFilesHashingDoneReported)
		return;
	if (!theApp.HasStartupDiagnosticsReachedStartupComplete())
		return;
	if (!m_bSharedTreeInitialized || !sharedfilesctrl.IsModelBound() || theApp.sharedfiles == NULL)
		return;

	const ULONGLONG ullPendingHashes = static_cast<ULONGLONG>(theApp.sharedfiles->GetHashingCount());
	const ULONGLONG ullSharedFileCount = static_cast<ULONGLONG>(theApp.sharedfiles->GetCount());
	const ULONGLONG ullVisibleRowCount = static_cast<ULONGLONG>(sharedfilesctrl.GetItemCount());
	const ULONGLONG ullHiddenSharedFileCount = (ullSharedFileCount >= ullVisibleRowCount) ? (ullSharedFileCount - ullVisibleRowCount) : 0ui64;
	const CDirectoryItem *pSelectedFilter = m_ctlSharedDirTree.GetSelectedFilter();

	if (!m_bStartupSharedFilesReadyReported) {
		if (!m_bStartupSharedModelPopulatedReported) {
			theApp.AppendStartupDiagnosticsLine(_T("shared.model.populated"), 0);
			m_bStartupSharedModelPopulatedReported = true;
		}
		theApp.AppendStartupDiagnosticsCounter(_T("shared.model.shared_files"), ullSharedFileCount, _T("files"));
		theApp.AppendStartupDiagnosticsCounter(_T("shared.model.visible_rows"), ullVisibleRowCount, _T("rows"));
		theApp.AppendStartupDiagnosticsCounter(_T("shared.model.hidden_shared_files"), ullHiddenSharedFileCount, _T("files"));
		theApp.AppendStartupDiagnosticsCounter(_T("shared.model.pending_hashes"), ullPendingHashes, _T("files"));
		theApp.AppendStartupDiagnosticsCounter(
			_T("shared.model.active_filter"),
			pSelectedFilter != NULL ? static_cast<ULONGLONG>(static_cast<unsigned>(pSelectedFilter->m_eItemType)) : 0ui64,
			_T("filter"));
		theApp.AppendStartupDiagnosticsLine(_T("ui.shared_files_ready"), 0);
		m_bStartupSharedFilesReadyReported = true;
	}

	// WHY: shutdown can clear shared-hash bookkeeping before the UI has accepted
	// every deferred result; the startup marker must only mean a live, caught-up list.
	if (!m_bStartupSharedFilesHashingDoneReported && ullPendingHashes == 0 && !theApp.IsClosing()) {
		theApp.AppendStartupDiagnosticsCounter(_T("shared.model.hashing_done_shared_files"), ullSharedFileCount, _T("files"));
		theApp.AppendStartupDiagnosticsCounter(_T("shared.model.hashing_done_visible_rows"), ullVisibleRowCount, _T("rows"));
		theApp.AppendStartupDiagnosticsLine(_T("ui.shared_files_hashing_done"), 0);
		m_bStartupSharedFilesHashingDoneReported = true;
	}
}
#endif

void CSharedFilesWnd::DoResize(int iDelta)
{
	CSplitterControl::ChangeWidth(&m_ctlSharedDirTree, iDelta);
	CSplitterControl::ChangeWidth(&m_ctlFilter, iDelta);
	CSplitterControl::ChangePos(&sharedfilesctrl, -iDelta, 0);
	CSplitterControl::ChangeWidth(&sharedfilesctrl, -iDelta);
	bool bAntiFlicker = (m_dlgDetails.IsWindowVisible() != FALSE);
	if (bAntiFlicker)
		m_dlgDetails.SetRedraw(FALSE);
	CSplitterControl::ChangePos(&m_dlgDetails, -iDelta, 0);
	CSplitterControl::ChangeWidth(&m_dlgDetails, -iDelta);
	if (bAntiFlicker)
		m_dlgDetails.SetRedraw(TRUE);

	RECT rcSpl;
	m_wndSplitter.GetWindowRect(&rcSpl);
	ScreenToClient(&rcSpl);
	thePrefs.SetSplitterbarPositionShared(rcSpl.left);

	RemoveAnchor(m_ctlFilter);
	RemoveAnchor(m_wndSplitter);
	RemoveAnchor(m_ctlSharedDirTree);
	RemoveAnchor(sharedfilesctrl);
	RemoveAnchor(m_dlgDetails);
	RemoveAnchor(IDC_SF_FICON);
	RemoveAnchor(IDC_SF_FNAME);

	GetDlgItem(IDC_SF_FICON)->SetWindowPos(NULL, rcSpl.right + 6, rcSpl.bottom - 18, 0, 0, SWP_NOSIZE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);
	CWnd *wname = GetDlgItem(IDC_SF_FNAME);
	RECT rcfname;
	wname->GetWindowRect(&rcfname);
	ScreenToClient(&rcfname);
	rcfname.left += iDelta;
	wname->MoveWindow(&rcfname);

	AddAnchor(m_ctlFilter, TOP_LEFT);
	AddAnchor(m_ctlSharedDirTree, TOP_LEFT, BOTTOM_LEFT);
	AddAnchor(m_wndSplitter, TOP_LEFT, BOTTOM_LEFT);
	AddAnchor(sharedfilesctrl, TOP_LEFT, BOTTOM_RIGHT);
	AddAnchor(m_dlgDetails, BOTTOM_LEFT, BOTTOM_RIGHT);
	AddAnchor(IDC_SF_FICON, BOTTOM_LEFT);
	AddAnchor(IDC_SF_FNAME, BOTTOM_LEFT, BOTTOM_RIGHT);

	CRect rcWnd;
	GetClientRect(&rcWnd);
	const int iClientWidth = rcWnd.Width();
	m_wndSplitter.SetRange(
		rcWnd.left + SharedFilesWndSeams::kMinTreeWidth,
		rcWnd.left + SharedFilesWndSeams::GetSplitterRangeMax(iClientWidth));

	Invalidate();
	UpdateWindow();
}

bool CSharedFilesWnd::IsSharedHashingReloadBlocked() const
{
	return SharedFilesWndSeams::ShouldDeferReloadForSharedHashing(theApp.sharedfiles != NULL && theApp.sharedfiles->HasSharedHashingWork());
}

void CSharedFilesWnd::DeferReloadUntilHashingDone(bool bForceTreeReload, bool bNotifyUser)
{
	const bool bWasDeferred = m_bDeferredFullReloadAfterHash || m_bDeferredSharedFilesReloadAfterHash;
	const SharedFilesWndSeams::ReloadDeferralState currentState = {
		m_bDeferredFullReloadAfterHash,
		m_bDeferredSharedFilesReloadAfterHash
	};
	const SharedFilesWndSeams::ReloadDeferralState newState = SharedFilesWndSeams::AddDeferredReloadRequest(currentState, bForceTreeReload);
	m_bDeferredFullReloadAfterHash = newState.bFullTreeReload;
	m_bDeferredSharedFilesReloadAfterHash = newState.bSharedFilesReload;

	if (bNotifyUser && !bWasDeferred)
		AddLogLine(false, GetResString(IDS_SF_RELOADDEFERRED));
	UpdateReloadButtonState();
}

void CSharedFilesWnd::RunDeferredReloadAfterHash()
{
	const SharedFilesWndSeams::ReloadDeferralState currentState = {
		m_bDeferredFullReloadAfterHash,
		m_bDeferredSharedFilesReloadAfterHash
	};
	if (!SharedFilesWndSeams::HasDeferredReload(currentState))
		return;

	const bool bForceTreeReload = currentState.bFullTreeReload;
	m_bDeferredFullReloadAfterHash = false;
	m_bDeferredSharedFilesReloadAfterHash = false;
	if (bForceTreeReload)
		(void)Reload(true);
	else {
		theApp.sharedfiles->Reload();
		sharedfilesctrl.ReloadFileList();
		ShowSelectedFilesDetails();
#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
		ReportStartupSharedFilesReadinessIfReady();
#endif
	}
}

void CSharedFilesWnd::OnSharedHashingDrained()
{
	RunDeferredReloadAfterHash();
	UpdateReloadButtonState();
#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
	ReportStartupSharedFilesReadinessIfReady();
#endif
}

bool CSharedFilesWnd::Reload(bool bForceTreeReload)
{
	if (IsSharedHashingReloadBlocked()) {
		DeferReloadUntilHashingDone(bForceTreeReload, true);
		return false;
	}

	sharedfilesctrl.EnsureModelBound();
	EnsureSharedTreeInitialized();
	sharedfilesctrl.SetDirectoryFilter(NULL, false);
	m_ctlSharedDirTree.Reload(bForceTreeReload); // force a reload of the tree to update the 'accessible' state of each directory
	sharedfilesctrl.SetDirectoryFilter(m_ctlSharedDirTree.GetSelectedFilter(), false);
	theApp.sharedfiles->Reload();

	ShowSelectedFilesDetails();
#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
	ReportStartupSharedFilesReadinessIfReady();
#endif
	return true;
}

void CSharedFilesWnd::OnStnDblClickFilesIco()
{
	theApp.emuledlg->ShowPreferences(IDD_PPG_DIRECTORIES);
}

void CSharedFilesWnd::OnBnClickedReloadSharedFiles()
{
	CWaitCursor curWait;
#ifdef _DEBUG
	if (GetKeyState(VK_CONTROL) < 0) {
		theApp.sharedfiles->RebuildMetaData();
		sharedfilesctrl.Invalidate();
		sharedfilesctrl.UpdateWindow();
		return;
	}
#endif
	Reload(true);
}

LRESULT CSharedFilesWnd::OnAICHHashingCountChanged(WPARAM wParam, LPARAM)
{
	sharedfilesctrl.ApplyAICHHashingCount(static_cast<INT_PTR>(wParam));
	return 0;
}

LRESULT CSharedFilesWnd::OnMonitoredSharedDirectoryUpdate(WPARAM wParam, LPARAM)
{
	std::unique_ptr<SMonitoredSharedDirectoryUpdate> pUpdate = TakePostedWorkerUiPayload<SMonitoredSharedDirectoryUpdate>(wParam);
	if (!pUpdate)
		return 0;

	bool bStateChanged = false;
	if (!pUpdate->liDowngradedRoots.IsEmpty()) {
		CStringList retainedMonitoredRoots;
		CStringList monitorOwnedDirs;
		thePrefs.CopyMonitoredSharedRootList(retainedMonitoredRoots);
		thePrefs.CopyMonitorOwnedDirectoryList(monitorOwnedDirs);
		for (POSITION pos = pUpdate->liDowngradedRoots.GetHeadPosition(); pos != NULL;)
			(void)SharedDirectoryOps::RemoveSharedDirectory(retainedMonitoredRoots, pUpdate->liDowngradedRoots.GetNext(pos), false);
		if (SharedDirectoryOps::RemoveMonitorOwnedDirectoriesForDowngradedRoots(monitorOwnedDirs, pUpdate->liDowngradedRoots, retainedMonitoredRoots)) {
			thePrefs.ReplaceMonitorOwnedDirectoryList(monitorOwnedDirs);
			bStateChanged = true;
		}
		for (POSITION pos = pUpdate->liDowngradedRoots.GetHeadPosition(); pos != NULL;) {
			const CString strRoot(pUpdate->liDowngradedRoots.GetNext(pos));
			bStateChanged |= thePrefs.RemoveMonitoredSharedRoot(strRoot);
		}
	}

	for (POSITION pos = pUpdate->liNewDirectories.GetHeadPosition(); pos != NULL;) {
		const CString strDirectory(pUpdate->liNewDirectories.GetNext(pos));
		bStateChanged |= thePrefs.AddSharedDirectoryIfAbsent(strDirectory);
		bStateChanged |= thePrefs.AddMonitorOwnedDirectoryIfAbsent(strDirectory);
	}
	for (POSITION pos = pUpdate->liNewMonitoredRoots.GetHeadPosition(); pos != NULL;) {
		const CString strDirectory(pUpdate->liNewMonitoredRoots.GetNext(pos));
		bStateChanged |= thePrefs.AddMonitoredSharedRootIfAbsent(strDirectory);
	}
	for (POSITION pos = pUpdate->liRemovedDirectories.GetHeadPosition(); pos != NULL;) {
		const CString strDirectory(pUpdate->liRemovedDirectories.GetNext(pos));
		bStateChanged |= thePrefs.RemoveMonitorOwnedDirectory(strDirectory);
		bStateChanged |= thePrefs.RemoveSharedDirectory(strDirectory);
	}

	if (bStateChanged) {
		(void)thePrefs.Save();
		theApp.WakeSharedDirectoryMonitor();
	}

	if (pUpdate->bForceTreeReload || bStateChanged)
		(void)Reload(true);
	else if (pUpdate->bReloadSharedFiles) {
		if (IsSharedHashingReloadBlocked())
			DeferReloadUntilHashingDone(false, false);
		else
			theApp.sharedfiles->Reload();
	}
	return 0;
}

void CSharedFilesWnd::OnDestroy()
{
	m_bWorkerUiClosing.store(true, std::memory_order_release);
	DiscardPostedWorkerUiPayloadsForOwner(GetWorkerUiPayloadOwnerKey());
	CResizableDialog::OnDestroy();
}

void CSharedFilesWnd::OnLvnItemActivateSharedFiles(LPNMHDR, LRESULT*)
{
	ShowSelectedFilesDetails();
}

void CSharedFilesWnd::OnNmClickSharedFiles(LPNMHDR pNMHDR, LRESULT *pResult)
{
	OnLvnItemActivateSharedFiles(pNMHDR, pResult);
	*pResult = 0;
}

BOOL CSharedFilesWnd::PreTranslateMessage(MSG *pMsg)
{
	if (theApp.emuledlg->IsTransientDialogActive())
		return FALSE;
	if (m_bReloadToolTipCreated)
		m_reloadToolTip.RelayEvent(pMsg);
	switch (pMsg->message) {
	case WM_KEYDOWN:
		// Don't handle Ctrl+Tab in this window. It will be handled by main window.
		if (pMsg->wParam == VK_TAB && GetKeyState(VK_CONTROL) < 0)
			return FALSE;
		if (pMsg->wParam == VK_ESCAPE)
			return FALSE;
		break;
	case WM_KEYUP:
		if (pMsg->hwnd == sharedfilesctrl.m_hWnd)
			OnLvnItemActivateSharedFiles(0, 0);
		break;
	case WM_MBUTTONUP:
		{
			CPoint point;
			if (!::GetCursorPos(&point))
				return FALSE;
			sharedfilesctrl.ScreenToClient(&point);
			int it = sharedfilesctrl.HitTest(point);
			if (it < 0)
				return FALSE;

			sharedfilesctrl.SetItemState(-1, 0, LVIS_SELECTED);
			sharedfilesctrl.SetItemState(it, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			sharedfilesctrl.SetSelectionMark(it);   // display selection mark correctly!
			sharedfilesctrl.ShowComments(sharedfilesctrl.GetFileByIndex(it));
			return TRUE;
		}
	}

	return CResizableDialog::PreTranslateMessage(pMsg);
}

void CSharedFilesWnd::OnSysColorChange()
{
	CResizableDialog::OnSysColorChange();
	SetAllIcons();
}

void CSharedFilesWnd::SetAllIcons()
{
	if (icon_files)
		VERIFY(::DestroyIcon(icon_files));
	icon_files = theApp.LoadIcon(_T("SharedFilesList"), 16, 16);
	static_cast<CStatic*>(GetDlgItem(IDC_FILES_ICO))->SetIcon(icon_files);
}

void CSharedFilesWnd::Localize()
{
	sharedfilesctrl.Localize();
	if (m_bSharedTreeInitialized)
		m_ctlSharedDirTree.Localize();
	m_ctlFilter.ShowColumnText(true);
	sharedfilesctrl.SetDirectoryFilter(NULL, true);
	SetDlgItemText(IDC_RELOADSHAREDFILES, GetResString(IDS_SF_RELOAD));
	UpdateReloadToolTipText();
	m_dlgDetails.Localize();
}

void CSharedFilesWnd::OnTvnSelChangedSharedDirsTree(LPNMHDR, LRESULT *pResult)
{
	sharedfilesctrl.SetDirectoryFilter(m_ctlSharedDirTree.GetSelectedFilter(), !m_ctlSharedDirTree.IsCreatingTree());
	*pResult = 0;
}

LRESULT CSharedFilesWnd::DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_PAINT:
		if (m_wndSplitter) {
			CRect rcWnd;
			GetWindowRect(rcWnd);
			if (rcWnd.Width() > 0) {
				RECT rcSpl;
				m_ctlSharedDirTree.GetWindowRect(&rcSpl);
				ScreenToClient(&rcSpl);
				rcSpl.left = rcSpl.right + SPLITTER_MARGIN;
				rcSpl.right = rcSpl.left + SharedFilesWndSeams::kSplitterWidth;

				RECT rcFilter;
				m_ctlFilter.GetWindowRect(&rcFilter);
				ScreenToClient(&rcFilter);
				rcSpl.top = rcFilter.top;
				m_wndSplitter.MoveWindow(&rcSpl, TRUE);
			}
		}
		break;
	case WM_NOTIFY:
		if (wParam == IDC_SPLITTER_SHAREDFILES) {
			SPC_NMHDR *pHdr = reinterpret_cast<SPC_NMHDR*>(lParam);
			DoResize(pHdr->delta);
		}
		break;
	case WM_SIZE:
		if (m_wndSplitter) {
			CRect rcWnd;
			GetClientRect(&rcWnd);
			m_wndSplitter.SetRange(
				rcWnd.left + SharedFilesWndSeams::kMinTreeWidth,
				rcWnd.left + SharedFilesWndSeams::GetSplitterRangeMax(rcWnd.Width()));
		}
	}
	return CResizableDialog::DefWindowProc(message, wParam, lParam);
}

LRESULT CSharedFilesWnd::OnChangeFilter(WPARAM wParam, LPARAM lParam)
{
	CWaitCursor curWait; // this may take a while

	bool bColumnDiff = (m_nFilterColumn != (uint32)wParam);
	m_nFilterColumn = (uint32)wParam;

	CStringArray astrFilter;
	const CString &strFullFilterExpr((LPCTSTR)lParam);
	for (int iPos = 0; iPos >= 0;) {
		const CString &strFilter(strFullFilterExpr.Tokenize(_T(" "), iPos));
		if (!strFilter.IsEmpty() && strFilter != _T("-"))
			astrFilter.Add(strFilter);
	}

	bool bFilterDiff = (astrFilter.GetCount() != m_astrFilter.GetCount());
	if (!bFilterDiff)
		for (INT_PTR i = astrFilter.GetCount(); --i >= 0;)
			if (astrFilter[i] != m_astrFilter[i]) {
				bFilterDiff = true;
				break;
			}

	if (bColumnDiff || bFilterDiff) {
		m_astrFilter.RemoveAll();
		m_astrFilter.Append(astrFilter);

		if (IsSharedHashingReloadBlocked())
			sharedfilesctrl.ScheduleStartupDeferredReload();
		else
			sharedfilesctrl.ReloadFileList();
	}
	return 0;
}

BOOL CSharedFilesWnd::OnHelpInfo(HELPINFO*)
{
	theApp.ShowHelp(eMule_FAQ_GUI_SharedFiles);
	return TRUE;
}

HBRUSH CSharedFilesWnd::OnCtlColor(CDC *pDC, CWnd *pWnd, UINT nCtlColor)
{
	HBRUSH hbr = theApp.emuledlg->GetCtlColor(pDC, pWnd, nCtlColor);
	return hbr ? hbr : __super::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CSharedFilesWnd::SetToolTipsDelay(DWORD dwDelay)
{
	sharedfilesctrl.SetToolTipsDelay(dwDelay);
	if (m_bReloadToolTipCreated)
		m_reloadToolTip.SetDelayTime(TTDT_INITIAL, dwDelay);
}

void CSharedFilesWnd::UpdateReloadToolTipText()
{
	if (!m_bReloadToolTipCreated || GetSafeHwnd() == NULL)
		return;

	CWnd *pReloadButton = GetDlgItem(IDC_RELOADSHAREDFILES);
	if (pReloadButton == NULL)
		return;

	m_reloadToolTip.UpdateTipText(
		GetResString(IsSharedHashingReloadBlocked() ? IDS_SF_RELOADBUSYTIP : IDS_SF_RELOADTIP),
		pReloadButton);
}

void CSharedFilesWnd::UpdateReloadButtonState()
{
	if (GetSafeHwnd() == NULL)
		return;
	UpdateReloadToolTipText();
}

void CSharedFilesWnd::ShowSelectedFilesDetails(bool bForce)
{
	CTypedPtrList<CPtrList, CShareableFile*> selectedList;
	UINT nItems = m_dlgDetails.GetItems().GetSize();
	if (m_bDetailsVisible) {
		sharedfilesctrl.CollectSelectedFiles(selectedList);
		int i = 0;
		for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL; ++i) {
			CShareableFile *file = selectedList.GetNext(pos);
			if (nItems <= (UINT)i || m_dlgDetails.GetItems()[i] != file)
				bForce = true;
		}
	} else if (GetDlgItem(IDC_SF_FNAME)->IsWindowVisible()) {
		CShareableFile *pFile = sharedfilesctrl.GetSingleSelectedFile();
		static_cast<CStatic*>(GetDlgItem(IDC_SF_FICON))->SetIcon(pFile ? icon_files : NULL);
		const CString &sName(pFile ? pFile->GetFileName() : _T(""));
		SetDlgItemText(IDC_SF_FNAME, sName);
	}
	if (bForce || nItems != (UINT)selectedList.GetCount())
		m_dlgDetails.SetFiles(selectedList);
}

void CSharedFilesWnd::ShowDetailsPanel(bool bShow)
{
	m_bDetailsVisible = bShow;
	thePrefs.SetShowSharedFilesDetails(bShow);
	RemoveAnchor(sharedfilesctrl);
	RemoveAnchor(IDC_SF_HIDESHOWDETAILS);

	CRect rcFiles;
	sharedfilesctrl.GetWindowRect(rcFiles);
	ScreenToClient(rcFiles);

	CRect rcDetailDlg;
	m_dlgDetails.GetWindowRect(rcDetailDlg);

	CWnd &button = *GetDlgItem(IDC_SF_HIDESHOWDETAILS);
	CRect rcButton;
	button.GetWindowRect(rcButton);

	RECT rcSpl;
	m_wndSplitter.GetWindowRect(&rcSpl);
	ScreenToClient(&rcSpl);

	if (bShow) {
		sharedfilesctrl.SetWindowPos(NULL, 0, 0, rcFiles.Width(), rcSpl.bottom - rcFiles.top - rcDetailDlg.Height() - 2, SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);
		m_dlgDetails.ShowWindow(SW_SHOW);
		GetDlgItem(IDC_SF_FICON)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_SF_FNAME)->ShowWindow(SW_HIDE);
		button.SetWindowPos(NULL, rcFiles.right - rcButton.Width() + 1, rcSpl.bottom - rcDetailDlg.Height() + 2, 0, 0, SWP_NOSIZE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);
		button.SetWindowText(_T("6"));
	} else {
		m_dlgDetails.ShowWindow(SW_HIDE);
		sharedfilesctrl.SetWindowPos(NULL, 0, 0, rcFiles.Width(), rcSpl.bottom - rcFiles.top - rcButton.Height() + 1, SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);
		GetDlgItem(IDC_SF_FICON)->ShowWindow(SW_SHOW);
		GetDlgItem(IDC_SF_FNAME)->ShowWindow(SW_SHOW);
		button.SetWindowPos(NULL, rcFiles.right - rcButton.Width() + 1, rcSpl.bottom - rcButton.Height() + 1, 0, 0, SWP_NOSIZE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);
		button.SetWindowText(_T("5"));
	}

	AddAnchor(sharedfilesctrl, TOP_LEFT, BOTTOM_RIGHT);
	AddAnchor(IDC_SF_HIDESHOWDETAILS, BOTTOM_RIGHT);
	sharedfilesctrl.SetFocus();
	ShowSelectedFilesDetails();
}

void CSharedFilesWnd::OnBnClickedSfHideshowdetails()
{
	ShowDetailsPanel(!m_bDetailsVisible);
}

void CSharedFilesWnd::OnLvnItemchangedSflist(LPNMHDR, LRESULT *pResult)
{
	if (!sharedfilesctrl.IsSelectionRestoreInProgress())
		ShowSelectedFilesDetails();
	*pResult = 0;
}

void CSharedFilesWnd::OnShowWindow(BOOL bShow, UINT)
{
	if (bShow) {
#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
		CString strPhase;
		const CDirectoryItem *pSelectedFilter = m_ctlSharedDirTree.GetSelectedFilter();
		strPhase.Format(_T("CSharedFilesWnd::OnShowWindow rows=%d filter=%d shared-count=%u"),
			sharedfilesctrl.GetItemCount(),
			pSelectedFilter != NULL ? static_cast<int>(pSelectedFilter->m_eItemType) : -1,
			static_cast<unsigned>(theApp.sharedfiles->GetCount()));
		theApp.AppendStartupDiagnosticsLine(strPhase, 0);
#endif
		ShowSelectedFilesDetails(true);
#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
		ReportStartupSharedFilesReadinessIfReady();
#endif
	}
}

void CSharedFilesWnd::OnVolumesChanged()
{
	if (m_bSharedTreeInitialized)
		m_ctlSharedDirTree.OnVolumesChanged();
}

void CSharedFilesWnd::OnSingleFileShareStatusChanged()
{
	if (m_bSharedTreeInitialized)
		m_ctlSharedDirTree.FileSystemTreeUpdateBoldState(NULL);
}


/////////////////////////////////////////////////////////////////////////////////////////////
// CSharedFileDetailsModelessSheet
IMPLEMENT_DYNAMIC(CSharedFileDetailsModelessSheet, CListViewPropertySheet)

BEGIN_MESSAGE_MAP(CSharedFileDetailsModelessSheet, CListViewPropertySheet)
	ON_MESSAGE(UM_DATA_CHANGED, OnDataChanged)
	ON_WM_CREATE()
END_MESSAGE_MAP()

int CSharedFileDetailsModelessSheet::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	// skip CResizableSheet::OnCreate because we don't need the styles and stuff which are set there
	//CreateSizeGrip(FALSE); // create grip but don't show it <- Do not do this with the new library!
	return CPropertySheet::OnCreate(lpCreateStruct);
}

bool NeedArchiveInfoPage(const CSimpleArray<CObject*> *paItems);
void UpdateFileDetailsPages(CListViewPropertySheet *pSheet, CResizablePage *pArchiveInfo
		, CResizablePage *pMediaInfo, CResizablePage *pFileLink);

CSharedFileDetailsModelessSheet::CSharedFileDetailsModelessSheet()
{
	m_psh.dwFlags &= ~PSH_HASHELP;
	m_psh.dwFlags |= PSH_MODELESS;

	m_wndStatistics.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndStatistics.m_psp.dwFlags |= PSP_USEICONID;
	m_wndStatistics.m_psp.pszIcon = _T("StatsDetail");
	m_wndStatistics.SetFiles(&m_aItems);
	AddPage(&m_wndStatistics);

	m_wndArchiveInfo.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndArchiveInfo.m_psp.dwFlags |= PSP_USEICONID;
	m_wndArchiveInfo.m_psp.pszIcon = _T("ARCHIVE_PREVIEW");
	m_wndArchiveInfo.SetReducedDialog();
	m_wndArchiveInfo.SetFiles(&m_aItems);

	m_wndMediaInfo.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndMediaInfo.m_psp.dwFlags |= PSP_USEICONID;
	m_wndMediaInfo.m_psp.pszIcon = _T("MEDIAINFO");
	m_wndMediaInfo.SetReducedDialog();
	m_wndMediaInfo.SetFiles(&m_aItems);
	if (NeedArchiveInfoPage(&m_aItems))
		AddPage(&m_wndArchiveInfo);
	else
		AddPage(&m_wndMediaInfo);

	m_wndFileLink.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndFileLink.m_psp.dwFlags |= PSP_USEICONID;
	m_wndFileLink.m_psp.pszIcon = _T("ED2KLINK");
	m_wndFileLink.SetReducedDialog();
	m_wndFileLink.SetFiles(&m_aItems);
	AddPage(&m_wndFileLink);

	m_wndMetaData.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndMetaData.m_psp.dwFlags |= PSP_USEICONID;
	m_wndMetaData.m_psp.pszIcon = _T("METADATA");
	m_wndMetaData.SetFiles(&m_aItems);
	AddPage(&m_wndMetaData);

	/*LPCTSTR pPshStartPage = m_pPshStartPage;
	if (m_uInvokePage != 0)
		pPshStartPage = MAKEINTRESOURCE(m_uInvokePage);
	for (INT_PTR i = m_pages.GetCount(); --i >= 0;)
		if (GetPage(i)->m_psp.pszTemplate == pPshStartPage) {
			m_psh.nStartPage = (UINT)i;
			break;
		}*/
}

BOOL CSharedFileDetailsModelessSheet::OnInitDialog()
{
	EnableStackedTabs(FALSE);
	BOOL bResult = CListViewPropertySheet::OnInitDialog();
	HighColorTab::UpdateImageList(*this);
	InitWindowStyles(this);
	return bResult;
}

void  CSharedFileDetailsModelessSheet::SetFiles(CTypedPtrList<CPtrList, CShareableFile*> &aFiles)
{
	m_aItems.RemoveAll();
	for (POSITION pos = aFiles.GetHeadPosition(); pos != NULL;)
	{
		CShareableFile *file = aFiles.GetNext(pos);
		if (IsLiveKnownFilePointer(static_cast<CKnownFile*>(file)))
			m_aItems.Add(file);
	}
	ChangedData();
}

void CSharedFileDetailsModelessSheet::Localize()
{
	m_wndStatistics.Localize();
	SetTabTitle(IDS_SF_STATISTICS, &m_wndStatistics, this);
	m_wndFileLink.Localize();
	SetTabTitle(IDS_SW_LINK, &m_wndFileLink, this);
	m_wndArchiveInfo.Localize();
	SetTabTitle(IDS_CONTENT_INFO, &m_wndArchiveInfo, this);
	m_wndMediaInfo.Localize();
	SetTabTitle(IDS_CONTENT_INFO, &m_wndMediaInfo, this);
	m_wndMetaData.Localize();
	SetTabTitle(IDS_META_DATA, &m_wndMetaData, this);
}

LRESULT CSharedFileDetailsModelessSheet::OnDataChanged(WPARAM, LPARAM)
{
	for (int i = m_aItems.GetSize(); --i >= 0;) {
		if (!IsLiveKnownFilePointer(static_cast<CKnownFile*>(m_aItems[i])))
			m_aItems.RemoveAt(i);
	}

	//When using up/down keys in shared files list, "Content" tab grabs focus on archives
	CWnd *pFocused = GetFocus();
	UpdateFileDetailsPages(this, &m_wndArchiveInfo, &m_wndMediaInfo, &m_wndFileLink);
	if (pFocused) //try to stay in file list
		pFocused->SetFocus();
	return TRUE;
}
