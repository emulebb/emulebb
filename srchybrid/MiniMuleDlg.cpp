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
	ON_WM_DESTROY()
	ON_WM_TIMER()
	ON_BN_CLICKED(IDC_MINIMULE_RESTORE, OnRestoreMainWindow)
	ON_BN_CLICKED(IDC_MINIMULE_OPENINC, OnOpenIncomingFolder)
	ON_BN_CLICKED(IDC_MINIMULE_OPTIONS, OnOptions)
END_MESSAGE_MAP()

CMiniMuleDlg::CMiniMuleDlg(CemuleDlg *pOwner)
	: CDialog(CMiniMuleDlg::IDD, pOwner)
	, m_pOwner(pOwner)
{
}

BOOL CMiniMuleDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
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

void CMiniMuleDlg::OnDestroy()
{
	KillTimer(IDT_MINIMULE_REFRESH);
	if (m_pOwner != NULL)
		m_pOwner->OnMiniMuleDestroyed(this);
	CDialog::OnDestroy();
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
