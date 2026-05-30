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
#include <algorithm>
#include "emule.h"
#include "MemDC.h"
#include "MuleListCtrl.h"
#include "MuleListKeyboardShortcutsSeams.h"
#include "MuleListCtrlSeams.h"
#include "Ini2.h"
#include "SharedFilesCtrl.h"
#include "MenuCmds.h"
#include "OtherFunctions.h"
#include "ShellUiHelpers.h"
#include "ListViewSearchDlg.h"
#include <atlimage.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define MAX_SORTORDERHISTORY 4
#define MLC_BLEND(A, B, X) (((A) + (B) * ((X)-1) + (((X)+1)/2)) / (X))

#define MLC_RGBBLEND(A, B, X) (                   \
	RGB(MLC_BLEND(GetRValue(A), GetRValue(B), X), \
	MLC_BLEND(GetGValue(A), GetGValue(B), X),     \
	MLC_BLEND(GetBValue(A), GetBValue(B), X))     \
)

#define MLC_IDC_MENU	4875
#define MLC_IDC_UPDATE	(MLC_IDC_MENU - 1)

//a value that's not a multiple of 4 and uncommon
#define MLC_MAGIC 0xFEEBDEEF

//used for very slow assertions
//#define MLC_ASSERT(f)	ASSERT(f)
#define MLC_ASSERT(f)	((void)0)

//////////////////////////////////
// CMuleListCtrl

// Be careful with these offsets, they are supposed to match *exactly* the Windows built-in metric.
// If it does not match that value, column auto-sizing (double clicking on header divider) will
// give inaccurate results.
const int CMuleListCtrl::sm_iIconOffset = 4;	// Offset from left window border to icon (of 1st column)
const int CMuleListCtrl::sm_iLabelOffset = 2;	// Offset between right icon border and item text (of 1st column)
const int CMuleListCtrl::sm_iSubItemInset = 4;	// Offset from left and right column border to item text

IMPLEMENT_DYNAMIC(CMuleListCtrl, CListCtrl)

BEGIN_MESSAGE_MAP(CMuleListCtrl, CListCtrl)
	ON_NOTIFY_REFLECT(LVN_ENDSCROLL, OnLvnEndScrollList)
	ON_NOTIFY_REFLECT(LVN_GETINFOTIP, OnLvnGetInfoTip)
	ON_WM_DRAWITEM()
	ON_WM_ERASEBKGND()
	ON_WM_KEYDOWN()
	ON_WM_MEASUREITEM_REFLECT()
	ON_WM_SYSCOLORCHANGE()
END_MESSAGE_MAP()

CMuleListCtrl::CMuleListCtrl(PFNLVCOMPARE pfnCompare, LPARAM iParamSort)
	: m_SortProc(pfnCompare)
	, m_dwParamSort(iParamSort)
	, m_crWindow()
	, m_crWindowText()
	, m_crWindowTextBk(m_crWindow)
	, m_crHighlight()
	, m_crHighlightText(m_crWindowText)
	, m_crGlow()
	, m_crFocusLine()
	, m_crNoHighlight()
	, m_crNoFocusLine()
	, m_lvcd()
	, m_bCustomDraw()
	, m_defaultColumnOrder()
	, m_defaultColumnWidths()
	, m_stockHiddenColumns()
	, m_extendedHiddenColumns()
	, m_uIDAccel(IDR_LISTVIEW)
	, m_hAccel()
	, m_eUpdateMode(lazy)
	, m_iAutoSizeWidth(LVSCW_AUTOSIZE)
	, m_iFindDirection(1)
	, m_iFindColumn()
	, m_bGeneralPurposeFind()
	, m_bCanSearchInAllColumns()
	, m_bFindMatchCase()
	, m_aColumns()
	, m_iColumnsTracked()
	, m_iCurrentSortItem(-1)
	, m_atSortArrow()
	, m_iRedrawCount()
{
}

int CALLBACK CMuleListCtrl::SortProc(LPARAM /*lParam1*/, LPARAM /*lParam2*/, LPARAM /*lParamSort*/)
{
	return 0;
}

void CMuleListCtrl::PreSubclassWindow()
{
	SetColors();
	CListCtrl::PreSubclassWindow();
	SetExtendedStyle(LVS_EX_HEADERDRAGDROP);

	// Vista: Reduce flickering in header control
	ModifyStyle(0, WS_CLIPCHILDREN);

	// If we want to handle the VK_RETURN key, we have to do that via accelerators!
	if (m_uIDAccel != UINT_MAX) {
		m_hAccel = ::LoadAccelerators(AfxGetResourceHandle(), MAKEINTRESOURCE(m_uIDAccel));
		ASSERT(m_hAccel);
	}

	// DEFAULT_GUI_FONT: Vista: "MS Shell Dlg" with 8 pts (regardless of system applet settings !!!)
	// SYSTEM_FONT:		 Vista: Good old Windows 3.11 System Font
	// NULL				 Vista: Font ('Symbol') with the face and size which is configured in System applet.
	if (thePrefs.GetUseSystemFontForMainControls())
		SendMessage(WM_SETFONT, NULL, FALSE);
}

int CMuleListCtrl::IndexToOrder(CHeaderCtrl *pHeader, int iIndex)
{
	int iCount = pHeader->GetItemCount();
	int *piArray = new int[iCount];
	Header_GetOrderArray(pHeader->m_hWnd, iCount, piArray);
	while (--iCount >= 0)
		if (piArray[iCount] == iIndex)
			break;
	delete[] piArray;
	return iCount;
}

void CMuleListCtrl::HideColumn(int iColumn)
{
	CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();
	int iCount = pHeaderCtrl->GetItemCount();
	if (iColumn < 1 || iColumn >= iCount || m_aColumns[iColumn].bHidden)
		return;

	//stop it from redrawing
	SetRedraw(false);

	//shrink width to 0
	HDITEM item;
	item.mask = HDI_WIDTH;
	pHeaderCtrl->GetItem(iColumn, &item);
	m_aColumns[iColumn].iWidth = item.cxy;
	item.cxy = 0;
	pHeaderCtrl->SetItem(iColumn, &item);

	//move to front of list
	INT *piArray = new INT[m_iColumnsTracked];
	pHeaderCtrl->GetOrderArray(piArray, m_iColumnsTracked);

	int iFrom = m_aColumns[iColumn].iLocation;
	for (int i = 0; i < m_iColumnsTracked; ++i)
		iFrom += static_cast<int>(m_aColumns[i].iLocation > m_aColumns[iColumn].iLocation && m_aColumns[i].bHidden);

	for (; iFrom > 0; --iFrom)
		piArray[iFrom] = piArray[iFrom - 1];
	piArray[0] = iColumn;
	pHeaderCtrl->SetOrderArray(m_iColumnsTracked, piArray);
	delete[] piArray;

	//update entry
	m_aColumns[iColumn].bHidden = true;

	//redraw
	SetRedraw(true);
	Invalidate(FALSE);
}

void CMuleListCtrl::ShowColumn(int iColumn)
{
	CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();
	int iCount = pHeaderCtrl->GetItemCount();
	if (iColumn < 1 || iColumn >= iCount || !m_aColumns[iColumn].bHidden)
		return;

	//stop it from redrawing
	SetRedraw(false);

	//restore position in list
	INT *piArray = new INT[m_iColumnsTracked];
	pHeaderCtrl->GetOrderArray(piArray, m_iColumnsTracked);
	int iCurrent = IndexToOrder(pHeaderCtrl, iColumn);

	for (; iCurrent < m_iColumnsTracked - 1 && iCurrent < IndexToOrder(pHeaderCtrl, 0); ++iCurrent)
		piArray[iCurrent] = piArray[iCurrent + 1];
	for (; iCurrent < m_iColumnsTracked - 1 &&
		m_aColumns[iColumn].iLocation > m_aColumns[pHeaderCtrl->OrderToIndex(iCurrent + 1)].iLocation; ++iCurrent)
	{
		piArray[iCurrent] = piArray[iCurrent + 1];
	}
	piArray[iCurrent] = iColumn;
	pHeaderCtrl->SetOrderArray(m_iColumnsTracked, piArray);
	delete[] piArray;

	//and THEN restore original width
	HDITEM item;
	item.mask = HDI_WIDTH;
	item.cxy = m_aColumns[iColumn].iWidth;
	pHeaderCtrl->SetItem(iColumn, &item);

	//update entry
	m_aColumns[iColumn].bHidden = false;

	//redraw
	SetRedraw(true);
	Invalidate(FALSE);
}

void CMuleListCtrl::SaveSettings()
{
	ASSERT(!m_Name.IsEmpty());

	ASSERT(GetHeaderCtrl()->GetItemCount() == m_iColumnsTracked);

	if (m_Name.IsEmpty() || GetHeaderCtrl()->GetItemCount() != m_iColumnsTracked)
		return;

	CIni ini(thePrefs.GetConfigFile(), _T("ListControlSetup"));

	ShowWindow(SW_HIDE);

	int iSortHist[MAX_SORTORDERHISTORY];
	int i = 0;
	for (POSITION pos = m_liSortHistory.GetHeadPosition(); pos != NULL;)
		iSortHist[i++] = m_liSortHistory.GetNext(pos) + 1;
	ini.SerGet(false, iSortHist, i, m_Name + _T("SortHistory"));
	// store additional settings
	ini.WriteInt(m_Name + _T("TableSortItem"), GetSortItem());
	ini.WriteInt(m_Name + _T("TableSortAscending"), GetSortType(m_atSortArrow));

	int *piColWidths = new int[m_iColumnsTracked];
	int *piColHidden = new int[m_iColumnsTracked];
	for (i = 0; i < m_iColumnsTracked; ++i) {
		piColWidths[i] = GetColumnWidth(i);
		piColHidden[i] = IsColumnHidden(i);
		ShowColumn(i);
	}

	int *piColOrders = new int[m_iColumnsTracked];
	GetHeaderCtrl()->GetOrderArray(piColOrders, m_iColumnsTracked);

	ini.SerGet(false, piColWidths, m_iColumnsTracked, m_Name + _T("ColumnWidths"));
	ini.SerGet(false, piColHidden, m_iColumnsTracked, m_Name + _T("ColumnHidden"));
	ini.SerGet(false, piColOrders, m_iColumnsTracked, m_Name + _T("ColumnOrders"));

	for (i = 0; i < m_iColumnsTracked; ++i)
		if (piColHidden[i])
			HideColumn(i);

	ShowWindow(SW_SHOW);

	delete[] piColOrders;
	delete[] piColWidths;
	delete[] piColHidden;
}

int CMuleListCtrl::GetSortType(ArrowType at)
{
	switch (at) {
	case arrowDown:
	default:
		return 0;
	case arrowUp:
		return 1;
	case arrowDoubleDown:
		return 2;
	case arrowDoubleUp:
		return 3;
	}
}

CMuleListCtrl::ArrowType CMuleListCtrl::GetArrowType(int iat)
{
	static const CMuleListCtrl::ArrowType art[] = {arrowDown, arrowUp, arrowDoubleDown, arrowDoubleUp};

	return art[(iat >= 0 && iat < (int)_countof(art)) ? iat : 0];
}

void CMuleListCtrl::LoadSettings()
{
	if (m_Name.IsEmpty()) {
		ASSERT(0);
		return;
	}

	CIni ini(thePrefs.GetConfigFile(), _T("ListControlSetup"));

	// sort history
	int iSortHist[MAX_SORTORDERHISTORY];
	ini.SerGet(true, iSortHist, MAX_SORTORDERHISTORY, m_Name + _T("SortHistory"));
	m_liSortHistory.RemoveAll();
	for (int i = 0; i < MAX_SORTORDERHISTORY; ++i) {
		if (iSortHist[i] <= 0)
			break;
		m_liSortHistory.AddTail(iSortHist[i] - 1);
	}

	m_iCurrentSortItem = ini.GetInt(m_Name + _T("TableSortItem"), 0);
	m_atSortArrow = GetArrowType(ini.GetInt(m_Name + _T("TableSortAscending"), 1));
	if (m_liSortHistory.IsEmpty())
		m_liSortHistory.AddTail(m_iCurrentSortItem);

	// columns settings
	int *piColWidths = new int[m_iColumnsTracked];
	int *piColHidden = new int[m_iColumnsTracked];
	int *piColOrders = new int[m_iColumnsTracked];
	const bool bHasSavedColumnOrder = !ini.GetString(m_Name + _T("ColumnOrders")).IsEmpty();
	ini.SerGet(true, piColWidths, m_iColumnsTracked, m_Name + _T("ColumnWidths"));
	ini.SerGet(true, piColHidden, m_iColumnsTracked, m_Name + _T("ColumnHidden"), 0, -1);
	ini.SerGet(true, piColOrders, m_iColumnsTracked, m_Name + _T("ColumnOrders"));
	const bool bHasValidSavedColumnOrder = bHasSavedColumnOrder && MuleListCtrlSeams::IsCompleteColumnOrder(piColOrders, m_iColumnsTracked);

	// apply column widths and verify sort order
	int *piArray = new int[m_iColumnsTracked];
	const bool bUseDefaultColumnOrder = !bHasValidSavedColumnOrder && TryBuildDefaultColumnOrder(piArray);
	for (int i = 0; i < m_iColumnsTracked; ++i) {
		if (!bUseDefaultColumnOrder)
			piArray[i] = i;

		if (piColWidths[i] >= 2) // don't allow column widths of 0 or 1 - just because it looks very confusing in GUI
			SetColumnWidth(i, piColWidths[i]);

		if (!bUseDefaultColumnOrder && bHasValidSavedColumnOrder)
			piArray[i] = piColOrders[i];
		m_aColumns[i].iLocation = piArray[i];
	}
	piArray[0] = 0;

	for (int i = m_iColumnsTracked; --i >= 0;)
		m_aColumns[piArray[i]].iLocation = i;
	GetHeaderCtrl()->SetOrderArray(m_iColumnsTracked, piArray);

	for (int i = 1; i < m_iColumnsTracked; ++i)
		if (piColHidden[i] > 0 || (piColHidden[i] < 0 && m_liDefaultHiddenColumns.Find(i) != NULL))
			HideColumn(i);

	delete[] piArray;
	delete[] piColOrders;
	delete[] piColWidths;
	delete[] piColHidden;
}

void CMuleListCtrl::SetDefaultColumnOrder(const int *piColumnOrder, int iColumnCount)
{
	m_defaultColumnOrder.clear();
	if (piColumnOrder == NULL || iColumnCount <= 0)
		return;
	m_defaultColumnOrder.assign(piColumnOrder, piColumnOrder + iColumnCount);
}

bool CMuleListCtrl::TryBuildDefaultColumnOrder(int *piColumnOrder) const
{
	if (piColumnOrder == NULL || m_defaultColumnOrder.size() != static_cast<size_t>(m_iColumnsTracked))
		return false;
	if (!MuleListCtrlSeams::IsCompleteColumnOrder(m_defaultColumnOrder.data(), m_iColumnsTracked))
		return false;
	for (int i = 0; i < m_iColumnsTracked; ++i)
		piColumnOrder[i] = m_defaultColumnOrder[static_cast<size_t>(i)];
	return true;
}

void CMuleListCtrl::SetViewPresetProfile(const MuleListCtrlViewPresets::SListControlViewPresetProfile &profile)
{
	SetPrefsKey(profile.pszControlName);
	SetDefaultColumnOrder(profile.piExtendedOrder, profile.iColumnCount);
	m_stockHiddenColumns.clear();
	if (profile.piStockHiddenColumns != nullptr && profile.iStockHiddenColumnCount > 0)
		m_stockHiddenColumns.assign(profile.piStockHiddenColumns, profile.piStockHiddenColumns + profile.iStockHiddenColumnCount);
	m_extendedHiddenColumns.clear();
	if (profile.piExtendedHiddenColumns != nullptr && profile.iExtendedHiddenColumnCount > 0)
		m_extendedHiddenColumns.assign(profile.piExtendedHiddenColumns, profile.piExtendedHiddenColumns + profile.iExtendedHiddenColumnCount);
}

bool CMuleListCtrl::BuildViewPresetColumnOrder(MuleListCtrlViewPresets::ETableViewPreset ePreset, std::vector<int> &rColumnOrder) const
{
	rColumnOrder.assign(static_cast<size_t>(m_iColumnsTracked), 0);
	if (ePreset == MuleListCtrlViewPresets::ETableViewPreset::Stock) {
		for (int i = 0; i < m_iColumnsTracked; ++i)
			rColumnOrder[static_cast<size_t>(i)] = i;
		return true;
	}

	if (m_defaultColumnOrder.size() != static_cast<size_t>(m_iColumnsTracked)
		|| !MuleListCtrlSeams::IsCompleteColumnOrder(m_defaultColumnOrder.data(), m_iColumnsTracked))
	{
		return false;
	}
	rColumnOrder = m_defaultColumnOrder;
	return true;
}

bool CMuleListCtrl::IsPresetHiddenColumn(MuleListCtrlViewPresets::ETableViewPreset ePreset, int iColumn) const
{
	if (ePreset == MuleListCtrlViewPresets::ETableViewPreset::Full)
		return false;
	const std::vector<int> &rHiddenColumns = ePreset == MuleListCtrlViewPresets::ETableViewPreset::Stock
		? m_stockHiddenColumns
		: m_extendedHiddenColumns;
	return std::find(rHiddenColumns.begin(), rHiddenColumns.end(), iColumn) != rHiddenColumns.end();
}

int CMuleListCtrl::GetDefaultColumnWidth(int iColumn) const
{
	if (iColumn >= 0 && static_cast<size_t>(iColumn) < m_defaultColumnWidths.size() && m_defaultColumnWidths[static_cast<size_t>(iColumn)] >= 2)
		return m_defaultColumnWidths[static_cast<size_t>(iColumn)];
	return GetColumnWidth(iColumn);
}

void CMuleListCtrl::ResetSortState()
{
	m_iCurrentSortItem = 0;
	m_atSortArrow = arrowUp;
	m_liSortHistory.RemoveAll();
	m_liSortHistory.AddTail(m_iCurrentSortItem);
	SetSortArrow();
}

bool CMuleListCtrl::ApplyViewPreset(MuleListCtrlViewPresets::ETableViewPreset ePreset, MuleListCtrlViewPresets::EColumnWidthMode eWidthMode)
{
	ASSERT(!m_Name.IsEmpty());
	if (m_Name.IsEmpty() || GetSafeHwnd() == NULL || GetHeaderCtrl() == NULL || GetHeaderCtrl()->GetItemCount() != m_iColumnsTracked)
		return false;

	std::vector<int> columnOrder;
	if (!BuildViewPresetColumnOrder(ePreset, columnOrder))
		return false;

	std::vector<int> columnWidths(static_cast<size_t>(m_iColumnsTracked), 0);
	for (int i = 0; i < m_iColumnsTracked; ++i) {
		columnWidths[static_cast<size_t>(i)] = eWidthMode == MuleListCtrlViewPresets::EColumnWidthMode::Reset
			? GetDefaultColumnWidth(i)
			: GetColumnWidth(i);
	}

	SetRedraw(false);
	for (int i = 1; i < m_iColumnsTracked; ++i)
		ShowColumn(i);

	for (int i = 0; i < m_iColumnsTracked; ++i) {
		const int iWidth = columnWidths[static_cast<size_t>(i)];
		SetColumnWidth(i, iWidth >= 2 ? iWidth : GetDefaultColumnWidth(i));
	}

	GetHeaderCtrl()->SetOrderArray(m_iColumnsTracked, columnOrder.data());
	for (int i = 0; i < m_iColumnsTracked; ++i)
		m_aColumns[columnOrder[static_cast<size_t>(i)]].iLocation = i;

	for (int i = 1; i < m_iColumnsTracked; ++i) {
		if (IsPresetHiddenColumn(ePreset, i))
			HideColumn(i);
	}
	ResetSortState();
	SaveSettings();
	SetRedraw(true);
	Invalidate(FALSE);
	return true;
}

HBITMAP LoadImageAsPARGB(LPCTSTR pszPath)
{
	HBITMAP hbmPARGB = NULL;
	ULONG_PTR gdiplusToken = 0;
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL) == Gdiplus::Ok) {
		Gdiplus::Bitmap bmp(pszPath);
		bmp.GetHBITMAP(NULL, &hbmPARGB);
	}
	Gdiplus::GdiplusShutdown(gdiplusToken);
	return hbmPARGB;
}

void CMuleListCtrl::SetColors()
{
	m_crWindow = ::GetSysColor(COLOR_WINDOW);
	m_crWindowText = ::GetSysColor(COLOR_WINDOWTEXT);
	m_crWindowTextBk = m_crWindow;

	COLORREF crHighlight = ::GetSysColor(COLOR_HIGHLIGHT);

	CString strBkImage;
	const CString &sSkinProfile(thePrefs.GetSkinProfile());
	if (!sSkinProfile.IsEmpty()) {
		const CString strKey(m_strSkinKey.IsEmpty() ? _T("DefLv") : m_strSkinKey);

		if (theApp.LoadSkinColorAlt(strKey + _T("Bk"), _T("DefLvBk"), m_crWindow))
			m_crWindowTextBk = m_crWindow;
		theApp.LoadSkinColorAlt(strKey + _T("Fg"), _T("DefLvFg"), m_crWindowText);
		theApp.LoadSkinColorAlt(strKey + _T("Hl"), _T("DefLvHl"), crHighlight);

		strBkImage = ShellUiHelpers::GetProfileString(_T("Colors"), strKey + _T("BkImg"), NULL, sSkinProfile);
		if (strBkImage.IsEmpty())
			strBkImage = ShellUiHelpers::GetProfileString(_T("Colors"), _T("DefLvBkImg"), NULL, sSkinProfile);
	}

	SetBkColor(m_crWindow);
	SetTextBkColor(m_crWindowTextBk);
	SetTextColor(m_crWindowText);

	// Must explicitly set a NULL watermark bitmap, to clear any already set watermark bitmap.
	LVBKIMAGE lvimg = {};
	lvimg.ulFlags = LVBKIF_TYPE_WATERMARK;
	SetBkImage(&lvimg);

	if (!strBkImage.IsEmpty() && !g_bLowColorDesktop) {
		const CString strFullResPath(ShellUiHelpers::ResolveSkinResourcePath(sSkinProfile, strBkImage));
		HBITMAP hbm = LoadImageAsPARGB(strFullResPath);
		if (hbm) {
			LVBKIMAGE lvbkimg = {};
			lvbkimg.ulFlags = LVBKIF_TYPE_WATERMARK;
			lvbkimg.ulFlags |= LVBKIF_FLAG_ALPHABLEND;
			lvbkimg.hbm = hbm;
			if (SetBkImage(&lvbkimg)) {
				m_crWindowTextBk = CLR_NONE;
				SetTextBkColor(m_crWindowTextBk);
			} else
				::DeleteObject(lvbkimg.hbm);
		}
	}

	m_crFocusLine = crHighlight;
	if (g_bLowColorDesktop) {
		m_crNoHighlight = crHighlight;
		m_crNoFocusLine = crHighlight;
		m_crHighlight = crHighlight;
		m_crHighlightText = ::GetSysColor(COLOR_HIGHLIGHTTEXT);
		m_crGlow = crHighlight;
	} else {
		m_crNoHighlight = MLC_RGBBLEND(crHighlight, m_crWindow, 8);
		m_crNoFocusLine = MLC_RGBBLEND(crHighlight, m_crWindow, 2);
		m_crHighlight = MLC_RGBBLEND(crHighlight, m_crWindow, 4);
		m_crHighlightText = m_crWindowText;
		m_crGlow = MLC_RGBBLEND(crHighlight, m_crWindow, 3);
	}
}

void CMuleListCtrl::SetSortArrow(int iColumn, ArrowType atType)
{
	HDITEM headerItem;
	headerItem.mask = HDI_FORMAT;
	CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();

	if (iColumn != m_iCurrentSortItem) {
		pHeaderCtrl->GetItem(m_iCurrentSortItem, &headerItem);
		headerItem.fmt &= ~(HDF_IMAGE | HDF_BITMAP_ON_RIGHT);
		pHeaderCtrl->SetItem(m_iCurrentSortItem, &headerItem);
		m_iCurrentSortItem = iColumn;
		m_imlHeaderCtrl.DeleteImageList();
	}

	//place new arrow unless we were given an invalid column
	if (iColumn >= 0 && pHeaderCtrl->GetItem(iColumn, &headerItem)) {
		m_atSortArrow = atType;

		HINSTANCE hInstRes = AfxFindResourceHandle(MAKEINTRESOURCE(m_atSortArrow), RT_BITMAP);
		if (hInstRes != NULL) {
			HBITMAP hbmSortStates = (HBITMAP)::LoadImage(hInstRes, MAKEINTRESOURCE(m_atSortArrow), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_LOADMAP3DCOLORS);
			if (hbmSortStates != NULL) {
				CBitmap bmSortStates;
				bmSortStates.Attach(hbmSortStates);

				CImageList imlSortStates;
				if (imlSortStates.Create(14, 14, theApp.m_iDfltImageListColorFlags | ILC_MASK, 1, 0)) {
					VERIFY(imlSortStates.Add(&bmSortStates, RGB(255, 0, 255)) >= 0);

					// To avoid drawing problems (which occur only with an image list *with* a mask) while
					// resizing list view columns which have the header control bitmap right aligned, set
					// the background color of the image list.
					if (theApp.m_ullComCtrlVer < MAKEDLLVERULL(6, 0, 0, 0))
						imlSortStates.SetBkColor(::GetSysColor(COLOR_BTNFACE));

					// When setting the image list for the header control for the first time we'll get
					// the image list of the listview control!! So, better store the header control image list separate.
					(void)pHeaderCtrl->SetImageList(&imlSortStates);
					m_imlHeaderCtrl.DeleteImageList();
					m_imlHeaderCtrl.Attach(imlSortStates.Detach());

					// Use smaller bitmap margins -- this saves some pixels which may be required for
					// rather small column titles.
					if (theApp.m_ullComCtrlVer >= MAKEDLLVERULL(5, 8, 0, 0)) {
						int iBmpMargin = pHeaderCtrl->GetBitmapMargin();
						int iNewBmpMargin = ::GetSystemMetrics(SM_CXEDGE) + ::GetSystemMetrics(SM_CXEDGE) / 2;
						if (iNewBmpMargin < iBmpMargin)
							pHeaderCtrl->SetBitmapMargin(iNewBmpMargin);
					}
				}
			}
		}
		headerItem.mask |= HDI_IMAGE;
		headerItem.fmt |= HDF_IMAGE | HDF_BITMAP_ON_RIGHT;
		headerItem.iImage = 0;
		pHeaderCtrl->SetItem(iColumn, &headerItem);
	}
}

// move item in list, returns index of new item
int CMuleListCtrl::MoveItem(int iOldIndex, int iNewIndex)
{
	if (iNewIndex > iOldIndex)
		--iNewIndex;

	// copy item
	LVITEM lvi;
	TCHAR szText[256];
	lvi.mask = LVIF_TEXT | LVIF_STATE | LVIF_PARAM | LVIF_INDENT | LVIF_IMAGE | LVIF_NORECOMPUTE;
	lvi.stateMask = UINT_MAX;
	lvi.iItem = iOldIndex;
	lvi.iSubItem = 0;
	lvi.pszText = szText;
	lvi.cchTextMax = _countof(szText);
	lvi.iIndent = 0;
	if (!GetItem(&lvi))
		return -1;

	// copy strings of sub items
	CSimpleArray<void*> aSubItems;
	DWORD Style = GetStyle();
	if ((Style & LVS_OWNERDATA) == 0) {
		TCHAR szText1[256];
		LVITEM lvi1;
		lvi1.mask = LVIF_TEXT | LVIF_NORECOMPUTE;
		lvi1.iItem = iOldIndex;
		lvi1.cchTextMax = _countof(szText1);
		for (int i = 1; i < m_iColumnsTracked; ++i) {
			void *pstrSubItem;
			lvi1.iSubItem = i;
			lvi1.pszText = szText1;
			if (GetItem(&lvi1))
				if (lvi1.pszText == LPSTR_TEXTCALLBACK)
					pstrSubItem = LPSTR_TEXTCALLBACK;
				else
					pstrSubItem = new CString(szText1);
			else
				pstrSubItem = NULL;
			aSubItems.Add(pstrSubItem);
		}
	}

	// do the move
	SetRedraw(false);
	DeleteItem(iOldIndex);
	lvi.iItem = iNewIndex;
	iNewIndex = InsertItem(&lvi);

	// restore strings of sub items
	if ((Style & LVS_OWNERDATA) == 0) {
		for (int i = 1; i < m_iColumnsTracked; ++i) {
			LVITEM lvi1;
			lvi1.iSubItem = i;
			void *pstrSubItem = aSubItems[i - 1];
			if (pstrSubItem != NULL) {
				if (pstrSubItem == LPSTR_TEXTCALLBACK)
					lvi1.pszText = LPSTR_TEXTCALLBACK;
				else
					lvi1.pszText = const_cast<LPTSTR>((LPCTSTR)(*(CString*)pstrSubItem));
				DefWindowProc(LVM_SETITEMTEXT, iNewIndex, (LPARAM)&lvi1);
				if (pstrSubItem != LPSTR_TEXTCALLBACK)
					delete (CString*)pstrSubItem;
			}
		}
	}

	SetRedraw(true);

	return iNewIndex;
}

bool CMuleListCtrl::EnsureParamSnapshot(bool bForce)
{
	if (GetStyle() & LVS_OWNERDATA)
		return true;

	const int iItemCount = GetItemCount();
	if (!bForce && m_Params.GetCount() == iItemCount)
		return true;

	m_Params.RemoveAll();
	for (int i = 0; i < iItemCount; ++i)
		m_Params.AddTail(CListCtrl::GetItemData(i));
	return m_Params.GetCount() == iItemCount;
}

int CMuleListCtrl::UpdateLocation(int iItem)
{
	if (GetStyle() & LVS_OWNERDATA)
		return iItem;

	int iItemCount = GetItemCount();
	if (iItem >= iItemCount || iItem < 0)
		return iItem;
	if (!EnsureParamSnapshot())
		return iItem;

	bool notLast = iItem + 1 < iItemCount;

	DWORD_PTR dwpItemData = GetItemData(iItem);
	if (dwpItemData == NULL)
		return iItem;

	if (iItem > 0) { //not first
		int iNewIndex = iItem - 1;
		POSITION pos = m_Params.FindIndex(iNewIndex);
		if (pos == NULL)
			return iItem;
		int iResult = m_SortProc(dwpItemData, GetParamAt(pos, iNewIndex), m_dwParamSort);
		if (iResult < 0) {
			POSITION posPrev = pos;
			int iDist = iNewIndex / 2;
			while (iDist > 1) {
				for (int i = 0; i < iDist; ++i)
					m_Params.GetPrev(posPrev);

				if (m_SortProc(dwpItemData, GetParamAt(posPrev, iNewIndex - iDist), m_dwParamSort) < 0) {
					iNewIndex = iNewIndex - iDist;
					pos = posPrev;
				} else
					posPrev = pos;

				iDist /= 2;
			}
			while (--iNewIndex >= 0) {
				m_Params.GetPrev(pos);
				if (m_SortProc(dwpItemData, GetParamAt(pos, iNewIndex), m_dwParamSort) >= 0)
					break;
			}
			MoveItem(iItem, iNewIndex + 1);
			return iNewIndex + 1;
		}
	}

	if (notLast) {
		int iNewIndex = iItem + 1;
		POSITION pos = m_Params.FindIndex(iNewIndex);
		if (pos == NULL)
			return iItem;
		int iResult = m_SortProc(dwpItemData, GetParamAt(pos, iNewIndex), m_dwParamSort);
		if (iResult > 0) {
			POSITION posNext = pos;
			int iDist = (GetItemCount() - iNewIndex) / 2;
			while (iDist > 1) {
				for (int i = 0; i < iDist; ++i)
					m_Params.GetNext(posNext);

				if (m_SortProc(dwpItemData, GetParamAt(posNext, iNewIndex + iDist), m_dwParamSort) > 0) {
					iNewIndex = iNewIndex + iDist;
					pos = posNext;
				} else
					posNext = pos;

				iDist /= 2;
			}
			while (++iNewIndex < iItemCount) {
				m_Params.GetNext(pos);
				if (m_SortProc(dwpItemData, GetParamAt(pos, iNewIndex), m_dwParamSort) <= 0)
					break;
			}
			MoveItem(iItem, iNewIndex);
			return iNewIndex;
		}
	}

	return iItem;
}

DWORD_PTR CMuleListCtrl::GetItemData(int iItem)
{
	if (GetStyle() & LVS_OWNERDATA)
		return GetVirtualItemData(iItem);

	POSITION pos = m_Params.FindIndex(iItem);
	if (pos == NULL && EnsureParamSnapshot())
		pos = m_Params.FindIndex(iItem);
	LPARAM lParam = GetParamAt(pos, iItem);
	MLC_ASSERT(pos == NULL || lParam == CListCtrl::GetItemData(iItem));
	return lParam;
}

//At this level, lower than everything else, no poorly overriden function should break us
BOOL CMuleListCtrl::OnWndMsg(UINT message, WPARAM wParam, LPARAM lParam, LRESULT *pResult)
{
	//let's look for the important messages that are essential to handle
	switch (message) {
	case WM_NOTIFY:
		if (wParam)
			break;
		switch (reinterpret_cast<LPNMHDR>(lParam)->code) {
		case NM_RCLICK: //handle right click on headers and show column menu
			{
				POINT point;
				::GetCursorPos(&point);

				CTitledMenu tmColumnMenu;
				tmColumnMenu.CreatePopupMenu();

				CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();
				int iCount = pHeaderCtrl->GetItemCount();
				for (int iCurrent = 1; iCurrent < iCount; ++iCurrent) {
					HDITEM item;
					TCHAR text[255];
					item.pszText = text;
					item.mask = HDI_TEXT;
					item.cchTextMax = _countof(text);
					pHeaderCtrl->GetItem(iCurrent, &item);

					tmColumnMenu.AppendMenu(MF_STRING | (m_aColumns[iCurrent].bHidden ? 0 : MF_CHECKED)
						, MLC_IDC_MENU + iCurrent, item.pszText);
				}
				tmColumnMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
				VERIFY(tmColumnMenu.DestroyMenu());
			}
			return (BOOL)(*pResult = 1);
		case HDN_BEGINTRACKW: //forbid changing the size of anything "before" the first column
		case HDN_BEGINTRACKA:
			if (m_aColumns[reinterpret_cast<HD_NOTIFY*>(lParam)->iItem].bHidden)
				return (BOOL)(*pResult = 1);
			break;
		case HDN_ENDDRAG: //forbid moving the first column
			{
				LPNMHEADER pHeader = reinterpret_cast<LPNMHEADER>(lParam);
				if (pHeader->iItem != 0 && pHeader->pitem->iOrder != 0) {
					int iNewLoc = pHeader->pitem->iOrder - GetHiddenColumnCount();
					if (iNewLoc > 0) {
						int iOldLoc = m_aColumns[pHeader->iItem].iLocation;
						if (iOldLoc != iNewLoc) {
							if (iOldLoc > iNewLoc) {
								for (int i = m_iColumnsTracked; --i >= 0;)
									if (m_aColumns[i].iLocation >= iNewLoc && m_aColumns[i].iLocation < iOldLoc)
										++m_aColumns[i].iLocation;
							} else { //iOldLoc < iNewLoc
								for (int i = m_iColumnsTracked; --i >= 0;)
									if (m_aColumns[i].iLocation > iOldLoc && m_aColumns[i].iLocation <= iNewLoc)
										--m_aColumns[i].iLocation;
							}
							m_aColumns[pHeader->iItem].iLocation = iNewLoc;

							Invalidate(FALSE);
							break;
						}
					}
				}
			}
			return (BOOL)(*pResult = 1);
		case HDN_DIVIDERDBLCLICKW:
		case HDN_DIVIDERDBLCLICKA:
			// The effect of LVSCW_AUTOSIZE_USEHEADER is as follows:
			//	If the listview control can query for all the items in a column, it is
			//	capable of computing the minimal width needed to display the item with
			//	the largest width. However, if the width of the header label is larger
			//	then the largest width of the items in a column, the width of the header label
			//	will overrule the width which would be needed for the items in the column.
			//	In practice this means, that the column could get larger than really needed
			//	for the items in the column (just because the width gets adjusted for
			//	showing the header label also).
			//	This is a good solution for some of our listviews which do not (yet) provide
			//	the according functions which would give the listview control the chance to
			//	query for all items in a column. This flag will thus lead to sizing the
			//	column at least to the width of the header label. That's at least better
			//	than resizing the column to zero width (which would be the alternative).
			//
			// Though, a few of our listviews are already capable of providing all the
			// information which is needed by the listview control to properly auto size
			// a column. Those listviews can set the 'm_iAutoSizeWidth' to 'LVSCW_AUTOSIZE'
			// which will lead to standard Windows behaviour.
			//
			if (GetStyle() & LVS_OWNERDRAWFIXED) {
				LPNMHEADER pHeader = reinterpret_cast<LPNMHEADER>(lParam);
				// If the listview is empty, the LVSCW_AUTOSIZE_USEHEADER is more appropriate,
				// even if some listview has requested LVSCW_AUTOSIZE.
				SetColumnWidth(pHeader->iItem, GetItemCount() == 0 ? LVSCW_AUTOSIZE_USEHEADER : m_iAutoSizeWidth);
				return (BOOL)(*pResult = 1);
			}
		}
		break;
	case WM_COMMAND:
		//deal with menu clicks
		if (wParam == MLC_IDC_UPDATE) {
			UpdateLocation((int)lParam);
			return (BOOL)(*pResult = 1);
		}
		if (wParam >= MLC_IDC_MENU) {
			int iCount = GetHeaderCtrl()->GetItemCount();
			int iToggle = (int)(wParam - MLC_IDC_MENU);
			if (iToggle < iCount) {
				if (m_aColumns[iToggle].bHidden)
					ShowColumn(iToggle);
				else
					HideColumn(iToggle);

				return (BOOL)(*pResult = 1);
			}
		}
		break;
	case LVM_DELETECOLUMN:
		if (m_aColumns != NULL) {
			for (int i = 0; i < m_iColumnsTracked; ++i)
				if (m_aColumns[i].bHidden)
					ShowColumn(i);

			delete[] m_aColumns;
			m_aColumns = NULL; // 'new' may throw an exception
		}
		m_aColumns = new MULE_COLUMN[--m_iColumnsTracked];
		for (int i = m_iColumnsTracked; --i >= 0;) {
			m_aColumns[i].iLocation = i;
			m_aColumns[i].bHidden = false;
		}
		break;
	case LVM_INSERTCOLUMNW:
	case LVM_INSERTCOLUMNA:
		if (m_aColumns != NULL) {
			for (int i = 0; i < m_iColumnsTracked; ++i)
				if (m_aColumns[i].bHidden)
					ShowColumn(i);

			delete[] m_aColumns;
			m_aColumns = NULL; // 'new' may throw an exception
		}
		m_aColumns = new MULE_COLUMN[++m_iColumnsTracked];
		for (int i = m_iColumnsTracked; --i >= 0;) {
			m_aColumns[i].iLocation = i;
			m_aColumns[i].bHidden = false;
		}
		break;
	case LVM_SETITEM:
		{
			POSITION pos = m_Params.FindIndex(reinterpret_cast<LPLVITEM>(lParam)->iItem);
			if (pos) {
				m_Params.SetAt(pos, MLC_MAGIC);
				if (m_eUpdateMode == lazy)
					PostMessage(LVM_UPDATE, reinterpret_cast<LPLVITEM>(lParam)->iItem);
				else if (m_eUpdateMode == direct)
					UpdateLocation(reinterpret_cast<LPLVITEM>(lParam)->iItem);
			}
		}
		break;
	case LVN_KEYDOWN:
		break;
	case LVM_SETITEMTEXT:
		//need to check for movement
		*pResult = DefWindowProc(message, wParam, lParam);
		if (*pResult) {
			if (m_eUpdateMode == lazy)
				PostMessage(WM_COMMAND, MLC_IDC_UPDATE, wParam);
			else if (m_eUpdateMode == direct)
				UpdateLocation((int)wParam);
		}
		return *pResult != 0;
	case LVM_SORTITEMS:
		m_dwParamSort = (LPARAM)wParam;
		m_SortProc = (PFNLVCOMPARE)lParam;
		for (POSITION pos = m_Params.GetHeadPosition(); pos != NULL; m_Params.GetNext(pos))
			m_Params.SetAt(pos, MLC_MAGIC);
		break;
	case LVM_DELETEALLITEMS:
		if (!CListCtrl::OnWndMsg(message, wParam, lParam, pResult) && DefWindowProc(message, wParam, lParam))
			m_Params.RemoveAll();
		return (BOOL)(*pResult = 1);
	case LVM_DELETEITEM:
		{
			const int iItem = static_cast<int>(wParam);
			const bool bValidItem = iItem >= 0 && iItem < GetItemCount();
			if (bValidItem)
				EnsureParamSnapshot();
			POSITION pos = m_Params.FindIndex(iItem);
			if (bValidItem && pos != NULL && m_Params.GetAt(pos) != CListCtrl::GetItemData(iItem)) {
				EnsureParamSnapshot(true);
				pos = m_Params.FindIndex(iItem);
			}
			MLC_ASSERT(!bValidItem || pos == NULL || m_Params.GetAt(pos) == CListCtrl::GetItemData(iItem));
			if (!CListCtrl::OnWndMsg(message, wParam, lParam, pResult) && DefWindowProc(message, wParam, lParam)) {
				if (pos != NULL)
					m_Params.RemoveAt(pos);
				else
					EnsureParamSnapshot(true);
			}
		}
		return (BOOL)(*pResult = 1);
	case LVM_INSERTITEMA:
	case LVM_INSERTITEMW:
		//try to fix position of inserted items
		{
			LPLVITEM pItem = reinterpret_cast<LPLVITEM>(lParam);
			int iItem = pItem->iItem;
			int iItemCount = GetItemCount();
			bool notLast = iItem < iItemCount;
			if (!EnsureParamSnapshot()) {
				LRESULT lResult = DefWindowProc(message, wParam, lParam);
				if (lResult != -1)
					EnsureParamSnapshot(true);
				return (BOOL)(*pResult = lResult);
			}

			if (iItem > 0) { //not first
				int iNewIndex = iItem - 1;
				POSITION pos = m_Params.FindIndex(iNewIndex);
				if (pos == NULL)
					notLast = false;
				else {
					int iResult = m_SortProc(pItem->lParam, GetParamAt(pos, iNewIndex), m_dwParamSort);
					if (iResult < 0) {
						POSITION posPrev = pos;
						for (int iDist = iNewIndex / 2; iDist > 1;) {
							for (int i = 0; i < iDist; ++i)
								m_Params.GetPrev(posPrev);

							if (m_SortProc(pItem->lParam, GetParamAt(posPrev, iNewIndex - iDist), m_dwParamSort) < 0) {
								iNewIndex = iNewIndex - iDist;
								pos = posPrev;
							} else
								posPrev = pos;

							iDist /= 2;
						}
						while (--iNewIndex >= 0) {
							m_Params.GetPrev(pos);
							if (m_SortProc(pItem->lParam, GetParamAt(pos, iNewIndex), m_dwParamSort) >= 0)
								break;
						}
						pItem->iItem = iNewIndex + 1;
						notLast = false;
					}
				}
			}

			if (notLast) {
				int iNewIndex = iItem;
				POSITION pos = m_Params.FindIndex(iNewIndex);
				if (pos == NULL)
					notLast = false;
				else {
					int iResult = m_SortProc(pItem->lParam, GetParamAt(pos, iNewIndex), m_dwParamSort);
					if (iResult > 0) {
						POSITION posNext = pos;
						int iDist = (GetItemCount() - iNewIndex) / 2;
						while (iDist > 1) {
							for (int i = 0; i < iDist; ++i)
								m_Params.GetNext(posNext);

							if (m_SortProc(pItem->lParam, GetParamAt(posNext, iNewIndex + iDist), m_dwParamSort) > 0) {
								iNewIndex = iNewIndex + iDist;
								pos = posNext;
							} else
								posNext = pos;

							iDist /= 2;
						}
						while (++iNewIndex < iItemCount) {
							m_Params.GetNext(pos);
							if (m_SortProc(pItem->lParam, GetParamAt(pos, iNewIndex), m_dwParamSort) <= 0)
								break;
						}
						pItem->iItem = iNewIndex;
					}
				}
			}

			if (pItem->iItem == 0) {
				m_Params.AddHead(pItem->lParam);
				return FALSE;
			}

			LRESULT lResult = DefWindowProc(message, wParam, lParam);
			if (lResult != -1) {
				if (lResult >= GetItemCount())
					m_Params.AddTail(pItem->lParam);
				else if (lResult == 0)
					m_Params.AddHead(pItem->lParam);
				else {
					POSITION posInsertAfter = m_Params.FindIndex(lResult - 1);
					if (posInsertAfter != NULL)
						m_Params.InsertAfter(posInsertAfter, pItem->lParam);
					else
						EnsureParamSnapshot(true);
				}
			}
			return (BOOL)(*pResult = lResult);
		}
	case WM_DESTROY:
		SaveSettings();
		break;
	case LVM_UPDATE:
		//better fix for old problem... normally Update(int) causes entire list to redraw
		if ((int)wParam == UpdateLocation((int)wParam)) { //no need to invalidate rect if item moved
			RECT rcItem;
			BOOL bResult = GetItemRect((int)wParam, &rcItem, LVIR_BOUNDS);
			if (bResult)
				InvalidateRect(&rcItem, FALSE);
			return (BOOL)(*pResult = bResult);
		}
		return (BOOL)(*pResult = 1);
	case WM_CONTEXTMENU:
		// If the context menu is opened with the _mouse_ and if it was opened _outside_
		// the client area of the list view, let Windows handle that message.
		// Otherwise we would prevent the context menu for e.g. scrollbars to be invoked.
		if ((HWND)wParam == m_hWnd) {
			CPoint ptMouse(lParam);
			if (ptMouse.x != -1 || ptMouse.y != -1) {
				ScreenToClient(&ptMouse);
				CRect rcClient;
				GetClientRect(&rcClient);
				if (!rcClient.PtInRect(ptMouse))
					return DefWindowProc(message, wParam, lParam) != 0;
			}
		}
	}

	return CListCtrl::OnWndMsg(message, wParam, lParam, pResult);
}

void CMuleListCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	switch(nChar) {
	case VK_DELETE:
		PostMessage(WM_COMMAND, MPG_DELETE, 0);
		break;
	case VK_ESCAPE:
		if (GetKeyState(VK_CONTROL) >= 0 && GetKeyState(VK_MENU) >= 0 && GetKeyState(VK_SHIFT) >= 0 && ClearFindTextIfActive())
			return;
		break;
	case VK_F2:
		PostMessage(WM_COMMAND, MPG_F2, 0);
		break;
	case VK_F3:
		if (m_bGeneralPurposeFind)
			if (GetKeyState(VK_SHIFT) & 0x8000)
				// Shift+F3: Search previous
				OnFindPrev();
			else
				// F3: Search next
				OnFindNext();
		break;
	default:
		if (GetKeyState(VK_CONTROL) < 0) {
			const UINT uCommonCommand = MuleListKeyboardShortcutsSeams::ClassifyCommonKeyMessage(
				WM_KEYDOWN,
				nChar,
				true,
				GetKeyState(VK_MENU) < 0,
				GetKeyState(VK_SHIFT) < 0);
			if (uCommonCommand != 0) {
				SendMessage(WM_COMMAND, uCommonCommand);
				break;
			}
			switch (nChar) {
			case 'A': // Ctrl+A: Select all items
				LVITEM theItem;
				theItem.mask = LVIF_STATE;
				theItem.iItem = -1;
				theItem.iSubItem = 0;
				theItem.state = LVIS_SELECTED;
				theItem.stateMask = 2;
				SetItemState(-1, &theItem);
				break;
			case 'C': // Ctrl+C: Copy key combo
				SendMessage(WM_COMMAND, MP_COPYSELECTED);
				break;
			case 'F': // Ctrl+F: Search item
				if (m_bGeneralPurposeFind)
					OnFindStart();
				break;
			case 'V': // Ctrl+V: Paste key combo
				SendMessage(WM_COMMAND, MP_PASTE);
				break;
			case 'X': // Ctrl+X: Cut key combo
				SendMessage(WM_COMMAND, MP_CUT);
			}
		}
	}

	return CListCtrl::OnKeyDown(nChar, nRepCnt, nFlags);
}

bool CMuleListCtrl::ClearFindTextIfActive()
{
	if (!m_bGeneralPurposeFind || m_strFindText.IsEmpty())
		return false;
	m_strFindText.Empty();
	return true;
}

BOOL CMuleListCtrl::OnChildNotify(UINT message, WPARAM wParam, LPARAM lParam, LRESULT *pResult)
{
	if (message != WM_DRAWITEM) {
		//catch the prepaint and copy struct
		if (message == WM_NOTIFY && reinterpret_cast<LPNMHDR>(lParam)->code == NM_CUSTOMDRAW
			&& reinterpret_cast<LPNMLVCUSTOMDRAW>(lParam)->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
		{
			m_bCustomDraw = CListCtrl::OnChildNotify(message, wParam, lParam, pResult);
			if (m_bCustomDraw)
				m_lvcd = *reinterpret_cast<LPNMLVCUSTOMDRAW>(lParam);

			return m_bCustomDraw;
		}

		return CListCtrl::OnChildNotify(message, wParam, lParam, pResult);
	}

	ASSERT(pResult == NULL); // no return value expected

	DrawItem(reinterpret_cast<LPDRAWITEMSTRUCT>(lParam));
	return TRUE;
}

//comctl32.dll's "smooth scroll" consistently leaves garbage below the bottom item
//Until a better way was found, clear the mess on EndScroll.
void CMuleListCtrl::OnLvnEndScrollList(LPNMHDR pNMHDR, LRESULT *pResult)
{
	if (reinterpret_cast<LPNMLVSCROLL>(pNMHDR)->dy > 0) { //down scroll only
		int iLast = GetItemCount() - 1;
		//if (IsItemVisible(iLast)) { //at least Server 2003 or Vista required
		if (iLast <= GetTopIndex() + GetCountPerPage()) {
			RECT rcClient, rcItem;
			GetClientRect(&rcClient);
			GetItemRect(iLast, &rcItem, LVIR_BOUNDS);
			if (rcItem.bottom < rcClient.bottom) {
				rcClient.top = rcItem.bottom;
				CDC *pDC = GetDC();
				if (pDC != NULL) {
					pDC->FillSolidRect(&rcClient, GetBkColor());
					ReleaseDC(pDC);
				}
			}
		}
	}
	*pResult = 0;
}

void CMuleListCtrl::InitItemMemDC(CMemoryDC &dc, LPDRAWITEMSTRUCT lpDrawItemStruct, BOOL &bCtrlFocused)
{
	bCtrlFocused = ((GetFocus() == this) || (GetStyle() & LVS_SHOWSELALWAYS));

	if (lpDrawItemStruct->itemState & ODS_SELECTED)
		dc.FillBackground(bCtrlFocused ? m_crHighlight : m_crNoHighlight);
	else {
		if (m_crWindowTextBk == CLR_NONE) {
			DefWindowProc(WM_ERASEBKGND, (WPARAM)(HDC)dc, 0);
			dc.SetBkMode(TRANSPARENT);
		} else {
			ASSERT(m_crWindowTextBk == GetBkColor());
			dc.FillBackground(m_crWindowTextBk);
		}
	}

	dc.SetTextColor((lpDrawItemStruct->itemState & ODS_SELECTED) ? m_crHighlightText : m_crWindowText);
	dc.SetFont(GetFont());
}

void CMuleListCtrl::LocaliseHeaderCtrl(const UINT *const uids, size_t cnt)
{
	CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();
	HDITEM hdi;
	hdi.mask = HDI_TEXT;
	for (size_t i = 0; i < cnt; ++i)
		if (uids[i]) {
			const CString &sText(GetResString(uids[i]));
			hdi.pszText = const_cast<LPTSTR>((LPCTSTR)sText);
			pHeaderCtrl->SetItem((int)i, &hdi);
		}
}

void CMuleListCtrl::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	//set up our flicker free drawing
	CRect rcItem(lpDrawItemStruct->rcItem);
	CMemoryDC pDC(CDC::FromHandle(lpDrawItemStruct->hDC), &rcItem, m_crWindow);
	CFont *pOldFont = pDC->SelectObject(GetFont());
	RECT rcClient;
	GetClientRect(&rcClient);

	int iItem = lpDrawItemStruct->itemID;

	//gets the item image and state info
	LVITEM lvi;
	lvi.mask = LVIF_IMAGE | LVIF_STATE;
	lvi.iItem = iItem;
	lvi.iSubItem = 0;
	lvi.stateMask = LVIS_DROPHILITED | LVIS_FOCUSED | LVIS_SELECTED | LVIS_GLOW;
	GetItem(&lvi);

	//see if the item is highlighted
	BOOL bHighlight = ((lvi.state & LVIS_DROPHILITED) || (lvi.state & LVIS_SELECTED));
	BOOL bCtrlFocused = ((GetFocus() == this) || (GetStyle() & LVS_SHOWSELALWAYS));
	BOOL bGlowing = (lvi.state & LVIS_GLOW);

	COLORREF clr;
	if (m_bCustomDraw)
		clr = (bHighlight && g_bLowColorDesktop) ? m_crHighlightText : m_lvcd.clrText;
	else
		clr = bHighlight ? m_crHighlightText : m_crWindowText;
	pDC->SetTextColor(clr);

	//get rectangles for drawing
	RECT rcBounds, rcLabel, rcIcon;
	GetItemRect(iItem, &rcBounds, LVIR_BOUNDS);
	GetItemRect(iItem, &rcLabel, LVIR_LABEL);
	GetItemRect(iItem, &rcIcon, LVIR_ICON);
	CRect rcCol(rcBounds);

	//draw the background color
	if (!bHighlight && !bGlowing && m_crWindowTextBk == CLR_NONE)
		DefWindowProc(WM_ERASEBKGND, (WPARAM)pDC->m_hDC, 0);
	else {
		if (bHighlight)
			if (bCtrlFocused)
				clr = m_crHighlight;
			else
				clr = bGlowing ? m_crGlow : m_crNoHighlight;
		else
			clr = bGlowing ? m_crGlow : m_crWindow;
		pDC->FillSolidRect(&rcBounds, clr);
	}

	//update column
	rcCol.right = rcCol.left + GetColumnWidth(0);

	//draw state icon
	if (lvi.state & LVIS_STATEIMAGEMASK) {
		int nImage = ((lvi.state & LVIS_STATEIMAGEMASK) >> 12) - 1;
		CImageList *pImageList = GetImageList(LVSIL_STATE);
		if (pImageList)
			pImageList->Draw(pDC, nImage, rcCol.TopLeft(), ILD_TRANSPARENT);
	}

	//draw the item's icon
	CImageList *pImageList = GetImageList(LVSIL_SMALL);
	if (pImageList) {
		int iIconY = max((rcItem.Height() - 16) / 2, 0);
		const POINT point{rcItem.left, rcItem.top + iIconY};
		pImageList->Draw(pDC, lvi.iImage, point, ILD_TRANSPARENT);
	}

	if (m_crWindowTextBk == CLR_NONE)
		pDC->SetBkMode(TRANSPARENT);

	//the label!
	const CString &sLabel0(GetItemText(iItem, 0));
	//labels are offset by a certain amount related to the width of a space character

	//draw item label (column 0)
	rcLabel.left += sm_iLabelOffset;
	rcLabel.right -= sm_iLabelOffset;
	pDC->DrawText(sLabel0, &rcLabel, MLC_DT_TEXT | DT_NOCLIP);

	//draw labels for remaining columns
	LVCOLUMN lvc;
	lvc.mask = LVCF_FMT | LVCF_WIDTH;

	const CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();
	int iCount = pHeaderCtrl->GetItemCount();
	for (int iCurrent = 1; iCurrent < iCount; ++iCurrent) {
		int iColumn = pHeaderCtrl->OrderToIndex(iCurrent);
		//don't draw column 0 again
		if (iColumn == 0)
			continue;

		GetColumn(iColumn, &lvc);
		//don't draw anything with 0 width
		if (lvc.cx <= 0)
			continue;

		rcCol.left = rcCol.right;
		rcCol.right += lvc.cx;

		if (HaveIntersection(rcClient, rcCol)) {
			const CString &sLabel(GetItemText(iItem, iColumn));
			if (sLabel.IsEmpty())
				continue;

			//get the text justification
			UINT nJustify;
			switch (lvc.fmt & LVCFMT_JUSTIFYMASK) {
			case LVCFMT_RIGHT:
				nJustify = DT_RIGHT;
				break;
			case LVCFMT_CENTER:
				nJustify = DT_CENTER;
				break;
			case LVCFMT_LEFT:
			default:
				nJustify = DT_LEFT;
			}

			//label text
			rcCol.left += sm_iLabelOffset + sm_iSubItemInset;
			rcCol.right -= sm_iLabelOffset + sm_iSubItemInset;
			pDC->SetBkColor(clr);
			pDC->DrawText(sLabel, &rcCol, MLC_DT_TEXT | nJustify);
			rcCol.right += sm_iLabelOffset + sm_iSubItemInset; //restore the position
		}
	}

	DrawFocusRect(pDC, &rcBounds, lvi.state & LVIS_FOCUSED, bCtrlFocused, lvi.state & LVIS_SELECTED);

	pDC->Flush();
	pDC->SelectObject(pOldFont);
}

void CMuleListCtrl::DrawFocusRect(CDC *pDC, LPCRECT rcItem, BOOL bItemFocused, BOOL bCtrlFocused, BOOL bItemSelected)
{
	//draw focus rectangle if the item has focus
	if (bItemFocused && (bCtrlFocused || bItemSelected)) {
		CBrush brush(bCtrlFocused && bItemSelected ? m_crFocusLine : m_crNoFocusLine);
		pDC->FrameRect(rcItem, &brush);
	}
}

BOOL CMuleListCtrl::OnEraseBkgnd(CDC *pDC)
{
	//if (m_crWindowTextBk == CLR_NONE) // this creates a lot of screen flickering
	//	return CListCtrl::OnEraseBkgnd(pDC);

	int itemCount = GetItemCount();
	if (!itemCount)
		return CListCtrl::OnEraseBkgnd(pDC);

	int topIndex = GetTopIndex();
	int maxItems = GetCountPerPage();
	RECT rcClient, rcItem;
	//draw top portion
	GetClientRect(&rcClient);
	RECT rcClip(rcClient);
	GetItemRect(topIndex, &rcItem, LVIR_BOUNDS);
	rcClient.bottom = rcItem.top;
	if (m_crWindowTextBk != CLR_NONE)
		pDC->FillSolidRect(&rcClient, GetBkColor());
	else
		rcClip.top = rcItem.top;

	//draw bottom portion if we have to
	if (topIndex + maxItems >= itemCount) {
		int drawnItems = itemCount < maxItems ? itemCount : maxItems;
		GetClientRect(&rcClient);
		GetItemRect(topIndex + drawnItems - 1, &rcItem, LVIR_BOUNDS);
		rcClient.top = rcItem.bottom;
		rcClip.bottom = rcItem.bottom;
		if (m_crWindowTextBk != CLR_NONE)
			pDC->FillSolidRect(&rcClient, GetBkColor());
	}

	//draw right half if we need to
	if (rcItem.right < rcClient.right) {
		GetClientRect(&rcClient);
		rcClient.left = rcItem.right;
		rcClip.right = rcItem.right;
		if (m_crWindowTextBk != CLR_NONE)
			pDC->FillSolidRect(&rcClient, GetBkColor());
	}

	if (m_crWindowTextBk == CLR_NONE) {
		CRect rcClipBox;
		pDC->GetClipBox(rcClipBox);
		rcClipBox.SubtractRect(rcClipBox, &rcClip);
		if (!rcClipBox.IsRectEmpty()) {
			pDC->ExcludeClipRect(&rcClip);
			CListCtrl::OnEraseBkgnd(pDC);
			InvalidateRect(&rcClip, FALSE);
		}
	}
	return TRUE;
}

void CMuleListCtrl::OnSysColorChange()
{
	//adjust colors
	CListCtrl::OnSysColorChange();
	SetColors();

	//redraw the up/down sort arrow (if it's there)
	if (m_iCurrentSortItem >= 0)
		SetSortArrow(m_iCurrentSortItem, (ArrowType)m_atSortArrow);

	if (thePrefs.GetUseSystemFontForMainControls()) {
		// Send a (useless) WM_WINDOWPOSCHANGED to the listview control to trigger a
		// WM_MEASUREITEM message which is needed to set the new item height in case
		// there was a font changed in the Windows System settings.
		//
		// Though it does not work as expected. Although we get the WM_MEASUREITEM and although
		// we return the correct (new) item height, the listview control does not redraw the
		// items with the new height until the control really gets resized.
		CRect rc;
		GetWindowRect(rc);
		WINDOWPOS wp;
		wp.hwnd = m_hWnd;
		wp.cx = rc.Width();
		wp.cy = rc.Height();
		wp.flags = SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_FRAMECHANGED;
		SendMessage(WM_WINDOWPOSCHANGED, 0, (LPARAM)&wp);
	}
}

void CMuleListCtrl::MeasureItem(LPMEASUREITEMSTRUCT lpMeasureItemStruct)
{
	Default();

	if (thePrefs.GetUseSystemFontForMainControls()) {
		CDC *pDC = GetDC();
		if (pDC) {
			CFont *pFont = GetFont();
			if (pFont) {
				CFont *pFontOld = pDC->SelectObject(pFont);
				TEXTMETRIC tm;
				pDC->GetTextMetrics(&tm);
				int iNewHeight = tm.tmHeight + tm.tmExternalLeading + 1;
				lpMeasureItemStruct->itemHeight = max(18, iNewHeight);
				pDC->SelectObject(pFontOld);
			}
			ReleaseDC(pDC);
		}
	}
}

HIMAGELIST CMuleListCtrl::ApplyImageList(HIMAGELIST himl)
{
	HIMAGELIST himlOld = (HIMAGELIST)SendMessage(LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)himl);
	if (m_imlHeaderCtrl.m_hImageList != NULL) {
		// Must *again* set the image list for the header control, because LVM_SETIMAGELIST
		// always resets any already specified header control image lists!
		GetHeaderCtrl()->SetImageList(&m_imlHeaderCtrl);
	}
	return himlOld;
}

void CMuleListCtrl::DoFind(int iStartItem, int iDirection /*1 = down, -1 = up*/, BOOL bShowError)
{
	if (iStartItem < 0) {
		MessageBeep(MB_OK);
		return;
	}

	CWaitCursor curHourglass;

	const int iNumItems = (iDirection > 0) ? GetItemCount() : 0;
	for (int iItem = iStartItem; ((iDirection > 0) ? iItem < iNumItems : iItem >= 0);) {
		const CString &strItemText(GetItemText(iItem, m_iFindColumn));
		if (!strItemText.IsEmpty()) {
			if ((m_bFindMatchCase ? _tcsstr(strItemText, m_strFindText) : stristr(strItemText, m_strFindText)) != NULL) {
				// Deselect all listview entries
				SetItemState(-1, 0, LVIS_SELECTED);

				// Select the found listview entry
				SetItemState(iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
				SetSelectionMark(iItem);
				EnsureVisible(iItem, FALSE/*bPartialOK*/);
				SetFocus();

				return;
			}
		}

		iItem += iDirection;
	}

	if (bShowError)
		LocMessageBox(IDS_SEARCH_NORESULT, MB_ICONINFORMATION, 0);
	else
		MessageBeep(MB_OK);
}

void CMuleListCtrl::OnFindStart()
{
	if (GetItemCount() == 0) {
		MessageBeep(MB_OK);
		return;
	}

	CListViewSearchDlg dlg;
	dlg.m_pListView = this;
	dlg.m_strFindText = m_strFindText;
	dlg.m_bCanSearchInAllColumns = m_bCanSearchInAllColumns;
	dlg.m_iSearchColumn = m_iFindColumn;
	if (dlg.DoModal() == IDOK && !dlg.m_strFindText.IsEmpty()) {
		m_strFindText = dlg.m_strFindText;
		m_iFindColumn = dlg.m_iSearchColumn;
		DoFindNext(TRUE/*bShowError*/);
	}
}

void CMuleListCtrl::OnFindNext()
{
	if (GetItemCount() == 0) {
		MessageBeep(MB_OK);
		return;
	}

	DoFindNext(FALSE/*bShowError*/);
}

void CMuleListCtrl::DoFindNext(BOOL bShowError)
{
	int iStartItem = GetNextItem(-1, LVNI_SELECTED | LVNI_FOCUSED);
	if (iStartItem < 0)
		iStartItem = 0;
	else
		iStartItem += m_iFindDirection;
	DoFind(iStartItem, m_iFindDirection, bShowError);
}

void CMuleListCtrl::OnFindPrev()
{
	if (GetItemCount() == 0) {
		MessageBeep(MB_OK);
		return;
	}

	int iStartItem = GetNextItem(-1, LVNI_SELECTED | LVNI_FOCUSED);
	if (iStartItem < 0)
		iStartItem = 0;
	else
		iStartItem -= m_iFindDirection;

	DoFind(iStartItem, -m_iFindDirection, FALSE/*bShowError*/);
}

BOOL CMuleListCtrl::PreTranslateMessage(MSG *pMsg)
{
	if (pMsg->message == WM_SYSKEYDOWN && pMsg->wParam == VK_RETURN && GetKeyState(VK_MENU) < 0) {
		PostMessage(WM_COMMAND, MPG_ALTENTER, 0);
		return TRUE;
	}

	if (m_hAccel != NULL && pMsg->message >= WM_KEYFIRST && pMsg->message <= WM_KEYLAST)
		// If we want to handle the VK_RETURN key, we have to do that via accelerators!
		if (TranslateAccelerator(m_hWnd, m_hAccel, pMsg))
			return TRUE;

	// Catch the "Ctrl+<NumPad_Plus_Key>" shortcut. CMuleListCtrl can not handle this.
	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_ADD && GetKeyState(VK_CONTROL) < 0)
		return TRUE;

	return CListCtrl::PreTranslateMessage(pMsg);
}

void CMuleListCtrl::AutoSelectItem()
{
	if (GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED) < 0) {
		int iItem = GetNextItem(-1, LVIS_FOCUSED);
		if (iItem >= 0) {
			SetItemState(iItem, LVIS_SELECTED, LVIS_SELECTED);
			SetSelectionMark(iItem);
		}
	}
}

// Parameter is used as a LONG value
// LOWORD(dwNewOrder) - sort item, HIWORD(dwNewOrder) - sort direction
// HIWORD(dwNewOrder) != 0 means inverse order (descending usually)
void CMuleListCtrl::UpdateSortHistory(LPARAM dwNewOrder)
{
	// delete all history for the item, for both direct and inverse order
	for (POSITION pos = m_liSortHistory.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		if (LOWORD(m_liSortHistory.GetNext(pos)) == LOWORD(dwNewOrder))
			m_liSortHistory.RemoveAt(pos2);
	}
	m_liSortHistory.AddHead((LONG)dwNewOrder);
	// limit it to MAX_SORTORDERHISTORY entries for now, just for performance
	if (m_liSortHistory.GetCount() > MAX_SORTORDERHISTORY)
		m_liSortHistory.RemoveTail();
}

LPARAM CMuleListCtrl::GetNextSortOrder(LPARAM iCurrentSortOrder) const
{
	POSITION pos = m_liSortHistory.Find((LONG)iCurrentSortOrder);
	if (pos) {
		m_liSortHistory.GetNext(pos);
		if (pos)
			return m_liSortHistory.GetAt(pos);
	}
	//ASSERT(0); // the current one not found - shouldn't happen
	return -1; // there are no more stored sort orders
}

CMuleListCtrl::EUpdateMode CMuleListCtrl::SetUpdateMode(EUpdateMode eUpdateMode)
{
	EUpdateMode eCurUpdateMode = m_eUpdateMode;
	m_eUpdateMode = eUpdateMode;
	return eCurUpdateMode;
}

void CMuleListCtrl::OnLvnGetInfoTip(LPNMHDR pNMHDR, LRESULT *pResult)
{
	// NOTE: Using 'Info Tips' for owner drawn list view controls (like almost all instances
	// of the CMuleListCtrl) gives potentially *wrong* results. One may and will experience
	// several situations where a tooltip should be shown and none will be shown. This is
	// because the Windows list view control code does not know anything about what the
	// owner drawn list view control was actually drawing. So, the Windows list view control
	// code is just *assuming* that the owner drawn list view control instance is using the
	// same drawing metrics as the Windows control. Because our owner drawn list view controls
	// almost always draw an additional icon before the actual item text and because the
	// Windows control does not know that, the calculations performed by the Windows control
	// regarding folded/unfolded items are in couple of cases wrong. E.g. because the Windows
	// control does not know about the additional icon and thus about the reduced space used
	// for drawing the item text, we may show folded item texts while the Windows control is
	// still assuming that we show the full text -> thus we will not receive a precomputed
	// info tip which contains the unfolded item text. Result: We would have to implement
	// our own info tip processing.
	LPNMLVGETINFOTIP pGetInfoTip = reinterpret_cast<LPNMLVGETINFOTIP>(pNMHDR);
	LVHITTESTINFO hti;
	if (pGetInfoTip->iSubItem == 0 && ::GetCursorPos(&hti.pt)) {
		ScreenToClient(&hti.pt);
		if (SubItemHitTest(&hti) < 0 || hti.iItem != pGetInfoTip->iItem || hti.iSubItem != 0) {
			// Don't show the default label tip for the main item, if the mouse is not over
			// the main item.
			if ((pGetInfoTip->dwFlags & LVGIT_UNFOLDED) == 0 && pGetInfoTip->cchTextMax > 0 && pGetInfoTip->pszText[0] != _T('\0')) {
				// the info tip text is explicitly set to empty, the list view control may
				pGetInfoTip->pszText[0] = _T('\0');
			}
			return;
		}
	}
	*pResult = 0;
}

int CMuleListCtrl::InsertColumn(int nCol, LPCTSTR lpszColumnHeading, int nFormat, int nWidth, int nSubItem, bool bHiddenByDefault)
{
	if (bHiddenByDefault)
		m_liDefaultHiddenColumns.AddTail(nCol);
	if (nCol >= 0) {
		const size_t uColumn = static_cast<size_t>(nCol);
		if (m_defaultColumnWidths.size() <= uColumn)
			m_defaultColumnWidths.resize(uColumn + 1, 0);
		m_defaultColumnWidths[uColumn] = nWidth;
	}
	return CListCtrl::InsertColumn(nCol, lpszColumnHeading, nFormat, nWidth, nSubItem);
}
