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
#include "ServerListCtrl.h"
#include "OtherFunctions.h"
#include "ProUserMenuCopySeams.h"
#include "MenuShortcutLabels.h"
#include "emuledlg.h"
#include "DownloadQueue.h"
#include "ServerList.h"
#include "Server.h"
#include "ServerConnect.h"
#include "MenuCmds.h"
#include "ServerWnd.h"
#include "IrcWnd.h"
#include "Opcodes.h"
#include "Log.h"
#include "IPFilter.h"
#include "MemDC.h"
#include "GeoLocation.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	bool IsLiveServerPointer(const CServer *pServer)
	{
		return theApp.serverlist != NULL && theApp.serverlist->ContainsServerPointer(pServer);
	}
}


IMPLEMENT_DYNAMIC(CServerListCtrl, CMuleListCtrl)

BEGIN_MESSAGE_MAP(CServerListCtrl, CMuleListCtrl)
	ON_NOTIFY_REFLECT(LVN_COLUMNCLICK, OnLvnColumnClick)
	ON_NOTIFY_REFLECT(LVN_GETINFOTIP, OnLvnGetInfoTip)
	ON_NOTIFY_REFLECT(NM_DBLCLK, OnNmDblClk)
	ON_WM_CONTEXTMENU()
	ON_WM_SYSCOLORCHANGE()
END_MESSAGE_MAP()

CServerListCtrl::CServerListCtrl()
	: m_bBulkLoading(false)
{
	SetGeneralPurposeFind(true);
	SetSkinKey(_T("ServersLv"));
}

bool CServerListCtrl::Init()
{
	SetPrefsKey(_T("ServerListCtrl"));
	SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	CToolTipCtrl *tooltip = GetToolTips();
	if (tooltip) {
		m_tooltip.SubclassWindow(*tooltip);
		tooltip->ModifyStyle(0, TTS_NOPREFIX);
		tooltip->SetDelayTime(TTDT_AUTOPOP, SEC2MS(20));
		//tooltip->SetDelayTime(TTDT_INITIAL, SEC2MS(thePrefs.GetToolTipDelay()));
	}

	InsertColumn(0,	 _T(""),	LVCFMT_LEFT,	150);			//IDS_SL_SERVERNAME
	InsertColumn(1,	 _T(""),	LVCFMT_LEFT,	140);			//IDS_IP
	InsertColumn(2,	 _T(""),	LVCFMT_LEFT,	150);			//IDS_DESCRIPTION
	InsertColumn(3,	 _T(""),	LVCFMT_RIGHT,	50);			//IDS_PING
	InsertColumn(4,	 _T(""),	LVCFMT_RIGHT,	60);			//IDS_UUSERS
	InsertColumn(5,	 _T(""),	LVCFMT_RIGHT,	60);			//IDS_MAXCLIENT
	InsertColumn(6,	 _T(""),	LVCFMT_RIGHT,	60);			//IDS_PW_FILES
	InsertColumn(7,	 _T(""),	LVCFMT_LEFT,	50);			//IDS_PREFERENCE
	InsertColumn(8,	 _T(""),	LVCFMT_RIGHT,	50);			//IDS_UFAILED
	InsertColumn(9,	 _T(""),	LVCFMT_LEFT,	50);			//IDS_STATICSERVER
	InsertColumn(10, _T(""),	LVCFMT_RIGHT,	60);			//IDS_SOFTFILES
	InsertColumn(11, _T(""),	LVCFMT_RIGHT,	60);			//IDS_HARDFILES
	InsertColumn(12, _T(""),	LVCFMT_LEFT,	50);			//IDS_VERSION
	InsertColumn(13, _T(""),	LVCFMT_RIGHT,	60);			//IDS_IDLOW
	InsertColumn(14, _T(""),	LVCFMT_RIGHT,	50);			//IDS_OBFUSCATION
	InsertColumn(15, _T(""),	LVCFMT_LEFT,	140);			//IDS_GEOLOCATION

	if (const auto *pProfile = MuleListCtrlViewPresets::FindProfile(_T("ServerListCtrl")))
		SetViewPresetProfile(*pProfile);
	SetAllIcons();
	Localize();
	LoadSettings();

	// Barry - Use preferred sort order from preferences
	SetSortArrow();
	SortItems(SortProc, MAKELONG(GetSortItem(), !GetSortAscending()));

	ShowServerCount();

	return true;
}

void CServerListCtrl::OnSysColorChange()
{
	CMuleListCtrl::OnSysColorChange();
	SetAllIcons();
}

void CServerListCtrl::SetAllIcons()
{
	ApplyImageList(NULL);
	// Apply the image list also to the listview control, even if we use our own 'DrawItem'.
	// This is needed to give the listview control a chance to initialize the row height.
	ASSERT((GetStyle() & LVS_SHAREIMAGELISTS) != 0);
	m_pImageList = &theApp.emuledlg->GetClientIconList();
	VERIFY(ApplyImageList(*m_pImageList) == NULL);
}

const CServer* CServerListCtrl::GetLiveServerByIndex(int iItem)
{
	if (iItem < 0 || iItem >= GetItemCount())
		return NULL;

	const CServer *pServer = reinterpret_cast<CServer*>(GetItemData(iItem));
	return IsLiveServer(pServer) ? pServer : NULL;
}

bool CServerListCtrl::IsLiveServer(const CServer *pServer) const
{
	return IsLiveServerPointer(pServer);
}

bool CServerListCtrl::PruneStaleServerItems()
{
	bool bRemoved = false;
	for (int iItem = GetItemCount(); --iItem >= 0;) {
		if (!IsLiveServer(reinterpret_cast<CServer*>(GetItemData(iItem)))) {
			DeleteItem(iItem);
			bRemoved = true;
		}
	}

	if (bRemoved)
		ShowServerCount();

	return bRemoved;
}

CString CServerListCtrl::GetItemDisplayText(const CServer *server, int iSubItem) const
{
	CString sText;
	switch (iSubItem) {
	case 0: //name
		sText = server->GetListName();
		break;
	case 1: //ip:port
		sText.Format(_T("%s : %i"), server->GetAddress(), server->GetPort());
		break;
	case 2: //description
		sText = server->GetDescription();
		break;
	case 3: //ping
		if (server->GetPing())
			sText.Format(_T("%u"), server->GetPing());
		break;
	case 4: //users
		if (server->GetUsers())
			sText = CastItoIShort(server->GetUsers());
		break;
	case 5: //max users
		if (server->GetUsers())
			sText = CastItoIShort(server->GetMaxUsers());
		break;
	case 6: //files
		if (server->GetFiles())
			sText = CastItoIShort(server->GetFiles());
		break;
	case 7: //priority
		{
			UINT uid;
			switch (server->GetPreference()) {
			case SRV_PR_LOW:
				uid = IDS_PRIOLOW;
				break;
			case SRV_PR_NORMAL:
				uid = IDS_PRIONORMAL;
				break;
			case SRV_PR_HIGH:
				uid = IDS_PRIOHIGH;
				break;
			default:
				uid = IDS_PRIONOPREF;
			}
			sText = GetResString(uid);
		}
		break;
	case 8: //failed count
		sText.Format(_T("%u"), server->GetFailedCount());
		break;
	case 9: //static
		sText = GetResString(server->IsStaticMember() ? IDS_YES : IDS_NO);
		break;
	case 10: //soft files
		sText = CastItoIShort(server->GetSoftFiles());
		break;
	case 11: //hard files
		sText = CastItoIShort(server->GetHardFiles());
		break;
	case 12: //version
		sText = server->GetVersion();
		if (thePrefs.GetDebugServerUDPLevel() > 0 && server->GetUDPFlags() > 0)
			sText.AppendFormat(&_T("; ExtUDP=%x")[sText.IsEmpty() ? 2 : 0], server->GetUDPFlags());
		if (thePrefs.GetDebugServerTCPLevel() > 0 && server->GetTCPFlags() > 0)
			sText.AppendFormat(&_T("; ExtTCP=%x")[sText.IsEmpty() ? 2 : 0], server->GetTCPFlags());
		break;
	case 13: //low ID users
		sText = CastItoIShort(server->GetLowIDUsers());
		break;
	case 14: //obfuscation
		sText = GetResString(server->SupportsObfuscationTCP() ? IDS_YES : IDS_NO);
		break;
	case 15: //geo location
		if (theApp.geolocation != NULL)
			sText = theApp.geolocation->GetDisplayText(server->GetIP());
	}
	return sText;
}

void CServerListCtrl::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	const CServer *pServer = reinterpret_cast<CServer*>(lpDrawItemStruct->itemData);
	if (!pServer || theApp.IsClosing())
		return;
	if (!IsLiveServer(pServer))
		return;

	CRect rcItem(lpDrawItemStruct->rcItem);
	CMemoryDC dc(CDC::FromHandle(lpDrawItemStruct->hDC), rcItem);
	BOOL bCtrlFocused;
	InitItemMemDC(dc, lpDrawItemStruct, bCtrlFocused);
	RECT rcServer;
	GetClientRect(&rcServer);

	const CServer *pConnectedServer = theApp.serverconnect->GetCurrentServer();
	// the server which we are connected to, always has a valid numerical IP member assigned,
	// therefore we do not need to call CServer::IsEqual which would be expensive
	if (pConnectedServer && pConnectedServer->GetIP() == pServer->GetIP() && pConnectedServer->GetPort() == pServer->GetPort()) {
		COLORREF crConnected = RGB(32, 32, 255);
		theApp.LoadSkinColor(_T("ServersLvFg_Connected"), crConnected);
		dc.SetTextColor(crConnected);
	} else if (pServer->GetFailedCount() >= thePrefs.GetDeadServerRetries()) {
		COLORREF crFailed = RGB(192, 192, 192);
		theApp.LoadSkinColor(_T("ServersLvFg_Failed"), crFailed);
		dc.SetTextColor(crFailed);
	} else if (pServer->GetFailedCount() >= 2) {
		COLORREF crWarning = RGB(128, 128, 128);
		theApp.LoadSkinColor(_T("ServersLvFg_Warning"), crWarning);
		dc.SetTextColor(crWarning);
	}

	const CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();
	int iCount = pHeaderCtrl->GetItemCount();
	LONG itemLeft = rcItem.left;
	LONG iIconY = max((rcItem.Height() - 15) / 2, 0);
	for (int iCurrent = 0; iCurrent < iCount; ++iCurrent) {
		int iColumn = pHeaderCtrl->OrderToIndex(iCurrent);
		if (IsColumnHidden(iColumn))
			continue;

		UINT uDrawTextAlignment;
		int iColumnWidth = GetColumnWidth(iColumn, uDrawTextAlignment);
		rcItem.left = itemLeft;
		rcItem.right = itemLeft + iColumnWidth - sm_iSubItemInset;
		if (rcItem.left < rcItem.right && HaveIntersection(rcServer, rcItem)) {
			const CString &sItem(GetItemDisplayText(pServer, iColumn));
			switch (iColumn) {
			case 0: //server name
				{
					int iImage = 15; //server
					UINT uOverlayImage = pServer->SupportsObfuscationTCP() ? 2 : 0;

					rcItem.left = itemLeft + sm_iIconOffset;
					const POINT point{rcItem.left, rcItem.top + iIconY};
					m_pImageList->Draw(dc, iImage, point, ILD_NORMAL | INDEXTOOVERLAYMASK(uOverlayImage));
					rcItem.left += 16 + sm_iLabelOffset - sm_iSubItemInset;
					rcItem.left += sm_iSubItemInset;
					dc.DrawText(sItem, &rcItem, MLC_DT_TEXT | uDrawTextAlignment);
					break;
				}
			case 15: //geo location
				if (theApp.geolocation != NULL) {
					const POINT point{itemLeft + sm_iIconOffset, rcItem.top + iIconY};
					if (theApp.geolocation->DrawFlag(dc, pServer->GetIP(), point))
						rcItem.left = itemLeft + sm_iIconOffset + 18 + sm_iLabelOffset - sm_iSubItemInset;
				}
				rcItem.left += sm_iSubItemInset;
				dc.DrawText(sItem, &rcItem, MLC_DT_TEXT | uDrawTextAlignment);
				break;
			default: //any text column
				rcItem.left += sm_iSubItemInset;
				dc.DrawText(sItem, &rcItem, MLC_DT_TEXT | uDrawTextAlignment);
				break;
			}
		}
		itemLeft += iColumnWidth;
	}

	DrawFocusRect(dc, &lpDrawItemStruct->rcItem, lpDrawItemStruct->itemState & ODS_FOCUS, bCtrlFocused, lpDrawItemStruct->itemState & ODS_SELECTED);
}

void CServerListCtrl::Localize()
{
	static const UINT uids[16] =
	{
		IDS_SL_SERVERNAME, IDS_IP, IDS_DESCRIPTION, IDS_PING, IDS_UUSERS
		, IDS_MAXCLIENT, IDS_PW_FILES, IDS_PREFERENCE, IDS_UFAILED, IDS_STATICSERVER
		, IDS_SOFTFILES, IDS_HARDFILES, IDS_VERSION, IDS_IDLOW, IDS_OBFUSCATION, IDS_GEOLOCATION
	};

	LocaliseHeaderCtrl(uids, _countof(uids));
}

void CServerListCtrl::RemoveServer(const CServer *pServer)
{
	int iItem = FindServer(pServer);
	if (iItem >= 0) {
		DeleteItem(iItem);
		ShowServerCount();
	}
	theApp.serverlist->RemoveServer(pServer);
}

void CServerListCtrl::RemoveAllDeadServers()
{
	ShowWindow(SW_HIDE);
	for (POSITION pos = theApp.serverlist->list.GetHeadPosition(); pos != NULL;) {
		const CServer *cur_server = theApp.serverlist->list.GetNext(pos);
		if (cur_server->GetFailedCount() >= thePrefs.GetDeadServerRetries())
			RemoveServer(cur_server);
	}
	ShowWindow(SW_SHOW);
}

void CServerListCtrl::RemoveAllFilteredServers()
{
	if (!thePrefs.GetFilterServerByIP())
		return;
	ShowWindow(SW_HIDE);
	for (POSITION pos = theApp.serverlist->list.GetHeadPosition(); pos != NULL;) {
		const CServer *cur_server = theApp.serverlist->list.GetNext(pos);
		if (theApp.ipfilter->IsFiltered(cur_server->GetIP())) {
			if (thePrefs.GetLogFilteredIPs())
				AddDebugLogLine(false, _T("IPFilter(Updated): Filtered server \"%s\" (IP=%s) - IP filter (%s)"), (LPCTSTR)cur_server->GetListName(), (LPCTSTR)ipstr(cur_server->GetIP()), (LPCTSTR)theApp.ipfilter->GetLastHit());
			RemoveServer(cur_server);
		}
	}
	ShowWindow(SW_SHOW);
}

bool CServerListCtrl::AddServer(const CServer *pServer, bool bAddToList, bool bRandom)
{
	bool bAddTail = !bRandom || (rand() % (1 + theApp.serverlist->GetServerCount()) != 0);
	if (!theApp.serverlist->AddServer(pServer, bAddTail))
		return false;
	if (bAddToList) {
		InsertItem(LVIF_TEXT | LVIF_PARAM, bAddTail ? GetItemCount() : 0, _T(""), 0, 0, 0, (LPARAM)pServer);
		if (!m_bBulkLoading)
			RefreshServer(pServer);
	}
	if (!m_bBulkLoading)
		ShowServerCount();
	return true;
}

void CServerListCtrl::EndBulkLoad()
{
	if (!m_bBulkLoading)
		return;

	m_bBulkLoading = false;
	ShowServerCount();
	Invalidate(FALSE);
}

void CServerListCtrl::OnContextMenu(CWnd*, CPoint point)
{
	PruneStaleServerItems();

	// get merged settings
	bool bFirstItem = true;
	int iSelectedItems = GetSelectedCount();
	int iStaticServers = 0;
	UINT uPrioMenuItem = 0;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		const CServer *pServer = GetLiveServerByIndex(GetNextSelectedItem(pos));
		if (pServer == NULL)
			continue;
		iStaticServers += static_cast<int>(pServer->IsStaticMember());

		UINT uCurPrioMenuItem;
		switch (pServer->GetPreference()) {
		case SRV_PR_LOW:
			uCurPrioMenuItem = MP_PRIOLOW;
			break;
		case SRV_PR_NORMAL:
			uCurPrioMenuItem = MP_PRIONORMAL;
			break;
		case SRV_PR_HIGH:
			uCurPrioMenuItem = MP_PRIOHIGH;
			break;
		default:
			uCurPrioMenuItem = 0;
			ASSERT(0);
		}

		if (bFirstItem)
			uPrioMenuItem = uCurPrioMenuItem;
		else if (uPrioMenuItem != uCurPrioMenuItem)
			uPrioMenuItem = 0;

		bFirstItem = false;
	}

	CTitledMenu ServerMenu;
	ServerMenu.CreatePopupMenu();
	ServerMenu.AddMenuTitle(GetResString(IDS_EM_SERVER), true);

	const bool bCanConnectToSelectedServer = iSelectedItems > 0
		&& theApp.emuledlg != NULL
		&& theApp.emuledlg->CanUseP2PConnectionCommands();
	ServerMenu.AppendMenu(MF_STRING | (bCanConnectToSelectedServer ? MF_ENABLED : MF_GRAYED), MP_CONNECTTO, GetResString(IDS_CONNECTTHIS), _T("CONNECT"));
	ServerMenu.SetDefaultItem(bCanConnectToSelectedServer ? MP_CONNECTTO : -1);

	CMenu ServerPrioMenu;
	ServerPrioMenu.CreateMenu();
	if (iSelectedItems > 0) {
		ServerPrioMenu.AppendMenu(MF_STRING, MP_PRIOLOW, GetResString(IDS_PRIOLOW));
		ServerPrioMenu.AppendMenu(MF_STRING, MP_PRIONORMAL, GetResString(IDS_PRIONORMAL));
		ServerPrioMenu.AppendMenu(MF_STRING, MP_PRIOHIGH, GetResString(IDS_PRIOHIGH));
		ServerPrioMenu.CheckMenuRadioItem(MP_PRIOLOW, MP_PRIOHIGH, uPrioMenuItem, 0);
	}
	ServerMenu.AppendMenu(MF_POPUP | (iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED), (UINT_PTR)ServerPrioMenu.m_hMenu, GetResString(IDS_PRIORITY), _T("PRIORITY"));

	// enable add/remove from static server list, if there is at least one selected server which can be used for the action
	ServerMenu.AppendMenu(MF_STRING | (iStaticServers < iSelectedItems ? MF_ENABLED : MF_GRAYED), MP_ADDTOSTATIC, GetResString(IDS_ADDTOSTATIC), _T("ListAdd"));
	ServerMenu.AppendMenu(MF_STRING | (iStaticServers > 0 ? MF_ENABLED : MF_GRAYED), MP_REMOVEFROMSTATIC, GetResString(IDS_REMOVEFROMSTATIC), _T("ListRemove"));
	ServerMenu.AppendMenu(MF_SEPARATOR);

	CTitledMenu CopyMenu;
	CopyMenu.CreateMenu();
	CopyMenu.AddMenuTitle(NULL, true);
	CopyMenu.AppendMenu(MF_STRING | (iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED), MP_COPY_SERVER_ENDPOINT, GetResString(IDS_COPY_SERVER_ENDPOINT));
	CopyMenu.AppendMenu(MF_STRING | (iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED), MP_COPY_SERVER_NAME, GetResString(IDS_COPY_SERVER_NAME));
	CopyMenu.AppendMenu(MF_STRING | (iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED), MP_GETED2KLINK, GetResString(IDS_DL_LINK1), _T("ED2KLINK"));
	CopyMenu.AppendMenu(MF_STRING | (iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED), MP_COPY_SERVER_SUMMARY, GetResString(IDS_COPY_SERVER_SUMMARY));
	ServerMenu.AppendMenu(MF_POPUP | (iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED), (UINT_PTR)CopyMenu.m_hMenu, GetResString(IDS_COPY));
	ServerMenu.AppendMenu(MF_STRING | (theApp.IsEd2kServerLinkInClipboard() ? MF_ENABLED : MF_GRAYED), MP_PASTE, GetResString(IDS_SW_DIRECTDOWNLOAD), _T("PASTELINK"));
	ServerMenu.AppendMenu(MF_STRING | (iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED), MP_REMOVE, GetResString(IDS_REMOVETHIS), _T("DELETESELECTED"));
	ServerMenu.AppendMenu(MF_STRING | (GetItemCount() > 0 ? MF_ENABLED : MF_GRAYED), MP_REMOVEALL, GetResString(IDS_REMOVEALL), _T("DELETE"));

	ServerMenu.AppendMenu(MF_SEPARATOR);
	ServerMenu.AppendMenu(MF_ENABLED | (GetItemCount() > 0 ? MF_ENABLED : MF_GRAYED), MP_FIND, AddMenuShortcutLabel(GetResString(IDS_FIND), _T("Ctrl+F")), _T("Search"));

	GetPopupMenuPos(*this, point);
	ServerMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);

	VERIFY(ServerPrioMenu.DestroyMenu());
	VERIFY(CopyMenu.DestroyMenu());
	VERIFY(ServerMenu.DestroyMenu());
}

BOOL CServerListCtrl::OnCommand(WPARAM wParam, LPARAM)
{
	wParam = LOWORD(wParam);
	PruneStaleServerItems();

	switch (wParam) {
	case MP_CONNECTTO:
	case IDA_ENTER:
		if (theApp.emuledlg != NULL && !theApp.emuledlg->CanUseP2PConnectionCommands()) {
			theApp.emuledlg->LogP2PConnectionCommandBlocked();
			return TRUE;
		}
		if (GetSelectedCount() > 1) {
			theApp.serverconnect->Disconnect();
			for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
				int iItem = GetNextSelectedItem(pos);
				if (iItem > -1) {
					const CServer *pServer = GetLiveServerByIndex(iItem);
					if (pServer != NULL && (!thePrefs.IsCryptLayerRequired() || pServer->SupportsObfuscationTCP() || !pServer->TriedCrypt()))
						theApp.serverlist->MoveServerDown(pServer);
				}
			}
			theApp.serverconnect->ConnectToAnyServer(theApp.serverlist->GetServerCount() - GetSelectedCount(), false, false);
		} else {
			int iItem = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
			if (iItem > -1) {
				const CServer *pServer = GetLiveServerByIndex(iItem);
				if (pServer != NULL && (!thePrefs.IsCryptLayerRequired() || pServer->SupportsObfuscationTCP() || !pServer->TriedCrypt()))
					theApp.serverconnect->ConnectToServer(const_cast<CServer*>(pServer));
			}
		}
		theApp.emuledlg->ShowConnectionState();
		return TRUE;
	case MP_CUT:
		{
			const CString &strURLs(CreateSelectedServersURLs());
			if (!strURLs.IsEmpty())
				theApp.CopyTextToClipboard(strURLs);
			DeleteSelectedServers();
		}
		return TRUE;
	case MP_COPYSELECTED:
	case MP_GETED2KLINK:
	case Irc_SetSendLink:
		{
			const CString &strURLs(CreateSelectedServersURLs());
			if (!strURLs.IsEmpty())
				if (wParam == Irc_SetSendLink)
					theApp.emuledlg->ircwnd->SetSendFileString(strURLs);
				else
					theApp.CopyTextToClipboard(strURLs);
		}
		return TRUE;
	case MP_COPY_SERVER_ENDPOINT:
	case MP_COPY_SERVER_NAME:
	case MP_COPY_SERVER_SUMMARY:
		{
			std::vector<CString> lines;
			for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
				const CServer *pServer = GetLiveServerByIndex(GetNextSelectedItem(pos));
				if (pServer == NULL)
					continue;

				CString line;
				if (wParam == MP_COPY_SERVER_ENDPOINT)
					line.Format(_T("%s:%u"), pServer->GetAddress(), pServer->GetPort());
				else if (wParam == MP_COPY_SERVER_NAME)
					line = pServer->GetListName();
				else {
					std::vector<ProUserMenuCopySeams::NamedField> fields;
					CString endpoint;
					endpoint.Format(_T("%s:%u"), pServer->GetAddress(), pServer->GetPort());
					ProUserMenuCopySeams::AppendField(fields, _T("name"), pServer->GetListName());
					ProUserMenuCopySeams::AppendField(fields, _T("endpoint"), endpoint);
					ProUserMenuCopySeams::AppendField(fields, _T("description"), pServer->GetDescription());
					if (pServer->GetUsers()) {
						CString users;
						users.Format(_T("%u"), pServer->GetUsers());
						ProUserMenuCopySeams::AppendField(fields, _T("users"), users);
					}
					if (pServer->GetFiles()) {
						CString files;
						files.Format(_T("%u"), pServer->GetFiles());
						ProUserMenuCopySeams::AppendField(fields, _T("files"), files);
					}
					line = ProUserMenuCopySeams::FormatSummary(fields);
				}
				lines.push_back(line);
			}
			const CString text = ProUserMenuCopySeams::JoinLines(lines);
			if (!text.IsEmpty())
				theApp.CopyTextToClipboard(text);
		}
		return TRUE;
	case MP_PASTE:
		if (theApp.IsEd2kServerLinkInClipboard())
			theApp.emuledlg->serverwnd->PasteServerFromClipboard();
		return TRUE;
	case MP_REMOVE:
	case MPG_DELETE:
		DeleteSelectedServers();
		return TRUE;
	case MP_REMOVEALL:
		if (LocMessageBox(IDS_REMOVEALLSERVERS, MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2, 0) == IDYES) {
			if (theApp.serverconnect->IsConnecting()) {
				theApp.downloadqueue->StopUDPRequests();
				theApp.serverconnect->StopConnectionTry();
				theApp.serverconnect->Disconnect();
				theApp.emuledlg->ShowConnectionState();
			}
			ShowWindow(SW_HIDE);
			DeleteAllItems();
			theApp.serverlist->RemoveAllServers();
			ShowWindow(SW_SHOW);
			ShowServerCount();
		}
		return TRUE;
	case MP_FIND:
		OnFindStart();
		return TRUE;
	case MP_ADDTOSTATIC:
		for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
			const CServer *pServer = GetLiveServerByIndex(GetNextSelectedItem(pos));
			if (pServer == NULL)
				continue;
			if (!StaticServerFileAppend(const_cast<CServer*>(pServer)))
				return FALSE;
			RefreshServer(pServer);
		}
		return TRUE;
	case MP_REMOVEFROMSTATIC:
		for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
			const CServer *pServer = GetLiveServerByIndex(GetNextSelectedItem(pos));
			if (pServer == NULL)
				continue;
			if (!StaticServerFileRemove(const_cast<CServer*>(pServer)))
				return FALSE;
			RefreshServer(pServer);
		}
		return TRUE;
	case MP_PRIOLOW:
		SetSelectedServersPriority(SRV_PR_LOW);
		return TRUE;
	case MP_PRIONORMAL:
		SetSelectedServersPriority(SRV_PR_NORMAL);
		return TRUE;
	case MP_PRIOHIGH:
		SetSelectedServersPriority(SRV_PR_HIGH);
		return TRUE;
	}
	return FALSE;
}

CString CServerListCtrl::CreateSelectedServersURLs()
{
	CString links;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		const CServer *pServer = GetLiveServerByIndex(GetNextSelectedItem(pos));
		if (pServer == NULL)
			continue;
		if (!links.IsEmpty())
			links += _T("\r\n");
		links.AppendFormat(_T("ed2k://|server|%s|%u|/"), pServer->GetAddress(), pServer->GetPort());
	}
	return links;
}

void CServerListCtrl::DeleteSelectedServers()
{
	SetRedraw(false);
	POSITION pos;
	while ((pos = GetFirstSelectedItemPosition()) != NULL) {
		int iItem = GetNextSelectedItem(pos);
		const CServer *pServer = GetLiveServerByIndex(iItem);
		DeleteItem(iItem);
		if (pServer != NULL)
			theApp.serverlist->RemoveServer(pServer);
	}
	ShowServerCount();
	SetRedraw(true);
	SetFocus();
	AutoSelectItem();
}

void CServerListCtrl::SetSelectedServersPriority(UINT uPriority)
{
	bool bUpdateStaticServersFile = false;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		CServer *pServer = const_cast<CServer*>(GetLiveServerByIndex(GetNextSelectedItem(pos)));
		if (pServer != NULL && pServer->GetPreference() != uPriority) {
			pServer->SetPreference(uPriority);
			if (pServer->IsStaticMember())
				bUpdateStaticServersFile = true;
			RefreshServer(pServer);
		}
	}
	if (bUpdateStaticServersFile)
		theApp.serverlist->SaveStaticServers();
}

void CServerListCtrl::OnNmDblClk(LPNMHDR, LRESULT*)
{
	int iItem = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
	if (iItem >= 0) {
		CServer *pServer = const_cast<CServer*>(GetLiveServerByIndex(iItem));
		if (pServer != NULL) {
			theApp.serverconnect->ConnectToServer(pServer);
			theApp.emuledlg->ShowConnectionState();
		}
	}
}

bool CServerListCtrl::AddServerMetToList(const CString &strFile)
{
	SetRedraw(false);
	bool bResult = theApp.serverlist->AddServerMetToList(strFile, true);
	RemoveAllDeadServers();
	ShowServerCount();
	SetRedraw(true);
	return bResult;
}

void CServerListCtrl::RefreshServer(const CServer *pServer)
{
	if (m_bBulkLoading)
		return;
	if (pServer && !theApp.IsClosing()) {
		if (!IsLiveServer(pServer)) {
			PruneStaleServerItems();
			return;
		}

		int iItem = FindServer(pServer);
		if (iItem >= 0) {
			for (int i = 0; i <= 15; ++i) //column autosizing requires item text
				SetItemText(iItem, i, GetItemDisplayText(pServer, i));
			Update(iItem);
		}
	}
}

void CServerListCtrl::OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult)
{
	const LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	bool sortAscending;
	if (GetSortItem() != pNMLV->iSubItem)
		switch (pNMLV->iSubItem) {
		case 4: // Users
		case 5: // Max Users
		case 6: // Files
		case 7: // Priority
		case 9: // Static
		case 10: // Soft Files
		case 11: // Hard Files
		case 12: // Version
		case 13: // Low IDs
		case 14: // Obfuscation
			sortAscending = false;
			break;
		default:
			sortAscending = true;
		}
	else
		sortAscending = !GetSortAscending();

	// Sort table
	UpdateSortHistory(MAKELONG(pNMLV->iSubItem, !sortAscending));
	SetSortArrow(pNMLV->iSubItem, sortAscending);
	PruneStaleServerItems();
	SortItems(SortProc, MAKELONG(pNMLV->iSubItem, !sortAscending));
	*pResult = 0;
}

int CALLBACK CServerListCtrl::SortProc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	if (lParam1 == 0 || lParam2 == 0)
		return 0;
	const CServer *item1 = reinterpret_cast<CServer*>(lParam1);
	const CServer *item2 = reinterpret_cast<CServer*>(lParam2);
	const bool bLiveItem1 = IsLiveServerPointer(item1);
	const bool bLiveItem2 = IsLiveServerPointer(item2);
	if (!bLiveItem1 || !bLiveItem2)
		return bLiveItem1 ? -1 : (bLiveItem2 ? 1 : 0);

	int iResult;
	switch (LOWORD(lParamSort)) {
	case 0:
		iResult = Undefined_at_bottom(item1->GetListName(), item2->GetListName());
		break;
	case 1:
		if (item1->HasDynIP() && item2->HasDynIP())
			iResult = item1->GetDynIP().CompareNoCase(item2->GetDynIP());
		else if (item1->HasDynIP())
			iResult = -1;
		else if (item2->HasDynIP())
			iResult = 1;
		else {
			iResult = CompareUnsigned(htonl(item1->GetIP()), htonl(item2->GetIP()));
			if (!iResult)
				iResult = CompareUnsigned(item1->GetPort(), item2->GetPort());
		}
		break;
	case 2:
		iResult = Undefined_at_bottom(item1->GetDescription(), item2->GetDescription());
		break;
	case 3:
		iResult = Undefined_at_bottom(item1->GetPing(), item2->GetPing());
		break;
	case 4:
		iResult = Undefined_at_bottom(item1->GetUsers(), item2->GetUsers());
		break;
	case 5:
		iResult = Undefined_at_bottom(item1->GetMaxUsers(), item2->GetMaxUsers());
		break;
	case 6:
		iResult = Undefined_at_bottom(item1->GetFiles(), item2->GetFiles());
		break;
	case 7:
		if (item2->GetPreference() == item1->GetPreference())
			iResult = 0;
		else if (item2->GetPreference() == SRV_PR_LOW)
			iResult = 1;
		else if (item1->GetPreference() == SRV_PR_LOW)
			iResult = -1;
		else if (item2->GetPreference() == SRV_PR_HIGH)
			iResult = -1;
		else if (item1->GetPreference() == SRV_PR_HIGH)
			iResult = 1;
		else
			iResult = 0;
		break;
	case 8:
		iResult = CompareUnsigned(item1->GetFailedCount(), item2->GetFailedCount());
		break;
	case 9:
		iResult = (int)item1->IsStaticMember() - (int)item2->IsStaticMember();
		break;
	case 10:
		iResult = Undefined_at_bottom(item1->GetSoftFiles(), item2->GetSoftFiles());
		break;
	case 11:
		iResult = Undefined_at_bottom(item1->GetHardFiles(), item2->GetHardFiles());
		break;
	case 12:
		iResult = Undefined_at_bottom(item1->GetVersion(), item2->GetVersion());
		break;
	case 13:
		iResult = Undefined_at_bottom(item1->GetLowIDUsers(), item2->GetLowIDUsers());
		break;
	case 14:
		iResult = (int)(item1->SupportsObfuscationTCP()) - (int)(item2->SupportsObfuscationTCP());
		break;
	case 15:
		if (theApp.geolocation != NULL)
			iResult = CompareLocaleStringNoCase(theApp.geolocation->GetDisplayText(item1->GetIP()), theApp.geolocation->GetDisplayText(item2->GetIP()));
		else
			iResult = 0;
		break;
	default:
		iResult = 0;
	}
	if (iResult > 3)
		return iResult - 5;

	//call secondary sort order, if the first one resulted as equal
	if (iResult == 0) {
		LPARAM iNextSort = theApp.emuledlg->serverwnd->serverlistctrl.GetNextSortOrder(lParamSort);
		if (iNextSort != -1)
			iResult = SortProc(lParam1, lParam2, iNextSort);
	}

	return HIWORD(lParamSort) ? -iResult : iResult;
}

bool CServerListCtrl::StaticServerFileAppend(CServer *pServer)
{
	AddLogLine(false, _T("'%s:%i,%s' %s"), pServer->GetAddress(), pServer->GetPort(), (LPCTSTR)pServer->GetListName(), (LPCTSTR)GetResString(IDS_ADDED2SSF));
	pServer->SetIsStaticMember(true);
	bool bResult = theApp.serverlist->SaveStaticServers();
	RefreshServer(pServer);
	return bResult;
}

bool CServerListCtrl::StaticServerFileRemove(CServer *pServer)
{
	if (!pServer->IsStaticMember())
		return true;
	pServer->SetIsStaticMember(false);
	return theApp.serverlist->SaveStaticServers();
}

void CServerListCtrl::ShowServerCount()
{
	CString sCount(GetResString(IDS_SV_SERVERLIST));
	sCount.AppendFormat(_T(" (%i)"), GetItemCount());
	theApp.emuledlg->serverwnd->SetDlgItemText(IDC_SERVLIST_TEXT, sCount);
}

void CServerListCtrl::OnLvnGetInfoTip(LPNMHDR pNMHDR, LRESULT *pResult)
{
	LPNMLVGETINFOTIP pGetInfoTip = reinterpret_cast<LPNMLVGETINFOTIP>(pNMHDR);
	LVHITTESTINFO hti;
	if (pGetInfoTip->iSubItem == 0 && ::GetCursorPos(&hti.pt)) {
		ScreenToClient(&hti.pt);
		bool bOverMainItem = (SubItemHitTest(&hti) != -1 && hti.iItem == pGetInfoTip->iItem && hti.iSubItem == 0);

		// those tooltips are very nice for debugging/testing but pretty annoying for general usage
		// enable tooltips only if Ctrl is currently pressed
		// Low noise mode: show tooltips at least two selected servers only
		bool bShowInfoTip = bOverMainItem && GetSelectedCount() > 1 && GetKeyState(VK_CONTROL) < 0;
		if (bShowInfoTip) {
			// Don't show the tooltip if the mouse cursor is not over at least one of the selected items
			bool bInfoTipItemIsPartOfMultiSelection = false;
			for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;)
				if (GetNextSelectedItem(pos) == pGetInfoTip->iItem) {
					bInfoTipItemIsPartOfMultiSelection = true;
					break;
				}

			if (!bInfoTipItemIsPartOfMultiSelection)
				bShowInfoTip = false;
		}

		if (!bShowInfoTip) {
			if (!bOverMainItem) {
				// don't show the default label tip for the main item, if the mouse is not over the main item
				if ((pGetInfoTip->dwFlags & LVGIT_UNFOLDED) == 0 && pGetInfoTip->cchTextMax > 0 && pGetInfoTip->pszText[0] != _T('\0'))
					pGetInfoTip->pszText[0] = _T('\0');
			}
			return;
		}

		int iSelected = 0;
		ULONGLONG ulTotalUsers = 0;
		ULONGLONG ulTotalLowIdUsers = 0;
		ULONGLONG ulTotalFiles = 0;
		for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
			const CServer *pServer = GetLiveServerByIndex(GetNextSelectedItem(pos));
			if (pServer) {
				++iSelected;
				ulTotalUsers += pServer->GetUsers();
				ulTotalFiles += pServer->GetFiles();
				ulTotalLowIdUsers += pServer->GetLowIDUsers();
			}
		}

		if (iSelected > 0) {
			CString strInfo(GetResString(IDS_FSTAT_SERVERS));
			strInfo.AppendFormat(_T(": %i\r\n%s: %s\r\n%s: %s\r\n%s: %s") TOOLTIP_AUTOFORMAT_SUFFIX
				, iSelected
				, (LPCTSTR)GetResString(IDS_UUSERS), (LPCTSTR)CastItoIShort(ulTotalUsers)
				, (LPCTSTR)GetResString(IDS_IDLOW), (LPCTSTR)CastItoIShort(ulTotalLowIdUsers)
				, (LPCTSTR)GetResString(IDS_PW_FILES), (LPCTSTR)CastItoIShort(ulTotalFiles)
			);
			_tcsncpy_s(pGetInfoTip->pszText, pGetInfoTip->cchTextMax, strInfo, _TRUNCATE);
		}
	}
	*pResult = 0;
}

int CServerListCtrl::Undefined_at_bottom(const uint32 i1, const uint32 i2)
{
	if (i1 == i2)
		return 5;
	if (i1 == 0)
		return 6;
	if (i2 == 0)
		return 4;
	return CompareUnsigned(i1, i2);
}

int CServerListCtrl::Undefined_at_bottom(const CString &s1, const CString &s2)
{
	if (s1.IsEmpty())
		return s2.IsEmpty() ? 5 : 6;
	return s2.IsEmpty() ? 4 : sgn(s1.CompareNoCase(s2));
}

int CServerListCtrl::FindServer(const CServer *pServer)
{
	LVFINDINFO find;
	find.flags = LVFI_PARAM;
	find.lParam = (LPARAM)pServer;
	return FindItem(&find);
}
