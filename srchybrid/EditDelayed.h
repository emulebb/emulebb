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

//////////////////////////////////////////////////////////////////////////////
// CIconWnd

class CIconWnd : public CStatic
{
public:
	CIconWnd();
	virtual	~CIconWnd();

	void	SetImageList(CImageList *pImageList)	{ m_pImageList = pImageList; }
	void	ShowIcon(int nIconNumber);

protected:
	CImageList	*m_pImageList;
	int m_nCurrentIcon;

	DECLARE_MESSAGE_MAP()
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC *pDC);
};


//////////////////////////////////////////////////////////////////////////////
// CEditDelayed

class CEditDelayed : public CEdit
{
public:
	CEditDelayed();

	void	OnInit(CHeaderCtrl *pColumnHeader, CArray<int, int> *paIgnoredColumns = NULL);
	void	ShowColumnText(bool bShow);

	 // when not using pColumnHeader this text will be shown when the control is empty and has no focus
	void	SetAlternateText(const CString &rstrText)	{ m_strAlternateText = rstrText; }

	// lets the owner window append items to the filter dropdown (via UM_FILTER_MENU_EXTEND)
	// and receive any menu command the control does not own; off by default so other
	// filter boxes are unaffected.
	void	SetAllowOwnerMenuExtension(bool bAllow)		{ m_bAllowOwnerMenuExtension = bAllow; }

protected:
	bool		m_bShuttingDown;
	UINT_PTR	m_uTimerResult;
	ULONGLONG	m_dwLastModified;
	CString		m_strLastEvaluatedContent;
	CIconWnd	m_iwReset;
	CIconWnd	m_iwColumn;
	HCURSOR		m_hCursor;
	CPoint		m_pointMousePos;
	bool		m_bShowResetButton;
	bool		m_bShowsColumnText;
	int			m_nCurrentColumnIdx;
	CString		m_strAlternateText;
	CHeaderCtrl	*m_pctrlColumnHeader;
	CArray<int, int> m_aIgnoredColumns;
	bool		m_bAllowOwnerMenuExtension = false;

	void	DoDelayedEvalute(bool bForce = false);
	void	SetEditRect(bool bUpdateResetButtonPos, bool bUpdateColumnButton = false);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnSetFocus(CWnd *pOldWnd);
	afx_msg void OnKillFocus(CWnd *pNewWnd);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnEnChange();
	afx_msg void OnDestroy();
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg BOOL OnSetCursor(CWnd *pWnd, UINT nHitTest, UINT message);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg HBRUSH CtlColor(CDC *pDC, UINT nCtlColor);
	afx_msg	BOOL OnCommand(WPARAM wParam, LPARAM);
	afx_msg void OnSize(UINT nType, int cx, int cy);
};
