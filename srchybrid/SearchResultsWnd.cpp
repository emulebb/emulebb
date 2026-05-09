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
#include "MenuCmds.h"
#include "SearchDlg.h"
#include "SearchResultsWnd.h"
#include "SearchParamsWnd.h"
#include "SearchParams.h"
#include "Packets.h"
#include "SearchFile.h"
#include "SearchList.h"
#include "ServerConnect.h"
#include "ServerList.h"
#include "Server.h"
#include "UpDownClient.h"
#include "ClientList.h"
#include "SafeFile.h"
#include "DownloadQueue.h"
#include "Statistics.h"
#include "emuledlg.h"
#include "opcodes.h"
#include "Kademlia/Kademlia/Kademlia.h"
#include "kademlia/kademlia/SearchManager.h"
#include "kademlia/kademlia/search.h"
#include "SearchExpr.h"
#define USE_FLEX
#include "Parser.hpp"
#include "Scanner.h"
#include "HelpIDs.h"
#include "Exceptions.h"
#include "StringConversion.h"
#include "UserMsgs.h"
#include "Log.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern int yyparse();
extern int yyerror(LPCTSTR errstr);
extern LPCTSTR g_aszInvKadKeywordChars;

enum ESearchTimerID
{
	TimerServerTimeout = 1,
	TimerGlobalSearch,
	TimerSearchQueue
};

enum ESearchResultImage
{
	sriServerActive,
	sriGlobalActive,
	sriKadActice,
	sriClient,
	sriServer,
	sriGlobal,
	sriKad
};

#define	SEARCH_LIST_MENU_BUTTON_XOFF	7
#define	SEARCH_LIST_MENU_BUTTON_YOFF	2
#define	SEARCH_LIST_MENU_BUTTON_WIDTH	170
#define	SEARCH_LIST_MENU_BUTTON_HEIGHT	22	// don't set the height do something different than 22 unless you know exactly what you are doing!
#define	SEARCH_QUEUE_SLOT_MS			SEC2MS(5)

// CSearchResultsWnd dialog

IMPLEMENT_DYNCREATE(CSearchResultsWnd, CResizableFormView)

BEGIN_MESSAGE_MAP(CSearchResultsWnd, CResizableFormView)
	ON_WM_TIMER()
	ON_BN_CLICKED(IDC_SDOWNLOAD, OnBnClickedDownloadSelected)
	ON_BN_CLICKED(IDC_CLEARALL, OnBnClickedClearAll)
	ON_NOTIFY(TCN_SELCHANGE, IDC_TAB1, OnSelChangeTab)
	ON_NOTIFY(TCN_SELCHANGING, IDC_TAB1, OnSelChangingTab)
	ON_MESSAGE(UM_CLOSETAB, OnCloseTab)
	ON_MESSAGE(UM_DBLCLICKTAB, OnDblClickTab)
	ON_WM_DESTROY()
	ON_WM_SYSCOLORCHANGE()
	ON_WM_CTLCOLOR()
	ON_WM_CLOSE()
	ON_WM_CREATE()
	ON_WM_HELPINFO()
	ON_MESSAGE(WM_IDLEUPDATECMDUI, OnIdleUpdateCmdUI)
	ON_BN_CLICKED(IDC_OPEN_PARAMS_WND, OnBnClickedOpenParamsWnd)
	ON_WM_SYSCOMMAND()
	ON_MESSAGE(UM_DELAYED_EVALUATE, OnChangeFilter)
	ON_NOTIFY(TBN_DROPDOWN, IDC_SEARCHLST_ICO, OnSearchListMenuBtnDropDown)
END_MESSAGE_MAP()

CSearchResultsWnd::CSearchResultsWnd(CWnd* /*pParent*/)
	: CResizableFormView(CSearchResultsWnd::IDD)
	, m_pwndParams()
	, m_searchpacket()
	, global_search_timer()
	, m_uTimerLocalServer()
	, m_uTimerSearchQueue()
	, m_nNextSearchID(0x80000000u)
	, m_nEd2kSearchID(0x80000000u)
	, m_nFilterColumn()
	, m_servercount()
	, m_iSentMoreReq()
	, m_b64BitSearchPacket()
	, m_globsearch()
	, m_cancelled()
{
	searchselect.m_bClosable = true;
}

CSearchResultsWnd::~CSearchResultsWnd()
{
	m_ctlSearchListHeader.Detach();
	delete m_searchpacket;
	if (m_uTimerLocalServer)
		VERIFY(KillTimer(m_uTimerLocalServer));
	DisarmSearchQueueTimer();
}

void CSearchResultsWnd::OnInitialUpdate()
{
	CResizableFormView::OnInitialUpdate();
	InitWindowStyles(this);
	theApp.searchlist->SetOutputWnd(&searchlistctrl);
	m_ctlSearchListHeader.Attach(searchlistctrl.GetHeaderCtrl()->Detach());
	searchlistctrl.Init(theApp.searchlist);
	searchlistctrl.SetPrefsKey(_T("SearchListCtrl"));

	static const RECT rc{SEARCH_LIST_MENU_BUTTON_XOFF
		, SEARCH_LIST_MENU_BUTTON_YOFF
		, SEARCH_LIST_MENU_BUTTON_XOFF + SEARCH_LIST_MENU_BUTTON_WIDTH
		, SEARCH_LIST_MENU_BUTTON_YOFF + SEARCH_LIST_MENU_BUTTON_HEIGHT};
	m_btnSearchListMenu.Init(true, true);
	m_btnSearchListMenu.MoveWindow(&rc);
	m_btnSearchListMenu.AddBtnStyle(IDC_SEARCHLST_ICO, TBSTYLE_AUTOSIZE);
	// Vista: Remove the TBSTYLE_TRANSPARENT to avoid flickering (can be done only after the toolbar was initially created with TBSTYLE_TRANSPARENT !?)
	m_btnSearchListMenu.ModifyStyle(TBSTYLE_TOOLTIPS | ((theApp.m_ullComCtrlVer >= MAKEDLLVERULL(6, 16, 0, 0)) ? TBSTYLE_TRANSPARENT : 0), 0);
	m_btnSearchListMenu.SetExtendedStyle(m_btnSearchListMenu.GetExtendedStyle() & ~TBSTYLE_EX_MIXEDBUTTONS);
	m_btnSearchListMenu.RecalcLayout(true);

	m_ctlFilter.OnInit(&m_ctlSearchListHeader);

	SetAllIcons();
	Localize();
	searchprogress.SetStep(1);
	global_search_timer = 0;
	m_globsearch = false;
	ShowSearchSelector(false); //set anchors for IDC_SEARCHLIST

	AddAnchor(m_btnSearchListMenu, TOP_LEFT);
	AddAnchor(IDC_FILTER, TOP_RIGHT);
	AddAnchor(IDC_SDOWNLOAD, BOTTOM_LEFT);
	AddAnchor(IDC_PROGRESS1, BOTTOM_LEFT, BOTTOM_RIGHT);
	AddAnchor(IDC_CLEARALL, BOTTOM_RIGHT);
	AddAnchor(IDC_OPEN_PARAMS_WND, TOP_RIGHT);
	AddAnchor(searchselect, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_STATIC_DLTOof, BOTTOM_LEFT);
	AddAnchor(m_cattabs, BOTTOM_LEFT, BOTTOM_RIGHT);

	if (theApp.m_fontSymbol.m_hObject) {
		GetDlgItem(IDC_STATIC_DLTOof)->SetFont(&theApp.m_fontSymbol);
		SetDlgItemText(IDC_STATIC_DLTOof, (GetExStyle() & WS_EX_LAYOUTRTL) ? _T("3") : _T("4")); // show a right-arrow
	}
}

BOOL CSearchResultsWnd::PreTranslateMessage(MSG *pMsg)
{
	if (theApp.emuledlg->m_pSplashWnd)
		return FALSE;
	if (pMsg->message == WM_MBUTTONUP) {
		CPoint point;
		::GetCursorPos(&point);
		searchlistctrl.ScreenToClient(&point);
		int it = searchlistctrl.HitTest(point);
		if (it == -1)
			return FALSE;

		searchlistctrl.SetItemState(-1, 0, LVIS_SELECTED);
		searchlistctrl.SetItemState(it, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		searchlistctrl.SetSelectionMark(it);   // display selection mark correctly!
		searchlistctrl.SendMessage(WM_COMMAND, MP_DETAIL);
		return TRUE;
	}

	return CResizableFormView::PreTranslateMessage(pMsg);
}

void CSearchResultsWnd::DoDataExchange(CDataExchange *pDX)
{
	CResizableFormView::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SEARCHLIST, searchlistctrl);
	DDX_Control(pDX, IDC_PROGRESS1, searchprogress);
	DDX_Control(pDX, IDC_TAB1, searchselect);
	DDX_Control(pDX, IDC_CATTAB2, m_cattabs);
	DDX_Control(pDX, IDC_FILTER, m_ctlFilter);
	DDX_Control(pDX, IDC_OPEN_PARAMS_WND, m_ctlOpenParamsWnd);
	DDX_Control(pDX, IDC_SEARCHLST_ICO, m_btnSearchListMenu);
}

void CSearchResultsWnd::StartSearch(SSearchParams *pParams)
{
	CString strError;
	if (!QueueSearch(pParams, strError) && !strError.IsEmpty())
		AfxMessageBox(strError, MB_ICONWARNING);
}

bool CSearchResultsWnd::StartSearchFromApi(SSearchParams *pParams, CString &rError)
{
	return QueueSearch(pParams, rError);
}

bool CSearchResultsWnd::ResolveAutomaticSearchType(SSearchParams *pParams, CString &rError) const
{
	if (pParams == NULL) {
		rError = _T("search parameters are required");
		return false;
	}

	if (pParams->eType == SearchTypeAutomatic) {
		if (!theApp.serverconnect->IsConnected() && Kademlia::CKademlia::IsRunning() && Kademlia::CKademlia::IsConnected())
			pParams->eType = SearchTypeKademlia;
		else if (theApp.serverconnect->IsConnected() && (!Kademlia::CKademlia::IsRunning() || !Kademlia::CKademlia::IsConnected()))
			pParams->eType = SearchTypeEd2kServer;
		else {
			if (!theApp.serverconnect->IsConnected() && (!Kademlia::CKademlia::IsRunning() || !Kademlia::CKademlia::IsConnected())) {
				rError = _T("not connected to eD2k or Kad");
				return false;
			}

			const CServer *const pCurrentServer = theApp.serverconnect->GetCurrentServer();
			pParams->eType = (theApp.serverconnect->IsConnected()
				&& pCurrentServer != NULL
				&& (pCurrentServer->IsStaticMember()
					|| (pCurrentServer->GetUsers() > 40000
						&& theApp.serverlist->GetServerCount() < 40
						&& pCurrentServer->GetUsers() < 2000000
						&& pCurrentServer->GetFiles() > 5000000)))
				? SearchTypeEd2kServer
				: SearchTypeKademlia;
		}
	}
	return true;
}

bool CSearchResultsWnd::QueueSearch(SSearchParams *pParams, CString &rError)
{
	if (!ResolveAutomaticSearchType(pParams, rError)) {
		delete pParams;
		return false;
	}

	if (pParams->eType != SearchTypeEd2kServer && pParams->eType != SearchTypeEd2kGlobal && pParams->eType != SearchTypeKademlia) {
		rError = _T("unsupported search method");
		delete pParams;
		return false;
	}

	if (pParams->dwSearchID == _UI32_MAX)
		pParams->dwSearchID = GetNextSearchID();

	theApp.searchlist->EnsureSearchResults(pParams->dwSearchID);
	if (!CreateNewTab(pParams, false)) {
		rError = _T("failed to create search tab");
		delete pParams;
		return false;
	}

	m_queuedSearches.AddTail(pParams);
	SearchStarted();
	ArmSearchQueueTimer(1);
	return true;
}

bool CSearchResultsWnd::StartQueuedSearch(SSearchParams *pParams, CString &rError)
{
	try {
		switch (pParams->eType) {
		case SearchTypeEd2kServer:
		case SearchTypeEd2kGlobal:
			if (!theApp.serverconnect->IsConnected()) {
				rError = _T("not connected to an eD2k server");
				return false;
			}
			if (!DoNewEd2kSearch(pParams)) {
				rError = _T("failed to start eD2k search");
				return false;
			}
			SetActiveSearchResultsIcon(pParams->dwSearchID);
			SearchStarted();
			return true;

		case SearchTypeKademlia:
			if (!Kademlia::CKademlia::IsRunning() || !Kademlia::CKademlia::IsConnected()) {
				rError = _T("Kad is not connected");
				return false;
			}
			if (!DoNewKadSearch(pParams)) {
				rError = _T("failed to start Kad search");
				return false;
			}
			SetActiveSearchResultsIcon(pParams->dwSearchID);
			SearchStarted();
			return true;

		default:
			rError = _T("unsupported search method");
			return false;
		}
	} catch (CMsgBoxException *pException) {
		rError = pException->m_strMsg;
		pException->Delete();
		return false;
	}
}

bool CSearchResultsWnd::IsQueuedSearchStartBlocked(const SSearchParams *pParams) const
{
	if (pParams == NULL)
		return false;

	switch (pParams->eType) {
	case SearchTypeEd2kServer:
	case SearchTypeEd2kGlobal:
		return m_uTimerLocalServer != 0 || global_search_timer != 0 || m_searchpacket != NULL;
	default:
		return false;
	}
}

void CSearchResultsWnd::ArmSearchQueueTimer(UINT uDelayMS)
{
	if (m_queuedSearches.IsEmpty() || m_uTimerSearchQueue != 0)
		return;
	m_uTimerSearchQueue = SetTimer(TimerSearchQueue, uDelayMS, NULL);
}

void CSearchResultsWnd::DisarmSearchQueueTimer()
{
	if (m_uTimerSearchQueue != 0) {
		VERIFY(KillTimer(m_uTimerSearchQueue));
		m_uTimerSearchQueue = 0;
	}
}

void CSearchResultsWnd::ProcessSearchQueue()
{
	DisarmSearchQueueTimer();
	if (m_queuedSearches.IsEmpty())
		return;

	SSearchParams *const pParams = m_queuedSearches.GetHead();
	if (IsQueuedSearchStartBlocked(pParams)) {
		ArmSearchQueueTimer(SEC2MS(1));
		return;
	}

	m_queuedSearches.RemoveHead();
	CString strError;
	if (!StartQueuedSearch(pParams, strError))
		AddLogLine(false, _T("Queued search '%s' was not started: %s"), (LPCTSTR)pParams->strExpression, (LPCTSTR)strError);

	ArmSearchQueueTimer(SEARCH_QUEUE_SLOT_MS);
}

void CSearchResultsWnd::OnTimer(UINT_PTR nIDEvent)
{
	CResizableFormView::OnTimer(nIDEvent);

	if (m_uTimerSearchQueue != 0 && nIDEvent == m_uTimerSearchQueue) {
		ProcessSearchQueue();
	} else if (m_uTimerLocalServer != 0 && nIDEvent == m_uTimerLocalServer) {
		if (thePrefs.GetDebugServerSearchesLevel() > 0)
			Debug(_T("Timeout waiting on search results of local server\n"));
		// the local server did not answer within the timeout
		VERIFY(KillTimer(m_uTimerLocalServer));
		m_uTimerLocalServer = 0;

		// start the global search
		if (m_globsearch) {
			if (global_search_timer == 0)
				VERIFY((global_search_timer = SetTimer(TimerGlobalSearch, SEC2MS(3) / 4, NULL)) != 0);
		} else
			CancelEd2kSearch();
	} else if (nIDEvent == global_search_timer) {
		if (theApp.serverconnect->IsConnected()) {
			CServer *pConnectedServer = theApp.serverconnect->GetCurrentServer();
			if (pConnectedServer)
				pConnectedServer = theApp.serverlist->GetServerByAddress(pConnectedServer->GetAddress(), pConnectedServer->GetPort());

			CServer *toask = NULL;
			while (++m_servercount < (unsigned)theApp.serverlist->GetServerCount()) {
				searchprogress.StepIt();
				toask = theApp.serverlist->GetNextSearchServer();
				if (toask == NULL || (toask != pConnectedServer && toask->GetFailedCount() < thePrefs.GetDeadServerRetries()))
					break;
				toask = NULL;
			}

			if (toask) {
				bool bRequestSent = false;
				if (toask->SupportsLargeFilesUDP() && (toask->GetUDPFlags() & SRV_UDPFLG_EXT_GETFILES)) {
					CSafeMemFile data(50);
					uint32 nTagCount = 1;
					data.WriteUInt32(nTagCount);
					CTag tagFlags(CT_SERVER_UDPSEARCH_FLAGS, SRVCAP_UDP_NEWTAGS_LARGEFILES);
					tagFlags.WriteNewEd2kTag(data);
					Packet *pExtSearchPacket = new Packet(OP_GLOBSEARCHREQ3, m_searchpacket->size + (uint32)data.GetLength());
					data.SeekToBegin();
					data.Read(pExtSearchPacket->pBuffer, (uint32)data.GetLength());
					memcpy(pExtSearchPacket->pBuffer + (uint32)data.GetLength(), m_searchpacket->pBuffer, m_searchpacket->size);
					theStats.AddUpDataOverheadServer(pExtSearchPacket->size);
					theApp.serverconnect->SendUDPPacket(pExtSearchPacket, toask, true);
					bRequestSent = true;
					if (thePrefs.GetDebugServerUDPLevel() > 0)
						Debug(_T(">>> Sending %s  to server %-21s (%3u of %3u)\n"), _T("OP_GlobSearchReq3"), (LPCTSTR)ipstr(toask->GetAddress(), toask->GetPort()), m_servercount, (unsigned)theApp.serverlist->GetServerCount());

				} else if (toask->GetUDPFlags() & SRV_UDPFLG_EXT_GETFILES) {
					if (!m_b64BitSearchPacket || toask->SupportsLargeFilesUDP()) {
						m_searchpacket->opcode = OP_GLOBSEARCHREQ2;
						if (thePrefs.GetDebugServerUDPLevel() > 0)
							Debug(_T(">>> Sending %s  to server %-21s (%3u of %3u)\n"), _T("OP_GlobSearchReq2"), (LPCTSTR)ipstr(toask->GetAddress(), toask->GetPort()), m_servercount, (unsigned)theApp.serverlist->GetServerCount());
						theStats.AddUpDataOverheadServer(m_searchpacket->size);
						theApp.serverconnect->SendUDPPacket(m_searchpacket, toask, false);
						bRequestSent = true;
					} else if (thePrefs.GetDebugServerUDPLevel() > 0)
						Debug(_T(">>> Skipped UDP search on server %-21s (%3u of %3u): No large file support\n"), (LPCTSTR)ipstr(toask->GetAddress(), toask->GetPort()), m_servercount, (unsigned)theApp.serverlist->GetServerCount());
				} else {
					if (!m_b64BitSearchPacket || toask->SupportsLargeFilesUDP()) {
						m_searchpacket->opcode = OP_GLOBSEARCHREQ;
						if (thePrefs.GetDebugServerUDPLevel() > 0)
							Debug(_T(">>> Sending %s  to server %-21s (%3u of %3u)\n"), _T("OP_GlobSearchReq1"), (LPCTSTR)ipstr(toask->GetAddress(), toask->GetPort()), m_servercount, (unsigned)theApp.serverlist->GetServerCount());
						theStats.AddUpDataOverheadServer(m_searchpacket->size);
						theApp.serverconnect->SendUDPPacket(m_searchpacket, toask, false);
						bRequestSent = true;
					} else if (thePrefs.GetDebugServerUDPLevel() > 0)
						Debug(_T(">>> Skipped UDP search on server %-21s (%3u of %3u): No large file support\n"), (LPCTSTR)ipstr(toask->GetAddress(), toask->GetPort()), m_servercount, (unsigned)theApp.serverlist->GetServerCount());
				}
				if (bRequestSent)
					theApp.searchlist->SentUDPRequestNotification(m_nEd2kSearchID, toask->GetIP());
			} else
				CancelEd2kSearch();
		} else
			CancelEd2kSearch();
	} else
		ASSERT(0);
}

void CSearchResultsWnd::SetSearchResultsIcon(uint32 uSearchID, int iImage)
{
	TCITEM ti;
	ti.mask = TCIF_PARAM;
	for (int i = searchselect.GetItemCount(); --i >= 0;)
		if (searchselect.GetItem(i, &ti) && ti.lParam != NULL && reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID == uSearchID) {
			ti.mask = TCIF_IMAGE;
			ti.iImage = iImage;
			searchselect.SetItem(i, &ti);
			break;
		}
}

void CSearchResultsWnd::SetActiveSearchResultsIcon(uint32 uSearchID)
{
	const SSearchParams *pParams = GetSearchResultsParams(uSearchID);
	if (pParams) {
		int iImage;
		if (pParams->eType == SearchTypeKademlia)
			iImage = sriKadActice;
		else if (pParams->eType == SearchTypeEd2kGlobal)
			iImage = sriGlobalActive;
		else
			iImage = sriServerActive;
		SetSearchResultsIcon(uSearchID, iImage);
	}
}

void CSearchResultsWnd::SetInactiveSearchResultsIcon(uint32 uSearchID)
{
	const SSearchParams *pParams = GetSearchResultsParams(uSearchID);
	if (pParams) {
		int iImage;
		if (pParams->eType == SearchTypeKademlia)
			iImage = sriKad;
		else if (pParams->eType == SearchTypeEd2kGlobal)
			iImage = sriGlobal;
		else
			iImage = sriServer;
		SetSearchResultsIcon(uSearchID, iImage);
	}
}

SSearchParams* CSearchResultsWnd::GetSearchResultsParams(uint32 uSearchID) const
{
	TCITEM ti;
	ti.mask = TCIF_PARAM;
	for (int i = searchselect.GetItemCount(); --i >= 0;)
		if (searchselect.GetItem(i, &ti) && ti.lParam != NULL && reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID == uSearchID)
			return reinterpret_cast<SSearchParams*>(ti.lParam);

	return NULL;
}

bool CSearchResultsWnd::IsSearchRunning(uint32 uSearchID) const
{
	if (IsSearchQueued(uSearchID))
		return true;

	const SSearchParams *const pParams = GetSearchResultsParams(uSearchID);
	if (pParams == NULL)
		return false;

	switch (pParams->eType) {
	case SearchTypeEd2kServer:
	case SearchTypeEd2kGlobal:
		return uSearchID == m_nEd2kSearchID && !m_cancelled
			&& (m_uTimerLocalServer != 0 || global_search_timer != 0 || m_searchpacket != NULL);
	case SearchTypeKademlia:
		return Kademlia::CSearchManager::IsSearching(uSearchID);
	default:
		return false;
	}
}

bool CSearchResultsWnd::IsSearchQueued(uint32 uSearchID) const
{
	for (POSITION pos = m_queuedSearches.GetHeadPosition(); pos != NULL;) {
		const SSearchParams *const pParams = m_queuedSearches.GetNext(pos);
		if (pParams != NULL && pParams->dwSearchID == uSearchID)
			return true;
	}
	return false;
}

bool CSearchResultsWnd::RemoveQueuedSearch(uint32 uSearchID)
{
	for (POSITION pos = m_queuedSearches.GetHeadPosition(); pos != NULL;) {
		POSITION posCurrent = pos;
		const SSearchParams *const pParams = m_queuedSearches.GetNext(pos);
		if (pParams != NULL && pParams->dwSearchID == uSearchID) {
			m_queuedSearches.RemoveAt(posCurrent);
			if (m_queuedSearches.IsEmpty())
				DisarmSearchQueueTimer();
			return true;
		}
	}
	return false;
}

void CSearchResultsWnd::ClearQueuedSearches()
{
	m_queuedSearches.RemoveAll();
	DisarmSearchQueueTimer();
}

void CSearchResultsWnd::CancelSearch(uint32 uSearchID)
{
	if (uSearchID == 0) {
		int iCurSel = searchselect.GetCurSel();
		if (iCurSel >= 0) {
			TCITEM ti;
			ti.mask = TCIF_PARAM;
			if (searchselect.GetItem(iCurSel, &ti) && ti.lParam != NULL)
				uSearchID = reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID;
		}
	}
	if (uSearchID == 0)
		return;

	const SSearchParams *pParams = GetSearchResultsParams(uSearchID);
	if (pParams == NULL)
		return;

	if (RemoveQueuedSearch(uSearchID)) {
		SearchCancelled(uSearchID);
		return;
	}

	switch (pParams->eType) {
	case SearchTypeEd2kServer:
	case SearchTypeEd2kGlobal:
		CancelEd2kSearch();
		break;
	case SearchTypeKademlia:
		Kademlia::CSearchManager::StopSearch(pParams->dwSearchID, false);
		CancelKadSearch(pParams->dwSearchID);
	}
}

void CSearchResultsWnd::CancelEd2kSearch()
{
	SetInactiveSearchResultsIcon(m_nEd2kSearchID);

	m_cancelled = true;

	// delete any global search timer
	if (global_search_timer) {
		VERIFY(KillTimer(global_search_timer));
		global_search_timer = 0;
		searchprogress.SetPos(0);
	}
	delete m_searchpacket;
	m_searchpacket = NULL;
	m_b64BitSearchPacket = false;
	m_globsearch = false;

	// delete local server timeout timer
	if (m_uTimerLocalServer) {
		VERIFY(KillTimer(m_uTimerLocalServer));
		m_uTimerLocalServer = 0;
	}

	SearchCancelled(m_nEd2kSearchID);
}

void CSearchResultsWnd::SearchStarted()
{
	const CWnd *pWndFocus = GetFocus();
	m_pwndParams->m_ctlStart.EnableWindow(FALSE);
	if (pWndFocus && pWndFocus->m_hWnd == m_pwndParams->m_ctlStart.m_hWnd)
		m_pwndParams->m_ctlName.SetFocus();
	m_pwndParams->m_ctlCancel.EnableWindow(TRUE);
}

void CSearchResultsWnd::SearchCancelled(uint32 uSearchID)
{
	SetInactiveSearchResultsIcon(uSearchID);

	int iSel = searchselect.GetCurSel();
	if (iSel >= 0) {
		TCITEM ti;
		ti.mask = TCIF_PARAM;
		if (searchselect.GetItem(iSel, &ti) && ti.lParam != NULL && uSearchID == reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID) {
			const CWnd *pWndFocus = GetFocus();
			m_pwndParams->m_ctlCancel.EnableWindow(FALSE);
			if (pWndFocus && pWndFocus->m_hWnd == m_pwndParams->m_ctlCancel.m_hWnd)
				m_pwndParams->m_ctlName.SetFocus();
			m_pwndParams->m_ctlStart.EnableWindow(m_pwndParams->m_ctlName.GetWindowTextLength() > 0);
		}
	}
}

void CSearchResultsWnd::LocalEd2kSearchEnd(UINT count, bool bMoreResultsAvailable)
{
	// local server has answered, kill the timeout timer
	if (m_uTimerLocalServer) {
		VERIFY(KillTimer(m_uTimerLocalServer));
		m_uTimerLocalServer = 0;
	}

	AddEd2kSearchResults(count);
	if (!m_cancelled) {
		if (!m_globsearch)
			SearchCancelled(m_nEd2kSearchID);
		else if (!global_search_timer)
			VERIFY((global_search_timer = SetTimer(TimerGlobalSearch, SEC2MS(3) / 4, NULL)) != 0);
	}
	const UINT uMaxMoreRequests = thePrefs.GetEd2kSearchMaxMoreRequests();
	m_pwndParams->m_ctlMore.EnableWindow(bMoreResultsAvailable && (uMaxMoreRequests == 0 || static_cast<UINT>(m_iSentMoreReq) < uMaxMoreRequests));
}

void CSearchResultsWnd::AddEd2kSearchResults(UINT count)
{
	const UINT uMaxResults = thePrefs.GetEd2kSearchMaxResults();
	if (!m_cancelled && uMaxResults != 0 && count > uMaxResults)
		CancelEd2kSearch();
}

void CSearchResultsWnd::OnBnClickedDownloadSelected()
{
	//start download(s)
	DownloadSelected();
}

void CSearchResultsWnd::OnDblClkSearchList(LPNMHDR, LRESULT *pResult)
{
	OnBnClickedDownloadSelected();
	*pResult = 0;
}

void CSearchResultsWnd::DownloadSelected()
{
	DownloadSelected(thePrefs.AddNewFilesPaused());
}

void CSearchResultsWnd::DownloadSelected(bool bPaused)
{
	CWaitCursor curWait;
	for (POSITION pos = searchlistctrl.GetFirstSelectedItemPosition(); pos != NULL;) {
		int iIndex = searchlistctrl.GetNextSelectedItem(pos);
		if (iIndex >= 0) {
			// get selected listview item (may be a child item from an expanded search result)
			const CSearchFile *sel_file = reinterpret_cast<CSearchFile*>(searchlistctrl.GetItemData(iIndex));

			// get parent
			const CSearchFile *parent = sel_file->GetListParent();
			if (parent == NULL)
				parent = sel_file;

			if (parent->IsComplete() == 0 && parent->GetSourceCount() >= 50) {
				CString strMsg;
				strMsg.Format(GetResString(IDS_ASKDLINCOMPLETE), (LPCTSTR)sel_file->GetFileName());
				if (AfxMessageBox(strMsg, MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES)
					continue;
			}

			// create new DL queue entry with all properties of parent (e.g. already received sources!)
			// but with the filename of the selected listview item.
			CSearchFile tempFile(parent);
			tempFile.SetAFileName(sel_file->GetFileName());
			tempFile.SetStrTagValue(FT_FILENAME, sel_file->GetFileName());
			theApp.downloadqueue->AddSearchToDownload(&tempFile, bPaused, GetSelectedCat());

			// update parent and all children
			searchlistctrl.UpdateSources(parent);
		}
	}
}

void CSearchResultsWnd::OnSysColorChange()
{
	CResizableFormView::OnSysColorChange();
	SetAllIcons();
	searchlistctrl.CreateMenus();
}

void CSearchResultsWnd::SetAllIcons()
{
	m_btnSearchListMenu.SetIcon(_T("SearchResults"));

	CImageList iml;
	iml.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	iml.Add(CTempIconLoader(_T("SearchMethod_ServerActive")));
	iml.Add(CTempIconLoader(_T("SearchMethod_GlobalActive")));
	iml.Add(CTempIconLoader(_T("SearchMethod_KademliaActive")));
	iml.Add(CTempIconLoader(_T("StatsClients")));
	iml.Add(CTempIconLoader(_T("SearchMethod_SERVER")));
	iml.Add(CTempIconLoader(_T("SearchMethod_GLOBAL")));
	iml.Add(CTempIconLoader(_T("SearchMethod_KADEMLIA")));
	searchselect.SetImageList(&iml);
	m_imlSearchResults.DeleteImageList();
	m_imlSearchResults.Attach(iml.Detach());
	searchselect.SetPadding(CSize(12, 3));
}

void CSearchResultsWnd::Localize()
{
	searchlistctrl.Localize();
	m_ctlFilter.ShowColumnText(true);
	UpdateCatTabs();

	SetDlgItemText(IDC_CLEARALL, GetResString(IDS_REMOVEALLSEARCH));
	m_btnSearchListMenu.SetWindowText(GetResString(IDS_SW_RESULT));
	SetDlgItemText(IDC_SDOWNLOAD, GetResString(IDS_SW_DOWNLOAD));
	m_ctlOpenParamsWnd.SetWindowText(GetResString(IDS_SEARCHPARAMS) + _T("..."));
}

void CSearchResultsWnd::OnBnClickedClearAll()
{
	DeleteAllSearches();
}

CString DbgGetFileMetaTagName(UINT uMetaTagID)
{
	LPCTSTR p;
	switch (uMetaTagID) {
	case FT_FILENAME:
		p = _T("@Name");
		break;
	case FT_FILESIZE:
		p = _T("@Size");
		break;
	case FT_FILESIZE_HI:
		p = _T("@SizeHI");
		break;
	case FT_FILETYPE:
		p = _T("@Type");
		break;
	case FT_FILEFORMAT:
		p = _T("@Format");
		break;
	case FT_LASTSEENCOMPLETE:
		p = _T("@LastSeenComplete");
		break;
	case FT_SOURCES:
		p = _T("@Sources");
		break;
	case FT_COMPLETE_SOURCES:
		p = _T("@Complete");
		break;
	case FT_MEDIA_ARTIST:
		p = _T("@Artist");
		break;
	case FT_MEDIA_ALBUM:
		p = _T("@Album");
		break;
	case FT_MEDIA_TITLE:
		p = _T("@Title");
		break;
	case FT_MEDIA_LENGTH:
		p = _T("@Length");
		break;
	case FT_MEDIA_BITRATE:
		p = _T("@Bitrate");
		break;
	case FT_MEDIA_CODEC:
		p = _T("@Codec");
		break;
	case FT_FILECOMMENT:
		p = _T("@Comment");
		break;
	case FT_FILERATING:
		p = _T("@Rating");
		break;
	case FT_FILEHASH:
		p = _T("@Filehash");
		break;
	default:
		{
			CString buffer;
			buffer.Format(_T("Tag0x%02X"), uMetaTagID);
			return buffer;
		}
	}
	return CString(p);
}

CString DbgGetFileMetaTagName(LPCSTR pszMetaTagID)
{
	if (strlen(pszMetaTagID) == 1)
		return DbgGetFileMetaTagName(((BYTE*)pszMetaTagID)[0]);
	CString strName;
	strName.Format(_T("\"%hs\""), pszMetaTagID);
	return strName;
}

CString DbgGetSearchOperatorName(UINT uOperator)
{
	static LPCTSTR const _aszEd2kOps[] =
	{
		_T("="),
		_T(">"),
		_T("<"),
		_T(">="),
		_T("<="),
		_T("<>"),
	};

	if (uOperator >= _countof(_aszEd2kOps)) {
		ASSERT(0);
		return _T("*UnkOp*");
	}
	return _aszEd2kOps[uOperator];
}

static CStringA s_strCurKadKeywordA;
static CSearchExpr s_SearchExpr;
CStringArray g_astrParserErrors;

static TCHAR s_chLastChar = 0;
static CString s_strSearchTree;

bool DumpSearchTree(int &iExpr, const CSearchExpr &rSearchExpr, int iLevel, bool bFlat)
{
	if (iExpr >= rSearchExpr.m_aExpr.GetCount())
		return false;
	if (!bFlat)
		s_strSearchTree.AppendFormat(_T("\n%s"), (LPCTSTR)CString(_T(' '), iLevel));
	const CSearchAttr &rSearchAttr(rSearchExpr.m_aExpr[iExpr++]);
	CStringA strTok(rSearchAttr.m_str);
	if (bFlat && s_chLastChar != _T('(') && s_chLastChar != _T('\0'))
		s_strSearchTree += _T(' ');
	if (strTok == SEARCHOPTOK_AND || strTok == SEARCHOPTOK_OR || strTok == SEARCHOPTOK_NOT) {
		s_strSearchTree.AppendFormat(_T("(%hs "), CPTRA(strTok, 1));
		s_chLastChar = _T('(');
		DumpSearchTree(iExpr, rSearchExpr, iLevel + 4, bFlat);
		DumpSearchTree(iExpr, rSearchExpr, iLevel + 4, bFlat);
		s_strSearchTree += _T(')');
		s_chLastChar = _T(')');
	} else {
		s_strSearchTree += rSearchAttr.DbgGetAttr();
		s_chLastChar = _T('\1');
	}
	return true;
}

bool DumpSearchTree(const CSearchExpr &rSearchExpr, bool bFlat)
{
	s_chLastChar = _T('\0');
	int iExpr = 0;
	int iLevel = 0;
	return DumpSearchTree(iExpr, rSearchExpr, iLevel, bFlat);
}

void ParsedSearchExpression(const CSearchExpr &expr)
{
	int iOpAnd = 0;
	int iOpOr = 0;
	int iOpNot = 0;
	int iNonDefTags = 0;
	//CStringA strDbg;
	for (INT_PTR i = 0; i < expr.m_aExpr.GetCount(); ++i) {
		const CSearchAttr &rSearchAttr(expr.m_aExpr[i]);
		const CStringA &rstr(rSearchAttr.m_str);
		if (rstr == SEARCHOPTOK_AND) {
			++iOpAnd;
			//strDbg.AppendFormat("%s ", CPTR(rstr, 1));
		} else if (rstr == SEARCHOPTOK_OR) {
			++iOpOr;
			//strDbg.AppendFormat("%s ", CPTR(rstr, 1));
		} else if (rstr == SEARCHOPTOK_NOT) {
			++iOpNot;
			//strDbg.AppendFormat("%s ", CPTR(rstr, 1));
		} else {
			if (rSearchAttr.m_iTag != FT_FILENAME)
				++iNonDefTags;
			//strDbg += rSearchAttr.DbgGetAttr() + " ";
		}
	}
	//if (thePrefs.GetDebugServerSearchesLevel() > 0)
	//	Debug(_T("Search Expr: %hs\n"), strDbg);

	// this limit (+ the additional operators which will be added later) has to match the limit in 'CreateSearchExpressionTree'
	//	+1 Type (Audio, Video)
	//	+1 MinSize
	//	+1 MaxSize
	//	+1 Avail
	//	+1 Extension
	//	+1 Complete sources
	//	+1 Codec
	//	+1 Bitrate
	//	+1 Length
	//	+1 Title
	//	+1 Album
	//	+1 Artist
	// ---------------
	//  12
	if (iOpAnd + iOpOr + iOpNot > 10)
		yyerror(GetResString(IDS_SEARCH_TOOCOMPLEX));

	// FIXME: When searching on Kad the keyword may not be included into the OR operator in anyway (or into the not part of NAND)
	// Currently we do not check this properly for all cases but only for the most common ones and more important we
	// do not try to rearrange keywords, which could make a search valid
	if (!s_strCurKadKeywordA.IsEmpty() && iOpOr > 0)
		if (iOpAnd + iOpNot > 0) {
			if (expr.m_aExpr.GetCount() > 2)
				if (expr.m_aExpr[0].m_str == SEARCHOPTOK_OR && expr.m_aExpr[1].m_str == s_strCurKadKeywordA)
					yyerror(GetResString(IDS_SEARCH_BADOPERATORCOMBINATION));
		} else // if we habe only OR its not going to work out for sure
			yyerror(GetResString(IDS_SEARCH_BADOPERATORCOMBINATION));

	s_SearchExpr.m_aExpr.RemoveAll();
	// optimize search expression, if no OR nor NOT specified
	if (iOpAnd > 0 && iOpOr == 0 && iOpNot == 0 && iNonDefTags == 0) {

		// figure out if we can use a better keyword than the one the user selected
		// for example most user will search like this "The oxymoronaccelerator 2", which would ask the node which indexes "the"
		// This causes higher traffic for such nodes and makes them a viable target to attackers, while the kad result should be
		// the same or even better if we ask the node which indexes the rare keyword "oxymoronaccelerator", so we try to rearrange
		// keywords and generally assume that the longer keywords are rarer
		if (thePrefs.GetRearrangeKadSearchKeywords() && !s_strCurKadKeywordA.IsEmpty()) {
			for (INT_PTR i = 0; i < expr.m_aExpr.GetCount(); ++i) {
				const CStringA &cs(expr.m_aExpr[i].m_str);
				if (   cs != SEARCHOPTOK_AND
					&& cs != s_strCurKadKeywordA
					&& cs.FindOneOf(g_aszInvKadKeywordCharsA) < 0
					&& cs[0] != '"' // no quoted expression as a keyword
					&& cs.GetLength() >= 3
					&& s_strCurKadKeywordA.GetLength() < cs.GetLength())
				{
					s_strCurKadKeywordA = cs;
				}
			}
		}

		CStringA strAndTerms;
		for (INT_PTR i = 0; i < expr.m_aExpr.GetCount(); ++i) {
			const CStringA &cs(expr.m_aExpr[i].m_str);
			if (cs != SEARCHOPTOK_AND) {
				ASSERT(expr.m_aExpr[i].m_iTag == FT_FILENAME);
				// Minor optimization: Because we added the Kad keyword to the boolean search expression,
				// we remove it here (and only here) again because we know that the entire search expression
				// does only contain (implicit) ANDed strings.
				if (cs != s_strCurKadKeywordA) {
					if (!strAndTerms.IsEmpty())
						strAndTerms += ' ';
					strAndTerms += cs;
				}
			}
		}
		ASSERT(s_SearchExpr.m_aExpr.IsEmpty());
		s_SearchExpr.m_aExpr.Add(CSearchAttr(strAndTerms));
	} else if (expr.m_aExpr.GetCount() != 1
			|| !(expr.m_aExpr[0].m_iTag == FT_FILENAME && expr.m_aExpr[0].m_str == s_strCurKadKeywordA))
	{
		s_SearchExpr.m_aExpr.Append(expr.m_aExpr);
	}
}

class CSearchExprTarget
{
public:
	CSearchExprTarget(CSafeMemFile &data, EUTF8str eStrEncode, bool bSupports64Bit, bool *pbPacketUsing64Bit)
		: m_data(data)
		, m_pbPacketUsing64Bit(pbPacketUsing64Bit)
		, m_eStrEncode(eStrEncode)
		, m_bSupports64Bit(bSupports64Bit)
	{
		if (m_pbPacketUsing64Bit)
			*m_pbPacketUsing64Bit = false;
	}

	const CString& GetDebugString() const
	{
		return m_strDbg;
	}

	void WriteBooleanAND()
	{
		m_data.WriteUInt8(0);						// boolean operator parameter type
		m_data.WriteUInt8(0x00);					// "AND"
		m_strDbg.AppendFormat(_T("AND "));
	}

	void WriteBooleanOR()
	{
		m_data.WriteUInt8(0);						// boolean operator parameter type
		m_data.WriteUInt8(0x01);					// "OR"
		m_strDbg.AppendFormat(_T("OR "));
	}

	void WriteBooleanNOT()
	{
		m_data.WriteUInt8(0);						// boolean operator parameter type
		m_data.WriteUInt8(0x02);					// "NOT"
		m_strDbg.AppendFormat(_T("NOT "));
	}

	void WriteMetaDataSearchParam(const CString &rstrValue)
	{
		m_data.WriteUInt8(1);						// string parameter type
		m_data.WriteString(rstrValue, m_eStrEncode); // string value
		m_strDbg.AppendFormat(_T("\"%s\" "), (LPCTSTR)rstrValue);
	}

	void WriteMetaDataSearchParam(UINT uMetaTagID, const CString &rstrValue)
	{
		m_data.WriteUInt8(2);						// string parameter type
		m_data.WriteString(rstrValue, m_eStrEncode); // string value
		m_data.WriteUInt16(sizeof(uint8));			// meta tag ID length
		m_data.WriteUInt8((uint8)uMetaTagID);		// meta tag ID name
		m_strDbg.AppendFormat(_T("%s=\"%s\" "), (LPCTSTR)DbgGetFileMetaTagName(uMetaTagID), (LPCTSTR)rstrValue);
	}

	void WriteMetaDataSearchParamA(UINT uMetaTagID, const CStringA &rstrValueA)
	{
		m_data.WriteUInt8(2);						// string parameter type
		m_data.WriteString(rstrValueA);			// string value
		m_data.WriteUInt16(sizeof(uint8));			// meta tag ID length
		m_data.WriteUInt8((uint8)uMetaTagID);		// meta tag ID name
		m_strDbg.AppendFormat(_T("%s=\"%hs\" "), (LPCTSTR)DbgGetFileMetaTagName(uMetaTagID), (LPCSTR)rstrValueA);
	}

	void WriteMetaDataSearchParam(LPCSTR pszMetaTagID, const CString &rstrValue)
	{
		m_data.WriteUInt8(2);						// string parameter type
		m_data.WriteString(rstrValue, m_eStrEncode); // string value
		m_data.WriteString(pszMetaTagID);			// meta tag ID
		m_strDbg.AppendFormat(_T("%s=\"%s\" "), (LPCTSTR)DbgGetFileMetaTagName(pszMetaTagID), (LPCTSTR)rstrValue);
	}

	void WriteMetaDataSearchParam(UINT uMetaTagID, UINT uOperator, uint64 ullValue)
	{
		bool b64BitValue = ullValue > 0xFFFFFFFFui64;
		if (b64BitValue && m_bSupports64Bit) {
			if (m_pbPacketUsing64Bit)
				*m_pbPacketUsing64Bit = true;
			m_data.WriteUInt8(8);					// numeric parameter type (int64)
			m_data.WriteUInt64(ullValue);			// numeric value
		} else {
			if (b64BitValue)
				ullValue = _UI32_MAX;
			m_data.WriteUInt8(3);					// numeric parameter type (int32)
			m_data.WriteUInt32((uint32)ullValue);	// numeric value
		}
		m_data.WriteUInt8((uint8)uOperator);		// comparison operator
		m_data.WriteUInt16(sizeof(uint8));			// meta tag ID length
		m_data.WriteUInt8((uint8)uMetaTagID);		// meta tag ID name
		m_strDbg.AppendFormat(_T("%s%s%I64u "), (LPCTSTR)DbgGetFileMetaTagName(uMetaTagID), (LPCTSTR)DbgGetSearchOperatorName(uOperator), ullValue);
	}

	void WriteMetaDataSearchParam(LPCSTR pszMetaTagID, UINT uOperator, uint64 ullValue)
	{
		bool b64BitValue = ullValue > _UI32_MAX;
		if (b64BitValue && m_bSupports64Bit) {
			if (m_pbPacketUsing64Bit)
				*m_pbPacketUsing64Bit = true;
			m_data.WriteUInt8(8);					// numeric parameter type (int64)
			m_data.WriteUInt64(ullValue);			// numeric value
		} else {
			if (b64BitValue)
				ullValue = _UI32_MAX;
			m_data.WriteUInt8(3);					// numeric parameter type (int32)
			m_data.WriteUInt32((uint32)ullValue);	// numeric value
		}
		m_data.WriteUInt8((uint8)uOperator);		// comparison operator
		m_data.WriteString(pszMetaTagID);			// meta tag ID
		m_strDbg.AppendFormat(_T("%s%s%I64u "), (LPCTSTR)DbgGetFileMetaTagName(pszMetaTagID), (LPCTSTR)DbgGetSearchOperatorName(uOperator), ullValue);
	}

protected:
	CSafeMemFile &m_data;
	CString m_strDbg;
	bool *m_pbPacketUsing64Bit;
	EUTF8str m_eStrEncode;
	bool m_bSupports64Bit;
};

static CSearchExpr s_SearchExpr2;

static void AddAndAttr(UINT uTag, const CString &rstr)
{
	s_SearchExpr2.m_aExpr.InsertAt(0, CSearchAttr(uTag, StrToUtf8(rstr)));
	if (s_SearchExpr2.m_aExpr.GetCount() > 1)
		s_SearchExpr2.m_aExpr.InsertAt(0, CSearchAttr(SEARCHOPTOK_AND));
}

static void AddAndAttr(UINT uTag, UINT uOpr, uint64 ullVal)
{
	s_SearchExpr2.m_aExpr.InsertAt(0, CSearchAttr(uTag, uOpr, ullVal));
	if (s_SearchExpr2.m_aExpr.GetCount() > 1)
		s_SearchExpr2.m_aExpr.InsertAt(0, CSearchAttr(SEARCHOPTOK_AND));
}

bool GetSearchPacket(CSafeMemFile &data, SSearchParams *pParams, bool bTargetSupports64Bit, bool *pbPacketUsing64Bit)
{
	LPCTSTR pFileType;
	if (pParams->strFileType == _T(ED2KFTSTR_ARCHIVE)) {
		// eDonkeyHybrid 0.48 uses type "Pro" for archives files
		// www.filedonkey.com used type "Pro" for archives files
		pFileType = _T(ED2KFTSTR_PROGRAM);
	} else if (pParams->strFileType == _T(ED2KFTSTR_CDIMAGE)) {
		// eDonkeyHybrid 0.48 uses *no* type for iso/nrg/cue/img files
		// www.filedonkey.com used type "Pro" for CD-image files
		pFileType = _T(ED2KFTSTR_PROGRAM);
	} else {
		//TODO: Support "Doc" types
		pFileType = pParams->strFileType;
	}
	const CString &strFileType(pFileType);

	s_strCurKadKeywordA.Empty();
	ASSERT(!pParams->strExpression.IsEmpty());
	if (pParams->eType == SearchTypeKademlia) {
		ASSERT(!pParams->strKeyword.IsEmpty());
		s_strCurKadKeywordA = StrToUtf8(pParams->strKeyword);
	}
	if (pParams->strBooleanExpr.IsEmpty())
		pParams->strBooleanExpr = pParams->strExpression;
	if (pParams->strBooleanExpr.IsEmpty())
		return false;

	//TRACE(_T("Raw search expr:\n"));
	//TRACE(_T("%s"), pParams->strBooleanExpr);
	//TRACE(_T("  %s\n"), (LPCTSTR)DbgGetHexDump((uchar*)(LPCTSTR)pParams->strBooleanExpr, pParams->strBooleanExpr.GetLength()*sizeof(TCHAR)));
	g_astrParserErrors.RemoveAll();
	s_SearchExpr.m_aExpr.RemoveAll();
	if (!pParams->strBooleanExpr.IsEmpty()) {
		LexInit(pParams->strBooleanExpr, true);
		int iParseResult = yyparse();
		LexFree();
		if (!g_astrParserErrors.IsEmpty()) {
			s_SearchExpr.m_aExpr.RemoveAll();
			CString strError(GetResString(IDS_SEARCH_EXPRERROR));
			strError.AppendFormat(_T("\n\n%s"), (LPCTSTR)g_astrParserErrors[g_astrParserErrors.GetCount() - 1]);
			throw new CMsgBoxException(strError, MB_ICONWARNING | MB_HELP, eMule_FAQ_Search - HID_BASE_PROMPT);
		}
		if (iParseResult != 0) {
			s_SearchExpr.m_aExpr.RemoveAll();
			CString strError(GetResString(IDS_SEARCH_EXPRERROR));
			strError.AppendFormat(_T("\n\n%s"), (LPCTSTR)GetResString(IDS_SEARCH_GENERALERROR));
			throw new CMsgBoxException(strError, MB_ICONWARNING | MB_HELP, eMule_FAQ_Search - HID_BASE_PROMPT);
		}

		if (pParams->eType == SearchTypeKademlia && s_strCurKadKeywordA != StrToUtf8(pParams->strKeyword)) {
			DebugLog(_T("KadSearch: Keyword was rearranged, using %s instead of %s"), (LPCTSTR)OptUtf8ToStr(s_strCurKadKeywordA), (LPCTSTR)pParams->strKeyword);
			pParams->strKeyword = OptUtf8ToStr(s_strCurKadKeywordA);
		}
	}
	//TRACE(_T("Parsed search expr:\n"));
	//for (INT_PTR i = 0; i < s_SearchExpr.m_aExpr.GetCount(); ++i) {
	//	TRACE(_T("%hs"), s_SearchExpr.m_aExpr[i]);
	//	TRACE(_T("  %s\n"), (LPCTSTR)DbgGetHexDump((uchar*)(LPCSTR)s_SearchExpr.m_aExpr[i], s_SearchExpr.m_aExpr[i].GetLength()*sizeof(CHAR)));
	//}

	// create ed2k search expression
	CSearchExprTarget target(data, UTF8strRaw, bTargetSupports64Bit, pbPacketUsing64Bit);

	s_SearchExpr2.m_aExpr.RemoveAll();

	if (!pParams->strExtension.IsEmpty())
		AddAndAttr(FT_FILEFORMAT, pParams->strExtension);

	if (pParams->uAvailability > 0)
		AddAndAttr(FT_SOURCES, ED2K_SEARCH_OP_GREATER_EQUAL, pParams->uAvailability);

	if (pParams->ullMaxSize > 0)
		AddAndAttr(FT_FILESIZE, ED2K_SEARCH_OP_LESS_EQUAL, pParams->ullMaxSize);

	if (pParams->ullMinSize > 0)
		AddAndAttr(FT_FILESIZE, ED2K_SEARCH_OP_GREATER_EQUAL, pParams->ullMinSize);

	if (!strFileType.IsEmpty())
		AddAndAttr(FT_FILETYPE, strFileType);

	if (pParams->uComplete > 0)
		AddAndAttr(FT_COMPLETE_SOURCES, ED2K_SEARCH_OP_GREATER_EQUAL, pParams->uComplete);

	if (pParams->uiMinBitrate > 0)
		AddAndAttr(FT_MEDIA_BITRATE, ED2K_SEARCH_OP_GREATER_EQUAL, pParams->uiMinBitrate);

	if (pParams->uiMinLength > 0)
		AddAndAttr(FT_MEDIA_LENGTH, ED2K_SEARCH_OP_GREATER_EQUAL, pParams->uiMinLength);

	if (!pParams->strCodec.IsEmpty())
		AddAndAttr(FT_MEDIA_CODEC, pParams->strCodec);

	if (!pParams->strTitle.IsEmpty())
		AddAndAttr(FT_MEDIA_TITLE, pParams->strTitle);

	if (!pParams->strAlbum.IsEmpty())
		AddAndAttr(FT_MEDIA_ALBUM, pParams->strAlbum);

	if (!pParams->strArtist.IsEmpty())
		AddAndAttr(FT_MEDIA_ARTIST, pParams->strArtist);

	if (!s_SearchExpr2.m_aExpr.IsEmpty()) {
		if (!s_SearchExpr.m_aExpr.IsEmpty())
			s_SearchExpr.m_aExpr.InsertAt(0, CSearchAttr(SEARCHOPTOK_AND));
		s_SearchExpr.Add(&s_SearchExpr2);
	}

	if (thePrefs.GetVerbose()) {
		s_strSearchTree.Empty();
		DumpSearchTree(s_SearchExpr, true);
		DebugLog(_T("Search Expr: %s"), (LPCTSTR)s_strSearchTree);
	}

	for (INT_PTR i = 0; i < s_SearchExpr.m_aExpr.GetCount(); ++i) {
		const CSearchAttr &rSearchAttr(s_SearchExpr.m_aExpr[i]);
		const CStringA &rstrA(rSearchAttr.m_str);
		if (rstrA == SEARCHOPTOK_AND)
			target.WriteBooleanAND();
		else if (rstrA == SEARCHOPTOK_OR)
			target.WriteBooleanOR();
		else if (rstrA == SEARCHOPTOK_NOT)
			target.WriteBooleanNOT();
		else
			switch (rSearchAttr.m_iTag) {
			case FT_FILESIZE:
			case FT_SOURCES:
			case FT_COMPLETE_SOURCES:
			case FT_FILERATING:
			case FT_MEDIA_BITRATE:
			case FT_MEDIA_LENGTH:
				// 11-Sep-2005 []: Kad comparison operators where changed to match the ED2K operators. For backward
				// compatibility with old Kad nodes, we map ">=val" and "<=val" to ">val-1" and "<val+1".
				// This way, the older Kad nodes will perform a ">=val" and "<=val".
				//
				// TODO: This should be removed in couple of months!
				//if (rSearchAttr.m_uIntegerOperator == ED2K_SEARCH_OP_GREATER_EQUAL)
				//	target.WriteMetaDataSearchParam(rSearchAttr.m_iTag, ED2K_SEARCH_OP_GREATER, rSearchAttr.m_nNum - 1);
				//else if (rSearchAttr.m_uIntegerOperator == ED2K_SEARCH_OP_LESS_EQUAL)
				//	target.WriteMetaDataSearchParam(rSearchAttr.m_iTag, ED2K_SEARCH_OP_LESS, rSearchAttr.m_nNum + 1);
				//else
				target.WriteMetaDataSearchParam(rSearchAttr.m_iTag, rSearchAttr.m_uIntegerOperator, rSearchAttr.m_nNum);
				break;
			case FT_FILETYPE:
			case FT_FILEFORMAT:
			case FT_MEDIA_CODEC:
			case FT_MEDIA_TITLE:
			case FT_MEDIA_ALBUM:
			case FT_MEDIA_ARTIST:
				ASSERT(rSearchAttr.m_uIntegerOperator == ED2K_SEARCH_OP_EQUAL);
				target.WriteMetaDataSearchParam(rSearchAttr.m_iTag, OptUtf8ToStr(rSearchAttr.m_str));
				break;
			default:
				ASSERT(rSearchAttr.m_iTag == FT_FILENAME);
				ASSERT(rSearchAttr.m_uIntegerOperator == ED2K_SEARCH_OP_EQUAL);
				target.WriteMetaDataSearchParam(OptUtf8ToStr(rstrA));
			}
	}

	if (thePrefs.GetDebugServerSearchesLevel() > 0)
		Debug(_T("Search Data: %s\n"), (LPCTSTR)target.GetDebugString());
	s_SearchExpr.m_aExpr.RemoveAll();
	s_SearchExpr2.m_aExpr.RemoveAll();
	return true;
}

bool CSearchResultsWnd::StartNewSearch(SSearchParams *pParams)
{

	if (pParams->eType == SearchTypeAutomatic) {
		// select between kad and server
		// its easy if we are connected to one network only
		if (!theApp.serverconnect->IsConnected() && Kademlia::CKademlia::IsRunning() && Kademlia::CKademlia::IsConnected())
			pParams->eType = SearchTypeKademlia;
		else if (theApp.serverconnect->IsConnected() && (!Kademlia::CKademlia::IsRunning() || !Kademlia::CKademlia::IsConnected()))
			pParams->eType = SearchTypeEd2kServer;
		else {
			if (!theApp.serverconnect->IsConnected() && (!Kademlia::CKademlia::IsRunning() || !Kademlia::CKademlia::IsConnected())) {
				LocMessageBox(IDS_NOTCONNECTEDANY, MB_ICONWARNING, 0);
				delete pParams;
				return false;
			}
			// connected to both
			// We choose Kad, except
			// - if we are connected to a static server
			// - or a server with more than 40k and less than 2mio users connected,
			//      more than 5 mio files and if our serverlist contains less than 40 servers
			//      (otherwise we have assume that its polluted with fake servers and we might
			//      just as well to be connected to one)
			// might be further optimized in the future
			const CServer *curserv = theApp.serverconnect->GetCurrentServer();
			pParams->eType = ( theApp.serverconnect->IsConnected() && curserv != NULL
				&& (curserv->IsStaticMember()
					|| (curserv->GetUsers() > 40000
						&& theApp.serverlist->GetServerCount() < 40
						&& curserv->GetUsers() < 2000000 //was 5M - copy & paste bug
						&& curserv->GetFiles() > 5000000))
				)
				? SearchTypeEd2kServer : SearchTypeKademlia;
		}
	}

	switch (pParams->eType) {
	case SearchTypeEd2kServer:
	case SearchTypeEd2kGlobal:
		if (!theApp.serverconnect->IsConnected()) {
			LocMessageBox(IDS_ERR_NOTCONNECTED, MB_ICONWARNING, 0);
			break;
		}

		try {
			if (!DoNewEd2kSearch(pParams))
				break;
		} catch (CMsgBoxException *ex) {
			AfxMessageBox(ex->m_strMsg, ex->m_uType, ex->m_uHelpID);
			ex->Delete();
			break;
		}

		SearchStarted();
		return true;
	case SearchTypeKademlia:
		if (!Kademlia::CKademlia::IsRunning() || !Kademlia::CKademlia::IsConnected()) {
			LocMessageBox(IDS_ERR_NOTCONNECTEDKAD, MB_ICONWARNING, 0);
			break;
		}

		try {
			if (!DoNewKadSearch(pParams))
				break;
		} catch (CMsgBoxException *ex) {
			AfxMessageBox(ex->m_strMsg, ex->m_uType, ex->m_uHelpID);
			ex->Delete();
			break;
		}

		SearchStarted();
		return true;
	default:
		ASSERT(0);
	}

	delete pParams;
	return false;
}

bool CSearchResultsWnd::DoNewEd2kSearch(SSearchParams *pParams)
{
	if (!theApp.serverconnect->IsConnected())
		return false;

	delete m_searchpacket;
	m_searchpacket = NULL;
	bool bServerSupports64Bit = theApp.serverconnect->GetCurrentServer() != NULL
		&& (theApp.serverconnect->GetCurrentServer()->GetTCPFlags() & SRV_TCPFLG_LARGEFILES);
	bool bPacketUsing64Bit = false;
	CSafeMemFile data(100);
	if (!GetSearchPacket(data, pParams, bServerSupports64Bit, &bPacketUsing64Bit) || data.GetLength() == 0)
		return false;

	CancelEd2kSearch();

	CString strResultType(pParams->strFileType);
	if (strResultType == _T(ED2KFTSTR_PROGRAM))
		strResultType.Empty();

	if (pParams->dwSearchID == _UI32_MAX)
		pParams->dwSearchID = GetNextSearchID();
	m_nEd2kSearchID = pParams->dwSearchID;
	theApp.searchlist->NewSearch(&searchlistctrl, strResultType, pParams);
	m_cancelled = false;

	if (m_uTimerLocalServer) {
		VERIFY(KillTimer(m_uTimerLocalServer));
		m_uTimerLocalServer = 0;
	}

	// sending a new search request invalidates any previously received 'More'
	const CWnd *pWndFocus = GetFocus();
	m_pwndParams->m_ctlMore.EnableWindow(FALSE);
	if (pWndFocus && pWndFocus->m_hWnd == m_pwndParams->m_ctlMore.m_hWnd)
		m_pwndParams->m_ctlCancel.SetFocus();
	m_iSentMoreReq = 0;

	Packet *packet = new Packet(data);
	packet->opcode = OP_SEARCHREQUEST;
	if (thePrefs.GetDebugServerTCPLevel() > 0)
		Debug(_T(">>> Sending OP_SearchRequest\n"));
	theStats.AddUpDataOverheadServer(packet->size);
	m_globsearch = pParams->eType == SearchTypeEd2kGlobal && theApp.serverconnect->IsUDPSocketAvailable();
	if (m_globsearch)
		m_searchpacket = new Packet(*packet);
	theApp.serverconnect->SendPacket(packet);

	if (m_globsearch) {
		// set timeout timer for local server
		m_uTimerLocalServer = SetTimer(TimerServerTimeout, SEC2MS(50), NULL);

		if (thePrefs.GetUseServerPriorities())
			theApp.serverlist->ResetSearchServerPos();

		m_searchpacket->opcode = OP_GLOBSEARCHREQ; // will be changed later when actually sending the packet!!
		m_b64BitSearchPacket = bPacketUsing64Bit;
		m_servercount = 0;
		searchprogress.SetRange32(0, (int)theApp.serverlist->GetServerCount() - 1);
	}
	CreateNewTab(pParams);
	return true;
}

bool CSearchResultsWnd::SearchMore()
{
	if (!theApp.serverconnect->IsConnected())
		return false;

	SetActiveSearchResultsIcon(m_nEd2kSearchID);
	m_cancelled = false;

	Packet *packet = new Packet();
	packet->opcode = OP_QUERY_MORE_RESULT;
	if (thePrefs.GetDebugServerTCPLevel() > 0)
		Debug(_T(">>> Sending OP_QueryMoreResults\n"));
	theStats.AddUpDataOverheadServer(packet->size);
	theApp.serverconnect->SendPacket(packet);
	++m_iSentMoreReq;
	return true;
}

bool CSearchResultsWnd::DoNewKadSearch(SSearchParams *pParams)
{
	if (!Kademlia::CKademlia::IsConnected())
		return false;

	int iPos = 0;
	pParams->strKeyword = pParams->strExpression.Tokenize(_T(" "), iPos);
	if (pParams->strKeyword[0] == _T('"')) {
		// remove leading and possibly trailing quotes, if they terminate properly (otherwise the keyword is later handled as invalid)
		// (quotes are still kept in search expr and matched against the result, so everything is fine)
		const int iLen = pParams->strKeyword.GetLength();
		if (iLen > 1 && pParams->strKeyword[iLen - 1] == _T('"'))
			pParams->strKeyword = pParams->strKeyword.Mid(1, iLen - 2);
		else if (pParams->strExpression.Find(_T('"'), 1) > iLen)
			pParams->strKeyword = pParams->strKeyword.Mid(1, iLen - 1);
	}
	pParams->strKeyword.Trim();

	CSafeMemFile data(100);
	if (!GetSearchPacket(data, pParams, true, NULL)/* || (!pParams->strBooleanExpr.IsEmpty() && data.GetLength() == 0)*/)
		return false;

	if (pParams->strKeyword.IsEmpty() || pParams->strKeyword.FindOneOf(g_aszInvKadKeywordChars) >= 0) {
		CString strError;
		strError.Format(GetResString(IDS_KAD_SEARCH_KEYWORD_INVALID), g_aszInvKadKeywordChars);
		throw new CMsgBoxException(strError, MB_ICONWARNING | MB_HELP, eMule_FAQ_Search - HID_BASE_PROMPT);
	}

	LPBYTE pSearchTermsData = NULL;
	UINT uSearchTermsSize = (UINT)data.GetLength();
	if (uSearchTermsSize) {
		pSearchTermsData = new BYTE[uSearchTermsSize];
		data.SeekToBegin();
		data.Read(pSearchTermsData, uSearchTermsSize);
	}

	Kademlia::CSearch *pSearch = NULL;
	try {
		pSearch = Kademlia::CSearchManager::PrepareFindKeywords(
			pParams->strKeyword,
			uSearchTermsSize,
			pSearchTermsData,
			pParams->dwSearchID != _UI32_MAX ? pParams->dwSearchID : 0);
		delete[] pSearchTermsData;
		pSearchTermsData = NULL;
		if (!pSearch) {
			ASSERT(0);
			return false;
		}
	} catch (const CString &strException) {
		delete[] pSearchTermsData;
		throw new CMsgBoxException(strException, MB_ICONWARNING | MB_HELP, eMule_FAQ_Search - HID_BASE_PROMPT);
	}
	pParams->dwSearchID = pSearch->GetSearchID();
	CString strResultType(pParams->strFileType);
	if (strResultType == ED2KFTSTR_PROGRAM)
		strResultType.Empty();
	theApp.searchlist->NewSearch(&searchlistctrl, strResultType, pParams);
	CreateNewTab(pParams);
	return true;
}

bool CSearchResultsWnd::CreateNewTab(SSearchParams *pParams, bool bActiveIcon)
{
	TCITEM ti;
	ti.mask = TCIF_PARAM;
	for (int i = searchselect.GetItemCount(); --i >= 0;)
		if (searchselect.GetItem(i, &ti) && ti.lParam != NULL && reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID == pParams->dwSearchID)
			return false;

	// add a new tab
	if (pParams->strExpression.IsEmpty())
		pParams->strExpression += _T('-');
	ti.mask = TCIF_PARAM | TCIF_TEXT | TCIF_IMAGE;
	ti.lParam = (LPARAM)pParams;
	pParams->strSearchTitle = (pParams->strSpecialTitle.IsEmpty() ? pParams->strExpression : pParams->strSpecialTitle);
	CString strTcLabel(pParams->strSearchTitle);
	DupAmpersand(strTcLabel);
	ti.pszText = const_cast<LPTSTR>((LPCTSTR)strTcLabel);
	ti.cchTextMax = 0;
	if (pParams->bClientSharedFiles)
		ti.iImage = sriClient;
	else if (pParams->eType == SearchTypeKademlia)
		ti.iImage = bActiveIcon ? sriKadActice : sriKad;
	else if (pParams->eType == SearchTypeEd2kGlobal)
		ti.iImage = bActiveIcon ? sriGlobalActive : sriGlobal;
	else {
		ASSERT(pParams->eType == SearchTypeEd2kServer);
		ti.iImage = bActiveIcon ? sriServerActive : sriServer;
	}
	int itemnr = searchselect.InsertItem(INT_MAX, &ti);
	if (!searchselect.IsWindowVisible())
		ShowSearchSelector(true);
	LRESULT lResult;
	OnSelChangingTab(NULL, &lResult);
	searchselect.SetCurSel(itemnr);
	searchlistctrl.ShowResults(pParams->dwSearchID);
	return true;
}

void CSearchResultsWnd::DeleteSelectedSearch()
{
	if (CanDeleteSearches()) {
		int iFocus = searchselect.GetCurFocus();
		TCITEM ti;
		ti.mask = TCIF_PARAM;
		if (iFocus >= 0 && searchselect.GetItem(iFocus, &ti) && ti.lParam != NULL)
			DeleteSearch(reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID);
	}
}

#pragma warning(push)
#pragma warning(disable:4701) //local variable 'ti'
void CSearchResultsWnd::DetachActiveResultView(uint32 uSearchID)
{
	int iCurSel = searchselect.GetCurSel();
	if (iCurSel < 0)
		return;

	TCITEM ti;
	ti.mask = TCIF_PARAM;
	if (!searchselect.GetItem(iCurSel, &ti) || ti.lParam == NULL)
		return;

	if (reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID == uSearchID)
		searchlistctrl.DeleteAllItems();
}

void CSearchResultsWnd::DeleteSearch(uint32 uSearchID)
{
	RemoveQueuedSearch(uSearchID);
	Kademlia::CSearchManager::StopSearch(uSearchID, false);

	TCITEM ti;
	ti.mask = TCIF_PARAM;
	int i = searchselect.GetItemCount();
	while (--i >= 0 && !(searchselect.GetItem(i, &ti) && ti.lParam != NULL && reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID == uSearchID));
	if (i < 0)
		return;

	// delete search results
	if (uSearchID == m_nEd2kSearchID) {
		if (!m_cancelled)
			CancelEd2kSearch();
		m_pwndParams->m_ctlMore.EnableWindow(FALSE);
	}
	DetachActiveResultView(uSearchID);
	theApp.searchlist->RemoveResults(uSearchID);

	// clean up stored states (scrolling pos. etc) for this search
	searchlistctrl.ClearResultViewState(uSearchID);

	// delete search tab
	SSearchParams *pClosedParams = reinterpret_cast<SSearchParams*>(ti.lParam);
	int iCurSel = searchselect.GetCurSel();
	searchselect.DeleteItem(i);

	int iTabItems = searchselect.GetItemCount();
	if (iTabItems > 0) {
		// select next search tab
		if (iCurSel == CB_ERR)
			iCurSel = 0;
		else if (iCurSel >= iTabItems)
			iCurSel = iTabItems - 1;
		(void)searchselect.SetCurSel(iCurSel);	// returns CB_ERR if error or no prev. selection(!)
		iCurSel = searchselect.GetCurSel();		// get the real current selection
		if (iCurSel == CB_ERR)					// if still error
			iCurSel = searchselect.SetCurSel(0);
		if (iCurSel != CB_ERR) {
			ti.mask = TCIF_PARAM;
			if (searchselect.GetItem(iCurSel, &ti) && ti.lParam != NULL) {
				searchselect.HighlightItem(iCurSel, FALSE);
				ShowResults(reinterpret_cast<SSearchParams*>(ti.lParam));
			}
		}
	} else
		NoTabItems();

	delete pClosedParams;
}
#pragma warning(pop)

void CSearchResultsWnd::DeleteAllSearches()
{
	ClearQueuedSearches();
	CancelEd2kSearch();

	CTypedPtrList<CPtrList, SSearchParams*> listSearchParamsToDelete;
	TCITEM ti;
	ti.mask = TCIF_PARAM;
	for (int i = searchselect.GetItemCount(); --i >= 0;)
		if (searchselect.GetItem(i, &ti) && ti.lParam != NULL) {
			SSearchParams *params = reinterpret_cast<SSearchParams*>(ti.lParam);
			Kademlia::CSearchManager::StopSearch(params->dwSearchID, false);
			listSearchParamsToDelete.AddTail(params);
		}
	Kademlia::CSearchManager::StopAllKeywordSearches();

	NoTabItems();

	while (!listSearchParamsToDelete.IsEmpty())
		delete listSearchParamsToDelete.RemoveHead();
}

void CSearchResultsWnd::NoTabItems()
{
	searchlistctrl.DeleteAllItems();
	ShowSearchSelector(false);
	searchselect.DeleteAllItems();
	searchlistctrl.NoTabs();
	theApp.searchlist->Clear();

	const CWnd *pWndFocus = GetFocus();
	m_pwndParams->m_ctlMore.EnableWindow(FALSE);
	m_pwndParams->m_ctlCancel.EnableWindow(FALSE);
	m_pwndParams->m_ctlStart.EnableWindow(m_pwndParams->m_ctlName.GetWindowTextLength() > 0);
	if (pWndFocus) {
		if (pWndFocus->m_hWnd == m_pwndParams->m_ctlMore.m_hWnd || pWndFocus->m_hWnd == m_pwndParams->m_ctlCancel.m_hWnd) {
			if (m_pwndParams->m_ctlStart.IsWindowEnabled())
				m_pwndParams->m_ctlStart.SetFocus();
			else
				m_pwndParams->m_ctlName.SetFocus();
		} else if (pWndFocus->m_hWnd == m_pwndParams->m_ctlStart.m_hWnd && !m_pwndParams->m_ctlStart.IsWindowEnabled())
			m_pwndParams->m_ctlName.SetFocus();
	}
}

void CSearchResultsWnd::ShowResults(const SSearchParams *pParams)
{
	// restoring the params works and is nice during development/testing but pretty annoying in practice.
	// TODO: maybe it should be done explicitly via a context menu function or such.
	if (GetKeyState(VK_CONTROL) < 0)
		m_pwndParams->SetParameters(pParams);

	if (pParams->eType == SearchTypeEd2kServer)
		m_pwndParams->m_ctlCancel.EnableWindow(IsSearchQueued(pParams->dwSearchID) || (pParams->dwSearchID == m_nEd2kSearchID && IsLocalEd2kSearchRunning()));
	else if (pParams->eType == SearchTypeEd2kGlobal)
		m_pwndParams->m_ctlCancel.EnableWindow(IsSearchQueued(pParams->dwSearchID) || (pParams->dwSearchID == m_nEd2kSearchID && (IsLocalEd2kSearchRunning() || IsGlobalEd2kSearchRunning())));
	else if (pParams->eType == SearchTypeKademlia)
		m_pwndParams->m_ctlCancel.EnableWindow(IsSearchQueued(pParams->dwSearchID) || Kademlia::CSearchManager::IsSearching(pParams->dwSearchID));

	searchlistctrl.ShowResults(pParams->dwSearchID);
}

void CSearchResultsWnd::OnSelChangeTab(LPNMHDR, LRESULT *pResult)
{
	CWaitCursor curWait; // this may take a while
	int cur_sel = searchselect.GetCurSel();
	if (cur_sel >= 0) {
		TCITEM ti;
		ti.mask = TCIF_PARAM;
		if (searchselect.GetItem(cur_sel, &ti) && ti.lParam != NULL) {
			searchselect.HighlightItem(cur_sel, FALSE);
			ShowResults(reinterpret_cast<SSearchParams*>(ti.lParam));
		}
	}
	*pResult = 0;
}

void CSearchResultsWnd::OnSelChangingTab(LPNMHDR, LRESULT *pResult)
{
	if (!m_astrFilter.IsEmpty()) {
		int cur_sel = searchselect.GetCurSel();
		if (cur_sel >= 0) {
			CString strTabLabel;
			TCITEM ti;
			ti.cchTextMax = 512;
			ti.pszText = strTabLabel.GetBuffer(ti.cchTextMax);
			ti.mask = TCIF_TEXT;
			bool b = searchselect.GetItem(cur_sel, &ti);
			strTabLabel.ReleaseBuffer();
			if (b) {
				int i = strTabLabel.ReverseFind(_T('/'));
				int j = strTabLabel.ReverseFind(_T('('));
				if (j >= 0 && i > j) {
					strTabLabel.Delete(j + 1, i - j);
					DupAmpersand(strTabLabel);
					ti.pszText = const_cast<LPTSTR>((LPCTSTR)strTabLabel);
					searchselect.SetItem(cur_sel, &ti);
				}
			}
		}
	}
	*pResult = 0;
}

LRESULT CSearchResultsWnd::OnCloseTab(WPARAM wParam, LPARAM)
{
	TCITEM ti;
	ti.mask = TCIF_PARAM;
	if (searchselect.GetItem((int)wParam, &ti) && ti.lParam != NULL) {
		uint32 uSearchID = reinterpret_cast<SSearchParams*>(ti.lParam)->dwSearchID;
		if (uSearchID == m_nEd2kSearchID && !m_cancelled)
			CancelEd2kSearch();
		else if (reinterpret_cast<SSearchParams*>(ti.lParam)->bClientSharedFiles) {
			CUpDownClient *client = theApp.clientlist->FindClientBySearchID(uSearchID);
			if (client)
				client->SetFileListRequested(0); //if data is still coming - discard
		}
		DeleteSearch(uSearchID);
	}
	return TRUE;
}

LRESULT CSearchResultsWnd::OnDblClickTab(WPARAM wParam, LPARAM)
{
	TCITEM ti;
	ti.mask = TCIF_PARAM;
	if (searchselect.GetItem((int)wParam, &ti) && ti.lParam != NULL)
		m_pwndParams->SetParameters(reinterpret_cast<SSearchParams*>(ti.lParam));
	return TRUE;
}

void CSearchResultsWnd::UpdateCatTabs()
{
	int oldsel = m_cattabs.GetCurSel();
	m_cattabs.DeleteAllItems();
	for (INT_PTR i = 0; i < thePrefs.GetCatCount(); ++i) {
		CString label(i ? thePrefs.GetCategory(i)->strTitle : GetResString(IDS_ALL));
		DupAmpersand(label);
		m_cattabs.InsertItem((int)i, label);
	}
	if (oldsel >= m_cattabs.GetItemCount() || oldsel < 0)
		oldsel = 0;

	m_cattabs.SetCurSel(oldsel);
	int flag = (m_cattabs.GetItemCount() > 1) ? SW_SHOW : SW_HIDE;
	m_cattabs.ShowWindow(flag);
	GetDlgItem(IDC_STATIC_DLTOof)->ShowWindow(flag);
}

void CSearchResultsWnd::ShowSearchSelector(bool visible)
{
	WINDOWPLACEMENT wpTabSelect, wpList;
	searchselect.GetWindowPlacement(&wpTabSelect);
	searchlistctrl.GetWindowPlacement(&wpList);

	int nCmdShow;
	if (visible) {
		nCmdShow = SW_SHOW;
		wpList.rcNormalPosition.top = wpTabSelect.rcNormalPosition.bottom;
	} else {
		nCmdShow = SW_HIDE;
		wpList.rcNormalPosition.top = wpTabSelect.rcNormalPosition.top;
	}
	searchselect.ShowWindow(nCmdShow);
	RemoveAnchor(searchlistctrl);
	searchlistctrl.SetWindowPlacement(&wpList);
	AddAnchor(searchlistctrl, TOP_LEFT, BOTTOM_RIGHT);
	GetDlgItem(IDC_CLEARALL)->ShowWindow(nCmdShow);
	m_ctlFilter.ShowWindow(nCmdShow);
}

void CSearchResultsWnd::OnDestroy()
{
	TCITEM ti;
	ti.mask = TCIF_PARAM;
	for (INT_PTR i = searchselect.GetItemCount(); --i >= 0;)
		if (searchselect.GetItem((int)i, &ti))
			delete reinterpret_cast<SSearchParams*>(ti.lParam);

	CResizableFormView::OnDestroy();
}

void CSearchResultsWnd::OnClose()
{
	// Do not pass the WM_CLOSE to the base class. Since we have a rich edit control *and*
	// an attached auto complete control, the WM_CLOSE will get generated by the rich edit control
	// when user presses ESC while the auto complete is open.
	//__super::OnClose();
}

BOOL CSearchResultsWnd::OnHelpInfo(HELPINFO*)
{
	theApp.ShowHelp(eMule_FAQ_GUI_Search);
	return TRUE;
}

LRESULT CSearchResultsWnd::OnIdleUpdateCmdUI(WPARAM, LPARAM)
{
	BOOL bSearchParamsWndVisible = theApp.emuledlg->searchwnd->IsSearchParamsWndVisible();
	m_ctlOpenParamsWnd.ShowWindow(bSearchParamsWndVisible ? SW_HIDE : SW_SHOW);

	return 0;
}

void CSearchResultsWnd::OnBnClickedOpenParamsWnd()
{
	theApp.emuledlg->searchwnd->OpenParametersWnd();
}

void CSearchResultsWnd::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) != SC_KEYMENU)
		__super::OnSysCommand(nID, lParam);
	else if (theApp.emuledlg != NULL && theApp.emuledlg->searchwnd != NULL)
		theApp.emuledlg->searchwnd->SendMessage(WM_SYSCOMMAND, nID, lParam);
	else if (theApp.emuledlg != NULL)
		theApp.emuledlg->SendMessage(WM_SYSCOMMAND, nID, lParam);
}

bool CSearchResultsWnd::CanSearchRelatedFiles() const
{
	return theApp.serverconnect->IsConnected()
		&& theApp.serverconnect->GetCurrentServer() != NULL
		&& theApp.serverconnect->GetCurrentServer()->GetRelatedSearchSupport();
}

// https://forum.emule-project.net/index.php?showtopic=79371&view=findpost&p=564252 )
// Syntax: related::<file hash> or related:<file size>:<file hash>
//
// "the file you 'search' for must be shared by at least 5 clients."
// A client can give several hashes in the related search request since v17.14.
void CSearchResultsWnd::SearchRelatedFiles(CPtrList &listFiles)
{
	POSITION pos = listFiles.GetHeadPosition();
	if (pos == NULL) {
		ASSERT(0);
		return;
	}
	SSearchParams *pParams = new SSearchParams;
	pParams->strExpression = _T("related");

	CString strNames;
	while (pos != NULL) {
		CAbstractFile *pFile = static_cast<CAbstractFile*>(listFiles.GetNext(pos));
		if (pFile->IsKindOf(RUNTIME_CLASS(CAbstractFile))) {
			pParams->strExpression.AppendFormat(_T("::%s"), (LPCTSTR)md4str(pFile->GetFileHash()));
			if (!strNames.IsEmpty())
				strNames += _T(", ");
			strNames += pFile->GetFileName();
		} else
			ASSERT(0);
	}

	pParams->strSpecialTitle.Format(_T("%s: %s"), (LPCTSTR)GetResString(IDS_RELATED), (LPCTSTR)strNames);
	if (pParams->strSpecialTitle.GetLength() > 50)
		pParams->strSpecialTitle = pParams->strSpecialTitle.Left(47) + _T("...");
	StartSearch(pParams);
}


///////////////////////////////////////////////////////////////////////////////
// CSearchResultsSelector

BEGIN_MESSAGE_MAP(CSearchResultsSelector, CClosableTabCtrl)
	ON_WM_CONTEXTMENU()
END_MESSAGE_MAP()

BOOL CSearchResultsSelector::OnCommand(WPARAM wParam, LPARAM lParam)
{
	if (wParam == MP_RESTORESEARCHPARAMS) {
		int iTab = GetTabUnderContextMenu();
		if (iTab >= 0) {
			GetParent()->SendMessage(UM_DBLCLICKTAB, (WPARAM)iTab);
			return TRUE;
		}
	}
	return CClosableTabCtrl::OnCommand(wParam, lParam);
}

void CSearchResultsSelector::OnContextMenu(CWnd*, CPoint point)
{
	if (point.x == -1 || point.y == -1) {
		if (!SetDefaultContextMenuPos())
			return;
		point = m_ptCtxMenu;
		ClientToScreen(&point);
	} else {
		m_ptCtxMenu = point;
		ScreenToClient(&m_ptCtxMenu);
	}

	CTitledMenu menu;
	menu.CreatePopupMenu();
	menu.AddMenuTitle(GetResString(IDS_SW_RESULT));
	menu.AppendMenu(MF_STRING, MP_RESTORESEARCHPARAMS, GetResString(IDS_RESTORESEARCHPARAMS));
	menu.AppendMenu(MF_STRING, MP_REMOVE, GetResString(IDS_FD_CLOSE));
	menu.SetDefaultItem(MP_RESTORESEARCHPARAMS);
	menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
}

LRESULT CSearchResultsWnd::OnChangeFilter(WPARAM wParam, LPARAM lParam)
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

	if (!bColumnDiff && !bFilterDiff)
		return 0;
	m_astrFilter.RemoveAll();
	m_astrFilter.Append(astrFilter);

	int iCurSel = searchselect.GetCurSel();
	if (iCurSel >= 0) {
		TCITEM ti;
		ti.mask = TCIF_PARAM;
		if (searchselect.GetItem(iCurSel, &ti) && ti.lParam != NULL)
			ShowResults(reinterpret_cast<SSearchParams*>(ti.lParam));
	}
	return 0;
}

void CSearchResultsWnd::OnSearchListMenuBtnDropDown(LPNMHDR, LRESULT*)
{
	CTitledMenu menu;
	menu.CreatePopupMenu();

	menu.AppendMenu(MF_STRING | (searchselect.GetItemCount() > 0 ? MF_ENABLED : MF_GRAYED), MP_REMOVEALL, GetResString(IDS_REMOVEALLSEARCH));
	menu.AppendMenu(MF_SEPARATOR);
	CMenu menuFileSizeFormat;
	menuFileSizeFormat.CreateMenu();
	menuFileSizeFormat.AppendMenu(MF_STRING, MP_SHOW_FILESIZE_DFLT, GetResString(IDS_DEFAULT));
	menuFileSizeFormat.AppendMenu(MF_STRING, MP_SHOW_FILESIZE_KBYTE, GetResString(IDS_KBYTES));
	menuFileSizeFormat.AppendMenu(MF_STRING, MP_SHOW_FILESIZE_MBYTE, GetResString(IDS_MBYTES));
	menuFileSizeFormat.CheckMenuRadioItem(MP_SHOW_FILESIZE_DFLT, MP_SHOW_FILESIZE_MBYTE, MP_SHOW_FILESIZE_DFLT + searchlistctrl.GetFileSizeFormat(), 0);
	menu.AppendMenu(MF_POPUP, (UINT_PTR)menuFileSizeFormat.m_hMenu, GetResString(IDS_DL_SIZE));

	RECT rc;
	m_btnSearchListMenu.GetWindowRect(&rc);
	menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, rc.left, rc.bottom, this);
}

BOOL CSearchResultsWnd::OnCommand(WPARAM wParam, LPARAM lParam)
{
	switch (wParam) {
	case MP_REMOVEALL:
		DeleteAllSearches();
		return TRUE;
	case MP_SHOW_FILESIZE_DFLT:
		searchlistctrl.SetFileSizeFormat(fsizeDefault);
		return TRUE;
	case MP_SHOW_FILESIZE_KBYTE:
		searchlistctrl.SetFileSizeFormat(fsizeKByte);
		return TRUE;
	case MP_SHOW_FILESIZE_MBYTE:
		searchlistctrl.SetFileSizeFormat(fsizeMByte);
		return TRUE;
	}
	return CResizableFormView::OnCommand(wParam, lParam);
}

HBRUSH CSearchResultsWnd::OnCtlColor(CDC *pDC, CWnd *pWnd, UINT nCtlColor)
{
	HBRUSH hbr = theApp.emuledlg->GetCtlColor(pDC, pWnd, nCtlColor);
	return hbr ? hbr : __super::OnCtlColor(pDC, pWnd, nCtlColor);
}
