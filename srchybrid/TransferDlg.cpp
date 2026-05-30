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
#include "emuleDlg.h"
#include "TransferDlg.h"
#include "OtherFunctions.h"
#include "HelpIDs.h"
#include "Preferences.h"
#include "AppKeyboardShortcutsSeams.h"
#include "MuleToolBarCtrl.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


#define	IDBAR_DOWNLOAD_TOOLBAR		(AFX_IDW_CONTROLBAR_FIRST + 32 + 2)	// do NOT change that ID, if not absolutely needed (it is stored by MFC in the bar profile!)
#define	DOWNLOAD_TOOLBAR_PROFILE	_T("DownloadFrmBarState")

IMPLEMENT_DYNCREATE(CTransferDlg, CFrameWnd)

BEGIN_MESSAGE_MAP(CTransferDlg, CFrameWnd)
	ON_WM_CREATE()
	ON_WM_SHOWWINDOW()
	ON_WM_SETFOCUS()
	ON_WM_CLOSE()
	ON_WM_SYSCOMMAND()
	ON_WM_HELPINFO()
END_MESSAGE_MAP()

CTransferDlg::CTransferDlg()
	: m_pwndTransfer()
{
}

BOOL CTransferDlg::CreateWnd(CWnd *pParent)
{
	// *) The initial size of that frame window must not exceed the window size of the
	//    dialog resource template of the client window (the search results window).
	// *) The dialog resource template's window size (of the search results window) must not
	//	  exceed the minimum client area of the frame window.
	// Otherwise we may get scrollbars in the search results window
	static const RECT rc{0, 0, 50, 50};
	return CFrameWnd::Create(NULL, _T("Transfer"), WS_CHILD | WS_CLIPCHILDREN, rc, pParent, NULL, 0, NULL);
}

int CTransferDlg::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CFrameWnd::OnCreate(lpCreateStruct) == -1)
		return -1;

	CCreateContext context;
	context.m_pCurrentFrame = this;
	context.m_pCurrentDoc = NULL;
	context.m_pNewViewClass = RUNTIME_CLASS(CTransferWnd);
	context.m_pNewDocTemplate = NULL;
#ifdef _DEBUG
	CFrameDoc dummy;
	context.m_pCurrentDoc = &dummy;
#endif
	m_pwndTransfer = static_cast<CTransferWnd*>(CreateView(&context));
	m_pwndTransfer->ModifyStyle(WS_BORDER, 0);
	m_pwndTransfer->ModifyStyleEx(WS_EX_CLIENTEDGE, WS_EX_STATICEDGE);

	m_wndToolbar.Create(this, IDD_DOWNLOAD_TOOLBARS
		, WS_CHILD | WS_VISIBLE | CBRS_TOP | CBRS_SIZE_FIXED | CBRS_SIZE_DYNAMIC | CBRS_GRIPPER
		, IDBAR_DOWNLOAD_TOOLBAR);
	ASSERT(m_wndToolbar.GetStyle() & WS_CLIPSIBLINGS);
	ASSERT(m_wndToolbar.GetStyle() & WS_CLIPCHILDREN);
	m_wndToolbar.SetWindowText(GetResString(IDS_DOWNLOADCOMMANDS));
	m_wndToolbar.EnableDocking(CBRS_ALIGN_ANY);

	EnableDocking(CBRS_ALIGN_ANY);
	DockControlBar(&m_wndToolbar, AFX_IDW_DOCKBAR_LEFT, (LPRECT)NULL);

	m_pwndTransfer->SendMessage(WM_INITIALUPDATE);

	LoadBarState(DOWNLOAD_TOOLBAR_PROFILE);
	DockToolbarWnd(); // Too many bug reports about vanished search parameters window. Force to dock.
	ShowToolbar(thePrefs.IsDownloadToolbarEnabled());
	m_wndToolbar.SetCommandTargetWnd(GetDownloadList());
	Localize();

	return 0;
}

void CTransferDlg::OnClose()
{
	SaveBarState(DOWNLOAD_TOOLBAR_PROFILE);
	CFrameWnd::OnClose();
}

void CTransferDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CFrameWnd::OnShowWindow(bShow, nStatus);
	if (m_wndToolbar.IsFloating()) {
		//ShowControlBar(&m_pwndParams, bShow, TRUE);
		DockToolbarWnd(); // Too many bug reports about vanished search parameters window. Force to dock.
	}
	if (m_pwndTransfer != NULL)
		m_pwndTransfer->RefreshTransferDisplayRefreshState(bShow != FALSE);
}

void CTransferDlg::OnSetFocus(CWnd *pOldWnd)
{
	CFrameWnd::OnSetFocus(pOldWnd);
}

void CTransferDlg::DockToolbarWnd()
{
	if (m_wndToolbar.IsFloating()) {
		UINT uMRUDockID = AFX_IDW_DOCKBAR_TOP;
		if (m_wndToolbar.m_pDockContext)
			uMRUDockID = m_wndToolbar.m_pDockContext->m_uMRUDockID;
		DockControlBar(&m_wndToolbar, uMRUDockID);
	}
}

void CTransferDlg::Localize()
{
	m_pwndTransfer->Localize();
	m_wndToolbar.Localize();
}

void CTransferDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) != SC_KEYMENU)
		CFrameWnd::OnSysCommand(nID, lParam);
	else {
		const AppKeyboardShortcutsSeams::ECommand eShortcutCommand =
			AppKeyboardShortcutsSeams::ClassifySystemKeyMenu(nID, lParam, false);
		if (eShortcutCommand != AppKeyboardShortcutsSeams::ECommand::None) {
			theApp.emuledlg->SendMessage(WM_SYSCOMMAND, nID, lParam);
			return;
		} else
			theApp.emuledlg->SendMessage(WM_SYSCOMMAND, nID, lParam);
	}
}

BOOL CTransferDlg::PreTranslateMessage(MSG *pMsg)
{
	if (pMsg->message == WM_KEYDOWN) {
		// Don't handle Ctrl+Tab in this window. It will be handled by main window.
		if (pMsg->wParam == VK_TAB && GetKeyState(VK_CONTROL) < 0) {
			// UGLY: Because this window is a 'CFrameWnd' (rather than a 'CDialog' like
			// the other eMule main windows) we can not use MFC's message routing. Need
			// to explicitly send that message to the main window.
			theApp.emuledlg->PostMessage(pMsg->message, pMsg->wParam, pMsg->lParam);
			return TRUE;
		}
	}
	return CFrameWnd::PreTranslateMessage(pMsg);
}

BOOL CTransferDlg::OnHelpInfo(HELPINFO*)
{
	theApp.ShowHelp(eMule_FAQ_GUI_Transfers);
	return TRUE;
}

void CTransferDlg::ShowToolbar(bool bShow)
{
	if (m_wndToolbar.IsVisible() != static_cast<BOOL>(bShow))
		if (thePrefs.IsDownloadToolbarEnabled() || !bShow)
			ShowControlBar(&m_wndToolbar, static_cast<BOOL>(bShow), TRUE);
}



//////////////////////////////////////////////////////////////////////
// Wrappers

void CTransferDlg::ShowQueueCount(INT_PTR number)
{
	m_pwndTransfer->ShowQueueCount(number);
}

void CTransferDlg::UpdateFilesCount(int iCount)
{
	m_pwndTransfer->UpdateFilesCount(iCount);
}

void CTransferDlg::UpdateCatTabTitles(bool force)
{
	m_pwndTransfer->UpdateCatTabTitles(force);
}

void CTransferDlg::VerifyCatTabSize()
{
	m_pwndTransfer->VerifyCatTabSize();
}

int CTransferDlg::AddCategoryInteractive()
{
	return m_pwndTransfer->AddCategoryInteractive();
}

void CTransferDlg::ManageCategoriesInteractive()
{
	m_pwndTransfer->ManageCategoriesInteractive();
}

void CTransferDlg::SwitchUploadList()
{
	m_pwndTransfer->SwitchUploadList();
}

void CTransferDlg::ResetTransToolbar(bool bShowToolbar, bool bResetLists)
{
	m_pwndTransfer->ResetTransToolbar(bShowToolbar, bResetLists);
}

void CTransferDlg::SetToolTipsDelay(DWORD dwDelay)
{
	m_pwndTransfer->SetToolTipsDelay(dwDelay);
}

void CTransferDlg::OnDisableList()
{
	m_pwndTransfer->OnDisableList();
}

void CTransferDlg::UpdateListCount(CTransferWnd::EWnd2 listindex, int iCount)
{
	m_pwndTransfer->UpdateListCount(listindex, iCount);
}

void CTransferDlg::QueueDisplayRefresh(uint32 nMask, bool bForce)
{
	if (m_pwndTransfer != NULL)
		m_pwndTransfer->QueueDisplayRefresh(nMask, bForce);
}

void CTransferDlg::RefreshTransferDisplayRefreshState(bool bFlushIfRunning)
{
	if (m_pwndTransfer != NULL)
		m_pwndTransfer->RefreshTransferDisplayRefreshState(bFlushIfRunning);
}

void CTransferDlg::FlushVisibleDisplayRefreshes()
{
	if (m_pwndTransfer != NULL)
		m_pwndTransfer->FlushVisibleDisplayRefreshes();
}

int CTransferDlg::AddCategory(const CString &newtitle, const CString &newincoming, const CString &newcomment, const CString &newautocat, bool addTab)
{
	return m_pwndTransfer->AddCategory(newtitle, newincoming, newcomment, newautocat, addTab);
}


CUploadListCtrl* CTransferDlg::GetUploadList()
{
	return &m_pwndTransfer->uploadlistctrl;
}

CDownloadListCtrl* CTransferDlg::GetDownloadList()
{
	return &m_pwndTransfer->downloadlistctrl;
}

CQueueListCtrl*	CTransferDlg::GetQueueList()
{
	return &m_pwndTransfer->queuelistctrl;
}

CClientListCtrl* CTransferDlg::GetClientList()
{
	return &m_pwndTransfer->clientlistctrl;
}

CDownloadClientsCtrl* CTransferDlg::GetDownloadClientsList()
{
	return &m_pwndTransfer->downloadclientsctrl;
}
//////////////////////////////////////////////////////////////////
