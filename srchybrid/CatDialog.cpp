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

	CString FormatCategoryPriority(UINT uPriority)
	{
		switch (NormalizeCategoryPriority(uPriority)) {
		case PR_LOW:
			return GetResString(IDS_PRIOLOW);
		case PR_HIGH:
			return GetResString(IDS_PRIOHIGH);
		default:
			return GetResString(IDS_PRIONORMAL);
		}
	}

	CString FormatCategoryFilter(const Category_Struct& category)
	{
		CString strFilter;
		if (category.filterNeg)
			strFilter = _T("!");

		switch (category.filter) {
		case 0:
			strFilter += GetResString(IDS_ALL);
			break;
		case 1:
			strFilter += GetResString(IDS_ALLOTHERS);
			break;
		case 2:
			strFilter += GetResString(IDS_STATUS_NOTCOMPLETED);
			break;
		case 3:
			strFilter += GetResString(IDS_DL_TRANSFCOMPL);
			break;
		case 4:
			strFilter += GetResString(IDS_WAITING);
			break;
		case 5:
			strFilter += GetResString(IDS_DOWNLOADING);
			break;
		case 6:
			strFilter += GetResString(IDS_ERRORLIKE);
			break;
		case 7:
			strFilter += GetResString(IDS_PAUSED);
			break;
		case 8:
			strFilter += GetResString(IDS_SEENCOMPL);
			break;
		case 10:
			strFilter += GetResString(IDS_VIDEO);
			break;
		case 11:
			strFilter += GetResString(IDS_AUDIO);
			break;
		case 12:
			strFilter += GetResString(IDS_SEARCH_ARC);
			break;
		case 13:
			strFilter += GetResString(IDS_SEARCH_CDIMG);
			break;
		case 14:
			strFilter += GetResString(IDS_SEARCH_DOC);
			break;
		case 15:
			strFilter += GetResString(IDS_SEARCH_PICS);
			break;
		case 16:
			strFilter += GetResString(IDS_SEARCH_PRG);
			break;
		case 18:
			strFilter.AppendFormat(_T("\"%s\""), (LPCTSTR)category.regexp);
			break;
		case 20:
			strFilter += GetResString(IDS_SEARCH_EMULECOLLECTION);
			break;
		default:
			strFilter += GetResString(IDS_ALL);
			break;
		}
		return strFilter;
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

///////////////////////////////////////////////////////////////////////////////
// CCategoryManagerDialog

IMPLEMENT_DYNAMIC(CCategoryManagerDialog, CDialog)

BEGIN_MESSAGE_MAP(CCategoryManagerDialog, CDialog)
	ON_BN_CLICKED(IDC_CATMAN_ADD, OnAdd)
	ON_BN_CLICKED(IDC_CATMAN_EDIT, OnEdit)
	ON_BN_CLICKED(IDC_CATMAN_REMOVE, OnRemove)
	ON_BN_CLICKED(IDC_CATMAN_MOVE_UP, OnMoveUp)
	ON_BN_CLICKED(IDC_CATMAN_MOVE_DOWN, OnMoveDown)
	ON_BN_CLICKED(IDC_CATMAN_OPEN_INCOMING, OnOpenIncoming)
	ON_BN_CLICKED(IDC_CATMAN_REFRESH, OnRefresh)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_CATMAN_LIST, OnItemChanged)
	ON_NOTIFY(NM_DBLCLK, IDC_CATMAN_LIST, OnDoubleClick)
	ON_NOTIFY(LVN_KEYDOWN, IDC_CATMAN_LIST, OnKeyDown)
END_MESSAGE_MAP()

CCategoryManagerDialog::CCategoryManagerDialog(CTransferWnd *pTransferWnd)
	: CDialog(CCategoryManagerDialog::IDD)
	, m_pTransferWnd(pTransferWnd)
{
}

BOOL CCategoryManagerDialog::OnInitDialog()
{
	CDialog::OnInitDialog();
	InitWindowStyles(this);
	Localize();

	m_categoryList.SetExtendedStyle(m_categoryList.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);
	m_categoryList.InsertColumn(0, GetResString(IDS_TITLE), LVCFMT_LEFT, 110);
	m_categoryList.InsertColumn(1, GetResString(IDS_PW_INCOMING), LVCFMT_LEFT, 180);
	m_categoryList.InsertColumn(2, GetResString(IDS_STARTPRIO), LVCFMT_LEFT, 70);
	m_categoryList.InsertColumn(3, GetResString(IDS_CATEGORY_MANAGER_ASSIGNED), LVCFMT_RIGHT, 55);
	m_categoryList.InsertColumn(4, GetResString(IDS_CATEGORY_MANAGER_FILTER), LVCFMT_LEFT, 100);
	m_categoryList.InsertColumn(5, GetResString(IDS_CATEGORY_MANAGER_AUTOCAT), LVCFMT_LEFT, 110);
	RefreshCategoryList(0);
	return TRUE;
}

void CCategoryManagerDialog::DoDataExchange(CDataExchange *pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_CATMAN_LIST, m_categoryList);
}

void CCategoryManagerDialog::Localize()
{
	SetWindowText(GetResString(IDS_CATEGORY_MANAGER_TITLE));
	SetDlgItemText(IDC_CATMAN_ADD, GetResString(IDS_CAT_ADD));
	SetDlgItemText(IDC_CATMAN_EDIT, GetResString(IDS_CAT_EDIT));
	SetDlgItemText(IDC_CATMAN_REMOVE, GetResString(IDS_CAT_REMOVE));
	SetDlgItemText(IDC_CATMAN_MOVE_UP, GetResString(IDS_CATEGORY_MANAGER_MOVE_UP));
	SetDlgItemText(IDC_CATMAN_MOVE_DOWN, GetResString(IDS_CATEGORY_MANAGER_MOVE_DOWN));
	SetDlgItemText(IDC_CATMAN_OPEN_INCOMING, GetResString(IDS_OPENINC));
	SetDlgItemText(IDC_CATMAN_REFRESH, GetResString(IDS_SV_UPDATE));
	SetDlgItemText(IDCANCEL, GetResString(IDS_FD_CLOSE));
}

void CCategoryManagerDialog::RefreshCategoryList(INT_PTR iSelectCategory)
{
	if (iSelectCategory < 0)
		iSelectCategory = GetSelectedCategory();

	m_categoryList.DeleteAllItems();
	for (INT_PTR i = 0; i < thePrefs.GetCatCount(); ++i) {
		const Category_Struct *pCategory = thePrefs.GetCategory(i);
		if (pCategory == NULL)
			continue;

		CString strTitle(i == 0 && pCategory->strTitle.IsEmpty() ? GetResString(IDS_ALL) : pCategory->strTitle);
		const int iItem = m_categoryList.InsertItem(static_cast<int>(i), strTitle);
		m_categoryList.SetItemData(iItem, static_cast<DWORD_PTR>(i));
		m_categoryList.SetItemText(iItem, 1, pCategory->strIncomingPath);
		m_categoryList.SetItemText(iItem, 2, FormatCategoryPriority(pCategory->prio));
		CString strAssigned;
		strAssigned.Format(_T("%u"), m_pTransferWnd != NULL ? static_cast<UINT>(m_pTransferWnd->CountFilesAssignedToCategory(static_cast<UINT>(i))) : 0u);
		m_categoryList.SetItemText(iItem, 3, strAssigned);
		m_categoryList.SetItemText(iItem, 4, FormatCategoryFilter(*pCategory));
		m_categoryList.SetItemText(iItem, 5, pCategory->autocat);
		if (i == iSelectCategory)
			m_categoryList.SetItemState(iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	}
	UpdateButtons();
}

INT_PTR CCategoryManagerDialog::GetSelectedCategory() const
{
	POSITION pos = m_categoryList.GetFirstSelectedItemPosition();
	if (pos == NULL)
		return -1;
	const int iItem = m_categoryList.GetNextSelectedItem(pos);
	return static_cast<INT_PTR>(m_categoryList.GetItemData(iItem));
}

void CCategoryManagerDialog::UpdateButtons()
{
	const INT_PTR iCategory = GetSelectedCategory();
	const bool bHasCategory = iCategory >= 0 && iCategory < thePrefs.GetCatCount();
	const bool bCustomCategory = bHasCategory && iCategory > 0;
	const bool bCategoryUnused = bCustomCategory && m_pTransferWnd != NULL && m_pTransferWnd->CountFilesAssignedToCategory(static_cast<UINT>(iCategory)) == 0;
	GetDlgItem(IDC_CATMAN_EDIT)->EnableWindow(bCustomCategory);
	GetDlgItem(IDC_CATMAN_REMOVE)->EnableWindow(bCategoryUnused);
	GetDlgItem(IDC_CATMAN_MOVE_UP)->EnableWindow(bCustomCategory && iCategory > 1);
	GetDlgItem(IDC_CATMAN_MOVE_DOWN)->EnableWindow(bCustomCategory && iCategory < thePrefs.GetCatCount() - 1);
	GetDlgItem(IDC_CATMAN_OPEN_INCOMING)->EnableWindow(bHasCategory && thePrefs.GetCategory(iCategory) != NULL && !thePrefs.GetCategory(iCategory)->strIncomingPath.IsEmpty());
}

void CCategoryManagerDialog::OnAdd()
{
	if (m_pTransferWnd == NULL)
		return;
	const int iCategory = m_pTransferWnd->AddCategoryInteractive();
	RefreshCategoryList(iCategory);
}

void CCategoryManagerDialog::OnEdit()
{
	if (m_pTransferWnd == NULL)
		return;
	const INT_PTR iCategory = GetSelectedCategory();
	if (m_pTransferWnd->EditCategoryInteractive(iCategory))
		RefreshCategoryList(iCategory);
}

void CCategoryManagerDialog::OnRemove()
{
	if (m_pTransferWnd == NULL)
		return;
	const INT_PTR iCategory = GetSelectedCategory();
	if (m_pTransferWnd->RemoveCategoryInteractive(iCategory))
		RefreshCategoryList(0);
}

void CCategoryManagerDialog::OnMoveUp()
{
	if (m_pTransferWnd == NULL)
		return;
	const INT_PTR iCategory = GetSelectedCategory();
	if (iCategory > 1)
		RefreshCategoryList(m_pTransferWnd->MoveCategoryInteractive(iCategory, iCategory - 1));
}

void CCategoryManagerDialog::OnMoveDown()
{
	if (m_pTransferWnd == NULL)
		return;
	const INT_PTR iCategory = GetSelectedCategory();
	if (iCategory > 0 && iCategory < thePrefs.GetCatCount() - 1)
		RefreshCategoryList(m_pTransferWnd->MoveCategoryInteractive(iCategory, iCategory + 2));
}

void CCategoryManagerDialog::OnOpenIncoming()
{
	const INT_PTR iCategory = GetSelectedCategory();
	const Category_Struct *pCategory = thePrefs.GetCategory(iCategory);
	if (pCategory != NULL && !pCategory->strIncomingPath.IsEmpty())
		ShellOpenFile(pCategory->strIncomingPath);
}

void CCategoryManagerDialog::OnRefresh()
{
	RefreshCategoryList();
}

void CCategoryManagerDialog::OnItemChanged(NMHDR *, LRESULT *pResult)
{
	UpdateButtons();
	*pResult = 0;
}

void CCategoryManagerDialog::OnDoubleClick(NMHDR *, LRESULT *pResult)
{
	OnEdit();
	*pResult = 0;
}

void CCategoryManagerDialog::OnKeyDown(NMHDR *pNMHDR, LRESULT *pResult)
{
	const LPNMLVKEYDOWN pKeyDown = reinterpret_cast<LPNMLVKEYDOWN>(pNMHDR);
	const bool bAltDown = (::GetKeyState(VK_MENU) & 0x8000) != 0;
	switch (pKeyDown->wVKey) {
	case VK_INSERT:
		OnAdd();
		break;
	case VK_RETURN:
		OnEdit();
		break;
	case VK_DELETE:
		OnRemove();
		break;
	case VK_F5:
		OnRefresh();
		break;
	case VK_UP:
		if (bAltDown)
			OnMoveUp();
		break;
	case VK_DOWN:
		if (bAltDown)
			OnMoveDown();
		break;
	}
	*pResult = 0;
}
