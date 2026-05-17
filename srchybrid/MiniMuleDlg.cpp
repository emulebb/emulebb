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
	, m_auUploadHistory()
	, m_auDownloadHistory()
	, m_uHistoryNext()
	, m_uHistoryCount()
{
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
	if (ShouldStartDragFromMessage(pMsg)) {
		ReleaseCapture();
		SendMessage(WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(pMsg->pt.x, pMsg->pt.y));
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
	dc.Draw3dRect(rcClient, ::GetSysColor(COLOR_3DSHADOW), ::GetSysColor(COLOR_3DHILIGHT));

	DrawTableFrame(dc);
	DrawSpeedChart(dc);
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
	const UINT uResolvedUpDatarate = uUpDatarate != UINT_MAX ? uUpDatarate : (theApp.uploadqueue != NULL ? theApp.uploadqueue->GetDatarate() : 0);
	const UINT uResolvedDownDatarate = uDownDatarate != UINT_MAX ? uDownDatarate : (theApp.downloadqueue != NULL ? theApp.downloadqueue->GetDatarate() : 0);

	SetDlgItemText(IDC_MINIMULE_CONNECTED_VALUE, GetResString(theApp.IsConnected() ? IDS_YES : IDS_NO));
	SetDlgItemText(IDC_MINIMULE_UPLOAD_VALUE, m_pOwner != NULL ? m_pOwner->GetUpDatarateString(uResolvedUpDatarate) : _T(""));
	SetDlgItemText(IDC_MINIMULE_DOWNLOAD_VALUE, m_pOwner != NULL ? m_pOwner->GetDownDatarateString(uResolvedDownDatarate) : _T(""));
	TrackSpeedSample(uResolvedUpDatarate, uResolvedDownDatarate);

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

	Invalidate(FALSE);
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
}

void CMiniMuleDlg::RefreshColorResources()
{
	m_brBackground.DeleteObject();
	VERIFY(m_brBackground.CreateSolidBrush(::GetSysColor(COLOR_WINDOW)));
}

CRect CMiniMuleDlg::MapDlgUnits(int iLeft, int iTop, int iRight, int iBottom)
{
	CRect rc(iLeft, iTop, iRight, iBottom);
	MapDialogRect(&rc);
	return rc;
}

void CMiniMuleDlg::DrawTableFrame(CDC &dc)
{
	const CRect rcTable = MapDlgUnits(8, 8, 278, 125);
	dc.Draw3dRect(rcTable, ::GetSysColor(COLOR_3DLIGHT), ::GetSysColor(COLOR_3DSHADOW));

	const int iRowTop = MapDlgUnits(0, 8, 0, 8).top;
	for (int iRow = 1; iRow < 9; ++iRow) {
		const int y = MapDlgUnits(0, 8 + iRow * 13, 0, 8 + iRow * 13).top;
		dc.FillSolidRect(rcTable.left + 1, y, rcTable.Width() - 2, 1, ::GetSysColor(COLOR_3DLIGHT));
	}

	const int xDivider = MapDlgUnits(137, 0, 137, 0).left;
	dc.FillSolidRect(xDivider, iRowTop + 1, 1, rcTable.Height() - 2, ::GetSysColor(COLOR_3DLIGHT));
}

void CMiniMuleDlg::DrawSpeedChart(CDC &dc)
{
	const CRect rcChart = MapDlgUnits(8, 132, 278, 166);
	dc.Draw3dRect(rcChart, ::GetSysColor(COLOR_3DLIGHT), ::GetSysColor(COLOR_3DSHADOW));

	CRect rcPlot = rcChart;
	rcPlot.DeflateRect(4, 4);
	dc.FillSolidRect(rcPlot, ::GetSysColor(COLOR_WINDOW));

	const int yMiddle = rcPlot.top + rcPlot.Height() / 2;
	dc.FillSolidRect(rcPlot.left, yMiddle, rcPlot.Width(), 1, ::GetSysColor(COLOR_3DLIGHT));

	if (m_uHistoryCount < 2)
		return;

	UINT uMaxRate = 1;
	for (UINT i = 0; i < m_uHistoryCount; ++i) {
		const UINT uIndex = m_uHistoryCount == SPEED_HISTORY_SIZE ? (m_uHistoryNext + i) % SPEED_HISTORY_SIZE : i;
		uMaxRate = max(uMaxRate, m_auUploadHistory[uIndex]);
		uMaxRate = max(uMaxRate, m_auDownloadHistory[uIndex]);
	}

	CPen penUpload(PS_SOLID, 1, ::GetSysColor(COLOR_WINDOWTEXT));
	CPen penDownload(PS_SOLID, 1, ::GetSysColor(COLOR_HIGHLIGHT));
	CPen *pOldPen = dc.SelectObject(&penUpload);

	for (int iSeries = 0; iSeries < 2; ++iSeries) {
		dc.SelectObject(iSeries == 0 ? &penUpload : &penDownload);
		for (UINT i = 0; i < m_uHistoryCount; ++i) {
			const UINT uIndex = m_uHistoryCount == SPEED_HISTORY_SIZE ? (m_uHistoryNext + i) % SPEED_HISTORY_SIZE : i;
			const UINT uRate = iSeries == 0 ? m_auUploadHistory[uIndex] : m_auDownloadHistory[uIndex];
			const int x = rcPlot.left + static_cast<int>(i * (rcPlot.Width() - 1) / (m_uHistoryCount - 1));
			const int y = rcPlot.bottom - 1 - MulDiv(static_cast<int>(uRate), rcPlot.Height() - 1, static_cast<int>(uMaxRate));
			if (i == 0)
				dc.MoveTo(x, y);
			else
				dc.LineTo(x, y);
		}
	}

	dc.SelectObject(pOldPen);
}

void CMiniMuleDlg::TrackSpeedSample(UINT uUpDatarate, UINT uDownDatarate)
{
	m_auUploadHistory[m_uHistoryNext] = uUpDatarate;
	m_auDownloadHistory[m_uHistoryNext] = uDownDatarate;
	m_uHistoryNext = (m_uHistoryNext + 1) % SPEED_HISTORY_SIZE;
	if (m_uHistoryCount < SPEED_HISTORY_SIZE)
		++m_uHistoryCount;
}

bool CMiniMuleDlg::ShouldStartDragFromMessage(const MSG *pMsg) const
{
	if (pMsg == NULL || pMsg->message != WM_LBUTTONDOWN || m_hWnd == NULL)
		return false;
	if (pMsg->hwnd != m_hWnd && !::IsChild(m_hWnd, pMsg->hwnd))
		return false;

	TCHAR szClassName[32] = {};
	::GetClassName(pMsg->hwnd, szClassName, _countof(szClassName));
	if (_tcscmp(szClassName, _T("Button")) == 0)
		return false;

	return true;
}

bool CMiniMuleDlg::IsLabelControl(UINT uCtrlID) const
{
	switch (uCtrlID) {
	case IDC_MINIMULE_CONNECTED_LABEL:
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

bool CMiniMuleDlg::IsValueControl(UINT uCtrlID) const
{
	return uCtrlID == IDC_MINIMULE_UPLOAD_VALUE
		|| uCtrlID == IDC_MINIMULE_DOWNLOAD_VALUE
		|| uCtrlID == IDC_MINIMULE_CONNECTED_VALUE
		|| uCtrlID == IDC_MINIMULE_COMPLETED_VALUE
		|| uCtrlID == IDC_MINIMULE_FREESPACE_VALUE
		|| uCtrlID == IDC_MINIMULE_ACTIVE_DOWNLOADS_VALUE
		|| uCtrlID == IDC_MINIMULE_ACTIVE_UPLOADS_VALUE
		|| uCtrlID == IDC_MINIMULE_WAITING_UPLOADS_VALUE
		|| uCtrlID == IDC_MINIMULE_TOTAL_DOWNLOADS_VALUE;
}
