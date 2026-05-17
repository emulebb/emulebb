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
#include "PPGProxy.h"
#include "opcodes.h"
#include "OtherFunctions.h"
#include "HelpIDs.h"

// FROZEN SURFACE (2026-05-17): proxy preferences receive no support, no new
// tests, and no hardening/refactor investment. They may be removed with proxy
// support. Only touch this area to delete it or to protect supported shared
// infrastructure. See repos/eMule-tooling/docs/active/FROZEN-SURFACES.md.

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


IMPLEMENT_DYNAMIC(CPPgProxy, CPropertyPage)

BEGIN_MESSAGE_MAP(CPPgProxy, CPropertyPage)
	ON_BN_CLICKED(IDC_ENABLEPROXY, OnBnClickedEnableProxy)
	ON_BN_CLICKED(IDC_ENABLEAUTH, OnBnClickedEnableAuthentication)
	ON_CBN_SELCHANGE(IDC_PROXYTYPE, OnCbnSelChangeProxyType)
	ON_EN_CHANGE(IDC_PROXYNAME, OnEnChangeProxyName)
	ON_EN_CHANGE(IDC_PROXYPORT, OnEnChangeProxyPort)
	ON_EN_CHANGE(IDC_USERNAME_A, OnEnChangeUserName)
	ON_EN_CHANGE(IDC_PASSWORD, OnEnChangePassword)
	ON_WM_HELPINFO()
END_MESSAGE_MAP()

CPPgProxy::CPPgProxy()
	: CPropertyPage(CPPgProxy::IDD)
	, proxy(thePrefs.GetProxySettings())
{
}

void CPPgProxy::DoDataExchange(CDataExchange *pDX)
{
	CPropertyPage::DoDataExchange(pDX);
}

BOOL CPPgProxy::OnInitDialog()
{
	CPropertyPage::OnInitDialog();
	InitWindowStyles(this);

	static_cast<CEdit*>(GetDlgItem(IDC_PROXYPORT))->SetLimitText(5);

	LoadSettings();
	Localize();
	UpdateToolTips();

	return TRUE;  // return TRUE unless you set the focus to the control
				  // EXCEPTION: OCX Property Pages should return FALSE
}

void CPPgProxy::UpdateToolTips()
{
	if (!m_toolTip.Init(this))
		return;

	m_toolTip.SetTool(this, IDC_ENABLEPROXY, GetResString(IDS_PPG_PROXY_TT_ENABLEPROXY));
	m_toolTip.SetTool(this, IDC_PROXYTYPE, GetResString(IDS_PPG_PROXY_TT_PROXYTYPE));
	m_toolTip.SetTool(this, IDC_PROXYNAME, GetResString(IDS_PPG_PROXY_TT_PROXYNAME));
	m_toolTip.SetTool(this, IDC_PROXYPORT, GetResString(IDS_PPG_PROXY_TT_PROXYPORT));
	m_toolTip.SetTool(this, IDC_ENABLEAUTH, GetResString(IDS_PPG_PROXY_TT_ENABLEAUTH));
	m_toolTip.SetTool(this, IDC_USERNAME_A, GetResString(IDS_PPG_PROXY_TT_USERNAME_A));
	m_toolTip.SetTool(this, IDC_PASSWORD, GetResString(IDS_PPG_PROXY_TT_PASSWORD));
}

BOOL CPPgProxy::OnApply()
{
	proxy.type = (uint16)static_cast<CComboBox*>(GetDlgItem(IDC_PROXYTYPE))->GetCurSel();

	CString str;
	if (GetDlgItemText(IDC_PROXYNAME, str)) {
		proxy.bUseProxy = IsDlgButtonChecked(IDC_ENABLEPROXY) != 0;
		int iColon = str.Find(':');
		if (iColon >= 0) {
			SetDlgItemText(IDC_PROXYPORT, CPTR(str, iColon + 1));
			str.Truncate(iColon);
		}
	} else
		proxy.bUseProxy = false;
	proxy.host = str;

	proxy.port = CPreferences::NormalizePortValue(GetDlgItemInt(IDC_PROXYPORT, NULL, FALSE), 1080);

	if (GetDlgItemText(IDC_USERNAME_A, str)) {
		proxy.user = str;
		proxy.bEnablePassword = IsDlgButtonChecked(IDC_ENABLEAUTH) != 0;
	} else {
		proxy.user.Empty();
		proxy.bEnablePassword = false;
	}

	if (GetDlgItemText(IDC_PASSWORD, str))
		proxy.password = str;
	else {
		proxy.password.Empty();
		proxy.bEnablePassword = false;
	}

	thePrefs.SetProxySettings(proxy);
	LoadSettings();
	return TRUE;
}

void CPPgProxy::OnBnClickedEnableProxy()
{
	SetModified(TRUE);

	bool bEnable = IsDlgButtonChecked(IDC_ENABLEPROXY) != 0;
	GetDlgItem(IDC_PROXYTYPE)->EnableWindow(bEnable);
	GetDlgItem(IDC_PROXYNAME)->EnableWindow(bEnable);
	GetDlgItem(IDC_PROXYPORT)->EnableWindow(bEnable);

	OnCbnSelChangeProxyType();
}

void CPPgProxy::OnBnClickedEnableAuthentication()
{
	SetModified(TRUE);
	bool bEnable = IsDlgButtonChecked(IDC_ENABLEAUTH) != 0;
	GetDlgItem(IDC_USERNAME_A)->EnableWindow(bEnable);
	GetDlgItem(IDC_PASSWORD)->EnableWindow(bEnable);
}

void CPPgProxy::OnCbnSelChangeProxyType()
{
	SetModified(TRUE);

	BOOL bEnable;
	switch (static_cast<CComboBox*>(GetDlgItem(IDC_PROXYTYPE))->GetCurSel()) {
	case PROXYTYPE_SOCKS5:
	case PROXYTYPE_HTTP10:
	case PROXYTYPE_HTTP11:
		bEnable = TRUE;
		break;
	default:
		CheckDlgButton(IDC_ENABLEAUTH, BST_UNCHECKED);
		bEnable = FALSE;
	}
	GetDlgItem(IDC_ENABLEAUTH)->EnableWindow(bEnable);
	OnBnClickedEnableAuthentication();
}

void CPPgProxy::LoadSettings()
{
	CheckDlgButton(IDC_ENABLEPROXY, proxy.bUseProxy ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_ENABLEAUTH, proxy.bEnablePassword ? BST_CHECKED : BST_UNCHECKED);
	static_cast<CComboBox*>(GetDlgItem(IDC_PROXYTYPE))->SetCurSel(proxy.type);
	SetDlgItemText(IDC_PROXYNAME, proxy.host);
	SetDlgItemInt(IDC_PROXYPORT, proxy.port);
	SetDlgItemText(IDC_USERNAME_A, proxy.user);
	SetDlgItemText(IDC_PASSWORD, proxy.password);
	OnBnClickedEnableProxy();
}

void CPPgProxy::Localize()
{
	if (m_hWnd) {
		SetWindowText(GetResString(IDS_PW_PROXY));
		SetDlgItemText(IDC_ENABLEPROXY, GetResString(IDS_PROXY_ENABLE));
		SetDlgItemText(IDC_PROXYTYPE_LBL, GetResString(IDS_PROXY_TYPE));
		SetDlgItemText(IDC_PROXYNAME_LBL, GetResString(IDS_PROXY_HOST));
		SetDlgItemText(IDC_PROXYPORT_LBL, GetResString(IDS_PROXY_PORT));
		SetDlgItemText(IDC_ENABLEAUTH, GetResString(IDS_PROXY_AUTH));
		SetDlgItemText(IDC_USERNAME_LBL, GetResString(IDS_CD_UNAME));
		SetDlgItemText(IDC_PASSWORD_LBL, GetResString(IDS_WS_PASS) + _T(':'));
		SetDlgItemText(IDC_AUTH_LBL, GetResString(IDS_AUTH));
		SetDlgItemText(IDC_AUTH_LBL2, GetResString(IDS_PW_GENERAL));
	}
}

void CPPgProxy::OnHelp()
{
	theApp.ShowHelp(eMule_FAQ_Preferences_Proxy);
}

BOOL CPPgProxy::OnCommand(WPARAM wParam, LPARAM lParam)
{
	return (wParam == ID_HELP) ? OnHelpInfo(NULL) : __super::OnCommand(wParam, lParam);
}

BOOL CPPgProxy::OnHelpInfo(HELPINFO*)
{
	OnHelp();
	return TRUE;
}

BOOL CPPgProxy::PreTranslateMessage(MSG *pMsg)
{
	m_toolTip.RelayEvent(pMsg);
	return CPropertyPage::PreTranslateMessage(pMsg);
}
