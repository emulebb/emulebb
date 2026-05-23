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
#include <uxtheme.h>
#include "emule.h"
#include "emuledlg.h"
#include "SharedFilesCtrl.h"
#include "UpDownClient.h"
#include "FileInfoDialog.h"
#include "MetaDataDlg.h"
#include "ED2kLinkDlg.h"
#include "ArchivePreviewDlg.h"
#include "CommentDialog.h"
#include "HighColorTab.hpp"
#include "ListViewWalkerPropertySheet.h"
#include "UserMsgs.h"
#include "ResizableLib/ResizableSheet.h"
#include "SharedFileList.h"
#include "MemDC.h"
#include "PartFile.h"
#include "SharedFileIntakePolicy.h"
#include "MenuCmds.h"
#include "IrcWnd.h"
#include "SharedFilesWnd.h"
#include "SharedFilesCtrlSeams.h"
#include "SharedFilesWndSeams.h"
#include "Opcodes.h"
#include "InputBox.h"
#include "WebServices.h"
#include "TransferDlg.h"
#include "ClientList.h"
#include "Collection.h"
#include "CollectionCreateDialog.h"
#include "CollectionViewDialog.h"
#include "SearchParams.h"
#include "SearchDlg.h"
#include "SearchResultsWnd.h"
#include "OtherFunctions.h"
#include "FileListKeyboardShortcutsSeams.h"
#include "ProUserMenuCopySeams.h"
#include "MenuShortcutLabels.h"
#include "ToolTipCtrlX.h"
#include "kademlia/kademlia/kademlia.h"
#include "kademlia/kademlia/UDPFirewallTester.h"
#include "MediaInfo.h"
#include "Log.h"
#include "KnownFileList.h"
#include "PathHelpers.h"
#include <algorithm>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	constexpr UINT_PTR kStartupDeferredReloadTimerId = 0x5346;
	constexpr UINT kStartupDeferredReloadDelayMs = 500;
	constexpr UINT_PTR kVisibleFilePruneTimerId = 0x5347;
	constexpr UINT kVisibleFilePruneDelayMs = 250;

	int FindSubMenuItemPosition(CMenu &menu, HMENU hSubMenu)
	{
		const int itemCount = menu.GetMenuItemCount();
		for (int i = 0; i < itemCount; ++i) {
			MENUITEMINFO mii = {};
			mii.cbSize = sizeof(mii);
			mii.fMask = MIIM_SUBMENU;
			if (menu.GetMenuItemInfo(static_cast<UINT>(i), &mii, TRUE) && mii.hSubMenu == hSubMenu)
				return i;
		}
		return -1;
	}

	bool EnableSubMenuItem(CMenu &menu, HMENU hSubMenu, UINT state)
	{
		const int position = FindSubMenuItemPosition(menu, hSubMenu);
		if (position < 0)
			return false;
		return menu.EnableMenuItem(static_cast<UINT>(position), MF_BYPOSITION | state) != static_cast<UINT>(-1);
	}

	CString BuildSharedFilesDirectoryKey(const CString &strDirectory)
	{
		CString strKey(PathHelpers::EnsureTrailingSeparator(PathHelpers::CanonicalizePath(PathHelpers::StripExtendedLengthPrefix(strDirectory))));
		strKey.MakeLower();
		return strKey;
	}

	CString BuildSharedFilesPathKey(const CString &strPath)
	{
		CString strKey(PathHelpers::CanonicalizePath(PathHelpers::StripExtendedLengthPrefix(strPath)));
		strKey.MakeLower();
		return strKey;
	}

	bool AreSharedFilesDirectoriesEqual(const CString &strLeft, const CString &strRight)
	{
		return BuildSharedFilesDirectoryKey(strLeft) == BuildSharedFilesDirectoryKey(strRight);
	}

	bool AreSharedFilesPathsEqual(const CString &strLeft, const CString &strRight)
	{
		return BuildSharedFilesPathKey(strLeft) == BuildSharedFilesPathKey(strRight);
	}

	CString FormatUploadRatio(float fRatio)
	{
		CString str;
		str.Format(_T("%.1f"), fRatio);
		return str;
	}

	int CompareRatio(float fLeft, float fRight)
	{
		if (fLeft < fRight)
			return -1;
		if (fLeft > fRight)
			return 1;
		return 0;
	}

	int CompareTimestampUndefinedAtBottom(uint64 uTime1, uint64 uTime2, bool bSortAscending)
	{
		if (uTime1 == 0) {
			if (uTime2 == 0)
				return 0;
			return bSortAscending ? 1 : -1;
		}
		if (uTime2 == 0)
			return bSortAscending ? -1 : 1;
		return CompareUnsigned(uTime1, uTime2);
	}

	CString FormatSharedFileTimestamp(uint64 uTimestamp)
	{
		if (uTimestamp == 0 || uTimestamp > 0x7fffffffffffffffui64)
			return CString();

		const __time64_t tTimestamp = static_cast<__time64_t>(uTimestamp);
		struct tm tmLocal = {};
		if (_localtime64_s(&tmLocal, &tTimestamp) != 0)
			return CString();

		TCHAR szTimestamp[128];
		if (_tcsftime(szTimestamp, _countof(szTimestamp), thePrefs.GetDateTimeFormat4Lists(), &tmLocal) == 0)
			return CString();
		return CString(szTimestamp);
	}

	bool TryCanonicalizeDroppedSharedPath(const CString &rstrOriginalPath, CString &rstrCanonicalPath, DWORD &rdwCanonicalizeError)
	{
		const CString strNormalizedInput(PathHelpers::NormalizePathSeparators(rstrOriginalPath));
		if (!LongPathSeams::PathExists(strNormalizedInput)) {
			rdwCanonicalizeError = ERROR_FILE_NOT_FOUND;
			rstrCanonicalPath.Empty();
			return false;
		}

		if (!PathHelpers::TryCanonicalizeExistingPath(strNormalizedInput, rstrCanonicalPath, &rdwCanonicalizeError))
			return false;

		if (strNormalizedInput.CompareNoCase(rstrCanonicalPath) != 0)
			DEBUG_ONLY(DebugLog(_T("Canonicalized dropped shared path: \"%s\" -> \"%s\""), (LPCTSTR)strNormalizedInput, (LPCTSTR)rstrCanonicalPath));
		return true;
	}

	bool IsSharedFileHashingActiveForReload()
	{
		return theApp.sharedfiles != NULL && theApp.sharedfiles->HasSharedHashingWork();
	}

	struct SharedFilesRepositionState
	{
		CArray<CShareableFile*, CShareableFile*> aSelectedItems;
		CShareableFile *pFocusedItem = NULL;
		CShareableFile *pSelectionMarkItem = NULL;
		int iTopIndex = 0;
		int iHScrollPosition = 0;
	};

	void CaptureSharedFilesRepositionState(CSharedFilesCtrl &listCtrl, SharedFilesRepositionState &state)
	{
		for (POSITION pos = listCtrl.GetFirstSelectedItemPosition(); pos != NULL;) {
			const int iSelectedIndex = listCtrl.GetNextSelectedItem(pos);
			CShareableFile *pSelectedFile = listCtrl.GetFileByIndex(iSelectedIndex);
			if (pSelectedFile != NULL)
				state.aSelectedItems.Add(pSelectedFile);
		}

		state.pFocusedItem = listCtrl.GetFileByIndex(listCtrl.GetNextItem(-1, LVNI_FOCUSED));
		state.pSelectionMarkItem = listCtrl.GetFileByIndex(listCtrl.GetSelectionMark());
		state.iTopIndex = max(0, listCtrl.GetTopIndex());

		SCROLLINFO siHorz = { sizeof(siHorz) };
		if (listCtrl.GetScrollInfo(SB_HORZ, &siHorz, SIF_POS))
			state.iHScrollPosition = siHorz.nPos;
	}

	void RestoreSharedFilesTopIndex(CSharedFilesCtrl &listCtrl, int iTopIndex)
	{
		const int iItemCount = listCtrl.GetItemCount();
		if (iItemCount <= 0)
			return;

		iTopIndex = max(0, min(iTopIndex, iItemCount - 1));
		if (iTopIndex == 0) {
			listCtrl.EnsureVisible(0, FALSE);
			return;
		}

		const int iRowsPerPage = listCtrl.GetCountPerPage();
		if (iRowsPerPage > 0) {
			int iBottomIndex = iTopIndex + iRowsPerPage - 1;
			if (iBottomIndex >= iItemCount)
				iBottomIndex = iItemCount - 1;
			listCtrl.EnsureVisible(iBottomIndex, FALSE);
		}
		else
			listCtrl.EnsureVisible(iTopIndex, FALSE);

		const int iCurrentTop = listCtrl.GetTopIndex();
		if (iCurrentTop == iTopIndex || iCurrentTop < 0)
			return;

		CRect rcTop;
		CRect rcNext;
		if (!listCtrl.GetItemRect(iCurrentTop, &rcTop, LVIR_BOUNDS) || iCurrentTop + 1 >= iItemCount || !listCtrl.GetItemRect(iCurrentTop + 1, &rcNext, LVIR_BOUNDS))
			return;

		const int iRowHeight = rcNext.top - rcTop.top;
		if (iRowHeight > 0)
			listCtrl.Scroll(CSize(0, (iTopIndex - iCurrentTop) * iRowHeight));
	}

	void RestoreSharedFilesHorizontalScroll(CSharedFilesCtrl &listCtrl, int iHScrollPosition)
	{
		SCROLLINFO siHorz = { sizeof(siHorz) };
		if (!listCtrl.GetScrollInfo(SB_HORZ, &siHorz, SIF_POS | SIF_RANGE | SIF_PAGE))
			return;

		int iMaxHScroll = siHorz.nMax;
		if (siHorz.nPage > 0)
			iMaxHScroll = max(siHorz.nMin, siHorz.nMax - static_cast<int>(siHorz.nPage) + 1);

		iHScrollPosition = max(siHorz.nMin, min(iHScrollPosition, iMaxHScroll));
		if (siHorz.nPos != iHScrollPosition)
			listCtrl.Scroll(CSize(iHScrollPosition - siHorz.nPos, 0));
	}

	void RestoreSharedFilesRepositionState(CSharedFilesCtrl &listCtrl, const SharedFilesRepositionState &state)
	{
		listCtrl.SetItemState(-1, 0, LVIS_FOCUSED | LVIS_SELECTED);

		int iFirstSelectedIndex = -1;
		for (INT_PTR i = 0; i < state.aSelectedItems.GetCount(); ++i) {
			const int iSelectedIndex = listCtrl.FindFile(state.aSelectedItems[i]);
			if (iSelectedIndex < 0)
				continue;

			listCtrl.SetItemState(iSelectedIndex, LVIS_SELECTED, LVIS_SELECTED);
			if (iFirstSelectedIndex == -1 || iSelectedIndex < iFirstSelectedIndex)
				iFirstSelectedIndex = iSelectedIndex;
		}

		int iSelectionMarkIndex = listCtrl.FindFile(state.pSelectionMarkItem);
		if (iSelectionMarkIndex < 0)
			iSelectionMarkIndex = iFirstSelectedIndex;
		listCtrl.SetSelectionMark(iSelectionMarkIndex);

		int iFocusedIndex = listCtrl.FindFile(state.pFocusedItem);
		if (iFocusedIndex < 0)
			iFocusedIndex = iSelectionMarkIndex;
		if (iFocusedIndex < 0)
			iFocusedIndex = iFirstSelectedIndex;
		if (iFocusedIndex >= 0 && iFocusedIndex < listCtrl.GetItemCount())
			listCtrl.SetItemState(iFocusedIndex, LVIS_FOCUSED, LVIS_FOCUSED);

		RestoreSharedFilesTopIndex(listCtrl, state.iTopIndex);
		RestoreSharedFilesHorizontalScroll(listCtrl, state.iHScrollPosition);
	}

	class CSharedFilesSelectionRestoreGuard
	{
	public:
		explicit CSharedFilesSelectionRestoreGuard(CSharedFilesCtrl &listCtrl)
			: m_listCtrl(listCtrl)
		{
			m_listCtrl.SetSelectionRestoreInProgress(true);
		}

		~CSharedFilesSelectionRestoreGuard()
		{
			m_listCtrl.SetSelectionRestoreInProgress(false);
		}

	private:
		CSharedFilesCtrl &m_listCtrl;
	};

	void NotifyStartupSharedFilesModelChanged()
	{
#if EMULE_COMPILED_STARTUP_PROFILING
		if (theApp.emuledlg != NULL && theApp.emuledlg->sharedfileswnd != NULL)
			theApp.emuledlg->sharedfileswnd->OnStartupSharedFilesModelChanged();
#endif
	}
}

bool NeedArchiveInfoPage(const CSimpleArray<CObject*> *paItems);
void UpdateFileDetailsPages(CListViewPropertySheet *pSheet
	, CResizablePage *pArchiveInfo, CResizablePage *pMediaInfo, CResizablePage *pFileLink);


//////////////////////////////////////////////////////////////////////////////
// CSharedFileDetailsSheet

class CSharedFileDetailsSheet : public CListViewWalkerPropertySheet
{
	DECLARE_DYNAMIC(CSharedFileDetailsSheet)

	void Localize();
public:
	CSharedFileDetailsSheet(CTypedPtrList<CPtrList, CShareableFile*> &aFiles, UINT uInvokePage = 0, CListCtrlItemWalk *pListCtrl = NULL);

protected:
	CArchivePreviewDlg	m_wndArchiveInfo;
	CCommentDialog		m_wndFileComments;
	CED2kLinkDlg		m_wndFileLink;
	CFileInfoDialog		m_wndMediaInfo;
	CMetaDataDlg		m_wndMetaData;

	UINT m_uInvokePage;
	static LPCTSTR m_pPshStartPage;

	void UpdateTitle();

	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
	virtual BOOL OnInitDialog();

	DECLARE_MESSAGE_MAP()
	afx_msg void OnDestroy();
	afx_msg LRESULT OnDataChanged(WPARAM, LPARAM);
};

LPCTSTR CSharedFileDetailsSheet::m_pPshStartPage;

IMPLEMENT_DYNAMIC(CSharedFileDetailsSheet, CListViewWalkerPropertySheet)

BEGIN_MESSAGE_MAP(CSharedFileDetailsSheet, CListViewWalkerPropertySheet)
	ON_WM_DESTROY()
	ON_MESSAGE(UM_DATA_CHANGED, OnDataChanged)
END_MESSAGE_MAP()

void CSharedFileDetailsSheet::Localize()
{
	m_wndMediaInfo.Localize();
	SetTabTitle(IDS_CONTENT_INFO, &m_wndMediaInfo, this);
	m_wndMetaData.Localize();
	SetTabTitle(IDS_META_DATA, &m_wndMetaData, this);
	m_wndFileLink.Localize();
	SetTabTitle(IDS_SW_LINK, &m_wndFileLink, this);
	m_wndFileComments.Localize();
	SetTabTitle(IDS_COMMENT, &m_wndFileComments, this);
	m_wndArchiveInfo.Localize();
	SetTabTitle(IDS_CONTENT_INFO, &m_wndArchiveInfo, this);
}

CSharedFileDetailsSheet::CSharedFileDetailsSheet(CTypedPtrList<CPtrList, CShareableFile*> &aFiles, UINT uInvokePage, CListCtrlItemWalk *pListCtrl)
	: CListViewWalkerPropertySheet(pListCtrl)
	, m_uInvokePage(uInvokePage)
{
	for (POSITION pos = aFiles.GetHeadPosition(); pos != NULL;)
		m_aItems.Add(aFiles.GetNext(pos));
	m_psh.dwFlags &= ~PSH_HASHELP;

	m_wndFileComments.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndFileComments.m_psp.dwFlags |= PSP_USEICONID;
	m_wndFileComments.m_psp.pszIcon = _T("FileComments");
	m_wndFileComments.SetFiles(&m_aItems);
	AddPage(&m_wndFileComments);

	m_wndArchiveInfo.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndArchiveInfo.m_psp.dwFlags |= PSP_USEICONID;
	m_wndArchiveInfo.m_psp.pszIcon = _T("ARCHIVE_PREVIEW");
	m_wndArchiveInfo.SetFiles(&m_aItems);

	m_wndMediaInfo.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndMediaInfo.m_psp.dwFlags |= PSP_USEICONID;
	m_wndMediaInfo.m_psp.pszIcon = _T("MEDIAINFO");
	m_wndMediaInfo.SetFiles(&m_aItems);
	if (NeedArchiveInfoPage(&m_aItems))
		AddPage(&m_wndArchiveInfo);
	else
		AddPage(&m_wndMediaInfo);

	m_wndMetaData.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndMetaData.m_psp.dwFlags |= PSP_USEICONID;
	m_wndMetaData.m_psp.pszIcon = _T("METADATA");
	if (m_aItems.GetSize() == 1) {
		m_wndMetaData.SetFiles(&m_aItems);
		AddPage(&m_wndMetaData);
	}

	m_wndFileLink.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndFileLink.m_psp.dwFlags |= PSP_USEICONID;
	m_wndFileLink.m_psp.pszIcon = _T("ED2KLINK");
	m_wndFileLink.SetFiles(&m_aItems);
	AddPage(&m_wndFileLink);

	LPCTSTR pPshStartPage = m_pPshStartPage;
	if (m_uInvokePage != 0)
		pPshStartPage = MAKEINTRESOURCE(m_uInvokePage);
	for (int i = (int)m_pages.GetCount(); --i >= 0;)
		if (GetPage(i)->m_psp.pszTemplate == pPshStartPage) {
			m_psh.nStartPage = i;
			break;
		}
}

void CSharedFileDetailsSheet::OnDestroy()
{
	if (m_uInvokePage == 0)
		m_pPshStartPage = GetPage(GetActiveIndex())->m_psp.pszTemplate;
	CListViewWalkerPropertySheet::OnDestroy();
}

BOOL CSharedFileDetailsSheet::OnInitDialog()
{
	EnableStackedTabs(FALSE);
	BOOL bResult = CListViewWalkerPropertySheet::OnInitDialog();
	HighColorTab::UpdateImageList(*this);
	InitWindowStyles(this);
	EnableSaveRestore(_T("SharedFileDetailsSheet")); // call this after(!) OnInitDialog
	Localize();
	UpdateTitle();
	return bResult;
}

LRESULT CSharedFileDetailsSheet::OnDataChanged(WPARAM, LPARAM)
{
	UpdateTitle();
	UpdateFileDetailsPages(this, &m_wndArchiveInfo, &m_wndMediaInfo, &m_wndFileLink);
	return 1;
}

void CSharedFileDetailsSheet::UpdateTitle()
{
	CString sTitle(GetResString(IDS_DETAILS));
	if (m_aItems.GetSize() == 1)
		sTitle.AppendFormat(_T(": %s"), (LPCTSTR)(static_cast<CAbstractFile*>(m_aItems[0])->GetFileName()));
	SetWindowText(sTitle);
}

BOOL CSharedFileDetailsSheet::OnCommand(WPARAM wParam, LPARAM lParam)
{
	if (wParam == ID_APPLY_NOW) {
		CSharedFilesCtrl *pSharedFilesCtrl = DYNAMIC_DOWNCAST(CSharedFilesCtrl, m_pListCtrl->GetListCtrl());
		if (pSharedFilesCtrl)
			for (int i = m_aItems.GetSize(); --i >= 0;)
				// so, and why does this not(!) work while the sheet is open ??
				pSharedFilesCtrl->UpdateFile(DYNAMIC_DOWNCAST(CKnownFile, m_aItems[i]));
	}
	return CListViewWalkerPropertySheet::OnCommand(wParam, lParam);
}


//////////////////////////////////////////////////////////////////////////////
// CSharedFilesCtrl

IMPLEMENT_DYNAMIC(CSharedFilesCtrl, CMuleListCtrl)

BEGIN_MESSAGE_MAP(CSharedFilesCtrl, CMuleListCtrl)
	ON_NOTIFY_REFLECT(LVN_COLUMNCLICK, OnLvnColumnClick)
	ON_NOTIFY_REFLECT(LVN_GETDISPINFO, OnLvnGetDispInfo)
	ON_NOTIFY_REFLECT(LVN_GETINFOTIP, OnLvnGetInfoTip)
	ON_NOTIFY_REFLECT(NM_DBLCLK, OnNmDblClk)
	ON_NOTIFY_REFLECT_EX(NM_CLICK, OnNMClick)
	ON_WM_CONTEXTMENU()
	ON_WM_KEYDOWN()
	ON_WM_SYSCOLORCHANGE()
	ON_WM_MOUSEMOVE()
	ON_WM_TIMER()
END_MESSAGE_MAP()

CSharedFilesCtrl::CSharedFilesCtrl()
	: CListCtrlItemWalk(this)
	, m_aSortBySecondValue()
	, m_bModelBound(false)
	, m_pDirectoryFilter()
	, nAICHHashing()
	, m_pHighlightedItem()
	, m_bSelectionRestoreInProgress(false)
	, m_bStartupDeferredReloadPending(false)
	, m_bVisibleFilePrunePending(false)
{
	SetGeneralPurposeFind(true);
	m_pToolTip = new CToolTipCtrlX;
	SetSkinKey(_T("SharedFilesLv"));
}

CSharedFilesCtrl::~CSharedFilesCtrl()
{
	while (!liTempShareableFilesInDir.IsEmpty())	// delete shareable files
		delete liTempShareableFilesInDir.RemoveHead();
	delete m_pToolTip;
}

void CSharedFilesCtrl::Init()
{
	SetPrefsKey(_T("SharedFilesCtrl"));
	ASSERT((GetStyle() & LVS_OWNERDATA) != 0);
	SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	ASSERT((GetStyle() & LVS_SINGLESEL) == 0);

	InsertColumn(0,		_T(""),	LVCFMT_LEFT,	DFLT_FILENAME_COL_WIDTH);			//IDS_DL_FILENAME
	InsertColumn(1,		_T(""),	LVCFMT_RIGHT,	DFLT_SIZE_COL_WIDTH);				//IDS_DL_SIZE
	InsertColumn(2,		_T(""),	LVCFMT_LEFT,	DFLT_FILETYPE_COL_WIDTH);			//IDS_TYPE
	InsertColumn(3,		_T(""),	LVCFMT_LEFT,	DFLT_PRIORITY_COL_WIDTH);			//IDS_PRIORITY
	InsertColumn(4,		_T(""),	LVCFMT_LEFT,	DFLT_HASH_COL_WIDTH, -1, true);		//IDS_FILEID
	InsertColumn(5,		_T(""),	LVCFMT_RIGHT,	100);								//IDS_SF_REQUESTS
	InsertColumn(6,		_T(""),	LVCFMT_LEFT,	140);								//IDS_SF_LASTREQUEST
	InsertColumn(7,		_T(""),	LVCFMT_RIGHT,	100);								//IDS_SF_ACCEPTS
	InsertColumn(8,		_T(""),	LVCFMT_RIGHT,	120);								//IDS_SF_TRANSFERRED
	InsertColumn(9,		_T(""),	LVCFMT_LEFT,	DFLT_PARTSTATUS_COL_WIDTH);			//IDS_SHARED_STATUS
	InsertColumn(10,	_T(""),	LVCFMT_LEFT,	DFLT_FOLDER_COL_WIDTH);			//IDS_FOLDER
	InsertColumn(11,	_T(""),	LVCFMT_RIGHT,	60);								//IDS_COMPLSOURCES
	InsertColumn(12,	_T(""),	LVCFMT_LEFT,	100);								//IDS_SHAREDTITLE
	InsertColumn(13,	_T(""),	LVCFMT_LEFT,	DFLT_ARTIST_COL_WIDTH, -1, true);	//IDS_ARTIST
	InsertColumn(14,	_T(""),	LVCFMT_LEFT,	DFLT_ALBUM_COL_WIDTH, -1, true);	//IDS_ALBUM
	InsertColumn(15,	_T(""),	LVCFMT_LEFT,	DFLT_TITLE_COL_WIDTH, -1, true);	//IDS_TITLE
	InsertColumn(16,	_T(""),	LVCFMT_RIGHT,	DFLT_LENGTH_COL_WIDTH, -1, true);	//IDS_LENGTH
	InsertColumn(17,	_T(""),	LVCFMT_RIGHT,	DFLT_BITRATE_COL_WIDTH, -1, true);	//IDS_BITRATE
	InsertColumn(18,	_T(""),	LVCFMT_LEFT,	DFLT_CODEC_COL_WIDTH, -1, true);	//IDS_CODEC
	InsertColumn(19,	_T(""),	LVCFMT_RIGHT,	85);								//IDS_ALL_TIME_RATIO
	InsertColumn(20,	_T(""),	LVCFMT_RIGHT,	85);								//IDS_SESSION_RATIO

	if (const auto *pProfile = MuleListCtrlViewPresets::FindProfile(_T("SharedFilesCtrl")))
		SetViewPresetProfile(*pProfile);
	SetAllIcons();
	CreateMenus();
	LoadSettings();

	m_aSortBySecondValue[0] = true; // Requests:			Sort by 2nd value by default
	m_aSortBySecondValue[1] = true; // Accepted Requests:	Sort by 2nd value by default
	m_aSortBySecondValue[2] = true; // Transferred Data:	Sort by 2nd value by default
	m_aSortBySecondValue[3] = false; // Shared ED2K|Kad:	Sort by 1st value by default
	if (GetSortItem() == 5)
		m_aSortBySecondValue[0] = GetSortSecondValue();
	else if (GetSortItem() == 7)
		m_aSortBySecondValue[1] = GetSortSecondValue();
	else if (GetSortItem() == 8)
		m_aSortBySecondValue[2] = GetSortSecondValue();
	else if (GetSortItem() == 12)
		m_aSortBySecondValue[3] = GetSortSecondValue();
	SetSortArrow();

	CToolTipCtrl *tooltip = GetToolTips();
	if (tooltip) {
		m_pToolTip->SetFileIconToolTip(true);
		m_pToolTip->SubclassWindow(*tooltip);
		tooltip->ModifyStyle(0, TTS_NOPREFIX);
		tooltip->SetDelayTime(TTDT_AUTOPOP, SEC2MS(20));
		tooltip->SetDelayTime(TTDT_INITIAL, SEC2MS(thePrefs.GetToolTipDelay()));
	}

	m_ShareDropTarget.SetParent(this);
	VERIFY(m_ShareDropTarget.Register(this));
}

void CSharedFilesCtrl::OnSysColorChange()
{
	CMuleListCtrl::OnSysColorChange();
	SetAllIcons();
	CreateMenus();
}

void CSharedFilesCtrl::SetAllIcons()
{
	ApplyImageList(NULL);
	m_ImageList.DeleteImageList();
	m_ImageList.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	m_ImageList.Add(CTempIconLoader(_T("EMPTY")));			//0
	m_ImageList.Add(CTempIconLoader(_T("FileSharedServer")));//1
	m_ImageList.Add(CTempIconLoader(_T("FileSharedKad")));	//2
	m_ImageList.Add(CTempIconLoader(_T("Rating_NotRated")));//3
	m_ImageList.Add(CTempIconLoader(_T("Rating_Fake")));	//4
	m_ImageList.Add(CTempIconLoader(_T("Rating_Poor")));	//5
	m_ImageList.Add(CTempIconLoader(_T("Rating_Fair")));	//6
	m_ImageList.Add(CTempIconLoader(_T("Rating_Good")));	//7
	m_ImageList.Add(CTempIconLoader(_T("Rating_Excellent")));//8
	m_ImageList.Add(CTempIconLoader(_T("Collection_Search"))); //9 rating for comments are searched on kad
	m_ImageList.SetOverlayImage(m_ImageList.Add(CTempIconLoader(_T("FileCommentsOvl"))), 1);
	// Apply the image list also to the listview control, even if we use our own 'DrawItem'.
	// This is needed to give the listview control a chance to initialize the row height.
	ASSERT((GetStyle() & LVS_SHAREIMAGELISTS) != 0);
	VERIFY(ApplyImageList(m_ImageList) == NULL);
}

void CSharedFilesCtrl::Localize()
{
	static const UINT uids[21] =
	{
		IDS_DL_FILENAME, IDS_DL_SIZE, IDS_TYPE, IDS_PRIORITY, IDS_FILEID
		, IDS_SF_REQUESTS, IDS_SF_LASTREQUEST, IDS_SF_ACCEPTS, IDS_SF_TRANSFERRED, IDS_SHARED_STATUS
		, IDS_FOLDER, IDS_COMPLSOURCES, IDS_SHAREDTITLE, IDS_ARTIST, IDS_ALBUM
		, IDS_TITLE, IDS_LENGTH, IDS_BITRATE, IDS_CODEC, IDS_ALL_TIME_RATIO, IDS_SESSION_RATIO
	};

	LocaliseHeaderCtrl(uids, _countof(uids));

	CreateMenus();

	if ((GetStyle() & LVS_OWNERDATA) != 0)
		RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
	else
		for (int i = GetItemCount(); --i >= 0;)
			Update(i);

	ShowFilesCount();
}

DWORD_PTR CSharedFilesCtrl::GetVirtualItemData(int iItem) const
{
	const CShareableFile *pFile = GetFileByIndex(iItem);
	return reinterpret_cast<DWORD_PTR>(pFile);
}

int CSharedFilesCtrl::GetVirtualItemCount() const
{
	return static_cast<int>(m_aVisibleFiles.size());
}

CShareableFile* CSharedFilesCtrl::GetFileByIndex(int iItem) const
{
	if (iItem < 0 || iItem >= static_cast<int>(m_aVisibleFiles.size()))
		return NULL;
	return m_aVisibleFiles[static_cast<size_t>(iItem)];
}

void CSharedFilesCtrl::CollectSelectedFiles(CTypedPtrList<CPtrList, CShareableFile*> &rSelectedFiles) const
{
	rSelectedFiles.RemoveAll();
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		const int iSelectedIndex = GetNextSelectedItem(pos);
		CShareableFile *pSelectedFile = GetFileByIndex(iSelectedIndex);
		if (pSelectedFile != NULL)
			rSelectedFiles.AddTail(pSelectedFile);
	}
}

CShareableFile* CSharedFilesCtrl::GetSingleSelectedFile() const
{
	if (GetSelectedCount() != 1)
		return NULL;

	POSITION pos = GetFirstSelectedItemPosition();
	if (pos == NULL)
		return NULL;

	return GetFileByIndex(GetNextSelectedItem(pos));
}

void CSharedFilesCtrl::RebuildVisibleFileIndex()
{
	m_mapVisibleFileIndex.RemoveAll();
	for (int i = 0; i < static_cast<int>(m_aVisibleFiles.size()); ++i)
		m_mapVisibleFileIndex.SetAt(m_aVisibleFiles[static_cast<size_t>(i)], i);
}

void CSharedFilesCtrl::UpdateVisibleFileIndexRange(int iStartIndex, int iEndIndex)
{
	const SharedFilesCtrlSeams::VisibleIndexRange range = SharedFilesCtrlSeams::GetMovedVisibleIndexRange(iStartIndex, iEndIndex, static_cast<int>(m_aVisibleFiles.size()));
	if (!range.bValid)
		return;

	for (int i = range.iFirst; i <= range.iLast; ++i)
		m_mapVisibleFileIndex.SetAt(m_aVisibleFiles[static_cast<size_t>(i)], i);
}

void CSharedFilesCtrl::ClearVisibleFiles()
{
	if (m_bVisibleFilePrunePending) {
		KillTimer(kVisibleFilePruneTimerId);
		m_bVisibleFilePrunePending = false;
	}
	m_aVisibleFiles.clear();
	m_mapVisibleFileIndex.RemoveAll();
}

bool CSharedFilesCtrl::IsLiveVisibleFilePointer(const CShareableFile *file) const
{
	if (file == NULL)
		return false;

	const CKnownFile *pKnownFile = static_cast<const CKnownFile*>(file);
	if (theApp.sharedfiles != NULL && theApp.sharedfiles->IsFilePtrInList(pKnownFile))
		return true;

	if (theApp.knownfiles != NULL && theApp.knownfiles->IsFilePtrInList(pKnownFile))
		return true;

	for (POSITION pos = liTempShareableFilesInDir.GetHeadPosition(); pos != NULL;) {
		if (liTempShareableFilesInDir.GetNext(pos) == file)
			return true;
	}

	return false;
}

bool CSharedFilesCtrl::PruneStaleVisibleFiles()
{
	const size_t uOldSize = m_aVisibleFiles.size();
	m_aVisibleFiles.erase(std::remove_if(m_aVisibleFiles.begin(), m_aVisibleFiles.end(),
		[this](const CShareableFile *file) {
			return !IsLiveVisibleFilePointer(file);
		}), m_aVisibleFiles.end());

	if (m_pHighlightedItem != NULL && !IsLiveVisibleFilePointer(m_pHighlightedItem))
		m_pHighlightedItem = NULL;

	if (m_aVisibleFiles.size() == uOldSize)
		return false;

	RebuildVisibleFileIndex();
	ApplyVisibleFileCount();
	return true;
}

void CSharedFilesCtrl::ScheduleVisibleFilePrune()
{
	if (!m_bModelBound || theApp.IsClosing() || GetSafeHwnd() == NULL || m_bVisibleFilePrunePending)
		return;

	m_bVisibleFilePrunePending = true;
	if (SetTimer(kVisibleFilePruneTimerId, kVisibleFilePruneDelayMs, NULL) == 0)
		FlushVisibleFilePrune();
}

void CSharedFilesCtrl::FlushVisibleFilePrune()
{
	if (!m_bVisibleFilePrunePending)
		return;

	KillTimer(kVisibleFilePruneTimerId);
	m_bVisibleFilePrunePending = false;
	if (PruneStaleVisibleFiles())
		Invalidate(FALSE);
}

void CSharedFilesCtrl::ApplyVisibleFileCount()
{
	const int iVisibleFileCount = static_cast<int>(min(m_aVisibleFiles.size(), static_cast<size_t>(INT_MAX)));
	if (CListCtrl::GetItemCount() == iVisibleFileCount)
		return;
	SetItemCountEx(iVisibleFileCount, LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
#if EMULE_COMPILED_STARTUP_PROFILING
	if (!theApp.IsClosing()) {
		CString strPhase;
		strPhase.Format(_T("CSharedFilesCtrl::ApplyVisibleFileCount model=%d control=%d"),
			iVisibleFileCount,
			CListCtrl::GetItemCount());
		theApp.AppendStartupProfileLine(strPhase, 0);
	}
#endif
}

bool CSharedFilesCtrl::ShouldPreserveVirtualListState() const
{
	return m_pDirectoryFilter == NULL || m_pDirectoryFilter->m_eItemType != SDI_UNSHAREDDIRECTORY;
}

bool CSharedFilesCtrl::ShouldDisplayFile(const CShareableFile *file) const
{
	if (file == NULL)
		return false;

	if (m_pDirectoryFilter != NULL) {
		ASSERT(file->IsKindOf(RUNTIME_CLASS(CKnownFile)) || m_pDirectoryFilter->m_eItemType == SDI_UNSHAREDDIRECTORY);
		switch (m_pDirectoryFilter->m_eItemType) {
		case SDI_ALL:
			break;
		case SDI_FILESYSTEMPARENT:
			return false;
		case SDI_UNSHAREDDIRECTORY:
			if (file->IsPartFile())
				return false;
		case SDI_NO:
		case SDI_CATINCOMING:
			if (!AreSharedFilesDirectoriesEqual(file->GetSharedDirectory(), m_pDirectoryFilter->m_strFullPath))
				return false;
			break;
		case SDI_TEMP:
			if (!file->IsPartFile())
				return false;
			if (m_pDirectoryFilter->m_nCatFilter != -1 && (UINT)m_pDirectoryFilter->m_nCatFilter != static_cast<const CPartFile*>(file)->GetCategory())
				return false;
			break;
		case SDI_DIRECTORY:
			if (file->IsPartFile())
				return false;
			if (AreSharedFilesDirectoriesEqual(file->GetSharedDirectory(), thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR)))
				return false;
			break;
		case SDI_INCOMING:
			if (!AreSharedFilesDirectoriesEqual(file->GetSharedDirectory(), thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR)))
				return false;
		}
	}

	return !IsFilteredOut(file);
}

void CSharedFilesCtrl::SortVisibleFiles()
{
	if (!HasActiveSortOrder()) {
		RebuildVisibleFileIndex();
		return;
	}

	const LPARAM lParamSort = MAKELONG(GetSortItem() + (GetSortSecondValue() ? 100 : 0), !GetSortAscending());
	std::stable_sort(m_aVisibleFiles.begin(), m_aVisibleFiles.end(),
		[lParamSort](const CShareableFile *pLeft, const CShareableFile *pRight) {
			return SortProc(reinterpret_cast<LPARAM>(pLeft), reinterpret_cast<LPARAM>(pRight), lParamSort) < 0;
		});
	RebuildVisibleFileIndex();
}

bool CSharedFilesCtrl::HasActiveSortOrder() const
{
	return GetSortItem() != -1 && m_aVisibleFiles.size() >= 2;
}

bool CSharedFilesCtrl::NeedsSortReposition(int iIndex) const
{
	if (iIndex < 0 || iIndex >= static_cast<int>(m_aVisibleFiles.size()))
		return false;

	const CShareableFile *pFile = m_aVisibleFiles[static_cast<size_t>(iIndex)];
	if (pFile == NULL)
		return false;

	const LPARAM lParamSort = MAKELONG(GetSortItem() + (GetSortSecondValue() ? 100 : 0), !GetSortAscending());
	if (iIndex > 0 && SortProc(reinterpret_cast<LPARAM>(m_aVisibleFiles[static_cast<size_t>(iIndex - 1)]), reinterpret_cast<LPARAM>(pFile), lParamSort) > 0)
		return true;
	if (iIndex + 1 < static_cast<int>(m_aVisibleFiles.size()) && SortProc(reinterpret_cast<LPARAM>(pFile), reinterpret_cast<LPARAM>(m_aVisibleFiles[static_cast<size_t>(iIndex + 1)]), lParamSort) > 0)
		return true;
	return false;
}

bool CSharedFilesCtrl::RepositionFileByCurrentSort(CShareableFile *file, int iIndex)
{
	if (file == NULL || iIndex < 0 || iIndex >= static_cast<int>(m_aVisibleFiles.size()) || m_aVisibleFiles[static_cast<size_t>(iIndex)] != file)
		return false;

	const LPARAM lParamSort = MAKELONG(GetSortItem() + (GetSortSecondValue() ? 100 : 0), !GetSortAscending());
	const bool bMoveLeft = iIndex > 0 && SortProc(reinterpret_cast<LPARAM>(m_aVisibleFiles[static_cast<size_t>(iIndex - 1)]), reinterpret_cast<LPARAM>(file), lParamSort) > 0;
	const bool bMoveRight = !bMoveLeft && iIndex + 1 < static_cast<int>(m_aVisibleFiles.size()) && SortProc(reinterpret_cast<LPARAM>(file), reinterpret_cast<LPARAM>(m_aVisibleFiles[static_cast<size_t>(iIndex + 1)]), lParamSort) > 0;
	if (!bMoveLeft && !bMoveRight)
		return false;

	SharedFilesRepositionState savedState;
	CaptureSharedFilesRepositionState(*this, savedState);

	auto sortFunc = [lParamSort](const CShareableFile *pLeft, const CShareableFile *pRight) {
		return SortProc(reinterpret_cast<LPARAM>(pLeft), reinterpret_cast<LPARAM>(pRight), lParamSort) < 0;
	};

	SetRedraw(false);

	int iNewIndex = iIndex;
	if (bMoveLeft) {
		std::vector<CShareableFile*>::iterator itNew = std::lower_bound(m_aVisibleFiles.begin(), m_aVisibleFiles.begin() + iIndex, file, sortFunc);
		iNewIndex = static_cast<int>(std::distance(m_aVisibleFiles.begin(), itNew));
		std::rotate(itNew, m_aVisibleFiles.begin() + iIndex, m_aVisibleFiles.begin() + iIndex + 1);
	}
	else {
		std::vector<CShareableFile*>::iterator itNew = std::upper_bound(m_aVisibleFiles.begin() + iIndex + 1, m_aVisibleFiles.end(), file, sortFunc);
		iNewIndex = static_cast<int>(std::distance(m_aVisibleFiles.begin(), itNew)) - 1;
		std::rotate(m_aVisibleFiles.begin() + iIndex, m_aVisibleFiles.begin() + iIndex + 1, itNew);
	}

	const int iStartIndex = min(iIndex, iNewIndex);
	const int iEndIndex = max(iIndex, iNewIndex);
	UpdateVisibleFileIndexRange(iStartIndex, iEndIndex);
	{
		CSharedFilesSelectionRestoreGuard guard(*this);
		RestoreSharedFilesRepositionState(*this, savedState);
	}
	SetRedraw(true);
	RedrawItems(iStartIndex, iEndIndex);
	return true;
}

void CSharedFilesCtrl::AppendVisibleFile(CShareableFile *file, const bool bUpdateIndex)
{
	if (file == NULL || !ShouldDisplayFile(file))
		return;

	int iExistingIndex = -1;
	if (bUpdateIndex && m_mapVisibleFileIndex.Lookup(file, iExistingIndex) && iExistingIndex >= 0)
		return;

	m_aVisibleFiles.push_back(file);
	if (bUpdateIndex)
		m_mapVisibleFileIndex.SetAt(file, static_cast<int>(m_aVisibleFiles.size() - 1));
}

void CSharedFilesCtrl::AddFile(const CShareableFile *file)
{
	if (!m_bModelBound)
		return;
	if (theApp.IsClosing())
		return;
	ScheduleVisibleFilePrune();
	if (!IsLiveVisibleFilePointer(file))
		return;
	if (!ShouldDisplayFile(file))
		return;
	if (FindFile(file) >= 0) {
		// in the file system view the shared status might have changed so we need to update the item to redraw the checkbox
		if (m_pDirectoryFilter != NULL && m_pDirectoryFilter->m_eItemType == SDI_UNSHAREDDIRECTORY)
			UpdateFile(file);
		return;
	}

	// if we are in the file system view, this might be a CKnownFile which has to replace a CShareableFile
	// (in case we start sharing this file), so make sure to replace the old one instead of adding a new
	if (m_pDirectoryFilter != NULL && m_pDirectoryFilter->m_eItemType == SDI_UNSHAREDDIRECTORY && file->IsKindOf(RUNTIME_CLASS(CKnownFile)))
		for (POSITION pos = liTempShareableFilesInDir.GetHeadPosition(); pos != NULL;) {
			const CShareableFile *pFile = liTempShareableFilesInDir.GetNext(pos);
			if (AreSharedFilesPathsEqual(pFile->GetFilePath(), file->GetFilePath())) {
				int iOldFile = FindFile(pFile);
				if (iOldFile >= 0) {
					m_aVisibleFiles[static_cast<size_t>(iOldFile)] = const_cast<CShareableFile*>(file);
					RebuildVisibleFileIndex();
					RedrawItems(iOldFile, iOldFile);
					ShowFilesCount();
					NotifyStartupSharedFilesModelChanged();
					return;
				}
			}
		}

	SharedFilesRepositionState savedState;
	const bool bPreserveState = ShouldPreserveVirtualListState();
	if (bPreserveState)
		CaptureSharedFilesRepositionState(*this, savedState);

	m_aVisibleFiles.push_back(const_cast<CShareableFile*>(file));
	SortVisibleFiles();
	ApplyVisibleFileCount();
	if (bPreserveState) {
		CSharedFilesSelectionRestoreGuard guard(*this);
		RestoreSharedFilesRepositionState(*this, savedState);
	}
	const int iItem = FindFile(file);
	if (iItem >= 0) {
		RedrawItems(iItem, iItem);
		if (!bPreserveState)
			EnsureVisible(iItem, FALSE);
	}
	ShowFilesCount();
	NotifyStartupSharedFilesModelChanged();
}

void CSharedFilesCtrl::RemoveFile(const CShareableFile *file, bool bDeletedFromDisk)
{
	if (!m_bModelBound)
		return;
	ScheduleVisibleFilePrune();
	int iItem = FindFile(file);
	if (iItem >= 0) {
		if (!bDeletedFromDisk && m_pDirectoryFilter != NULL && m_pDirectoryFilter->m_eItemType == SDI_UNSHAREDDIRECTORY)
			// in the file system view we usually don't need to remove a file, if it becomes unshared it will
			// still be visible as its still in the file system and the knownfile object doesn't get deleted neither
			// so to avoid having to reload the whole list we just update it instead of removing and re-finding
			UpdateFile(file);
		else {
			SharedFilesRepositionState savedState;
			const bool bPreserveState = ShouldPreserveVirtualListState();
			if (bPreserveState)
				CaptureSharedFilesRepositionState(*this, savedState);
			m_aVisibleFiles.erase(m_aVisibleFiles.begin() + iItem);
			RebuildVisibleFileIndex();
			ApplyVisibleFileCount();
			if (bPreserveState) {
				CSharedFilesSelectionRestoreGuard guard(*this);
				RestoreSharedFilesRepositionState(*this, savedState);
			}
			if (GetFirstSelectedItemPosition() == NULL)
				AutoSelectItem();
			theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails(false);
			Invalidate(FALSE);
		}
		ShowFilesCount();
		NotifyStartupSharedFilesModelChanged();
	}
}

void CSharedFilesCtrl::UpdateFile(const CShareableFile *file, bool bUpdateFileSummary)
{
	if (!m_bModelBound)
		return;
	if (theApp.IsClosing())
		return;
	if (IsLiveVisibleFilePointer(file)) {
		int iItem = FindFile(file);
		if (!ShouldDisplayFile(file)) {
			if (iItem >= 0)
				RemoveFile(file, false);
			return;
		}
		if (iItem < 0) {
			AddFile(file);
			return;
		}
		if (iItem >= 0) {
			bool bItemMoved = false;
			if (ShouldPreserveVirtualListState() && HasActiveSortOrder() && NeedsSortReposition(iItem))
				bItemMoved = RepositionFileByCurrentSort(const_cast<CShareableFile*>(file), iItem);
			if (!bItemMoved)
				RedrawItems(iItem, iItem);
			else
				iItem = FindFile(file);
			if (bUpdateFileSummary && iItem >= 0 && GetItemState(iItem, LVIS_SELECTED))
				theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails(true); //force update
		}
	}
}

void CSharedFilesCtrl::UpdateFilesAfterPublishedED2KBatch()
{
	if (!m_bModelBound)
		return;
	if (theApp.IsClosing())
		return;
	if (m_aVisibleFiles.empty())
		return;

	SharedFilesRepositionState savedState;
	const bool bPreserveState = ShouldPreserveVirtualListState() && HasActiveSortOrder();
	if (bPreserveState)
		CaptureSharedFilesRepositionState(*this, savedState);

	if (HasActiveSortOrder())
		SortVisibleFiles();

	if (bPreserveState) {
		CSharedFilesSelectionRestoreGuard guard(*this);
		RestoreSharedFilesRepositionState(*this, savedState);
	}

	Invalidate(FALSE);
	if (GetFirstSelectedItemPosition() != NULL)
		theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails(true);
}

int CSharedFilesCtrl::FindFile(const CShareableFile *pFile)
{
	int iItem = -1;
	return (pFile != NULL && m_mapVisibleFileIndex.Lookup(pFile, iItem)) ? iItem : -1;
}

void CSharedFilesCtrl::EnsureModelBound()
{
	if (m_bModelBound)
		return;

#if EMULE_COMPILED_STARTUP_PROFILING
	const ULONGLONG ullPhaseStart = theApp.GetStartupProfileTimestampUs();
#endif
	m_bModelBound = true;
	ReloadFileList();
#if EMULE_COMPILED_STARTUP_PROFILING
	{
		CString strPhase;
		strPhase.Format(_T("CSharedFilesCtrl model bind (%u shared, %d visible)"),
			static_cast<unsigned>(theApp.sharedfiles->GetCount()),
			static_cast<int>(m_aVisibleFiles.size()));
		theApp.AppendStartupProfileLine(strPhase, theApp.GetStartupProfileElapsedUs(ullPhaseStart));
		theApp.AppendStartupProfileCounter(_T("shared.list.model_shared_files"), static_cast<ULONGLONG>(theApp.sharedfiles->GetCount()), _T("files"));
		theApp.AppendStartupProfileCounter(_T("shared.list.model_visible_rows"), static_cast<ULONGLONG>(m_aVisibleFiles.size()), _T("rows"));
	}
#endif
	NotifyStartupSharedFilesModelChanged();
}

void CSharedFilesCtrl::ScheduleStartupDeferredReload()
{
	if (!m_bModelBound || theApp.IsClosing() || GetSafeHwnd() == NULL)
		return;
	if (m_bStartupDeferredReloadPending)
		return;

	m_bStartupDeferredReloadPending = true;
	if (SetTimer(kStartupDeferredReloadTimerId, kStartupDeferredReloadDelayMs, NULL) == 0
		&& SharedFilesWndSeams::ShouldRunStartupDeferredListReload(m_bStartupDeferredReloadPending, IsSharedFileHashingActiveForReload()))
	{
		m_bStartupDeferredReloadPending = false;
		ReloadFileList();
	}
}

void CSharedFilesCtrl::FlushStartupDeferredReload()
{
	if (!m_bModelBound || theApp.IsClosing())
		return;
	if (!m_bStartupDeferredReloadPending)
		return;
	if (!SharedFilesWndSeams::ShouldRunStartupDeferredListReload(m_bStartupDeferredReloadPending, IsSharedFileHashingActiveForReload()))
		return;

	KillTimer(kStartupDeferredReloadTimerId);
	m_bStartupDeferredReloadPending = false;
	ReloadFileList();
}

void CSharedFilesCtrl::ReloadFileList()
{
	if (!m_bModelBound)
		return;
	if (m_bStartupDeferredReloadPending) {
		KillTimer(kStartupDeferredReloadTimerId);
		m_bStartupDeferredReloadPending = false;
	}
#if EMULE_COMPILED_STARTUP_PROFILING
	const ULONGLONG ullReloadStart = theApp.GetStartupProfileTimestampUs();
	ULONGLONG ullPhaseStart = ullReloadStart;
#endif
	SetRedraw(false);
	SharedFilesRepositionState savedState;
	const bool bPreserveState = ShouldPreserveVirtualListState() && !m_aVisibleFiles.empty();
	if (bPreserveState)
		CaptureSharedFilesRepositionState(*this, savedState);
	ClearVisibleFiles();
	m_aVisibleFiles.reserve(static_cast<size_t>(theApp.sharedfiles->GetCount()));
	ApplyVisibleFileCount();
	theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails();

	for (const CKnownFilesMap::CPair *pair = theApp.sharedfiles->m_Files_map.PGetFirstAssoc(); pair != NULL; pair = theApp.sharedfiles->m_Files_map.PGetNextAssoc(pair))
		AppendVisibleFile(pair->value, false);
#if EMULE_COMPILED_STARTUP_PROFILING
	{
		CString strPhase;
		strPhase.Format(_T("CSharedFilesCtrl::ReloadFileList shared map append (%u shared -> %d visible pre-extra)"),
			static_cast<unsigned>(theApp.sharedfiles->GetCount()),
			static_cast<int>(m_aVisibleFiles.size()));
		theApp.AppendStartupProfileLine(strPhase, theApp.GetStartupProfileElapsedUs(ullPhaseStart));
		theApp.AppendStartupProfileCounter(_T("shared.list.shared_map_files"), static_cast<ULONGLONG>(theApp.sharedfiles->GetCount()), _T("files"));
		theApp.AppendStartupProfileCounter(_T("shared.list.visible_rows_pre_extra"), static_cast<ULONGLONG>(m_aVisibleFiles.size()), _T("rows"));
		ullPhaseStart = theApp.GetStartupProfileTimestampUs();
	}
#endif

	if (m_pDirectoryFilter != NULL && m_pDirectoryFilter->m_eItemType == SDI_UNSHAREDDIRECTORY && !m_pDirectoryFilter->m_strFullPath.IsEmpty())
		AddShareableFiles(m_pDirectoryFilter->m_strFullPath);
	else
		while (!liTempShareableFilesInDir.IsEmpty())	// clear temp file list
			delete liTempShareableFilesInDir.RemoveHead();
#if EMULE_COMPILED_STARTUP_PROFILING
	{
		CString strPhase;
		strPhase.Format(_T("CSharedFilesCtrl::ReloadFileList extra view append (filter=%d rows=%d)"),
			m_pDirectoryFilter != NULL ? static_cast<int>(m_pDirectoryFilter->m_eItemType) : -1,
			static_cast<int>(m_aVisibleFiles.size()));
		theApp.AppendStartupProfileLine(strPhase, theApp.GetStartupProfileElapsedUs(ullPhaseStart));
		theApp.AppendStartupProfileCounter(_T("shared.list.active_filter"), m_pDirectoryFilter != NULL ? static_cast<ULONGLONG>(static_cast<unsigned>(m_pDirectoryFilter->m_eItemType)) : 0ui64, _T("filter"));
		theApp.AppendStartupProfileCounter(_T("shared.list.visible_rows_post_extra"), static_cast<ULONGLONG>(m_aVisibleFiles.size()), _T("rows"));
		ullPhaseStart = theApp.GetStartupProfileTimestampUs();
	}
#endif

	SortVisibleFiles();
	ApplyVisibleFileCount();
	if (bPreserveState) {
		CSharedFilesSelectionRestoreGuard guard(*this);
		RestoreSharedFilesRepositionState(*this, savedState);
	}
	SetRedraw(true);
	Invalidate(FALSE);
	theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails(false);
	ShowFilesCount();
#if EMULE_COMPILED_STARTUP_PROFILING
	{
		CString strPhase;
		strPhase.Format(_T("CSharedFilesCtrl::ReloadFileList sort/apply complete (%d rows)"), static_cast<int>(m_aVisibleFiles.size()));
		const ULONGLONG ullVisibleRows = static_cast<ULONGLONG>(m_aVisibleFiles.size());
		const ULONGLONG ullSharedFileCount = static_cast<ULONGLONG>(theApp.sharedfiles->GetCount());
		theApp.AppendStartupProfileLine(strPhase, theApp.GetStartupProfileElapsedUs(ullPhaseStart));
		theApp.AppendStartupProfileCounter(_T("shared.list.visible_rows"), ullVisibleRows, _T("rows"));
		theApp.AppendStartupProfileCounter(_T("shared.list.hidden_shared_files"), ullSharedFileCount >= ullVisibleRows ? (ullSharedFileCount - ullVisibleRows) : 0ui64, _T("files"));
		theApp.AppendStartupProfileLine(_T("CSharedFilesCtrl::ReloadFileList total"), theApp.GetStartupProfileElapsedUs(ullReloadStart));
	}
#endif
	NotifyStartupSharedFilesModelChanged();
}

void CSharedFilesCtrl::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == kStartupDeferredReloadTimerId) {
		KillTimer(kStartupDeferredReloadTimerId);
		if (!SharedFilesWndSeams::ShouldRunStartupDeferredListReload(m_bStartupDeferredReloadPending, IsSharedFileHashingActiveForReload()))
			return;
		m_bStartupDeferredReloadPending = false;
		ReloadFileList();
		return;
	}
	if (nIDEvent == kVisibleFilePruneTimerId) {
		FlushVisibleFilePrune();
		return;
	}

	CMuleListCtrl::OnTimer(nIDEvent);
}

void CSharedFilesCtrl::ShowFilesCount()
{
	if (!m_bModelBound)
		return;
	CString str;
	if (theApp.sharedfiles->GetHashingCount() + nAICHHashing > 0)
		str.Format(_T(" (%i, %s %i)"), (int)theApp.sharedfiles->GetCount(), (LPCTSTR)GetResString(IDS_HASHING), (int)(theApp.sharedfiles->GetHashingCount() + nAICHHashing));
	else
		str.Format(_T(" (%i)"), (int)theApp.sharedfiles->GetCount());
	theApp.emuledlg->sharedfileswnd->SetDlgItemText(IDC_TRAFFIC_TEXT, GetResString(IDS_SF_FILES) + str);
	theApp.emuledlg->sharedfileswnd->UpdateReloadButtonState();
}

void CSharedFilesCtrl::ApplyAICHHashingCount(INT_PTR nVal)
{
	SetAICHHashing(nVal);
	ShowFilesCount();
}

void CSharedFilesCtrl::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	CShareableFile *file = GetFileByIndex(static_cast<int>(lpDrawItemStruct->itemID));
	if (file == NULL || theApp.IsClosing())
		return;

	CRect rcItem(lpDrawItemStruct->rcItem);
	CMemoryDC dc(CDC::FromHandle(lpDrawItemStruct->hDC), rcItem);
	BOOL bCtrlFocused;
	InitItemMemDC(dc, lpDrawItemStruct, bCtrlFocused);
	RECT rcClient;
	GetClientRect(&rcClient);
	CKnownFile *pKnownFile = file->IsKindOf(RUNTIME_CLASS(CKnownFile)) ? static_cast<CKnownFile*>(file) : NULL;

	const CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();
	int iCount = pHeaderCtrl->GetItemCount();
	LONG itemLeft = rcItem.left;
	int iIconDrawWidth = theApp.GetSmallSytemIconSize().cx;
	LONG iIconY = max((rcItem.Height() - theApp.GetSmallSytemIconSize().cy - 1) / 2, 0);
	for (int iCurrent = 0; iCurrent < iCount; ++iCurrent) {
		int iColumn = pHeaderCtrl->OrderToIndex(iCurrent);
		if (IsColumnHidden(iColumn))
			continue;

		UINT uDrawTextAlignment;
		int iColumnWidth = GetColumnWidth(iColumn, uDrawTextAlignment);
		rcItem.left = itemLeft;
		rcItem.right = itemLeft + iColumnWidth;
		if (rcItem.left < rcItem.right && HaveIntersection(rcClient, rcItem)) {
			const CString &sItem(GetItemDisplayText(file, iColumn));
			switch (iColumn) {
			case 0: //file name
				{
					rcItem.left += sm_iIconOffset;
					LONG rcIconTop = rcItem.top + iIconY;
					if (CheckBoxesEnabled()) {
						CHECKBOXSTATES iState;
						int iNoStyleState;
						// no interaction with default shared directories
						if (theApp.sharedfiles->ShouldBeShared(file->GetSharedDirectory(), file->GetFilePath(), true))
						{
							iState = CBS_CHECKEDDISABLED;
							iNoStyleState = DFCS_CHECKED | DFCS_INACTIVE;
						} else if (theApp.sharedfiles->ShouldBeShared(file->GetSharedDirectory(), file->GetFilePath(), false)) {
							iState = (file == m_pHighlightedItem) ? CBS_CHECKEDHOT : CBS_CHECKEDNORMAL;
							iNoStyleState = (file == m_pHighlightedItem) ? DFCS_PUSHED | DFCS_CHECKED : DFCS_CHECKED;
						} else if (!thePrefs.IsShareableDirectory(file->GetPath())) {
							iState = CBS_UNCHECKEDDISABLED;
							iNoStyleState = DFCS_INACTIVE;
						} else {
							iState = (file == m_pHighlightedItem) ? CBS_UNCHECKEDHOT : CBS_UNCHECKEDNORMAL;
							iNoStyleState = (file == m_pHighlightedItem) ? DFCS_PUSHED : 0;
						}

						HTHEME hTheme = (::IsThemeActive() && ::IsAppThemed()) ? ::OpenThemeData(NULL, L"BUTTON") : NULL;
						RECT rcCheckBox{rcItem.left, rcIconTop, rcItem.left + 16, rcIconTop + 16};
						if (hTheme != NULL)
							::DrawThemeBackground(hTheme, dc.GetSafeHdc(), BP_CHECKBOX, iState, &rcCheckBox, NULL);
						else
							dc.DrawFrameControl(&rcCheckBox, DFC_BUTTON, DFCS_BUTTONCHECK | iNoStyleState | DFCS_FLAT);
						rcItem.left += 16 + sm_iLabelOffset;
					}

					if (theApp.GetSystemImageList() != NULL) {
						int iImage = theApp.GetFileTypeSystemImageIdx(file->GetFileName());
						::ImageList_Draw(theApp.GetSystemImageList(), iImage, dc.GetSafeHdc(), rcItem.left, rcIconTop, ILD_TRANSPARENT);
					}

					if (!file->GetFileComment().IsEmpty() || file->GetFileRating()) //not rated
						m_ImageList.Draw(dc, 0, POINT{rcItem.left, rcIconTop}, ILD_NORMAL | INDEXTOOVERLAYMASK(1));

					rcItem.left += iIconDrawWidth + sm_iLabelOffset;
					if (thePrefs.ShowRatingIndicator() && (file->HasComment() || file->HasRating() || file->IsKadCommentSearchRunning())) {
						m_ImageList.Draw(dc, 3 + file->UserRating(true), POINT{rcItem.left, rcIconTop}, ILD_NORMAL);
						rcItem.left += 16 + sm_iLabelOffset;
					}
					rcItem.left -= sm_iSubItemInset;
				}
				[[fallthrough]];
			default: //any text column
				rcItem.left += sm_iSubItemInset;
				rcItem.right -= sm_iSubItemInset;
				dc.DrawText(sItem, &rcItem, MLC_DT_TEXT | uDrawTextAlignment);
				break;
			case 9: //shared parts bar
				if (pKnownFile != NULL && pKnownFile->GetPartCount()) {
					++rcItem.top;
					--rcItem.bottom;
					pKnownFile->DrawShareStatusBar(dc, &rcItem, false, thePrefs.UseFlatBar());
					++rcItem.bottom;
					--rcItem.top;
				}
				break;
			case 12: //shared ed2k/kad
				if (pKnownFile != NULL) {
					rcItem.left += sm_iIconOffset;
					POINT point{rcItem.left, rcItem.top + iIconY};
					if (pKnownFile->GetPublishedED2K())
						m_ImageList.Draw(dc, 1, point, ILD_NORMAL);
					if (IsSharedInKad(pKnownFile)) {
						point.x += 16 + sm_iSubItemInset;
						m_ImageList.Draw(dc, IsSharedInKad(pKnownFile) ? 2 : 0, point, ILD_NORMAL);
					}
				}
			}
		}
		itemLeft += iColumnWidth;
	}

	DrawFocusRect(dc, &lpDrawItemStruct->rcItem, lpDrawItemStruct->itemState & ODS_FOCUS, bCtrlFocused, lpDrawItemStruct->itemState & ODS_SELECTED);
}

CString CSharedFilesCtrl::GetItemDisplayText(const CShareableFile *file, int iSubItem) const
{
	CString sText;
	switch (iSubItem) {
	case 0:
		return file->GetFileName();
	case 1:
		return CastItoXBytes((uint64)file->GetFileSize());
	case 2:
		return file->GetFileTypeDisplayStr();
	case 10:
		sText = PathHelpers::TrimTrailingSeparator(file->GetPath());
		return sText;
	}

	if (file->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
		const CKnownFile *pKnownFile = static_cast<const CKnownFile*>(file);
		switch (iSubItem) {
		case 3:
			sText = pKnownFile->GetUpPriorityDisplayString();
			break;
		case 4:
			sText = md4str(pKnownFile->GetFileHash());
			break;
		case 5:
			sText.Format(_T("%u (%u)"), pKnownFile->statistic.GetRequests(), pKnownFile->statistic.GetAllTimeRequests());
			break;
		case 6:
			sText = FormatSharedFileTimestamp(pKnownFile->statistic.GetAllTimeLastRequest());
			break;
		case 7:
			sText.Format(_T("%u (%u)"), pKnownFile->statistic.GetAccepts(), pKnownFile->statistic.GetAllTimeAccepts());
			break;
		case 8:
			sText.Format(_T("%s (%s)"), (LPCTSTR)CastItoXBytes(pKnownFile->statistic.GetTransferred()), (LPCTSTR)CastItoXBytes(pKnownFile->statistic.GetAllTimeTransferred()));
			break;
		case 9:
			sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(IDS_SHARED_STATUS), pKnownFile->GetPartCount());
			break;
		case 11:
			if (pKnownFile->m_nCompleteSourcesCountLo == pKnownFile->m_nCompleteSourcesCountHi)
				sText.Format(_T("%u"), pKnownFile->m_nCompleteSourcesCountLo);
			else if (pKnownFile->m_nCompleteSourcesCountLo == 0)
				sText.Format(_T("< %u"), pKnownFile->m_nCompleteSourcesCountHi);
			else
				sText.Format(_T("%u - %u"), pKnownFile->m_nCompleteSourcesCountLo, pKnownFile->m_nCompleteSourcesCountHi);
			break;
		case 12:
			sText.Format(_T("%s|%s"), (LPCTSTR)GetResString(pKnownFile->GetPublishedED2K() ? IDS_YES : IDS_NO), (LPCTSTR)GetResString(IsSharedInKad(pKnownFile) ? IDS_YES : IDS_NO));
			break;
		case 13:
			sText = pKnownFile->GetStrTagValue(FT_MEDIA_ARTIST);
			break;
		case 14:
			sText = pKnownFile->GetStrTagValue(FT_MEDIA_ALBUM);
			break;
		case 15:
			sText = pKnownFile->GetStrTagValue(FT_MEDIA_TITLE);
			break;
		case 16:
			{
				uint32 nMediaLength = pKnownFile->GetIntTagValue(FT_MEDIA_LENGTH);
				if (nMediaLength)
					sText = SecToTimeLength(nMediaLength);
			}
			break;
		case 17:
			{
				uint32 nBitrate = pKnownFile->GetIntTagValue(FT_MEDIA_BITRATE);
				if (nBitrate)
					sText.Format(_T("%u %s"), nBitrate, (LPCTSTR)GetResString(IDS_KBITSSEC));
			}
			break;
		case 18:
			sText = GetCodecDisplayName(pKnownFile->GetStrTagValue(FT_MEDIA_CODEC));
			break;
		case 19:
			sText = FormatUploadRatio(pKnownFile->GetAllTimeUploadRatio());
			break;
		case 20:
			sText = FormatUploadRatio(pKnownFile->GetSessionUploadRatio());
		}
	}
	return sText;
}

void CSharedFilesCtrl::OnContextMenu(CWnd*, CPoint point)
{
	// get merged settings
	bool bFirstItem = true;
	bool bContainsShareableFiles = false;
	bool bContainsOnlyShareableFile = true;
	bool bContainsUnshareableFile = false;
	int iSelectedItems = GetSelectedCount();
	int iCompleteFileSelected = -1;
	UINT uPrioMenuItem = 0;
	const CShareableFile *pSingleSelFile = NULL;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		const CShareableFile *pFile = GetFileByIndex(GetNextSelectedItem(pos));
		if (pFile == NULL)
			continue;
		pSingleSelFile = bFirstItem ? pFile : NULL;

		int iCurCompleteFile = static_cast<uint16>(!pFile->IsPartFile());
		if (bFirstItem)
			iCompleteFileSelected = iCurCompleteFile;
		else if (iCompleteFileSelected != iCurCompleteFile)
			iCompleteFileSelected = -1;

		bContainsUnshareableFile = !pFile->IsPartFile() && (bContainsUnshareableFile || (theApp.sharedfiles->ShouldBeShared(pFile->GetSharedDirectory(), pFile->GetFilePath(), false)
			&& !theApp.sharedfiles->ShouldBeShared(pFile->GetSharedDirectory(), pFile->GetFilePath(), true)));

		if (pFile->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
			bContainsOnlyShareableFile = false;
			UINT uCurPrioMenuItem = 0;
			if (static_cast<const CKnownFile*>(pFile)->IsAutoUpPriority())
				uCurPrioMenuItem = MP_PRIOAUTO;
			else
				switch (static_cast<const CKnownFile*>(pFile)->GetUpPriority()) {
				case PR_VERYLOW:
					uCurPrioMenuItem = MP_PRIOVERYLOW;
					break;
				case PR_LOW:
					uCurPrioMenuItem = MP_PRIOLOW;
					break;
				case PR_NORMAL:
					uCurPrioMenuItem = MP_PRIONORMAL;
					break;
				case PR_HIGH:
					uCurPrioMenuItem = MP_PRIOHIGH;
					break;
				case PR_VERYHIGH:
					uCurPrioMenuItem = MP_PRIOVERYHIGH;
					break;
				default:
					ASSERT(0);
				}

			if (bFirstItem)
				uPrioMenuItem = uCurPrioMenuItem;
			else if (uPrioMenuItem != uCurPrioMenuItem)
				uPrioMenuItem = 0;
		} else
			bContainsShareableFiles = true;

		bFirstItem = false;
	}

	EnableSubMenuItem(m_SharedFilesMenu, m_PrioMenu.m_hMenu, (!bContainsShareableFiles && iSelectedItems > 0) ? MF_ENABLED : MF_GRAYED);
	m_PrioMenu.CheckMenuRadioItem(MP_PRIOVERYLOW, MP_PRIOAUTO, uPrioMenuItem, 0);

	bool bSingleCompleteFileSelected = (iSelectedItems == 1 && (iCompleteFileSelected == 1 || bContainsOnlyShareableFile));
	m_SharedFilesMenu.EnableMenuItem(MP_OPEN, bSingleCompleteFileSelected ? MF_ENABLED : MF_GRAYED);
	UINT uInsertedMenuItem = 0;
	static const TCHAR _szSkinPkgSuffix1[] = _T(".") EMULSKIN_BASEEXT _T(".zip");
	static const TCHAR _szSkinPkgSuffix2[] = _T(".") EMULSKIN_BASEEXT _T(".rar");
	if (bSingleCompleteFileSelected
		&& pSingleSelFile
		&& (pSingleSelFile->GetFilePath().Right(_countof(_szSkinPkgSuffix1) - 1).CompareNoCase(_szSkinPkgSuffix1) == 0
			|| pSingleSelFile->GetFilePath().Right(_countof(_szSkinPkgSuffix2) - 1).CompareNoCase(_szSkinPkgSuffix2) == 0))
	{
		MENUITEMINFO mii = {};
		mii.cbSize = (UINT)sizeof mii;
		mii.fMask = MIIM_TYPE | MIIM_STATE | MIIM_ID;
		mii.fType = MFT_STRING;
		mii.fState = MFS_ENABLED;
		mii.wID = MP_INSTALL_SKIN;
		const CString &strBuff(GetResString(IDS_INSTALL_SKIN));
		mii.dwTypeData = const_cast<LPTSTR>((LPCTSTR)strBuff);
		if (m_SharedFilesMenu.InsertMenuItem(MP_OPENFOLDER, &mii, FALSE))
			uInsertedMenuItem = mii.wID;
	}
	m_SharedFilesMenu.EnableMenuItem(MP_OPENFOLDER, bSingleCompleteFileSelected ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_RENAME, (!bContainsShareableFiles && bSingleCompleteFileSelected) ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_REMOVE, iCompleteFileSelected > 0 ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_UNSHAREFILE, bContainsUnshareableFile ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.SetDefaultItem(bSingleCompleteFileSelected ? MP_OPEN : -1);
	m_SharedFilesMenu.EnableMenuItem(MP_CMT, (!bContainsShareableFiles && iSelectedItems > 0) ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_DETAIL, iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(thePrefs.GetShowCopyEd2kLinkCmd() ? MP_GETED2KLINK : MP_SHOWED2KLINK, (!bContainsOnlyShareableFile && iSelectedItems > 0) ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_FIND, GetItemCount() > 0 ? MF_ENABLED : MF_GRAYED);

	const CCollection *coll = pSingleSelFile ? static_cast<const CKnownFile*>(pSingleSelFile)->m_pCollection : NULL;
	m_CollectionsMenu.EnableMenuItem(MP_MODIFYCOLLECTION, (!bContainsShareableFiles && coll != NULL) ? MF_ENABLED : MF_GRAYED);
	m_CollectionsMenu.EnableMenuItem(MP_VIEWCOLLECTION, (!bContainsShareableFiles && coll != NULL) ? MF_ENABLED : MF_GRAYED);
	m_CollectionsMenu.EnableMenuItem(MP_SEARCHAUTHOR, (!bContainsShareableFiles && coll != NULL && !coll->GetAuthorKeyHashString().IsEmpty()) ? MF_ENABLED : MF_GRAYED);
#if defined(_DEBUG)
	//JOHNTODO: Not for release as we need kad lowID users in the network to see how well this work. Also, we do not support these links yet.
	bool bEnable = (iSelectedItems > 0 && theApp.IsConnected() && theApp.IsFirewalled() && theApp.clientlist->GetBuddy());
	m_SharedFilesMenu.EnableMenuItem(MP_GETKADSOURCELINK, (bEnable ? MF_ENABLED : MF_GRAYED));
#endif
	m_SharedFilesMenu.EnableMenuItem(Irc_SetSendLink, (!bContainsOnlyShareableFile && iSelectedItems == 1 && theApp.emuledlg->ircwnd->IsConnected()) ? MF_ENABLED : MF_GRAYED);

	CTitledMenu CopyMenu;
	CopyMenu.CreateMenu();
	CopyMenu.AddMenuTitle(NULL, true);
	CopyMenu.AppendMenu(MF_STRING | ((!bContainsOnlyShareableFile && iSelectedItems > 0) ? MF_ENABLED : MF_GRAYED), MP_COPY_FILE_NAME, GetResString(IDS_COPY_FILE_NAME));
	CopyMenu.AppendMenu(MF_STRING | ((!bContainsOnlyShareableFile && iSelectedItems > 0) ? MF_ENABLED : MF_GRAYED), MP_COPY_FILE_HASH, GetResString(IDS_COPY_HASH));
	CopyMenu.AppendMenu(MF_STRING | ((!bContainsOnlyShareableFile && iSelectedItems > 0) ? MF_ENABLED : MF_GRAYED), MP_COPY_FILE_SIZE, GetResString(IDS_COPY_FILE_SIZE));
	CopyMenu.AppendMenu(MF_STRING | ((!bContainsOnlyShareableFile && iSelectedItems > 0) ? MF_ENABLED : MF_GRAYED), MP_COPY_FILE_PATH, GetResString(IDS_COPY_FILE_PATH));
	CopyMenu.AppendMenu(MF_STRING | ((!bContainsOnlyShareableFile && iSelectedItems > 0) ? MF_ENABLED : MF_GRAYED), MP_COPY_FOLDER_PATH, GetResString(IDS_COPY_FOLDER_PATH));
	CopyMenu.AppendMenu(MF_STRING | ((!bContainsOnlyShareableFile && iSelectedItems > 0) ? MF_ENABLED : MF_GRAYED), MP_GETED2KLINK, GetResString(IDS_DL_LINK1), _T("ED2KLINK"));
	CopyMenu.AppendMenu(MF_STRING | ((!bContainsOnlyShareableFile && iSelectedItems > 0) ? MF_ENABLED : MF_GRAYED), MP_GETHTMLED2KLINK, GetResString(IDS_DL_LINK2), _T("ED2KLINK"));
	CopyMenu.AppendMenu(MF_STRING | ((!bContainsOnlyShareableFile && iSelectedItems > 0) ? MF_ENABLED : MF_GRAYED), MP_COPY_FILE_SUMMARY, AddMenuShortcutLabel(GetResString(IDS_COPY_FILE_SUMMARY), _T("Ctrl+Shift+C")));
	m_SharedFilesMenu.AppendMenu(MF_POPUP | ((!bContainsOnlyShareableFile && iSelectedItems > 0) ? MF_ENABLED : MF_GRAYED), (UINT_PTR)CopyMenu.m_hMenu, GetResString(IDS_COPY));

	CTitledMenu WebMenu;
	WebMenu.CreateMenu();
	WebMenu.AddMenuTitle(NULL, true);
	int iWebMenuEntries = theWebServices.GetFileMenuEntries(&WebMenu);
	UINT flag2 = (iWebMenuEntries == 0 || iSelectedItems != 1) ? MF_GRAYED : MF_STRING;
	m_SharedFilesMenu.AppendMenu(flag2 | MF_POPUP, (UINT_PTR)WebMenu.m_hMenu, GetResString(IDS_WEBSERVICES), _T("WEB"));

	GetPopupMenuPos(*this, point);
	m_SharedFilesMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);

	m_SharedFilesMenu.RemoveMenu(m_SharedFilesMenu.GetMenuItemCount() - 1, MF_BYPOSITION);
	m_SharedFilesMenu.RemoveMenu(m_SharedFilesMenu.GetMenuItemCount() - 1, MF_BYPOSITION);
	VERIFY(CopyMenu.DestroyMenu());
	VERIFY(WebMenu.DestroyMenu());
	if (uInsertedMenuItem)
		VERIFY(m_SharedFilesMenu.RemoveMenu(uInsertedMenuItem, MF_BYCOMMAND));
}

BOOL CSharedFilesCtrl::OnCommand(WPARAM wParam, LPARAM)
{
	wParam = LOWORD(wParam);

	CTypedPtrList<CPtrList, CShareableFile*> selectedList;
	CollectSelectedFiles(selectedList);

	if (wParam == MP_CREATECOLLECTION || wParam == MP_FIND || !selectedList.IsEmpty()) {
		CShareableFile *file = (selectedList.GetCount() == 1) ? selectedList.GetHead() : NULL;

		CKnownFile *pKnownFile;
		if (file != NULL && file->IsKindOf(RUNTIME_CLASS(CKnownFile)))
			pKnownFile = static_cast<CKnownFile*>(file);
		else
			pKnownFile = NULL;

		switch (wParam) {
		case Irc_SetSendLink:
			if (pKnownFile != NULL)
				theApp.emuledlg->ircwnd->SetSendFileString(pKnownFile->GetED2kLink());
			break;
		case MP_GETED2KLINK:
		case MP_GETHTMLED2KLINK:
			{
				CString str;
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					CKnownFile *pfile = static_cast<CKnownFile*>(selectedList.GetNext(pos));
					if (pfile != NULL && pfile->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
						if (!str.IsEmpty())
							str += (wParam == MP_GETHTMLED2KLINK) ? _T("<br>\r\n") : _T("\r\n");
						str += pfile->GetED2kLink(false, wParam == MP_GETHTMLED2KLINK);
					}
				}
				theApp.CopyTextToClipboard(str);
			}
			break;
		case MP_COPY_FILE_NAME:
		case MP_COPY_FILE_HASH:
		case MP_COPY_FILE_SIZE:
		case MP_COPY_FILE_PATH:
		case MP_COPY_FOLDER_PATH:
		case MP_COPY_FILE_SUMMARY:
			{
				std::vector<CString> lines;
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					CKnownFile *pfile = static_cast<CKnownFile*>(selectedList.GetNext(pos));
					if (pfile == NULL || !pfile->IsKindOf(RUNTIME_CLASS(CKnownFile)))
						continue;
					CString size;
					if (wParam == MP_COPY_FILE_NAME)
						lines.push_back(pfile->GetFileName());
					else if (wParam == MP_COPY_FILE_HASH)
						lines.push_back(md4str(pfile->GetFileHash()));
					else if (wParam == MP_COPY_FILE_SIZE)
						lines.push_back(ProUserMenuCopySeams::FormatUInt64(static_cast<uint64>(pfile->GetFileSize())));
					else if (wParam == MP_COPY_FILE_PATH)
						lines.push_back(pfile->GetFilePath());
					else if (wParam == MP_COPY_FOLDER_PATH)
						lines.push_back(pfile->GetPath());
					else if (wParam == MP_COPY_FILE_SUMMARY) {
						size = ProUserMenuCopySeams::FormatUInt64(static_cast<uint64>(pfile->GetFileSize()));
						lines.push_back(ProUserMenuCopySeams::FormatFileSummary(
							pfile->GetFileName(),
							md4str(pfile->GetFileHash()),
							size,
							CString(),
							CString(),
							pfile->GetFilePath(),
							pfile->GetED2kLink()));
					}
				}
				const CString text = ProUserMenuCopySeams::JoinLines(lines);
				if (!text.IsEmpty())
					theApp.CopyTextToClipboard(text);
			}
			break;
#if defined(_DEBUG)
		//JOHNTODO: Not for release as we need kad lowID users in the network to see how well this works. Also, we do not support these links yet.
		case MP_GETKADSOURCELINK:
			{
				CString str;
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					const CKnownFile *pfile = static_cast<CKnownFile*>(selectedList.GetNext(pos));
					if (pfile->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
						if (!str.IsEmpty())
							str += _T("\r\n");
						str += theApp.CreateKadSourceLink(pfile);
					}
				}
				theApp.CopyTextToClipboard(str);
			}
			break;
#endif
		// file operations
		case MP_OPEN:
		case IDA_ENTER:
			if (file && !file->IsPartFile())
				OpenFile(file);
			break;
		case MP_INSTALL_SKIN:
			if (file && !file->IsPartFile())
				InstallSkin(file->GetFilePath());
			break;
		case MP_OPENFOLDER:
			if (file && !file->IsPartFile()) {
				CString sParam;
				sParam.Format(_T("/select,\"%s\""), (LPCTSTR)file->GetFilePath());
				ShellOpen(_T("explorer"), sParam);
			}
			break;
		case MP_RENAME:
		case MPG_F2:
			if (pKnownFile && !pKnownFile->IsPartFile()) {
				InputBox inputbox;
				inputbox.SetLabels(GetResNoAmp(IDS_RENAME), GetResString(IDS_DL_FILENAME), pKnownFile->GetFileName());
				inputbox.SetEditFilenameMode();
				inputbox.DoModal();
				const CString &newname(inputbox.GetInput());
				if (!inputbox.WasCancelled() && !newname.IsEmpty()) {
					// at least prevent users from specifying something like "..\dir\file"
					if (newname.FindOneOf(sBadFileNameChar) >= 0) {
						AfxMessageBox(GetErrorMessage(ERROR_BAD_PATHNAME));
						break;
					}

					CString newpath(PathHelpers::EnsureTrailingSeparator(pKnownFile->GetPath()));
					newpath += newname;
					if (!LongPathSeams::MoveFile(pKnownFile->GetFilePath(), newpath)) {
						CString strError;
						strError.Format(GetResString(IDS_ERR_RENAMESF), (LPCTSTR)pKnownFile->GetFilePath(), (LPCTSTR)newpath, (LPCTSTR)GetErrorMessage(::GetLastError()));
						AfxMessageBox(strError);
						break;
					}

					if (pKnownFile->IsKindOf(RUNTIME_CLASS(CPartFile))) {
						pKnownFile->SetFileName(newname);
						static_cast<CPartFile*>(pKnownFile)->SetFullName(newpath);
					} else {
						theApp.sharedfiles->RemoveKeywords(pKnownFile);
						pKnownFile->SetFileName(newname);
						theApp.sharedfiles->AddKeywords(pKnownFile);
					}
					pKnownFile->SetFilePath(newpath);
					UpdateFile(pKnownFile);
				}
			} else
				MessageBeep(MB_OK);
			break;
		case MP_REMOVE:
		case MPG_DELETE:
			{
				if (LocMessageBox(IDS_CONFIRM_FILEDELETE, MB_ICONWARNING | MB_DEFBUTTON2 | MB_YESNO, 0) != IDYES)
					return TRUE;

				SetRedraw(false);
				bool bRemovedItems = false;
				while (!selectedList.IsEmpty()) {
					CShareableFile *myfile = selectedList.RemoveHead();
					if (!myfile || myfile->IsPartFile())
						continue;

					bool delsucc = ShellDeleteFile(myfile->GetFilePath());
					if (delsucc) {
						if (myfile->IsKindOf(RUNTIME_CLASS(CKnownFile)))
							theApp.sharedfiles->RemoveFile(static_cast<CKnownFile*>(myfile), true);
						else
							RemoveFile(myfile, true);
						bRemovedItems = true;
						if (myfile->IsKindOf(RUNTIME_CLASS(CPartFile)))
							theApp.emuledlg->transferwnd->GetDownloadList()->ClearCompleted(static_cast<CPartFile*>(myfile));
					} else {
						CString strError;
						strError.Format(GetResString(IDS_ERR_DELFILE), (LPCTSTR)myfile->GetFilePath());
						strError.AppendFormat(_T("\r\n\r\n%s"), (LPCTSTR)GetErrorMessage(GetLastError()));
						AfxMessageBox(strError);
					}
				}
				SetRedraw(true);
				if (bRemovedItems) {
					AutoSelectItem();
					// Depending on <no-idea> this does not always cause an LVN_ITEMACTIVATE
					// message to be sent. So, explicitly redraw the item.
					theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails();
					theApp.emuledlg->sharedfileswnd->OnSingleFileShareStatusChanged(); // might have been a single shared file
				}
			}
			break;
		case MP_UNSHAREFILE:
			{
				SetRedraw(false);
				bool bUnsharedItems = false;
				while (!selectedList.IsEmpty()) {
					const CShareableFile *myfile = selectedList.RemoveHead();
					if (myfile && !myfile->IsPartFile() && theApp.sharedfiles->ShouldBeShared(myfile->GetPath(), myfile->GetFilePath(), false)
						&& !theApp.sharedfiles->ShouldBeShared(myfile->GetPath(), myfile->GetFilePath(), true))
					{
						bUnsharedItems |= theApp.sharedfiles->ExcludeFile(myfile->GetFilePath());
						ASSERT(bUnsharedItems);
					}
				}
				SetRedraw(true);
				if (bUnsharedItems) {
					theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails();
					theApp.emuledlg->sharedfileswnd->OnSingleFileShareStatusChanged();
					if (GetFirstSelectedItemPosition() == NULL)
						AutoSelectItem();
				}
			}
			break;
		case MP_CMT:
			ShowFileDialog(selectedList, IDD_COMMENT);
			break;
		case MPG_ALTENTER:
		case MP_DETAIL:
			ShowFileDialog(selectedList);
			break;
		case MP_FIND:
			OnFindStart();
			break;
		case MP_CREATECOLLECTION:
			{
				CCollection *pCollection = new CCollection();
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					CShareableFile *pFile = selectedList.GetNext(pos);
					if (pFile->IsKindOf(RUNTIME_CLASS(CKnownFile)))
						pCollection->AddFileToCollection(pFile, true);
				}
				CCollectionCreateDialog dialog;
				dialog.SetCollection(pCollection, true);
				dialog.DoModal();
				//We delete this collection object because when the newly created
				//collection file is added to the shared file list, it is read and verified
				//and which creates the collection object that is attached to that file.
				delete pCollection;
			}
			break;
		case MP_SEARCHAUTHOR:
			if (pKnownFile && pKnownFile->m_pCollection) {
				SSearchParams *pParams = new SSearchParams;
				pParams->strExpression = pKnownFile->m_pCollection->GetCollectionAuthorKeyString();
				pParams->eType = SearchTypeKademlia;
				pParams->strFileType = _T(ED2KFTSTR_EMULECOLLECTION);
				pParams->strSpecialTitle = pKnownFile->m_pCollection->m_sCollectionAuthorName;
				if (pParams->strSpecialTitle.GetLength() > 50) {
					pParams->strSpecialTitle.Truncate(50);
					pParams->strSpecialTitle += _T("...");
				}

				theApp.emuledlg->searchwnd->m_pwndResults->StartSearch(pParams);
			}
			break;
		case MP_VIEWCOLLECTION:
			if (pKnownFile && pKnownFile->m_pCollection) {
				CCollectionViewDialog dialog;
				dialog.SetCollection(pKnownFile->m_pCollection);
				dialog.DoModal();
			}
			break;
		case MP_MODIFYCOLLECTION:
			if (pKnownFile && pKnownFile->m_pCollection) {
				CCollectionCreateDialog dialog;
				CCollection *pCollection = new CCollection(pKnownFile->m_pCollection);
				dialog.SetCollection(pCollection, false);
				dialog.DoModal();
				delete pCollection;
			}
			break;
		case MP_SHOWED2KLINK:
			ShowFileDialog(selectedList, IDD_ED2KLINK);
			break;
		case MP_PRIOVERYLOW:
		case MP_PRIOLOW:
		case MP_PRIONORMAL:
		case MP_PRIOHIGH:
		case MP_PRIOVERYHIGH:
		case MP_PRIOAUTO:
			for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
				CKnownFile *pfile = static_cast<CKnownFile*>(selectedList.GetNext(pos));
				if (pfile->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
					pfile->SetAutoUpPriority(wParam == MP_PRIOAUTO);
					switch (wParam) {
					case MP_PRIOVERYLOW:
						pfile->SetUpPriority(PR_VERYLOW);
						break;
					case MP_PRIOLOW:
						pfile->SetUpPriority(PR_LOW);
						break;
					case MP_PRIONORMAL:
						pfile->SetUpPriority(PR_NORMAL);
						break;
					case MP_PRIOHIGH:
						pfile->SetUpPriority(PR_HIGH);
						break;
					case MP_PRIOVERYHIGH:
						pfile->SetUpPriority(PR_VERYHIGH);
						break;
					case MP_PRIOAUTO:
						pfile->UpdateAutoUpPriority();
					}
					UpdateFile(pfile);
				}
			}
			break;
		default:
			if (file && wParam >= MP_WEBURL && wParam <= MP_WEBURL + 256)
				theWebServices.RunURL(file, (UINT)wParam);
		}
	}
	return TRUE;
}

void CSharedFilesCtrl::OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult)
{
	const LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	SortByColumn(pNMLV->iSubItem);
	*pResult = 0;
}

void CSharedFilesCtrl::SortByColumn(int iSubItem)
{
	bool sortAscending;
	if (GetSortItem() != iSubItem)
		switch (iSubItem) {
		case 3:  // Priority
		case 5:  // Requests
		case 6:  // Last Request
		case 7:  // Accepted Requests
		case 8:  // Transferred Data
		case 11: // Complete Sources
		case 12: // Shared ed2k/kad
		case 19: // All-time ratio
		case 20: // Session ratio
			// Keep the current 'm_aSortBySecondValue' for that column, but reset to 'descending'
			sortAscending = false;
			break;
		default:
			sortAscending = true;
		}
	else
		sortAscending = !GetSortAscending();

	// Ornis 4-way-sorting
	int adder = 0;
	int iSortBySecondValueIndex = -1;
	if (iSubItem == 5)
		iSortBySecondValueIndex = 0;
	else if (iSubItem == 7)
		iSortBySecondValueIndex = 1;
	else if (iSubItem == 8)
		iSortBySecondValueIndex = 2;

	if (iSortBySecondValueIndex >= 0) { // 5=IDS_SF_REQUESTS, 7=IDS_SF_ACCEPTS, 8=IDS_SF_TRANSFERRED
		ASSERT(iSortBySecondValueIndex < (int)_countof(m_aSortBySecondValue));
		if (GetSortItem() == iSubItem && !sortAscending) // check for 'descending' because the initial sort order is also 'descending'
			m_aSortBySecondValue[iSortBySecondValueIndex] = !m_aSortBySecondValue[iSortBySecondValueIndex];
		if (m_aSortBySecondValue[iSortBySecondValueIndex])
			adder = 100;
	} else if (iSubItem == 12) { // 12=IDS_SHAREDTITLE
		ASSERT(3 < _countof(m_aSortBySecondValue));
		if (GetSortItem() == iSubItem && !sortAscending) // check for 'descending' because the initial sort order is also 'descending'
			m_aSortBySecondValue[3] = !m_aSortBySecondValue[3];
		if (m_aSortBySecondValue[3])
			adder = 100;
	}

	// Sort table
	if (adder == 0)
		SetSortArrow(iSubItem, sortAscending);
	else
		SetSortArrow(iSubItem, sortAscending ? arrowDoubleUp : arrowDoubleDown);

	UpdateSortHistory(MAKELONG(iSubItem + adder, !sortAscending));
	SharedFilesRepositionState savedState;
	const bool bPreserveState = ShouldPreserveVirtualListState() && !m_aVisibleFiles.empty();
	if (bPreserveState)
		CaptureSharedFilesRepositionState(*this, savedState);
	SortVisibleFiles();
	if (bPreserveState) {
		CSharedFilesSelectionRestoreGuard guard(*this);
		RestoreSharedFilesRepositionState(*this, savedState);
	}
	Invalidate(FALSE);
}

int CALLBACK CSharedFilesCtrl::SortProc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	const CShareableFile *item1 = reinterpret_cast<CShareableFile*>(lParam1);
	const CShareableFile *item2 = reinterpret_cast<CShareableFile*>(lParam2);

	bool bSortAscending = !HIWORD(lParamSort);

	int iResult = 0;
	bool bExtColumn = false;

	switch (LOWORD(lParamSort)) {
	case 0: //file name
		iResult = CompareLocaleStringNoCase(item1->GetFileName(), item2->GetFileName());
		break;
	case 1: //file size
		iResult = CompareUnsigned(item1->GetFileSize(), item2->GetFileSize());
		break;
	case 2: //file type
		iResult = item1->GetFileTypeDisplayStr().Compare(item2->GetFileTypeDisplayStr());
		// if the type is equal, sub-sort by extension
		if (iResult == 0) {
			LPCTSTR pszExt1 = ::PathFindExtension(item1->GetFileName());
			LPCTSTR pszExt2 = ::PathFindExtension(item2->GetFileName());
			if (!*pszExt1 ^ !*pszExt2)
				iResult = *pszExt1 ? -1 : 1;
			else
				iResult = *pszExt1 ? _tcsicmp(pszExt1, pszExt2) : 0;
		}
		break;
	case 10: //folder
		iResult = CompareLocaleStringNoCase(item1->GetPath(), item2->GetPath());
		break;
	default:
		bExtColumn = true;
	}

	if (bExtColumn) {
		if (item1->IsKindOf(RUNTIME_CLASS(CKnownFile)) && !item2->IsKindOf(RUNTIME_CLASS(CKnownFile)))
			iResult = -1;
		else if (!item1->IsKindOf(RUNTIME_CLASS(CKnownFile)) && item2->IsKindOf(RUNTIME_CLASS(CKnownFile)))
			iResult = 1;
		else if (item1->IsKindOf(RUNTIME_CLASS(CKnownFile)) && item2->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
			const CKnownFile *kitem1 = static_cast<const CKnownFile*>(item1);
			const CKnownFile *kitem2 = static_cast<const CKnownFile*>(item2);

			switch (LOWORD(lParamSort)) {
			case 3: //prio
				{
					uint8 p1 = kitem1->GetUpPriority() + 1;
					if (p1 == 5)
						p1 = 0;
					uint8 p2 = kitem2->GetUpPriority() + 1;
					if (p2 == 5)
						p2 = 0;
					iResult = p1 - p2;
				}
				break;
			case 4: //fileID
				iResult = memcmp(kitem1->GetFileHash(), kitem2->GetFileHash(), 16);
				break;
			case 5: //requests
				iResult = CompareUnsigned(kitem1->statistic.GetRequests(), kitem2->statistic.GetRequests());
				break;
			case 6: //last request
				iResult = CompareTimestampUndefinedAtBottom(kitem1->statistic.GetAllTimeLastRequest(), kitem2->statistic.GetAllTimeLastRequest(), bSortAscending);
				break;
			case 7: //accepted requests
				iResult = CompareUnsigned(kitem1->statistic.GetAccepts(), kitem2->statistic.GetAccepts());
				break;
			case 8: //all transferred
				iResult = CompareUnsigned(kitem1->statistic.GetTransferred(), kitem2->statistic.GetTransferred());
				break;
			case 11: //complete sources
				iResult = CompareUnsigned(kitem1->m_nCompleteSourcesCount, kitem2->m_nCompleteSourcesCount);
				break;
			case 12: //ed2k shared
				iResult = kitem1->GetPublishedED2K() - kitem2->GetPublishedED2K();
				break;
			case 13:
				iResult = CompareOptLocaleStringNoCaseUndefinedAtBottom(kitem1->GetStrTagValue(FT_MEDIA_ARTIST), kitem2->GetStrTagValue(FT_MEDIA_ARTIST), bSortAscending);
				break;
			case 14:
				iResult = CompareOptLocaleStringNoCaseUndefinedAtBottom(kitem1->GetStrTagValue(FT_MEDIA_ALBUM), kitem2->GetStrTagValue(FT_MEDIA_ALBUM), bSortAscending);
				break;
			case 15:
				iResult = CompareOptLocaleStringNoCaseUndefinedAtBottom(kitem1->GetStrTagValue(FT_MEDIA_TITLE), kitem2->GetStrTagValue(FT_MEDIA_TITLE), bSortAscending);
				break;
			case 16:
				iResult = CompareUnsignedUndefinedAtBottom(kitem1->GetIntTagValue(FT_MEDIA_LENGTH), kitem2->GetIntTagValue(FT_MEDIA_LENGTH), bSortAscending);
				break;
			case 17:
				iResult = CompareUnsignedUndefinedAtBottom(kitem1->GetIntTagValue(FT_MEDIA_BITRATE), kitem2->GetIntTagValue(FT_MEDIA_BITRATE), bSortAscending);
				break;
			case 18:
				iResult = CompareOptLocaleStringNoCaseUndefinedAtBottom(GetCodecDisplayName(kitem1->GetStrTagValue(FT_MEDIA_CODEC)), GetCodecDisplayName(kitem2->GetStrTagValue(FT_MEDIA_CODEC)), bSortAscending);
				break;
			case 19:
				iResult = CompareRatio(kitem1->GetAllTimeUploadRatio(), kitem2->GetAllTimeUploadRatio());
				break;
			case 20:
				iResult = CompareRatio(kitem1->GetSessionUploadRatio(), kitem2->GetSessionUploadRatio());
				break;

			case 105: //all requests
				iResult = CompareUnsigned(kitem1->statistic.GetAllTimeRequests(), kitem2->statistic.GetAllTimeRequests());
				break;
			case 107: //all accepted requests
				iResult = CompareUnsigned(kitem1->statistic.GetAllTimeAccepts(), kitem2->statistic.GetAllTimeAccepts());
				break;
			case 108: //all transferred
				iResult = CompareUnsigned(kitem1->statistic.GetAllTimeTransferred(), kitem2->statistic.GetAllTimeTransferred());
				break;
			case 112: //kad shared
				{
					time_t tNow = time(NULL);
					int i1 = static_cast<int>(tNow < kitem1->GetLastPublishTimeKadSrc());
					int i2 = static_cast<int>(tNow < kitem2->GetLastPublishTimeKadSrc());
					iResult = i1 - i2;
				}
			}
		}
	}

	//call secondary sort order, if the first one resulted as equal
	if (iResult == 0) {
		LPARAM iNextSort = theApp.emuledlg->sharedfileswnd->sharedfilesctrl.GetNextSortOrder(lParamSort);
		if (iNextSort != -1)
			return SortProc(lParam1, lParam2, iNextSort);
	}
	return bSortAscending ? iResult : -iResult;
}

void CSharedFilesCtrl::OpenFile(const CShareableFile *file)
{
	if (file->IsKindOf(RUNTIME_CLASS(CKnownFile)) && static_cast<const CKnownFile*>(file)->m_pCollection) {
		CCollectionViewDialog dialog;
		dialog.SetCollection(static_cast<const CKnownFile*>(file)->m_pCollection);
		dialog.DoModal();
	} else
		ShellDefaultVerb(file->GetFilePath());
}

void CSharedFilesCtrl::OnNmDblClk(LPNMHDR, LRESULT *pResult)
{
	int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
	if (iSel >= 0) {
		CShareableFile *file = GetFileByIndex(iSel);
		if (file) {
			if (GetKeyState(VK_MENU) & 0x8000) {
				CTypedPtrList<CPtrList, CShareableFile*> aFiles;
				aFiles.AddHead(file);
				ShowFileDialog(aFiles);
			} else if (!file->IsPartFile())
				OpenFile(file);
		}
	}
	*pResult = 0;
}

void CSharedFilesCtrl::CreateMenus()
{
	if (m_PrioMenu)
		VERIFY(m_PrioMenu.DestroyMenu());
	if (m_CollectionsMenu)
		VERIFY(m_CollectionsMenu.DestroyMenu());
	if (m_SharedFilesMenu)
		VERIFY(m_SharedFilesMenu.DestroyMenu());

	m_PrioMenu.CreateMenu();
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOVERYLOW, GetResString(IDS_PRIOVERYLOW));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOLOW, GetResString(IDS_PRIOLOW));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIONORMAL, GetResString(IDS_PRIONORMAL));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOHIGH, GetResString(IDS_PRIOHIGH));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOVERYHIGH, GetResString(IDS_PRIORELEASE));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOAUTO, GetResString(IDS_PRIOAUTO));//UAP

	m_CollectionsMenu.CreateMenu();
	m_CollectionsMenu.AddMenuTitle(NULL, true);
	m_CollectionsMenu.AppendMenu(MF_STRING, MP_CREATECOLLECTION, GetResString(IDS_CREATECOLLECTION), _T("COLLECTION_ADD"));
	m_CollectionsMenu.AppendMenu(MF_STRING, MP_MODIFYCOLLECTION, GetResString(IDS_MODIFYCOLLECTION), _T("COLLECTION_EDIT"));
	m_CollectionsMenu.AppendMenu(MF_STRING, MP_VIEWCOLLECTION, GetResString(IDS_VIEWCOLLECTION), _T("COLLECTION_VIEW"));
	m_CollectionsMenu.AppendMenu(MF_STRING, MP_SEARCHAUTHOR, GetResString(IDS_SEARCHAUTHORCOLLECTION), _T("COLLECTION_SEARCH"));

	m_SharedFilesMenu.CreatePopupMenu();
	m_SharedFilesMenu.AddMenuTitle(GetResString(IDS_SHAREDFILES), true);

	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_OPEN, AddMenuShortcutLabel(GetResString(IDS_OPENFILE), _T("Ctrl+O")), _T("OPENFILE"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_OPENFOLDER, AddMenuShortcutLabel(GetResString(IDS_OPENFOLDER), _T("Ctrl+Shift+O")), _T("OPENFOLDER"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_RENAME, AddMenuShortcutLabel(GetResString(IDS_RENAME) + _T("..."), _T("F2")), _T("FILERENAME"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_REMOVE, AddMenuShortcutLabel(GetResString(IDS_DELETE), _T("Delete")), _T("DELETE"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_UNSHAREFILE, GetResString(IDS_UNSHARE), _T("KADBOOTSTRAP")); // TODO: better icon
	m_SharedFilesMenu.AppendMenu(MF_STRING, Irc_SetSendLink, GetResString(IDS_IRC_ADDLINKTOIRC), _T("IRCCLIPBOARD"));

	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
	CString sPrio(GetResString(IDS_PRIORITY));
	sPrio.AppendFormat(_T(" (%s)"), (LPCTSTR)GetResString(IDS_PW_CON_UPLBL));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_PrioMenu.m_hMenu, sPrio, _T("FILEPRIORITY"));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);

	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_CollectionsMenu.m_hMenu, GetResString(IDS_META_COLLECTION), _T("AABCollectionFileType"));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);

	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_DETAIL, AddMenuShortcutLabel(GetResString(IDS_SHOWDETAILS), _T("Ctrl+I")), _T("FILEINFO"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_CMT, GetResString(IDS_CMT_ADD), _T("FILECOMMENTS"));
	if (thePrefs.GetShowCopyEd2kLinkCmd())
		m_SharedFilesMenu.AppendMenu(MF_STRING, MP_GETED2KLINK, AddMenuShortcutLabel(GetResString(IDS_DL_LINK1), _T("Ctrl+L")), _T("ED2KLINK"));
	else
		m_SharedFilesMenu.AppendMenu(MF_STRING, MP_SHOWED2KLINK, GetResString(IDS_DL_SHOWED2KLINK), _T("ED2KLINK"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_FIND, AddMenuShortcutLabel(GetResString(IDS_FIND), _T("Ctrl+F")), _T("Search"));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);

#if defined(_DEBUG)
	//JOHNTODO: Not for release as we need kad lowID users in the network to see how well this works. Also, we do not support these links yet.
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_GETKADSOURCELINK, GetResString(IDS_COPY_ED2K_LINKS_TO_CLIPBOARD_KAD));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
#endif
}

void CSharedFilesCtrl::ShowComments(CShareableFile *file)
{
	if (file) {
		CTypedPtrList<CPtrList, CShareableFile*> aFiles;
		aFiles.AddHead(file);
		ShowFileDialog(aFiles, IDD_COMMENT);
	}
}

void CSharedFilesCtrl::OnLvnGetDispInfo(LPNMHDR pNMHDR, LRESULT *pResult)
{
	if (!theApp.IsClosing()) {
		// Although we have an owner drawn listview control we store the text for the primary item in the
		// listview, to be capable of quick searching those items via the keyboard. Because our listview
		// items may change their contents, we do this via a text callback function. The listview control
		// will send us the LVN_DISPINFO notification if it needs to know the contents of the primary item.
		//
		// But, the listview control sends this notification all the time, even if we do not search for an item.
		// At least this notification is only sent for the visible items and not for all items in the list.
		// Though, because this function is invoked *very* often, do *NOT* put any time consuming code in here.
		//
		// Vista: That callback is used to get the strings for the label tips for the sub(!)-items.
		//
		const LVITEMW &rItem = reinterpret_cast<NMLVDISPINFO*>(pNMHDR)->item;
		if (rItem.mask & LVIF_TEXT) {
			const CShareableFile *pFile = GetFileByIndex(rItem.iItem);
#if EMULE_COMPILED_STARTUP_PROFILING
			if (rItem.iItem == 0 && rItem.iSubItem == 0) {
				CString strPhase;
				strPhase.Format(_T("CSharedFilesCtrl::OnLvnGetDispInfo first row requested (rows=%d file=%d)"),
					static_cast<int>(m_aVisibleFiles.size()),
					pFile != NULL ? 1 : 0);
				theApp.AppendStartupProfileLine(strPhase, 0);
			}
#endif
			if (pFile != NULL)
				_tcsncpy_s(rItem.pszText, rItem.cchTextMax, GetItemDisplayText(pFile, rItem.iSubItem), _TRUNCATE);
		}
	}
	*pResult = 0;
}

void CSharedFilesCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (HandleSortShortcut(nChar))
		return;

	const UINT uCommand = FileListKeyboardShortcutsSeams::ClassifyKeyMessage(
		FileListKeyboardShortcutsSeams::EContext::SharedFiles,
		WM_KEYDOWN,
		nChar,
		GetKeyState(VK_CONTROL) < 0,
		GetKeyState(VK_MENU) < 0,
		GetKeyState(VK_SHIFT) < 0);
	if (uCommand != 0) {
		SendMessage(WM_COMMAND, uCommand);
		return;
	}

	if (nChar == 'C' && GetKeyState(VK_CONTROL) < 0) {
		// Ctrl+C: Copy listview items to clipboard
		SendMessage(WM_COMMAND, MP_GETED2KLINK);
		return;
	}
	if (nChar == VK_F5)
		ReloadFileList();
	else if (nChar == VK_SPACE && CheckBoxesEnabled()) {
		// Toggle Checkboxes
		// selection and item position might change during processing (shouldn't though, but lets make sure), so first get all pointers instead using the selection pos directly
		SetRedraw(false);
		CTypedPtrList<CPtrList, CShareableFile*> selectedList;
		CollectSelectedFiles(selectedList);
		while (!selectedList.IsEmpty())
			CheckBoxClicked(FindFile(selectedList.RemoveHead()));

		SetRedraw(true);
	}

	CMuleListCtrl::OnKeyDown(nChar, nRepCnt, nFlags);
}

bool CSharedFilesCtrl::HandleSortShortcut(UINT nChar)
{
	const FileListKeyboardShortcutsSeams::ESortRole eSortRole = FileListKeyboardShortcutsSeams::ClassifySortKeyMessage(
		WM_KEYDOWN,
		nChar,
		GetKeyState(VK_CONTROL) < 0,
		GetKeyState(VK_MENU) < 0,
		GetKeyState(VK_SHIFT) < 0);
	if (eSortRole == FileListKeyboardShortcutsSeams::ESortRole::None)
		return false;

	const int iColumn = FileListKeyboardShortcutsSeams::GetSortShortcutColumn(FileListKeyboardShortcutsSeams::EContext::SharedFiles, eSortRole);
	if (iColumn < 0) {
		MessageBeep(MB_OK);
		return true;
	}
	SortByColumn(iColumn);
	return true;
}

void CSharedFilesCtrl::ShowFileDialog(CTypedPtrList<CPtrList, CShareableFile*> &aFiles, UINT uInvokePage)
{
	if (!aFiles.IsEmpty()) {
		CSharedFileDetailsSheet dialog(aFiles, uInvokePage, this);
		dialog.DoModal();
	}
}

void CSharedFilesCtrl::SetDirectoryFilter(CDirectoryItem *pNewFilter, bool bRefresh)
{
	if (m_pDirectoryFilter != pNewFilter) {
		m_pDirectoryFilter = pNewFilter;
		if (bRefresh && m_bModelBound)
			ReloadFileList();
	}
}

void CSharedFilesCtrl::OnLvnGetInfoTip(LPNMHDR pNMHDR, LRESULT *pResult)
{
	LPNMLVGETINFOTIP pGetInfoTip = reinterpret_cast<LPNMLVGETINFOTIP>(pNMHDR);
	LVHITTESTINFO hti;
	if (pGetInfoTip && pGetInfoTip->iSubItem == 0 && ::GetCursorPos(&hti.pt)) {
		ScreenToClient(&hti.pt);
		if (SubItemHitTest(&hti) == -1 || hti.iItem != pGetInfoTip->iItem || hti.iSubItem != 0) {
			// don' show the default label tip for the main item, if the mouse is not over the main item
			if ((pGetInfoTip->dwFlags & LVGIT_UNFOLDED) == 0 && pGetInfoTip->cchTextMax > 0 && pGetInfoTip->pszText[0] != _T('\0'))
				pGetInfoTip->pszText[0] = _T('\0');
			return;
		}

		const CShareableFile *pFile = GetFileByIndex(pGetInfoTip->iItem);
		if (pFile && pGetInfoTip->pszText && pGetInfoTip->cchTextMax > 0) {
			CString strInfo(pFile->GetInfoSummary());
			strInfo += TOOLTIP_AUTOFORMAT_SUFFIX_CH;
			_tcsncpy_s(pGetInfoTip->pszText, pGetInfoTip->cchTextMax, strInfo, _TRUNCATE);
		}
	}
	*pResult = 0;
}

bool CSharedFilesCtrl::IsFilteredOut(const CShareableFile *pFile) const
{
	const CStringArray &rastrFilter = theApp.emuledlg->sharedfileswnd->m_astrFilter;
	if (!rastrFilter.IsEmpty()) {
		// filtering is done by text only for all columns to keep it consistent and simple for the user
		// even if that doesn't allow complex filters
		const CString &szFilterTarget(GetItemDisplayText(pFile, theApp.emuledlg->sharedfileswnd->GetFilterColumn()));

		for (INT_PTR i = rastrFilter.GetCount(); --i >= 0;) {
			LPCTSTR pszText = (LPCTSTR)rastrFilter[i];
			bool bAnd = (*pszText != _T('-'));
			if (!bAnd)
				++pszText;

			bool bFound = (stristr(szFilterTarget, pszText) != NULL);
			if (bAnd != bFound)
				return true;
		}
	}
	return false;
}

void CSharedFilesCtrl::SetToolTipsDelay(DWORD dwDelay)
{
	CToolTipCtrl *tooltip = GetToolTips();
	if (tooltip)
		tooltip->SetDelayTime(TTDT_INITIAL, dwDelay);
}

bool CSharedFilesCtrl::IsSharedInKad(const CKnownFile *file) const
{
	if (!Kademlia::CKademlia::IsConnected() || time(NULL) >= file->GetLastPublishTimeKadSrc())
		return false;
	if (!Kademlia::CKademlia::IsFirewalled())
		return true;
	return (theApp.clientlist->GetBuddy() && (file->GetLastPublishBuddy() == theApp.clientlist->GetBuddy()->GetIP()))
		|| (Kademlia::CKademlia::IsRunning() && !Kademlia::CUDPFirewallTester::IsFirewalledUDP(true) && Kademlia::CUDPFirewallTester::IsVerified());
}

void CSharedFilesCtrl::AddShareableFiles(const CString &strFromDir)
{
	while (!liTempShareableFilesInDir.IsEmpty())	// clear old file list
		delete liTempShareableFilesInDir.RemoveHead();

	ASSERT(strFromDir.Right(1) == _T("\\"));
	DWORD dwEnumerateError = ERROR_SUCCESS;
	if (!PathHelpers::ForEachDirectoryEntry(strFromDir, [&](const WIN32_FIND_DATA &findData) -> bool {
		if ((findData.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_TEMPORARY)) != 0)
			return true;

		const ULONGLONG ullFoundFileSize = (static_cast<ULONGLONG>(findData.nFileSizeHigh) << 32) | findData.nFileSizeLow;
		if (ullFoundFileSize == 0 || ullFoundFileSize > MAX_EMULE_FILE_SIZE)
			return true;

		const CString strFoundFileName(findData.cFileName);
		const CString strFoundDirectory(strFromDir);
		const CString strFoundFilePath(PathHelpers::AppendPathComponent(strFoundDirectory, strFoundFileName));
		if (ShouldIgnoreSharedFileCandidate(strFoundFilePath, strFoundFileName))
			return true;

		time_t fdate = (time_t)FileTimeToUnixTime(findData.ftLastWriteTime);
		if (fdate == 0)
			fdate = (time_t)-1;
		if (fdate == (time_t)-1) {
			if (thePrefs.GetVerbose())
				AddDebugLogLine(false, _T("Failed to get file date of \"%s\""), (LPCTSTR)strFoundFilePath);
		}

		CKnownFile *toadd = theApp.knownfiles->FindKnownFile(strFoundFileName, fdate, ullFoundFileSize);
		if (toadd == NULL || theApp.sharedfiles->GetFileByID(toadd->GetFileHash()) == NULL) {
			if (toadd != NULL) {
				toadd->SetFilePath(strFoundFilePath);
				toadd->SetPath(strFoundDirectory);
				AppendVisibleFile(toadd);
			} else {
				CShareableFile *pNewTempFile = new CShareableFile();
				pNewTempFile->SetFilePath(strFoundFilePath);
				pNewTempFile->SetAFileName(strFoundFileName);
				pNewTempFile->SetPath(strFoundDirectory);
				pNewTempFile->SetFileSize(ullFoundFileSize);
				uchar aucMD4[MDX_DIGEST_SIZE] = {};
				pNewTempFile->SetFileHash(aucMD4);
				liTempShareableFilesInDir.AddTail(pNewTempFile);
				AppendVisibleFile(pNewTempFile);
			}
		}
		return true;
	}, &dwEnumerateError)) {
		const DWORD dwError = dwEnumerateError;
		if (dwError != ERROR_FILE_NOT_FOUND)
			DebugLogError(_T("Failed to find files for SharedFilesListCtrl in %s, %s"), (LPCTSTR)strFromDir, (LPCTSTR)GetErrorMessage(dwError));
		return;
	}
}

BOOL CSharedFilesCtrl::OnNMClick(LPNMHDR pNMHDR, LRESULT *pResult)
{
	if (CheckBoxesEnabled()) { // do we have checkboxes?
		NMLISTVIEW *pNMListView = reinterpret_cast<NM_LISTVIEW*>(pNMHDR);

		int iItem = HitTest(pNMListView->ptAction);
		if (iItem >= 0) {
			// determine if the checkbox was clicked
			CRect rcItem;
			if (GetItemRect(iItem, rcItem, LVIR_BOUNDS)) {
				CPoint pointHit(pNMListView->ptAction);
				ASSERT(rcItem.PtInRect(pointHit));
				rcItem.left += sm_iIconOffset;
				rcItem.right = rcItem.left + 16;
				rcItem.top += (rcItem.Height() > 16) ? ((rcItem.Height() - 15) / 2) : 0;
				rcItem.bottom = rcItem.top + 16;
				if (rcItem.PtInRect(pointHit)) {
					// user clicked on the checkbox
					CheckBoxClicked(iItem);
				}
			}

		}
	}

	return (BOOL)(*pResult = 0); // pass on to the parent window
}

void CSharedFilesCtrl::CheckBoxClicked(int iItem)
{
	if (iItem == -1) {
		ASSERT(0);
		return;
	}
	// check which state the checkbox (should) currently have
	const CShareableFile *pFile = GetFileByIndex(iItem);
	if (pFile == NULL)
		return;
	if (theApp.sharedfiles->ShouldBeShared(pFile->GetPath(), pFile->GetFilePath(), false)) {
		// this is currently shared so unshare it
		if (theApp.sharedfiles->ShouldBeShared(pFile->GetPath(), pFile->GetFilePath(), true))
			return; // not allowed to unshare this file
		VERIFY(theApp.sharedfiles->ExcludeFile(pFile->GetFilePath()));
		// update GUI stuff
		ShowFilesCount();
		theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails();
		theApp.emuledlg->sharedfileswnd->OnSingleFileShareStatusChanged();
		// no need to update the list itself, will be handled in the RemoveFile function
	} else {
		if (!thePrefs.IsShareableDirectory(pFile->GetPath()))
			return; // not allowed to share
		VERIFY(theApp.sharedfiles->AddSingleSharedFile(pFile->GetFilePath()));
		ShowFilesCount();
		theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails();
		theApp.emuledlg->sharedfileswnd->OnSingleFileShareStatusChanged();
		UpdateFile(pFile);
	}
}

bool CSharedFilesCtrl::CheckBoxesEnabled() const
{
	return m_pDirectoryFilter != NULL && m_pDirectoryFilter->m_eItemType == SDI_UNSHAREDDIRECTORY;
}

void CSharedFilesCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	// highlighting Checkboxes
	if (CheckBoxesEnabled()) {
		// are we currently on any checkbox?
		int iItem = HitTest(point);
		if (iItem >= 0) {
			CRect rcItem;
			if (GetItemRect(iItem, rcItem, LVIR_BOUNDS)) {
				rcItem.left += sm_iIconOffset;
				rcItem.right = rcItem.left + 16;
				rcItem.top += (rcItem.Height() > 16) ? ((rcItem.Height() - 15) / 2) : 0;
				rcItem.bottom = rcItem.top + 16;
				if (rcItem.PtInRect(point)) {
					// is this checkbox already hot?
					CShareableFile *pHoveredItem = GetFileByIndex(iItem);
					if (pHoveredItem == NULL) {
						CMuleListCtrl::OnMouseMove(nFlags, point);
						return;
					}
					if (m_pHighlightedItem != pHoveredItem) {
						// update old highlighted item
						CShareableFile *pOldItem = m_pHighlightedItem;
						m_pHighlightedItem = pHoveredItem;
						UpdateFile(pOldItem, false);
						// highlight current item
						InvalidateRect(rcItem);
					}
					CMuleListCtrl::OnMouseMove(nFlags, point);
					return;
				}
			}
		}
		// no checkbox should be hot
		if (m_pHighlightedItem != NULL) {
			CShareableFile *pOldItem = m_pHighlightedItem;
			m_pHighlightedItem = NULL;
			UpdateFile(pOldItem, false);
		}
	}
	CMuleListCtrl::OnMouseMove(nFlags, point);
}

CSharedFilesCtrl::CShareDropTarget::CShareDropTarget()
{
	m_pParent = NULL;
	m_bUseDnDHelper = SUCCEEDED(m_piDropHelper.CoCreateInstance(CLSID_DragDropHelper));
}

CSharedFilesCtrl::CShareDropTarget::~CShareDropTarget()
{
}

DROPEFFECT CSharedFilesCtrl::CShareDropTarget::OnDragEnter(CWnd *pWnd, COleDataObject *pDataObject, DWORD /*dwKeyState*/, CPoint point)
{
	DROPEFFECT dwEffect = pDataObject->IsDataAvailable(CF_HDROP) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
	if (m_bUseDnDHelper) {
		IDataObject *piDataObj = pDataObject->GetIDataObject(FALSE);
		m_piDropHelper->DragEnter(pWnd->GetSafeHwnd(), piDataObj, &point, dwEffect);
	}
	return dwEffect;
}

DROPEFFECT CSharedFilesCtrl::CShareDropTarget::OnDragOver(CWnd*, COleDataObject *pDataObject, DWORD, CPoint point)
{
	DROPEFFECT dwEffect = pDataObject->IsDataAvailable(CF_HDROP) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
	if (m_bUseDnDHelper)
		m_piDropHelper->DragOver(&point, dwEffect);
	return dwEffect;
}

BOOL CSharedFilesCtrl::CShareDropTarget::OnDrop(CWnd*, COleDataObject *pDataObject, DROPEFFECT dropEffect, CPoint point)
{
	HGLOBAL hGlobal = pDataObject->GetGlobalData(CF_HDROP);
	if (hGlobal != NULL) {
		HDROP hDrop = (HDROP)::GlobalLock(hGlobal);
		if (hDrop != NULL) {
			CString strFilePath;
			CStringList liToAddFiles; // all files to add
			CStringList liToAddDirs; // all directories to add
			bool bFromSingleDirectory = true;	// all files are in the same directory,
			CString strSingleDirectory;			// which would be this one

			UINT nFileCount = ::DragQueryFile(hDrop, UINT_MAX, NULL, 0);
			for (UINT nFile = 0; nFile < nFileCount; ++nFile) {
				const UINT cchFilePath = ::DragQueryFile(hDrop, nFile, NULL, 0);
				if (cchFilePath > 0) {
					VERIFY(::DragQueryFile(hDrop, nFile, strFilePath.GetBuffer(cchFilePath + 1), cchFilePath + 1) == cchFilePath);
					strFilePath.ReleaseBuffer(cchFilePath);
					const CString strDroppedPath(PathHelpers::NormalizePathSeparators(strFilePath));
					CString strCanonicalDropPath;
					DWORD dwCanonicalizeError = ERROR_SUCCESS;
					if (!TryCanonicalizeDroppedSharedPath(strDroppedPath, strCanonicalDropPath, dwCanonicalizeError)) {
						if (dwCanonicalizeError == ERROR_FILE_NOT_FOUND || dwCanonicalizeError == ERROR_PATH_NOT_FOUND) {
							DebugLogError(_T("Drag&Drop'ed shared File not found (%s)"), (LPCTSTR)strDroppedPath);
						} else {
							LogWarning(LOG_STATUSBAR, _T("Rejected dropped shared path \"%s\" because canonical long-path resolution failed (%s)."), (LPCTSTR)strDroppedPath, (LPCTSTR)GetErrorMessage(dwCanonicalizeError));
						}
						continue;
					}

					WIN32_FILE_ATTRIBUTE_DATA fileData = {};
					if (LongPathSeams::GetFileAttributesEx(strCanonicalDropPath, GetFileExInfoStandard, &fileData)) {
						const CString strNormalizedPath(strCanonicalDropPath);
						const bool bIsDirectory = (fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
						const CString ffpath(bIsDirectory ? PathHelpers::EnsureTrailingSeparator(strNormalizedPath) : strNormalizedPath);
						const CString strLeafPath(PathHelpers::TrimTrailingSeparatorForLeaf(ffpath));
						const int iLastSlash = strLeafPath.ReverseFind(_T('\\'));
						const CString strParentDirectory(iLastSlash >= 0 ? strLeafPath.Left(iLastSlash) : CString());
						const CString strDisplayDirectory(iLastSlash >= 0 ? strLeafPath.Left(iLastSlash + 1) : CString());
						const CString strFileName(iLastSlash >= 0 ? strLeafPath.Mid(iLastSlash + 1) : strLeafPath);
						const bool bIsDots = strFileName == _T(".") || strFileName == _T("..");
						const bool bIsSystem = (fileData.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0;
						const bool bIsTemporary = (fileData.dwFileAttributes & FILE_ATTRIBUTE_TEMPORARY) != 0;
						ULARGE_INTEGER fileSize = {};
						fileSize.LowPart = fileData.nFileSizeLow;
						fileSize.HighPart = fileData.nFileSizeHigh;

						// just a quick pre-check, complete check is done later in the share function itself
						if (bIsDots || bIsSystem || bIsTemporary
							|| (!bIsDirectory && (ShouldIgnoreSharedFileCandidate(ffpath, strFileName)
								|| fileSize.QuadPart == 0 || fileSize.QuadPart > MAX_EMULE_FILE_SIZE
								|| theApp.sharedfiles->ShouldBeShared(strParentDirectory, ffpath, false)))
							|| (bIsDirectory && (!thePrefs.IsShareableDirectory(ffpath)
								|| theApp.sharedfiles->ShouldBeShared(ffpath, NULL, false))))
						{
							DebugLog(_T("Drag&Drop'ed shared File ignored (%s)"), (LPCTSTR)ffpath);
						} else if (bIsDirectory) {
							DEBUG_ONLY(DebugLog(_T("Drag&Drop'ed directory: %s"), (LPCTSTR)ffpath));
							liToAddDirs.AddTail(ffpath);
						} else {
							DEBUG_ONLY(DebugLog(_T("Drag&Drop'ed file: %s"), (LPCTSTR)ffpath));
							liToAddFiles.AddTail(ffpath);
							if (bFromSingleDirectory) {
								if (strSingleDirectory.IsEmpty())
									strSingleDirectory = strDisplayDirectory;
								else if (!AreSharedFilesDirectoriesEqual(strSingleDirectory, strDisplayDirectory))
									bFromSingleDirectory = false;
							}
						}
					} else
						DebugLogError(_T("Drag&Drop'ed shared File not found (%s)"), (LPCTSTR)strCanonicalDropPath);
				} else {
					ASSERT(0);
				}
			}

			if (!liToAddFiles.IsEmpty() || !liToAddDirs.IsEmpty()) {
				// add the directories first as this would invalidate addition of
				// single files, contained in one of those dirs
				bool bHaveDirectories = false;
				for (POSITION pos = liToAddDirs.GetHeadPosition(); pos != NULL;)
					bHaveDirectories |= theApp.sharedfiles->AddSingleSharedDirectory(liToAddDirs.GetNext(pos));

				bool bHaveFiles = false;
				while (!liToAddFiles.IsEmpty())
					bHaveFiles |= theApp.sharedfiles->AddSingleSharedFile(liToAddFiles.RemoveHead()); // could fail, due to the dirs added above

				// GUI updates
				if (!liToAddDirs.IsEmpty())
					theApp.emuledlg->sharedfileswnd->m_ctlSharedDirTree.Reload(true);
				if (bHaveFiles)
					theApp.emuledlg->sharedfileswnd->OnSingleFileShareStatusChanged();
				m_pParent->ShowFilesCount();

				if (bHaveFiles && liToAddDirs.IsEmpty() && bFromSingleDirectory) {
					// if we added only files from the same directory, show and select this in the file system tree
					ASSERT(!strSingleDirectory.IsEmpty());
					VERIFY(theApp.emuledlg->sharedfileswnd->m_ctlSharedDirTree.ShowFileSystemDirectory(strSingleDirectory));
				} else if (!liToAddDirs.IsEmpty() && !bHaveFiles && bHaveDirectories) {
					// only directories added, if only one select the specific shared dir, otherwise the Shared Directories section
					const CString &sShow(liToAddDirs.GetCount() == 1 ? liToAddDirs.GetHead() : _T(""));
					theApp.emuledlg->sharedfileswnd->m_ctlSharedDirTree.ShowSharedDirectory(sShow);
				} else {
					// otherwise select the All Shared Files category
					theApp.emuledlg->sharedfileswnd->m_ctlSharedDirTree.ShowAllSharedFiles();
				}
			}
			::GlobalUnlock(hGlobal);
		}
		::GlobalFree(hGlobal);
	}

	if (m_bUseDnDHelper) {
		IDataObject *piDataObj = pDataObject->GetIDataObject(FALSE);
		m_piDropHelper->Drop(piDataObj, &point, dropEffect);
	}

	return TRUE;
}

void CSharedFilesCtrl::CShareDropTarget::OnDragLeave(CWnd*)
{
	if (m_bUseDnDHelper)
		m_piDropHelper->DragLeave();
}
