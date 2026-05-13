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
#include "PPgFiles.h"
#include "Inputbox.h"
#include "OtherFunctions.h"
#include "TransferDlg.h"
#include "emuledlg.h"
#include "Preferences.h"
#include "HelpIDs.h"
#include "FileCompletionCommandSeams.h"
#include "PartFilePreviewSeams.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
void EnableDlgItemIfPresent(CWnd &wndParent, int iControlId, BOOL bEnabled)
{
	CWnd *pControl = wndParent.GetDlgItem(iControlId);
	if (pControl != NULL)
		pControl->EnableWindow(bEnabled);
}
}


IMPLEMENT_DYNAMIC(CPPgFiles, CPropertyPage)

BEGIN_MESSAGE_MAP(CPPgFiles, CPropertyPage)
	ON_BN_CLICKED(IDC_PF_TIMECALC, OnSettingsChange)
	ON_BN_CLICKED(IDC_UAP, OnSettingsChange)
	ON_BN_CLICKED(IDC_DAP, OnSettingsChange)
	ON_BN_CLICKED(IDC_PREVIEWPRIO, OnSettingsChange)
	ON_BN_CLICKED(IDC_ADDNEWFILESPAUSED, OnSettingsChange)
	ON_BN_CLICKED(IDC_FULLCHUNKTRANS, OnSettingsChange)
	ON_BN_CLICKED(IDC_STARTNEXTFILE, OnSettingsChange)
	ON_BN_CLICKED(IDC_WATCHCB, OnSettingsChange)
	ON_BN_CLICKED(IDC_STARTNEXTFILECAT, OnSettingsChangeCat1)
	ON_BN_CLICKED(IDC_STARTNEXTFILECAT2, OnSettingsChangeCat2)
	ON_BN_CLICKED(IDC_FNCLEANUP, OnSettingsChange)
	ON_BN_CLICKED(IDC_FNC, OnSetCleanupFilter)
	ON_EN_CHANGE(IDC_VIDEOPLAYER, OnSettingsChange)
	ON_EN_CHANGE(IDC_VIDEOPLAYER_ARGS, OnSettingsChange)
	ON_BN_CLICKED(IDC_VIDEOBACKUP, OnSettingsChange)
	ON_EN_CHANGE(IDC_THUMBNAIL_FFMPEG, OnSettingsChange)
	ON_EN_CHANGE(IDC_THUMBNAIL_INTERVAL, OnSettingsChange)
	ON_BN_CLICKED(IDC_RUNONFILECOMPLETE, OnSettingsChange)
	ON_EN_CHANGE(IDC_FILECOMPLETEPROGRAM, OnSettingsChange)
	ON_EN_CHANGE(IDC_FILECOMPLETEARGS, OnSettingsChange)
	ON_BN_CLICKED(IDC_REMEMBERDOWNLOADED, OnSettingsChange)
	ON_BN_CLICKED(IDC_REMEMBERCANCELLED, OnSettingsChange)
	ON_BN_CLICKED(IDC_BROWSEV, BrowseVideoplayer)
	ON_BN_CLICKED(IDC_BROWSE_THUMBNAIL_FFMPEG, BrowseThumbnailFfmpeg)
	ON_BN_CLICKED(IDC_BROWSE_FILECOMPLETEPROGRAM, BrowseFileCompletionProgram)
	ON_WM_HELPINFO()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

CPPgFiles::CPPgFiles()
	: CPropertyPage(CPPgFiles::IDD)
	, m_icoBrowse()
{
}

void CPPgFiles::DoDataExchange(CDataExchange *pDX)
{
	CPropertyPage::DoDataExchange(pDX);
}

BOOL CPPgFiles::OnInitDialog()
{
	CPropertyPage::OnInitDialog();
	InitWindowStyles(this);

	AddBuddyButton(GetDlgItem(IDC_VIDEOPLAYER)->m_hWnd, ::GetDlgItem(m_hWnd, IDC_BROWSEV));
	InitAttachedBrowseButton(::GetDlgItem(m_hWnd, IDC_BROWSEV), m_icoBrowse);
	AddBuddyButton(GetDlgItem(IDC_THUMBNAIL_FFMPEG)->m_hWnd, ::GetDlgItem(m_hWnd, IDC_BROWSE_THUMBNAIL_FFMPEG));
	InitAttachedBrowseButton(::GetDlgItem(m_hWnd, IDC_BROWSE_THUMBNAIL_FFMPEG), m_icoBrowse);
	AddBuddyButton(GetDlgItem(IDC_FILECOMPLETEPROGRAM)->m_hWnd, ::GetDlgItem(m_hWnd, IDC_BROWSE_FILECOMPLETEPROGRAM));
	InitAttachedBrowseButton(::GetDlgItem(m_hWnd, IDC_BROWSE_FILECOMPLETEPROGRAM), m_icoBrowse);

	CSpinButtonCtrl *pThumbnailIntervalSpin = static_cast<CSpinButtonCtrl*>(GetDlgItem(IDC_THUMBNAIL_INTERVAL_SPIN));
	if (pThumbnailIntervalSpin)
		pThumbnailIntervalSpin->SetRange(0, PartFilePreviewSeams::kVideoThumbnailMaxIntervalSeconds);

	LoadSettings();
	Localize();
	UpdateToolTips();

	return TRUE;  // return TRUE unless you set the focus to the control
				  // EXCEPTION: OCX Property Pages should return FALSE
}

void CPPgFiles::UpdateToolTips()
{
	if (!m_toolTip.Init(this))
		return;

	m_toolTip.SetTool(this, IDC_ADDNEWFILESPAUSED, GetResString(IDS_PPG_FILES_TT_ADDNEWFILESPAUSED));
	m_toolTip.SetTool(this, IDC_UAP, GetResString(IDS_PPG_FILES_TT_UAP));
	m_toolTip.SetTool(this, IDC_DAP, GetResString(IDS_PPG_FILES_TT_DAP));
	m_toolTip.SetTool(this, IDC_PF_TIMECALC, GetResString(IDS_PPG_FILES_TT_PF_TIMECALC));
	m_toolTip.SetTool(this, IDC_FULLCHUNKTRANS, GetResString(IDS_PPG_FILES_TT_FULLCHUNKTRANS));
	m_toolTip.SetTool(this, IDC_PREVIEWPRIO, GetResString(IDS_PPG_FILES_TT_PREVIEWPRIO));
	m_toolTip.SetTool(this, IDC_WATCHCB, GetResString(IDS_PPG_FILES_TT_WATCHCB));
	m_toolTip.SetTool(this, IDC_FNCLEANUP, GetResString(IDS_PPG_FILES_TT_FNCLEANUP));
	m_toolTip.SetTool(this, IDC_FNC, GetResString(IDS_PPG_FILES_TT_FNC));
	m_toolTip.SetTool(this, IDC_STARTNEXTFILE, GetResString(IDS_PPG_FILES_TT_STARTNEXTFILE));
	m_toolTip.SetTool(this, IDC_STARTNEXTFILECAT, GetResString(IDS_PPG_FILES_TT_STARTNEXTFILECAT));
	m_toolTip.SetTool(this, IDC_STARTNEXTFILECAT2, GetResString(IDS_PPG_FILES_TT_STARTNEXTFILECAT2));
	m_toolTip.SetTool(this, IDC_REMEMBERDOWNLOADED, GetResString(IDS_PPG_FILES_TT_REMEMBERDOWNLOADED));
	m_toolTip.SetTool(this, IDC_REMEMBERCANCELLED, GetResString(IDS_PPG_FILES_TT_REMEMBERCANCELLED));
	m_toolTip.SetTool(this, IDC_VIDEOPLAYER, GetResString(IDS_PPG_FILES_TT_VIDEOPLAYER));
	m_toolTip.SetTool(this, IDC_BROWSEV, GetResString(IDS_PPG_FILES_TT_BROWSEV));
	m_toolTip.SetTool(this, IDC_VIDEOPLAYER_ARGS, GetResString(IDS_PPG_FILES_TT_VIDEOPLAYER_ARGS));
	m_toolTip.SetTool(this, IDC_VIDEOBACKUP, GetResString(IDS_PPG_FILES_TT_VIDEOBACKUP));
	m_toolTip.SetTool(this, IDC_THUMBNAIL_FFMPEG, GetResString(IDS_PPG_FILES_TT_THUMBNAIL_FFMPEG));
	m_toolTip.SetTool(this, IDC_BROWSE_THUMBNAIL_FFMPEG, GetResString(IDS_PPG_FILES_TT_THUMBNAIL_FFMPEG));
	m_toolTip.SetTool(this, IDC_THUMBNAIL_INTERVAL, GetResString(IDS_PPG_FILES_TT_VIDEOTHUMBNAILS));
	m_toolTip.SetTool(this, IDC_RUNONFILECOMPLETE, GetResString(IDS_PPG_FILES_TT_RUNONFILECOMPLETE));
	m_toolTip.SetTool(this, IDC_FILECOMPLETEPROGRAM, GetResString(IDS_PPG_FILES_TT_FILECOMPLETEPROGRAM));
	m_toolTip.SetTool(this, IDC_BROWSE_FILECOMPLETEPROGRAM, GetResString(IDS_PPG_FILES_TT_BROWSE_FILECOMPLETEPROGRAM));
	m_toolTip.SetTool(this, IDC_FILECOMPLETEARGS, GetResString(IDS_PPG_FILES_TT_FILECOMPLETEARGS));
}

void CPPgFiles::LoadSettings()
{
	CheckDlgButton(IDC_ADDNEWFILESPAUSED, static_cast<UINT>(thePrefs.addnewfilespaused));
	CheckDlgButton(IDC_PF_TIMECALC, static_cast<UINT>(!thePrefs.m_bUseOldTimeRemaining));
	CheckDlgButton(IDC_PREVIEWPRIO, static_cast<UINT>(thePrefs.m_bpreviewprio));
	CheckDlgButton(IDC_DAP, static_cast<UINT>(thePrefs.m_bDAP));
	CheckDlgButton(IDC_UAP, static_cast<UINT>(thePrefs.m_bUAP));
	CheckDlgButton(IDC_FULLCHUNKTRANS, static_cast<UINT>(thePrefs.m_btransferfullchunks));

	CheckDlgButton(IDC_STARTNEXTFILECAT, FALSE);
	CheckDlgButton(IDC_STARTNEXTFILECAT2, FALSE);
	if (thePrefs.m_istartnextfile) {
		CheckDlgButton(IDC_STARTNEXTFILE, 1);
		if (thePrefs.m_istartnextfile == 2)
			CheckDlgButton(IDC_STARTNEXTFILECAT, TRUE);
		else if (thePrefs.m_istartnextfile == 3)
			CheckDlgButton(IDC_STARTNEXTFILECAT2, TRUE);
	} else
		CheckDlgButton(IDC_STARTNEXTFILE, 0);

	SetDlgItemText(IDC_VIDEOPLAYER, thePrefs.m_strVideoPlayer);
	SetDlgItemText(IDC_VIDEOPLAYER_ARGS, thePrefs.m_strVideoPlayerArgs);
	SetDlgItemText(IDC_THUMBNAIL_FFMPEG, thePrefs.m_strVideoThumbnailFfmpegPath);
	SetDlgItemInt(IDC_THUMBNAIL_INTERVAL, thePrefs.m_uVideoThumbnailIntervalSeconds, FALSE);
	CheckDlgButton(IDC_RUNONFILECOMPLETE, static_cast<UINT>(thePrefs.GetRunCommandOnFileCompletion()));
	SetDlgItemText(IDC_FILECOMPLETEPROGRAM, thePrefs.GetFileCompletionProgram());
	SetDlgItemText(IDC_FILECOMPLETEARGS, thePrefs.GetFileCompletionArguments());

	CheckDlgButton(IDC_VIDEOBACKUP, static_cast<UINT>(thePrefs.m_bMoviePreviewBackup));
	CheckDlgButton(IDC_FNCLEANUP, static_cast<UINT>(thePrefs.AutoFilenameCleanup()));
	CheckDlgButton(IDC_WATCHCB, static_cast<UINT>(thePrefs.watchclipboard));
	CheckDlgButton(IDC_REMEMBERDOWNLOADED, static_cast<UINT>(thePrefs.IsRememberingDownloadedFiles()));
	CheckDlgButton(IDC_REMEMBERCANCELLED, static_cast<UINT>(thePrefs.IsRememberingCancelledFiles()));

	GetDlgItem(IDC_STARTNEXTFILECAT)->EnableWindow(IsDlgButtonChecked(IDC_STARTNEXTFILE));
	UpdateCompletionCommandControls();
}

BOOL CPPgFiles::OnApply()
{
	const CString strOldVideoThumbnailFfmpegPath(thePrefs.m_strVideoThumbnailFfmpegPath);
	const UINT uOldVideoThumbnailIntervalSeconds = thePrefs.m_uVideoThumbnailIntervalSeconds;
	bool bOldPreviewPrio = thePrefs.m_bpreviewprio;
	thePrefs.m_bpreviewprio = IsDlgButtonChecked(IDC_PREVIEWPRIO) != 0;
	if (bOldPreviewPrio != thePrefs.m_bpreviewprio)
		theApp.emuledlg->transferwnd->GetDownloadList()->CreateMenus();

	thePrefs.m_bDAP = IsDlgButtonChecked(IDC_DAP) != 0;

	thePrefs.m_bUAP = IsDlgButtonChecked(IDC_UAP) != 0;

	if (IsDlgButtonChecked(IDC_STARTNEXTFILE)) {
		thePrefs.m_istartnextfile = 1;
		if (IsDlgButtonChecked(IDC_STARTNEXTFILECAT))
			thePrefs.m_istartnextfile = 2;
		else if (IsDlgButtonChecked(IDC_STARTNEXTFILECAT2))
			thePrefs.m_istartnextfile = 3;
	} else
		thePrefs.m_istartnextfile = 0;

	thePrefs.m_btransferfullchunks = IsDlgButtonChecked(IDC_FULLCHUNKTRANS) != 0;

	thePrefs.watchclipboard = IsDlgButtonChecked(IDC_WATCHCB) != 0;

	thePrefs.SetRememberDownloadedFiles(IsDlgButtonChecked(IDC_REMEMBERDOWNLOADED) != 0);

	thePrefs.SetRememberCancelledFiles(IsDlgButtonChecked(IDC_REMEMBERCANCELLED) != 0);

	thePrefs.addnewfilespaused = IsDlgButtonChecked(IDC_ADDNEWFILESPAUSED) != 0;
	thePrefs.autofilenamecleanup = IsDlgButtonChecked(IDC_FNCLEANUP) != 0;
	thePrefs.m_bUseOldTimeRemaining = !IsDlgButtonChecked(IDC_PF_TIMECALC);

	GetDlgItemText(IDC_VIDEOPLAYER, thePrefs.m_strVideoPlayer);
	thePrefs.m_strVideoPlayer.Trim();
	GetDlgItemText(IDC_VIDEOPLAYER_ARGS, thePrefs.m_strVideoPlayerArgs);
	thePrefs.m_strVideoPlayerArgs.Trim();
	GetDlgItemText(IDC_THUMBNAIL_FFMPEG, thePrefs.m_strVideoThumbnailFfmpegPath);
	thePrefs.m_strVideoThumbnailFfmpegPath.Trim();
	BOOL bThumbnailIntervalTranslated = FALSE;
	const UINT uVideoThumbnailIntervalSeconds = PartFilePreviewSeams::NormalizeVideoThumbnailIntervalSeconds(GetDlgItemInt(IDC_THUMBNAIL_INTERVAL, &bThumbnailIntervalTranslated, FALSE));
	thePrefs.m_uVideoThumbnailIntervalSeconds = bThumbnailIntervalTranslated ? uVideoThumbnailIntervalSeconds : PartFilePreviewSeams::kVideoThumbnailDefaultIntervalSeconds;
	thePrefs.m_bRunCommandOnFileCompletion = IsDlgButtonChecked(IDC_RUNONFILECOMPLETE) != 0;
	GetDlgItemText(IDC_FILECOMPLETEPROGRAM, thePrefs.m_strFileCompletionProgram);
	thePrefs.m_strFileCompletionProgram.Trim();
	GetDlgItemText(IDC_FILECOMPLETEARGS, thePrefs.m_strFileCompletionArguments);
	thePrefs.m_strFileCompletionArguments.Trim();
	if (thePrefs.m_bRunCommandOnFileCompletion && !FileCompletionCommandSeams::IsValidConfiguredProgramPath(thePrefs.m_strFileCompletionProgram)) {
		AfxMessageBox(GetResString(IDS_COMPLETIONCOMMAND_INVALID), MB_ICONWARNING);
		return FALSE;
	}
	if (thePrefs.m_uVideoThumbnailIntervalSeconds > 0 && !PartFilePreviewSeams::IsValidConfiguredFfmpegPath(thePrefs.m_strVideoThumbnailFfmpegPath)) {
		thePrefs.m_strVideoThumbnailFfmpegPath = strOldVideoThumbnailFfmpegPath;
		thePrefs.m_uVideoThumbnailIntervalSeconds = uOldVideoThumbnailIntervalSeconds;
		AfxMessageBox(GetResString(IDS_VIDEOTHUMBNAILS_INVALID), MB_ICONWARNING);
		return FALSE;
	}
	thePrefs.m_bMoviePreviewBackup = IsDlgButtonChecked(IDC_VIDEOBACKUP) != 0;
	thePrefs.m_bVideoPreviewThumbnails = false;
	if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL)
		theApp.emuledlg->transferwnd->GetDownloadList()->UpdateVideoThumbnailTimer();

	LoadSettings();
	SetModified(FALSE);
	return CPropertyPage::OnApply();
}

void CPPgFiles::Localize()
{
	if (m_hWnd) {
		SetWindowText(GetResString(IDS_PW_FILES));
		SetDlgItemText(IDC_LBL_MISC, GetResString(IDS_PW_MISC));
		SetDlgItemText(IDC_PF_TIMECALC, GetResString(IDS_PF_ADVANCEDCALC));
		SetDlgItemText(IDC_UAP, GetResString(IDS_PW_UAP));
		SetDlgItemText(IDC_DAP, GetResString(IDS_PW_DAP));
		SetDlgItemText(IDC_PREVIEWPRIO, GetResString(IDS_DOWNLOADMOVIECHUNKS));
		SetDlgItemText(IDC_ADDNEWFILESPAUSED, GetResString(IDS_ADDNEWFILESPAUSED));
		SetDlgItemText(IDC_WATCHCB, GetResString(IDS_PF_WATCHCB));
		SetDlgItemText(IDC_FULLCHUNKTRANS, GetResString(IDS_FULLCHUNKTRANS));
		SetDlgItemText(IDC_STARTNEXTFILE, GetResString(IDS_STARTNEXTFILE));
		SetDlgItemText(IDC_STARTNEXTFILECAT, GetResString(IDS_PREF_STARTNEXTFILECAT));
		SetDlgItemText(IDC_STARTNEXTFILECAT2, GetResString(IDS_PREF_STARTNEXTFILECATONLY));
		SetDlgItemText(IDC_FNC, GetResString(IDS_EDIT));
		SetDlgItemText(IDC_ONND, GetResString(IDS_ONNEWDOWNLOAD));
		SetDlgItemText(IDC_FNCLEANUP, GetResString(IDS_AUTOCLEANUPFN));
		SetDlgItemText(IDC_STATICVIDEOPLAYER, GetResString(IDS_PW_VIDEOPLAYER));
		SetDlgItemText(IDC_VIDEOPLAYER_CMD_LBL, GetResString(IDS_COMMAND));
		SetDlgItemText(IDC_VIDEOPLAYER_ARGS_LBL, GetResString(IDS_ARGUMENTS));
		SetDlgItemText(IDC_VIDEOBACKUP, GetResString(IDS_VIDEOBACKUP));
		SetDlgItemText(IDC_VIDEOTHUMBNAILS_GROUP, GetResString(IDS_VIDEOTHUMBNAILS));
		SetDlgItemText(IDC_THUMBNAIL_FFMPEG_LBL, GetResString(IDS_THUMBNAIL_FFMPEG));
		SetDlgItemText(IDC_THUMBNAIL_INTERVAL_LBL, GetResString(IDS_THUMBNAIL_INTERVAL_SECONDS));
		SetDlgItemText(IDC_FILECOMPLETE_GROUP, GetResString(IDS_COMPLETIONCOMMAND));
		SetDlgItemText(IDC_RUNONFILECOMPLETE, GetResString(IDS_RUNONFILECOMPLETE));
		SetDlgItemText(IDC_FILECOMPLETEPROGRAM_LBL, GetResString(IDS_COMMAND));
		SetDlgItemText(IDC_FILECOMPLETEARGS_LBL, GetResString(IDS_ARGUMENTS));
		SetDlgItemText(IDC_REMEMBERDOWNLOADED, GetResString(IDS_PW_REMEMBERDOWNLOADED));
		SetDlgItemText(IDC_REMEMBERCANCELLED, GetResString(IDS_PW_REMEMBERCANCELLED));
	}
}

void CPPgFiles::OnSetCleanupFilter()
{
	InputBox inputbox;
	inputbox.SetLabels(GetResString(IDS_FNFILTERTITLE), GetResString(IDS_FILTERFILENAMEWORD), thePrefs.GetFilenameCleanups());
	inputbox.DoModal();
	if (!inputbox.WasCancelled())
		thePrefs.SetFilenameCleanups(inputbox.GetInput());
}

void CPPgFiles::UpdateCompletionCommandControls()
{
	const BOOL bEnabled = IsDlgButtonChecked(IDC_RUNONFILECOMPLETE) != 0;
	EnableDlgItemIfPresent(*this, IDC_FILECOMPLETEPROGRAM_LBL, bEnabled);
	EnableDlgItemIfPresent(*this, IDC_FILECOMPLETEPROGRAM, bEnabled);
	EnableDlgItemIfPresent(*this, IDC_BROWSE_FILECOMPLETEPROGRAM, bEnabled);
	EnableDlgItemIfPresent(*this, IDC_FILECOMPLETEARGS_LBL, bEnabled);
	EnableDlgItemIfPresent(*this, IDC_FILECOMPLETEARGS, bEnabled);
}

void CPPgFiles::BrowseVideoplayer()
{
	CString strPlayerPath;
	GetDlgItemText(IDC_VIDEOPLAYER, strPlayerPath);
	if (DialogBrowseFile(strPlayerPath, _T("Executable (*.exe)|*.exe||"), (strPlayerPath.IsEmpty() ? NULL : strPlayerPath), OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY, true, GetSafeHwnd(), NULL, _T("exe"))) {
		SetDlgItemText(IDC_VIDEOPLAYER, strPlayerPath);
		SetModified();
	}
}

void CPPgFiles::BrowseThumbnailFfmpeg()
{
	CString strFfmpegPath;
	GetDlgItemText(IDC_THUMBNAIL_FFMPEG, strFfmpegPath);
	if (DialogBrowseFile(strFfmpegPath, _T("FFmpeg executable (ffmpeg.exe)|ffmpeg.exe|Executable (*.exe)|*.exe||"), (strFfmpegPath.IsEmpty() ? NULL : strFfmpegPath), OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY, true, GetSafeHwnd(), NULL, _T("exe"))) {
		SetDlgItemText(IDC_THUMBNAIL_FFMPEG, strFfmpegPath);
		SetModified();
	}
}

void CPPgFiles::BrowseFileCompletionProgram()
{
	CString strProgramPath;
	GetDlgItemText(IDC_FILECOMPLETEPROGRAM, strProgramPath);
	if (DialogBrowseFile(strProgramPath, _T("Executable (*.exe;*.com)|*.exe;*.com||"), (strProgramPath.IsEmpty() ? NULL : strProgramPath), OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY, true, GetSafeHwnd(), NULL, _T("exe"))) {
		SetDlgItemText(IDC_FILECOMPLETEPROGRAM, strProgramPath);
		SetModified();
	}
}

void CPPgFiles::OnHelp()
{
	theApp.ShowHelp(eMule_FAQ_Preferences_Files);
}

BOOL CPPgFiles::OnCommand(WPARAM wParam, LPARAM lParam)
{
	return (wParam == ID_HELP) ? OnHelpInfo(NULL) : __super::OnCommand(wParam, lParam);
}

BOOL CPPgFiles::OnHelpInfo(HELPINFO*)
{
	OnHelp();
	return TRUE;
}

BOOL CPPgFiles::PreTranslateMessage(MSG *pMsg)
{
	m_toolTip.RelayEvent(pMsg);
	return CPropertyPage::PreTranslateMessage(pMsg);
}

void CPPgFiles::OnSettingsChange()
{
	SetModified();
	EnableDlgItemIfPresent(*this, IDC_STARTNEXTFILECAT, IsDlgButtonChecked(IDC_STARTNEXTFILE));
	EnableDlgItemIfPresent(*this, IDC_STARTNEXTFILECAT2, IsDlgButtonChecked(IDC_STARTNEXTFILE));
	UpdateCompletionCommandControls();
}

void CPPgFiles::OnSettingsChangeCat(uint8 index)
{
	bool on = IsDlgButtonChecked(index == 1 ? IDC_STARTNEXTFILECAT : IDC_STARTNEXTFILECAT2) != 0;
	if (on)
		CheckDlgButton(index == 1 ? IDC_STARTNEXTFILECAT2 : IDC_STARTNEXTFILECAT, FALSE);
	OnSettingsChange();
}

void CPPgFiles::OnDestroy()
{
	CPropertyPage::OnDestroy();
	if (m_icoBrowse) {
		VERIFY(::DestroyIcon(m_icoBrowse));
		m_icoBrowse = NULL;
	}
}
