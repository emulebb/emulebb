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
#pragma once
#include "MuleListCtrl.h"
#include "ListCtrlItemWalk.h"
#include "ToolTipCtrlX.h"

class CUpDownClient;

class CUploadListCtrl : public CMuleListCtrl, public CListCtrlItemWalk
{
	DECLARE_DYNAMIC(CUploadListCtrl)

	CImageList	*m_pImageList;
public:
	CUploadListCtrl();
	CUploadListCtrl(const CUploadListCtrl&) = delete;
	CUploadListCtrl& operator=(const CUploadListCtrl&) = delete;

	void	Init();
	void	AddClient(const CUpDownClient *client);
	void	RemoveClient(const CUpDownClient *client);
	void	RefreshClient(const CUpDownClient *client);
	void	RefreshVisibleItems();
	void	Hide()								{ ShowWindow(SW_HIDE); }
	void	Show()								{ ShowWindow(SW_SHOW); }
	void	Localize();
	void	ShowSelectedUserDetails();
	virtual CObject* GetNextSelectableItem();
	virtual CObject* GetPrevSelectableItem();

protected:
	CToolTipCtrlX m_tooltip;
	ULONGLONG m_ullLastRefreshSortTick;

	void SetAllIcons();
	const CUpDownClient* GetLiveClientByIndex(int iItem);
	bool IsLiveClient(const CUpDownClient *client) const;
	bool PruneStaleClientItems();
	CObject* WalkToLiveClientItem(int iDirection);
	CString GetItemDisplayText(const CUpDownClient *client, int iSubItem) const;
	static int CALLBACK SortProc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort);

	virtual BOOL OnCommand(WPARAM wParam, LPARAM);
	virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnContextMenu(CWnd*, CPoint point);
	afx_msg void OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnGetDispInfo(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnGetInfoTip(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnNmDblClk(LPNMHDR, LRESULT *pResult);
	afx_msg void OnSysColorChange();
};
