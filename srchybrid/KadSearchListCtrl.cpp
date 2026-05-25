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
#include "KademliaWnd.h"
#include "KadSearchListCtrl.h"
#include "KadContactListCtrl.h"
#include "Ini2.h"
#include "OtherFunctions.h"
#include "emuledlg.h"
#include "DownloadQueue.h"
#include "PartFile.h"
#ifndef CRYPTOPP_ENABLE_NAMESPACE_WEAK
#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1
#endif
#include "kademlia/kademlia/search.h"
#include "kademlia/kademlia/SearchManager.h"
#include "kademlia/utils/LookupHistory.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	bool IsLiveKadSearchPointer(const Kademlia::CSearch *search)
	{
		return Kademlia::CSearchManager::ContainsSearchPointer(search);
	}
}

// CKadSearchListCtrl

IMPLEMENT_DYNAMIC(CKadSearchListCtrl, CMuleListCtrl)

#pragma warning(push)
#pragma warning(disable:4191)
BEGIN_MESSAGE_MAP(CKadSearchListCtrl, CMuleListCtrl)
	ON_NOTIFY_REFLECT(LVN_COLUMNCLICK, OnLvnColumnClick)
	ON_WM_SYSCOLORCHANGE()
END_MESSAGE_MAP()
#pragma warning(pop)

CKadSearchListCtrl::CKadSearchListCtrl()
{
	SetGeneralPurposeFind(true);
	SetSkinKey(_T("KadActionsLv"));
}

void CKadSearchListCtrl::Init()
{
	SetPrefsKey(_T("KadSearchListCtrl"));
	SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	InsertColumn(colNum,			_T(""),	LVCFMT_LEFT,	60);						//IDS_NUMBER
	InsertColumn(colKey,			_T(""),	LVCFMT_LEFT,	DFLT_HASH_COL_WIDTH);		//IDS_KEY
	InsertColumn(colType,			_T(""),	LVCFMT_LEFT,	100);						//IDS_TYPE
	InsertColumn(colName,			_T(""),	LVCFMT_LEFT,	DFLT_FILENAME_COL_WIDTH);	//IDS_SW_NAME
	InsertColumn(colStop,			_T(""),	LVCFMT_LEFT,	100);						//IDS_STATUS
	InsertColumn(colLoad,			_T(""),	LVCFMT_LEFT,	100);						//IDS_THELOAD
	InsertColumn(colPacketsSent,	_T(""),	LVCFMT_LEFT,	100);						//(IDS_PACKSENT
	InsertColumn(colResponses,		_T(""),	LVCFMT_LEFT,	100);						//IDS_RESPONSES

	SetAllIcons();
	Localize();

	LoadSettings();
	SetSortArrow();
	SortItems(SortProc, MAKELONG(GetSortItem(), !GetSortAscending()));
}

void CKadSearchListCtrl::UpdateKadSearchCount()
{
	CString id(GetResString(IDS_KADSEARCHLAB));
	id.AppendFormat(_T(" (%i)"), GetItemCount());
	theApp.emuledlg->kademliawnd->SetDlgItemText(IDC_KADSEARCHLAB, id);
}

void CKadSearchListCtrl::OnSysColorChange()
{
	CMuleListCtrl::OnSysColorChange();
	SetAllIcons();
}

void CKadSearchListCtrl::SetAllIcons()
{
	CImageList iml;
	iml.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	iml.Add(CTempIconLoader(_T("KadFileSearch")));
	iml.Add(CTempIconLoader(_T("KadWordSearch")));
	iml.Add(CTempIconLoader(_T("KadNodeSearch")));
	iml.Add(CTempIconLoader(_T("KadStoreFile")));
	iml.Add(CTempIconLoader(_T("KadStoreWord")));
	ASSERT((GetStyle() & LVS_SHAREIMAGELISTS) == 0);
	HIMAGELIST himl = ApplyImageList(iml.Detach());
	if (himl)
		::ImageList_Destroy(himl);
}

void CKadSearchListCtrl::Localize()
{
	static const UINT uids[8] =
	{
		IDS_NUMBER, IDS_KEY, IDS_TYPE, IDS_SW_NAME, IDS_STATUS,
		IDS_THELOAD, IDS_PACKSENT, IDS_RESPONSES
	};

	LocaliseHeaderCtrl(uids, _countof(uids));

	PruneStaleSearchItems();
	for (int i = GetItemCount(); --i >= 0;)
		if (const Kademlia::CSearch *search = GetLiveSearchByIndex(i))
			SearchRef(search);

	UpdateKadSearchCount();
}

const Kademlia::CSearch* CKadSearchListCtrl::GetLiveSearchByIndex(int iItem)
{
	if (iItem < 0 || iItem >= GetItemCount())
		return NULL;

	const Kademlia::CSearch *search = reinterpret_cast<Kademlia::CSearch*>(GetItemData(iItem));
	return IsLiveSearch(search) ? search : NULL;
}

bool CKadSearchListCtrl::IsLiveSearch(const Kademlia::CSearch *search) const
{
	return IsLiveKadSearchPointer(search);
}

bool CKadSearchListCtrl::PruneStaleSearchItems()
{
	bool bRemoved = false;
	for (int i = GetItemCount(); --i >= 0;) {
		if (!IsLiveSearch(reinterpret_cast<Kademlia::CSearch*>(GetItemData(i)))) {
			DeleteItem(i);
			bRemoved = true;
		}
	}
	if (bRemoved)
		UpdateKadSearchCount();
	return bRemoved;
}

void CKadSearchListCtrl::UpdateSearch(int iItem, const Kademlia::CSearch *search)
{
	CString id;
	id.Format(_T("%u"), search->GetSearchID());
	SetItemText(iItem, colNum, id);

	int nImage;
	uint32 uType = search->GetSearchType();
	switch (uType) {
	case Kademlia::CSearch::FILE:
		nImage = 0;
		break;
	case Kademlia::CSearch::KEYWORD:
		nImage = 1;
		break;
	case Kademlia::CSearch::NODE:
	case Kademlia::CSearch::NODECOMPLETE:
	case Kademlia::CSearch::NODESPECIAL:
	case Kademlia::CSearch::NODEFWCHECKUDP:
		nImage = 2;
		break;
	case Kademlia::CSearch::STOREFILE:
		nImage = 3;
		break;
	case Kademlia::CSearch::STOREKEYWORD:
		nImage = 4;
		break;
	default:
		nImage = -1; //none
	}
	//JOHNTODO: -
	//I also need to understand skinning so the icons are done correctly.
	if (nImage >= 0)
		SetItem(iItem, 0, LVIF_IMAGE, 0, nImage, 0, 0, 0, 0);

#ifndef _DEBUG
	SetItemText(iItem, colType, Kademlia::CSearch::GetTypeName(uType));
#else
	id.Format(_T("%s (%u)"), (LPCTSTR)Kademlia::CSearch::GetTypeName(uType), uType);
	SetItemText(iItem, colType, id);
#endif
	SetItemText(iItem, colName, (CString)search->GetGUIName());

	if (search->GetTarget() != NULL) {
		search->GetTarget().ToHexString(id);
		SetItemText(iItem, colKey, id);
	}

	SetItemText(iItem, colStop, GetResString(search->Stoping() ? IDS_KADSTATUS_STOPPING : IDS_KADSTATUS_ACTIVE));

	id.Format(_T("%u (%u|%u)"), search->GetNodeLoad(), search->GetNodeLoadResponse(), search->GetNodeLoadTotal());
	SetItemText(iItem, colLoad, id);

	id.Format(_T("%u"), search->GetAnswers());
	SetItemText(iItem, colResponses, id);

	id.Format(_T("%u|%u"), search->GetKadPacketSent(), search->GetRequestAnswer());
	SetItemText(iItem, colPacketsSent, id);
}

void CKadSearchListCtrl::SearchAdd(const Kademlia::CSearch *search)
{
	try {
		ASSERT(search != NULL);
		if (search == NULL)
			return;
		LVFINDINFO find;
		find.flags = LVFI_PARAM;
		find.lParam = (LPARAM)search;
		if (FindItem(&find) >= 0)
			return;
		int iItem = InsertItem(LVIF_TEXT | LVIF_PARAM, GetItemCount(), _T(""), 0, 0, 0, (LPARAM)search);
		if (iItem >= 0) {
			UpdateSearch(iItem, search);
			UpdateKadSearchCount();
		}
	} catch (...) {
		ASSERT(0);
	}
}

void CKadSearchListCtrl::SearchRem(const Kademlia::CSearch *search)
{
	try {
		ASSERT(search != NULL);
		if (search == NULL)
			return;
		bool bRemoved = false;
		LVFINDINFO find;
		find.flags = LVFI_PARAM;
		find.lParam = (LPARAM)search;
		int iItem;
		while ((iItem = FindItem(&find)) >= 0) {
			DeleteItem(iItem);
			bRemoved = true;
		}
		if (bRemoved)
			UpdateKadSearchCount();
	} catch (...) {
		ASSERT(0);
	}
}

void CKadSearchListCtrl::SearchRef(const Kademlia::CSearch *search)
{
	try {
		ASSERT(search != NULL);
		if (search == NULL)
			return;
		if (!IsLiveSearch(search)) {
			SearchRem(search);
			return;
		}
		LVFINDINFO find;
		find.flags = LVFI_PARAM;
		find.lParam = (LPARAM)search;
		int iItem = FindItem(&find);
		if (iItem >= 0)
			UpdateSearch(iItem, search);
	} catch (...) {
		ASSERT(0);
	}
}

BOOL CKadSearchListCtrl::OnCommand(WPARAM, LPARAM)
{
	// ???
	return TRUE;
}

void CKadSearchListCtrl::OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult)
{
	const LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	// Determine ascending based on whether already sorted on this column
	bool bSortAscending = (GetSortItem() != pNMLV->iSubItem) || !GetSortAscending();

	// Item is the clicked column
	int iSortItem = pNMLV->iSubItem;

	// Sort table
	PruneStaleSearchItems();
	UpdateSortHistory(MAKELONG(iSortItem, !bSortAscending));
	SetSortArrow(iSortItem, bSortAscending);
	SortItems(SortProc, MAKELONG(iSortItem, !bSortAscending));
	*pResult = 0;
}

int CALLBACK CKadSearchListCtrl::SortProc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	const Kademlia::CSearch *item1 = reinterpret_cast<Kademlia::CSearch*>(lParam1);
	const Kademlia::CSearch *item2 = reinterpret_cast<Kademlia::CSearch*>(lParam2);
	if (item1 == NULL || item2 == NULL)
		return 0;
	const bool bItem1Live = IsLiveKadSearchPointer(item1);
	const bool bItem2Live = IsLiveKadSearchPointer(item2);
	if (!bItem1Live || !bItem2Live)
		return bItem1Live ? -1 : (bItem2Live ? 1 : 0);

	int iResult;
	switch (LOWORD(lParamSort)) {
	case colNum:
		iResult = CompareUnsigned(item1->GetSearchID(), item2->GetSearchID());
		break;
	case colKey:
		if (item1->GetTarget() == NULL && item2->GetTarget() == NULL)
			iResult = 0;
		else if (item1->GetTarget() != NULL && item2->GetTarget() == NULL)
			iResult = -1;
		else if (item1->GetTarget() == NULL && item2->GetTarget() != NULL)
			iResult = 1;
		else
			iResult = item1->GetTarget().CompareTo(item2->GetTarget());
		break;
	case colType:
		iResult = item1->GetSearchType() - item2->GetSearchType();
		break;
	case colName:
		iResult = CompareLocaleStringNoCaseW(item1->GetGUIName(), item2->GetGUIName());
		break;
	case colStop:
		iResult = (int)item1->Stoping() - (int)item2->Stoping();
		break;
	case colLoad:
		iResult = CompareUnsigned(item1->GetNodeLoad(), item2->GetNodeLoad());
		break;
	case colPacketsSent:
		iResult = CompareUnsigned(item1->GetKadPacketSent(), item2->GetKadPacketSent());
		break;
	case colResponses:
		iResult = CompareUnsigned(item1->GetAnswers(), item2->GetAnswers());
		break;
	default:
		return 0;
	}
	return HIWORD(lParamSort) ? -iResult : iResult;
}

Kademlia::CLookupHistory* CKadSearchListCtrl::FetchAndSelectActiveSearch(bool bMark)
{
	PruneStaleSearchItems();
	int iIntrestingItem = -1;
	int iItem = -1;
	Kademlia::CLookupHistory *pInterestingLookupHistory = NULL;
	Kademlia::CLookupHistory *pLookupHistory = NULL;

	for (int i = GetItemCount(); --i >= 0;) {
		const Kademlia::CSearch *pSearch = GetLiveSearchByIndex(i);
		if (pSearch != NULL && !pSearch->GetLookupHistory()->IsSearchStopped() && !pSearch->GetLookupHistory()->IsSearchDeleted()) {
			// prefer interesting search rather than node searches
			switch (pSearch->GetSearchType()) {
			case Kademlia::CSearch::FILE:
			case Kademlia::CSearch::KEYWORD:
			case Kademlia::CSearch::STORENOTES:
			case Kademlia::CSearch::NOTES:
			case Kademlia::CSearch::STOREFILE:
			case Kademlia::CSearch::STOREKEYWORD:
				iIntrestingItem = i;
				pInterestingLookupHistory = pSearch->GetLookupHistory();
				break;
			case Kademlia::CSearch::NODE:
			case Kademlia::CSearch::NODECOMPLETE:
			case Kademlia::CSearch::NODESPECIAL:
			case Kademlia::CSearch::NODEFWCHECKUDP:
			case Kademlia::CSearch::FINDBUDDY:
			default:
				if (iItem == -1) {
					iItem = i;
					pLookupHistory = pSearch->GetLookupHistory();
				}
			}
			if (iIntrestingItem >= 0)
				break;
		}
	}
	if (iIntrestingItem >= 0) {
		if (bMark)
			SetItemState(iIntrestingItem, LVIS_SELECTED, LVIS_SELECTED);
		return pInterestingLookupHistory;
	}
	if (iItem >= 0) {
		if (bMark)
			SetItemState(iItem, LVIS_SELECTED, LVIS_SELECTED);
		return pLookupHistory;
	}
	return NULL;
}
