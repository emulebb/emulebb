//this file is part of eMule
// added by quekky
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
#include "ColorButton.h"

struct Category_Struct;
class CCustomAutoComplete;
class CTransferWnd;

class CCatDialog : public CDialog
{
	DECLARE_DYNAMIC(CCatDialog)

	enum
	{
		IDD = IDD_CAT
	};
	void ErrorBalloon(int iEdit, UINT uid);
public:
	explicit CCatDialog(int index);   // standard constructor
	virtual	~CCatDialog();
	void SetAddDialog(bool bAddDialog) { m_bAddDialog = bAddDialog; }

protected:
	CColorButton m_ctlColor;
	INT_PTR m_iCategory;
	Category_Struct *m_myCat;
	CComboBox m_prio;
	CCustomAutoComplete *m_pacRegExp;
	COLORREF m_newcolor;
	bool m_bAddDialog;

	void Localize();
	void UpdateData();

	virtual BOOL OnInitDialog();
	virtual void DoDataExchange(CDataExchange *pDX);    // DDX/DDV support

	DECLARE_MESSAGE_MAP()
	afx_msg LRESULT OnSelChange(WPARAM wParam, LPARAM);
	afx_msg void OnBnClickedBrowse();
	afx_msg void OnBnClickedOk();
	afx_msg void OnDDBnClicked();
};

class CCategoryManagerDialog : public CDialog
{
	DECLARE_DYNAMIC(CCategoryManagerDialog)

	enum
	{
		IDD = IDD_CATEGORY_MANAGER
	};

public:
	explicit CCategoryManagerDialog(CTransferWnd *pTransferWnd);

protected:
	CTransferWnd *m_pTransferWnd;
	CListCtrl m_categoryList;

	void Localize();
	void RefreshCategoryList(INT_PTR iSelectCategory = -1);
	INT_PTR GetSelectedCategory() const;
	void UpdateButtons();

	virtual BOOL OnInitDialog();
	virtual void DoDataExchange(CDataExchange *pDX);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnAdd();
	afx_msg void OnEdit();
	afx_msg void OnRemove();
	afx_msg void OnMoveUp();
	afx_msg void OnMoveDown();
	afx_msg void OnOpenIncoming();
	afx_msg void OnRefresh();
	afx_msg void OnItemChanged(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnDoubleClick(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnKeyDown(NMHDR *pNMHDR, LRESULT *pResult);
};
