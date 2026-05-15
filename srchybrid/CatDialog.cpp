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
#include "CustomAutoComplete.h"
#include "SharedFileList.h"
#include "SharedFilesWnd.h"
#include "emuledlg.h"
#include "TransferDlg.h"
#include "CatDialog.h"
#include "OtherFunctions.h"
#include "PartFile.h"
#include "PathHelpers.h"
#include "UserMsgs.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define	REGULAREXPRESSIONS_STRINGS_PROFILE	_T("AC_VF_RegExpr.dat")

namespace
{
	UINT NormalizeCategoryPriority(UINT uPriority)
	{
		return uPriority <= PR_HIGH ? uPriority : PR_NORMAL;
	}

	bool CategoryTitleExists(const CString& strTitle, INT_PTR iExcludeCategory)
	{
		CString strCandidate(strTitle);
		strCandidate.Trim();
		if (strCandidate.IsEmpty())
			return false;

		for (INT_PTR i = 1; i < thePrefs.GetCatCount(); ++i) {
			if (i == iExcludeCategory)
				continue;
			const Category_Struct *pCategory = thePrefs.GetCategory(i);
			if (pCategory != NULL && pCategory->strTitle.CompareNoCase(strCandidate) == 0)
				return true;
		}
		return false;
	}
}

// CCatDialog dialog

IMPLEMENT_DYNAMIC(CCatDialog, CDialog)

BEGIN_MESSAGE_MAP(CCatDialog, CDialog)
	ON_BN_CLICKED(IDC_BROWSE, OnBnClickedBrowse)
	ON_BN_CLICKED(IDOK, OnBnClickedOk)
	ON_BN_CLICKED(IDC_REB, OnDDBnClicked)
	ON_MESSAGE(UM_CPN_SELENDOK, OnSelChange) //UM_CPN_SELCHANGE
END_MESSAGE_MAP()

CCatDialog::CCatDialog(int index)
	: CDialog(CCatDialog::IDD)
	, m_iCategory(index)
	, m_myCat(thePrefs.GetCategory(index))
	, m_pacRegExp()
	, m_newcolor(CLR_NONE)
{
}

CCatDialog::~CCatDialog()
{
	if (m_pacRegExp) {
		m_pacRegExp->SaveList(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + REGULAREXPRESSIONS_STRINGS_PROFILE);
		m_pacRegExp->Unbind();
		m_pacRegExp->Release();
	}
}

BOOL CCatDialog::OnInitDialog()
{
	CDialog::OnInitDialog();
	if (m_myCat == NULL) {
		EndDialog(IDCANCEL);
		return FALSE;
	}

	InitWindowStyles(this);
	Localize();
	m_ctlColor.SetDefaultColor(::GetSysColor(COLOR_BTNTEXT));
	UpdateData();

	m_pacRegExp = new CCustomAutoComplete();
	m_pacRegExp->AddRef();
	if (m_pacRegExp->Bind(::GetDlgItem(m_hWnd, IDC_REGEXP), ACO_UPDOWNKEYDROPSLIST | ACO_AUTOSUGGEST))
		m_pacRegExp->LoadList(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + REGULAREXPRESSIONS_STRINGS_PROFILE);

	if (theApp.m_fontSymbol.m_hObject) {
		GetDlgItem(IDC_REB)->SetFont(&theApp.m_fontSymbol);
		SetDlgItemText(IDC_REB, _T("6")); // show a down-arrow
	}

	return TRUE;
}

void CCatDialog::UpdateData()
{
	SetDlgItemText(IDC_TITLE, m_myCat->strTitle);
	SetDlgItemText(IDC_INCOMING, m_myCat->strIncomingPath);
	SetDlgItemText(IDC_COMMENT, m_myCat->strComment);

	if (m_myCat->filter == 18)
		SetDlgItemText(IDC_REGEXP, m_myCat->regexp);

	CheckDlgButton(IDC_REGEXPR, m_myCat->ac_regexpeval);

	m_newcolor = m_myCat->color;
	m_ctlColor.SetColor(m_myCat->color == CLR_NONE ? m_ctlColor.GetDefaultColor() : m_myCat->color);

	SetDlgItemText(IDC_AUTOCATEXT, m_myCat->autocat);

	m_prio.SetCurSel(NormalizeCategoryPriority(m_myCat->prio));
}

void CCatDialog::DoDataExchange(CDataExchange *pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_CATCOLOR, m_ctlColor);
	DDX_Control(pDX, IDC_PRIOCOMBO, m_prio);
}

void CCatDialog::Localize()
{
	SetDlgItemText(IDC_STATIC_TITLE, GetResString(IDS_TITLE));
	SetDlgItemText(IDC_STATIC_INCOMING, GetResString(IDS_PW_INCOMING) + _T("  ") + GetResString(IDS_SHAREWARNING));
	SetDlgItemText(IDC_STATIC_COMMENT, GetResString(IDS_COMMENT));
	SetDlgItemText(IDCANCEL, GetResString(IDS_CANCEL));
	SetDlgItemText(IDC_STATIC_COLOR, GetResString(IDS_COLOR));
	SetDlgItemText(IDC_STATIC_PRIO, GetResString(IDS_STARTPRIO));
	SetDlgItemText(IDC_STATIC_AUTOCAT, GetResString(IDS_AUTOCAT_LABEL));
	SetDlgItemText(IDC_REGEXPR, GetResString(IDS_ASREGEXPR));
	SetDlgItemText(IDOK, GetResString(IDS_TREEOPTIONS_OK));

	m_ctlColor.CustomText = GetResString(IDS_COL_MORECOLORS);
	m_ctlColor.DefaultText = GetResString(IDS_DEFAULT);

	SetWindowText(GetResString(IDS_EDITCAT));

	SetDlgItemText(IDC_STATIC_REGEXP, GetResString(IDS_STATIC_REGEXP));

	m_prio.ResetContent();
	m_prio.AddString(GetResString(IDS_PRIOLOW));
	m_prio.AddString(GetResString(IDS_PRIONORMAL));
	m_prio.AddString(GetResString(IDS_PRIOHIGH));
	m_prio.SetCurSel(NormalizeCategoryPriority(m_myCat->prio));
}

void CCatDialog::OnBnClickedBrowse()
{
	CString strIncomingPath;
	GetDlgItemText(IDC_INCOMING, strIncomingPath);
	if (SelectDir(strIncomingPath, GetSafeHwnd(), GetResString(IDS_SELECT_INCOMINGDIR)))
		SetDlgItemText(IDC_INCOMING, strIncomingPath);
}

void CCatDialog::OnBnClickedOk()
{
	Category_Struct proposed = *m_myCat;
	const CString oldpath(m_myCat->strIncomingPath);

	GetDlgItemText(IDC_TITLE, proposed.strTitle);
	proposed.strTitle.Trim();
	if (m_iCategory != 0 && proposed.strTitle.IsEmpty()) {
		ErrorBalloon(IDC_TITLE, IDS_ERR_CATEGORY_TITLE_REQUIRED);
		return;
	}
	if (m_iCategory != 0 && CategoryTitleExists(proposed.strTitle, m_iCategory)) {
		ErrorBalloon(IDC_TITLE, IDS_ERR_CATEGORY_DUPLICATE);
		return;
	}

	if (GetDlgItem(IDC_INCOMING)->GetWindowTextLength() > 2)
		GetDlgItemText(IDC_INCOMING, proposed.strIncomingPath);

	GetDlgItemText(IDC_COMMENT, proposed.strComment);

	proposed.strIncomingPath = PathHelpers::CanonicalizeDirectoryPath(proposed.strIncomingPath);
	if (!thePrefs.IsShareableDirectory(proposed.strIncomingPath))
		proposed.strIncomingPath = thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR);

	if (!LongPathSeams::PathExists(proposed.strIncomingPath) && !LongPathSeams::CreateDirectory(proposed.strIncomingPath, 0)) {
		ErrorBalloon(IDC_INCOMING, IDS_ERR_BADFOLDER);
		return;
	}

	if (!EqualPaths(proposed.strIncomingPath, oldpath)) {
		if (theApp.emuledlg != NULL && theApp.emuledlg->sharedfileswnd != NULL)
			(void)theApp.emuledlg->sharedfileswnd->Reload();
		else if (theApp.sharedfiles != NULL)
			theApp.sharedfiles->Reload();
	}

	proposed.color = m_newcolor;
	proposed.prio = NormalizeCategoryPriority(m_prio.GetCurSel());

	proposed.ac_regexpeval = IsDlgButtonChecked(IDC_REGEXPR) != 0;

	GetDlgItemText(IDC_AUTOCATEXT, proposed.autocat);
	if (proposed.ac_regexpeval && !IsRegExpValid(proposed.autocat)) {
		ErrorBalloon(IDC_AUTOCATEXT, IDS_ERR_REGEXP);
		return;
	}

	GetDlgItemText(IDC_REGEXP, proposed.regexp);
	if (proposed.regexp.GetLength() > 0) {
		if (!IsRegExpValid(proposed.regexp)) {
			ErrorBalloon(IDC_REGEXP, IDS_ERR_REGEXP);
			return;
		}
		if (m_pacRegExp && m_pacRegExp->IsBound()) {
			m_pacRegExp->AddItem(proposed.regexp, 0);
			proposed.filter = 18;
		}
	} else if (proposed.filter == 18) {
		// deactivate regexp
		proposed.filter = 0;
	}

	*m_myCat = proposed;
	theApp.emuledlg->transferwnd->GetDownloadList()->Invalidate();

	OnOK();
}

LRESULT CCatDialog::OnSelChange(WPARAM wParam, LPARAM)
{
	m_newcolor = (wParam == CLR_DEFAULT) ? CLR_NONE : m_ctlColor.GetColor();
	return 0;
}

void CCatDialog::OnDDBnClicked()
{
	CWnd *box = GetDlgItem(IDC_REGEXP);
	box->SetFocus();
	box->SetWindowText(_T(""));
	box->SendMessage(WM_KEYDOWN, VK_DOWN, 0x00510001);
}

void CCatDialog::ErrorBalloon(int iEdit, UINT uid)
{
	static_cast<CEdit*>(GetDlgItem(iEdit))->ShowBalloonTip(GetResString(IDS_ERROR), GetResString(uid), TTI_ERROR);
}
