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
#include "PreferencesDlg.h"
#include "Wizard.h"
#include "emuledlg.h"
#include "StatisticsDlg.h"
#include "opcodes.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
struct BroadbandPreset
{
	LPCTSTR pszName;
	uint32 uploadMbit;
	uint32 downloadMbit;
	uint32 uploadLimitKiB;
	uint32 downloadLimitKiB;
	UINT maxConnections;
	UINT maxSourcesPerFile[3];
};

constexpr uint32 MbitToKiB(uint32 nMbit)
{
	return (nMbit * 1000000u + 4096u) / 8192u;
}

const BroadbandPreset kBroadbandPresets[] =
{
	{ _T("Tier 1"),  25u,  100u, MbitToKiB(25u),  MbitToKiB(100u), 400u, { 350u, 250u, 175u } },
	{ _T("Tier 2"),  50u,  250u, MbitToKiB(50u),  MbitToKiB(250u), 500u, { 500u, 350u, 225u } },
	{ _T("Tier 3"), 100u,  500u, MbitToKiB(100u), MbitToKiB(500u), 500u, { 650u, 450u, 300u } },
	{ _T("Tier 4"), 250u, 1000u, MbitToKiB(250u), MbitToKiB(1000u), 500u, { 800u, 550u, 375u } }
};

constexpr UINT kRecommendedMaxConnections = 500u;

int GetPresetIndex(int iSelectionMark)
{
	return iSelectionMark - 1;
}
}


// CConnectionWizardDlg dialog

IMPLEMENT_DYNAMIC(CConnectionWizardDlg, CDialog)

BEGIN_MESSAGE_MAP(CConnectionWizardDlg, CDialog)
	ON_BN_CLICKED(IDC_WIZ_APPLY_BUTTON, OnBnClickedApply)
	ON_BN_CLICKED(IDC_WIZ_CANCEL_BUTTON, OnBnClickedCancel)
	ON_BN_CLICKED(IDC_WIZ_LOWDOWN_RADIO, OnBnClickedWizLowdownloadRadio)
	ON_BN_CLICKED(IDC_WIZ_MEDIUMDOWN_RADIO, OnBnClickedWizMediumdownloadRadio)
	ON_BN_CLICKED(IDC_WIZ_HIGHDOWN_RADIO, OnBnClickedWizHighdownloadRadio)
	ON_NOTIFY(NM_CLICK, IDC_PROVIDERS, OnNmClickProviders)
END_MESSAGE_MAP()

CConnectionWizardDlg::CConnectionWizardDlg(CWnd *pParent /*=NULL*/)
	: CDialog(CConnectionWizardDlg::IDD, pParent)
	, m_icoWnd()
	, m_iTotalDownload()
{
}

CConnectionWizardDlg::~CConnectionWizardDlg()
{
	if (m_icoWnd)
		VERIFY(::DestroyIcon(m_icoWnd));
}

void CConnectionWizardDlg::DoDataExchange(CDataExchange *pDX)
{
	CDialog::DoDataExchange(pDX);
	//DDX_Radio(pDX, IDC_WIZ_XP_RADIO, m_iOS);
	DDX_Control(pDX, IDC_PROVIDERS, m_provider);
	DDX_Radio(pDX, IDC_WIZ_LOWDOWN_RADIO, m_iTotalDownload);
}

void CConnectionWizardDlg::OnBnClickedApply()
{
	UINT download = GetDlgItemInt(IDC_WIZ_TRUEDOWNLOAD_BOX, NULL, FALSE);
	if (download <= 0) {
		GetDlgItem(IDC_WIZ_TRUEDOWNLOAD_BOX)->SetFocus();
		return;
	}

	UINT upload = GetDlgItemInt(IDC_WIZ_TRUEUPLOAD_BOX, NULL, FALSE);
	if (upload <= 0) {
		GetDlgItem(IDC_WIZ_TRUEUPLOAD_BOX)->SetFocus();
		return;
	}

	int iPreset = GetPresetIndex(m_provider.GetSelectionMark());
	if (iPreset >= 0 && iPreset < static_cast<int>(_countof(kBroadbandPresets))) {
		const BroadbandPreset &preset = kBroadbandPresets[iPreset];
		upload = preset.uploadLimitKiB;
		download = preset.downloadLimitKiB;
		thePrefs.maxconnections = min(preset.maxConnections, kRecommendedMaxConnections);
		thePrefs.maxsourceperfile = preset.maxSourcesPerFile[m_iTotalDownload];
	} else {
		static const UINT aMaxConnections[4] = { 400u, 500u, 500u, 500u };
		static const UINT aMaxSourcesPerFile[4][3] =
		{
			{ 350u, 250u, 175u },
			{ 500u, 350u, 225u },
			{ 650u, 450u, 300u },
			{ 800u, 550u, 375u }
		};

		int iProfile = 0;
		if (download > kBroadbandPresets[2].downloadLimitKiB)
			iProfile = 3;
		else if (download > kBroadbandPresets[1].downloadLimitKiB)
			iProfile = 2;
		else if (download > kBroadbandPresets[0].downloadLimitKiB)
			iProfile = 1;

		thePrefs.maxconnections = min(aMaxConnections[iProfile], kRecommendedMaxConnections);
		thePrefs.maxsourceperfile = aMaxSourcesPerFile[iProfile][m_iTotalDownload];
	}

	thePrefs.SetMaxUpload(upload);
	thePrefs.SetMaxDownload(download);
	thePrefs.SetMaxGraphDownloadRate(download);
	theApp.emuledlg->statisticswnd->SetARange(false, thePrefs.GetMaxUpload());
	theApp.emuledlg->statisticswnd->SetARange(true, thePrefs.GetMaxGraphDownloadRate());
	theApp.emuledlg->preferenceswnd->m_wndConnection.LoadSettings();
	CDialog::OnOK();
}

void CConnectionWizardDlg::OnBnClickedCancel()
{
	CDialog::OnCancel();
}

void CConnectionWizardDlg::OnBnClickedWizLowdownloadRadio()
{
	m_iTotalDownload = 0;
}

void CConnectionWizardDlg::OnBnClickedWizMediumdownloadRadio()
{
	m_iTotalDownload = 1;
}

void CConnectionWizardDlg::OnBnClickedWizHighdownloadRadio()
{
	m_iTotalDownload = 2;
}

void CConnectionWizardDlg::OnBnClickedWizResetButton()
{
	SetDlgItemInt(IDC_WIZ_TRUEDOWNLOAD_BOX, 0, FALSE);
	SetDlgItemInt(IDC_WIZ_TRUEUPLOAD_BOX, 0, FALSE);
}

BOOL CConnectionWizardDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	InitWindowStyles(this);

	SetIcon(m_icoWnd = theApp.LoadIcon(_T("Wizard")), FALSE);

	CheckRadioButton(IDC_WIZ_LOWDOWN_RADIO, IDC_WIZ_HIGHDOWN_RADIO, IDC_WIZ_LOWDOWN_RADIO);
	GetDlgItem(IDC_KBITS)->ShowWindow(SW_HIDE);
	GetDlgItem(IDC_KBYTES)->ShowWindow(SW_HIDE);

	SetDlgItemInt(IDC_WIZ_TRUEDOWNLOAD_BOX, thePrefs.GetConfiguredMaxDownload(), FALSE);
	SetDlgItemInt(IDC_WIZ_TRUEUPLOAD_BOX, thePrefs.GetConfiguredMaxUpload(), FALSE);

	m_provider.InsertColumn(0, GetResString(IDS_TYPE), LVCFMT_LEFT, 150);
	m_provider.InsertColumn(1, GetResString(IDS_WIZ_DOWN), LVCFMT_LEFT, 85);
	m_provider.InsertColumn(2, GetResString(IDS_WIZ_UP), LVCFMT_LEFT, 85);
	m_provider.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	m_provider.InsertItem(0, GetResString(IDS_WIZARD_CUSTOM));
	m_provider.SetItemText(0, 1, GetResString(IDS_WIZARD_ENTERBELOW));
	m_provider.SetItemText(0, 2, GetResString(IDS_WIZARD_ENTERBELOW));
	for (int i = 0; i < static_cast<int>(_countof(kBroadbandPresets)); ++i) {
		CString sValue;
		const int iItem = i + 1;
		m_provider.InsertItem(iItem, kBroadbandPresets[i].pszName);
		sValue.Format(_T("%u"), kBroadbandPresets[i].downloadMbit);
		m_provider.SetItemText(iItem, 1, sValue);
		sValue.Format(_T("%u"), kBroadbandPresets[i].uploadMbit);
		m_provider.SetItemText(iItem, 2, sValue);
	}

	m_provider.SetSelectionMark(1);
	m_provider.SetItemState(1, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
	SetCustomItemsActivation();
	SetDlgItemInt(IDC_WIZ_TRUEDOWNLOAD_BOX, kBroadbandPresets[0].downloadLimitKiB, FALSE);
	SetDlgItemInt(IDC_WIZ_TRUEUPLOAD_BOX, kBroadbandPresets[0].uploadLimitKiB, FALSE);

	Localize();

	return TRUE;
}

void CConnectionWizardDlg::OnNmClickProviders(LPNMHDR, LRESULT *pResult)
{
	SetCustomItemsActivation();
	int iPreset = GetPresetIndex(m_provider.GetSelectionMark());
	if (iPreset >= 0 && iPreset < static_cast<int>(_countof(kBroadbandPresets))) {
		SetDlgItemInt(IDC_WIZ_TRUEDOWNLOAD_BOX, kBroadbandPresets[iPreset].downloadLimitKiB, FALSE);
		SetDlgItemInt(IDC_WIZ_TRUEUPLOAD_BOX, kBroadbandPresets[iPreset].uploadLimitKiB, FALSE);
	} else {
		SetDlgItemInt(IDC_WIZ_TRUEDOWNLOAD_BOX, thePrefs.GetConfiguredMaxDownload(), FALSE);
		SetDlgItemInt(IDC_WIZ_TRUEUPLOAD_BOX, thePrefs.GetConfiguredMaxUpload(), FALSE);
	}
	if (pResult)
		*pResult = 0;
}

void CConnectionWizardDlg::Localize()
{
	SetWindowText(GetResString(IDS_WIZARD));
	SetDlgItemText(IDC_WIZ_OS_FRAME, GetResString(IDS_WIZ_OS_FRAME));
	SetDlgItemText(IDC_WIZ_CONCURENTDOWN_FRAME, GetResString(IDS_CONCURDWL));
	SetDlgItemText(IDC_WIZ_HOTBUTTON_FRAME, GetResString(IDS_WIZ_CTFRAME));
	SetDlgItemText(IDC_WIZ_TRUEUPLOAD_TEXT, GetResString(IDS_WIZ_TRUEUPLOAD_TEXT));
	SetDlgItemText(IDC_WIZ_TRUEDOWNLOAD_TEXT, GetResString(IDS_WIZ_TRUEDOWNLOAD_TEXT));
	SetDlgItemText(IDC_WIZ_APPLY_BUTTON, GetResString(IDS_PW_APPLY));
	SetDlgItemText(IDC_WIZ_CANCEL_BUTTON, GetResString(IDS_CANCEL));
}

void CConnectionWizardDlg::SetCustomItemsActivation()
{
	BOOL bActive = (m_provider.GetSelectionMark() == 0);
	GetDlgItem(IDC_WIZ_TRUEUPLOAD_BOX)->EnableWindow(bActive);
	GetDlgItem(IDC_WIZ_TRUEDOWNLOAD_BOX)->EnableWindow(bActive);
}
