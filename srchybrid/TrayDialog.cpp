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
#include "TrayDialog.h"
#include "Preferences.h"
#include "MenuCmds.h"
#include "TrayNotificationSeams.h"
#include "UserMsgs.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define	IDT_SINGLE_CLICK	100

//#define	NOTIFYICONDATA_V1_TIP_SIZE	64
#define	NOTIFYICONDATA_TIP_SIZE		128 //for Windows 2K and above

/////////////////////////////////////////////////////////////////////////////
// CTrayDialog dialog

const UINT WM_TASKBARCREATED = RegisterWindowMessage(_T("TaskbarCreated"));

BEGIN_MESSAGE_MAP(CTrayDialog, CTrayDialogBase)
	ON_WM_CREATE()
	ON_WM_DESTROY()
	ON_WM_SYSCOMMAND()
	ON_MESSAGE(UM_TRAY_ICON_NOTIFY_MESSAGE, OnTrayNotify)
	ON_REGISTERED_MESSAGE(WM_TASKBARCREATED, OnTaskBarCreated)
	ON_WM_TIMER()
END_MESSAGE_MAP()

CTrayDialog::CTrayDialog(UINT uIDD, CWnd *pParent /*=NULL*/)
	: CTrayDialogBase(uIDD, pParent)
	, m_pbMinimizeToTray()
	, m_hPrevIconDelete()
	, m_uSingleClickTimer()
	, u_DblClickSpeed(::GetDoubleClickTime())
	, m_uLButtonDown()
	, m_bCurIconDelete()
	, m_bTrayIconVisible()
	, m_bTrayModernCallbacks()
	//, m_nDefaultMenuItem()
{
	memset(&m_nidIconData, 0, sizeof m_nidIconData);
	m_nidIconData.cbSize = sizeof(m_nidIconData);
	m_nidIconData.hWnd = 0;
	m_nidIconData.uID = 1;
	m_nidIconData.uCallbackMessage = UM_TRAY_ICON_NOTIFY_MESSAGE;
	m_nidIconData.hIcon = 0;
	m_nidIconData.szTip[0] = _T('\0');
	m_nidIconData.uFlags = NIF_MESSAGE;
}

int CTrayDialog::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CTrayDialogBase::OnCreate(lpCreateStruct) == -1)
		return -1;

	ASSERT(WM_TASKBARCREATED);
	m_nidIconData.hWnd = m_hWnd;
	m_nidIconData.uID = 1;
	return 0;
}

void CTrayDialog::OnDestroy()
{
	KillSingleClickTimer();
	CTrayDialogBase::OnDestroy();

	// shouldn't that be done before passing the message to DefWinProc?
	if (m_nidIconData.hWnd && m_nidIconData.uID > 0 && TrayIconVisible())
		VERIFY(Shell_NotifyIcon(NIM_DELETE, &m_nidIconData));
}

void CTrayDialog::TraySetIcon(HICON hIcon, bool bDelete)
{
	ASSERT(hIcon);
	if (hIcon) {
		//ASSERT(m_hPrevIconDelete == NULL);
		if (m_bCurIconDelete) {
			ASSERT(m_nidIconData.hIcon != NULL && (m_nidIconData.uFlags & NIF_ICON));
			m_hPrevIconDelete = m_nidIconData.hIcon;
		}
		m_bCurIconDelete = bDelete;
		m_nidIconData.hIcon = hIcon;
		m_nidIconData.uFlags |= NIF_ICON;
	}
}

void CTrayDialog::TraySetIcon(UINT nResourceID)
{
	TraySetIcon(AfxGetApp()->LoadIcon(nResourceID));
}

void CTrayDialog::TraySetIcon(LPCTSTR lpszResourceName)
{
	TraySetIcon(AfxGetApp()->LoadIcon(lpszResourceName));
}

void CTrayDialog::TraySetToolTip(LPCTSTR lpszToolTip)
{
	ASSERT(*lpszToolTip != '\0' && _tcslen(lpszToolTip) < NOTIFYICONDATA_TIP_SIZE);
	_tcsncpy_s(m_nidIconData.szTip, _countof(m_nidIconData.szTip), lpszToolTip, _TRUNCATE);
	m_nidIconData.uFlags |= NIF_TIP;

	Shell_NotifyIcon(NIM_MODIFY, &m_nidIconData);
}

bool CTrayDialog::TrayShow()
{
	if (m_bTrayIconVisible)
		return true;

	m_bTrayIconVisible = Shell_NotifyIcon(NIM_ADD, &m_nidIconData) != 0;
	if (m_bTrayIconVisible) {
		m_nidIconData.uVersion = NOTIFYICON_VERSION_4;
		m_bTrayModernCallbacks = Shell_NotifyIcon(NIM_SETVERSION, &m_nidIconData) != 0;
	}
	return m_bTrayIconVisible;
}

bool CTrayDialog::TrayHide()
{
	bool bSuccess = Shell_NotifyIcon(NIM_DELETE, &m_nidIconData);
	m_bTrayIconVisible = false;
	m_bTrayModernCallbacks = false;
	m_uLButtonDown = 0;
	return bSuccess;
}

void CTrayDialog::TrayReset()
{
	Shell_NotifyIcon(NIM_DELETE, &m_nidIconData); //fix for DPI change as it keeps the icon
	const bool bWasVisible = m_bTrayIconVisible;
	m_bTrayIconVisible = false;
	m_bTrayModernCallbacks = false;
	if (bWasVisible)
		TrayShow();
}

BOOL CTrayDialog::TrayUpdate()
{
	BOOL bSuccess = FALSE;
	if (m_bTrayIconVisible) {
		bSuccess = Shell_NotifyIcon(NIM_MODIFY, &m_nidIconData);
		if (!bSuccess) {
			//ASSERT(0);
			return FALSE; // don't delete 'm_hPrevIconDelete' because it's still attached to the tray
		}
	}

	if (m_hPrevIconDelete != NULL) {
		VERIFY(::DestroyIcon(m_hPrevIconDelete));
		m_hPrevIconDelete = NULL;
	}

	return bSuccess;
}

bool CTrayDialog::TrayShowBalloon(LPCTSTR pszTitle, LPCTSTR pszText, DWORD dwInfoFlags)
{
	if (!m_bTrayIconVisible)
		return false;

	NOTIFYICONDATA nidBalloon = m_nidIconData;
	nidBalloon.uFlags |= NIF_INFO;
	_tcsncpy_s(nidBalloon.szInfoTitle, _countof(nidBalloon.szInfoTitle), pszTitle != NULL ? pszTitle : _T(""), _TRUNCATE);
	_tcsncpy_s(nidBalloon.szInfo, _countof(nidBalloon.szInfo), pszText != NULL ? pszText : _T(""), _TRUNCATE);
	nidBalloon.dwInfoFlags = dwInfoFlags;
	return Shell_NotifyIcon(NIM_MODIFY, &nidBalloon) != 0;
}

BOOL CTrayDialog::TraySetMenu(UINT nResourceID)
{
	BOOL bSuccess = m_mnuTrayMenu.LoadMenu(nResourceID);
	ASSERT(bSuccess);
	return bSuccess;
}

BOOL CTrayDialog::TraySetMenu(LPCTSTR lpszMenuName)
{
	BOOL bSuccess = m_mnuTrayMenu.LoadMenu(lpszMenuName);
	ASSERT(bSuccess);
	return bSuccess;
}

BOOL CTrayDialog::TraySetMenu(HMENU hMenu)
{
	m_mnuTrayMenu.Attach(hMenu);
	return TRUE;
}

LRESULT CTrayDialog::OnTrayNotify(WPARAM wParam, LPARAM lParam)
{
	UINT uEvent = 0;
	if (m_bTrayModernCallbacks) {
		if (HIWORD(lParam) != m_nidIconData.uID)
			return 0;
		uEvent = LOWORD(lParam);
	} else {
		if (wParam != m_nidIconData.uID) //check ID
			return 0;
		uEvent = static_cast<UINT>(lParam);
	}

	if (uEvent == NIN_BALLOONUSERCLICK) {
		OnTrayBalloonUserClick();
		return 1;
	}

	if (uEvent == NIN_KEYSELECT) {
		KillSingleClickTimer();
		RestoreWindow();
		return 1;
	}
	if (uEvent == NIN_SELECT) {
		KillSingleClickTimer();
		TrayNotificationSeams::CTrayPrimaryActivationState state;
		state.bMiniMuleEnabled = thePrefs.GetEnableMiniMule();
		state.bMainWindowVisible = IsWindowVisible() != FALSE;
		state.eActivation = TrayNotificationSeams::ETrayPrimaryActivation::MouseSingleClick;
		const TrayNotificationSeams::ETrayPrimaryActivationAction eAction =
			TrayNotificationSeams::ResolveTrayPrimaryActivation(state);
		if (eAction == TrayNotificationSeams::ETrayPrimaryActivationAction::RestoreMainWindow)
			RestoreWindow();
		else if (eAction == TrayNotificationSeams::ETrayPrimaryActivationAction::ShowMiniMule)
			QueueSingleClickTrayAction();
		return 1;
	}

	POINT pt;
	switch (uEvent) {
	case WM_MOUSEMOVE:
		OnTrayMouseMove();
		break;
	case WM_LBUTTONDOWN:
		OnTrayLButtonDown();
		break;
	case WM_LBUTTONUP:
		// Handle the WM_LBUTTONUP only if we know that there was also an according
		// WM_LBUTTONDOWN or WM_LBUTTONDBLCLK on our tray bar icon. If we would handle
		// WM_LBUTTONUP without checking this, we may get a single WM_LBUTTONUP message
		// whereby the according WM_LBUTTONDOWN message was meant for some other tray bar
		// icon.
		if (m_uLButtonDown) {
			TrayNotificationSeams::CTrayPrimaryActivationState state;
			state.bMiniMuleEnabled = thePrefs.GetEnableMiniMule();
			state.bMainWindowVisible = IsWindowVisible() != FALSE;
			state.eActivation = (m_uLButtonDown > 1)
				? TrayNotificationSeams::ETrayPrimaryActivation::MouseDoubleClick
				: TrayNotificationSeams::ETrayPrimaryActivation::MouseSingleClick;
			const TrayNotificationSeams::ETrayPrimaryActivationAction eAction =
				TrayNotificationSeams::ResolveTrayPrimaryActivation(state);
			if (eAction == TrayNotificationSeams::ETrayPrimaryActivationAction::RestoreMainWindow) {
				KillSingleClickTimer();
				RestoreWindow();
			} else if (eAction == TrayNotificationSeams::ETrayPrimaryActivationAction::ShowMiniMule)
				QueueSingleClickTrayAction();
			else
				KillSingleClickTimer();
		}
		break;
	case WM_LBUTTONDBLCLK:
		KillSingleClickTimer();
		OnTrayLButtonDblClk();
		RestoreWindow();
		m_uLButtonDown = 0;
		break;
	case WM_RBUTTONUP:
	case WM_CONTEXTMENU:
		KillSingleClickTimer();
		::GetCursorPos(&pt);
		OnTrayRButtonUp(pt);
		break;
	case WM_RBUTTONDBLCLK:
		KillSingleClickTimer();
		OnTrayRButtonDblClk();
	}
	return 1;
}

void CTrayDialog::KillSingleClickTimer()
{
	if (m_uSingleClickTimer) {
		VERIFY(KillTimer(m_uSingleClickTimer));
		m_uSingleClickTimer = 0;
	}
	m_uLButtonDown = 0;
}

void CTrayDialog::QueueSingleClickTrayAction()
{
	if (!m_uSingleClickTimer)
		m_uSingleClickTimer = SetTimer(IDT_SINGLE_CLICK, u_DblClickSpeed, NULL);
}

void CTrayDialog::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == m_uSingleClickTimer) {
		TRACE("%s: nIDEvent=%u\n", __FUNCTION__, nIDEvent);
		// Kill that timer before calling 'OnTrayLButtonUp' to keep tray-click state consistent.
		KillSingleClickTimer();
		OnTrayLButtonUp();
	} else
		CDialogMinTrayBtn<CResizableDialog>::OnTimer(nIDEvent);
}

void CTrayDialog::OnSysCommand(UINT nID, LPARAM lParam)
{
	if (m_pbMinimizeToTray && *m_pbMinimizeToTray) {
		if ((nID & 0xFFF0) == SC_MINIMIZE) {
			if (TrayShow())
				ShowWindow(SW_HIDE);
		} else
			CTrayDialogBase::OnSysCommand(nID, lParam);
	} else if ((nID & 0xFFF0) == MP_MINIMIZETOTRAY) {
		if (TrayShow())
			ShowWindow(SW_HIDE);
	} else
		CTrayDialogBase::OnSysCommand(nID, lParam);
}

void CTrayDialog::TrayMinimizeToTrayChange()
{
	if (m_pbMinimizeToTray)
		if (*m_pbMinimizeToTray || thePrefs.IsRunningAeroGlassTheme()) //there is no easy way to draw in caption
			MinTrayBtnHide();
		else
			MinTrayBtnShow();
}

void CTrayDialog::OnTrayRButtonUp(CPoint)
{
}

void CTrayDialog::OnTrayLButtonDown()
{
	/*POINT pt;
	::GetCursorPos(&pt);
	ClientToScreen(&pt);*/
	m_uLButtonDown = 1;
}

void CTrayDialog::OnTrayLButtonUp()
{
}

void CTrayDialog::OnTrayLButtonDblClk()
{
	/*POINT pt;
	::GetCursorPos(&pt);
	ClientToScreen(&pt);*/
	m_uLButtonDown = 2;
}

void CTrayDialog::OnTrayRButtonDblClk()
{
	/*POINT pt;
	::GetCursorPos(&pt);
	ClientToScreen(&pt);*/
}

void CTrayDialog::OnTrayMouseMove()
{
	/*POINT pt;
	::GetCursorPos(&pt);
	ClientToScreen(&pt);*/
}

void CTrayDialog::OnTrayBalloonUserClick()
{
	RestoreWindow();
}

LRESULT CTrayDialog::OnTaskBarCreated(WPARAM, LPARAM)
{
	TrayReset();
	return 0;
}

void CTrayDialog::RestoreWindow()
{
	ShowWindow(SW_NORMAL);
	SetForegroundWindow();
}
