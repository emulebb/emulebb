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
#include "TreeOptionsCtrlEx.h"
#include "UserMsgs.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


BEGIN_MESSAGE_MAP(CTreeOptionsCtrlEx, CTreeOptionsCtrl)
	ON_WM_DESTROY()
END_MESSAGE_MAP()

CTreeOptionsCtrlEx::CTreeOptionsCtrlEx(UINT uImageListColorFlags)
{
	m_uImageListColorFlags = uImageListColorFlags;
	//SetAutoSelect(TRUE);	// does not work!?
	SetToggleOverIconOnly(TRUE);
}

void CTreeOptionsCtrlEx::HandleCheckBox(HTREEITEM hItem, BOOL bCheck)
{
	//Turn of redraw to Q all the changes we're going to make here
	SetRedraw(FALSE);

	//Toggle the state
	BOOL bOldState;
	GetCheckBox(hItem, bOldState);
	VERIFY(SetCheckBox(hItem, !bCheck));
	if (static_cast<bool>(bOldState) != static_cast<bool>(!bCheck))
		NotifyParent(BN_CLICKED, hItem);

	//If the item has children, then iterate through them and for all items
	//which are check boxes set their state to be the same as the parent
	for (HTREEITEM hChild = GetNextItem(hItem, TVGN_CHILD); hChild; hChild = GetNextItem(hChild, TVGN_NEXT))
		if (IsCheckBox(hChild)) {
			BOOL bThisChecked;
			GetCheckBox(hChild, bThisChecked);
			SetCheckBox(hChild, !bCheck);
			if (static_cast<bool>(bThisChecked) != static_cast<bool>(!bCheck))
				NotifyParent(BN_CLICKED, hChild);
		}

	//Get the parent item and if it is a checkbox, then iterate through
	//all its children and if all the checkboxes are checked, then also
	//automatically check the parent. If no checkboxes are checked, then
	//also automatically uncheck the parent.
	HTREEITEM hParent = GetNextItem(hItem, TVGN_PARENT);
	UpdateCheckBoxGroup(hParent);

	//Reset the redraw flag
	SetRedraw(TRUE);
}

void CTreeOptionsCtrlEx::UpdateCheckBoxGroup(HTREEITEM hItem)
{
	SetRedraw(FALSE);

	//Iterate through all children and if all the checkboxes are checked, then also
	//automatically check the item. If no checkboxes are checked, then
	//also automatically uncheck the item.
	HTREEITEM hParent = hItem;
	if (hParent && IsCheckBox(hParent)) {
		bool bNoCheckBoxesChecked = true;
		bool bAllCheckBoxesChecked = true;
		for (HTREEITEM hChild = GetNextItem(hParent, TVGN_CHILD); hChild; hChild = GetNextItem(hChild, TVGN_NEXT))
			if (IsCheckBox(hChild)) {
				BOOL bThisChecked;
				VERIFY(GetCheckBox(hChild, bThisChecked));
				bNoCheckBoxesChecked &= static_cast<bool>(!bThisChecked);
				bAllCheckBoxesChecked &= static_cast<bool>(bThisChecked);
			}

		if (bNoCheckBoxesChecked) {
			BOOL bOldState;
			GetCheckBox(hParent, bOldState);
			SetCheckBox(hParent, FALSE);
			if (bOldState)
				NotifyParent(BN_CLICKED, hParent);
		} else if (bAllCheckBoxesChecked) {
			BOOL bOldState;
			GetCheckBox(hParent, bOldState);
			SetCheckBox(hParent, FALSE); //gets rid of the semi state
			SetCheckBox(hParent, TRUE);
			if (!bOldState)
				NotifyParent(BN_CLICKED, hParent);
		} else {
			BOOL bEnable;
			VERIFY(GetCheckBoxEnable(hParent, bEnable));
			SetSemiCheckBox(hParent, bEnable);
		}
	}

	//Reset the redraw flag
	SetRedraw(TRUE);
}

BOOL CTreeOptionsCtrlEx::SetRadioButton(HTREEITEM hParent, int nIndex)
{
	//Validate our parameters
	ASSERT(IsGroup(hParent)); //Parent item must be a group item

	//Turn of redraw to Q all the changes we're going to make here
	SetRedraw(FALSE);

	int i = 0;
	bool bCheckedSomeItem = false;
	//Iterate through the child items and turn on the specified one and turn off all the other ones
	//if we reach a non radio button then break out of the loop
	for (HTREEITEM hChild = GetNextItem(hParent, TVGN_CHILD); hChild && IsRadioButton(hChild); hChild = GetNextItem(hChild, TVGN_NEXT)) {
		if (i == nIndex) {
			//Turn this item on
			BOOL bOldState;
			GetRadioButton(hChild, bOldState);
			VERIFY(SetItemImage(hChild, 3, 3));
			bCheckedSomeItem = true;
			if (!bOldState)
				NotifyParent(BN_CLICKED, hChild);
		} else {
			BOOL bEnable;
			VERIFY(GetRadioButtonEnable(hChild, bEnable));

			//Turn this item off
			if (bEnable)
				VERIFY(SetItemImage(hChild, 2, 2));
			else
				VERIFY(SetItemImage(hChild, 4, 4));
		}

		++i;
	}
	ASSERT(bCheckedSomeItem); //You specified an index which does not exist

	//Reset the redraw flag
	SetRedraw(TRUE);

	return TRUE;
}

BOOL CTreeOptionsCtrlEx::SetRadioButton(HTREEITEM hItem)
{
	//Validate our parameters
	ASSERT(IsRadioButton(hItem)); //Must be a radio item to check it

	//Iterate through the sibling items and turn them all off except this one
	HTREEITEM hParent = GetNextItem(hItem, TVGN_PARENT);
	ASSERT(IsGroup(hParent)); //Parent item must be a group item

	//Turn of redraw to Q all the changes we're going to make here
	SetRedraw(FALSE);

	//Iterate through the child items and turn on the specified one and turn off all the other ones
	//if we reach a non radio button then break out of the loop
	for (HTREEITEM hChild = GetNextItem(hParent, TVGN_CHILD); hChild && IsRadioButton(hChild); hChild = GetNextItem(hChild, TVGN_NEXT))
		if (hChild == hItem) {
			//Turn this item on
			BOOL bOldState;
			GetRadioButton(hChild, bOldState);
			VERIFY(SetItemImage(hChild, 3, 3));
			if (!bOldState)
				NotifyParent(BN_CLICKED, hChild);
		} else {
			BOOL bEnable;
			VERIFY(GetRadioButtonEnable(hChild, bEnable));

			//Turn this item off
			if (bEnable)
				VERIFY(SetItemImage(hChild, 2, 2));
			else
				VERIFY(SetItemImage(hChild, 6, 6));
		}

	//Reset the redraw flag
	SetRedraw(TRUE);

	return TRUE;
}

BOOL CTreeOptionsCtrlEx::NotifyParent(UINT uCode, HTREEITEM hItem)
{
	CWnd *pWnd = GetParent();
	if (!pWnd)
		return FALSE;

	TREEOPTSCTRLNOTIFY ton;
	ton.nmhdr.hwndFrom = m_hWnd;
	ton.nmhdr.idFrom = ::GetWindowLongPtr(m_hWnd, GWLP_ID);
	ton.nmhdr.code = uCode;
	ton.hItem = hItem;
	return pWnd->SendMessage(UM_TREEOPTSCTRL_NOTIFY, ::GetWindowLongPtr(m_hWnd, GWLP_ID), (LPARAM)&ton) != 0;
}

void CTreeOptionsCtrlEx::OnCreateImageList()
{
	CDC *pDCScreen = CDC::FromHandle(::GetDC(HWND_DESKTOP)); // explicitly use screen-dc, for proper RTL support
	if (pDCScreen) {
		static const int iBmpWidth = 16;
		static const int iBmpHeight = 16;
		static const int iBitmaps = 13;
		CBitmap bmpControls;
		if (bmpControls.CreateCompatibleBitmap(pDCScreen, iBmpWidth * iBitmaps, iBmpHeight)) {
			if (m_ilTree.Create(iBmpWidth, iBmpHeight, m_uImageListColorFlags | ILC_MASK, 0, 1)) {
				CDC dcMem;
				if (dcMem.CreateCompatibleDC(pDCScreen)) {
					HTHEME hTheme = (::IsThemeActive() && ::IsAppThemed()) ? ::OpenThemeData(NULL, L"BUTTON") : NULL;
					CBitmap *pOldBmp = dcMem.SelectObject(&bmpControls);
					dcMem.FillSolidRect(0, 0, iBmpWidth * iBitmaps, iBmpHeight, ::GetSysColor(COLOR_WINDOW));

					int iCtrlWidth = iBmpWidth - 3;
					int iCtrlHeight = iBmpHeight - 3;
					int iCtrlLeft = (iBmpWidth - iCtrlWidth) / 2;
					int iCtrlTop = (iBmpHeight - iCtrlHeight) / 2;

					// checkbox
					CRect rcBmp(0, 0, iBmpWidth, iBmpHeight);
					CRect rcCtrl(iCtrlLeft, iCtrlTop, iCtrlLeft + iCtrlWidth, iCtrlTop + iCtrlHeight);
					if (hTheme) {
						::DrawThemeBackground(hTheme, dcMem, BP_CHECKBOX, CBS_UNCHECKEDNORMAL, &rcCtrl, NULL);
						::DrawThemeEdge(hTheme, dcMem, BP_CHECKBOX, CBS_UNCHECKEDNORMAL, &rcCtrl, 0, 0, NULL);
					} else
						dcMem.DrawFrameControl(&rcCtrl, DFC_BUTTON, DFCS_BUTTONCHECK | DFCS_FLAT);

					// checkbox checked
					rcCtrl.MoveToX(iCtrlLeft + iBmpWidth * 1);
					if (hTheme) {
						::DrawThemeBackground(hTheme, dcMem, BP_CHECKBOX, CBS_CHECKEDNORMAL, &rcCtrl, NULL);
						::DrawThemeEdge(hTheme, dcMem, BP_CHECKBOX, CBS_CHECKEDNORMAL, &rcCtrl, 0, 0, NULL);
					} else
						dcMem.DrawFrameControl(&rcCtrl, DFC_BUTTON, DFCS_BUTTONCHECK | DFCS_CHECKED | DFCS_FLAT);

					// radio
					rcCtrl.MoveToX(iCtrlLeft + iBmpWidth * 2);
					if (hTheme) {
						::DrawThemeBackground(hTheme, dcMem, BP_RADIOBUTTON, RBS_UNCHECKEDNORMAL, &rcCtrl, NULL);
						::DrawThemeEdge(hTheme, dcMem, BP_CHECKBOX, RBS_UNCHECKEDNORMAL, &rcCtrl, 0, 0, NULL);
					} else
						dcMem.DrawFrameControl(&rcCtrl, DFC_BUTTON, DFCS_BUTTONRADIO | DFCS_FLAT);

					// radio checked
					rcCtrl.MoveToX(iCtrlLeft + iBmpWidth * 3);
					if (hTheme) {
						::DrawThemeBackground(hTheme, dcMem, BP_RADIOBUTTON, RBS_CHECKEDNORMAL, &rcCtrl, NULL);
						::DrawThemeEdge(hTheme, dcMem, BP_CHECKBOX, RBS_CHECKEDNORMAL, &rcCtrl, 0, 0, NULL);
					} else
						dcMem.DrawFrameControl(&rcCtrl, DFC_BUTTON, DFCS_BUTTONRADIO | DFCS_CHECKED | DFCS_FLAT);

					// checkbox disabled
					rcCtrl.MoveToX(iCtrlLeft + iBmpWidth * 4);
					if (hTheme) {
						::DrawThemeBackground(hTheme, dcMem, BP_CHECKBOX, CBS_UNCHECKEDDISABLED, &rcCtrl, NULL);
						::DrawThemeEdge(hTheme, dcMem, BP_CHECKBOX, CBS_UNCHECKEDDISABLED, &rcCtrl, 0, 0, NULL);
					} else
						dcMem.DrawFrameControl(&rcCtrl, DFC_BUTTON, DFCS_BUTTONCHECK | DFCS_INACTIVE | DFCS_FLAT);

					// checkbox checked disabled
					rcCtrl.MoveToX(iCtrlLeft + iBmpWidth * 5);
					if (hTheme) {
						::DrawThemeBackground(hTheme, dcMem, BP_CHECKBOX, CBS_CHECKEDDISABLED, &rcCtrl, NULL);
						::DrawThemeEdge(hTheme, dcMem, BP_CHECKBOX, CBS_CHECKEDDISABLED, &rcCtrl, 0, 0, NULL);
					} else
						dcMem.DrawFrameControl(&rcCtrl, DFC_BUTTON, DFCS_BUTTONCHECK | DFCS_CHECKED | DFCS_INACTIVE | DFCS_FLAT);

					// radio disabled
					rcCtrl.MoveToX(iCtrlLeft + iBmpWidth * 6);
					if (hTheme) {
						::DrawThemeBackground(hTheme, dcMem, BP_RADIOBUTTON, RBS_UNCHECKEDDISABLED, &rcCtrl, NULL);
						::DrawThemeEdge(hTheme, dcMem, BP_CHECKBOX, RBS_UNCHECKEDDISABLED, &rcCtrl, 0, 0, NULL);
					} else
						dcMem.DrawFrameControl(&rcCtrl, DFC_BUTTON, DFCS_BUTTONRADIO | DFCS_INACTIVE | DFCS_FLAT);

					// radio checked disabled
					rcCtrl.MoveToX(iCtrlLeft + iBmpWidth * 7);
					if (hTheme) {
						::DrawThemeBackground(hTheme, dcMem, BP_RADIOBUTTON, RBS_CHECKEDDISABLED, &rcCtrl, NULL);
						::DrawThemeEdge(hTheme, dcMem, BP_CHECKBOX, RBS_CHECKEDDISABLED, &rcCtrl, 0, 0, NULL);
					} else
						dcMem.DrawFrameControl(&rcCtrl, DFC_BUTTON, DFCS_BUTTONRADIO | DFCS_CHECKED | DFCS_INACTIVE | DFCS_FLAT);

					// checkbox checked tri-state
					rcCtrl.MoveToX(iCtrlLeft + iBmpWidth * 8);
					if (hTheme) {
						::DrawThemeBackground(hTheme, dcMem, BP_CHECKBOX, CBS_MIXEDNORMAL, &rcCtrl, NULL);
						::DrawThemeEdge(hTheme, dcMem, BP_CHECKBOX, CBS_MIXEDNORMAL, &rcCtrl, 0, 0, NULL);
					} else
						dcMem.DrawFrameControl(&rcCtrl, DFC_BUTTON, DFCS_BUTTON3STATE | DFCS_CHECKED | DFCS_FLAT);

					// checkbox checked tri-state disabled
					rcCtrl.MoveToX(iCtrlLeft + iBmpWidth * 9);
					if (hTheme) {
						::DrawThemeBackground(hTheme, dcMem, BP_CHECKBOX, CBS_MIXEDDISABLED, &rcCtrl, NULL);
						::DrawThemeEdge(hTheme, dcMem, BP_CHECKBOX, CBS_MIXEDDISABLED, &rcCtrl, 0, 0, NULL);
					} else
						dcMem.DrawFrameControl(&rcCtrl, DFC_BUTTON, DFCS_BUTTON3STATE | DFCS_CHECKED | DFCS_INACTIVE | DFCS_FLAT);

					ASSERT(TREEOPTSCTRLIMG_EDIT == 11);
					rcBmp.MoveToX(iBmpWidth * 11);
					rcCtrl.MoveToX(iCtrlLeft + iBmpWidth * 11);
					CFont font;
					if (font.CreatePointFont(10, _T("Courier"))) {
						CFont *pOldFont = dcMem.SelectObject(&font);
						dcMem.TextOut(rcCtrl.left + 2, rcCtrl.top, _T("I"));
						dcMem.SelectObject(pOldFont);
					}
					RECT rcEdge(rcBmp);
					rcEdge.top += 1;
					rcEdge.bottom -= 1;
					dcMem.DrawEdge(&rcEdge, EDGE_ETCHED, BF_RECT);

					if (hTheme)
						::CloseThemeData(hTheme);

					hTheme = (::IsThemeActive() && ::IsAppThemed()) ? ::OpenThemeData(NULL, L"COMBOBOX") : NULL;
					rcCtrl.MoveToX(iCtrlLeft + iBmpWidth * 12);
					if (hTheme) {
						::DrawThemeBackground(hTheme, dcMem, CP_DROPDOWNBUTTON, CBXS_NORMAL, &rcCtrl, NULL);
						::DrawThemeEdge(hTheme, dcMem, CP_DROPDOWNBUTTON, CBXS_NORMAL, &rcCtrl, 0, 0, NULL);
					} else
						dcMem.DrawFrameControl(&rcCtrl, DFC_SCROLL, DFCS_SCROLLCOMBOBOX | DFCS_FLAT);

					dcMem.SelectObject(pOldBmp);
					m_ilTree.Add(&bmpControls, RGB(255, 0, 255));
					if (hTheme)
						::CloseThemeData(hTheme);
				}
			}
		}
		::ReleaseDC(HWND_DESKTOP, *pDCScreen);
	}
}

void CTreeOptionsCtrlEx::SetEditLabel(HTREEITEM hItem, const CString &rstrLabel)
{
	CString sLabel(rstrLabel);
	const CString &sSeparator = GetTextSeparator();
	if (!sSeparator.IsEmpty()) {
		// Avoid embedding the exact value separator in labels; value parsing depends on it.
		CString sSafeSeparator(sSeparator);
		const int nLast = sSafeSeparator.GetLength() - 1;
		if (nLast >= 0) {
			if (sSafeSeparator[nLast] == _T(' '))
				sSafeSeparator.SetAt(nLast, (TCHAR)0x00A0);
			else
				sSafeSeparator += (TCHAR)0x00A0;
		}
		sLabel.Replace(sSeparator, sSafeSeparator);
	}

	CString sItemText(GetItemText(hItem));
	int nSeparator = sItemText.Find(sSeparator);
	sItemText.Delete(0, nSeparator < 0 ? INT_MAX : nSeparator);
	sItemText.Insert(0, sLabel);
	SetItemText(hItem, sItemText);
}

void CTreeOptionsCtrlEx::OnDestroy()
{
	CTreeOptionsCtrl::OnDestroy();
	m_ilTree.DeleteImageList();
}


//////////////////////////////////////////////////////////////////////////////
// DDX_...

void EditTextFloatFormat(CDataExchange *pDX, int nIDC, HTREEITEM hItem, void *pData, double value, int nSizeGcvt)
{
	ASSERT(pData != NULL);

	HWND hWndCtrl = pDX->PrepareEditCtrl(nIDC);
	ASSERT(hWndCtrl != NULL);
	CTreeOptionsCtrl *pCtrlTreeOptions = static_cast<CTreeOptionsCtrl*>(CWnd::FromHandlePermanent(hWndCtrl));
	ASSERT(pCtrlTreeOptions);
	ASSERT(pCtrlTreeOptions->IsKindOf(RUNTIME_CLASS(CTreeOptionsCtrl)));

	if (pDX->m_bSaveAndValidate) {
		CString sText(pCtrlTreeOptions->GetEditText(hItem));
		double d;
		if (_stscanf_s(sText, _T("%lf"), &d) != 1) {
			AfxMessageBox(AFX_IDP_PARSE_REAL);
			pDX->Fail();	// throws exception
		}
		if (nSizeGcvt == FLT_DIG)
			*((float*)pData) = (float)d;
		else
			*((double*)pData) = d;
	} else {
		TCHAR szBuffer[400];
		_sntprintf_s(szBuffer, _countof(szBuffer), _TRUNCATE, _T("%.*g"), nSizeGcvt, value);
		pCtrlTreeOptions->SetEditText(hItem, szBuffer);
	}
}

#pragma warning(push)
#pragma warning(disable:4774)
// only supports windows output formats - no floating point
void EditTextWithFormat(CDataExchange *pDX, int nIDC, HTREEITEM hItem, LPCTSTR lpszFormat, UINT nIDPrompt, ...)
{
	va_list pData;
	va_start(pData, nIDPrompt);

	HWND hWndCtrl = pDX->PrepareEditCtrl(nIDC);
	ASSERT(hWndCtrl != NULL);
	CTreeOptionsCtrl *pCtrlTreeOptions = static_cast<CTreeOptionsCtrl*>(CWnd::FromHandlePermanent(hWndCtrl));
	ASSERT(pCtrlTreeOptions);
	ASSERT(pCtrlTreeOptions->IsKindOf(RUNTIME_CLASS(CTreeOptionsCtrl)));

	if (pDX->m_bSaveAndValidate) {
		void *pResult = va_arg(pData, void*);
		// the following works for %d, %u, %ld, %lu
		CString sText(pCtrlTreeOptions->GetEditText(hItem));
		if (_stscanf_s(sText, lpszFormat, pResult) != 1) {
			AfxMessageBox(nIDPrompt);
			pDX->Fail();	// throws exception
		}
	} else {
		TCHAR szT[64];
		_vsntprintf_s(szT, _countof(szT), _TRUNCATE, lpszFormat, pData);
		// does not support floating point numbers - see dlgfloat.cpp
		pCtrlTreeOptions->SetEditText(hItem, szT);
	}

	va_end(pData);
}
#pragma warning(pop)

void DDX_TreeCheck(CDataExchange *pDX, int nIDC, HTREEITEM hItem, bool &bCheck)
{
	BOOL biBool = bCheck;
	DDX_TreeCheck(pDX, nIDC, hItem, biBool);
	bCheck = biBool != FALSE;
}

void DDX_Text(CDataExchange *pDX, int nIDC, HTREEITEM hItem, int &value)
{
	if (pDX->m_bSaveAndValidate)
		EditTextWithFormat(pDX, nIDC, hItem, _T("%d"), AFX_IDP_PARSE_INT, &value);
	else
		EditTextWithFormat(pDX, nIDC, hItem, _T("%d"), AFX_IDP_PARSE_INT, value);
}

void DDX_Text(CDataExchange *pDX, int nIDC, HTREEITEM hItem, UINT &value)
{
	if (pDX->m_bSaveAndValidate)
		EditTextWithFormat(pDX, nIDC, hItem, _T("%u"), AFX_IDP_PARSE_UINT, &value);
	else
		EditTextWithFormat(pDX, nIDC, hItem, _T("%u"), AFX_IDP_PARSE_UINT, value);
}

void DDX_Text(CDataExchange *pDX, int nIDC, HTREEITEM hItem, long &value)
{
	if (pDX->m_bSaveAndValidate)
		EditTextWithFormat(pDX, nIDC, hItem, _T("%ld"), AFX_IDP_PARSE_INT, &value);
	else
		EditTextWithFormat(pDX, nIDC, hItem, _T("%ld"), AFX_IDP_PARSE_INT, value);
}

void DDX_Text(CDataExchange *pDX, int nIDC, HTREEITEM hItem, DWORD &value)
{
	if (pDX->m_bSaveAndValidate)
		EditTextWithFormat(pDX, nIDC, hItem, _T("%lu"), AFX_IDP_PARSE_UINT, &value);
	else
		EditTextWithFormat(pDX, nIDC, hItem, _T("%lu"), AFX_IDP_PARSE_UINT, value);
}

void DDX_Text(CDataExchange *pDX, int nIDC, HTREEITEM hItem, float &value)
{
	EditTextFloatFormat(pDX, nIDC, hItem, &value, value, FLT_DIG);
}

void DDX_Text(CDataExchange *pDX, int nIDC, HTREEITEM hItem, double &value)
{
	EditTextFloatFormat(pDX, nIDC, hItem, &value, value, DBL_DIG);
}

void DDX_Text(CDataExchange *pDX, int nIDC, HTREEITEM hItem, CString &sText)
{
	HWND hWndCtrl = pDX->PrepareCtrl(nIDC);
	CTreeOptionsCtrl *pCtrlTreeOptions = static_cast<CTreeOptionsCtrl*>(CWnd::FromHandlePermanent(hWndCtrl));
	ASSERT(pCtrlTreeOptions);
	ASSERT(pCtrlTreeOptions->IsKindOf(RUNTIME_CLASS(CTreeOptionsCtrl)));

	if (pDX->m_bSaveAndValidate)
		sText = pCtrlTreeOptions->GetEditText(hItem);
	else
		pCtrlTreeOptions->SetEditText(hItem, sText);
}


///////////////////////////////////////////////////////////////////////////////
// CNumTreeOptionsEdit

IMPLEMENT_DYNCREATE(CNumTreeOptionsEdit, CTreeOptionsEdit)

BEGIN_MESSAGE_MAP(CNumTreeOptionsEdit, CTreeOptionsEdit)
	ON_WM_CREATE()
	ON_CONTROL_REFLECT(EN_CHANGE, OnEnChange)
END_MESSAGE_MAP()

int CNumTreeOptionsEdit::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	m_bSelf = true;
	if (CTreeOptionsEdit::OnCreate(lpCreateStruct) == -1)
		return -1;
	m_bSelf = false;

	return 0;
}

void CNumTreeOptionsEdit::OnEnChange()
{
	if (!m_bSelf)
		static_cast<CTreeOptionsCtrlEx*>(m_pTreeCtrl)->NotifyParent(EN_CHANGE, m_hTreeCtrlItem);
}


///////////////////////////////////////////////////////////////////////////////
// CTreeOptionsEditEx

IMPLEMENT_DYNCREATE(CTreeOptionsEditEx, CTreeOptionsEdit)

BEGIN_MESSAGE_MAP(CTreeOptionsEditEx, CTreeOptionsEdit)
	ON_WM_CREATE()
	ON_CONTROL_REFLECT(EN_CHANGE, OnEnChange)
END_MESSAGE_MAP()

int CTreeOptionsEditEx::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	m_bSelf = true;
	if (CTreeOptionsEdit::OnCreate(lpCreateStruct) == -1)
		return -1;
	m_bSelf = false;

	return 0;
}

void CTreeOptionsEditEx::OnEnChange()
{
	if (!m_bSelf)
		static_cast<CTreeOptionsCtrlEx*>(m_pTreeCtrl)->NotifyParent(EN_CHANGE, m_hTreeCtrlItem);
}
