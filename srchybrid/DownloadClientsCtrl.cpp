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
#include "emuledlg.h"
#include "DownloadClientsCtrl.h"
#include "DownloadProgressBarSeams.h"
#include "ClientDetailDialog.h"
#include "MemDC.h"
#include "MenuCmds.h"
#include "GeoLocation.h"
#include "TransferDlg.h"
#include "UpDownClient.h"
#include "ClientCredits.h"
#include "ClientList.h"
#include "PartFile.h"
#include "FriendList.h"
#include "ChatWnd.h"
#include "Kademlia/Kademlia/Kademlia.h"
#include "OtherFunctions.h"
#include "ProUserMenuCopySeams.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	uint32 GetClientGeoIP(const CUpDownClient* client)
	{
		if (client == NULL)
			return 0;
		return client->GetIP() != 0 ? client->GetIP() : client->GetConnectIP();
	}

	bool IsLiveDownloadClient(const CUpDownClient *client)
	{
		return theApp.clientlist != NULL && theApp.clientlist->ContainsClientPointer(client);
	}
}


IMPLEMENT_DYNAMIC(CDownloadClientsCtrl, CMuleListCtrl)

BEGIN_MESSAGE_MAP(CDownloadClientsCtrl, CMuleListCtrl)
	ON_NOTIFY_REFLECT(LVN_COLUMNCLICK, OnLvnColumnClick)
	ON_NOTIFY_REFLECT(LVN_GETDISPINFO, OnLvnGetDispInfo)
	ON_NOTIFY_REFLECT(NM_DBLCLK, OnNmDblClk)
	ON_WM_CONTEXTMENU()
	ON_WM_SYSCOLORCHANGE()
END_MESSAGE_MAP()

CDownloadClientsCtrl::CDownloadClientsCtrl()
	: CListCtrlItemWalk(this)
{
	SetGeneralPurposeFind(true);
	SetSkinKey(_T("DownloadingLv"));
}

void CDownloadClientsCtrl::Init()
{
	SetPrefsKey(_T("DownloadClientsCtrl"));
	SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	InsertColumn(0,	_T(""),	LVCFMT_LEFT,	DFLT_CLIENTNAME_COL_WIDTH);	//IDS_QL_USERNAME
	InsertColumn(1,	_T(""),	LVCFMT_LEFT,	DFLT_CLIENTSOFT_COL_WIDTH);	//IDS_CD_CSOFT
	InsertColumn(2,	_T(""),	LVCFMT_LEFT,	DFLT_FILENAME_COL_WIDTH);	//IDS_FILE
	InsertColumn(3,	_T(""),	LVCFMT_RIGHT,	DFLT_DATARATE_COL_WIDTH);	//IDS_DL_SPEED
	InsertColumn(4,	_T(""),	LVCFMT_LEFT,	DFLT_PARTSTATUS_COL_WIDTH);	//IDS_AVAILABLEPARTS
	InsertColumn(5,	_T(""),	LVCFMT_RIGHT,	DFLT_SIZE_COL_WIDTH);		//IDS_CL_TRANSFDOWN
	InsertColumn(6,	_T(""),	LVCFMT_RIGHT,	DFLT_SIZE_COL_WIDTH);		//IDS_CL_TRANSFUP
	InsertColumn(7,	_T(""),	LVCFMT_LEFT,	100);						//IDS_META_SRCTYPE
	InsertColumn(8,	_T(""),	LVCFMT_LEFT,	140);						//IDS_GEOLOCATION

	static const int aiDefaultColumnOrder[] = {0, 3, 4, 2, 1, 7, 5, 6, 8};
	SetDefaultColumnOrder(aiDefaultColumnOrder, _countof(aiDefaultColumnOrder));
	SetAllIcons();
	Localize();
	LoadSettings();
	SetSortArrow();
	SortItems(SortProc, MAKELONG(GetSortItem(), !GetSortAscending()));
}

void CDownloadClientsCtrl::Localize()
{
	static const UINT uids[9] =
	{
		IDS_QL_USERNAME, IDS_CD_CSOFT, IDS_FILE, IDS_DL_SPEED, IDS_AVAILABLEPARTS
		, IDS_CL_TRANSFDOWN, IDS_CL_TRANSFUP, IDS_META_SRCTYPE, IDS_GEOLOCATION
	};

	LocaliseHeaderCtrl(uids, _countof(uids));
}

void CDownloadClientsCtrl::OnSysColorChange()
{
	CMuleListCtrl::OnSysColorChange();
	SetAllIcons();
}

void CDownloadClientsCtrl::SetAllIcons()
{
	ApplyImageList(NULL);
	// Apply the image list also to the listview control, even if we use our own 'DrawItem'.
	// This is needed to give the listview control a chance to initialize the row height.
	ASSERT((GetStyle() & LVS_SHAREIMAGELISTS) != 0);
	m_pImageList = &theApp.emuledlg->GetClientIconList();
	VERIFY(ApplyImageList(*m_pImageList) == NULL);
}

const CUpDownClient* CDownloadClientsCtrl::GetLiveClientByIndex(int iItem)
{
	if (iItem < 0 || iItem >= GetItemCount())
		return NULL;

	const CUpDownClient *client = reinterpret_cast<CUpDownClient*>(GetItemData(iItem));
	return IsLiveClient(client) ? client : NULL;
}

bool CDownloadClientsCtrl::IsLiveClient(const CUpDownClient *client) const
{
	return IsLiveDownloadClient(client);
}

bool CDownloadClientsCtrl::PruneStaleClientItems()
{
	bool bRemoved = false;
	for (int iItem = GetItemCount(); --iItem >= 0;) {
		if (!IsLiveClient(reinterpret_cast<CUpDownClient*>(GetItemData(iItem)))) {
			DeleteItem(iItem);
			bRemoved = true;
		}
	}

	if (bRemoved)
		theApp.emuledlg->transferwnd->UpdateListCount(CTransferWnd::wnd2Downloading, GetItemCount());

	return bRemoved;
}

void CDownloadClientsCtrl::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	if (!lpDrawItemStruct->itemData || theApp.IsClosing())
		return;

	CRect rcItem(lpDrawItemStruct->rcItem);
	CMemoryDC dc(CDC::FromHandle(lpDrawItemStruct->hDC), rcItem);
	BOOL bCtrlFocused;
	InitItemMemDC(dc, lpDrawItemStruct, bCtrlFocused);
	RECT rcClient;
	GetClientRect(&rcClient);
	const CUpDownClient *client = reinterpret_cast<CUpDownClient*>(lpDrawItemStruct->itemData);
	if (!IsLiveClient(client))
		return;

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
		rcItem.right = itemLeft + iColumnWidth;
		if (rcItem.left < rcItem.right && HaveIntersection(rcClient, rcItem)) {
			const CString &sItem(GetItemDisplayText(client, iColumn));
			switch (iColumn) {
			case 0: //user name
				{
					int iImage;
					UINT uOverlayImage;
					client->GetDisplayImage(iImage, uOverlayImage);

					rcItem.left += sm_iIconOffset;
					const POINT point{rcItem.left, rcItem.top + iIconY};
					m_pImageList->Draw(dc, iImage, point, ILD_NORMAL | INDEXTOOVERLAYMASK(uOverlayImage));
					rcItem.left += 16 + sm_iLabelOffset - sm_iSubItemInset;
					rcItem.left += sm_iSubItemInset;
					rcItem.right -= sm_iSubItemInset;
					dc.DrawText(sItem, &rcItem, MLC_DT_TEXT | uDrawTextAlignment);
					break;
				}
			case 8: //geo location
				if (theApp.geolocation != NULL) {
					const POINT point{itemLeft + sm_iIconOffset, rcItem.top + iIconY};
					if (theApp.geolocation->DrawFlag(dc, GetClientGeoIP(client), point))
						rcItem.left = itemLeft + sm_iIconOffset + 18 + sm_iLabelOffset - sm_iSubItemInset;
				}
				rcItem.left += sm_iSubItemInset;
				rcItem.right -= sm_iSubItemInset;
				dc.DrawText(sItem, &rcItem, MLC_DT_TEXT | uDrawTextAlignment);
				break;
			default: //any text column
				rcItem.left += sm_iSubItemInset;
				rcItem.right -= sm_iSubItemInset;
				dc.DrawText(sItem, &rcItem, MLC_DT_TEXT | uDrawTextAlignment);
				break;
			case 4: //download status bar
				++rcItem.top;
				--rcItem.bottom;
				if (DownloadProgressBarSeams::HasDrawableExtent(rcItem.Width(), rcItem.Height())) {
					const bool bUseFlatBar = thePrefs.UseFlatBar();
					const int iSavedDC = DownloadProgressBarSeams::ShouldIsolateFlatBarDcState(bUseFlatBar) ? dc.SaveDC() : 0;
					client->DrawStatusBar(dc, &rcItem, false, bUseFlatBar);
					if (iSavedDC != 0)
						dc.RestoreDC(iSavedDC);
				}
				++rcItem.bottom;
				--rcItem.top;
			}
		}
		itemLeft += iColumnWidth;
	}

	DrawFocusRect(dc, &lpDrawItemStruct->rcItem, lpDrawItemStruct->itemState & ODS_FOCUS, bCtrlFocused, lpDrawItemStruct->itemState & ODS_SELECTED);
}

CString CDownloadClientsCtrl::GetItemDisplayText(const CUpDownClient *client, int iSubItem) const
{
	CString sText;
	switch (iSubItem) {
	case 0:
		if (client->GetUserName() != NULL)
			sText = client->GetUserName();
		else
			sText.Format(_T("(%s)"), (LPCTSTR)GetResString(IDS_UNKNOWN));
		break;
	case 1:
		sText = client->DbgGetFullClientSoftVer();
		break;
	case 2:
		sText = client->GetRequestFile()->GetFileName();
		break;
	case 3:
		sText = CastItoXBytes((float)client->GetDownloadDatarate(), false, true);
		break;
	case 4:
		sText = GetResString(IDS_AVAILABLEPARTS);
		break;
	case 5:
		if (client->credits == NULL || client->GetSessionDown() >= client->credits->GetDownloadedTotal())
			sText = CastItoXBytes(client->GetSessionDown());
		else
			sText.Format(_T("%s (%s)"), (LPCTSTR)CastItoXBytes(client->GetSessionDown()), (LPCTSTR)CastItoXBytes(client->credits->GetDownloadedTotal()));
		break;
	case 6:
		if (client->credits == NULL || client->GetSessionUp() >= client->credits->GetUploadedTotal())
			sText = CastItoXBytes(client->GetSessionUp());
		else
			sText.Format(_T("%s (%s)"), (LPCTSTR)CastItoXBytes(client->GetSessionUp()), (LPCTSTR)CastItoXBytes(client->credits->GetUploadedTotal()));
		break;
	case 7:
		{
			UINT uid;
			switch (client->GetSourceFrom()) {
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
				uid = IDS_UNKNOWN;
			}
			sText = GetResString(uid);
		}
		break;
	case 8:
		if (theApp.geolocation != NULL)
			sText = theApp.geolocation->GetDisplayText(GetClientGeoIP(client));
	}
	return sText;
}

void CDownloadClientsCtrl::OnLvnGetDispInfo(LPNMHDR pNMHDR, LRESULT *pResult)
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
		const LVITEMW &rItem = reinterpret_cast<NMLVDISPINFO*>(pNMHDR)->item;
		if ((rItem.mask & LVIF_TEXT) && rItem.pszText != NULL && rItem.cchTextMax > 0) {
			const CUpDownClient *pClient = GetLiveClientByIndex(rItem.iItem);
			if (pClient != NULL)
				_tcsncpy_s(rItem.pszText, rItem.cchTextMax, GetItemDisplayText(pClient, rItem.iSubItem), _TRUNCATE);
			else
				rItem.pszText[0] = _T('\0');
		}
	}
	*pResult = 0;
}

void CDownloadClientsCtrl::OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult)
{
	const LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	bool sortAscending;
	if (GetSortItem() != pNMLV->iSubItem)
		switch (pNMLV->iSubItem) {
		case 1: // Client Software
		case 3: // Download Rate
		case 4: // Part Count
		case 5: // Session Down
		case 6: // Session Up
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
	PruneStaleClientItems();
	SortItems(SortProc, MAKELONG(pNMLV->iSubItem, !sortAscending));
	*pResult = 0;
}

int CALLBACK CDownloadClientsCtrl::SortProc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	const CUpDownClient *item1 = reinterpret_cast<CUpDownClient*>(lParam1);
	const CUpDownClient *item2 = reinterpret_cast<CUpDownClient*>(lParam2);
	const bool bLiveItem1 = IsLiveDownloadClient(item1);
	const bool bLiveItem2 = IsLiveDownloadClient(item2);
	if (!bLiveItem1 || !bLiveItem2)
		return bLiveItem1 ? -1 : (bLiveItem2 ? 1 : 0);

	int iResult = 0;
	switch (LOWORD(lParamSort)) {
	case 0: //user name
		if (item1->GetUserName() && item2->GetUserName())
			iResult = CompareLocaleStringNoCase(item1->GetUserName(), item2->GetUserName());
		else if (item1->GetUserName() == NULL)
			iResult = 1; // place clients with no user names at bottom
		else if (item2->GetUserName() == NULL)
			iResult = -1; // place clients with no user names at bottom
		break;
	case 1: //version
		if (item1->GetClientSoft() == item2->GetClientSoft())
			iResult = item1->GetVersion() - item2->GetVersion();
		else
			iResult = -(item1->GetClientSoft() - item2->GetClientSoft()); // invert result to place eMule's at top
		break;
	case 2: //file name
		{
			const CKnownFile *file1 = item1->GetRequestFile();
			const CKnownFile *file2 = item2->GetRequestFile();
			if ((file1 != NULL) && (file2 != NULL))
				iResult = CompareLocaleStringNoCase(file1->GetFileName(), file2->GetFileName());
			else if (file1 == NULL)
				iResult = 1;
			else
				iResult = -1;
		}
		break;
	case 3: //download rate
		iResult = CompareUnsigned(item1->GetDownloadDatarate(), item2->GetDownloadDatarate());
		break;
	case 4: //part count
		iResult = CompareUnsigned(item1->GetPartCount(), item2->GetPartCount());
		break;
	case 5: //session download
		iResult = CompareUnsigned(item1->GetSessionDown(), item2->GetSessionDown());
		break;
	case 6: //session upload
		iResult = CompareUnsigned(item1->GetSessionUp(), item2->GetSessionUp());
		break;
	case 7: //source origin
		iResult = item1->GetSourceFrom() - item2->GetSourceFrom();
		break;
	case 8:
		if (theApp.geolocation != NULL)
			iResult = CompareLocaleStringNoCase(theApp.geolocation->GetDisplayText(GetClientGeoIP(item1)), theApp.geolocation->GetDisplayText(GetClientGeoIP(item2)));
	}

	if (HIWORD(lParamSort))
		iResult = -iResult;

	//call secondary sort order, if the first one resulted as equal
	if (iResult == 0) {
		LPARAM iNextSort = theApp.emuledlg->transferwnd->GetDownloadClientsList()->GetNextSortOrder(lParamSort);
		if (iNextSort != -1)
			iResult = SortProc(lParam1, lParam2, iNextSort);
	}

	return iResult;
}

void CDownloadClientsCtrl::OnNmDblClk(LPNMHDR, LRESULT *pResult)
{
	int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
	if (iSel >= 0) {
		const CUpDownClient *client = GetLiveClientByIndex(iSel);
		if (client) {
			CClientDetailDialog dialog(const_cast<CUpDownClient*>(client), this);
			dialog.DoModal();
		}
	}
	*pResult = 0;
}

void CDownloadClientsCtrl::OnContextMenu(CWnd*, CPoint point)
{
	PruneStaleClientItems();
	int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
	const CUpDownClient *client = GetLiveClientByIndex(iSel);
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
	ClientMenu.AppendMenu(MF_POPUP | (client ? MF_ENABLED : MF_GRAYED), (UINT_PTR)CopyMenu.m_hMenu, GetResString(IDS_COPY));
	ClientMenu.AppendMenu(MF_SEPARATOR);
	ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && !client->IsBanned()) ? MF_ENABLED : MF_GRAYED), MP_BAN, GetResString(IDS_BAN));
	ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && client->IsBanned()) ? MF_ENABLED : MF_GRAYED), MP_UNBAN, GetResString(IDS_UNBAN));
	ClientMenu.AppendMenu(MF_STRING | (GetItemCount() > 0 ? MF_ENABLED : MF_GRAYED), MP_FIND, GetResString(IDS_FIND), _T("Search"));
	GetPopupMenuPos(*this, point);
	ClientMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
	VERIFY(CopyMenu.DestroyMenu());
}

BOOL CDownloadClientsCtrl::OnCommand(WPARAM wParam, LPARAM)
{
	wParam = LOWORD(wParam);

	if (wParam == MP_FIND) {
		OnFindStart();
		return TRUE;
	}

	int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
	if (iSel >= 0) {
		const CUpDownClient *client = GetLiveClientByIndex(iSel);
		switch (wParam) {
		case MP_SHOWLIST:
			if (client)
				const_cast<CUpDownClient*>(client)->RequestSharedFileList();
			break;
		case MP_MESSAGE:
			if (client)
				theApp.emuledlg->chatwnd->StartSession(const_cast<CUpDownClient*>(client));
			break;
		case MP_ADDFRIEND:
			if (client && theApp.friendlist->AddFriend(const_cast<CUpDownClient*>(client)))
				Update(iSel);
			break;
		case MP_BAN:
			if (client && !client->IsBanned()) {
				const_cast<CUpDownClient*>(client)->Ban(_T("Manual download-client ban"));
				Update(iSel);
			}
			break;
		case MP_UNBAN:
			if (client && client->IsBanned()) {
				const_cast<CUpDownClient*>(client)->UnBan();
				Update(iSel);
			}
			break;
		case MP_COPY_CLIENT_IP:
		case MP_COPY_CLIENT_USERHASH:
		case MP_COPY_CLIENT_SUMMARY:
			if (client) {
				CString text;
				if (wParam == MP_COPY_CLIENT_IP) {
					text = ipstr(GetClientGeoIP(client));
				} else if (wParam == MP_COPY_CLIENT_USERHASH) {
					text = md4str(client->GetUserHash());
				} else {
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
		case IDA_ENTER:
			if (client) {
				CClientDetailDialog dialog(const_cast<CUpDownClient*>(client), this);
				dialog.DoModal();
			}
			break;
		case MP_BOOT:
			if (client && client->GetKadPort() && client->GetKadVersion() >= KADEMLIA_VERSION2_47a)
				Kademlia::CKademlia::Bootstrap(ntohl(client->GetIP()), client->GetKadPort());
		}
	}
	return TRUE;
}

void CDownloadClientsCtrl::AddClient(const CUpDownClient *client)
{
	if (theApp.IsClosing())
		return;
	if (!IsLiveClient(client))
		return;
	PruneStaleClientItems();

	LVFINDINFO find;
	find.flags = LVFI_PARAM;
	find.lParam = (LPARAM)client;
	if (FindItem(&find) >= 0)
		return;

	int iItemCount = GetItemCount();
	InsertItem(LVIF_TEXT | LVIF_PARAM, iItemCount, LPSTR_TEXTCALLBACK, 0, 0, 0, (LPARAM)client);
	theApp.emuledlg->transferwnd->UpdateListCount(CTransferWnd::wnd2Downloading, iItemCount + 1);
}

void CDownloadClientsCtrl::RemoveClient(const CUpDownClient *client)
{
	if (theApp.IsClosing())
		return;

	LVFINDINFO find;
	find.flags = LVFI_PARAM;
	find.lParam = (LPARAM)client;
	bool bRemoved = false;
	for (;;) {
		int iItem = FindItem(&find);
		if (iItem < 0)
			break;
		DeleteItem(iItem);
		bRemoved = true;
	}
	if (bRemoved)
		theApp.emuledlg->transferwnd->UpdateListCount(CTransferWnd::wnd2Downloading, GetItemCount());
}

void CDownloadClientsCtrl::RefreshClient(const CUpDownClient *client)
{
	if (theApp.emuledlg->activewnd == theApp.emuledlg->transferwnd
		&& theApp.emuledlg->transferwnd->GetDownloadClientsList()->IsWindowVisible()
		&& !theApp.IsClosing())
	{
		if (!IsLiveClient(client)) {
			RemoveClient(client);
			return;
		}

		LVFINDINFO find;
		find.flags = LVFI_PARAM;
		find.lParam = (LPARAM)client;
		int iItem = FindItem(&find);
		if (iItem >= 0)
			Update(iItem);
	}
}

void CDownloadClientsCtrl::ShowSelectedUserDetails()
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

	const CUpDownClient *client = GetLiveClientByIndex(GetSelectionMark());
	if (client) {
		CClientDetailDialog dialog(const_cast<CUpDownClient*>(client), this);
		dialog.DoModal();
	}
}
