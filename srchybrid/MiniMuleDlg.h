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
#include "resource.h"

class CemuleDlg;

/**
 * @brief Native MiniMule status popup shown from the tray while the main window is hidden.
 */
class CMiniMuleDlg : public CDialog
{
	enum
	{
		IDD = IDD_MINIMULE
	};

public:
	explicit CMiniMuleDlg(CemuleDlg *pOwner);
	virtual ~CMiniMuleDlg();

	/**
	 * @brief Refreshes visible transfer and queue statistics.
	 */
	void UpdateContent(UINT uUpDatarate = UINT_MAX, UINT uDownDatarate = UINT_MAX);

	/**
	 * @brief Applies localized labels and button captions.
	 */
	void Localize();

protected:
	virtual BOOL OnInitDialog();
	virtual void PostNcDestroy();
	virtual void OnOK();
	virtual void OnCancel();
	virtual BOOL PreTranslateMessage(MSG *pMsg);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnClose();
	afx_msg HBRUSH OnCtlColor(CDC *pDC, CWnd *pWnd, UINT nCtlColor);
	afx_msg void OnDestroy();
	afx_msg void OnPaint();
	afx_msg void OnSysColorChange();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnRestoreMainWindow();
	afx_msg void OnOpenIncomingFolder();
	afx_msg void OnOptions();

private:
	/**
	 * @brief Positions the popup near the taskbar without using cross-thread shell appbar calls.
	 */
	void AutoSizeAndPosition();
	void ApplyVisualStyle();
	void CreateDerivedFonts();
	void RefreshColorResources();
	bool IsHeaderTextControl(UINT uCtrlID) const;
	bool IsLabelControl(UINT uCtrlID) const;
	bool IsMetricValueControl(UINT uCtrlID) const;
	bool IsValueControl(UINT uCtrlID) const;

	CemuleDlg *m_pOwner;
	CFont m_fontTitle;
	CFont m_fontMetricValue;
	CFont m_fontValue;
	CBrush m_brBackground;
	CBrush m_brHeader;
	HICON m_hAppIcon;
};
