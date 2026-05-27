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
#include "DisplayRefreshSeams.h"
#include "UploadListCtrl.h"
#include "TransferWnd.h"
#include "TransferDlg.h"
#include "UpDownClient.h"
#include "MenuCmds.h"
#include "MenuShortcutLabels.h"
#include "ClientDetailDialog.h"
#include "emuledlg.h"
#include "friendlist.h"
#include "MemDC.h"
#include "GeoLocation.h"
#include "KnownFile.h"
#include "SharedFileList.h"
#include "ClientCredits.h"
#include "ClientList.h"
#include "ChatWnd.h"
#include "kademlia/kademlia/Kademlia.h"
#include "UploadQueue.h"
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

	const CKnownFile* GetUploadClientFile(const CUpDownClient* client)
	{
		return client != NULL ? theApp.sharedfiles->GetFileByID(client->GetUploadFileID()) : NULL;
	}

	bool IsLiveUploadingClient(const CUpDownClient *client)
	{
		if (client == NULL || theApp.clientlist == NULL || theApp.uploadqueue == NULL)
			return false;
		if (!theApp.clientlist->ContainsClientPointer(client))
			return false;

		for (POSITION pos = theApp.uploadqueue->GetFirstFromUploadList(); pos != NULL;) {
			if (theApp.uploadqueue->GetNextFromUploadList(pos) == client)
				return true;
		}
		return false;
	}

	CString FormatUploadRatio(float fRatio)
	{
		CString str;
		str.Format(_T("%.1f"), fRatio);
		return str;
	}

	CString FormatCooldown(ULONGLONG ullRemainingMs)
	{
		if (ullRemainingMs == 0)
			return _T("-");
		CString str;
		str.Format(_T("%us"), static_cast<UINT>((ullRemainingMs + 999) / 1000));
		return str;
	}

	CString FormatUploadScoreColumn(const CUpDownClient *client)
	{
		return UploadScoreSeams::FormatUploadScoreCompact(
			client->GetScoreBreakdown(false, client->IsDownloading(), false),
			GetResString(IDS_LOW_RATIO_BONUS),
			GetResString(IDS_UPLOAD_POLICY_LOWID_DIVISOR),
			GetResString(IDS_COOLDOWN),
			GetResString(IDS_FRIENDDETAIL),
			_T("-"));
	}

	int CompareRatio(float fLeft, float fRight)
	{
		if (fLeft < fRight)
			return -1;
		if (fLeft > fRight)
			return 1;
		return 0;
	}
}


IMPLEMENT_DYNAMIC(CUploadListCtrl, CMuleListCtrl)

BEGIN_MESSAGE_MAP(CUploadListCtrl, CMuleListCtrl)
	ON_NOTIFY_REFLECT(LVN_COLUMNCLICK, OnLvnColumnClick)
	ON_NOTIFY_REFLECT(LVN_GETDISPINFO, OnLvnGetDispInfo)
	ON_NOTIFY_REFLECT(LVN_GETINFOTIP, OnLvnGetInfoTip)
	ON_NOTIFY_REFLECT(NM_DBLCLK, OnNmDblClk)
	ON_WM_CONTEXTMENU()
	ON_WM_SYSCOLORCHANGE()
END_MESSAGE_MAP()

CUploadListCtrl::CUploadListCtrl()
	: CListCtrlItemWalk(this)
	, m_ullLastRefreshSortTick()
{
	SetGeneralPurposeFind(true);
	SetSkinKey(_T("UploadsLv"));
}

void CUploadListCtrl::Init()
{
	SetPrefsKey(_T("UploadListCtrl"));
	SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	CToolTipCtrl *tooltip = GetToolTips();
	if (tooltip) {
		m_tooltip.SubclassWindow(tooltip->m_hWnd);
		tooltip->ModifyStyle(0, TTS_NOPREFIX);
		tooltip->SetDelayTime(TTDT_AUTOPOP, SEC2MS(20));
		tooltip->SetDelayTime(TTDT_INITIAL, SEC2MS(thePrefs.GetToolTipDelay()));
	}

	InsertColumn(0,	_T(""),	LVCFMT_LEFT, DFLT_CLIENTNAME_COL_WIDTH);	//IDS_QL_USERNAME
	InsertColumn(1,	_T(""),	LVCFMT_LEFT, DFLT_FILENAME_COL_WIDTH);		//IDS_FILE
	InsertColumn(2,	_T(""),	LVCFMT_RIGHT,DFLT_DATARATE_COL_WIDTH);		//IDS_DL_SPEED
	InsertColumn(3,	_T(""),	LVCFMT_RIGHT,DFLT_DATARATE_COL_WIDTH);		//IDS_DL_TRANSF
	InsertColumn(4,	_T(""),	LVCFMT_LEFT, 60);							//IDS_WAITED
	InsertColumn(5,	_T(""),	LVCFMT_LEFT, 80);							//IDS_UPLOADTIME
	InsertColumn(6,	_T(""),	LVCFMT_LEFT, 100);							//IDS_STATUS
	InsertColumn(7,	_T(""),	LVCFMT_RIGHT,85);							//IDS_ALL_TIME_RATIO
	InsertColumn(8,	_T(""),	LVCFMT_RIGHT,85);							//IDS_SESSION_RATIO
	InsertColumn(9,	_T(""),	LVCFMT_RIGHT,70);							//IDS_COOLDOWN
	InsertColumn(10,_T(""),	LVCFMT_LEFT, 145);							//IDS_EFFECTIVE_SCORE
	InsertColumn(11,_T(""),	LVCFMT_LEFT, DFLT_PARTSTATUS_COL_WIDTH);	//IDS_UPSTATUS
	InsertColumn(12,_T(""),	LVCFMT_LEFT, 140);							//IDS_GEOLOCATION
	InsertColumn(13,_T(""),	LVCFMT_LEFT, 100);							//IDS_CD_CSOFT
	InsertColumn(14,_T(""),	LVCFMT_LEFT, 100);							//IDS_CLIENT_UPLOADED
	InsertColumn(15,_T(""),	LVCFMT_LEFT, 100);							//IDS_IP
	InsertColumn(16,_T(""),	LVCFMT_LEFT, 70);							//IDS_IDLOW
	InsertColumn(17,_T(""),	LVCFMT_LEFT, 100);							//IDS_CLIENT_HASH
	InsertColumn(18,_T(""),	LVCFMT_RIGHT, 70);							//IDS_UPLOAD_PCT
	InsertColumn(19,_T(""),	LVCFMT_RIGHT, 85);							//IDS_FILE_SIZE
	InsertColumn(20,_T(""),	LVCFMT_RIGHT, 70);							//IDS_ETA
	InsertColumn(21,_T(""),	LVCFMT_LEFT, 120);							//IDS_FOLDER

	if (const auto *pProfile = MuleListCtrlViewPresets::FindProfile(_T("UploadListCtrl")))
		SetViewPresetProfile(*pProfile);
	SetAllIcons();
	Localize();
	LoadSettings();
	SetSortArrow();
	SortItems(SortProc, MAKELONG(GetSortItem(), !GetSortAscending()));
}

void CUploadListCtrl::Localize()
{
	static const UINT uids[22] =
	{
		IDS_QL_USERNAME, IDS_FILE, IDS_DL_SPEED, IDS_DL_TRANSF, IDS_WAITED
		, IDS_UPLOADTIME, IDS_STATUS, IDS_ALL_TIME_RATIO, IDS_SESSION_RATIO, IDS_COOLDOWN, IDS_EFFECTIVE_SCORE, IDS_UPSTATUS, IDS_GEOLOCATION
		, IDS_CD_CSOFT, IDS_CLIENT_UPLOADED, IDS_IP, IDS_IDLOW, IDS_CLIENT_HASH, IDS_UPLOAD_PCT, IDS_FILE_SIZE, IDS_ETA, IDS_FOLDER
	};

	LocaliseHeaderCtrl(uids, _countof(uids));
}

void CUploadListCtrl::OnSysColorChange()
{
	CMuleListCtrl::OnSysColorChange();
	SetAllIcons();
}

void CUploadListCtrl::SetAllIcons()
{
	ApplyImageList(NULL);
	// Apply the image list also to the listview control, even if we use our own 'DrawItem'.
	// This is needed to give the listview control a chance to initialize the row height.
	ASSERT((GetStyle() & LVS_SHAREIMAGELISTS) != 0);
	m_pImageList = &theApp.emuledlg->GetClientIconList();
	VERIFY(ApplyImageList(*m_pImageList) == NULL);
}

const CUpDownClient* CUploadListCtrl::GetLiveClientByIndex(int iItem)
{
	if (iItem < 0 || iItem >= GetItemCount())
		return NULL;

	const CUpDownClient *client = reinterpret_cast<CUpDownClient*>(GetItemData(iItem));
	return IsLiveClient(client) ? client : NULL;
}

bool CUploadListCtrl::IsLiveClient(const CUpDownClient *client) const
{
	return IsLiveUploadingClient(client);
}

bool CUploadListCtrl::PruneStaleClientItems()
{
	bool bRemoved = false;
	for (int iItem = GetItemCount(); --iItem >= 0;) {
		if (!IsLiveClient(reinterpret_cast<CUpDownClient*>(GetItemData(iItem)))) {
			DeleteItem(iItem);
			bRemoved = true;
		}
	}

	if (bRemoved)
		theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount(CTransferWnd::wnd2Uploading);

	return bRemoved;
}

CObject* CUploadListCtrl::WalkToLiveClientItem(int iDirection)
{
	const int iItemCount = GetItemCount();
	if (iItemCount < 2)
		return NULL;

	POSITION pos = GetFirstSelectedItemPosition();
	if (pos == NULL)
		return NULL;

	const int iCurSelItem = GetNextSelectedItem(pos);
	for (int iItem = iCurSelItem + iDirection; iItem >= 0 && iItem < iItemCount; iItem += iDirection) {
		CUpDownClient *client = reinterpret_cast<CUpDownClient*>(GetItemData(iItem));
		if (!IsLiveClient(client))
			continue;

		SetItemState(iCurSelItem, 0, LVIS_SELECTED | LVIS_FOCUSED);
		SetItemState(iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		SetSelectionMark(iItem);
		EnsureVisible(iItem, FALSE);
		return client;
	}
	return NULL;
}

CObject* CUploadListCtrl::GetPrevSelectableItem()
{
	return WalkToLiveClientItem(-1);
}

CObject* CUploadListCtrl::GetNextSelectableItem()
{
	return WalkToLiveClientItem(1);
}

void CUploadListCtrl::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
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
	if (client->GetSlotNumber() > (UINT)theApp.uploadqueue->GetActiveUploadsCount())
		dc.SetTextColor(::GetSysColor(COLOR_GRAYTEXT));

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
			case 12: //geo location
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
			case 11: //upload status bar
				++rcItem.top;
				--rcItem.bottom;
				client->DrawUpStatusBar(dc, &rcItem, false, thePrefs.UseFlatBar());
				++rcItem.bottom;
				--rcItem.top;
			}
		}
		itemLeft += iColumnWidth;
	}

	DrawFocusRect(dc, &lpDrawItemStruct->rcItem, lpDrawItemStruct->itemState & ODS_FOCUS, bCtrlFocused, lpDrawItemStruct->itemState & ODS_SELECTED);
}

CString  CUploadListCtrl::GetItemDisplayText(const CUpDownClient *client, int iSubItem) const
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
		{
			const CKnownFile *file = GetUploadClientFile(client);
			if (file)
				sText = file->GetFileName();
		}
		break;
	case 2:
		sText = CastItoXBytes(client->GetUploadDatarate(), false, true);
		break;
	case 3:
		// NOTE: If you change (add/remove) anything which is displayed here, update also the sorting part.
		sText.Format(_T("%s (%s)"), (LPCTSTR)CastItoXBytes(client->GetSessionUp()), (LPCTSTR)CastItoXBytes(client->GetQueueSessionPayloadUp()));
		break;
	case 4:
		if (!client->HasLowID())
			sText = CastSecondsToHM(client->GetWaitTime() / SEC2MS(1));
		else
			sText.Format(_T("%s (%s)"), (LPCTSTR)CastSecondsToHM(client->GetWaitTime() / SEC2MS(1)), (LPCTSTR)GetResString(IDS_IDLOW));
		break;
	case 5:
		sText = CastSecondsToHM(client->GetUpStartTimeDelay() / SEC2MS(1));
		break;
	case 6:
		sText = client->GetUploadStateDisplayString();
		break;
	case 7:
		{
			const CKnownFile *file = GetUploadClientFile(client);
			sText = file ? FormatUploadRatio(file->GetAllTimeUploadRatio()) : _T("-");
		}
		break;
	case 8:
		{
			const CKnownFile *file = GetUploadClientFile(client);
			sText = file ? FormatUploadRatio(file->GetSessionUploadRatio()) : _T("-");
		}
		break;
	case 9:
		sText = client->GetFriendSlot() ? _T("-") : FormatCooldown(client->GetSlowUploadCooldownRemaining());
		break;
	case 10:
		sText = FormatUploadScoreColumn(client);
		break;
	case 11:
		sText = GetResString(IDS_UPSTATUS);
		break;
	case 12:
		if (theApp.geolocation != NULL)
			sText = theApp.geolocation->GetDisplayText(GetClientGeoIP(client));
		break;
	case 13:
		sText = client->DbgGetFullClientSoftVer();
		break;
	case 14:
		if (client->Credits() != NULL)
			sText = CastItoXBytes(client->Credits()->GetUploadedTotal(), false, false);
		else
			sText = _T("?");
		break;
	case 15:
		sText = ipstr(GetClientGeoIP(client));
		break;
	case 16:
		sText = GetResString(client->HasLowID() ? IDS_IDLOW : IDS_IDHIGH);
		break;
	case 17:
		sText = client->HasValidHash() ? CString(md4str(client->GetUserHash())) : CString(_T("?"));
		break;
	case 18:
		{
			const CKnownFile *file = GetUploadClientFile(client);
			const uint64 uFileSize = file != NULL ? static_cast<uint64>(file->GetFileSize()) : 0;
			if (uFileSize > 0)
				sText.Format(_T("%.1f%%"), static_cast<double>(client->GetSessionUp()) * 100.0 / static_cast<double>(uFileSize));
			else
				sText = _T("-");
		}
		break;
	case 19:
		{
			const CKnownFile *file = GetUploadClientFile(client);
			sText = file ? CastItoXBytes(file->GetFileSize()) : _T("-");
		}
		break;
	case 20:
		{
			const CKnownFile *file = GetUploadClientFile(client);
			if (file != NULL && file->GetFileSize() > client->GetSessionUp()) {
				const uint64 uDataLeft = file->GetFileSize() - client->GetSessionUp();
				const uint32 uDataRate = client->GetUploadDatarate();
				if (uDataLeft > 1024 && uDataRate > 1024) {
					const uint64 uEta = uDataLeft / uDataRate;
					if (uEta > 0 && uEta < 60 * 60 * 10)
						sText = CastSecondsToHM(static_cast<time_t>(uEta));
					else
						sText = _T("-");
				}
				else
					sText = _T("-");
			}
			else
				sText = _T("-");
		}
		break;
	case 21:
		{
			const CKnownFile *file = GetUploadClientFile(client);
			if (file != NULL)
				sText = file->GetPath();
		}
	}
	return sText;
}

void CUploadListCtrl::OnLvnGetDispInfo(LPNMHDR pNMHDR, LRESULT *pResult)
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

void CUploadListCtrl::OnLvnGetInfoTip(LPNMHDR pNMHDR, LRESULT *pResult)
{
	LPNMLVGETINFOTIP pGetInfoTip = reinterpret_cast<LPNMLVGETINFOTIP>(pNMHDR);
	LVHITTESTINFO hti;
	if (pGetInfoTip->iSubItem == 0 && ::GetCursorPos(&hti.pt)) {
		ScreenToClient(&hti.pt);
		if (SubItemHitTest(&hti) == -1 || hti.iItem != pGetInfoTip->iItem || hti.iSubItem != 0) {
			// don't show the default label tip for the main item, if the mouse is not over the main item
			if ((pGetInfoTip->dwFlags & LVGIT_UNFOLDED) == 0 && pGetInfoTip->cchTextMax > 0 && pGetInfoTip->pszText)
				pGetInfoTip->pszText[0] = _T('\0');
			return;
		}

		const CUpDownClient *client = GetLiveClientByIndex(pGetInfoTip->iItem);
		if (client && pGetInfoTip->pszText && pGetInfoTip->cchTextMax > 0) {
			CString strInfo;
			strInfo.Format(GetResString(IDS_USERINFO), client->GetUserName());
			const CKnownFile *file = theApp.sharedfiles->GetFileByID(client->GetUploadFileID());
			if (file) {
				strInfo.AppendFormat(_T("%s %s\n"), (LPCTSTR)GetResString(IDS_SF_REQUESTED), (LPCTSTR)file->GetFileName());
				strInfo.AppendFormat(GetResString(IDS_FILESTATS_SESSION) + GetResString(IDS_FILESTATS_TOTAL)
					, file->statistic.GetAccepts(), file->statistic.GetRequests(), (LPCTSTR)CastItoXBytes(file->statistic.GetTransferred())
					, file->statistic.GetAllTimeAccepts(), file->statistic.GetAllTimeRequests(), (LPCTSTR)CastItoXBytes(file->statistic.GetAllTimeTransferred()));
			} else
				strInfo += GetResString(IDS_REQ_UNKNOWNFILE);

			strInfo += TOOLTIP_AUTOFORMAT_SUFFIX_CH;
			_tcsncpy_s(pGetInfoTip->pszText, pGetInfoTip->cchTextMax, strInfo, _TRUNCATE);
		}
	}
	*pResult = 0;
}

void CUploadListCtrl::OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult)
{
	const LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	bool sortAscending;
	if (GetSortItem() != pNMLV->iSubItem) {
		switch (pNMLV->iSubItem) {
		case 2: // Data rate
		case 3: // Session Up
		case 4: // Wait Time
		case 7: // All-time ratio
		case 8: // Session ratio
		case 9: // Cooldown
		case 10: // Effective score
		case 11: // Part Count
		case 14: // Client Uploaded
		case 18: // Upload %
		case 19: // File Size
		case 20: // ETA
			sortAscending = false;
			break;
		default:
			sortAscending = true;
		}
	} else
		sortAscending = !GetSortAscending();

	// Sort table
	UpdateSortHistory(MAKELONG(pNMLV->iSubItem, !sortAscending));
	SetSortArrow(pNMLV->iSubItem, sortAscending);
	PruneStaleClientItems();
	SortItems(SortProc, MAKELONG(pNMLV->iSubItem, !sortAscending));
	*pResult = 0;
}

int CALLBACK CUploadListCtrl::SortProc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	const CUpDownClient *item1 = reinterpret_cast<CUpDownClient*>(lParam1);
	const CUpDownClient *item2 = reinterpret_cast<CUpDownClient*>(lParam2);
	const bool bLiveItem1 = IsLiveUploadingClient(item1);
	const bool bLiveItem2 = IsLiveUploadingClient(item2);
	if (!bLiveItem1 || !bLiveItem2)
		return bLiveItem1 ? -1 : (bLiveItem2 ? 1 : 0);

	int iResult = 0;
	switch (LOWORD(lParamSort)) {
	case 0:
		if (item1->GetUserName() && item2->GetUserName())
			iResult = CompareLocaleStringNoCase(item1->GetUserName(), item2->GetUserName());
		else if (item1->GetUserName() == NULL || item2->GetUserName() == NULL)
			iResult = 1; // place clients with no user names at the bottom
		break;
	case 1:
		{
			const CKnownFile *file1 = GetUploadClientFile(item1);
			const CKnownFile *file2 = GetUploadClientFile(item2);
			if (file1 != NULL && file2 != NULL)
				iResult = CompareLocaleStringNoCase(file1->GetFileName(), file2->GetFileName());
			else
				iResult = (file1 == NULL) ? 1 : -1;
		}
		break;
	case 2:
		iResult = CompareUnsigned(item1->GetUploadDatarate(), item2->GetUploadDatarate());
		break;
	case 3:
		iResult = CompareUnsigned(item1->GetSessionUp(), item2->GetSessionUp());
		if (iResult == 0)
			iResult = CompareUnsigned(item1->GetQueueSessionPayloadUp(), item2->GetQueueSessionPayloadUp());
		break;
	case 4:
		iResult = CompareUnsigned(item1->GetWaitTime(), item2->GetWaitTime());
		break;
	case 5:
		iResult = CompareUnsigned(item1->GetUpStartTimeDelay(), item2->GetUpStartTimeDelay());
		break;
	case 6:
		iResult = item1->GetUploadState() - item2->GetUploadState();
		break;
	case 7:
		{
			const CKnownFile *file1 = GetUploadClientFile(item1);
			const CKnownFile *file2 = GetUploadClientFile(item2);
			if (file1 != NULL && file2 != NULL)
				iResult = CompareRatio(file1->GetAllTimeUploadRatio(), file2->GetAllTimeUploadRatio());
			else
				iResult = (file1 == NULL) ? 1 : -1;
		}
		break;
	case 8:
		{
			const CKnownFile *file1 = GetUploadClientFile(item1);
			const CKnownFile *file2 = GetUploadClientFile(item2);
			if (file1 != NULL && file2 != NULL)
				iResult = CompareRatio(file1->GetSessionUploadRatio(), file2->GetSessionUploadRatio());
			else
				iResult = (file1 == NULL) ? 1 : -1;
		}
		break;
	case 9:
		iResult = CompareUnsigned(item1->GetSlowUploadCooldownRemaining(), item2->GetSlowUploadCooldownRemaining());
		break;
	case 10:
		iResult = CompareUnsigned(item1->GetScore(false), item2->GetScore(false));
		if (iResult == 0) {
			iResult = CompareUnsigned(
				UploadScoreSeams::BuildUploadScoreModifierSortKey(item1->GetScoreBreakdown(false, item1->IsDownloading(), false)),
				UploadScoreSeams::BuildUploadScoreModifierSortKey(item2->GetScoreBreakdown(false, item2->IsDownloading(), false)));
		}
		break;
	case 11:
		iResult = CompareUnsigned(item1->GetUpPartCount(), item2->GetUpPartCount());
		break;
	case 12:
		if (theApp.geolocation != NULL)
			iResult = CompareLocaleStringNoCase(theApp.geolocation->GetDisplayText(GetClientGeoIP(item1)), theApp.geolocation->GetDisplayText(GetClientGeoIP(item2)));
		break;
	case 13:
		iResult = CompareLocaleStringNoCase(item1->DbgGetFullClientSoftVer(), item2->DbgGetFullClientSoftVer());
		break;
	case 14:
		if (item1->Credits() != NULL && item2->Credits() != NULL)
			iResult = CompareUnsigned(item1->Credits()->GetUploadedTotal(), item2->Credits()->GetUploadedTotal());
		else if (item1->Credits() == NULL || item2->Credits() == NULL)
			iResult = (item1->Credits() == NULL) ? 1 : -1;
		break;
	case 15:
		iResult = CompareLocaleStringNoCase(ipstr(GetClientGeoIP(item1)), ipstr(GetClientGeoIP(item2)));
		break;
	case 16:
		iResult = CompareUnsigned(item1->HasLowID(), item2->HasLowID());
		break;
	case 17:
		if (item1->HasValidHash() && item2->HasValidHash())
			iResult = CompareLocaleStringNoCase(md4str(item1->GetUserHash()), md4str(item2->GetUserHash()));
		else if (!item1->HasValidHash() || !item2->HasValidHash())
			iResult = item1->HasValidHash() ? -1 : 1;
		break;
	case 18:
		{
			const CKnownFile *file1 = GetUploadClientFile(item1);
			const CKnownFile *file2 = GetUploadClientFile(item2);
			const uint64 uFileSize1 = file1 != NULL ? static_cast<uint64>(file1->GetFileSize()) : 0;
			const uint64 uFileSize2 = file2 != NULL ? static_cast<uint64>(file2->GetFileSize()) : 0;
			if (uFileSize1 > 0 && uFileSize2 > 0) {
				const double fPct1 = static_cast<double>(item1->GetSessionUp()) * 1000.0 / static_cast<double>(uFileSize1);
				const double fPct2 = static_cast<double>(item2->GetSessionUp()) * 1000.0 / static_cast<double>(uFileSize2);
				iResult = (fPct1 < fPct2) ? -1 : static_cast<int>(fPct1 > fPct2);
			}
			else
				iResult = (uFileSize1 == 0) ? 1 : -1;
		}
		break;
	case 19:
		{
			const CKnownFile *file1 = GetUploadClientFile(item1);
			const CKnownFile *file2 = GetUploadClientFile(item2);
			if (file1 != NULL && file2 != NULL)
				iResult = CompareUnsigned(file1->GetFileSize(), file2->GetFileSize());
			else
				iResult = (file1 == NULL) ? 1 : -1;
		}
		break;
	case 20:
		{
			const CKnownFile *file1 = GetUploadClientFile(item1);
			const CKnownFile *file2 = GetUploadClientFile(item2);
			if (file1 != NULL && file2 != NULL && file1->GetFileSize() > item1->GetSessionUp() && file2->GetFileSize() > item2->GetSessionUp()) {
				const uint64 uDataLeft1 = file1->GetFileSize() - item1->GetSessionUp();
				const uint32 uDataRate1 = item1->GetUploadDatarate();
				const uint64 uDataLeft2 = file2->GetFileSize() - item2->GetSessionUp();
				const uint32 uDataRate2 = item2->GetUploadDatarate();
				if (uDataLeft1 > 1024 && uDataRate1 > 1024 && uDataLeft2 > 1024 && uDataRate2 > 1024) {
					const uint64 uEta1 = uDataLeft1 / uDataRate1;
					const uint64 uEta2 = uDataLeft2 / uDataRate2;
					if (uEta1 > 0 && uEta1 < 60 * 60 * 24 && uEta2 > 0 && uEta2 < 60 * 60 * 24)
						iResult = CompareUnsigned(uEta1, uEta2);
				}
			}
		}
		break;
	case 21:
		{
			const CKnownFile *file1 = GetUploadClientFile(item1);
			const CKnownFile *file2 = GetUploadClientFile(item2);
			if (file1 != NULL && file2 != NULL)
				iResult = CompareLocaleStringNoCase(file1->GetPath(), file2->GetPath());
			else
				iResult = (file1 == NULL) ? 1 : -1;
		}
	}

	if (HIWORD(lParamSort))
		iResult = -iResult;

	//call secondary sort order, if the first one resulted as equal
	if (iResult == 0) {
		LPARAM iNextSort = theApp.emuledlg->transferwnd->m_pwndTransfer->uploadlistctrl.GetNextSortOrder(lParamSort);
		if (iNextSort != -1)
			iResult = SortProc(lParam1, lParam2, iNextSort);
	}

	return iResult;
}

void CUploadListCtrl::OnNmDblClk(LPNMHDR, LRESULT *pResult)
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

void CUploadListCtrl::OnContextMenu(CWnd*, CPoint point)
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
	ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && client->IsFriend()) ? MF_ENABLED : MF_GRAYED), MP_REMOVEFRIEND, GetResString(IDS_REMOVEFRIEND), _T("DELETEFRIEND"));
	ClientMenu.AppendMenu(MF_STRING | (is_ed2k ? MF_ENABLED : MF_GRAYED), MP_MESSAGE, GetResString(IDS_SEND_MSG), _T("SENDMESSAGE"));
	ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && client->GetViewSharedFilesSupport()) ? MF_ENABLED : MF_GRAYED), MP_SHOWLIST, GetResString(IDS_VIEWFILES), _T("VIEWFILES"));
	const CKnownFile *file = GetUploadClientFile(client);
	const bool bCanOpenFile = (file != NULL && !file->IsPartFile());
	ClientMenu.AppendMenu(MF_STRING | (bCanOpenFile ? MF_ENABLED : MF_GRAYED), MP_OPEN, GetResString(IDS_OPENFILE), _T("OPENFILE"));
	ClientMenu.AppendMenu(MF_STRING | (bCanOpenFile ? MF_ENABLED : MF_GRAYED), MP_OPENFOLDER, GetResString(IDS_OPENFOLDER), _T("OPENFOLDER"));
	CTitledMenu CopyMenu;
	CopyMenu.CreateMenu();
	CopyMenu.AddMenuTitle(NULL, true);
	CopyMenu.AppendMenu(MF_STRING | (client ? MF_ENABLED : MF_GRAYED), MP_COPY_CLIENT_IP, GetResString(IDS_COPY_CLIENT_IP));
	CopyMenu.AppendMenu(MF_STRING | (client ? MF_ENABLED : MF_GRAYED), MP_COPY_CLIENT_USERHASH, GetResString(IDS_COPY_USER_HASH));
	CopyMenu.AppendMenu(MF_STRING | (client ? MF_ENABLED : MF_GRAYED), MP_COPY_CLIENT_SUMMARY, GetResString(IDS_COPY_CLIENT_SUMMARY));
	CopyMenu.AppendMenu(MF_STRING | (bCanOpenFile ? MF_ENABLED : MF_GRAYED), MP_COPY_FILE_NAME, GetResString(IDS_COPY_FILE_NAME));
	CopyMenu.AppendMenu(MF_STRING | (bCanOpenFile ? MF_ENABLED : MF_GRAYED), MP_COPY_ED2K_HASH, GetResString(IDS_COPY_HASH));
	CopyMenu.AppendMenu(MF_STRING | (bCanOpenFile ? MF_ENABLED : MF_GRAYED), MP_COPY_FILE_PATH, GetResString(IDS_COPY_FILE_PATH));
	CopyMenu.AppendMenu(MF_STRING | (bCanOpenFile ? MF_ENABLED : MF_GRAYED), MP_COPY_FOLDER_PATH, GetResString(IDS_COPY_FOLDER_PATH));
	ClientMenu.AppendMenu(MF_POPUP | (client ? MF_ENABLED : MF_GRAYED), (UINT_PTR)CopyMenu.m_hMenu, GetResString(IDS_COPY));
	ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && !client->IsBanned()) ? MF_ENABLED : MF_GRAYED), MP_BAN, GetResString(IDS_BAN));
	ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && client->IsBanned()) ? MF_ENABLED : MF_GRAYED), MP_UNBAN, GetResString(IDS_UNBAN));
	if (Kademlia::CKademlia::IsRunning() && !Kademlia::CKademlia::IsConnected())
		ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && client->GetKadPort() && client->GetKadVersion() >= KADEMLIA_VERSION2_47a) ? MF_ENABLED : MF_GRAYED), MP_BOOT, GetResString(IDS_BOOTSTRAP));
	ClientMenu.AppendMenu(MF_STRING | (GetItemCount() > 0 ? MF_ENABLED : MF_GRAYED), MP_FIND, AddMenuShortcutLabel(GetResString(IDS_FIND), _T("Ctrl+F")), _T("Search"));
	GetPopupMenuPos(*this, point);
	ClientMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
	VERIFY(CopyMenu.DestroyMenu());
}

BOOL CUploadListCtrl::OnCommand(WPARAM wParam, LPARAM)
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
			if (client && !const_cast<CUpDownClient*>(client)->RequestSharedFileList())
				return TRUE;
			break;
		case MP_MESSAGE:
			if (client)
				theApp.emuledlg->chatwnd->StartSession(const_cast<CUpDownClient*>(client));
			break;
		case MP_ADDFRIEND:
			if (client && theApp.friendlist->AddFriend(const_cast<CUpDownClient*>(client)))
				Update(iSel);
			break;
		case MP_REMOVEFRIEND:
			if (client) {
				CFriend *pFriend = theApp.friendlist->SearchFriend(client->GetUserHash(), 0, 0);
				if (pFriend != NULL) {
					theApp.friendlist->RemoveFriend(pFriend);
					Update(iSel);
				}
			}
			break;
		case MP_BAN:
			if (client && !client->IsBanned()) {
				const_cast<CUpDownClient*>(client)->Ban(GetResString(IDS_BAN_ARBITRARY));
				Update(iSel);
			}
			break;
		case MP_UNBAN:
			if (client && client->IsBanned()) {
				const_cast<CUpDownClient*>(client)->UnBan();
				Update(iSel);
			}
			break;
		case MP_COPY_ED2K_HASH:
		case MP_COPY_FILE_NAME:
		case MP_COPY_FILE_PATH:
		case MP_COPY_FOLDER_PATH:
		case MP_COPY_CLIENT_IP:
		case MP_COPY_CLIENT_USERHASH:
		case MP_COPY_CLIENT_SUMMARY:
			if (client) {
				const CKnownFile *file = GetUploadClientFile(client);
				CString text;
				if (wParam == MP_COPY_CLIENT_IP)
					text = ipstr(GetClientGeoIP(client));
				else if (wParam == MP_COPY_CLIENT_USERHASH)
					text = md4str(client->GetUserHash());
				else if (wParam == MP_COPY_CLIENT_SUMMARY) {
					std::vector<ProUserMenuCopySeams::NamedField> fields;
					ProUserMenuCopySeams::AppendField(fields, _T("username"), client->GetUserName());
					ProUserMenuCopySeams::AppendField(fields, _T("client"), client->GetClientSoftVer());
					ProUserMenuCopySeams::AppendField(fields, _T("ip"), ipstr(GetClientGeoIP(client)));
					ProUserMenuCopySeams::AppendField(fields, _T("userhash"), md4str(client->GetUserHash()));
					ProUserMenuCopySeams::AppendField(fields, _T("upload_state"), client->GetUploadStateDisplayString());
					if (file != NULL && !file->IsPartFile()) {
						ProUserMenuCopySeams::AppendField(fields, _T("file"), file->GetFileName());
						ProUserMenuCopySeams::AppendField(fields, _T("filehash"), md4str(file->GetFileHash()));
					}
					text = ProUserMenuCopySeams::FormatSummary(fields);
				} else if (file != NULL && !file->IsPartFile()) {
					if (wParam == MP_COPY_ED2K_HASH)
						text = md4str(file->GetFileHash());
					else if (wParam == MP_COPY_FILE_NAME)
						text = file->GetFileName();
					else if (wParam == MP_COPY_FILE_PATH)
						text = file->GetFilePath();
					else if (wParam == MP_COPY_FOLDER_PATH)
						text = file->GetPath();
				}
				if (!text.IsEmpty())
					theApp.CopyTextToClipboard(text);
			}
			break;
		case MP_OPEN:
			if (client) {
				const CKnownFile *file = GetUploadClientFile(client);
				if (file != NULL && !file->IsPartFile())
					ShellDefaultVerb(file->GetFilePath());
			}
			break;
		case MP_OPENFOLDER:
			if (client) {
				const CKnownFile *file = GetUploadClientFile(client);
				if (file != NULL && !file->IsPartFile())
					ShellOpen(_T("explorer"), _T("/select,\"") + file->GetFilePath() + _T('\"'));
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

void CUploadListCtrl::AddClient(const CUpDownClient *client)
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
	int iItem = InsertItem(LVIF_TEXT | LVIF_PARAM, iItemCount, LPSTR_TEXTCALLBACK, 0, 0, 0, (LPARAM)client);
	Update(iItem);
	theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount(CTransferWnd::wnd2Uploading, iItemCount + 1);
}

void CUploadListCtrl::RemoveClient(const CUpDownClient *client)
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
		theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount(CTransferWnd::wnd2Uploading);
}

void CUploadListCtrl::RefreshClient(const CUpDownClient *client)
{
	if (theApp.emuledlg->activewnd == theApp.emuledlg->transferwnd
		&& theApp.emuledlg->transferwnd->m_pwndTransfer->uploadlistctrl.IsWindowVisible()
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

void CUploadListCtrl::RefreshVisibleItems()
{
	if (theApp.IsClosing() || !IsWindowVisible())
		return;

	const int iItemCount = GetItemCount();
	const int iFirst = max(0, GetTopIndex());
	const int iLast = min(iItemCount, iFirst + max(1, GetCountPerPage()) + 1);
	bool bPruneStaleItems = false;
	for (int iItem = iFirst; iItem < iLast; ++iItem) {
		if (!IsLiveClient(reinterpret_cast<CUpDownClient*>(GetItemData(iItem)))) {
			bPruneStaleItems = true;
			continue;
		}
		Update(iItem);
	}

	if (bPruneStaleItems)
		PruneStaleClientItems();
	if (ShouldRunTransferRefreshSensitiveSort(TRANSFER_DISPLAY_LIST_UPLOADS, GetSortItem(), m_ullLastRefreshSortTick, ::GetTickCount64()))
		SortItems(SortProc, MAKELONG(GetSortItem(), !GetSortAscending()));
}

void CUploadListCtrl::ShowSelectedUserDetails()
{
	CPoint point;
	if (!::GetCursorPos(&point))
		return;
	ScreenToClient(&point);
	int it = HitTest(point);
	if (it < 0)
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
