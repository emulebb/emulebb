//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( devs@emule-project.net / https://www.emule-project.net )
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
#include "SharedDirsTreeCtrl.h"
#include "preferences.h"
#include "otherfunctions.h"
#include "SharedFilesCtrl.h"
#include "MenuCmds.h"
#include "partfile.h"
#include "ShellUiHelpers.h"
#include "emuledlg.h"
#include "TransferDlg.h"
#include "SharedFileList.h"
#include "SharedFilesWnd.h"
#include "SharedDirectoryOps.h"
#include "FileListKeyboardShortcutsSeams.h"
#include "MenuShortcutLabels.h"

#include <algorithm>
#include <vector>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

CString GetFolderLabel(const CString &strFolderPath, bool bTopFolder, bool bAccessible);

namespace
{
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

CString BuildSharedTreePathKey(const CString &rstrPath)
{
	CString strKey(PathHelpers::EnsureTrailingSeparator(rstrPath));
	strKey.MakeLower();
	return strKey;
}

bool AreSharedTreePathKeysEqual(const CString &rstrLeft, const CString &rstrRight)
{
	return BuildSharedTreePathKey(rstrLeft) == BuildSharedTreePathKey(rstrRight);
}

bool SharedDirectoryKeyLess(const CString &rstrLeft, const CString &rstrRight)
{
	return rstrLeft.Compare(rstrRight) < 0;
}

bool HasSharedDirectoryKeyPrefix(const CString &rstrPrefix, const CString &rstrCandidate)
{
	return rstrCandidate.GetLength() > rstrPrefix.GetLength()
		&& _tcsncmp(rstrCandidate, rstrPrefix, rstrPrefix.GetLength()) == 0;
}

bool ListContainsEquivalentPath(const CStringList &rList, const CString &rstrPath)
{
	const CString strPathKey(BuildSharedTreePathKey(rstrPath));
	for (POSITION pos = rList.GetHeadPosition(); pos != NULL;) {
		if (BuildSharedTreePathKey(rList.GetNext(pos)) == strPathKey)
			return true;
	}
	return false;
}

bool RemoveEquivalentPath(CStringList &rList, const CString &rstrPath)
{
	const CString strPathKey(BuildSharedTreePathKey(rstrPath));
	for (POSITION pos = rList.GetHeadPosition(); pos != NULL;) {
		const POSITION posCurrent = pos;
		if (BuildSharedTreePathKey(rList.GetNext(pos)) != strPathKey)
			continue;
		rList.RemoveAt(posCurrent);
		return true;
	}
	return false;
}

bool RemovePathsWithinDirectory(CStringList &rList, const CString &rstrPath, bool bIncludeRoot)
{
	bool bChanged = false;
	const CString strPathKey(BuildSharedTreePathKey(rstrPath));
	for (POSITION pos = rList.GetHeadPosition(); pos != NULL;) {
		const POSITION posCurrent = pos;
		const CString strCurrent(rList.GetNext(pos));
		const CString strCurrentKey(BuildSharedTreePathKey(strCurrent));
		const bool bSamePath = strCurrentKey == strPathKey;
		if ((!bIncludeRoot && bSamePath)
			|| (!bSamePath && !HasSharedDirectoryKeyPrefix(strPathKey, strCurrentKey)))
		{
			continue;
		}
		rList.RemoveAt(posCurrent);
		bChanged = true;
	}
	return bChanged;
}

bool IsAccessibleDirectoryForSharedTree(const CString &rstrDirectory)
{
	const LongPathSeams::PathString strNormalized = LongPathSeams::NormalizeAbsolutePathSeparators(rstrDirectory);
	const LongPathSeams::PathString strLogical = LongPathSeams::StripLongPathPrefixRaw(strNormalized);
	const bool bRequiresExactNamePrefix = LongPathSeams::RequiresExtendedLengthPathForExactNameRaw(strLogical);
	const LongPathSeams::PathString strPrepared = LongPathSeams::HasLongPathPrefix(strNormalized.c_str())
		? strNormalized
		: LongPathSeams::PreparePathForLongPathRaw(strNormalized, strNormalized, bRequiresExactNamePrefix);
	const DWORD dwAttributes = ::GetFileAttributes(strPrepared.c_str());
	return dwAttributes != INVALID_FILE_ATTRIBUTES && (dwAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

CDirectoryItem* InsertSharedDirectoryItem(CSharedDirsTreeCtrl *pTreeCtrl, CDirectoryItem *pParentItem, const CString &rstrPath, bool bTopFolder, bool bAccessible, bool &rbShowWarning)
{
	const CString strName(GetFolderLabel(rstrPath, bTopFolder, bAccessible));
	CDirectoryItem *pNewItem = new CDirectoryItem(rstrPath);
	pNewItem->m_htItem = pTreeCtrl->InsertItem(TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE, strName, 5, 5, 0, 0, (LPARAM)pNewItem, pParentItem->m_htItem, TVI_SORT);
	if (!bAccessible) {
		pTreeCtrl->SetItemState(pNewItem->m_htItem, INDEXTOOVERLAYMASK(2), TVIS_OVERLAYMASK);
		rbShowWarning = true;
	}

	pParentItem->liSubDirectories.AddTail(pNewItem);
	return pNewItem;
}

struct SSharedTreeEntry
{
	explicit SSharedTreeEntry(const CString &rstrPath)
		: strPath(rstrPath)
		, iParentIndex(-1)
		, bAccessible(false)
		, pItem(NULL)
	{
		strPath = PathHelpers::EnsureTrailingSeparator(strPath);
		strKey = BuildSharedTreePathKey(strPath);
	}

	CString strPath;
	CString strKey;
	int iParentIndex;
	bool bAccessible;
	CDirectoryItem *pItem;
};

CString GetSharedTreeParentPath(const CString &rstrPath)
{
	CString strParent(PathHelpers::EnsureTrailingSeparator(rstrPath));
	if (strParent.IsEmpty())
		return CString();

	const CString strTrimmed(PathHelpers::TrimTrailingSeparator(strParent));
	const PathHelpers::ParsedPathRoot root(PathHelpers::ParsePathRoot(strTrimmed));
	if (!root.strPrefix.IsEmpty() && strTrimmed.GetLength() == root.strPrefix.GetLength())
		return CString();

	strParent = strTrimmed;
	const int iPos = strParent.ReverseFind(_T('\\'));
	if (iPos < 0)
		return CString();

	strParent = strParent.Left(iPos + 1);
	return strParent;
}

void BuildSharedDirectoryTree(CSharedDirsTreeCtrl *pTreeCtrl, CDirectoryItem *pRootItem, const CStringList &rliDirs, bool &rbShowWarning)
{
#if EMULE_COMPILED_STARTUP_PROFILING
	const ULONGLONG ullStart = theApp.GetStartupProfileTimestampUs();
	theApp.AppendStartupProfileLine(_T("BuildSharedDirectoryTree begin"), 0);
	theApp.AppendStartupProfileCounter(_T("shared.tree.requested_entries"), static_cast<ULONGLONG>(rliDirs.GetCount()), _T("directories"));
#endif
	std::vector<SSharedTreeEntry> aEntries;
	aEntries.reserve((size_t)rliDirs.GetCount());

	CMapStringToPtr mapSeen;
	for (POSITION pos = rliDirs.GetHeadPosition(); pos != NULL;) {
		CString strDir(PathHelpers::EnsureTrailingSeparator(rliDirs.GetNext(pos)));
		if (strDir.IsEmpty())
			continue;

		const CString strKey(BuildSharedTreePathKey(strDir));
		void *pvSeen = NULL;
		if (mapSeen.Lookup(strKey, pvSeen))
			continue;

		mapSeen.SetAt(strKey, (void*)1);
		aEntries.emplace_back(strDir);
	}
#if EMULE_COMPILED_STARTUP_PROFILING
	{
		CString strPhase;
		strPhase.Format(_T("BuildSharedDirectoryTree deduped (%d entries)"), static_cast<int>(aEntries.size()));
		theApp.AppendStartupProfileLine(strPhase, theApp.GetStartupProfileElapsedUs(ullStart));
		theApp.AppendStartupProfileCounter(_T("shared.tree.deduped_entries"), static_cast<ULONGLONG>(aEntries.size()), _T("directories"));
	}
#endif

	if (aEntries.empty())
		return;

	std::sort(aEntries.begin(), aEntries.end(), [](const SSharedTreeEntry &left, const SSharedTreeEntry &right) {
		const int iLeftLen = left.strPath.GetLength();
		const int iRightLen = right.strPath.GetLength();
		if (iLeftLen != iRightLen)
			return iLeftLen < iRightLen;

		return left.strPath.CompareNoCase(right.strPath) < 0;
	});
#if EMULE_COMPILED_STARTUP_PROFILING
	theApp.AppendStartupProfileLine(_T("BuildSharedDirectoryTree sorted"), theApp.GetStartupProfileElapsedUs(ullStart));
#endif

	CMapStringToPtr mapIndexByKey;
	for (size_t i = 0; i < aEntries.size(); ++i)
		mapIndexByKey.SetAt(aEntries[i].strKey, (void*)(INT_PTR)(i + 1));
#if EMULE_COMPILED_STARTUP_PROFILING
	theApp.AppendStartupProfileLine(_T("BuildSharedDirectoryTree indexed"), theApp.GetStartupProfileElapsedUs(ullStart));
#endif

	size_t uAccessibleEntries = 0;
	size_t uInaccessibleEntries = 0;
	for (size_t i = 0; i < aEntries.size(); ++i) {
		SSharedTreeEntry &rCurrent = aEntries[i];
#if EMULE_COMPILED_STARTUP_PROFILING
		{
			CString strPhase;
			strPhase.Format(_T("BuildSharedDirectoryTree item %d start: %s"), static_cast<int>(i), (LPCTSTR)rCurrent.strPath);
			theApp.AppendStartupProfileLine(strPhase, theApp.GetStartupProfileElapsedUs(ullStart));
		}
#endif

		CString strParent = GetSharedTreeParentPath(rCurrent.strPath);
		while (!strParent.IsEmpty()) {
			CString strParentKey(strParent);
			strParentKey.MakeLower();

			void *pvIndex = NULL;
			if (mapIndexByKey.Lookup(strParentKey, pvIndex)) {
				rCurrent.iParentIndex = static_cast<int>((INT_PTR)pvIndex - 1);
				break;
			}

			strParent = GetSharedTreeParentPath(strParent);
		}

		CDirectoryItem *pParentItem = pRootItem;
		bool bParentAccessible = true;
		if (rCurrent.iParentIndex >= 0) {
			SSharedTreeEntry &rParent = aEntries[(size_t)rCurrent.iParentIndex];
			if (rParent.pItem != NULL)
				pParentItem = rParent.pItem;
			bParentAccessible = rParent.bAccessible;
		}

		rCurrent.bAccessible = bParentAccessible ? IsAccessibleDirectoryForSharedTree(rCurrent.strPath) : false;
		if (rCurrent.bAccessible)
			++uAccessibleEntries;
		else
			++uInaccessibleEntries;
#if EMULE_COMPILED_STARTUP_PROFILING
		{
			CString strPhase;
			strPhase.Format(_T("BuildSharedDirectoryTree item %d accessible=%d"), static_cast<int>(i), rCurrent.bAccessible ? 1 : 0);
			theApp.AppendStartupProfileLine(strPhase, theApp.GetStartupProfileElapsedUs(ullStart));
		}
#endif
		rCurrent.pItem = InsertSharedDirectoryItem(pTreeCtrl, pParentItem, rCurrent.strPath, rCurrent.iParentIndex < 0, rCurrent.bAccessible, rbShowWarning);
	}
#if EMULE_COMPILED_STARTUP_PROFILING
	theApp.AppendStartupProfileCounter(_T("shared.tree.inserted_entries"), static_cast<ULONGLONG>(aEntries.size()), _T("directories"));
	theApp.AppendStartupProfileCounter(_T("shared.tree.accessible_entries"), static_cast<ULONGLONG>(uAccessibleEntries), _T("directories"));
	theApp.AppendStartupProfileCounter(_T("shared.tree.inaccessible_entries"), static_cast<ULONGLONG>(uInaccessibleEntries), _T("directories"));
	theApp.AppendStartupProfileLine(_T("BuildSharedDirectoryTree done"), theApp.GetStartupProfileElapsedUs(ullStart));
#endif
}
}

//**********************************************************************************
// CDirectoryItem

CDirectoryItem::CDirectoryItem(const CString &strFullPath, HTREEITEM htItem, ESpecialDirectoryItems eItemType, int nCatFilter)
	: m_strFullPath(strFullPath)
	, m_htItem(htItem)
	, m_nCatFilter(nCatFilter)
	, m_eItemType(eItemType)
{
}

CDirectoryItem::~CDirectoryItem()
{
	while (!liSubDirectories.IsEmpty())
		delete liSubDirectories.RemoveHead();
}

// search tree for a given filter
HTREEITEM CDirectoryItem::FindItem(CDirectoryItem *pContentToFind) const
{
	if (pContentToFind == NULL) {
		ASSERT(0);
		return NULL;
	}

	if (pContentToFind->m_eItemType == m_eItemType && pContentToFind->m_strFullPath == m_strFullPath && pContentToFind->m_nCatFilter == m_nCatFilter)
		return m_htItem;

	for (POSITION pos = liSubDirectories.GetHeadPosition(); pos != NULL;) {
		HTREEITEM htResult = liSubDirectories.GetNext(pos)->FindItem(pContentToFind);
		if (htResult != NULL)
			return htResult;
	}
	return NULL;
}

//**********************************************************************************
// CSharedDirsTreeCtrl


IMPLEMENT_DYNAMIC(CSharedDirsTreeCtrl, CTreeCtrl)

BEGIN_MESSAGE_MAP(CSharedDirsTreeCtrl, CTreeCtrl)
	ON_WM_CONTEXTMENU()
	ON_WM_KEYDOWN()
	ON_WM_RBUTTONDOWN()
	ON_WM_MOUSEMOVE()
	ON_WM_CANCELMODE()
	ON_WM_LBUTTONUP()
	ON_WM_SYSCOLORCHANGE()
	ON_NOTIFY_REFLECT(TVN_ITEMEXPANDING, OnTvnItemexpanding)
	ON_NOTIFY_REFLECT(TVN_GETDISPINFO, OnTvnGetdispinfo)
	ON_NOTIFY_REFLECT(TVN_BEGINDRAG, OnTvnBeginDrag)
END_MESSAGE_MAP()

CSharedDirsTreeCtrl::CSharedDirsTreeCtrl()
	: m_pRootDirectoryItem()
	, m_pRootUnsharedDirectries()
	, m_pDraggingItem()
	, m_pSharedFilesCtrl()
	, m_bFileSystemRootDirty()
	, m_bCreatingTree()
	, m_bUseIcons()
{
}

CSharedDirsTreeCtrl::~CSharedDirsTreeCtrl()
{
	delete m_pRootDirectoryItem;
	delete m_pRootUnsharedDirectries;
}

void CSharedDirsTreeCtrl::Initialize(CSharedFilesCtrl *pSharedFilesCtrl)
{
#if EMULE_COMPILED_STARTUP_PROFILING
	const ULONGLONG ullInitStart = theApp.GetStartupProfileTimestampUs();
	ULONGLONG ullPhaseStart = ullInitStart;
#endif
	m_pSharedFilesCtrl = pSharedFilesCtrl;

	m_bUseIcons = true;
	SetAllIcons();
#if EMULE_COMPILED_STARTUP_PROFILING
	theApp.AppendStartupProfileLine(_T("CSharedDirsTreeCtrl::Initialize SetAllIcons"), theApp.GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = theApp.GetStartupProfileTimestampUs();
#endif
	Localize();
#if EMULE_COMPILED_STARTUP_PROFILING
	theApp.AppendStartupProfileLine(_T("CSharedDirsTreeCtrl::Initialize Localize"), theApp.GetStartupProfileElapsedUs(ullPhaseStart));
	theApp.AppendStartupProfileLine(_T("CSharedDirsTreeCtrl::Initialize total"), theApp.GetStartupProfileElapsedUs(ullInitStart));
#endif
}

void CSharedDirsTreeCtrl::RebuildSharedDirectoryLookup()
{
	m_mapSharedDirectoryKeys.RemoveAll();
	m_aSortedSharedDirectoryKeys.clear();
	m_aSortedSharedDirectoryKeys.reserve(static_cast<size_t>(m_strliSharedDirs.GetCount()));

	for (POSITION pos = m_strliSharedDirs.GetHeadPosition(); pos != NULL;) {
		const CString strKey(BuildSharedTreePathKey(m_strliSharedDirs.GetNext(pos)));
		void *pvExisting = NULL;
		if (m_mapSharedDirectoryKeys.Lookup(strKey, pvExisting))
			continue;

		m_mapSharedDirectoryKeys.SetAt(strKey, reinterpret_cast<void*>(1));
		m_aSortedSharedDirectoryKeys.push_back(strKey);
	}

	std::sort(m_aSortedSharedDirectoryKeys.begin(), m_aSortedSharedDirectoryKeys.end(), SharedDirectoryKeyLess);
}

bool CSharedDirsTreeCtrl::HasSharedDirectoryDescendant(const CString &strDir) const
{
	if (m_aSortedSharedDirectoryKeys.empty())
		return false;

	const CString strKey(BuildSharedTreePathKey(strDir));
	const std::vector<CString>::const_iterator it = std::upper_bound(
		m_aSortedSharedDirectoryKeys.begin(),
		m_aSortedSharedDirectoryKeys.end(),
		strKey,
		[](const CString &rstrLeft, const CString &rstrRight) {
			return SharedDirectoryKeyLess(rstrLeft, rstrRight);
		});
	return it != m_aSortedSharedDirectoryKeys.end() && HasSharedDirectoryKeyPrefix(strKey, *it);
}

bool CSharedDirsTreeCtrl::IsSharedTreeDirectoryAccessible(const CString &strDir)
{
	const CString strKey(BuildSharedTreePathKey(strDir));
	void *pCachedValue = NULL;
	if (m_mapAccessibleDirectoryCache.Lookup(strKey, pCachedValue))
		return pCachedValue != NULL;

	const bool bAccessible = LongPathSeams::PathExists(strDir);
	m_mapAccessibleDirectoryCache.SetAt(strKey, bAccessible ? reinterpret_cast<void*>(1) : NULL);
	return bAccessible;
}

void CSharedDirsTreeCtrl::OnSysColorChange()
{
	CTreeCtrl::OnSysColorChange();
	SetAllIcons();
	CreateMenus();
}

void CSharedDirsTreeCtrl::SetAllIcons()
{
	// This treeview control contains an image list which contains our own icons and a
	// couple of icons which are copied from the Windows System image list. To properly
	// support an update of the control and the image list, we need to 'replace' our own
	// images so that we are able to keep the already stored images from the Windows System
	// image list.

	//Should be terminated with a backslash for a directory image
	int nImage = theApp.GetFileTypeSystemImageIdx(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));

	CImageList *pCurImageList = GetImageList(TVSIL_NORMAL);
	if (pCurImageList != NULL && pCurImageList->GetImageCount() >= 7) {
		pCurImageList->Replace(0, CTempIconLoader(_T("AllFiles")));			// 0: All Directory
		pCurImageList->Replace(1, CTempIconLoader(_T("Incomplete")));		// 1: Temp Directory
		pCurImageList->Replace(2, CTempIconLoader(_T("Incoming")));			// 2: Incoming Directory
		pCurImageList->Replace(3, CTempIconLoader(_T("Category")));			// 3: Cats
		pCurImageList->Replace(4, CTempIconLoader(_T("HardDisk")));			// 4: All Dirs
		if (nImage > 0 && theApp.GetSystemImageList() != NULL) {			// 5: System Folder Icon
			HICON hIcon = ::ImageList_GetIcon(theApp.GetSystemImageList(), nImage, 0);
			pCurImageList->Replace(5, hIcon);
			::DestroyIcon(hIcon);
		} else
			pCurImageList->Replace(5, CTempIconLoader(_T("OpenFolder")));

		pCurImageList->Replace(6, CTempIconLoader(_T("SharedFolderOvl")));	// 6: Overlay
		pCurImageList->Replace(7, CTempIconLoader(_T("NoAccessFolderOvl")));// 7: Overlay
	} else {
		CImageList iml;
		iml.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
		iml.Add(CTempIconLoader(_T("AllFiles")));							// 0: All Directory
		iml.Add(CTempIconLoader(_T("Incomplete")));							// 1: Temp Directory
		iml.Add(CTempIconLoader(_T("Incoming")));							// 2: Incoming Directory
		iml.Add(CTempIconLoader(_T("Category")));							// 3: Cats
		iml.Add(CTempIconLoader(_T("HardDisk")));							// 4: All Dirs
		if (nImage > 0 && theApp.GetSystemImageList() != NULL) {			// 5: System Folder Icon
			HICON hIcon = ::ImageList_GetIcon(theApp.GetSystemImageList(), nImage, 0);
			iml.Add(hIcon);
			::DestroyIcon(hIcon);
		} else
			iml.Add(CTempIconLoader(_T("OpenFolder")));

		iml.SetOverlayImage(iml.Add(CTempIconLoader(_T("SharedFolderOvl"))), 1);	// 6: Overlay
		iml.SetOverlayImage(iml.Add(CTempIconLoader(_T("NoAccessFolderOvl"))), 2);	// 7: Overlay

		SetImageList(&iml, TVSIL_NORMAL);
		m_mapSystemIcons.RemoveAll();
		m_imlTree.DeleteImageList();
		m_imlTree.Attach(iml.Detach());
	}

	COLORREF crBk = ::GetSysColor(COLOR_WINDOW);
	COLORREF crFg = ::GetSysColor(COLOR_WINDOWTEXT);
	theApp.LoadSkinColorAlt(_T("SharedDirsTvBk"), _T("SharedFilesLvBk"), crBk);
	theApp.LoadSkinColorAlt(_T("SharedDirsTvFg"), _T("SharedFilesLvFg"), crFg);
	SetBkColor(crBk);
	SetTextColor(crFg);
}

void CSharedDirsTreeCtrl::Localize()
{
#if EMULE_COMPILED_STARTUP_PROFILING
	const ULONGLONG ullStart = theApp.GetStartupProfileTimestampUs();
	ULONGLONG ullPhaseStart = ullStart;
#endif
	InitalizeStandardItems();
#if EMULE_COMPILED_STARTUP_PROFILING
	theApp.AppendStartupProfileLine(_T("CSharedDirsTreeCtrl::Localize InitalizeStandardItems"), theApp.GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = theApp.GetStartupProfileTimestampUs();
#endif
	FilterTreeReloadTree();
#if EMULE_COMPILED_STARTUP_PROFILING
	theApp.AppendStartupProfileLine(_T("CSharedDirsTreeCtrl::Localize FilterTreeReloadTree"), theApp.GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = theApp.GetStartupProfileTimestampUs();
#endif
	CreateMenus();
#if EMULE_COMPILED_STARTUP_PROFILING
	theApp.AppendStartupProfileLine(_T("CSharedDirsTreeCtrl::Localize CreateMenus"), theApp.GetStartupProfileElapsedUs(ullPhaseStart));
	theApp.AppendStartupProfileLine(_T("CSharedDirsTreeCtrl::Localize total"), theApp.GetStartupProfileElapsedUs(ullStart));
#endif
}

void CSharedDirsTreeCtrl::InitalizeStandardItems()
{
#if EMULE_COMPILED_STARTUP_PROFILING
	const ULONGLONG ullStart = theApp.GetStartupProfileTimestampUs();
	ULONGLONG ullPhaseStart = ullStart;
#endif
	// add standard items
	DeleteAllItems();
	delete m_pRootDirectoryItem;
	delete m_pRootUnsharedDirectries;

	FetchSharedDirsList();
#if EMULE_COMPILED_STARTUP_PROFILING
	{
		CString strPhase;
		strPhase.Format(_T("CSharedDirsTreeCtrl::InitalizeStandardItems FetchSharedDirsList (%d shared dirs)"), static_cast<int>(m_strliSharedDirs.GetCount()));
		theApp.AppendStartupProfileLine(strPhase, theApp.GetStartupProfileElapsedUs(ullPhaseStart));
		theApp.AppendStartupProfileCounter(_T("shared.tree.configured_shared_dirs"), static_cast<ULONGLONG>(m_strliSharedDirs.GetCount()), _T("directories"));
		ullPhaseStart = theApp.GetStartupProfileTimestampUs();
	}
#endif

	static const CString sEmpty;
	m_pRootDirectoryItem = new CDirectoryItem(sEmpty, TVI_ROOT);
	CDirectoryItem *pAll = new CDirectoryItem(sEmpty, 0, SDI_ALL);
	pAll->m_htItem = InsertItem(TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE, GetResString(IDS_ALLSHAREDFILES), 0, 0, TVIS_EXPANDED, TVIS_EXPANDED, (LPARAM)pAll, TVI_ROOT, TVI_LAST);
	m_pRootDirectoryItem->liSubDirectories.AddTail(pAll);

	CDirectoryItem *pIncoming = new CDirectoryItem(sEmpty, pAll->m_htItem, SDI_INCOMING);
	pIncoming->m_htItem = InsertItem(TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE, GetResString(IDS_INCOMING_FILES), 2, 2, 0, 0, (LPARAM)pIncoming, pAll->m_htItem, TVI_LAST);
	m_pRootDirectoryItem->liSubDirectories.AddTail(pIncoming);

	CDirectoryItem *pTemp = new CDirectoryItem(sEmpty, pAll->m_htItem, SDI_TEMP);
	pTemp->m_htItem = InsertItem(TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE, GetResString(IDS_INCOMPLETE_FILES), 1, 1, 0, 0, (LPARAM)pTemp, pAll->m_htItem, TVI_LAST);
	m_pRootDirectoryItem->liSubDirectories.AddTail(pTemp);

	CDirectoryItem *pDir = new CDirectoryItem(sEmpty, pAll->m_htItem, SDI_DIRECTORY);
	pDir->m_htItem = InsertItem(TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE, GetResString(IDS_SHARED_DIRECTORIES), 5, 5, TVIS_EXPANDED, TVIS_EXPANDED, (LPARAM)pDir, pAll->m_htItem, TVI_LAST);
	m_pRootDirectoryItem->liSubDirectories.AddTail(pDir);

	m_pRootUnsharedDirectries = new CDirectoryItem(sEmpty, TVI_ROOT, SDI_FILESYSTEMPARENT);
	const CString &sAll(GetResString(IDS_ALLDIRECTORIES));
	TVINSERTSTRUCT tvis;
	tvis.hParent = TVI_ROOT;
	tvis.hInsertAfter = TVI_LAST;
	tvis.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_CHILDREN;
	tvis.item.pszText = const_cast<LPTSTR>((LPCTSTR)sAll);
	tvis.item.iImage = 4;
	tvis.item.iSelectedImage = 4;
	tvis.item.state = 0;
	tvis.item.stateMask = 0;
	tvis.item.lParam = (LPARAM)m_pRootUnsharedDirectries;
	tvis.item.cChildren = 1; //ensure '+' symbol for the item
	m_pRootUnsharedDirectries->m_htItem = InsertItem(&tvis);
#if EMULE_COMPILED_STARTUP_PROFILING
	theApp.AppendStartupProfileLine(_T("CSharedDirsTreeCtrl::InitalizeStandardItems insert roots"), theApp.GetStartupProfileElapsedUs(ullPhaseStart));
	theApp.AppendStartupProfileLine(_T("CSharedDirsTreeCtrl::InitalizeStandardItems total"), theApp.GetStartupProfileElapsedUs(ullStart));
#endif
}

bool CSharedDirsTreeCtrl::FilterTreeIsSubDirectory(const CString &strDir, const CString &strRoot, const CStringList &liDirs)
{
	ASSERT(strRoot.IsEmpty() || strRoot.Right(1) == _T("\\"));
	ASSERT(strDir.Right(1) == _T("\\"));
	const CString strDirKey(BuildSharedTreePathKey(strDir));
	const CString strRootKey(BuildSharedTreePathKey(strRoot));
	for (POSITION pos = liDirs.GetHeadPosition(); pos != NULL;) {
		const CString strCurrent(liDirs.GetNext(pos));
		ASSERT(strCurrent.Right(1) == _T("\\"));
		const CString strCurrentKey(BuildSharedTreePathKey(strCurrent));
		if ((!strRoot.IsEmpty() && strCurrentKey == strRootKey) || strCurrentKey == strDirKey)
			continue;
		if (HasSharedDirectoryKeyPrefix(strCurrentKey, strDirKey)
			&& (strRoot.IsEmpty() || !HasSharedDirectoryKeyPrefix(strCurrentKey, strRootKey)))
		{
			return true;
		}
	}
	return false;
}

CString GetFolderLabel(const CString &strFolderPath, bool bTopFolder, bool bAccessible)
{
	CString strLabel(PathHelpers::TrimTrailingSeparatorForLeaf(strFolderPath));
	if (strLabel.GetLength() == 2 && strLabel[1] == _T(':')) {
		ASSERT(bTopFolder);
		strLabel += _T('\\');
	} else {
		strLabel.Delete(0, strLabel.ReverseFind(_T('\\')) + 1);
		if (bTopFolder) {
			CString strParentFolder(PathHelpers::TrimTrailingSeparatorForLeaf(strFolderPath));
			::PathRemoveFileSpec(strParentFolder.GetBuffer());
			strParentFolder.ReleaseBuffer();
			strLabel.AppendFormat(_T("  (%s)"), (LPCTSTR)strParentFolder);
		}
	}
	if (!bAccessible && bTopFolder)
		strLabel.AppendFormat(_T(" [%s]"), (LPCTSTR)GetResString(IDS_NOTCONNECTED));

	return strLabel;
}

void CSharedDirsTreeCtrl::FilterTreeAddSubDirectories(CDirectoryItem *pDirectory, const CStringList &liDirs
	, int nLevel, bool &rbShowWarning, bool bParentAccessible)
{
	// just some sanity check against too deep shared dirs
	// shouldn't be needed, but never trust the file system or a recursive function ;)
	if (nLevel > 14) {
		ASSERT(0);
		return;
	}

	const CString &strDirectoryPath(pDirectory->m_strFullPath);
	const CString strDirectoryKey(BuildSharedTreePathKey(strDirectoryPath));
	for (POSITION pos = liDirs.GetHeadPosition(); pos != NULL;) { //all paths in liDirs should have a trailing backslash
		const CString &strCurrent(liDirs.GetNext(pos));
		const CString strCurrentKey(BuildSharedTreePathKey(strCurrent));
		if (!strDirectoryPath.IsEmpty() && !HasSharedDirectoryKeyPrefix(strDirectoryKey, strCurrentKey))
			continue;
		if (strCurrentKey == strDirectoryKey)
			continue;
		if (!FilterTreeIsSubDirectory(strCurrent, strDirectoryPath, liDirs)) {
			bool bAccessible = bParentAccessible ? IsSharedTreeDirectoryAccessible(strCurrent) : false;
			const CString &strName(GetFolderLabel(strCurrent, nLevel == 0, bAccessible));
			CDirectoryItem *pNewItem = new CDirectoryItem(strCurrent);
			pNewItem->m_htItem = InsertItem(TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE, strName, 5, 5, 0, 0, (LPARAM)pNewItem, pDirectory->m_htItem, TVI_SORT);
			if (!bAccessible) {
				SetItemState(pNewItem->m_htItem, INDEXTOOVERLAYMASK(2), TVIS_OVERLAYMASK);
				rbShowWarning = true;
			}
			pDirectory->liSubDirectories.AddTail(pNewItem);
			FilterTreeAddSubDirectories(pNewItem, liDirs, nLevel + 1, rbShowWarning, bAccessible);
		}
	}
}

void CSharedDirsTreeCtrl::FilterTreeReloadTree()
{
#if EMULE_COMPILED_STARTUP_PROFILING
	const ULONGLONG ullStart = theApp.GetStartupProfileTimestampUs();
	ULONGLONG ullPhaseStart = ullStart;
	theApp.AppendStartupProfileLine(_T("CSharedDirsTreeCtrl::FilterTreeReloadTree begin"), 0);
#endif
	m_bCreatingTree = true;
	m_mapAccessibleDirectoryCache.RemoveAll();
	// store current selection
	CDirectoryItem *pOldSelectedItem = NULL;
	if (GetSelectedFilter() != NULL)
		pOldSelectedItem = GetSelectedFilter()->CloneContent();
#if EMULE_COMPILED_STARTUP_PROFILING
	theApp.AppendStartupProfileLine(_T("CSharedDirsTreeCtrl::FilterTreeReloadTree selection snapshot"), theApp.GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = theApp.GetStartupProfileTimestampUs();
#endif

	// create the tree substructure of directories we want to show
	for (POSITION pos = m_pRootDirectoryItem->liSubDirectories.GetHeadPosition(); pos != NULL;) {
		CDirectoryItem *pCurrent = m_pRootDirectoryItem->liSubDirectories.GetNext(pos);
		// clear old items
		DeleteChildItems(pCurrent);
#if EMULE_COMPILED_STARTUP_PROFILING
		{
			CString strPhase;
			strPhase.Format(_T("CSharedDirsTreeCtrl::FilterTreeReloadTree branch start (%d)"), static_cast<int>(pCurrent->m_eItemType));
			theApp.AppendStartupProfileLine(strPhase, theApp.GetStartupProfileElapsedUs(ullPhaseStart));
			ullPhaseStart = theApp.GetStartupProfileTimestampUs();
		}
#endif

		switch (pCurrent->m_eItemType) {
		case SDI_ALL:
			break;
		case SDI_INCOMING:
			{
				CString strMainIncDir(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));

				bool bShowWarning = false;
				if (thePrefs.GetCatCount() > 1) {
					m_strliCatIncomingDirs.RemoveAll();
					for (INT_PTR i = 0; i < thePrefs.GetCatCount(); ++i) {
						Category_Struct *pCatStruct = thePrefs.GetCategory(i);
						if (pCatStruct != NULL) {
							const CString &strCatIncomingPath(pCatStruct->strIncomingPath);
							ASSERT(strCatIncomingPath.IsEmpty() || strCatIncomingPath.Right(1) == _T("\\"));
							if (!strCatIncomingPath.IsEmpty() && !AreSharedTreePathKeysEqual(strCatIncomingPath, strMainIncDir)
								&& !ListContainsEquivalentPath(m_strliCatIncomingDirs, strCatIncomingPath))
							{
								m_strliCatIncomingDirs.AddTail(strCatIncomingPath);
								bool bAccessible = IsSharedTreeDirectoryAccessible(strCatIncomingPath);
								const CString &strName(GetFolderLabel(strCatIncomingPath, true, bAccessible));
								CDirectoryItem *pCatInc = new CDirectoryItem(strCatIncomingPath, 0, SDI_CATINCOMING);
								pCatInc->m_htItem = InsertItem(TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE, strName, 5, 5, 0, 0, (LPARAM)pCatInc, pCurrent->m_htItem, TVI_SORT);
								if (!bAccessible) {
									SetItemState(pCatInc->m_htItem, INDEXTOOVERLAYMASK(2), TVIS_OVERLAYMASK);
									bShowWarning = true;
								}
								pCurrent->liSubDirectories.AddTail(pCatInc);
							}
						}
					}
				}
				SetItemState(pCurrent->m_htItem, bShowWarning ? INDEXTOOVERLAYMASK(2) : 0, TVIS_OVERLAYMASK);
			}
			break;
		case SDI_TEMP:
			if (thePrefs.GetCatCount() > 1) {
				for (INT_PTR i = 0; i < thePrefs.GetCatCount(); ++i) {
					Category_Struct *pCatStruct = thePrefs.GetCategory(i);
					if (pCatStruct != NULL) {
						//temp dir
						CDirectoryItem *pCatTemp = new CDirectoryItem(CString(), 0, SDI_TEMP, (int)i);
						pCatTemp->m_htItem = InsertItem(TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE, pCatStruct->strTitle, 3, 3, 0, 0, (LPARAM)pCatTemp, pCurrent->m_htItem, TVI_LAST);
						pCurrent->liSubDirectories.AddTail(pCatTemp);

					}
				}
			}
			break;
		case SDI_DIRECTORY:
			{
				bool bShowWarning = false;
				BuildSharedDirectoryTree(this, pCurrent, m_strliSharedDirs, bShowWarning);
				SetItemState(pCurrent->m_htItem, bShowWarning ? INDEXTOOVERLAYMASK(2) : 0, TVIS_OVERLAYMASK);
			}
			break;
		default:
			ASSERT(0);
		}
#if EMULE_COMPILED_STARTUP_PROFILING
		{
			CString strPhase;
			strPhase.Format(_T("CSharedDirsTreeCtrl::FilterTreeReloadTree branch done (%d)"), static_cast<int>(pCurrent->m_eItemType));
			theApp.AppendStartupProfileLine(strPhase, theApp.GetStartupProfileElapsedUs(ullPhaseStart));
			ullPhaseStart = theApp.GetStartupProfileTimestampUs();
		}
#endif
	}

	// restore selection
	HTREEITEM htOldSection;
	if (pOldSelectedItem != NULL && (htOldSection = m_pRootDirectoryItem->FindItem(pOldSelectedItem)) != NULL) {
		Select(htOldSection, TVGN_CARET);
		EnsureVisible(htOldSection);
	} else if (GetSelectedItem() == NULL && !m_pRootDirectoryItem->liSubDirectories.IsEmpty())
		Select(m_pRootDirectoryItem->liSubDirectories.GetHead()->m_htItem, TVGN_CARET);
#if EMULE_COMPILED_STARTUP_PROFILING
	theApp.AppendStartupProfileLine(_T("CSharedDirsTreeCtrl::FilterTreeReloadTree selection restore"), theApp.GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = theApp.GetStartupProfileTimestampUs();
#endif

	delete pOldSelectedItem;
	m_bCreatingTree = false;
#if EMULE_COMPILED_STARTUP_PROFILING
	theApp.AppendStartupProfileLine(_T("CSharedDirsTreeCtrl::FilterTreeReloadTree before redraw"), theApp.GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = theApp.GetStartupProfileTimestampUs();
#endif
#if EMULE_COMPILED_STARTUP_PROFILING
	{
		CString strPhase;
		strPhase.Format(_T("CSharedDirsTreeCtrl::FilterTreeReloadTree total (%d shared dirs)"), static_cast<int>(m_strliSharedDirs.GetCount()));
		theApp.AppendStartupProfileLine(strPhase, theApp.GetStartupProfileElapsedUs(ullStart));
	}
#endif
}

CDirectoryItem* CSharedDirsTreeCtrl::GetSelectedFilter() const
{
	const HTREEITEM item = GetSelectedItem();
	return item ? reinterpret_cast<CDirectoryItem*>(GetItemData(item)) : NULL;
}

void CSharedDirsTreeCtrl::CreateMenus()
{
	if (m_PrioMenu)
		VERIFY(m_PrioMenu.DestroyMenu());
	if (m_SharedFilesMenu)
		VERIFY(m_SharedFilesMenu.DestroyMenu());
	if (m_ShareDirsMenu)
		VERIFY(m_ShareDirsMenu.DestroyMenu());

	m_PrioMenu.CreateMenu();
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOVERYLOW, GetResString(IDS_PRIOVERYLOW));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOLOW, GetResString(IDS_PRIOLOW));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIONORMAL, GetResString(IDS_PRIONORMAL));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOHIGH, GetResString(IDS_PRIOHIGH));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOVERYHIGH, GetResString(IDS_PRIORELEASE));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOAUTO, GetResString(IDS_PRIOAUTO));//UAP

	m_SharedFilesMenu.CreatePopupMenu();
	m_SharedFilesMenu.AddMenuTitle(GetResString(IDS_SHAREDFILES), true);
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_OPENFOLDER, AddMenuShortcutLabel(GetResString(IDS_OPENFOLDER), _T("Ctrl+Shift+O")), _T("OPENFOLDER"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_REMOVE, GetResString(IDS_DELETE), _T("DELETE"));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
	CString sPrio(GetResString(IDS_PRIORITY));
	sPrio.AppendFormat(_T(" (%s)"), (LPCTSTR)GetResString(IDS_PW_CON_UPLBL));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_PrioMenu.m_hMenu, sPrio, _T("FILEPRIORITY"));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_DETAIL, GetResString(IDS_SHOWDETAILS), _T("FILEINFO"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_CMT, GetResString(IDS_CMT_ADD), _T("FILECOMMENTS"));
	if (thePrefs.GetShowCopyEd2kLinkCmd())
		m_SharedFilesMenu.AppendMenu(MF_STRING, MP_GETED2KLINK, GetResString(IDS_DL_LINK1), _T("ED2KLINK"));
	else
		m_SharedFilesMenu.AppendMenu(MF_STRING, MP_SHOWED2KLINK, GetResString(IDS_DL_SHOWED2KLINK), _T("ED2KLINK"));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_UNSHAREDIR, GetResString(IDS_UNSHAREDIR));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_UNSHAREDIRSUB, GetResString(IDS_UNSHAREDIRSUB));

	m_ShareDirsMenu.CreatePopupMenu();
	m_ShareDirsMenu.AddMenuTitle(GetResString(IDS_SHAREDFILES), true);
	m_ShareDirsMenu.AppendMenu(MF_STRING, MP_OPENFOLDER, AddMenuShortcutLabel(GetResString(IDS_OPENFOLDER), _T("Ctrl+Shift+O")), _T("OPENFOLDER"));
	m_ShareDirsMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
	m_ShareDirsMenu.AppendMenu(MF_STRING, MP_SHAREDIR, GetResString(IDS_SHAREDIR));
	m_ShareDirsMenu.AppendMenu(MF_STRING, MP_SHAREDIRSUB, GetResString(IDS_SHAREDIRSUB));
	m_ShareDirsMenu.AppendMenu(MF_STRING, MP_SHAREDIRMONITOR, GetResString(IDS_SHAREDIRMONITOR));
	m_ShareDirsMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
	m_ShareDirsMenu.AppendMenu(MF_STRING, MP_UNSHAREDIR, GetResString(IDS_UNSHAREDIR));
	m_ShareDirsMenu.AppendMenu(MF_STRING, MP_UNSHAREDIRSUB, GetResString(IDS_UNSHAREDIRSUB));
}

void CSharedDirsTreeCtrl::OnContextMenu(CWnd*, CPoint point)
{
	if (!PointInClient(*this, point)) {
		Default();
		return;
	}

	CDirectoryItem *pSelectedDir = GetSelectedFilter();
	if (pSelectedDir != NULL && pSelectedDir->m_eItemType != SDI_UNSHAREDDIRECTORY && pSelectedDir->m_eItemType != SDI_FILESYSTEMPARENT) {
		int iSelectedItems = m_pSharedFilesCtrl->GetItemCount();
		int iCompleteFileSelected = -1;
		UINT uPrioMenuItem = 0;
		bool bFirstItem = true;
		for (int i = 0; i < iSelectedItems; ++i) {
			const CKnownFile *pFile = reinterpret_cast<CKnownFile*>(m_pSharedFilesCtrl->GetItemData(i));

			int iCurCompleteFile = static_cast<int>(!pFile->IsPartFile());
			if (bFirstItem)
				iCompleteFileSelected = iCurCompleteFile;
			else if (iCompleteFileSelected != iCurCompleteFile)
				iCompleteFileSelected = -1;

			UINT uCurPrioMenuItem;
			if (pFile->IsAutoUpPriority())
				uCurPrioMenuItem = MP_PRIOAUTO;
			else
				switch (pFile->GetUpPriority()) {
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
					uCurPrioMenuItem = 0;
					ASSERT(0);
				}

			if (bFirstItem)
				uPrioMenuItem = uCurPrioMenuItem;
			else if (uPrioMenuItem != uCurPrioMenuItem)
				uPrioMenuItem = 0;

			bFirstItem = false;
		}

		// just avoid that users get bad ideas by showing the comment/delete-option for the "all" selections
		// as the same comment for all files/all incomplete files/ etc is probably not too usefull
		// - even if it can be done in other ways if the user really wants to do it
		bool bWideRangeSelection = (pSelectedDir->m_nCatFilter == -1 && pSelectedDir->m_eItemType != SDI_NO);

		EnableSubMenuItem(m_SharedFilesMenu, m_PrioMenu.m_hMenu, iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED);
		m_PrioMenu.CheckMenuRadioItem(MP_PRIOVERYLOW, MP_PRIOAUTO, uPrioMenuItem, 0);

		m_SharedFilesMenu.EnableMenuItem(MP_OPENFOLDER, !pSelectedDir->m_strFullPath.IsEmpty() || pSelectedDir->m_eItemType == SDI_INCOMING || pSelectedDir->m_eItemType == SDI_TEMP || pSelectedDir->m_eItemType == SDI_CATINCOMING ? MF_ENABLED : MF_GRAYED);
		m_SharedFilesMenu.EnableMenuItem(MP_REMOVE, (iCompleteFileSelected > 0 && !bWideRangeSelection) ? MF_ENABLED : MF_GRAYED);
		m_SharedFilesMenu.EnableMenuItem(MP_CMT, (iSelectedItems > 0 && !bWideRangeSelection) ? MF_ENABLED : MF_GRAYED);
		m_SharedFilesMenu.EnableMenuItem(MP_DETAIL, iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED);
		m_SharedFilesMenu.EnableMenuItem(thePrefs.GetShowCopyEd2kLinkCmd() ? MP_GETED2KLINK : MP_SHOWED2KLINK, iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED);
		const bool bIsMonitoredRoot = IsMonitoredRoot(pSelectedDir->m_strFullPath);
		const bool bIsUnderMonitoredRoot = !FindContainingMonitoredRoot(pSelectedDir->m_strFullPath, false).IsEmpty();
		const bool bCanUnshareSubtree = !bIsMonitoredRoot && !bIsUnderMonitoredRoot
			&& ((pSelectedDir->m_eItemType == SDI_DIRECTORY && ItemHasChildren(pSelectedDir->m_htItem))
			|| (pSelectedDir->m_eItemType == SDI_NO && !pSelectedDir->m_strFullPath.IsEmpty()
			&& (FileSystemTreeIsShared(pSelectedDir->m_strFullPath)
			|| FileSystemTreeHasSharedSubdirectory(pSelectedDir->m_strFullPath, false))));
		m_SharedFilesMenu.EnableMenuItem(MP_UNSHAREDIR, (pSelectedDir->m_eItemType == SDI_NO && !pSelectedDir->m_strFullPath.IsEmpty() && (bIsMonitoredRoot || (!bIsUnderMonitoredRoot && FileSystemTreeIsShared(pSelectedDir->m_strFullPath)))) ? MF_ENABLED : MF_GRAYED);
		m_SharedFilesMenu.EnableMenuItem(MP_UNSHAREDIRSUB, bCanUnshareSubtree ? MF_ENABLED : MF_GRAYED);

		GetPopupMenuPos(*this, point);
		m_SharedFilesMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
	} else if (pSelectedDir != NULL && pSelectedDir->m_eItemType == SDI_UNSHAREDDIRECTORY) {
		const bool bIsMonitoredRoot = IsMonitoredRoot(pSelectedDir->m_strFullPath);
		const bool bIsUnderMonitoredRoot = !FindContainingMonitoredRoot(pSelectedDir->m_strFullPath, false).IsEmpty();
		const bool bShareableDirectory = thePrefs.IsShareableDirectory(pSelectedDir->m_strFullPath);
		m_ShareDirsMenu.EnableMenuItem(MP_UNSHAREDIR, (bIsMonitoredRoot || (!bIsUnderMonitoredRoot && FileSystemTreeIsShared(pSelectedDir->m_strFullPath))) ? MF_ENABLED : MF_GRAYED);
		m_ShareDirsMenu.EnableMenuItem(MP_UNSHAREDIRSUB, (!bIsMonitoredRoot && !bIsUnderMonitoredRoot && (FileSystemTreeIsShared(pSelectedDir->m_strFullPath) || FileSystemTreeHasSharedSubdirectory(pSelectedDir->m_strFullPath, false))) ? MF_ENABLED : MF_GRAYED);
		m_ShareDirsMenu.EnableMenuItem(MP_SHAREDIR, (!bIsMonitoredRoot && !bIsUnderMonitoredRoot && !FileSystemTreeIsShared(pSelectedDir->m_strFullPath) && bShareableDirectory) ? MF_ENABLED : MF_GRAYED);
		m_ShareDirsMenu.EnableMenuItem(MP_SHAREDIRSUB, (!bIsMonitoredRoot && !bIsUnderMonitoredRoot && FileSystemTreeHasSubdirectories(pSelectedDir->m_strFullPath) && bShareableDirectory) ? MF_ENABLED : MF_GRAYED);
		m_ShareDirsMenu.EnableMenuItem(MP_SHAREDIRMONITOR, (!bIsMonitoredRoot && !bIsUnderMonitoredRoot && bShareableDirectory) ? MF_ENABLED : MF_GRAYED);

		GetPopupMenuPos(*this, point);
		m_ShareDirsMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
	}
}

void CSharedDirsTreeCtrl::OnRButtonDown(UINT, CPoint point)
{
	UINT uHitFlags;
	HTREEITEM hItem = HitTest(point, &uHitFlags);
	if (hItem != NULL && (uHitFlags & TVHT_ONITEM)) {
		Select(hItem, TVGN_CARET);
		SetItemState(hItem, TVIS_SELECTED, TVIS_SELECTED);
	}
}

void CSharedDirsTreeCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	const UINT uCommand = FileListKeyboardShortcutsSeams::ClassifyKeyMessage(
		FileListKeyboardShortcutsSeams::EContext::SharedDirs,
		WM_KEYDOWN,
		nChar,
		GetKeyState(VK_CONTROL) < 0,
		GetKeyState(VK_MENU) < 0,
		GetKeyState(VK_SHIFT) < 0);
	if (uCommand != 0) {
		SendMessage(WM_COMMAND, uCommand);
		return;
	}

	CTreeCtrl::OnKeyDown(nChar, nRepCnt, nFlags);
}

#pragma warning(push)
#pragma warning(disable:4701) //local variable 'pri'
BOOL CSharedDirsTreeCtrl::OnCommand(WPARAM wParam, LPARAM)
{
	CTypedPtrList<CPtrList, CShareableFile*> selectedList;
	int iSelectedItems = m_pSharedFilesCtrl->GetItemCount();
	for (int i = 0; i < iSelectedItems; ++i)
		selectedList.AddTail(reinterpret_cast<CShareableFile*>(m_pSharedFilesCtrl->GetItemData(i)));

	const CDirectoryItem *pSelectedDir = GetSelectedFilter();
	if (pSelectedDir == NULL)
		return TRUE;

	// folder based
	switch (wParam) {
	case MP_OPENFOLDER:
		if (!pSelectedDir->m_strFullPath.IsEmpty() /*&& pSelectedDir->m_eItemType == SDI_NO*/)
			ShellOpenFile(pSelectedDir->m_strFullPath);
		else if (pSelectedDir->m_eItemType == SDI_INCOMING)
			ShellOpenFile(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
		else if (pSelectedDir->m_eItemType == SDI_TEMP)
			ShellOpenFile(thePrefs.GetTempDir());
		break;
	case MP_SHAREDIR:
		EditSharedDirectories(pSelectedDir, true, false);
		break;
	case MP_SHAREDIRSUB:
		EditSharedDirectories(pSelectedDir, true, true);
		break;
	case MP_SHAREDIRMONITOR:
		AddMonitoredSharedDirectory(pSelectedDir->m_strFullPath);
		break;
	case MP_UNSHAREDIR:
		if (IsMonitoredRoot(pSelectedDir->m_strFullPath))
			RemoveMonitoredSharedDirectory(pSelectedDir->m_strFullPath);
		else
			EditSharedDirectories(pSelectedDir, false, false);
		break;
	case MP_UNSHAREDIRSUB:
		if (!IsMonitoredRoot(pSelectedDir->m_strFullPath) && FindContainingMonitoredRoot(pSelectedDir->m_strFullPath, false).IsEmpty())
			EditSharedDirectories(pSelectedDir, false, true);
	}

	// file based
	if (iSelectedItems > 0) {
		switch (wParam) {
		case MP_GETED2KLINK:
			{
				CString str;
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					const CKnownFile *file = static_cast<CKnownFile*>(selectedList.GetNext(pos));
					if (file && file->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
						if (!str.IsEmpty())
							str += _T("\r\n");
						str += file->GetED2kLink();
					}
				}
				theApp.CopyTextToClipboard(str);
			}
			break;
		// file operations
		case MP_REMOVE:
		case MPG_DELETE:
			{
				if (LocMessageBox(IDS_CONFIRM_FILEDELETE, MB_ICONWARNING | MB_DEFBUTTON2 | MB_YESNO, 0) != IDYES)
					return TRUE;

				m_pSharedFilesCtrl->SetRedraw(false);
				bool bRemovedItems = false;
				while (!selectedList.IsEmpty()) {
					CShareableFile *myfile = selectedList.RemoveHead();
					if (!myfile || myfile->IsPartFile())
						continue;

					bool delsucc = ShellDeleteFile(myfile->GetFilePath());
					if (delsucc) {
						if (myfile->IsKindOf(RUNTIME_CLASS(CKnownFile)))
							theApp.sharedfiles->RemoveFile(static_cast<CKnownFile*>(myfile), true);
						bRemovedItems = true;
						if (myfile->IsKindOf(RUNTIME_CLASS(CPartFile)))
							theApp.emuledlg->transferwnd->GetDownloadList()->ClearCompleted(static_cast<CPartFile*>(myfile));
					} else {
						CString strError;
						strError.Format(GetResString(IDS_ERR_DELFILE), (LPCTSTR)myfile->GetFilePath());
						strError.AppendFormat(_T("\r\n\r\n%s"), (LPCTSTR)GetErrorMessage(::GetLastError()));
						AfxMessageBox(strError);
					}
				}
				m_pSharedFilesCtrl->SetRedraw(true);
				if (bRemovedItems) {
					m_pSharedFilesCtrl->AutoSelectItem();
					// Depending on <no-idea> this does not always cause an LVN_ITEMACTIVATE
					// message to be sent. So, explicitly redraw the item.
					theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails();
					theApp.emuledlg->sharedfileswnd->OnSingleFileShareStatusChanged(); // might have been a single shared file
				}
			}
			break;
		case MP_CMT:
			ShowFileDialog(selectedList, IDD_COMMENT);
			break;
		case MP_DETAIL:
		case MPG_ALTENTER:
			ShowFileDialog(selectedList);
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
				CKnownFile *file = static_cast<CKnownFile*>(selectedList.GetNext(pos));
				if (file->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
					uint8 pri;
					switch (wParam) {
					case MP_PRIOVERYLOW:
						pri = PR_VERYLOW;
						break;
					case MP_PRIOLOW:
						pri = PR_LOW;
						break;
					case MP_PRIONORMAL:
						pri = PR_NORMAL;
						break;
					case MP_PRIOHIGH:
						pri = PR_HIGH;
						break;
					case MP_PRIOVERYHIGH:
						pri = PR_VERYHIGH;
						break;
					case MP_PRIOAUTO:
						break;
					default:
						wParam = MP_PRIOAUTO;
					}
					file->SetAutoUpPriority(wParam == MP_PRIOAUTO);
					if (wParam == MP_PRIOAUTO)
						file->UpdateAutoUpPriority();
					else {
						file->SetUpPriority(pri);
						m_pSharedFilesCtrl->UpdateFile(file);
					}
				}
			}
			break;
		default:
			break;
		}
	}
	return TRUE;
}
#pragma warning(pop)

void CSharedDirsTreeCtrl::ShowFileDialog(CTypedPtrList<CPtrList, CShareableFile*> &aFiles, UINT uInvokePage)
{
	m_pSharedFilesCtrl->ShowFileDialog(aFiles, uInvokePage);
}

void CSharedDirsTreeCtrl::FileSystemTreeCreateTree()
{
	TCHAR drivebuffer[500];
	DWORD dwRet = GetLogicalDriveStrings(_countof(drivebuffer) - 1, drivebuffer);
	if (dwRet > 0 && dwRet < _countof(drivebuffer)) {
		drivebuffer[_countof(drivebuffer) - 1] = _T('\0');

		for (TCHAR *pos = drivebuffer; *pos != _T('\0'); pos += _tcslen(pos) + 2) {
			// Copy drive name
			pos[2] = _T('\0'); //drop backslash, leave only drive letter with column; e.g. "C:"
			FileSystemTreeAddChildItem(m_pRootUnsharedDirectries, pos, true);
		}
	}
}

void CSharedDirsTreeCtrl::FileSystemTreeAddChildItem(CDirectoryItem *pRoot, const CString &strText, bool bTopLevel)
{
	CString strPath(pRoot->m_strFullPath);
	if (!strPath.IsEmpty())
		strPath = PathHelpers::EnsureTrailingSeparator(strPath);
	CString strDir(strPath + strText);
	strDir = PathHelpers::EnsureTrailingSeparator(strDir);
	const bool bIsShared = FileSystemTreeIsShared(strDir);
	const bool bHasSharedDescendantOrFiles = FileSystemTreeHasSharedSubdirectory(strDir, true);
	TVINSERTSTRUCT itInsert = {};

	if (m_bUseIcons) {
		itInsert.item.mask = TVIF_CHILDREN | TVIF_HANDLE | TVIF_TEXT | TVIF_STATE | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
		itInsert.item.stateMask = TVIS_BOLD | TVIS_STATEIMAGEMASK;
	} else {
		itInsert.item.mask = TVIF_CHILDREN | TVIF_HANDLE | TVIF_TEXT | TVIF_STATE;
		itInsert.item.stateMask = TVIS_BOLD;
	}

	if (bHasSharedDescendantOrFiles || bIsShared)
		itInsert.item.state = TVIS_BOLD;
	else
		itInsert.item.state = 0;
	// Avoid probing every listed child directory for descendants before the user expands it.
	itInsert.item.cChildren = I_CHILDRENCALLBACK;

	CDirectoryItem *pti = new CDirectoryItem(strDir, 0, SDI_UNSHAREDDIRECTORY);

	CString strItemLabel(strText);
	itInsert.hInsertAfter = !bTopLevel ? TVI_SORT : TVI_LAST;
	itInsert.hParent = pRoot->m_htItem;
	itInsert.item.mask |= TVIF_PARAM;
	itInsert.item.lParam = (LPARAM)pti;

	if (m_bUseIcons) {
		if (bIsShared) {
			itInsert.item.stateMask |= TVIS_OVERLAYMASK;
			itInsert.item.state |= INDEXTOOVERLAYMASK(1);
		}

		strDir = PathHelpers::EnsureTrailingSeparator(strDir);
		UINT nType = ::GetDriveType(strDir);
		if (DRIVE_REMOVABLE <= nType && nType <= DRIVE_RAMDISK)
			itInsert.item.iImage = nType;

		const int nSystemImage = theApp.GetFileTypeSystemImageIdx(strDir);
		if (nSystemImage > 0 && theApp.GetSystemImageList() != NULL) {
			HICON hIcon = ::ImageList_GetIcon(theApp.GetSystemImageList(), nSystemImage, 0);
			if (hIcon != NULL) {
				itInsert.itemex.iImage = AddSystemIcon(hIcon, nSystemImage);
				::DestroyIcon(hIcon);
			} else
				itInsert.itemex.iImage = 0;
		} else
			itInsert.itemex.iImage = 0;

		if (bTopLevel && ShellUiHelpers::CanUseShellDisplayName(strDir)) {
			SHFILEINFO shFinfo = {};
			if (::SHGetFileInfo(strDir, 0, &shFinfo, sizeof(shFinfo), SHGFI_DISPLAYNAME) && shFinfo.szDisplayName[0] != _T('\0'))
				strItemLabel = shFinfo.szDisplayName;
		}
	}
	strItemLabel = BuildFileSystemTreeLabel(strDir, strItemLabel);
	itInsert.item.pszText = strItemLabel.GetBuffer();

	pti->m_htItem = InsertItem(&itInsert);
	strItemLabel.ReleaseBuffer();
	pRoot->liSubDirectories.AddTail(pti);
}

bool CSharedDirsTreeCtrl::FileSystemTreeHasSubdirectories(const CString &strDir)
{
	return ::HasSubdirectories(strDir);
}

bool CSharedDirsTreeCtrl::FileSystemTreeHasSharedSubdirectory(const CString &strDir, bool bOrFiles)
{
	return HasSharedDirectoryDescendant(strDir)
		|| (bOrFiles && theApp.sharedfiles->ContainsSingleSharedFiles(strDir));
}

void CSharedDirsTreeCtrl::FileSystemTreeAddSubdirectories(CDirectoryItem *pRoot)
{
	ASSERT(pRoot->m_strFullPath.Right(1) == _T("\\"));

	CStringList childNames;
	if (!SharedDirectoryOps::EnumerateChildDirectories(pRoot->m_strFullPath, childNames))
		return;

	for (POSITION pos = childNames.GetHeadPosition(); pos != NULL;)
		FileSystemTreeAddChildItem(pRoot, childNames.GetNext(pos), false);
}

int CSharedDirsTreeCtrl::AddSystemIcon(HICON hIcon, int nSystemListPos)
{
	int nPos;
	if (!m_mapSystemIcons.Lookup(nSystemListPos, nPos)) {
		nPos = GetImageList(TVSIL_NORMAL)->Add(hIcon);
		m_mapSystemIcons[nSystemListPos] = nPos;
	} else
		nPos = 0;
	return nPos;
}

void CSharedDirsTreeCtrl::OnTvnItemexpanding(LPNMHDR pNMHDR, LRESULT *pResult)
{
	CWaitCursor curWait;
	SetRedraw(FALSE);

	LPNMTREEVIEW pNMTreeView = reinterpret_cast<LPNMTREEVIEW>(pNMHDR);
	CDirectoryItem *pExpanded = reinterpret_cast<CDirectoryItem*>(pNMTreeView->itemNew.lParam);
	if (pExpanded != NULL) {
		const bool bCollapse = pNMTreeView->action == TVE_COLLAPSE || pNMTreeView->action == TVE_COLLAPSERESET;
		if (pExpanded->m_eItemType == SDI_UNSHAREDDIRECTORY && !pExpanded->m_strFullPath.IsEmpty()) {
			DeleteChildItems(pExpanded);
			if (!bCollapse)
				FileSystemTreeAddSubdirectories(pExpanded);

			TVITEM tvi = {};
			tvi.mask = TVIF_CHILDREN;
			tvi.hItem = pExpanded->m_htItem;
			tvi.cChildren = bCollapse ? 1 : (pExpanded->liSubDirectories.IsEmpty() ? 0 : 1);
			SetItem(&tvi);
		} else if (pExpanded->m_eItemType == SDI_FILESYSTEMPARENT) {
			DeleteChildItems(pExpanded);
			if (!bCollapse)
				FileSystemTreeCreateTree();
		}
	} else
		ASSERT(0);

	SetRedraw(TRUE);
	Invalidate();
	*pResult = 0;
}

void CSharedDirsTreeCtrl::DeleteChildItems(CDirectoryItem *pParent)
{
	while (!pParent->liSubDirectories.IsEmpty()) {
		CDirectoryItem *pToDelete = pParent->liSubDirectories.RemoveHead();
		DeleteItem(pToDelete->m_htItem);
		DeleteChildItems(pToDelete);
		delete pToDelete;
	}
}

bool CSharedDirsTreeCtrl::FileSystemTreeIsShared(const CString &strDir) const
{
	void *pvShared = NULL;
	return m_mapSharedDirectoryKeys.Lookup(BuildSharedTreePathKey(strDir), pvShared) != FALSE;
}

void CSharedDirsTreeCtrl::OnTvnGetdispinfo(LPNMHDR pNMHDR, LRESULT *pResult)
{
	reinterpret_cast<LPNMTVDISPINFO>(pNMHDR)->item.cChildren = 1;
	*pResult = 0;
}

void CSharedDirsTreeCtrl::AddSharedDirectory(const CString &strDir, bool bSubDirectories)
{
	(void)SharedDirectoryOps::AddSharedDirectory(m_strliSharedDirs, strDir, bSubDirectories, [](const CString &rstrDirectory) -> bool {
		return thePrefs.IsShareableDirectory(rstrDirectory);
	});
	RebuildSharedDirectoryLookup();
}

void CSharedDirsTreeCtrl::RemoveSharedDirectory(const CString &strDir, bool bSubDirectories)
{
	(void)SharedDirectoryOps::RemoveSharedDirectory(m_strliSharedDirs, strDir, bSubDirectories);
	RebuildSharedDirectoryLookup();
}

void CSharedDirsTreeCtrl::RemoveAllSharedDirectories()
{
	m_strliSharedDirs.RemoveAll();
	RebuildSharedDirectoryLookup();
}

void CSharedDirsTreeCtrl::AddMonitoredSharedDirectory(const CString &strDir)
{
	CWaitCursor curWait;
	AddSharedDirectory(strDir, true);
	(void)RemovePathsWithinDirectory(m_strliMonitorOwnedDirs, strDir, false);
	SharedDirectoryOps::AddMonitoredSharedRoot(m_strliMonitoredRoots, m_strliMonitorOwnedDirs, strDir, [](const CString &rstrDirectory) -> bool {
		return thePrefs.IsShareableDirectory(rstrDirectory);
	});

	thePrefs.ReplaceSharedDirectoryList(m_strliSharedDirs);
	thePrefs.ReplaceMonitoredSharedRootList(m_strliMonitoredRoots);
	thePrefs.ReplaceMonitorOwnedDirectoryList(m_strliMonitorOwnedDirs);
	m_bFileSystemRootDirty = true;
	theApp.WakeSharedDirectoryMonitor();
	theApp.emuledlg->sharedfileswnd->Reload(true);
	if (GetSelectedFilter() != NULL && GetSelectedFilter()->m_eItemType == SDI_UNSHAREDDIRECTORY)
		m_pSharedFilesCtrl->UpdateWindow();
	thePrefs.Save();
}

void CSharedDirsTreeCtrl::RemoveMonitoredSharedDirectory(const CString &strDir)
{
	CWaitCursor curWait;
	RemoveSharedDirectory(strDir, true);
	(void)RemoveEquivalentPath(m_strliMonitoredRoots, strDir);
	(void)RemovePathsWithinDirectory(m_strliMonitorOwnedDirs, strDir, false);

	thePrefs.ReplaceSharedDirectoryList(m_strliSharedDirs);
	thePrefs.ReplaceMonitoredSharedRootList(m_strliMonitoredRoots);
	thePrefs.ReplaceMonitorOwnedDirectoryList(m_strliMonitorOwnedDirs);
	m_bFileSystemRootDirty = true;
	theApp.WakeSharedDirectoryMonitor();
	theApp.emuledlg->sharedfileswnd->Reload(true);
	if (GetSelectedFilter() != NULL && GetSelectedFilter()->m_eItemType == SDI_UNSHAREDDIRECTORY)
		m_pSharedFilesCtrl->UpdateWindow();
	thePrefs.Save();
}

void CSharedDirsTreeCtrl::FileSystemTreeUpdateBoldState(const CDirectoryItem *pDir)
{
	if (pDir == NULL)
		pDir = m_pRootUnsharedDirectries;
	else
		SetItemState(pDir->m_htItem, ((FileSystemTreeHasSharedSubdirectory(pDir->m_strFullPath, true) || FileSystemTreeIsShared(pDir->m_strFullPath)) ? TVIS_BOLD : 0), TVIS_BOLD);
	for (POSITION pos = pDir->liSubDirectories.GetHeadPosition(); pos != NULL;)
		FileSystemTreeUpdateBoldState(pDir->liSubDirectories.GetNext(pos));
}

void CSharedDirsTreeCtrl::FileSystemTreeUpdateShareState(const CDirectoryItem *pDir)
{
	if (pDir == NULL)
		pDir = m_pRootUnsharedDirectries;
	else
		SetItemState(pDir->m_htItem, FileSystemTreeIsShared(pDir->m_strFullPath) ? INDEXTOOVERLAYMASK(1) : 0, TVIS_OVERLAYMASK);
	for (POSITION pos = pDir->liSubDirectories.GetHeadPosition(); pos != NULL;)
		FileSystemTreeUpdateShareState(pDir->liSubDirectories.GetNext(pos));
}

void CSharedDirsTreeCtrl::FileSystemTreeSetShareState(const CDirectoryItem *pDir, bool bSubDirectories)
{
	if (m_bUseIcons && pDir->m_htItem != NULL)
		SetItemState(pDir->m_htItem, FileSystemTreeIsShared(pDir->m_strFullPath) ? INDEXTOOVERLAYMASK(1) : 0, TVIS_OVERLAYMASK);
	if (bSubDirectories)
		for (POSITION pos = pDir->liSubDirectories.GetHeadPosition(); pos != NULL;)
			FileSystemTreeSetShareState(pDir->liSubDirectories.GetNext(pos), true);
}

void CSharedDirsTreeCtrl::EditSharedDirectories(const CDirectoryItem *pDir, bool bAdd, bool bSubDirectories)
{
	ASSERT(pDir->m_eItemType == SDI_UNSHAREDDIRECTORY || pDir->m_eItemType == SDI_NO || (pDir->m_eItemType == SDI_DIRECTORY && !bAdd && pDir->m_strFullPath.IsEmpty()));
	if (!bAdd && !FindContainingMonitoredRoot(pDir->m_strFullPath, false).IsEmpty())
		return;
	if (bAdd && !FindContainingMonitoredRoot(pDir->m_strFullPath, true).IsEmpty())
		return;

	CWaitCursor curWait;
	if (bAdd)
		AddSharedDirectory(pDir->m_strFullPath, bSubDirectories);
	else if (pDir->m_eItemType == SDI_DIRECTORY)
		RemoveAllSharedDirectories();
	else
		RemoveSharedDirectory(pDir->m_strFullPath, bSubDirectories);

	if (pDir->m_eItemType == SDI_NO || pDir->m_eItemType == SDI_DIRECTORY) {
		// An 'Unshare' was invoked from within the virtual "Shared Directories" folder, thus we do not have
		// the tree view item handle of the item within the "All Directories" tree -> need to update the
		// entire tree in case the tree view item is currently visible.
		FileSystemTreeUpdateShareState();
	} else {
		// A 'Share' or 'Unshare' was invoked for a certain tree view item within the "All Directories" tree,
		// thus we know the tree view item handle which needs to be updated for showing the new share state.
		FileSystemTreeSetShareState(pDir, bSubDirectories);
	}
	FileSystemTreeUpdateBoldState();
	FilterTreeReloadTree();

	// sync with the preferences list
	thePrefs.ReplaceSharedDirectoryList(m_strliSharedDirs);
	thePrefs.ReplaceMonitoredSharedRootList(m_strliMonitoredRoots);
	thePrefs.ReplaceMonitorOwnedDirectoryList(m_strliMonitorOwnedDirs);

	// update the shared files list
	m_bFileSystemRootDirty = true;
	theApp.emuledlg->sharedfileswnd->Reload(true);
	if (GetSelectedFilter() != NULL && GetSelectedFilter()->m_eItemType == SDI_UNSHAREDDIRECTORY)
		m_pSharedFilesCtrl->UpdateWindow(); // if in file system view, update the list to reflect the changes in the checkboxes
	thePrefs.Save();
}

void CSharedDirsTreeCtrl::Reload(bool bForce)
{
	if (!bForce) {
		// check for changes in shared dirs
		CStringList sharedDirs;
		thePrefs.CopySharedDirectoryList(sharedDirs);
		bForce = (sharedDirs.GetCount() != m_strliSharedDirs.GetCount());
		if (!bForce) {
			POSITION pos = m_strliSharedDirs.GetHeadPosition();
			POSITION pos2 = sharedDirs.GetHeadPosition();
			while (pos != NULL && pos2 != NULL) {
				const CString &str(m_strliSharedDirs.GetNext(pos));
				const CString &str2(sharedDirs.GetNext(pos2));
				if (!AreSharedTreePathKeysEqual(str, str2)) {
					bForce = true;
					break;
				}
			}
		}

		if (!bForce) {
			CStringList monitoredRoots;
			thePrefs.CopyMonitoredSharedRootList(monitoredRoots);
			bForce = (monitoredRoots.GetCount() != m_strliMonitoredRoots.GetCount());
			if (!bForce) {
				POSITION pos = m_strliMonitoredRoots.GetHeadPosition();
				POSITION pos2 = monitoredRoots.GetHeadPosition();
				while (pos != NULL && pos2 != NULL) {
					if (!AreSharedTreePathKeysEqual(m_strliMonitoredRoots.GetNext(pos), monitoredRoots.GetNext(pos2))) {
						bForce = true;
						break;
					}
				}
			}
		}

		if (!bForce) {
			// check for changes in categories incoming dirs
			const CString &strMainIncDir(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
			CStringList strliFound;
			for (INT_PTR i = 0; i < thePrefs.GetCatCount(); ++i) {
				const Category_Struct *pCatStruct = thePrefs.GetCategory(i);
				if (pCatStruct != NULL) {
					CString strCatIncomingPath(pCatStruct->strIncomingPath);

					if (!strCatIncomingPath.IsEmpty() && !AreSharedTreePathKeysEqual(strCatIncomingPath, strMainIncDir)
						&& !ListContainsEquivalentPath(strliFound, strCatIncomingPath))
					{
						if (!ListContainsEquivalentPath(m_strliCatIncomingDirs, strCatIncomingPath)) {
							bForce = true;
							break;
						}
						strliFound.AddTail(strCatIncomingPath);
					}
				}
			}
			if (strliFound.GetCount() != m_strliCatIncomingDirs.GetCount())
				bForce = true;
		}
	}
	if (bForce) {
		FetchSharedDirsList();
		FilterTreeReloadTree();
		if (m_bFileSystemRootDirty) {
			Expand(m_pRootUnsharedDirectries->m_htItem, TVE_COLLAPSE); // collapsing is enough to sync tree with the filter, as all items are recreated on every expanding
			m_bFileSystemRootDirty = false;
		}
	}
}

void CSharedDirsTreeCtrl::FetchSharedDirsList()
{
	m_strliSharedDirs.RemoveAll();
	thePrefs.CopySharedDirectoryList(m_strliSharedDirs);
	m_strliMonitoredRoots.RemoveAll();
	thePrefs.CopyMonitoredSharedRootList(m_strliMonitoredRoots);
	m_strliMonitorOwnedDirs.RemoveAll();
	thePrefs.CopyMonitorOwnedDirectoryList(m_strliMonitorOwnedDirs);
	RebuildSharedDirectoryLookup();
#if EMULE_COMPILED_STARTUP_PROFILING
	{
		CString strPhase;
		strPhase.Format(_T("CSharedDirsTreeCtrl::FetchSharedDirsList copied (%d entries)"), static_cast<int>(m_strliSharedDirs.GetCount()));
		theApp.AppendStartupProfileLine(strPhase, 0);
	}
#endif
}

bool CSharedDirsTreeCtrl::IsMonitoredRoot(const CString &strDir) const
{
	return ListContainsEquivalentPath(m_strliMonitoredRoots, strDir);
}

CString CSharedDirsTreeCtrl::FindContainingMonitoredRoot(const CString &strDir, bool bAllowExactMatch) const
{
	CString strBestMatch;
	const CString strDirKey(BuildSharedTreePathKey(strDir));
	for (POSITION pos = m_strliMonitoredRoots.GetHeadPosition(); pos != NULL;) {
		const CString strRoot(m_strliMonitoredRoots.GetNext(pos));
		const CString strRootKey(BuildSharedTreePathKey(strRoot));
		if (strRootKey == strDirKey) {
			if (!bAllowExactMatch)
				continue;
		} else if (!HasSharedDirectoryKeyPrefix(strRootKey, strDirKey))
			continue;
		if (strBestMatch.IsEmpty() || strRoot.GetLength() > strBestMatch.GetLength())
			strBestMatch = strRoot;
	}
	return strBestMatch;
}

bool CSharedDirsTreeCtrl::HasManagedSharedAncestor(const CString &strDir) const
{
	CString strParent(GetSharedTreeParentPath(strDir));
	while (!strParent.IsEmpty()) {
		void *pvShared = NULL;
		if (m_mapSharedDirectoryKeys.Lookup(BuildSharedTreePathKey(strParent), pvShared))
			return true;
		strParent = GetSharedTreeParentPath(strParent);
	}
	return false;
}

CString CSharedDirsTreeCtrl::BuildFileSystemTreeLabel(const CString &strDir, LPCTSTR pszBaseLabel) const
{
	CString strLabel(pszBaseLabel != NULL ? pszBaseLabel : _T(""));
	if (IsMonitoredRoot(strDir))
		strLabel.AppendFormat(_T(" %s"), (LPCTSTR)GetResString(IDS_SHARESTATE_MONITORED));
	else if (!HasManagedSharedAncestor(strDir) && FileSystemTreeIsShared(strDir))
		strLabel.AppendFormat(_T(" %s"), (LPCTSTR)GetResString(HasSharedDirectoryDescendant(strDir) ? IDS_SHARESTATE_RECURSIVE : IDS_SHARESTATE_SHARED));
	return strLabel;
}

void CSharedDirsTreeCtrl::OnTvnBeginDrag(LPNMHDR pNMHDR, LRESULT *pResult)
{
	LPNMTREEVIEW lpnmtv = (LPNMTREEVIEW)pNMHDR;
	*pResult = 0;

	CDirectoryItem *pToDrag = reinterpret_cast<CDirectoryItem*>(lpnmtv->itemNew.lParam);
	if (pToDrag == NULL || pToDrag->m_eItemType != SDI_UNSHAREDDIRECTORY || FileSystemTreeIsShared(pToDrag->m_strFullPath))
		return;

	ASSERT(m_pDraggingItem == NULL);
	delete m_pDraggingItem;
	m_pDraggingItem = pToDrag->CloneContent(); // to be safe we store a copy, as items can be deleted when collapsing the tree etc

	CImageList *piml = CreateDragImage(lpnmtv->itemNew.hItem);
	if (piml == NULL)
		return;

	CPoint ptOffset;
	CRect rcItem;
	/* get the bounding rectangle of the item being dragged (rel to top-left of control) */
	if (GetItemRect(lpnmtv->itemNew.hItem, &rcItem, TRUE)) {
		/* get offset into image that the mouse is at */
		/* item rect doesn't include the image */
		int nX, nY;
		::ImageList_GetIconSize(piml->GetSafeHandle(), &nX, &nY);
		ptOffset = CPoint(lpnmtv->ptDrag) - rcItem.BottomRight() + POINT{nX, nY};

		/* convert the item rect to screen co-ords for later use */
		MapWindowPoints(NULL, &rcItem);
	} else {
		GetWindowRect(&rcItem);
		ptOffset.x = ptOffset.y = 8;
	}

	if (piml->BeginDrag(0, ptOffset)) {
		CPoint ptDragEnter(lpnmtv->ptDrag);
		ClientToScreen(&ptDragEnter);
		piml->DragEnter(NULL, ptDragEnter);
	}
	delete piml;

	/* set the focus here, so we get a WM_CANCELMODE if needed */
	SetFocus();

	/* redraw item being dragged, otherwise it remains (looking) selected */
	InvalidateRect(&rcItem, TRUE);
	UpdateWindow();

	/* Hide the mouse cursor, and direct mouse input to this window */
	SetCapture();
}

void CSharedDirsTreeCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_pDraggingItem != NULL) {
		/* drag the item to the current position */
		CPoint pt(point);
		ClientToScreen(&pt);

		CImageList::DragMove(pt);
		CImageList::DragShowNolock(FALSE);
		LPCTSTR pCursor = IDC_NO;
		if (CWnd::WindowFromPoint(pt) == this) {
			TVHITTESTINFO tvhti;
			tvhti.pt = pt;
			ScreenToClient(&tvhti.pt);
			HTREEITEM hItemSel = HitTest(&tvhti);
			if (hItemSel != NULL) {
				CDirectoryItem *pDragTarget = reinterpret_cast<CDirectoryItem*>(GetItemData(hItemSel));
				//allow dragging only to shared folders
				if (pDragTarget != NULL && (pDragTarget->m_eItemType == SDI_DIRECTORY || pDragTarget->m_eItemType == SDI_NO)) {
					pCursor = IDC_ARROW;
					SelectDropTarget(pDragTarget->m_htItem);
				}
			}
		}
		SetCursor(AfxGetApp()->LoadStandardCursor(pCursor));

		CImageList::DragShowNolock(TRUE);
	}

	CTreeCtrl::OnMouseMove(nFlags, point);
}

void CSharedDirsTreeCtrl::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_pDraggingItem != NULL) {
		CPoint pt(point);
		ClientToScreen(&pt);

		TVHITTESTINFO tvhti;
		tvhti.pt = pt;
		ScreenToClient(&tvhti.pt);
		HTREEITEM hItemSel = HitTest(&tvhti);
		if (hItemSel != NULL) {
			CDirectoryItem *pDragTarget = reinterpret_cast<CDirectoryItem*>(GetItemData(hItemSel));
			//only allow dragging to shared folders
			if (pDragTarget && (pDragTarget->m_eItemType == SDI_DIRECTORY || pDragTarget->m_eItemType == SDI_NO)) {

				HTREEITEM htReal = m_pRootUnsharedDirectries->FindItem(m_pDraggingItem);
				// get the original drag src
				CDirectoryItem *pRealDragItem = htReal ? reinterpret_cast<CDirectoryItem*>(GetItemData(htReal)) : NULL;
				// if item was deleted - no problem as when we don't need to update the visible part
				// we can use the content copy just as well
				EditSharedDirectories(pRealDragItem ? pRealDragItem : m_pDraggingItem, true, false);
			}
		}

		CancelMode();
	}
	CTreeCtrl::OnLButtonUp(nFlags, point);
}

void CSharedDirsTreeCtrl::CancelMode()
{
	CImageList::DragLeave(NULL);
	CImageList::EndDrag();
	::ReleaseCapture();
	ShowCursor(TRUE);
	SelectDropTarget(NULL);

	delete m_pDraggingItem;
	m_pDraggingItem = NULL;
	RedrawWindow();
}

void CSharedDirsTreeCtrl::OnCancelMode()
{
	if (m_pDraggingItem != NULL)
		CancelMode();
	CTreeCtrl::OnCancelMode();
}

void CSharedDirsTreeCtrl::OnVolumesChanged()
{
	m_bFileSystemRootDirty = true;
}

bool CSharedDirsTreeCtrl::ShowFileSystemDirectory(const CString &strDir)
{
	// expand directories until we find our target directory and select it
	const CString strTargetKey(BuildSharedTreePathKey(strDir));
	const int iTargetKeyLength = strTargetKey.GetLength();
	const CDirectoryItem *pCurrentItem = m_pRootUnsharedDirectries;
	for (bool bContinue = true; bContinue;) {
		bContinue = false;
		Expand(pCurrentItem->m_htItem, TVE_EXPAND);
		for (POSITION pos = pCurrentItem->liSubDirectories.GetHeadPosition(); pos != NULL;) {
			const CDirectoryItem *pTemp = pCurrentItem->liSubDirectories.GetNext(pos);
			const CString strCandidateKey(BuildSharedTreePathKey(pTemp->m_strFullPath));
			if (strTargetKey == strCandidateKey) {
				Select(pTemp->m_htItem, TVGN_CARET);
				EnsureVisible(pTemp->m_htItem);
				return true;
			}
			const int iCandidateKeyLength = strCandidateKey.GetLength();
			if (iCandidateKeyLength < iTargetKeyLength
				&& _tcsncmp(strTargetKey, strCandidateKey, iCandidateKeyLength) == 0)
			{
				pCurrentItem = pTemp;
				bContinue = true;
				break;
			}
		}
	}
	return false;
}

bool CSharedDirsTreeCtrl::ShowSharedDirectory(const CString &strDir)
{
	// expand directories until we find our target directory and select it
	for (POSITION pos = m_pRootDirectoryItem->liSubDirectories.GetHeadPosition(); pos != NULL;) {
		const CDirectoryItem *pTemp = m_pRootDirectoryItem->liSubDirectories.GetNext(pos);
		if (pTemp->m_eItemType == SDI_DIRECTORY) {
			Expand(pTemp->m_htItem, TVE_EXPAND);
			if (strDir.IsEmpty()) { // we want the parent item only
				Select(pTemp->m_htItem, TVGN_CARET);
				EnsureVisible(pTemp->m_htItem);
				return true;
			}
			// search for the fitting sub dir
			for (POSITION pos2 = pTemp->liSubDirectories.GetHeadPosition(); pos2 != NULL;) {
				CDirectoryItem *pTemp2 = pTemp->liSubDirectories.GetNext(pos2);
				if (AreSharedTreePathKeysEqual(strDir, pTemp2->m_strFullPath)) {
					Select(pTemp2->m_htItem, TVGN_CARET);
					EnsureVisible(pTemp2->m_htItem);
					return true;
				}
			}
			return false;
		}
	}
	return false;
}

void CSharedDirsTreeCtrl::ShowAllSharedFiles()
{
	Select(GetRootItem(), TVGN_CARET);
	EnsureVisible(GetRootItem());
}
