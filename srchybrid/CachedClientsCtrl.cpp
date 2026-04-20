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
#include "CachedClientsCtrl.h"
#include "MenuCmds.h"
#include "OtherFunctions.h"
#include "RemoteBrowseManager.h"
#include "TitledMenu.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

IMPLEMENT_DYNAMIC(CCachedClientsCtrl, CMuleListCtrl)

BEGIN_MESSAGE_MAP(CCachedClientsCtrl, CMuleListCtrl)
	ON_WM_CONTEXTMENU()
	ON_NOTIFY_REFLECT(NM_DBLCLK, OnNmDblClk)
END_MESSAGE_MAP()

CCachedClientsCtrl::CCachedClientsCtrl()
	: CListCtrlItemWalk(this)
{
	SetGeneralPurposeFind(true);
	SetSkinKey(_T("CachedClientsLv"));
}

void CCachedClientsCtrl::Init()
{
	SetPrefsKey(_T("CachedClientsCtrl"));
	SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	PopulateColumns();
	Localize();
	LoadSettings();
}

void CCachedClientsCtrl::PopulateColumns()
{
	InsertColumn(0, _T(""), LVCFMT_LEFT, 160);
	InsertColumn(1, _T(""), LVCFMT_LEFT, 150);
	InsertColumn(2, _T(""), LVCFMT_LEFT, 140);
	InsertColumn(3, _T(""), LVCFMT_LEFT, 120);
	InsertColumn(4, _T(""), LVCFMT_RIGHT, 90);
	InsertColumn(5, _T(""), LVCFMT_RIGHT, 120);
	InsertColumn(6, _T(""), LVCFMT_LEFT, 130);
	InsertColumn(7, _T(""), LVCFMT_LEFT, 130);
}

void CCachedClientsCtrl::Localize()
{
	static const LPCTSTR apszHeaders[] = {
		_T("Client"),
		_T("Identity"),
		_T("Status"),
		_T("Outcome"),
		_T("Files"),
		_T("Size"),
		_T("Last Attempt"),
		_T("Last Success")
	};

	CHeaderCtrl *pHeader = GetHeaderCtrl();
	if (pHeader == NULL)
		return;

	for (int i = 0; i < _countof(apszHeaders); ++i) {
		HDITEM hdi = {};
		hdi.mask = HDI_TEXT;
		hdi.pszText = const_cast<LPTSTR>(apszHeaders[i]);
		pHeader->SetItem(i, &hdi);
	}
}

void CCachedClientsCtrl::Reload()
{
	DeleteAllItems();
	m_rows.clear();
	if (theApp.remoteBrowseManager == NULL)
		return;

	theApp.remoteBrowseManager->EnsureCacheIndexLoaded();
	theApp.remoteBrowseManager->GetSnapshots(m_rows);
	for (size_t i = 0; i < m_rows.size(); ++i)
		InsertSnapshotRow(m_rows[i]);
}

void CCachedClientsCtrl::InsertSnapshotRow(const CRemoteBrowseManager::Snapshot &snapshot)
{
	const int iItem = InsertItem(LVIF_TEXT | LVIF_PARAM, GetItemCount(), snapshot.strDisplayName, 0, 0, 0, static_cast<LPARAM>(m_rows.size() - 1));
	SetItemText(iItem, 1, snapshot.strIdentity);
	SetItemText(iItem, 2, snapshot.strStatus);
	SetItemText(iItem, 3, snapshot.strOutcome);

	CString strBuffer;
	strBuffer.Format(_T("%u"), snapshot.uCachedFileCount);
	SetItemText(iItem, 4, strBuffer);
	SetItemText(iItem, 5, CastItoXBytes(snapshot.ullCachedTotalSize));
	SetItemText(iItem, 6, CRemoteBrowseManager::FormatTimeForList(snapshot.tLastAttempt));
	SetItemText(iItem, 7, CRemoteBrowseManager::FormatTimeForList(snapshot.tLastSuccess));
}

const CRemoteBrowseManager::Snapshot* CCachedClientsCtrl::GetSelectedSnapshot()
{
	const int iSelection = GetSelectionMark();
	if (iSelection < 0)
		return NULL;

	const int iIndex = static_cast<int>(GetItemData(iSelection));
	if (iIndex < 0 || static_cast<size_t>(iIndex) >= m_rows.size())
		return NULL;
	return &m_rows[iIndex];
}

void CCachedClientsCtrl::OnNmDblClk(LPNMHDR, LRESULT *pResult)
{
	const CRemoteBrowseManager::Snapshot *pSnapshot = GetSelectedSnapshot();
	if (pSnapshot != NULL && theApp.remoteBrowseManager != NULL)
		theApp.remoteBrowseManager->OpenCachedOnlyByKey(pSnapshot->strKey);
	*pResult = 0;
}

void CCachedClientsCtrl::OnContextMenu(CWnd*, CPoint point)
{
	const CRemoteBrowseManager::Snapshot *pSnapshot = GetSelectedSnapshot();
	if (pSnapshot == NULL || theApp.remoteBrowseManager == NULL)
		return;

	CTitledMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, MP_CACHEDCLIENTS_OPEN, _T("Open Cached Only"));
	menu.AppendMenu(MF_STRING | (pSnapshot->bCanRefresh ? MF_ENABLED : MF_GRAYED), MP_CACHEDCLIENTS_REFRESH, _T("Refresh Live"));
	menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
}

BOOL CCachedClientsCtrl::OnCommand(WPARAM wParam, LPARAM lParam)
{
	const CRemoteBrowseManager::Snapshot *pSnapshot = GetSelectedSnapshot();
	if (pSnapshot == NULL || theApp.remoteBrowseManager == NULL)
		return CMuleListCtrl::OnCommand(wParam, lParam);

	switch (wParam) {
	case MP_CACHEDCLIENTS_OPEN:
		theApp.remoteBrowseManager->OpenCachedOnlyByKey(pSnapshot->strKey);
		return TRUE;
	case MP_CACHEDCLIENTS_REFRESH:
		theApp.remoteBrowseManager->RefreshKey(pSnapshot->strKey);
		return TRUE;
	}
	return CMuleListCtrl::OnCommand(wParam, lParam);
}
