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
#include "MiniMuleDlg.h"
#include "emule.h"
#include "emuleDlg.h"
#include "DownloadListCtrl.h"
#include "OtherFunctions.h"
#include "DownloadQueue.h"
#include "MenuCmds.h"
#include "Preferences.h"
#include "TransferDlg.h"
#include "UploadQueue.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	const UINT_PTR IDT_MINIMULE_REFRESH = 1;

	void SetControlFont(CWnd *pParent, UINT uCtrlID, CFont *pFont)
	{
		if (pParent != NULL && pFont != NULL && pFont->GetSafeHandle() != NULL) {
			CWnd *pWnd = pParent->GetDlgItem(uCtrlID);
			if (pWnd != NULL)
				pWnd->SetFont(pFont);
		}
	}
}

BEGIN_MESSAGE_MAP(CMiniMuleDlg, CDialog)
	ON_WM_CLOSE()
	ON_WM_CTLCOLOR()
	ON_WM_DESTROY()
	ON_WM_PAINT()
	ON_WM_SYSCOLORCHANGE()
	ON_WM_TIMER()
	ON_BN_CLICKED(IDC_MINIMULE_RESTORE, OnRestoreMainWindow)
	ON_BN_CLICKED(IDC_MINIMULE_OPENINC, OnOpenIncomingFolder)
	ON_BN_CLICKED(IDC_MINIMULE_OPTIONS, OnOptions)
END_MESSAGE_MAP()

CMiniMuleDlg::CMiniMuleDlg(CemuleDlg *pOwner)
	: CDialog(CMiniMuleDlg::IDD, pOwner)
	, m_pOwner(pOwner)
	, m_hAppIcon()
{
}

CMiniMuleDlg::~CMiniMuleDlg()
{
	if (m_hAppIcon != NULL) {
		VERIFY(::DestroyIcon(m_hAppIcon));
		m_hAppIcon = NULL;
	}
}

BOOL CMiniMuleDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	ApplyVisualStyle();
	Localize();
	UpdateContent();
	AutoSizeAndPosition();
	SetTimer(IDT_MINIMULE_REFRESH, SEC2MS(1), NULL);
	return TRUE;
}

void CMiniMuleDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	delete this;
}

void CMiniMuleDlg::OnOK()
{
	DestroyWindow();
}

void CMiniMuleDlg::OnCancel()
{
	DestroyWindow();
}

BOOL CMiniMuleDlg::PreTranslateMessage(MSG *pMsg)
{
	if (pMsg != NULL && pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_ESCAPE) {
		DestroyWindow();
		return TRUE;
	}
	return CDialog::PreTranslateMessage(pMsg);
}

void CMiniMuleDlg::OnClose()
{
	DestroyWindow();
}

HBRUSH CMiniMuleDlg::OnCtlColor(CDC *pDC, CWnd *pWnd, UINT nCtlColor)
{
	if (pDC != NULL && pWnd != NULL && nCtlColor == CTLCOLOR_STATIC) {
		const UINT uCtrlID = static_cast<UINT>(pWnd->GetDlgCtrlID());
		pDC->SetBkMode(TRANSPARENT);
		if (IsHeaderTextControl(uCtrlID)) {
			pDC->SetTextColor(::GetSysColor(COLOR_WINDOWTEXT));
			return static_cast<HBRUSH>(m_brHeader.GetSafeHandle());
		}
		if (IsLabelControl(uCtrlID)) {
			pDC->SetTextColor(::GetSysColor(COLOR_GRAYTEXT));
			return static_cast<HBRUSH>(m_brBackground.GetSafeHandle());
		}
		if (IsValueControl(uCtrlID)) {
			pDC->SetTextColor(::GetSysColor(COLOR_WINDOWTEXT));
			return static_cast<HBRUSH>(m_brBackground.GetSafeHandle());
		}
	}
	return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CMiniMuleDlg::OnDestroy()
{
	KillTimer(IDT_MINIMULE_REFRESH);
	if (m_pOwner != NULL)
		m_pOwner->OnMiniMuleDestroyed(this);
	CDialog::OnDestroy();
}

void CMiniMuleDlg::OnPaint()
{
	CPaintDC dc(this);
	CRect rcClient;
	GetClientRect(&rcClient);
	dc.FillSolidRect(rcClient, ::GetSysColor(COLOR_WINDOW));

	CRect rcHeader(0, 0, 286, 36);
	MapDialogRect(&rcHeader);
	rcHeader.left = rcClient.left;
	rcHeader.top = rcClient.top;
	rcHeader.right = rcClient.right;
	dc.FillSolidRect(rcHeader, ::GetSysColor(COLOR_3DFACE));
	dc.FillSolidRect(rcHeader.left, rcHeader.bottom - 1, rcHeader.right, rcHeader.bottom, ::GetSysColor(COLOR_3DSHADOW));

	CRect rcMetricDivider(12, 76, 274, 77);
	MapDialogRect(&rcMetricDivider);
	dc.FillSolidRect(rcMetricDivider, ::GetSysColor(COLOR_3DLIGHT));

	CRect rcVerticalDivider(143, 42, 144, 68);
	MapDialogRect(&rcVerticalDivider);
	dc.FillSolidRect(rcVerticalDivider, ::GetSysColor(COLOR_3DLIGHT));
}

void CMiniMuleDlg::OnSysColorChange()
{
	CDialog::OnSysColorChange();
	RefreshColorResources();
	Invalidate();
}

void CMiniMuleDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == IDT_MINIMULE_REFRESH)
		UpdateContent();
	else
		CDialog::OnTimer(nIDEvent);
}

void CMiniMuleDlg::OnRestoreMainWindow()
{
	if (m_pOwner != NULL && !theApp.IsClosing()) {
		CemuleDlg *pOwner = m_pOwner;
		m_pOwner = NULL;
		pOwner->OnMiniMuleDestroyed(this);
		ShowWindow(SW_HIDE);
		pOwner->RestoreWindow();
		PostMessage(WM_CLOSE);
	}
}

void CMiniMuleDlg::OnOpenIncomingFolder()
{
	if (m_pOwner != NULL && !theApp.IsClosing())
		m_pOwner->SendMessage(WM_COMMAND, MP_HM_OPENINC);
}

void CMiniMuleDlg::OnOptions()
{
	if (m_pOwner != NULL && !theApp.IsClosing())
		if (m_pOwner->ShowPreferences() == -1)
			MessageBeep(MB_OK);
}

void CMiniMuleDlg::Localize()
{
	SetWindowText(_T("MiniMule"));
	SetDlgItemText(IDC_MINIMULE_TITLE, _T("MiniMule"));
	SetDlgItemText(IDC_MINIMULE_CONNECTED_LABEL, GetResString(IDS_CONNECTED));
	SetDlgItemText(IDC_MINIMULE_UPLOAD_LABEL, GetResString(IDS_PW_CON_UPLBL));
	SetDlgItemText(IDC_MINIMULE_DOWNLOAD_LABEL, GetResString(IDS_PW_CON_DOWNLBL));
	SetDlgItemText(IDC_MINIMULE_COMPLETED_LABEL, GetResString(IDS_DL_TRANSFCOMPL));
	SetDlgItemText(IDC_MINIMULE_FREESPACE_LABEL, GetResString(IDS_STATS_FREESPACE));
	SetDlgItemText(IDC_MINIMULE_ACTIVE_DOWNLOADS_LABEL, GetResString(IDS_ST_ACTIVED));
	SetDlgItemText(IDC_MINIMULE_ACTIVE_UPLOADS_LABEL, GetResString(IDS_ST_ACTIVEU));
	SetDlgItemText(IDC_MINIMULE_WAITING_UPLOADS_LABEL, GetResString(IDS_WAITING));
	SetDlgItemText(IDC_MINIMULE_TOTAL_DOWNLOADS_LABEL, GetResString(IDS_TW_DOWNLOADS));
	SetDlgItemText(IDC_MINIMULE_RESTORE, GetResString(IDS_MAIN_POPUP_RESTORE));
	SetDlgItemText(IDC_MINIMULE_OPENINC, GetResString(IDS_OPENINC));
	SetDlgItemText(IDC_MINIMULE_OPTIONS, GetResString(IDS_EM_PREFS));
}

void CMiniMuleDlg::UpdateContent(UINT uUpDatarate, UINT uDownDatarate)
{
	SetDlgItemText(IDC_MINIMULE_CONNECTED_VALUE, GetResString(theApp.IsConnected() ? IDS_YES : IDS_NO));
	SetDlgItemText(IDC_MINIMULE_UPLOAD_VALUE, m_pOwner != NULL ? m_pOwner->GetUpDatarateString(uUpDatarate) : _T(""));
	SetDlgItemText(IDC_MINIMULE_DOWNLOAD_VALUE, m_pOwner != NULL ? m_pOwner->GetDownDatarateString(uDownDatarate) : _T(""));

	UINT uCompleted = 0;
	if (thePrefs.GetRemoveFinishedDownloads())
		uCompleted = thePrefs.GetDownSessionCompletedFiles();
	else if (m_pOwner != NULL && m_pOwner->transferwnd != NULL && m_pOwner->transferwnd->GetDownloadList()->m_hWnd != NULL) {
		int iTotal = 0;
		uCompleted = static_cast<UINT>(m_pOwner->transferwnd->GetDownloadList()->GetCompleteDownloads(-1, iTotal));
	}

	CString strValue;
	strValue.Format(_T("%u"), uCompleted);
	SetDlgItemText(IDC_MINIMULE_COMPLETED_VALUE, strValue);
	SetDlgItemText(IDC_MINIMULE_FREESPACE_VALUE, CastItoXBytes(GetFreeTempSpace(-1)));

	strValue.Format(_T("%u"), theApp.downloadqueue != NULL ? theApp.downloadqueue->GetDownloadingFileCount() : 0);
	SetDlgItemText(IDC_MINIMULE_ACTIVE_DOWNLOADS_VALUE, strValue);
	strValue.Format(_T("%u"), theApp.uploadqueue != NULL ? static_cast<UINT>(theApp.uploadqueue->GetActiveUploadsCount()) : 0);
	SetDlgItemText(IDC_MINIMULE_ACTIVE_UPLOADS_VALUE, strValue);
	strValue.Format(_T("%u"), theApp.uploadqueue != NULL ? static_cast<UINT>(theApp.uploadqueue->GetWaitingUserCount()) : 0);
	SetDlgItemText(IDC_MINIMULE_WAITING_UPLOADS_VALUE, strValue);
	strValue.Format(_T("%u"), theApp.downloadqueue != NULL ? static_cast<UINT>(theApp.downloadqueue->GetFileCount()) : 0);
	SetDlgItemText(IDC_MINIMULE_TOTAL_DOWNLOADS_VALUE, strValue);
}

void CMiniMuleDlg::AutoSizeAndPosition()
{
	CRect rcWnd;
	GetWindowRect(&rcWnd);

	const CSize sizDesktop(::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN));
	CRect rcTaskbar(0, sizDesktop.cy - 34, sizDesktop.cx, sizDesktop.cy);
	HWND hWndTaskbar = ::FindWindow(_T("Shell_TrayWnd"), NULL);
	if (hWndTaskbar)
		::GetWindowRect(hWndTaskbar, &rcTaskbar);

	UINT uTaskbarPos;
	if (rcTaskbar.left <= 0) {
		if (rcTaskbar.top <= 0)
			uTaskbarPos = (rcTaskbar.Width() > rcTaskbar.Height()) ? ABE_TOP : ABE_LEFT;
		else
			uTaskbarPos = ABE_BOTTOM;
	} else
		uTaskbarPos = ABE_RIGHT;

	CPoint ptWnd;
	switch (uTaskbarPos) {
	case ABE_TOP:
		ptWnd = CPoint(sizDesktop.cx - 8 - rcWnd.Width(), rcTaskbar.Height() + 8);
		break;
	case ABE_LEFT:
		ptWnd = CPoint(rcTaskbar.Width() + 8, sizDesktop.cy - 8 - rcWnd.Height());
		break;
	case ABE_RIGHT:
		ptWnd = CPoint(sizDesktop.cx - rcTaskbar.Width() - 8 - rcWnd.Width(), sizDesktop.cy - 8 - rcWnd.Height());
		break;
	default:
		ptWnd = CPoint(sizDesktop.cx - 8 - rcWnd.Width(), sizDesktop.cy - rcTaskbar.Height() - 8 - rcWnd.Height());
		break;
	}

	SetWindowPos(&wndTopMost, ptWnd.x, ptWnd.y, rcWnd.Width(), rcWnd.Height(), SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void CMiniMuleDlg::ApplyVisualStyle()
{
	RefreshColorResources();
	CreateDerivedFonts();

	SetControlFont(this, IDC_MINIMULE_TITLE, &m_fontTitle);
	SetControlFont(this, IDC_MINIMULE_UPLOAD_VALUE, &m_fontMetricValue);
	SetControlFont(this, IDC_MINIMULE_DOWNLOAD_VALUE, &m_fontMetricValue);

	const UINT aValueControls[] = {
		IDC_MINIMULE_CONNECTED_VALUE,
		IDC_MINIMULE_COMPLETED_VALUE,
		IDC_MINIMULE_FREESPACE_VALUE,
		IDC_MINIMULE_ACTIVE_DOWNLOADS_VALUE,
		IDC_MINIMULE_ACTIVE_UPLOADS_VALUE,
		IDC_MINIMULE_WAITING_UPLOADS_VALUE,
		IDC_MINIMULE_TOTAL_DOWNLOADS_VALUE
	};
	for (UINT uCtrlID : aValueControls)
		SetControlFont(this, uCtrlID, &m_fontValue);

	m_hAppIcon = theApp.LoadIcon(_T("AAAEMULEAPP"), 20, 20);
	if (m_hAppIcon != NULL) {
		SetIcon(m_hAppIcon, FALSE);
		if (CWnd *pIconWnd = GetDlgItem(IDC_MINIMULE_ICON))
			static_cast<CStatic*>(pIconWnd)->SetIcon(m_hAppIcon);
	}
}

void CMiniMuleDlg::CreateDerivedFonts()
{
	CFont *pBaseFont = GetFont();
	if (pBaseFont == NULL)
		return;

	LOGFONT lf = {};
	if (pBaseFont->GetLogFont(&lf) == 0)
		return;

	LOGFONT lfTitle = lf;
	lfTitle.lfWeight = FW_BOLD;
	if (lfTitle.lfHeight < 0)
		lfTitle.lfHeight = lfTitle.lfHeight * 13 / 10;
	else if (lfTitle.lfHeight > 0)
		lfTitle.lfHeight = lfTitle.lfHeight * 13 / 10;
	m_fontTitle.DeleteObject();
	VERIFY(m_fontTitle.CreateFontIndirect(&lfTitle));

	LOGFONT lfMetric = lf;
	lfMetric.lfWeight = FW_BOLD;
	if (lfMetric.lfHeight < 0)
		lfMetric.lfHeight = lfMetric.lfHeight * 12 / 10;
	else if (lfMetric.lfHeight > 0)
		lfMetric.lfHeight = lfMetric.lfHeight * 12 / 10;
	m_fontMetricValue.DeleteObject();
	VERIFY(m_fontMetricValue.CreateFontIndirect(&lfMetric));

	LOGFONT lfValue = lf;
	lfValue.lfWeight = FW_BOLD;
	m_fontValue.DeleteObject();
	VERIFY(m_fontValue.CreateFontIndirect(&lfValue));
}

void CMiniMuleDlg::RefreshColorResources()
{
	m_brBackground.DeleteObject();
	VERIFY(m_brBackground.CreateSolidBrush(::GetSysColor(COLOR_WINDOW)));
	m_brHeader.DeleteObject();
	VERIFY(m_brHeader.CreateSolidBrush(::GetSysColor(COLOR_3DFACE)));
}

bool CMiniMuleDlg::IsHeaderTextControl(UINT uCtrlID) const
{
	return uCtrlID == IDC_MINIMULE_TITLE
		|| uCtrlID == IDC_MINIMULE_ICON
		|| uCtrlID == IDC_MINIMULE_CONNECTED_LABEL
		|| uCtrlID == IDC_MINIMULE_CONNECTED_VALUE;
}

bool CMiniMuleDlg::IsLabelControl(UINT uCtrlID) const
{
	switch (uCtrlID) {
	case IDC_MINIMULE_UPLOAD_LABEL:
	case IDC_MINIMULE_DOWNLOAD_LABEL:
	case IDC_MINIMULE_COMPLETED_LABEL:
	case IDC_MINIMULE_FREESPACE_LABEL:
	case IDC_MINIMULE_ACTIVE_DOWNLOADS_LABEL:
	case IDC_MINIMULE_ACTIVE_UPLOADS_LABEL:
	case IDC_MINIMULE_WAITING_UPLOADS_LABEL:
	case IDC_MINIMULE_TOTAL_DOWNLOADS_LABEL:
		return true;
	default:
		return false;
	}
}

bool CMiniMuleDlg::IsMetricValueControl(UINT uCtrlID) const
{
	return uCtrlID == IDC_MINIMULE_UPLOAD_VALUE
		|| uCtrlID == IDC_MINIMULE_DOWNLOAD_VALUE;
}

bool CMiniMuleDlg::IsValueControl(UINT uCtrlID) const
{
	return IsMetricValueControl(uCtrlID)
		|| uCtrlID == IDC_MINIMULE_CONNECTED_VALUE
		|| uCtrlID == IDC_MINIMULE_COMPLETED_VALUE
		|| uCtrlID == IDC_MINIMULE_FREESPACE_VALUE
		|| uCtrlID == IDC_MINIMULE_ACTIVE_DOWNLOADS_VALUE
		|| uCtrlID == IDC_MINIMULE_ACTIVE_UPLOADS_VALUE
		|| uCtrlID == IDC_MINIMULE_WAITING_UPLOADS_VALUE
		|| uCtrlID == IDC_MINIMULE_TOTAL_DOWNLOADS_VALUE;
}
