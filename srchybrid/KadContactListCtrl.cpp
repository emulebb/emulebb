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
#include "KadContactListCtrl.h"
#include "emuledlg.h"
#include "GeoLocation.h"
#include "MemDC.h"
#include "Opcodes.h"
#include "OtherFunctions.h"
#include "Kademlia/Kademlia/Kademlia.h"
#include "Kademlia/Routing/RoutingZone.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	UINT GetKadDistanceBucket(const Kademlia::CUInt128 &uDistance)
	{
		for (UINT uBit = 0; uBit < 128; ++uBit) {
			if (uDistance.GetBitNumber(uBit) != 0)
				return uBit + 1;
		}
		return 0;
	}

	LPCTSTR GetKadContactTypeLabel(byte byType)
	{
		switch (byType) {
		case 0:
			return _T("stable");
		case 1:
			return _T("mature");
		case 2:
			return _T("recent");
		case 3:
			return _T("new");
		case 4:
			return _T("expired");
		default:
			return _T("unknown");
		}
	}

	LPCTSTR GetKadEmuleVersionLabel(uint8 uKadVersion)
	{
		switch (uKadVersion) {
		case KADEMLIA_VERSION1_46c:
			return _T("eMule 0.46c");
		case KADEMLIA_VERSION2_47a:
			return _T("eMule 0.47a");
		case KADEMLIA_VERSION3_47b:
			return _T("eMule 0.47b");
		case KADEMLIA_VERSION4_47c:
			return _T("eMule 0.47c");
		case KADEMLIA_VERSION5_48a:
			return _T("eMule 0.48a");
		case KADEMLIA_VERSION6_49aBETA:
			return _T("eMule 0.49a beta");
		case KADEMLIA_VERSION7_49a:
			return _T("eMule 0.49a");
		case KADEMLIA_VERSION8_49b:
			return _T("eMule 0.49b");
		case KADEMLIA_VERSION9_50a:
			return _T("eMule 0.50a");
		case KADEMLIA_VERSION:
			return _T("eMule 0.72a");
		default:
			return NULL;
		}
	}

	bool IsLiveKadContactPointer(const Kademlia::CContact *contact)
	{
		if (contact == NULL)
			return false;
		if (Kademlia::CKademlia::GetRoutingZone() != NULL && Kademlia::CKademlia::GetRoutingZone()->ContainsContactPointer(contact))
			return true;
		return Kademlia::CKademlia::s_liBootstrapList.Find(const_cast<Kademlia::CContact*>(contact)) != NULL;
	}
}

// CONContactListCtrl

IMPLEMENT_DYNAMIC(CKadContactListCtrl, CMuleListCtrl)

BEGIN_MESSAGE_MAP(CKadContactListCtrl, CMuleListCtrl)
	ON_NOTIFY_REFLECT(LVN_COLUMNCLICK, OnLvnColumnClick)
	ON_WM_DESTROY()
	ON_WM_SYSCOLORCHANGE()
END_MESSAGE_MAP()

CKadContactListCtrl::CKadContactListCtrl()
{
	SetGeneralPurposeFind(true);
	SetSkinKey(_T("KadContactsLv"));
}

void CKadContactListCtrl::Init()
{
	SetPrefsKey(_T("ONContactListCtrl"));
	SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	InsertColumn(colID,		  _T(""),	LVCFMT_LEFT, 16 + DFLT_HASH_COL_WIDTH);	//IDS_ID
	InsertColumn(colType,	  _T(""),	LVCFMT_LEFT, 100);						//IDS_TYPE
	InsertColumn(colVersion,  _T(""),	LVCFMT_LEFT, 125);						//IDS_VERSION
	InsertColumn(colDistance, _T(""),	LVCFMT_LEFT, 360);						//IDS_KADDISTANCE
	InsertColumn(colIP,		  _T(""),	LVCFMT_LEFT, 105);						//IDS_IP
	InsertColumn(colLocation, _T(""),	LVCFMT_LEFT, 180);						//IDS_GEOLOCATION

	SetAllIcons();
	Localize();

	LoadSettings();
	int iSortItem = GetSortItem();
	bool bSortAscending = GetSortAscending();

	SetSortArrow(iSortItem, bSortAscending);
	SortItems(SortProc, MAKELONG(iSortItem, !bSortAscending));
}

void CKadContactListCtrl::OnSysColorChange()
{
	CMuleListCtrl::OnSysColorChange();
	SetAllIcons();
}

void CKadContactListCtrl::SetAllIcons()
{
	CImageList iml;
	iml.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	iml.Add(CTempIconLoader(_T("Contact0")));
	iml.Add(CTempIconLoader(_T("Contact1")));
	iml.Add(CTempIconLoader(_T("Contact2")));
	iml.Add(CTempIconLoader(_T("Contact3")));
	iml.Add(CTempIconLoader(_T("Contact4")));
	iml.Add(CTempIconLoader(_T("SrcUnknown"))); // replace
	ASSERT((GetStyle() & LVS_SHAREIMAGELISTS) == 0);
	HIMAGELIST himl = ApplyImageList(iml.Detach());
	if (himl)
		::ImageList_Destroy(himl);
}

void CKadContactListCtrl::Localize()
{
	static const UINT uids[6] =
	{
		IDS_ID, IDS_TYPE, IDS_VERSION, IDS_KADDISTANCE, IDS_IP, IDS_GEOLOCATION
	};

	LocaliseHeaderCtrl(uids, _countof(uids));
}

CString CKadContactListCtrl::GetContactLocationText(const Kademlia::CContact *contact) const
{
	if (theApp.geolocation == NULL || contact == NULL)
		return CString();
	return theApp.geolocation->GetDisplayText(contact->GetIPAddress());
}

CString CKadContactListCtrl::GetContactTypeText(const Kademlia::CContact *contact) const
{
	if (contact == NULL)
		return CString();

	CString strType;
	const byte byType = contact->GetType();
	strType.Format(_T("%u - %s"), static_cast<unsigned>(byType), GetKadContactTypeLabel(byType));
	return strType;
}

CString CKadContactListCtrl::GetContactVersionText(const Kademlia::CContact *contact) const
{
	if (contact == NULL)
		return CString();

	CString strVersion;
	const uint8 uKadVersion = contact->GetVersion();
	const LPCTSTR pszEmuleVersion = GetKadEmuleVersionLabel(uKadVersion);
	if (pszEmuleVersion != NULL)
		strVersion.Format(_T("%u - %s"), static_cast<unsigned>(uKadVersion), pszEmuleVersion);
	else
		strVersion.Format(_T("%u"), static_cast<unsigned>(uKadVersion));
	return strVersion;
}

CString CKadContactListCtrl::GetContactDistanceText(const Kademlia::CContact *contact) const
{
	if (contact == NULL)
		return CString();

	Kademlia::CUInt128 uDistance;
	contact->GetDistance(uDistance);

	CString strDistance;
	strDistance.Format(_T("Bucket %u - %s"), GetKadDistanceBucket(uDistance), (LPCTSTR)uDistance.ToHexString());
	return strDistance;
}

const Kademlia::CContact* CKadContactListCtrl::GetLiveContactByIndex(int iItem)
{
	if (iItem < 0 || iItem >= GetItemCount())
		return NULL;

	const Kademlia::CContact *contact = reinterpret_cast<Kademlia::CContact*>(GetItemData(iItem));
	return IsLiveContact(contact) ? contact : NULL;
}

bool CKadContactListCtrl::IsLiveContact(const Kademlia::CContact *contact) const
{
	return IsLiveKadContactPointer(contact);
}

bool CKadContactListCtrl::PruneStaleContactItems()
{
	bool bRemoved = false;
	for (int iItem = GetItemCount(); --iItem >= 0;) {
		if (!IsLiveContact(reinterpret_cast<Kademlia::CContact*>(GetItemData(iItem)))) {
			DeleteItem(iItem);
			bRemoved = true;
		}
	}

	if (bRemoved)
		UpdateKadContactCount();

	return bRemoved;
}

void CKadContactListCtrl::UpdateContact(int iItem, const Kademlia::CContact *contact)
{
	CString id;
	contact->GetClientID(id);
	SetItemText(iItem, colID, id);

	SetItemText(iItem, colType, GetContactTypeText(contact));

	SetItemText(iItem, colVersion, GetContactVersionText(contact));

	SetItemText(iItem, colDistance, GetContactDistanceText(contact));

	contact->GetIPAddress(id);
	SetItemText(iItem, colIP, id);

	SetItemText(iItem, colLocation, GetContactLocationText(contact));

	UINT nImageShown;
	if (contact->IsBootstrapContact())
		nImageShown = 5; //contact->IsBootstrapFailed() ? 4 : 5;
	else {
		nImageShown = contact->GetType() > 4 ? 4 : contact->GetType();
		if (nImageShown < 3 && !contact->IsIpVerified())
			nImageShown = 5; // if we have an active contact, which is however not IP verified (and therefore not used), show this icon instead
	}
	SetItem(iItem, 0, LVIF_IMAGE, 0, nImageShown, 0, 0, 0, 0);
}

void CKadContactListCtrl::UpdateKadContactCount()
{
	theApp.emuledlg->kademliawnd->UpdateContactCount();
}

bool CKadContactListCtrl::ContactAdd(const Kademlia::CContact *contact)
{
	try {
		ASSERT(contact != NULL);
		if (contact == NULL)
			return false;
		if (!IsLiveContact(contact))
			return false;
		PruneStaleContactItems();

		LVFINDINFO find;
		find.flags = LVFI_PARAM;
		find.lParam = (LPARAM)contact;
		if (FindItem(&find) >= 0)
			return true;

		int iItem = InsertItem(LVIF_TEXT | LVIF_PARAM, GetItemCount(), _T(""), 0, 0, 0, (LPARAM)contact);
		if (iItem >= 0) {
			UpdateContact(iItem, contact);
			UpdateKadContactCount();
			return true;
		}
	} catch (...) {
		ASSERT(0);
	}
	return false;
}

void CKadContactListCtrl::ContactRem(const Kademlia::CContact *contact)
{
	try {
		ASSERT(contact != NULL);
		if (contact == NULL)
			return;
		LVFINDINFO find;
		find.flags = LVFI_PARAM;
		find.lParam = (LPARAM)contact;
		bool bRemoved = false;
		for (;;) {
			int iItem = FindItem(&find);
			if (iItem < 0)
				break;
			DeleteItem(iItem);
			bRemoved = true;
		}
		if (bRemoved)
			UpdateKadContactCount();
	} catch (...) {
		ASSERT(0);
	}
}

void CKadContactListCtrl::ContactRef(const Kademlia::CContact *contact)
{
	try {
		ASSERT(contact != NULL);
		if (contact == NULL)
			return;
		if (!IsLiveContact(contact)) {
			ContactRem(contact);
			return;
		}

		LVFINDINFO find;
		find.flags = LVFI_PARAM;
		find.lParam = (LPARAM)contact;
		int iItem = FindItem(&find);
		if (iItem >= 0)
			UpdateContact(iItem, contact);
	} catch (...) {
		ASSERT(0);
	}
}

BOOL CKadContactListCtrl::OnCommand(WPARAM, LPARAM)
{
	// ???
	return TRUE;
}

void CKadContactListCtrl::OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult)
{
	const LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	// Determine ascending based on whether already sorted on this column
	bool bSortAscending = (GetSortItem() != pNMLV->iSubItem || !GetSortAscending());

	// Item is column clicked
	int iSortItem = pNMLV->iSubItem;

	// Sort table
	UpdateSortHistory(MAKELONG(iSortItem, !bSortAscending));
	SetSortArrow(iSortItem, bSortAscending);
	PruneStaleContactItems();
	SortItems(SortProc, MAKELONG(iSortItem, !bSortAscending));
	*pResult = 0;
}

void CKadContactListCtrl::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	if (!lpDrawItemStruct->itemData || theApp.IsClosing())
		return;

	const Kademlia::CContact *contact = reinterpret_cast<Kademlia::CContact*>(lpDrawItemStruct->itemData);
	if (!IsLiveContact(contact))
		return;

	CRect rcItem(lpDrawItemStruct->rcItem);
	CMemoryDC dc(CDC::FromHandle(lpDrawItemStruct->hDC), rcItem);
	BOOL bCtrlFocused;
	InitItemMemDC(dc, lpDrawItemStruct, bCtrlFocused);
	RECT rcClient;
	GetClientRect(&rcClient);

	LVITEM lvi = {};
	lvi.mask = LVIF_IMAGE;
	lvi.iItem = lpDrawItemStruct->itemID;
	lvi.iSubItem = 0;
	GetItem(&lvi);

	const CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();
	CImageList *pImageList = GetImageList(LVSIL_SMALL);
	const int iCount = pHeaderCtrl->GetItemCount();
	const LONG iIconY = max((rcItem.Height() - 15) / 2, 0);
	const int iItem = lpDrawItemStruct->itemID;
	LONG itemLeft = rcItem.left;
	for (int iCurrent = 0; iCurrent < iCount; ++iCurrent) {
		const int iColumn = pHeaderCtrl->OrderToIndex(iCurrent);
		if (IsColumnHidden(iColumn))
			continue;

		UINT uDrawTextAlignment;
		const int iColumnWidth = GetColumnWidth(iColumn, uDrawTextAlignment);
		rcItem.left = itemLeft;
		rcItem.right = itemLeft + iColumnWidth - sm_iSubItemInset;
		if (rcItem.left < rcItem.right && HaveIntersection(rcClient, rcItem)) {
			const CString &sItem = GetItemText(iItem, iColumn);
			switch (iColumn) {
			case colID:
				{
					rcItem.left = itemLeft + sm_iIconOffset;
					if (pImageList != NULL) {
						const POINT point{rcItem.left, rcItem.top + iIconY};
						pImageList->Draw(&dc, lvi.iImage, point, ILD_TRANSPARENT);
					}
					rcItem.left += 16 + sm_iLabelOffset - sm_iSubItemInset;
					rcItem.left += sm_iSubItemInset;
					dc.DrawText(sItem, &rcItem, MLC_DT_TEXT | uDrawTextAlignment);
					break;
				}
			case colLocation:
				if (theApp.geolocation != NULL) {
					const POINT point{itemLeft + sm_iIconOffset, rcItem.top + iIconY};
					if (theApp.geolocation->DrawFlag(dc, contact->GetIPAddress(), point))
						rcItem.left = itemLeft + sm_iIconOffset + 18 + sm_iLabelOffset - sm_iSubItemInset;
				}
				rcItem.left += sm_iSubItemInset;
				dc.DrawText(sItem, &rcItem, MLC_DT_TEXT | uDrawTextAlignment);
				break;
			default:
				rcItem.left += sm_iSubItemInset;
				dc.DrawText(sItem, &rcItem, MLC_DT_TEXT | uDrawTextAlignment);
				break;
			}
		}
		itemLeft += iColumnWidth;
	}

	DrawFocusRect(&dc, &lpDrawItemStruct->rcItem, lpDrawItemStruct->itemState & ODS_FOCUS, bCtrlFocused, lpDrawItemStruct->itemState & ODS_SELECTED);
}

int CALLBACK CKadContactListCtrl::SortProc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	const Kademlia::CContact *item1 = reinterpret_cast<Kademlia::CContact*>(lParam1);
	const Kademlia::CContact *item2 = reinterpret_cast<Kademlia::CContact*>(lParam2);
	if (item1 == NULL || item2 == NULL)
		return 0;
	const bool bLiveItem1 = IsLiveKadContactPointer(item1);
	const bool bLiveItem2 = IsLiveKadContactPointer(item2);
	if (!bLiveItem1 || !bLiveItem2)
		return bLiveItem1 ? -1 : (bLiveItem2 ? 1 : 0);

	int iResult;
	switch (LOWORD(lParamSort)) {
	case colID:
		{
			Kademlia::CUInt128 i1;
			Kademlia::CUInt128 i2;
			item1->GetClientID(i1);
			item2->GetClientID(i2);
			iResult = i1.CompareTo(i2);
		}
		break;
	case colType:
		iResult = static_cast<int>(item1->GetType()) - static_cast<int>(item2->GetType());
		if (iResult == 0)
			iResult = static_cast<int>(item1->GetVersion()) - static_cast<int>(item2->GetVersion());
		break;
	case colVersion:
		iResult = static_cast<int>(item1->GetVersion()) - static_cast<int>(item2->GetVersion());
		break;
	case colDistance:
		{
			Kademlia::CUInt128 distance1, distance2;
			item1->GetDistance(distance1);
			item2->GetDistance(distance2);
			iResult = distance1.CompareTo(distance2);
		}
		break;
	case colIP:
		iResult = CompareUnsigned(item1->GetIPAddress(), item2->GetIPAddress());
		break;
	case colLocation:
		if (theApp.geolocation != NULL)
			iResult = CompareLocaleStringNoCase(theApp.geolocation->GetDisplayText(item1->GetIPAddress()), theApp.geolocation->GetDisplayText(item2->GetIPAddress()));
		else
			iResult = 0;
		break;
	default:
		return 0;
	}
	return HIWORD(lParamSort) ? -iResult : iResult;
}
