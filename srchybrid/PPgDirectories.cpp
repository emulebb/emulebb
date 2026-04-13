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
#include "emuledlg.h"
#include "SharedFilesWnd.h"
#include "PPgDirectories.h"
#include "otherfunctions.h"
#include "PathHelpers.h"
#include "InputBox.h"
#include "Preferences.h"
#include "HelpIDs.h"
#include "UserMsgs.h"
#include "opcodes.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


IMPLEMENT_DYNAMIC(CPPgDirectories, CPropertyPage)

BEGIN_MESSAGE_MAP(CPPgDirectories, CPropertyPage)
	ON_BN_CLICKED(IDC_SELTEMPDIR, OnBnClickedSeltempdir)
	ON_BN_CLICKED(IDC_SELINCDIR, OnBnClickedSelincdir)
	ON_EN_CHANGE(IDC_INCFILES, OnSettingsChange)
	ON_EN_CHANGE(IDC_TEMPFILES, OnSettingsChange)
	ON_BN_CLICKED(IDC_UNCADD, OnBnClickedAddUNC)
	ON_WM_HELPINFO()
	ON_BN_CLICKED(IDC_SELTEMPDIRADD, OnBnClickedSeltempdiradd)
	ON_WM_DESTROY()
END_MESSAGE_MAP()

CPPgDirectories::CPPgDirectories()
	: CPropertyPage(CPPgDirectories::IDD)
	, m_icoBrowse()
{
}

void CPPgDirectories::DoDataExchange(CDataExchange *pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SHARESELECTOR, m_ShareSelector);
}

BOOL CPPgDirectories::OnInitDialog()
{
	CWaitCursor curWait; // initialization of that dialog may take a while
	CPropertyPage::OnInitDialog();
	InitWindowStyles(this);

	AddBuddyButton(GetDlgItem(IDC_INCFILES)->m_hWnd, ::GetDlgItem(m_hWnd, IDC_SELINCDIR));
	InitAttachedBrowseButton(::GetDlgItem(m_hWnd, IDC_SELINCDIR), m_icoBrowse);

	AddBuddyButton(GetDlgItem(IDC_TEMPFILES)->m_hWnd, ::GetDlgItem(m_hWnd, IDC_SELTEMPDIR));
	InitAttachedBrowseButton(::GetDlgItem(m_hWnd, IDC_SELTEMPDIR), m_icoBrowse);

	GetDlgItem(IDC_SELTEMPDIRADD)->ShowWindow(thePrefs.IsExtControlsEnabled() ? SW_SHOW : SW_HIDE);

	LoadSettings();
	Localize();

	return TRUE;  // return TRUE unless you set the focus to the control
				  // EXCEPTION: OCX Property Pages should return FALSE
}

void CPPgDirectories::LoadSettings()
{
	SetDlgItemText(IDC_INCFILES, thePrefs.m_strIncomingDir);

	CString tempfolders;
	for (INT_PTR i = 0; i < thePrefs.GetTempDirCount(); ++i) {
		if (i > 0)
			tempfolders += _T('|');
		tempfolders += thePrefs.GetTempDir(i);
	}
	SetDlgItemText(IDC_TEMPFILES, tempfolders);

	CStringList sharedDirs;
	thePrefs.CopySharedDirectoryList(sharedDirs);
	m_ShareSelector.SetSharedDirectories(sharedDirs);
}

void CPPgDirectories::OnBnClickedSelincdir()
{
	CString strIncomingPath;
	GetDlgItemText(IDC_INCFILES, strIncomingPath);
	if (SelectDir(strIncomingPath, GetSafeHwnd(), GetResString(IDS_SELECT_INCOMINGDIR)))
		SetDlgItemText(IDC_INCFILES, strIncomingPath);
}

void CPPgDirectories::OnBnClickedSeltempdir()
{
	CString strTempPath;
	GetDlgItemText(IDC_TEMPFILES, strTempPath);
	if (SelectDir(strTempPath, GetSafeHwnd(), GetResString(IDS_SELECT_TEMPDIR)))
		SetDlgItemText(IDC_TEMPFILES, strTempPath);
}

BOOL CPPgDirectories::OnApply()
{
	CString strIncomingDir;
	GetDlgItemText(IDC_INCFILES, strIncomingDir);
	strIncomingDir = PathHelpers::CanonicalizeDirectoryPath(strIncomingDir);
	if (strIncomingDir.IsEmpty()) {
		strIncomingDir = thePrefs.GetDefaultDirectory(EMULE_INCOMINGDIR, true); // will create the directory here if it doesn't exist
		SetDlgItemText(IDC_INCFILES, strIncomingDir);
	} else if (thePrefs.IsInstallationDirectory(strIncomingDir)) {
		ErrorBalloon(IDC_INCFILES, IDS_WRN_INCFILE_RESERVED);
		return FALSE;
	}

	const CString &sOldIncoming(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
	if (!EqualPaths(strIncomingDir, sOldIncoming) && !EqualPaths(strIncomingDir, thePrefs.GetDefaultDirectory(EMULE_INCOMINGDIR, false))) {
		// if the user chooses a non-default directory which already contains files,
		// inform him that all those files will be shared
		bool bExistingFile = false;
		DWORD dwEnumerateError = ERROR_SUCCESS;
		(void)PathHelpers::ForEachDirectoryEntry(strIncomingDir, [&](const WIN32_FIND_DATA &findData) -> bool {
			if ((findData.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_TEMPORARY)) != 0)
				return true;

			const ULONGLONG ullFoundFileSize = (static_cast<ULONGLONG>(findData.nFileSizeHigh) << 32) | findData.nFileSizeLow;
			if (ullFoundFileSize == 0 || ullFoundFileSize > MAX_EMULE_FILE_SIZE)
				return true;

			const CString strFoundFilePath(PathHelpers::AppendPathComponent(strIncomingDir, findData.cFileName));
			bExistingFile = !ShouldIgnoreSharedFileCandidate(strFoundFilePath, findData.cFileName);
			return !bExistingFile;
		}, &dwEnumerateError);
		if (bExistingFile && LocMessageBox(IDS_WRN_INCFILE_EXISTS, MB_OKCANCEL | MB_ICONINFORMATION, 0) == IDCANCEL)
			return FALSE;
	}

	// checking specified tempdir(s)
	CString strTempDir;
	GetDlgItemText(IDC_TEMPFILES, strTempDir);
	if (strTempDir.IsEmpty()) {
		strTempDir = thePrefs.GetDefaultDirectory(EMULE_TEMPDIR, true); // will create the directory if it doesn't exist
		SetDlgItemText(IDC_TEMPFILES, strTempDir);
	}

	bool testtempdirchanged = false;
	CStringArray temptempfolders;
	for (int iPos = 0; iPos >= 0;) {
		CString atmp(strTempDir.Tokenize(_T("|"), iPos));
		if (atmp.Trim().IsEmpty())
			continue;
		UINT uid;
		if (EqualPaths(strIncomingDir, atmp))
			uid = IDS_WRN_INCTEMP_SAME;
		else if (thePrefs.IsInstallationDirectory(atmp))
			uid = IDS_WRN_TEMPFILES_RESERVED;
		else
			uid = 0;
		if (uid) {
			ErrorBalloon(IDC_TEMPFILES, uid);
			return FALSE;
		}

		bool bDup = false;
		for (INT_PTR i = temptempfolders.GetCount(); --i >= 0;)	// avoid duplicate tempdirs
			if (EqualPaths(atmp, temptempfolders[i])) {
				bDup = true;
				break;
			}

		if (!bDup) {
			temptempfolders.Add(atmp);
			if (thePrefs.GetTempDirCount() < temptempfolders.GetCount()
				|| !EqualPaths(atmp, thePrefs.GetTempDir(temptempfolders.GetCount() - 1)))
			{
				testtempdirchanged = true;
			}
		}
	}

	if (temptempfolders.IsEmpty())
		temptempfolders.Add(thePrefs.GetDefaultDirectory(EMULE_TEMPDIR, true));

	if (temptempfolders.GetCount() != thePrefs.GetTempDirCount())
		testtempdirchanged = true;

	// applying tempdirs
	if (testtempdirchanged) {
		thePrefs.tempdir.RemoveAll();
		for (INT_PTR i = 0; i < temptempfolders.GetCount(); ++i) {
			CString toadd(PathHelpers::CanonicalizeDirectoryPath(temptempfolders[i]));
			if (!LongPathSeams::PathExists(toadd))
				LongPathSeams::CreateDirectory(toadd, NULL);
			if (LongPathSeams::PathExists(toadd))
				thePrefs.tempdir.Add(toadd);
		}
	}
	if (thePrefs.tempdir.IsEmpty())
		thePrefs.tempdir.Add(thePrefs.GetDefaultDirectory(EMULE_TEMPDIR, true));

	thePrefs.m_strIncomingDir = PathHelpers::CanonicalizeDirectoryPath(strIncomingDir);

	CStringList sharedDirs;
	m_ShareSelector.GetSharedDirectories(sharedDirs);

	// check shared directories for reserved folder names
	for (POSITION pos = sharedDirs.GetHeadPosition(); pos != NULL;) {
		POSITION posLast = pos;
		if (!thePrefs.IsShareableDirectory(sharedDirs.GetNext(pos)))
			sharedDirs.RemoveAt(posLast);
	}
	thePrefs.ReplaceSharedDirectoryList(sharedDirs);

	// on changing incoming dir, update directories for categories with the same path
	const CString strNewIncoming(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
	if (!EqualPaths(sOldIncoming, strNewIncoming)) {
		thePrefs.GetCategory(0)->strIncomingPath = strNewIncoming;
		bool bAskedOnce = false;
		const CString strOldIncomingCanonical(PathHelpers::CanonicalizeDirectoryPath(sOldIncoming));
		const CString strNewIncomingCanonical(PathHelpers::CanonicalizeDirectoryPath(strNewIncoming));
		for (INT_PTR cat = thePrefs.GetCatCount(); --cat > 0;) { //skip 0
			const CString strOldPath(PathHelpers::CanonicalizeDirectoryPath(thePrefs.GetCatPath(cat)));
			if (EqualPaths(strOldPath, strOldIncomingCanonical) || PathHelpers::IsPathWithinDirectory(strOldIncomingCanonical, strOldPath)) {
				if (!bAskedOnce) {
					bAskedOnce = true;
					if (LocMessageBox(IDS_UPDATECATINCOMINGDIRS, MB_YESNO, 0) == IDNO)
						break;
				}
				thePrefs.GetCategory(cat)->strIncomingPath = strNewIncomingCanonical + strOldPath.Mid(strOldIncomingCanonical.GetLength());
			}
		}
		thePrefs.SaveCats();
	}


	if (testtempdirchanged)
		LocMessageBox(IDS_SETTINGCHANGED_RESTART, MB_OK, 0);

	theApp.emuledlg->sharedfileswnd->Reload();

	SetModified(0);
	return CPropertyPage::OnApply();
}

BOOL CPPgDirectories::OnCommand(WPARAM wParam, LPARAM lParam)
{
	if (wParam == UM_ITEMSTATECHANGED)
		SetModified();
	else if (wParam == ID_HELP) {
		OnHelp();
		return TRUE;
	}
	return CPropertyPage::OnCommand(wParam, lParam);
}

void CPPgDirectories::Localize()
{
	if (m_hWnd) {
		SetWindowText(GetResString(IDS_PW_DIR));

		SetDlgItemText(IDC_INCOMING_FRM, GetResString(IDS_PW_INCOMING));
		SetDlgItemText(IDC_TEMP_FRM, GetResString(IDS_PW_TEMP));
		SetDlgItemText(IDC_SHARED_FRM, GetResString(IDS_PW_SHARED));
	}
}

void CPPgDirectories::OnBnClickedAddUNC()
{
	InputBox inputbox;
	inputbox.SetLabels(GetResString(IDS_UNCFOLDERS), GetResString(IDS_UNCFOLDERS), _T("\\\\Server\\Share"));
	if (inputbox.DoModal() != IDOK)
		return;
	CString unc(inputbox.GetInput());

	// basic UNC check
	if (!::PathIsUNC(unc)) {
		LocMessageBox(IDS_ERR_BADUNC, MB_ICONERROR, 0);
		return;
	}
	unc = PathHelpers::EnsureTrailingSeparator(unc);

	if (thePrefs.IsSharedDirectoryListed(unc))
		return;

	if (m_ShareSelector.AddUNCShare(unc))
		SetModified();
}

void CPPgDirectories::OnHelp()
{
	theApp.ShowHelp(eMule_FAQ_Preferences_Directories);
}

BOOL CPPgDirectories::OnHelpInfo(HELPINFO*)
{
	OnHelp();
	return TRUE;
}

void CPPgDirectories::OnBnClickedSeltempdiradd()
{
	CString paths;
	GetDlgItemText(IDC_TEMPFILES, paths);

	CString strTempPath;
	if (SelectDir(strTempPath, GetSafeHwnd(), GetResString(IDS_SELECT_TEMPDIR))) {
		paths.AppendFormat(_T("|%s"), (LPCTSTR)strTempPath);
		SetDlgItemText(IDC_TEMPFILES, paths);
	}
}

void CPPgDirectories::OnDestroy()
{
	CPropertyPage::OnDestroy();
	if (m_icoBrowse) {
		VERIFY(::DestroyIcon(m_icoBrowse));
		m_icoBrowse = NULL;
	}
}

void CPPgDirectories::ErrorBalloon(int iEdit, UINT uid)
{
	static_cast<CEdit*>(GetDlgItem(iEdit))->ShowBalloonTip(GetResString(IDS_ERROR), GetResString(uid), TTI_ERROR);
}
