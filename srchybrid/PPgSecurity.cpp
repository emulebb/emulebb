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
#include <share.h>
#include "emule.h"
#include "PPgSecurity.h"
#include "OtherFunctions.h"
#include "IPFilter.h"
#include "IPFilterUpdater.h"
#include "Preferences.h"
#include "CustomAutoComplete.h"
#include "emuledlg.h"
#include "HelpIDs.h"
#include "ServerWnd.h"
#include "ServerListCtrl.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define	IPFILTERUPDATEURL_STRINGS_PROFILE	_T("AC_IPFilterUpdateURLs.dat")
#define UM_RESTORE_UPDATEURL				(WM_APP + 0x3D1)

static const LPCTSTR s_apszDefaultIPFilterUpdateUrls[] = {
	_T("https://upd.emule-security.org/ipfilter.zip"),
	_T("https://emuling.gitlab.io/ipfilter.zip"),
	_T("https://github.com/DavidMoore/ipfilter/releases/download/lists/ipfilter.zip"),
	_T("https://raw.githubusercontent.com/Naunter/BT_BlockLists/master/bt_blocklists.gz")
};

IMPLEMENT_DYNAMIC(CPPgSecurity, CPropertyPage)

BEGIN_MESSAGE_MAP(CPPgSecurity, CPropertyPage)
	ON_BN_CLICKED(IDC_ENABLE_IPFILTER, OnBnClickedIPFilterEnabled)
	ON_BN_CLICKED(IDC_FILTERSERVERBYIPFILTER, OnSettingsChange)
	ON_BN_CLICKED(IDC_RELOADFILTER, OnReloadIPFilter)
	ON_BN_CLICKED(IDC_EDITFILTER, OnEditIPFilter)
	ON_EN_CHANGE(IDC_FILTERLEVEL, OnSettingsChange)
	ON_BN_CLICKED(IDC_USESECIDENT, OnSettingsChange)
	ON_BN_CLICKED(IDC_LOADURL, OnLoadIPFFromURL)
	ON_EN_CHANGE(IDC_UPDATEURL, OnEnChangeUpdateUrl)
	ON_MESSAGE(UM_RESTORE_UPDATEURL, OnRestoreUpdateUrl)
	ON_BN_CLICKED(IDC_AUTOUPDATE_IPFILTER, OnBnClickedAutoupdateIpfilter)
	ON_EN_CHANGE(IDC_IPFILTERPERIOD, OnEnChangeIpfilterperiod)
	ON_BN_CLICKED(IDC_DD, OnDDClicked)
	ON_WM_HELPINFO()
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDC_SEESHARE1, OnSettingsChange)
	ON_BN_CLICKED(IDC_SEESHARE2, OnSettingsChange)
	ON_BN_CLICKED(IDC_SEESHARE3, OnSettingsChange)
	ON_BN_CLICKED(IDC_ENABLEOBFUSCATION, OnObfuscatedRequestedChange)
	ON_BN_CLICKED(IDC_ONLYOBFUSCATED, OnSettingsChange)
	ON_BN_CLICKED(IDC_DISABLEOBFUSCATION, OnObfuscatedDisabledChange)
	ON_BN_CLICKED(IDC_SEARCHSPAMFILTER, OnSettingsChange)
	ON_BN_CLICKED(IDC_CHECK_FILE_OPEN, OnSettingsChange)
END_MESSAGE_MAP()

CPPgSecurity::CPPgSecurity()
	: CPropertyPage(CPPgSecurity::IDD)
	, m_pacIPFilterURL()
	, m_strUpdateUrlText()
	, m_bAutoUpdate()
	, m_uPeriodDays()
{
}

void CPPgSecurity::DoDataExchange(CDataExchange *pDX)
{
	CPropertyPage::DoDataExchange(pDX);
}

void CPPgSecurity::LoadSettings()
{
	CheckDlgButton(IDC_ENABLE_IPFILTER, static_cast<UINT>(thePrefs.IsIPFilterEnabled()));
	SetDlgItemInt(IDC_FILTERLEVEL, thePrefs.filterlevel);
	CheckDlgButton(IDC_FILTERSERVERBYIPFILTER, thePrefs.filterserverbyip);

	CheckDlgButton(IDC_USESECIDENT, thePrefs.m_bUseSecureIdent);

	CheckDlgButton(IDC_DISABLEOBFUSCATION, static_cast<UINT>(!thePrefs.IsCryptLayerEnabled()));
	GetDlgItem(IDC_ENABLEOBFUSCATION)->EnableWindow(thePrefs.IsCryptLayerEnabled());
	CheckDlgButton(IDC_ENABLEOBFUSCATION, static_cast<UINT>(thePrefs.IsCryptLayerPreferred()));
	GetDlgItem(IDC_ONLYOBFUSCATED)->EnableWindow(thePrefs.IsCryptLayerPreferred());

	CheckDlgButton(IDC_ONLYOBFUSCATED, thePrefs.IsCryptLayerRequired());
	CheckDlgButton(IDC_SEARCHSPAMFILTER, thePrefs.IsSearchSpamFilterEnabled());
	CheckDlgButton(IDC_CHECK_FILE_OPEN, thePrefs.GetCheckFileOpen());
	m_bAutoUpdate = thePrefs.GetAutoIPFilterUpdate();
	m_uPeriodDays = thePrefs.GetIPFilterUpdatePeriodDays();
	CheckDlgButton(IDC_AUTOUPDATE_IPFILTER, static_cast<UINT>(m_bAutoUpdate));
	SetDlgItemInt(IDC_IPFILTERPERIOD, m_uPeriodDays, FALSE);
	UpdateIPFilterControls();
	UpdateAutoUpdateControls();
	UpdateIPFilterStats();

	ASSERT(vsfaEverybody == 0);
	ASSERT(vsfaFriends == 1);
	ASSERT(vsfaNobody == 2);
	CheckRadioButton(IDC_SEESHARE1, IDC_SEESHARE3, IDC_SEESHARE1 + thePrefs.m_iSeeShares);
}

BOOL CPPgSecurity::OnInitDialog()
{
	CPropertyPage::OnInitDialog();
	InitWindowStyles(this);

	LoadSettings();
	Localize();
	UpdateToolTips();

	if (thePrefs.GetUseAutocompletion()) {
		if (!m_pacIPFilterURL) {
			m_pacIPFilterURL = new CCustomAutoComplete();
			m_pacIPFilterURL->AddRef();
			if (m_pacIPFilterURL->Bind(::GetDlgItem(m_hWnd, IDC_UPDATEURL), ACO_UPDOWNKEYDROPSLIST | ACO_AUTOSUGGEST | ACO_FILTERPREFIXES)) {
				m_pacIPFilterURL->LoadList(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + IPFILTERUPDATEURL_STRINGS_PROFILE);
				SeedDefaultIPFilterUpdateUrls();
			}
		}
		CString strUpdateUrl(thePrefs.GetIPFilterUpdateUrl());
		if (strUpdateUrl.IsEmpty() && m_pacIPFilterURL->GetItemCount() > 0)
			strUpdateUrl = m_pacIPFilterURL->GetItem(0);
		SetDlgItemText(IDC_UPDATEURL, strUpdateUrl);
		m_strUpdateUrlText = strUpdateUrl;
		if (theApp.m_fontSymbol.m_hObject) {
			GetDlgItem(IDC_DD)->SetFont(&theApp.m_fontSymbol);
			SetDlgItemText(IDC_DD, _T("6")); // show a down-arrow
		}
	} else {
		m_strUpdateUrlText = thePrefs.GetIPFilterUpdateUrl();
		SetDlgItemText(IDC_UPDATEURL, m_strUpdateUrlText);
		GetDlgItem(IDC_DD)->ShowWindow(SW_HIDE);
	}
	SetModified(FALSE);

	return TRUE;  // return TRUE unless you set the focus to the control
				  // EXCEPTION: OCX Property Pages should return FALSE
}

void CPPgSecurity::UpdateToolTips()
{
	if (!m_toolTip.Init(this))
		return;

	m_toolTip.SetTool(this, IDC_USESECIDENT, GetResString(IDS_PPG_SECURITY_TT_USESECIDENT));
	m_toolTip.SetTool(this, IDC_ENABLE_IPFILTER, GetResString(IDS_PPG_SECURITY_TT_ENABLE_IPFILTER));
	m_toolTip.SetTool(this, IDC_FILTERSERVERBYIPFILTER, GetResString(IDS_PPG_SECURITY_TT_FILTERSERVERBYIPFILTER));
	m_toolTip.SetTool(this, IDC_FILTERLEVEL, GetResString(IDS_PPG_SECURITY_TT_FILTERLEVEL));
	m_toolTip.SetTool(this, IDC_RELOADFILTER, GetResString(IDS_PPG_SECURITY_TT_RELOADFILTER));
	m_toolTip.SetTool(this, IDC_EDITFILTER, GetResString(IDS_PPG_SECURITY_TT_EDITFILTER));
	m_toolTip.SetTool(this, IDC_UPDATEURL, GetResString(IDS_PPG_SECURITY_TT_UPDATEURL));
	m_toolTip.SetTool(this, IDC_DD, GetResString(IDS_PPG_SECURITY_TT_DD));
	m_toolTip.SetTool(this, IDC_LOADURL, GetResString(IDS_PPG_SECURITY_TT_LOADURL));
	m_toolTip.SetTool(this, IDC_AUTOUPDATE_IPFILTER, GetResString(IDS_PPG_SECURITY_TT_AUTOUPDATE_IPFILTER));
	m_toolTip.SetTool(this, IDC_IPFILTERPERIOD, GetResString(IDS_PPG_SECURITY_TT_IPFILTERPERIOD));
	m_toolTip.SetTool(this, IDC_SEESHARE1, GetResString(IDS_PPG_SECURITY_TT_SEESHARE1));
	m_toolTip.SetTool(this, IDC_SEESHARE2, GetResString(IDS_PPG_SECURITY_TT_SEESHARE2));
	m_toolTip.SetTool(this, IDC_SEESHARE3, GetResString(IDS_PPG_SECURITY_TT_SEESHARE3));
	m_toolTip.SetTool(this, IDC_ENABLEOBFUSCATION, GetResString(IDS_PPG_SECURITY_TT_ENABLEOBFUSCATION));
	m_toolTip.SetTool(this, IDC_ONLYOBFUSCATED, GetResString(IDS_PPG_SECURITY_TT_ONLYOBFUSCATED));
	m_toolTip.SetTool(this, IDC_DISABLEOBFUSCATION, GetResString(IDS_PPG_SECURITY_TT_DISABLEOBFUSCATION));
	m_toolTip.SetTool(this, IDC_SEARCHSPAMFILTER, GetResString(IDS_PPG_SECURITY_TT_SEARCHSPAMFILTER));
	m_toolTip.SetTool(this, IDC_CHECK_FILE_OPEN, GetResString(IDS_PPG_SECURITY_TT_CHECK_FILE_OPEN));
}

BOOL CPPgSecurity::OnApply()
{
	UINT uLevel = thePrefs.filterlevel;
	bool bFilter = thePrefs.filterserverbyip;
	const bool bIPFilterEnabledOld = thePrefs.IsIPFilterEnabled();
	const bool bAutoUpdateOld = thePrefs.GetAutoIPFilterUpdate();
	const UINT uPeriodDaysOld = thePrefs.GetIPFilterUpdatePeriodDays();
	const CString strUpdateUrlOld(thePrefs.GetIPFilterUpdateUrl());
	thePrefs.SetIPFilterEnabled(IsDlgButtonChecked(IDC_ENABLE_IPFILTER) != 0);
	thePrefs.filterlevel = GetDlgItemInt(IDC_FILTERLEVEL, NULL, FALSE);
	thePrefs.filterserverbyip = IsDlgButtonChecked(IDC_FILTERSERVERBYIPFILTER) != 0;
	if (thePrefs.IsIPFilterEnabled() && !bIPFilterEnabledOld && theApp.ipfilter != NULL)
		theApp.ipfilter->LoadFromDefaultFile(true);
	if (thePrefs.IsIPFilterEnabled() && thePrefs.filterserverbyip && (bIPFilterEnabledOld != thePrefs.IsIPFilterEnabled() || !bFilter || uLevel != thePrefs.filterlevel))
		theApp.emuledlg->serverwnd->serverlistctrl.RemoveAllFilteredServers();

	thePrefs.m_bUseSecureIdent = IsDlgButtonChecked(IDC_USESECIDENT) != 0;
	thePrefs.m_bCryptLayerRequested = IsDlgButtonChecked(IDC_ENABLEOBFUSCATION) != 0;
	thePrefs.m_bCryptLayerRequired = IsDlgButtonChecked(IDC_ONLYOBFUSCATED) != 0;
	thePrefs.m_bCryptLayerSupported = !IsDlgButtonChecked(IDC_DISABLEOBFUSCATION);
	thePrefs.m_bCheckFileOpen = IsDlgButtonChecked(IDC_CHECK_FILE_OPEN) != 0;
	thePrefs.m_bEnableSearchResultFilter = IsDlgButtonChecked(IDC_SEARCHSPAMFILTER) != 0;


	if (IsDlgButtonChecked(IDC_SEESHARE1))
		thePrefs.m_iSeeShares = vsfaEverybody;
	else if (IsDlgButtonChecked(IDC_SEESHARE2))
		thePrefs.m_iSeeShares = vsfaFriends;
	else
		thePrefs.m_iSeeShares = vsfaNobody;

	CString strUpdateUrl;
	GetDlgItemText(IDC_UPDATEURL, strUpdateUrl);
	thePrefs.SetIPFilterUpdateUrl(strUpdateUrl);
	m_bAutoUpdate = IsDlgButtonChecked(IDC_AUTOUPDATE_IPFILTER) != 0;
	thePrefs.SetAutoIPFilterUpdate(m_bAutoUpdate);
	BOOL bTranslated = FALSE;
	m_uPeriodDays = GetDlgItemInt(IDC_IPFILTERPERIOD, &bTranslated, FALSE);
	if (!bTranslated
		|| m_uPeriodDays < thePrefs.GetMinIPFilterUpdatePeriodDays()
		|| m_uPeriodDays > thePrefs.GetMaxIPFilterUpdatePeriodDays())
	{
		CString strDetail;
		strDetail.Format(GetResString(IDS_TWEAKS_VALIDATION_INT_RANGE_FMT),
			static_cast<int>(thePrefs.GetMinIPFilterUpdatePeriodDays()),
			static_cast<int>(thePrefs.GetMaxIPFilterUpdatePeriodDays()));
		CString strMessage;
		strMessage.Format(GetResString(IDS_TWEAKS_VALIDATION_INVALID_VALUE_FMT),
			static_cast<LPCTSTR>(GetResString(IDS_IPFILTER_UPDATE_DAYS)),
			static_cast<LPCTSTR>(strDetail));
		AfxMessageBox(strMessage, MB_OK | MB_ICONWARNING);
		GetDlgItem(IDC_IPFILTERPERIOD)->SetFocus();
		return FALSE;
	}
	thePrefs.SetIPFilterUpdatePeriodDays(m_uPeriodDays);

	if (theApp.ipfilterUpdater != NULL
		&& thePrefs.GetAutoIPFilterUpdate()
		&& (bAutoUpdateOld != thePrefs.GetAutoIPFilterUpdate()
			|| uPeriodDaysOld != thePrefs.GetIPFilterUpdatePeriodDays()
			|| strUpdateUrlOld != thePrefs.GetIPFilterUpdateUrl()))
	{
		(void)theApp.ipfilterUpdater->QueueBackgroundRefresh();
	}

	LoadSettings();
	SetModified(FALSE);
	return CPropertyPage::OnApply();
}

void CPPgSecurity::Localize()
{
	if (m_hWnd) {
		SetWindowText(GetResString(IDS_SECURITY));
		SetDlgItemText(IDC_STATIC_IPFILTER, GetResString(IDS_IPFILTER));
		SetDlgItemText(IDC_ENABLE_IPFILTER, GetResString(IDS_ENABLE_IPFILTER));
		SetDlgItemText(IDC_IPFILTER_EXPLANATION, GetResString(IDS_IPFILTER_EXPLANATION));
		UpdateIPFilterStats();
		SetDlgItemText(IDC_RELOADFILTER, GetResString(IDS_SF_RELOAD));
		SetDlgItemText(IDC_EDITFILTER, GetResString(IDS_EDIT));
		SetDlgItemText(IDC_STATIC_FILTERLEVEL, GetResString(IDS_FILTERLEVEL) + _T(':'));
		SetDlgItemText(IDC_FILTERSERVERBYIPFILTER, GetResString(IDS_FILTERSERVERBYIPFILTER));

		SetDlgItemText(IDC_SEC_MISC, GetResString(IDS_PW_MISC));
		SetDlgItemText(IDC_USESECIDENT, GetResString(IDS_USESECIDENT));
		SetDlgItemText(IDC_STATIC_UPDATEFROM, GetResString(IDS_UPDATEFROM));
		SetDlgItemText(IDC_LOADURL, GetResString(IDS_LOADURL));
		SetDlgItemText(IDC_AUTOUPDATE_IPFILTER, GetResString(IDS_IPFILTER_AUTO_UPDATE));
		SetDlgItemText(IDC_IPFILTERPERIOD_LABEL, GetResString(IDS_IPFILTER_UPDATE_DAYS));

		SetDlgItemText(IDC_SEEMYSHARE_FRM, GetResString(IDS_PW_SHARE));
		SetDlgItemText(IDC_SEESHARE1, GetResString(IDS_PW_EVER));
		SetDlgItemText(IDC_SEESHARE2, GetResString(IDS_FSTATUS_FRIENDSONLY));
		SetDlgItemText(IDC_SEESHARE3, GetResString(IDS_PW_NOONE));

		SetDlgItemText(IDC_DISABLEOBFUSCATION, GetResString(IDS_DISABLEOBFUSCATION));
		SetDlgItemText(IDC_ONLYOBFUSCATED, GetResString(IDS_ONLYOBFUSCATED));
		SetDlgItemText(IDC_ENABLEOBFUSCATION, GetResString(IDS_ENABLEOBFUSCATION));
		SetDlgItemText(IDC_SEC_OBFUSCATIONBOX, GetResString(IDS_PROTOCOLOBFUSCATION));
		SetDlgItemText(IDC_SEARCHSPAMFILTER, GetResString(IDS_SEARCHSPAMFILTER));
		SetDlgItemText(IDC_CHECK_FILE_OPEN, GetResString(IDS_CHECK_FILE_OPEN));
	}
}

void CPPgSecurity::SeedDefaultIPFilterUpdateUrls()
{
	if (m_pacIPFilterURL == NULL || !m_pacIPFilterURL->IsBound() || m_pacIPFilterURL->GetItemCount() > 0)
		return;

	for (size_t i = 0; i < _countof(s_apszDefaultIPFilterUpdateUrls); ++i)
		m_pacIPFilterURL->AddItem(s_apszDefaultIPFilterUpdateUrls[i], -1);
}

void CPPgSecurity::OnReloadIPFilter()
{
	CWaitCursor curHourglass;
	theApp.ipfilter->LoadFromDefaultFile();
	if (thePrefs.GetFilterServerByIP())
		theApp.emuledlg->serverwnd->serverlistctrl.RemoveAllFilteredServers();
	UpdateIPFilterStats();
}

void CPPgSecurity::OnEditIPFilter()
{
	ShellOpen(thePrefs.GetTxtEditor(), _T('"') + CIPFilter::GetDefaultFilePath() + _T('"'));
}

void CPPgSecurity::OnLoadIPFFromURL()
{
	CString url;
	GetDlgItemText(IDC_UPDATEURL, url);
	thePrefs.SetIPFilterUpdateUrl(url, true);
	if (!url.IsEmpty() && m_pacIPFilterURL && m_pacIPFilterURL->IsBound())
		m_pacIPFilterURL->AddItem(url, 0);
	if (theApp.ipfilterUpdater != NULL)
		(void)theApp.ipfilterUpdater->UpdateFromUrlInteractive(url);
	else
		OnReloadIPFilter();
	UpdateIPFilterStats();
}

void CPPgSecurity::OnDestroy()
{
	DeleteDDB();
	CPropertyPage::OnDestroy();
}

void CPPgSecurity::DeleteDDB()
{
	if (m_pacIPFilterURL) {
		m_pacIPFilterURL->SaveList(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + IPFILTERUPDATEURL_STRINGS_PROFILE);
		m_pacIPFilterURL->Unbind();
		m_pacIPFilterURL->Release();
		m_pacIPFilterURL = NULL;
	}
}

BOOL CPPgSecurity::PreTranslateMessage(MSG *pMsg)
{
	m_toolTip.RelayEvent(pMsg);
	if (pMsg->message == WM_KEYDOWN) {

		if (pMsg->wParam == VK_ESCAPE)
			return FALSE;

		if (pMsg->hwnd == GetDlgItem(IDC_UPDATEURL)->m_hWnd) {
			switch (pMsg->wParam) {
			case VK_RETURN:
				if (m_pacIPFilterURL && m_pacIPFilterURL->IsBound()) {
					CString strText;
					GetDlgItemText(IDC_UPDATEURL, strText);
					if (!strText.IsEmpty()) {
						SetDlgItemText(IDC_UPDATEURL, _T("")); // this seems to be the only chance to let the drop-down list to disappear
						SetDlgItemText(IDC_UPDATEURL, strText);
						static_cast<CEdit*>(GetDlgItem(IDC_UPDATEURL))->SetSel(strText.GetLength(), strText.GetLength());
					}
				}
				return TRUE;
			case VK_DELETE:
				if (m_pacIPFilterURL && m_pacIPFilterURL->IsBound()) {
					BYTE bKeyState;
					if (GetKeyboardState(&bKeyState) && (bKeyState & (VK_LCONTROL | VK_RCONTROL | VK_LMENU | VK_RMENU)))
						m_pacIPFilterURL->Clear();
					else
						m_pacIPFilterURL->RemoveSelectedItem();
				}
			}
		}
	}

	return CPropertyPage::PreTranslateMessage(pMsg);
}

void CPPgSecurity::OnEnChangeUpdateUrl()
{
	CString strUrl;
	GetDlgItemText(IDC_UPDATEURL, strUrl);
	if (!strUrl.IsEmpty())
		m_strUpdateUrlText = strUrl;
	GetDlgItem(IDC_LOADURL)->EnableWindow(!strUrl.IsEmpty());
	SetModified();
}

LRESULT CPPgSecurity::OnRestoreUpdateUrl(WPARAM, LPARAM)
{
	if (!m_strUpdateUrlText.IsEmpty()) {
		CString strUrl;
		GetDlgItemText(IDC_UPDATEURL, strUrl);
		if (strUrl.IsEmpty())
			SetDlgItemText(IDC_UPDATEURL, m_strUpdateUrlText);
		GetDlgItem(IDC_LOADURL)->EnableWindow(TRUE);
	}
	return 0;
}

void CPPgSecurity::UpdateAutoUpdateControls()
{
	const BOOL bAutoUpdate = IsDlgButtonChecked(IDC_AUTOUPDATE_IPFILTER) != 0;
	GetDlgItem(IDC_IPFILTERPERIOD_LABEL)->EnableWindow(bAutoUpdate);
	GetDlgItem(IDC_IPFILTERPERIOD)->EnableWindow(bAutoUpdate);
}

void CPPgSecurity::UpdateIPFilterControls()
{
	const BOOL bEnabled = IsDlgButtonChecked(IDC_ENABLE_IPFILTER) != 0;
	GetDlgItem(IDC_FILTERSERVERBYIPFILTER)->EnableWindow(bEnabled);
	GetDlgItem(IDC_STATIC_FILTERLEVEL)->EnableWindow(bEnabled);
	GetDlgItem(IDC_STATIC_FILTERLEVEL2)->EnableWindow(bEnabled);
	GetDlgItem(IDC_FILTERLEVEL)->EnableWindow(bEnabled);
	UpdateIPFilterStats();
}

void CPPgSecurity::UpdateIPFilterStats()
{
	if (GetDlgItem(IDC_IPFILTER_STATS) == NULL)
		return;

	BOOL bTranslated = FALSE;
	UINT uFilterLevel = GetDlgItemInt(IDC_FILTERLEVEL, &bTranslated, FALSE);
	if (!bTranslated)
		uFilterLevel = thePrefs.filterlevel;

	const INT_PTR nRules = theApp.ipfilter != NULL ? theApp.ipfilter->GetIPFilter().GetCount() : 0;
	CString strStats;
	strStats.Format(GetResString(IDS_IPFILTER_STATS_FMT),
		static_cast<LPCTSTR>(IsDlgButtonChecked(IDC_ENABLE_IPFILTER) != 0 ? GetResString(IDS_ENABLED) : GetResString(IDS_DISABLED)),
		static_cast<LPCTSTR>(GetFormatedUInt(static_cast<ULONG>(nRules))),
		uFilterLevel);
	SetDlgItemText(IDC_IPFILTER_STATS, strStats);
}

void CPPgSecurity::OnBnClickedIPFilterEnabled()
{
	UpdateIPFilterControls();
	SetModified();
}

void CPPgSecurity::OnSettingsChange()
{
	UpdateIPFilterStats();
	SetModified();
}

void CPPgSecurity::OnBnClickedAutoupdateIpfilter()
{
	UpdateAutoUpdateControls();
	SetModified();
}

void CPPgSecurity::OnEnChangeIpfilterperiod()
{
	SetModified();
}

void CPPgSecurity::OnDDClicked()
{
	CWnd *box = GetDlgItem(IDC_UPDATEURL);
	CString strText;
	box->GetWindowText(strText);
	if (strText.IsEmpty() && !m_strUpdateUrlText.IsEmpty()) {
		strText = m_strUpdateUrlText;
		box->SetWindowText(strText);
	}
	box->SetFocus();
	if (!strText.IsEmpty()) {
		m_strUpdateUrlText = strText;
		static_cast<CEdit*>(box)->SetSel(strText.GetLength(), strText.GetLength());
		PostMessage(UM_RESTORE_UPDATEURL);
		return;
	}
	box->SendMessage(WM_KEYDOWN, VK_DOWN, 0x00510001);
}

void CPPgSecurity::OnHelp()
{
	theApp.ShowHelp(eMule_FAQ_Preferences_Security);
}

BOOL CPPgSecurity::OnCommand(WPARAM wParam, LPARAM lParam)
{
	return (wParam == ID_HELP) ? OnHelpInfo(NULL) : __super::OnCommand(wParam, lParam);
}

BOOL CPPgSecurity::OnHelpInfo(HELPINFO*)
{
	OnHelp();
	return TRUE;
}

void CPPgSecurity::OnObfuscatedDisabledChange()
{
	GetDlgItem(IDC_ENABLEOBFUSCATION)->EnableWindow(!IsDlgButtonChecked(IDC_DISABLEOBFUSCATION));
	if (IsDlgButtonChecked(IDC_DISABLEOBFUSCATION)) {
		GetDlgItem(IDC_ONLYOBFUSCATED)->EnableWindow(FALSE);
		CheckDlgButton(IDC_ENABLEOBFUSCATION, 0);
		CheckDlgButton(IDC_ONLYOBFUSCATED, 0);
	}
	OnSettingsChange();
}

void CPPgSecurity::OnObfuscatedRequestedChange()
{
	bool bCheck = IsDlgButtonChecked(IDC_ENABLEOBFUSCATION) != 0;
	if (bCheck)
		GetDlgItem(IDC_ENABLEOBFUSCATION)->EnableWindow(bCheck);
	else
		CheckDlgButton(IDC_ONLYOBFUSCATED, bCheck);
	GetDlgItem(IDC_ONLYOBFUSCATED)->EnableWindow(bCheck);
	OnSettingsChange();
}
