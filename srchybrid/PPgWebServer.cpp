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
#include "PPgWebServer.h"
#include "otherfunctions.h"
#include "WebServer.h"
#include "emuledlg.h"
#include "Preferences.h"
#include "ServerWnd.h"
#include "HelpIDs.h"
#include "UPnPImplWrapper.h"
#include "UPnPImpl.h"
#include "Log.h"
#include "WebServerCertificate.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
const CString kDefaultWebBindAddr(_T("0.0.0.0"));

/**
 * Returns the effective WebServer bind address text shown in the options page.
 */
CString GetEffectiveWebBindAddrForUi()
{
	const CString &strStoredBindAddr = thePrefs.GetWebBindAddr();
	return strStoredBindAddr.IsEmpty() ? kDefaultWebBindAddr : strStoredBindAddr;
}

/**
 * Converts the UI bind-address text back to the persisted override semantics.
 */
CString NormalizeWebBindAddrOverrideForPrefs(const CString &strUiBindAddr)
{
	CString strBindAddr(strUiBindAddr);
	strBindAddr.Trim();
	return strBindAddr == kDefaultWebBindAddr ? CString() : strBindAddr;
}

void EnableDlgItem(CWnd *pDialog, UINT uId, bool bEnable)
{
	CWnd *const pWnd = pDialog != NULL ? pDialog->GetDlgItem(uId) : NULL;
	if (pWnd != NULL)
		pWnd->EnableWindow(bEnable);
}
}

IMPLEMENT_DYNAMIC(CPPgWebServer, CPropertyPage)

BEGIN_MESSAGE_MAP(CPPgWebServer, CPropertyPage)
	ON_EN_CHANGE(IDC_WSPASS, OnDataChange)
	ON_EN_CHANGE(IDC_WSAPIKEY, OnDataChange)
	ON_EN_SETFOCUS(IDC_WSAPIKEY, OnEnSetfocusWsApiKey)
	ON_EN_CHANGE(IDC_WSPASSLOW, OnDataChange)
	ON_EN_CHANGE(IDC_WSPORT, OnDataChange)
	ON_EN_CHANGE(IDC_WEBBINDADDR, OnDataChange)
	ON_EN_CHANGE(IDC_WS_MAXFILEUPLOAD, OnDataChange)
	ON_EN_CHANGE(IDC_WS_ALLOWEDIPS, OnDataChange)
	ON_EN_CHANGE(IDC_TMPLPATH, OnDataChange)
	ON_EN_CHANGE(IDC_CERTPATH, OnDataChange)
	ON_EN_CHANGE(IDC_KEYPATH, OnDataChange)
	ON_EN_CHANGE(IDC_WSTIMEOUT, OnDataChange)
	ON_BN_CLICKED(IDC_WSENABLED, OnEnChangeWSEnabled)
	ON_BN_CLICKED(IDC_WSLEGACYUI, OnLegacyWebUiChanged)
	ON_BN_CLICKED(IDC_WEB_HTTPS, OnChangeHTTPS)
	ON_BN_CLICKED(IDC_WEB_GENERATE, OnGenerateCertificate)
	ON_BN_CLICKED(IDC_WSENABLEDLOW, OnEnChangeWSEnabled)
	ON_BN_CLICKED(IDC_WSRELOADTMPL, OnReloadTemplates)
	ON_BN_CLICKED(IDC_TMPLBROWSE, OnBnClickedTmplbrowse)
	ON_BN_CLICKED(IDC_CERTBROWSE, OnBnClickedCertbrowse)
	ON_BN_CLICKED(IDC_KEYBROWSE, OnBnClickedKeybrowse)
	ON_BN_CLICKED(IDC_WS_GZIP, OnDataChange)
	ON_BN_CLICKED(IDC_WS_ALLOWHILEVFUNC, OnDataChange)
	ON_BN_CLICKED(IDC_WSUPNP, OnDataChange)
	ON_WM_HELPINFO()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

CPPgWebServer::CPPgWebServer()
	: CPropertyPage(CPPgWebServer::IDD)
	, m_generating()
	, m_bNewCert()
	, m_bModified()
	, m_icoBrowse()
{
}

bool CPPgWebServer::IsValidBindAddressOverride(const CString &strAddr)
{
	if (strAddr.IsEmpty())
		return true;
	const CStringA strAddrA(strAddr);
	const unsigned long ulAddr = inet_addr(strAddrA);
	return ulAddr != INADDR_NONE || strAddr == _T("255.255.255.255");
}

void CPPgWebServer::DoDataExchange(CDataExchange *pDX)
{
	CPropertyPage::DoDataExchange(pDX);
}

BOOL CPPgWebServer::OnInitDialog()
{
	CPropertyPage::OnInitDialog();
	InitWindowStyles(this);

	AddBuddyButton(GetDlgItem(IDC_TMPLPATH)->m_hWnd, ::GetDlgItem(m_hWnd, IDC_TMPLBROWSE));
	InitAttachedBrowseButton(::GetDlgItem(m_hWnd, IDC_TMPLBROWSE), m_icoBrowse);

	AddBuddyButton(GetDlgItem(IDC_CERTPATH)->m_hWnd, ::GetDlgItem(m_hWnd, IDC_CERTBROWSE));
	InitAttachedBrowseButton(::GetDlgItem(m_hWnd, IDC_CERTBROWSE), m_icoBrowse);

	AddBuddyButton(GetDlgItem(IDC_KEYPATH)->m_hWnd, ::GetDlgItem(m_hWnd, IDC_KEYBROWSE));
	InitAttachedBrowseButton(::GetDlgItem(m_hWnd, IDC_KEYBROWSE), m_icoBrowse);

	static_cast<CEdit*>(GetDlgItem(IDC_WSPASS))->SetLimitText(32);
	static_cast<CEdit*>(GetDlgItem(IDC_WSPASSLOW))->SetLimitText(32);
	static_cast<CEdit*>(GetDlgItem(IDC_WSAPIKEY))->SetLimitText(64);
	static_cast<CEdit*>(GetDlgItem(IDC_WSPORT))->SetLimitText(5);
	static_cast<CEdit*>(GetDlgItem(IDC_WEBBINDADDR))->SetLimitText(15);
	static_cast<CEdit*>(GetDlgItem(IDC_WS_MAXFILEUPLOAD))->SetLimitText(5);
	static_cast<CEdit*>(GetDlgItem(IDC_WS_ALLOWEDIPS))->SetLimitText(512);

	LoadSettings();
	Localize();
	UpdateToolTips();

	OnEnChangeWSEnabled();

	return TRUE;
}

void CPPgWebServer::UpdateToolTips()
{
	if (!m_toolTip.Init(this))
		return;

	m_toolTip.SetTool(this, IDC_WSENABLED, GetResString(IDS_WEBSERVER_TT_WSENABLED));
	m_toolTip.SetTool(this, IDC_WSLEGACYUI, GetResString(IDS_WEBSERVER_TT_WSLEGACYUI));
	m_toolTip.SetTool(this, IDC_WS_GZIP, GetResString(IDS_WEBSERVER_TT_WS_GZIP));
	m_toolTip.SetTool(this, IDC_WEBBINDADDR, GetResString(IDS_WEBSERVER_TT_WEBBINDADDR));
	m_toolTip.SetTool(this, IDC_WSPORT, GetResString(IDS_WEBSERVER_TT_WSPORT));
	m_toolTip.SetTool(this, IDC_TMPLPATH, GetResString(IDS_WEBSERVER_TT_TMPLPATH));
	m_toolTip.SetTool(this, IDC_TMPLBROWSE, GetResString(IDS_WEBSERVER_TT_TMPLBROWSE));
	m_toolTip.SetTool(this, IDC_WSRELOADTMPL, GetResString(IDS_WEBSERVER_TT_WSRELOADTMPL));
	m_toolTip.SetTool(this, IDC_WSTIMEOUT, GetResString(IDS_WEBSERVER_TT_WSTIMEOUT));
	m_toolTip.SetTool(this, IDC_WSAPIKEY, GetResString(IDS_WEBSERVER_TT_WSAPIKEY));
	m_toolTip.SetTool(this, IDC_WSUPNP, GetResString(IDS_WEBSERVER_TT_WSUPNP));
	m_toolTip.SetTool(this, IDC_WS_MAXFILEUPLOAD, GetResString(IDS_WEBSERVER_TT_WS_MAXFILEUPLOAD));
	m_toolTip.SetTool(this, IDC_WS_MAXFILEUPLOAD_LBL, GetResString(IDS_WEBSERVER_TT_WS_MAXFILEUPLOAD_LBL));
	m_toolTip.SetTool(this, IDC_WS_ALLOWEDIPS, GetResString(IDS_WEBSERVER_TT_WS_ALLOWEDIPS));
	m_toolTip.SetTool(this, IDC_WS_ALLOWEDIPS_LBL, GetResString(IDS_WEBSERVER_TT_WS_ALLOWEDIPS_LBL));
	m_toolTip.SetTool(this, IDC_WS_ALLOWHILEVFUNC, GetResString(IDS_WEBSERVER_TT_WS_ALLOWHILEVFUNC));
	m_toolTip.SetTool(this, IDC_WEB_HTTPS, GetResString(IDS_WEBSERVER_TT_WEB_HTTPS));
	m_toolTip.SetTool(this, IDC_WEB_GENERATE, GetResString(IDS_WEBSERVER_TT_WEB_GENERATE));
	m_toolTip.SetTool(this, IDC_CERTPATH, GetResString(IDS_WEBSERVER_TT_CERTPATH));
	m_toolTip.SetTool(this, IDC_CERTBROWSE, GetResString(IDS_WEBSERVER_TT_CERTBROWSE));
	m_toolTip.SetTool(this, IDC_KEYPATH, GetResString(IDS_WEBSERVER_TT_KEYPATH));
	m_toolTip.SetTool(this, IDC_KEYBROWSE, GetResString(IDS_WEBSERVER_TT_KEYBROWSE));
	m_toolTip.SetTool(this, IDC_WSPASS, GetResString(IDS_WEBSERVER_TT_WSPASS));
	m_toolTip.SetTool(this, IDC_WSENABLEDLOW, GetResString(IDS_WEBSERVER_TT_WSENABLEDLOW));
	m_toolTip.SetTool(this, IDC_WSPASSLOW, GetResString(IDS_WEBSERVER_TT_WSPASSLOW));
}

void CPPgWebServer::LoadSettings()
{
	CheckDlgButton(IDC_WSENABLED, static_cast<UINT>(thePrefs.GetWSIsEnabled()));
	CheckDlgButton(IDC_WSLEGACYUI, static_cast<UINT>(thePrefs.GetLegacyWebUiEnabled()));
	CheckDlgButton(IDC_WS_GZIP, static_cast<UINT>(thePrefs.GetWebUseGzip()));

	CheckDlgButton(IDC_WSUPNP, static_cast<UINT>(thePrefs.m_bWebUseUPnP));
	GetDlgItem(IDC_WSUPNP)->EnableWindow(thePrefs.IsUPnPEnabled() && thePrefs.GetWSIsEnabled());
	SetDlgItemInt(IDC_WSPORT, thePrefs.GetWSPort());
	SetDlgItemText(IDC_WEBBINDADDR, GetEffectiveWebBindAddrForUi());
	SetDlgItemInt(IDC_WS_MAXFILEUPLOAD, thePrefs.GetMaxWebUploadFileSizeMB());
	SetDlgItemText(IDC_WS_ALLOWEDIPS, CPreferences::GetAllowedRemoteAccessIPsString());

	SetDlgItemText(IDC_TMPLPATH, thePrefs.GetTemplate());
	SetDlgItemInt(IDC_WSTIMEOUT, thePrefs.GetWebTimeoutMins());

	CheckDlgButton(IDC_WEB_HTTPS, static_cast<UINT>(thePrefs.GetWebUseHttps()));
	SetDlgItemText(IDC_CERTPATH, thePrefs.GetWebCertPath());
	SetDlgItemText(IDC_KEYPATH, thePrefs.GetWebKeyPath());

	SetDlgItemText(IDC_WSPASS, sHiddenPassword);
	SetDlgItemText(IDC_WSAPIKEY, thePrefs.GetWSApiKey());
	CheckDlgButton(IDC_WS_ALLOWHILEVFUNC, static_cast<UINT>(thePrefs.GetWebAdminAllowedHiLevFunc()));
	CheckDlgButton(IDC_WSENABLEDLOW, static_cast<UINT>(thePrefs.GetWSIsLowUserEnabled()));
	SetDlgItemText(IDC_WSPASSLOW, sHiddenPassword);

	SetModified(FALSE);	// FoRcHa
}

void CPPgWebServer::OnDataChange()
{
	SetModified();
	SetTmplButtonState();
}

BOOL CPPgWebServer::OnApply()
{
	if (m_bModified) {
		bool bUPnP = thePrefs.GetWSUseUPnP();
		bool bWSIsEnabled = IsDlgButtonChecked(IDC_WSENABLED) != 0;
		bool bLegacyWebUiEnabled = IsDlgButtonChecked(IDC_WSLEGACYUI) != 0;
		bool bRestartWebServerSockets = false;
		CString sBuf;
		GetDlgItemText(IDC_TMPLPATH, sBuf);
		thePrefs.SetTemplate(sBuf);

		bool bHTTPS = IsDlgButtonChecked(IDC_WEB_HTTPS) != 0;
		GetDlgItemText(IDC_CERTPATH, sBuf);
		if (bWSIsEnabled && bHTTPS) {
			if (!LongPathSeams::PathExists(sBuf)) {
				AfxMessageBox(GetResString(IDS_CERT_NOT_FOUND), MB_OK);
				return FALSE;
			}
			if (!m_bNewCert)
				m_bNewCert = EqualPaths(thePrefs.GetWebCertPath(), sBuf);
		}
		thePrefs.SetWebCertPath(sBuf);

		GetDlgItemText(IDC_KEYPATH, sBuf);
		if (bWSIsEnabled && bHTTPS) {
			if (!LongPathSeams::PathExists(sBuf)) {
				AfxMessageBox(GetResString(IDS_KEY_NOT_FOUND), MB_OK);
				return FALSE;
			}
			if (!m_bNewCert)
				m_bNewCert = EqualPaths(thePrefs.GetWebKeyPath(), sBuf);
		}
		thePrefs.SetWebKeyPath(sBuf);

		GetDlgItemText(IDC_WSPASS, sBuf);
		if (sBuf != sHiddenPassword) {
			thePrefs.SetWSPass(sBuf);
			SetDlgItemText(IDC_WSPASS, sHiddenPassword);
		}

		GetDlgItemText(IDC_WSAPIKEY, sBuf);
		sBuf.Trim();
		if (sBuf != thePrefs.GetWSApiKey()) {
			thePrefs.SetWSApiKey(sBuf);
			SetDlgItemText(IDC_WSAPIKEY, thePrefs.GetWSApiKey());
		}

		GetDlgItemText(IDC_WSPASSLOW, sBuf);
		if (sBuf != sHiddenPassword) {
			thePrefs.SetWSLowPass(sBuf);
			SetDlgItemText(IDC_WSPASSLOW, sHiddenPassword);
		}

		thePrefs.SetWebTimeoutMins(GetDlgItemInt(IDC_WSTIMEOUT, NULL, FALSE));

		uint16 u = CPreferences::NormalizePortValue(GetDlgItemInt(IDC_WSPORT, NULL, FALSE), CPreferences::GetDefaultWSPort());
		if (u != thePrefs.GetWSPort()) {
			thePrefs.SetWSPort(u);
			bRestartWebServerSockets = true;
		}

		CString strWebBindAddr;
		GetDlgItemText(IDC_WEBBINDADDR, strWebBindAddr);
		strWebBindAddr.Trim();
		if (!IsValidBindAddressOverride(strWebBindAddr)) {
			AfxMessageBox(GetResString(IDS_WEB_INVALID_BINDADDR), MB_OK | MB_ICONWARNING);
			GetDlgItem(IDC_WEBBINDADDR)->SetFocus();
			return FALSE;
		}
		strWebBindAddr = NormalizeWebBindAddrOverrideForPrefs(strWebBindAddr);
		if (strWebBindAddr != thePrefs.GetWebBindAddr()) {
			thePrefs.SetWebBindAddr(strWebBindAddr);
			bRestartWebServerSockets = true;
		}

		BOOL bTranslated = FALSE;
		const uint32 uMaxUploadMB = CPreferences::NormalizeMaxWebUploadFileSizeMB(GetDlgItemInt(IDC_WS_MAXFILEUPLOAD, &bTranslated, FALSE));
		if (!bTranslated) {
			AfxMessageBox(GetResString(IDS_WEB_INVALID_UPLOAD_SIZE_MIB), MB_OK | MB_ICONWARNING);
			GetDlgItem(IDC_WS_MAXFILEUPLOAD)->SetFocus();
			return FALSE;
		}
		thePrefs.SetMaxWebUploadFileSizeMB(uMaxUploadMB);
		SetDlgItemInt(IDC_WS_MAXFILEUPLOAD, thePrefs.GetMaxWebUploadFileSizeMB());

		CString strAllowedIPs;
		GetDlgItemText(IDC_WS_ALLOWEDIPS, strAllowedIPs);
		CString strInvalidIP;
		if (!CPreferences::SetAllowedRemoteAccessIPsString(strAllowedIPs, strInvalidIP)) {
			CString strMessage;
			strMessage.Format(GetResString(IDS_WEB_INVALID_ALLOWED_IP_FMT), (LPCTSTR)strInvalidIP);
			AfxMessageBox(strMessage, MB_OK | MB_ICONWARNING);
			GetDlgItem(IDC_WS_ALLOWEDIPS)->SetFocus();
			return FALSE;
		}
		SetDlgItemText(IDC_WS_ALLOWEDIPS, CPreferences::GetAllowedRemoteAccessIPsString());

		if (thePrefs.GetWebUseHttps() != bHTTPS || (bHTTPS && m_bNewCert))
			theApp.webserver->StopServer();
		m_bNewCert = false;

		thePrefs.SetWSIsEnabled(bWSIsEnabled);
		thePrefs.SetLegacyWebUiEnabled(bLegacyWebUiEnabled);
		thePrefs.SetWebUseGzip(bLegacyWebUiEnabled && IsDlgButtonChecked(IDC_WS_GZIP) != 0);
		thePrefs.SetWebUseHttps(bHTTPS);
		thePrefs.SetWSIsLowUserEnabled(IsDlgButtonChecked(IDC_WSENABLEDLOW) != 0);
		if (bWSIsEnabled && bLegacyWebUiEnabled)
			theApp.webserver->ReloadTemplates();
		if (bRestartWebServerSockets && bWSIsEnabled)
			theApp.webserver->RestartSockets();
		theApp.webserver->StartServer();
		thePrefs.m_bAllowAdminHiLevFunc = IsDlgButtonChecked(IDC_WS_ALLOWHILEVFUNC) != 0;

		thePrefs.m_bWebUseUPnP = IsDlgButtonChecked(IDC_WSUPNP) != 0;
		//add the port to existing mapping without having eMule restarting (if all conditions are met)
		if (bUPnP != (thePrefs.m_bWebUseUPnP && bWSIsEnabled) && thePrefs.IsUPnPEnabled() && theApp.m_pUPnPFinder != NULL)
			theApp.m_pUPnPFinder->GetImplementation()->LateEnableWebServerPort(bUPnP ? 0 : thePrefs.GetWSPort());

		theApp.emuledlg->serverwnd->UpdateMyInfo();
		SetModified(FALSE);
		SetTmplButtonState();
	}

	return CPropertyPage::OnApply();
}

void CPPgWebServer::Localize()
{
	if (m_hWnd) {
		SetWindowText(GetResString(IDS_PW_WS));

		SetDlgItemText(IDC_WSENABLED, GetResString(IDS_WS_ENABLE_REST_API));
		SetDlgItemText(IDC_WSLEGACYUI, GetResString(IDS_WS_ENABLE_LEGACY_WEBUI));
		SetDlgItemText(IDC_WS_GZIP, GetResString(IDS_WEB_GZIP_COMPRESSION));
		SetDlgItemText(IDC_WSUPNP, GetResString(IDS_WEBUPNPINCLUDE));
		SetDlgItemText(IDC_WSPORT_LBL, GetResString(IDS_PORT) + _T(':'));
		SetDlgItemText(IDC_WEBBINDADDR_LBL, GetResString(IDS_WEB_BIND_ADDR) + _T(':'));
		SetDlgItemText(IDC_WS_MAXFILEUPLOAD_LBL, GetResString(IDS_WEB_MAX_UPLOAD_MIB_LABEL));
		SetDlgItemText(IDC_WS_ALLOWEDIPS_LBL, GetResString(IDS_WEB_ALLOWED_IPS_LABEL));

		SetDlgItemText(IDC_TEMPLATE, GetResString(IDS_WS_RELOAD_TMPL) + _T(':'));
		SetDlgItemText(IDC_WSRELOADTMPL, GetResString(IDS_SF_RELOAD));

		SetDlgItemText(IDC_STATIC_GENERAL, GetResString(IDS_PW_GENERAL));

		SetDlgItemText(IDC_WSTIMEOUTLABEL, GetResString(IDS_WEB_SESSIONTIMEOUT) + _T(':'));
		SetDlgItemText(IDC_MINS, GetResString(IDS_LONGMINS).MakeLower());

		SetDlgItemText(IDC_WEB_HTTPS, GetResString(IDS_WEB_HTTPS));
		SetDlgItemText(IDC_STATIC_HTTPS, GetResString(IDS_WEB_HTTPS));
		SetDlgItemText(IDC_WEB_GENERATE, GetResString(IDS_WEB_GENERATE));
		SetDlgItemText(IDC_WEB_CERT, GetResString(IDS_CERTIFICATE) + _T(':'));
		SetDlgItemText(IDC_WEB_KEY, GetResString(IDS_KEY) + _T(':'));

		SetDlgItemText(IDC_STATIC_ADMIN, GetResString(IDS_ADMIN));
		SetDlgItemText(IDC_WSPASS_LBL, GetResString(IDS_WS_PASS) + _T(':'));
		SetDlgItemText(IDC_WSAPIKEY_LBL, GetResString(IDS_WS_APIKEY) + _T(':'));
		SetDlgItemText(IDC_WS_ALLOWHILEVFUNC, GetResString(IDS_WEB_ALLOWHILEVFUNC));

		SetDlgItemText(IDC_STATIC_LOWUSER, GetResString(IDS_WEB_LOWUSER));
		SetDlgItemText(IDC_WSENABLEDLOW, GetResString(IDS_ENABLED));
		SetDlgItemText(IDC_WSPASS_LBL2, GetResString(IDS_WS_PASS) + _T(':'));
		SetDlgItemText(IDC_STATIC_LEGACY_WEBUI, GetResString(IDS_WS_ENABLE_LEGACY_WEBUI));
	}
}

void CPPgWebServer::SetUPnPState()
{
	EnableDlgItem(this, IDC_WSUPNP, thePrefs.IsUPnPEnabled() && IsDlgButtonChecked(IDC_WSENABLED));
}

void CPPgWebServer::UpdateLegacyWebUiControls()
{
	const bool bServerEnabled = IsDlgButtonChecked(IDC_WSENABLED) != 0;
	const bool bLegacyWebUiEnabled = bServerEnabled && IsDlgButtonChecked(IDC_WSLEGACYUI) != 0;
	const bool bHttpsEnabled = bServerEnabled && IsDlgButtonChecked(IDC_WEB_HTTPS) != 0;

	EnableDlgItem(this, IDC_STATIC_LEGACY_WEBUI, bServerEnabled);
	EnableDlgItem(this, IDC_WS_GZIP, bLegacyWebUiEnabled && !bHttpsEnabled);
	EnableDlgItem(this, IDC_WS_MAXFILEUPLOAD_LBL, bLegacyWebUiEnabled);
	EnableDlgItem(this, IDC_WS_MAXFILEUPLOAD, bLegacyWebUiEnabled);
	EnableDlgItem(this, IDC_WSTIMEOUTLABEL, bLegacyWebUiEnabled);
	EnableDlgItem(this, IDC_WSTIMEOUT, bLegacyWebUiEnabled);
	EnableDlgItem(this, IDC_MINS, bLegacyWebUiEnabled);
	EnableDlgItem(this, IDC_TEMPLATE, bLegacyWebUiEnabled);
	EnableDlgItem(this, IDC_TMPLPATH, bLegacyWebUiEnabled);
	EnableDlgItem(this, IDC_TMPLBROWSE, bLegacyWebUiEnabled);
	SetTmplButtonState();
}

void CPPgWebServer::OnChangeHTTPS()
{
	const bool bServerEnabled = IsDlgButtonChecked(IDC_WSENABLED) != 0;
	const bool bEnable = bServerEnabled && IsDlgButtonChecked(IDC_WEB_HTTPS) != 0;
	if (bEnable && IsDlgButtonChecked(IDC_WS_GZIP))
		CheckDlgButton(IDC_WS_GZIP, BST_UNCHECKED);

	EnableDlgItem(this, IDC_STATIC_HTTPS, bServerEnabled);
	EnableDlgItem(this, IDC_WEB_GENERATE, bEnable && !m_generating);
	EnableDlgItem(this, IDC_WEB_CERT, bEnable);
	EnableDlgItem(this, IDC_CERTPATH, bEnable);
	EnableDlgItem(this, IDC_CERTBROWSE, bEnable);
	EnableDlgItem(this, IDC_WEB_KEY, bEnable);
	EnableDlgItem(this, IDC_KEYPATH, bEnable);
	EnableDlgItem(this, IDC_KEYBROWSE, bEnable);
	UpdateLegacyWebUiControls();
	SetModified();
}

void CPPgWebServer::OnEnChangeWSEnabled()
{
	const bool bIsWIEnabled = IsDlgButtonChecked(IDC_WSENABLED) != 0;
	const bool bLowUserEnabled = bIsWIEnabled && IsDlgButtonChecked(IDC_WSENABLEDLOW) != 0;

	EnableDlgItem(this, IDC_WSPORT_LBL, bIsWIEnabled);
	EnableDlgItem(this, IDC_WSPORT, bIsWIEnabled);
	EnableDlgItem(this, IDC_WEBBINDADDR_LBL, bIsWIEnabled);
	EnableDlgItem(this, IDC_WEBBINDADDR, bIsWIEnabled);
	EnableDlgItem(this, IDC_WSAPIKEY_LBL, bIsWIEnabled);
	EnableDlgItem(this, IDC_WSAPIKEY, bIsWIEnabled);
	EnableDlgItem(this, IDC_WS_ALLOWEDIPS_LBL, bIsWIEnabled);
	EnableDlgItem(this, IDC_WS_ALLOWEDIPS, bIsWIEnabled);
	EnableDlgItem(this, IDC_STATIC_ADMIN, bIsWIEnabled);
	EnableDlgItem(this, IDC_WSPASS_LBL, bIsWIEnabled);
	EnableDlgItem(this, IDC_WSPASS, bIsWIEnabled);
	EnableDlgItem(this, IDC_WS_ALLOWHILEVFUNC, bIsWIEnabled);
	EnableDlgItem(this, IDC_STATIC_LOWUSER, bIsWIEnabled);
	EnableDlgItem(this, IDC_WSENABLEDLOW, bIsWIEnabled);
	EnableDlgItem(this, IDC_WSPASS_LBL2, bLowUserEnabled);
	EnableDlgItem(this, IDC_WSPASSLOW, bLowUserEnabled);
	EnableDlgItem(this, IDC_WSLEGACYUI, bIsWIEnabled);
	EnableDlgItem(this, IDC_WEB_HTTPS, bIsWIEnabled);
	SetUPnPState();
	UpdateLegacyWebUiControls();
	OnChangeHTTPS();

	SetModified();
}

void CPPgWebServer::OnLegacyWebUiChanged()
{
	UpdateLegacyWebUiControls();
	SetModified();
}

void CPPgWebServer::OnReloadTemplates()
{
	theApp.webserver->ReloadTemplates();
}

void CPPgWebServer::OnBnClickedTmplbrowse()
{
	CString strTempl;
	GetDlgItemText(IDC_TMPLPATH, strTempl);
	CString buffer(GetResString(IDS_WS_RELOAD_TMPL) + _T(" (*.tmpl)|*.tmpl||"));
	if (DialogBrowseFile(buffer, buffer, strTempl)) {
		SetDlgItemText(IDC_TMPLPATH, buffer);
		SetModified();
	}
	SetTmplButtonState();
}

//create cert.key and cert.crt in config directory
void CPPgWebServer::OnGenerateCertificate()
{
	if (::InterlockedExchange(&m_generating, 1))
		return;
	CWaitCursor curWaiting;

	const CString &confdir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	const CString &fkey(confdir + _T("cert.key"));
	const CString &fcrt(confdir + _T("cert.crt"));
	const WebServerCertificate::SGenerationRequest request = WebServerCertificate::BuildDefaultLocalRequest(fkey, fcrt);
	m_bNewCert = !WebServerCertificate::CreateSelfSignedCertificate(request);
	if (m_bNewCert) {
		AddLogLine(false, _T("New certificate created; serial %d"), request.uSerial);
		SetDlgItemText(IDC_KEYPATH, fkey);
		SetDlgItemText(IDC_CERTPATH, fcrt);
		GetDlgItem(IDC_WEB_GENERATE)->EnableWindow(FALSE);
		SetModified();
	} else {
		LogError(_T("Certificate creation failed"));
		AfxMessageBox(GetResString(IDS_CERT_ERR_CREATE));
		::InterlockedExchange(&m_generating, 0); //re-enable only if failed
	}
}

void CPPgWebServer::OnBnClickedCertbrowse()
{
	CString strCert;
	GetDlgItemText(IDC_CERTPATH, strCert);
	CString buffer(GetResString(IDS_CERTIFICATE));
	buffer += _T(" (*.crt)|*.crt|All Files (*.*)|*.*||");
	if (DialogBrowseFile(buffer, buffer, strCert))
		SetDlgItemText(IDC_CERTPATH, buffer);
	if (!EqualPaths(buffer, strCert))
		SetModified();
}

void CPPgWebServer::OnBnClickedKeybrowse()
{
	CString strKey;
	GetDlgItemText(IDC_KEYPATH, strKey);
	CString buffer(GetResString(IDS_KEY));
	buffer += _T(" (*.key)|*.key|All Files (*.*)|*.*||");
	if (DialogBrowseFile(buffer, buffer, strKey))
		SetDlgItemText(IDC_KEYPATH, buffer);
	if (!EqualPaths(buffer, strKey))
		SetModified();
}

void CPPgWebServer::SetTmplButtonState()
{
	CString buffer;
	GetDlgItemText(IDC_TMPLPATH, buffer);

	EnableDlgItem(this, IDC_WSRELOADTMPL, IsDlgButtonChecked(IDC_WSENABLED) && IsDlgButtonChecked(IDC_WSLEGACYUI) && EqualPaths(buffer, thePrefs.GetTemplate()));
}

void CPPgWebServer::OnHelp()
{
	theApp.ShowHelp(eMule_FAQ_Preferences_WebInterface);
}

BOOL CPPgWebServer::OnCommand(WPARAM wParam, LPARAM lParam)
{
	return (wParam == ID_HELP) ? OnHelpInfo(NULL) : __super::OnCommand(wParam, lParam);
}

BOOL CPPgWebServer::OnHelpInfo(HELPINFO*)
{
	OnHelp();
	return TRUE;
}

BOOL CPPgWebServer::PreTranslateMessage(MSG *pMsg)
{
	m_toolTip.RelayEvent(pMsg);
	return CPropertyPage::PreTranslateMessage(pMsg);
}

/**
 * Copies the visible REST API key as soon as the user focuses the textbox.
 */
void CPPgWebServer::OnEnSetfocusWsApiKey()
{
	CEdit *const pApiKeyEdit = static_cast<CEdit*>(GetDlgItem(IDC_WSAPIKEY));
	if (pApiKeyEdit == NULL)
		return;

	CString strApiKey;
	pApiKeyEdit->GetWindowText(strApiKey);
	if (strApiKey.IsEmpty())
		return;

	pApiKeyEdit->SetSel(0, -1);
	theApp.CopyTextToClipboard(strApiKey);
}

void CPPgWebServer::OnDestroy()
{
	CPropertyPage::OnDestroy();
	if (m_icoBrowse) {
		VERIFY(::DestroyIcon(m_icoBrowse));
		m_icoBrowse = NULL;
	}
}
