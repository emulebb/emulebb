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
#include <richole.h>
#include <share.h>
#include "emule.h"
#include "HTRichEditCtrl.h"
#include "OtherFunctions.h"
#include "Preferences.h"
#include "MenuCmds.h"
#include "Log.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


int CHTRichEditCtrl::sm_iSmileyClients = 0;
CComPtr<IStorage> CHTRichEditCtrl::sm_pIStorageSmileys;
CMapStringToPtr CHTRichEditCtrl::sm_aSmileyBitmaps;

IMPLEMENT_DYNAMIC(CHTRichEditCtrl, CRichEditCtrl)

BEGIN_MESSAGE_MAP(CHTRichEditCtrl, CRichEditCtrl)
	ON_WM_CONTEXTMENU()
	ON_WM_KEYDOWN()
	ON_CONTROL_REFLECT(EN_ERRSPACE, OnEnErrspace)
	ON_CONTROL_REFLECT(EN_MAXTEXT, OnEnMaxtext)
	ON_NOTIFY_REFLECT_EX(EN_LINK, OnEnLink)
	ON_WM_CREATE()
	ON_WM_SYSCOLORCHANGE()
	ON_WM_SETCURSOR()
	ON_WM_SIZE()
END_MESSAGE_MAP()

CHTRichEditCtrl::CHTRichEditCtrl()
	: m_cfDefault()
	, m_hArrowCursor(::LoadCursor(NULL, IDC_ARROW))
	, m_crBackground(CLR_DEFAULT)
	, m_crDfltBackground(CLR_DEFAULT)
	, m_crForeground(CLR_DEFAULT)
	, m_crDfltForeground(CLR_DEFAULT)
	, m_iLimitText()
	, m_bAutoScroll(true)
	, m_bEnableSmileys()
	, m_bEnErrSpace()
	, m_bForceArrowCursor()
	, m_bNoPaint()
	, m_bRestoreFormat()
	, m_bDfltForeground()
	, m_bDfltBackground()
	, m_bRollingLogWindow()
	, m_bRollingWindowNeedsRebuild()
	, m_bUpdateBatchNeedsInvalidate()
	, m_bUpdateBatchOldNoPaint()
	, m_bUpdateBatchWasVisible()
	, m_iUpdateBatchDepth()
	, m_uRollingMaxEntries()
	, m_iRollingMaxLineChars()
	, m_iRollingPendingTrimChars()
{
	m_cfDefault.cbSize = (UINT)sizeof(CHARFORMAT2);
}

CHTRichEditCtrl::~CHTRichEditCtrl()
{
	EnableSmileys(false);
}

BOOL CHTRichEditCtrl::Create(DWORD dwStyle, const RECT &rect, CWnd *parent, UINT nID)
{
	return static_cast<CWnd*>(this)->Create(MSFTEDIT_CLASS, NULL, dwStyle, rect, parent, nID);
}

void CHTRichEditCtrl::Localize()
{
}

void CHTRichEditCtrl::Init(LPCTSTR pszTitle, LPCTSTR pszSkinKey)
{
	SetProfileSkinKey(pszSkinKey);
	SetTitle(pszTitle);

	VERIFY(SendMessage(EM_SETUNDOLIMIT, 0, 0) == 0);
	int iMaxLogBuff = thePrefs.GetMaxLogBuff();
	LimitText(iMaxLogBuff ? iMaxLogBuff : 128 * 1024);
	m_iLimitText = GetLimitText();

	VERIFY(GetSelectionCharFormat(m_cfDefault));

	// prevent the RE control to change the font height within single log lines (may happen with some Unicode chars)
	LRESULT dwLangOpts = SendMessage(EM_GETLANGOPTIONS);
	SendMessage(EM_SETLANGOPTIONS, 0, (WPARAM)(dwLangOpts & ~(IMF_AUTOFONT /*| IMF_AUTOFONTSIZEADJUST*/)));
	//SendMessage(EM_SETEDITSTYLE, SES_EMULATESYSEDIT, SES_EMULATESYSEDIT);
}

void CHTRichEditCtrl::EnableRollingLogWindow(const size_t uMaxEntries, const int iMaxLineChars)
{
	m_bRollingLogWindow = uMaxEntries > 0;
	m_uRollingMaxEntries = uMaxEntries;
	m_iRollingMaxLineChars = max(0, iMaxLineChars);

	if (!m_bRollingLogWindow)
		return;

	LimitText(RollingLogWindowSeams::BuildRichEditTextLimit(m_uRollingMaxEntries, m_iRollingMaxLineChars));
	m_iLimitText = GetLimitText();
}

void CHTRichEditCtrl::EnableSmileys(bool bEnable)
{
	if (bEnable) {
		if (!m_bEnableSmileys) {
			m_bEnableSmileys = true;
			++sm_iSmileyClients;
		}
	} else {
		if (m_bEnableSmileys) {
			m_bEnableSmileys = false;
			--sm_iSmileyClients;
			if (sm_iSmileyClients <= 0) {
				PurgeSmileyCaches();
				sm_iSmileyClients = 0;
			}
		}
	}
}

void CHTRichEditCtrl::PurgeSmileyCaches()
{
	CString strKey;
	for (POSITION pos = sm_aSmileyBitmaps.GetStartPosition(); pos != NULL;) {
		void *pValue;
		sm_aSmileyBitmaps.GetNextAssoc(pos, strKey, pValue);
		VERIFY(::DeleteObject((HBITMAP)pValue));
	}
	sm_aSmileyBitmaps.RemoveAll();
	sm_pIStorageSmileys.Release();
}

int CHTRichEditCtrl::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CRichEditCtrl::OnCreate(lpCreateStruct) == -1)
		return -1;
	Init(NULL);
	return 0;
}

LRESULT CHTRichEditCtrl::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_SHOWWINDOW:
		if (wParam != FALSE)
			EnsureRollingWindowMaterialized();
		break;
	case WM_ERASEBKGND:
		if (m_bNoPaint)
			return TRUE;
		break;
	case WM_PAINT:
		if (!m_bNoPaint)
			EnsureRollingWindowMaterialized();
		if (m_bNoPaint)
			return TRUE;
		break;
	}
	return CRichEditCtrl::WindowProc(message, wParam, lParam);
}

void CHTRichEditCtrl::OnSize(UINT nType, int cx, int cy)
{
	// Use the 'ScrollInfo' only, if there is a scrollbar available, otherwise we would
	// use a scrollinfo which points to the top and we would thus stay at the top.
	bool bAtEndOfScroll;
	SCROLLINFO si;
	si.cbSize = (UINT)sizeof si;
	si.fMask = SIF_ALL;
	if ((GetStyle() & WS_VSCROLL) && GetScrollInfo(SB_VERT, &si))
		bAtEndOfScroll = (si.nPos >= (int)(si.nMax - si.nPage));
	else
		bAtEndOfScroll = true;

	CRichEditCtrl::OnSize(nType, cx, cy);

	if (bAtEndOfScroll)
		ScrollToLastLine();
}

COLORREF GetLogLineColor(UINT eMsgType)
{
	switch (eMsgType) {
	case LOG_SUCCESS:
		return thePrefs.m_crLogSuccess;
	case LOG_ERROR:
		return thePrefs.m_crLogError;
	case LOG_WARNING:
		return thePrefs.m_crLogWarning;
	default:
		ASSERT(eMsgType == LOG_INFO);
	}
	return CLR_DEFAULT;
}

CString CHTRichEditCtrl::PrepareRollingLine(LPCTSTR pszMsg, const int iLen) const
{
	CString strLine((iLen == -1) ? pszMsg : CString(pszMsg, iLen));
	if (!m_bRollingLogWindow || m_iRollingMaxLineChars <= 0 || strLine.GetLength() <= m_iRollingMaxLineChars)
		return strLine;

	CString strLineEnding;
	if (strLine.GetLength() >= 2 && strLine.Right(2) == _T("\r\n")) {
		strLineEnding = _T("\r\n");
		strLine.Truncate(strLine.GetLength() - 2);
	} else if (!strLine.IsEmpty() && (strLine.Right(1) == _T("\r") || strLine.Right(1) == _T("\n"))) {
		strLineEnding = strLine.Right(1);
		strLine.Truncate(strLine.GetLength() - 1);
	}

	const int iMaxPayloadChars = max(0, m_iRollingMaxLineChars - strLineEnding.GetLength());
	if (strLine.GetLength() > iMaxPayloadChars)
		strLine.Truncate(iMaxPayloadChars);
	return strLine + strLineEnding;
}

void CHTRichEditCtrl::RemoveTextFromHead(const int iCharsToRemove)
{
	if (m_hWnd == NULL || iCharsToRemove <= 0)
		return;

	BOOL bIsVisible = IsWindowVisible();
	if (bIsVisible)
		SetRedraw(FALSE);
	const bool bOldNoPaint = m_bNoPaint;
	m_bNoPaint = true;

	const int iCurrentLength = GetWindowTextLength();
	if (iCharsToRemove >= iCurrentLength)
		SetWindowText(_T(""));
	else {
		SetSel(0, iCharsToRemove);
		ReplaceSel(_T(""));
	}

	m_bNoPaint = bOldNoPaint;
	if (bIsVisible && !m_bNoPaint) {
		SetRedraw();
		Invalidate();
	}
}

void CHTRichEditCtrl::TrimRollingEntries()
{
	if (!m_bRollingLogWindow || m_uRollingMaxEntries == 0)
		return;

	const int iCharsToRemove = RollingLogWindowSeams::ApplyTrimPlan(
		m_aRollingEntries,
		m_aRollingPendingEntries,
		m_uRollingMaxEntries,
		[](const RollingLogWindowSeams::SRollingLogEntry &rEntry) { return rEntry.strText.GetLength(); });

	if (iCharsToRemove <= 0)
		return;
	if (m_iUpdateBatchDepth > 0)
		m_iRollingPendingTrimChars = RollingLogWindowSeams::SaturatingAddChars(m_iRollingPendingTrimChars, iCharsToRemove);
	else if (m_hWnd != NULL && IsWindowVisible() && !m_bRollingWindowNeedsRebuild)
		RemoveTextFromHead(iCharsToRemove);
	else if (m_hWnd != NULL)
		m_bRollingWindowNeedsRebuild = true;
}

void CHTRichEditCtrl::EnsureRollingWindowMaterialized()
{
	if (m_bRollingLogWindow && m_bRollingWindowNeedsRebuild && m_hWnd != NULL && IsWindowVisible())
		RebuildRollingWindow();
}

void CHTRichEditCtrl::RebuildRollingWindow()
{
	if (m_hWnd == NULL)
		return;

	BOOL bIsVisible = IsWindowVisible();
	if (bIsVisible)
		SetRedraw(FALSE);
	const bool bOldNoPaint = m_bNoPaint;
	m_bNoPaint = true;

	SetWindowText(_T(""));
	m_bRollingWindowNeedsRebuild = false;
	AppendRollingEntriesToWindow(m_aRollingEntries);

	m_bNoPaint = bOldNoPaint;
	if (bIsVisible && !m_bNoPaint) {
		SetRedraw();
		Invalidate();
	}
}

void CHTRichEditCtrl::AppendRollingEntry(const CString &strText, const bool bTyped, const UINT uMsgType)
{
	RollingLogWindowSeams::SRollingLogEntry entry;
	entry.strText = strText;
	entry.bTyped = bTyped;
	entry.uMsgType = uMsgType & LOGMSGTYPEMASK;
	m_aRollingEntries.push_back(entry);

	if (m_hWnd != NULL && IsWindowVisible() && !m_bRollingWindowNeedsRebuild && m_iUpdateBatchDepth > 0)
		m_aRollingPendingEntries.push_back(entry);

	TrimRollingEntries();

	if (m_hWnd == NULL)
		return;
	if (!IsWindowVisible()) {
		m_bRollingWindowNeedsRebuild = true;
		ClearRollingPendingDisplay();
		return;
	}
	if (m_bRollingWindowNeedsRebuild) {
		if (m_iUpdateBatchDepth > 0)
			ClearRollingPendingDisplay();
		else
			RebuildRollingWindow();
		return;
	}
	if (m_iUpdateBatchDepth > 0)
		return;

	FlushBuffer();
	if (bTyped)
		AddLine(strText, strText.GetLength(), false, GetLogLineColor(entry.uMsgType));
	else
		AddLine(strText, strText.GetLength());
}

void CHTRichEditCtrl::BeginUpdateBatch()
{
	if (++m_iUpdateBatchDepth != 1)
		return;

	m_bUpdateBatchNeedsInvalidate = false;
	m_bUpdateBatchOldNoPaint = m_bNoPaint;
	// Startup progress pumping can dispatch timer/log work while CemuleDlg already
	// exists but the server log child controls have not created their HWNDs yet.
	// Keep the Begin/End depth balanced so queued log entries still take the
	// buffered path, but avoid MFC CWnd visibility helpers until the HWND is live:
	// debug MFC asserts on those helpers and release builds would still be making
	// an invalid window-state query.
	m_bUpdateBatchWasVisible = ::IsWindow(m_hWnd) ? ::IsWindowVisible(m_hWnd) : FALSE;
	m_bNoPaint = true;
	if (m_bUpdateBatchWasVisible)
		SetRedraw(FALSE);
}

void CHTRichEditCtrl::EndUpdateBatch()
{
	if (m_iUpdateBatchDepth <= 0) {
		ASSERT(FALSE);
		return;
	}

	if (--m_iUpdateBatchDepth != 0)
		return;

	const BOOL bWasVisible = m_bUpdateBatchWasVisible;
	const bool bNeedsInvalidate = m_bUpdateBatchNeedsInvalidate;
	if (m_bRollingLogWindow)
		FlushRollingBatchUpdate();
	m_bNoPaint = m_bUpdateBatchOldNoPaint;
	m_bUpdateBatchWasVisible = FALSE;
	m_bUpdateBatchNeedsInvalidate = false;
	if (bWasVisible && !m_bNoPaint && ::IsWindow(m_hWnd)) {
		SetRedraw();
		if (bNeedsInvalidate)
			Invalidate();
	}
}

void CHTRichEditCtrl::ClearRollingPendingDisplay()
{
	m_aRollingPendingEntries.clear();
	m_iRollingPendingTrimChars = 0;
}

void CHTRichEditCtrl::FlushRollingBatchUpdate()
{
	if (m_hWnd == NULL) {
		ClearRollingPendingDisplay();
		return;
	}
	if (!IsWindowVisible()) {
		m_bRollingWindowNeedsRebuild = true;
		ClearRollingPendingDisplay();
		return;
	}
	if (m_bRollingWindowNeedsRebuild) {
		ClearRollingPendingDisplay();
		RebuildRollingWindow();
		m_bUpdateBatchNeedsInvalidate = true;
		return;
	}

	if (m_iRollingPendingTrimChars > 0) {
		RemoveTextFromHead(m_iRollingPendingTrimChars);
		m_bUpdateBatchNeedsInvalidate = true;
	}
	m_iRollingPendingTrimChars = 0;

	if (m_aRollingPendingEntries.empty())
		return;

	int iPendingChars = 0;
	for (const RollingLogWindowSeams::SRollingLogEntry &rEntry : m_aRollingPendingEntries)
		iPendingChars = RollingLogWindowSeams::SaturatingAddChars(iPendingChars, rEntry.strText.GetLength());
	if (GetWindowTextLength() > m_iLimitText - iPendingChars) {
		ClearRollingPendingDisplay();
		RebuildRollingWindow();
		m_bUpdateBatchNeedsInvalidate = true;
		return;
	}

	AppendRollingEntriesToWindow(m_aRollingPendingEntries);
	m_aRollingPendingEntries.clear();
	m_bUpdateBatchNeedsInvalidate = true;
}

void CHTRichEditCtrl::AppendRollingEntriesToWindow(const std::deque<RollingLogWindowSeams::SRollingLogEntry> &rEntries)
{
	if (rEntries.empty() || m_hWnd == NULL)
		return;

	FlushBuffer();

	long lStartChar = 0;
	long lEndChar = 0;
	GetSel(lStartChar, lEndChar);
	bool bAutoAutoScroll = m_bAutoScroll;
	SCROLLINFO si;
	si.cbSize = (UINT)sizeof si;
	si.fMask = SIF_ALL;
	if ((GetStyle() & WS_VSCROLL) && GetScrollInfo(SB_VERT, &si))
		bAutoAutoScroll = (si.nPos >= (int)(si.nMax - si.nPage - 20));

	POINT ptScrollPos;
	SendMessage(EM_GETSCROLLPOS, 0, (LPARAM)&ptScrollPos);

	int iSize = GetWindowTextLength();
	for (const RollingLogWindowSeams::SRollingLogEntry &rEntry : rEntries) {
		const int iLen = rEntry.strText.GetLength();
		if (iLen <= 0)
			continue;
		SafeAddLine(
			iSize,
			rEntry.strText,
			iLen,
			lStartChar,
			lEndChar,
			false,
			rEntry.bTyped ? GetLogLineColor(rEntry.uMsgType & LOGMSGTYPEMASK) : CLR_DEFAULT,
			CLR_DEFAULT,
			0);
		iSize = GetWindowTextLength();
	}

	SetSel(lStartChar, lEndChar);
	if (bAutoAutoScroll)
		ScrollToLastLine();
	else
		SendMessage(EM_SETSCROLLPOS, 0, (LPARAM)&ptScrollPos);
}

void CHTRichEditCtrl::FlushBuffer()
{
	if (!m_astrBuff.IsEmpty()) { // flush buffer
		for (INT_PTR i = 0; i < m_astrBuff.GetCount(); ++i) {
			const CString &rstrLine(m_astrBuff[i]);
			if (!rstrLine.IsEmpty()) {
				if ((UINT)rstrLine[0] < 8)
					AddLine((LPCTSTR)rstrLine + 1, rstrLine.GetLength() - 1, false, GetLogLineColor((UINT)rstrLine[0]));
				else
					AddLine((LPCTSTR)rstrLine, rstrLine.GetLength());
			}
		}
		m_astrBuff.RemoveAll();
	}
}

void CHTRichEditCtrl::AddEntry(LPCTSTR pszMsg)
{
	CString strLine(pszMsg);
	strLine += _T('\n');
	if (m_bRollingLogWindow)
		AppendRollingEntry(strLine, false, LOG_INFO);
	else if (m_hWnd == NULL)
		m_astrBuff.Add(strLine);
	else {
		FlushBuffer();
		AddLine(strLine, strLine.GetLength());
	}
}

void CHTRichEditCtrl::Add(LPCTSTR pszMsg, int iLen)
{
	if (m_bRollingLogWindow)
		AppendRollingEntry(PrepareRollingLine(pszMsg, iLen), false, LOG_INFO);
	else if (m_hWnd == NULL)
		m_astrBuff.Add(CString(pszMsg));
	else {
		FlushBuffer();
		AddLine(pszMsg, iLen);
	}
}

void CHTRichEditCtrl::AddTyped(LPCTSTR pszMsg, int iLen, UINT eMsgType)
{
	if (m_bRollingLogWindow)
		AppendRollingEntry(PrepareRollingLine(pszMsg, iLen), true, eMsgType & LOGMSGTYPEMASK);
	else if (m_hWnd == NULL) {
		CString strLine((TCHAR)(eMsgType & LOGMSGTYPEMASK));
		m_astrBuff.Add(strLine + pszMsg);
	} else {
		FlushBuffer();
		AddLine(pszMsg, iLen, false, GetLogLineColor(eMsgType & LOGMSGTYPEMASK));
	}
}

void CHTRichEditCtrl::AddLine(LPCTSTR pszMsg, int iLen, bool bLink, COLORREF cr, COLORREF bk, DWORD mask)
{
	int iMsgLen = (iLen == -1) ? (int)_tcslen(pszMsg) : iLen;
	if (iMsgLen == 0)
		return;

	// Get Edit contents dimensions and cursor position
	long lStartChar, lEndChar;
	GetSel(lStartChar, lEndChar);
	int iSize = GetWindowTextLength();

	// Get Auto-AutoScroll state depending on scrollbar position
	bool bAutoAutoScroll = m_bAutoScroll;
	// Use the 'ScrollInfo' only, if there is a scrollbar available, otherwise we would
	// use a scrollinfo which points to the top and we would thus stay at the top.
	SCROLLINFO si;
	si.cbSize = (UINT)sizeof si;
	si.fMask = SIF_ALL;
	if ((GetStyle() & WS_VSCROLL) && GetScrollInfo(SB_VERT, &si)) {
		// use some threshold to determine if at end or "very near" at end, unfortunately
		// this is needed to get around richedit specific stuff. this threshold (pixels)
		// should somewhat reflect the font size used in the control.
		bAutoAutoScroll = (si.nPos >= (int)(si.nMax - si.nPage - 20));
	}

	// Reduce flicker by ignoring WM_PAINT
	const bool bBatchedUpdate = m_iUpdateBatchDepth > 0;
	const bool bOldNoPaint = m_bNoPaint;
	m_bNoPaint = true;
	BOOL bIsVisible = IsWindowVisible();
	if (bIsVisible && !bBatchedUpdate)
		SetRedraw(FALSE);

	// Remember where we are
	//int iFirstLine = !bAutoAutoScroll ? GetFirstVisibleLine() : 0;
	POINT ptScrollPos;
	SendMessage(EM_GETSCROLLPOS, 0, (LPARAM)&ptScrollPos);

	// Select at the end of text and replace the selection
	SafeAddLine(iSize, pszMsg, iMsgLen, lStartChar, lEndChar, bLink, cr, bk, mask);
	SetSel(lStartChar, lEndChar); // Restore previous selection

	if (bAutoAutoScroll)
		ScrollToLastLine();
	else {
		//LineScroll(iFirstLine - GetFirstVisibleLine());
		SendMessage(EM_SETSCROLLPOS, 0, (LPARAM)&ptScrollPos);
	}

	m_bNoPaint = bOldNoPaint;
	if (bIsVisible) {
		if (bBatchedUpdate)
			m_bUpdateBatchNeedsInvalidate = true;
		else if (!m_bNoPaint) {
			SetRedraw();
			Invalidate();
		}
	}
}

void CHTRichEditCtrl::OnEnErrspace()
{
	m_bEnErrSpace = true;
}

void CHTRichEditCtrl::OnEnMaxtext()
{
	m_bEnErrSpace = true;
}

void CHTRichEditCtrl::ScrollToLastLine(bool bForceLastLineAtBottom)
{
	EnsureRollingWindowMaterialized();
	if (bForceLastLineAtBottom) {
		int iFirstVisible = GetFirstVisibleLine();
		if (iFirstVisible > 0)
			LineScroll(-iFirstVisible);
	}
	SendMessage(WM_VSCROLL, SB_BOTTOM);
}

void CHTRichEditCtrl::ScrollToFirstLine()
{
	SendMessage(WM_VSCROLL, SB_TOP);
}

void CHTRichEditCtrl::AddString(int nPos, LPCTSTR pszString, bool bLink, COLORREF cr, COLORREF bk, DWORD mask)
{
	SetSel(nPos, nPos);
	if (bLink) {
		SETTEXTEX stx = SETTEXTEX{ST_SELECTION, GetACP()};
		SendMessage(EM_SETTEXTEX, (WPARAM)&stx, (LPARAM)(LPCSTR)CStringA(pszString));
		return;
	}

	bool bRestoreFormat = false;
	m_bEnErrSpace = false;
	if (cr != CLR_DEFAULT || bk != CLR_DEFAULT || (mask & (CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE)) != 0) {
		CHARFORMAT2 cf = {};
		cf.cbSize = (UINT)sizeof cf;
		GetSelectionCharFormat(cf);

		cf.dwMask |= CFM_COLOR;
		if (cr == CLR_DEFAULT) {
			if (m_bDfltForeground)
				cf.dwEffects |= CFE_AUTOCOLOR;
			else {
				ASSERT(m_crForeground != CLR_DEFAULT);
				cf.dwEffects &= ~CFE_AUTOCOLOR;
				cf.crTextColor = m_crForeground;
			}
		} else {
			cf.dwEffects &= ~CFE_AUTOCOLOR;
			cf.crTextColor = cr;
		}

		if (bk == CLR_DEFAULT) {
			// Background color is a little different than foreground color. Even if the
			// background color is set to a non-standard value (e.g. via skin), the
			// CFE_AUTOBACKCOLOR can be used to get this value. The usage of this flag instead
			// of the explicit color has the advantage that on a change of the skin or on a
			// change of the windows system colors, the control can display the colored
			// text in the new color scheme better (e.g. the text is at least readable and
			// not shown as black-on-black or white-on-white).
			//if (m_bDfltBackground) {
			cf.dwMask |= CFM_BACKCOLOR;
			cf.dwEffects |= CFE_AUTOBACKCOLOR;
		//}
		//else {
		//	ASSERT(m_crBackground != CLR_DEFAULT);
		//	cf.dwEffects &= ~CFE_AUTOBACKCOLOR;
		//	cf.crBackColor = m_crBackground;
		//}
		} else {
			cf.dwMask |= CFM_BACKCOLOR;
			cf.dwEffects &= ~CFE_AUTOBACKCOLOR;
			cf.crBackColor = bk;
		}

		cf.dwMask |= mask;

		if (mask & CFM_BOLD)
			cf.dwEffects |= CFE_BOLD;
		else if (cf.dwEffects & CFE_BOLD)
			cf.dwEffects ^= CFE_BOLD;

		if (mask & CFM_ITALIC)
			cf.dwEffects |= CFE_ITALIC;
		else if (cf.dwEffects & CFE_ITALIC)
			cf.dwEffects ^= CFE_ITALIC;

		if (mask & CFM_UNDERLINE)
			cf.dwEffects |= CFE_UNDERLINE;
		else if (cf.dwEffects & CFE_UNDERLINE)
			cf.dwEffects ^= CFE_UNDERLINE;

		SetSelectionCharFormat(cf);
		bRestoreFormat = true;
	} else if (m_bRestoreFormat)
		SetSelectionCharFormat(m_cfDefault);

	if (m_bEnableSmileys)
		AddSmileys(pszString);
	else
		ReplaceSel(pszString);

	m_bRestoreFormat = bRestoreFormat;
}

void CHTRichEditCtrl::SafeAddLine(int nPos, LPCTSTR pszLine, int iLen, long &lStartChar, long &lEndChar, bool bLink, COLORREF cr, COLORREF bk, DWORD mask)
{
	// EN_ERRSPACE and EN_MAXTEXT are not working for rich edit control (at least not same as for standard control),
	// need to explicitly check the log buffer limit.
	int iCurSize = nPos;
	if (iCurSize + iLen >= m_iLimitText) {
		bool bOldNoPaint = m_bNoPaint;
		m_bNoPaint = true;
		BOOL bIsVisible = IsWindowVisible();
		if (bIsVisible)
			SetRedraw(FALSE);

		while (iCurSize > 0 && iCurSize + iLen > m_iLimitText) {
			// delete 1st line
			int iLine0Len = LineLength(0) + 1; // add NL character
			SetSel(0, iLine0Len);
			ReplaceSel(_T(""));

			// update any possible available selection
			lStartChar -= iLine0Len;
			if (lStartChar < 0)
				lStartChar = 0;
			lEndChar -= iLine0Len;
			if (lEndChar < 0)
				lEndChar = 0;

			iCurSize = GetWindowTextLength();
		}

		m_bNoPaint = bOldNoPaint;
		if (bIsVisible && !m_bNoPaint) {
			SetRedraw();
			Invalidate();
		}
	}

	AddString(nPos, pszLine, bLink, cr, bk, mask);

	if (m_bEnErrSpace) {
		bool bOldNoPaint = m_bNoPaint;
		m_bNoPaint = true;
		BOOL bIsVisible = IsWindowVisible();
		if (bIsVisible)
			SetRedraw(FALSE);

		// remove the first line as long as we are capable of adding the new line
		int iSafetyCounter = 0;
		while (m_bEnErrSpace && iSafetyCounter < 10) {
			// delete the previous partially added line
			SetSel(nPos, -1);
			ReplaceSel(_T(""));

			// delete 1st line
			int iLine0Len = LineLength(0) + 1; // add NL character
			SetSel(0, iLine0Len);
			ReplaceSel(_T(""));

			// update any possible available selection
			lStartChar -= iLine0Len;
			if (lStartChar < 0)
				lStartChar = 0;
			lEndChar -= iLine0Len;
			if (lEndChar < 0)
				lEndChar = 0;

			// add the new line again
			nPos = GetWindowTextLength();
			AddString(nPos, pszLine, bLink, cr, bk, mask);

			if (m_bEnErrSpace && nPos == 0) {
				// should never happen: if we tried to add the line another time in the 1st line, there
				// will be no chance to add the line at all -> avoid endless loop!
				break;
			}
			++iSafetyCounter; // never ever create an endless loop!
		}
		m_bNoPaint = bOldNoPaint;
		if (bIsVisible && !m_bNoPaint) {
			SetRedraw();
			Invalidate();
		}
	}
}

void CHTRichEditCtrl::Reset()
{
	m_astrBuff.RemoveAll();
	m_aRollingEntries.clear();
	ClearRollingPendingDisplay();
	m_bRollingWindowNeedsRebuild = false;
	SetRedraw(FALSE);
	SetWindowText(_T(""));
	SetRedraw();
	Invalidate();
}

void CHTRichEditCtrl::OnContextMenu(CWnd*, CPoint point)
{
	EnsureRollingWindowMaterialized();
	if (!PointInClient(*this, point)) {
		Default();
		return;
	}

	long lSelStart, lSelEnd;
	GetSel(lSelStart, lSelEnd);

	// ugly, simulate a left click to get around the text cursor problem when right clicking.
	if (point.x != -1 && point.y != -1 && lSelStart == lSelEnd) {
		ASSERT(GetStyle() & ES_NOHIDESEL); // this works only if ES_NOHIDESEL is set
		CPoint ptMouse(point);
		ScreenToClient(&ptMouse);
		SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELONG(ptMouse.x, ptMouse.y));
		SendMessage(WM_LBUTTONUP, MK_LBUTTON, MAKELONG(ptMouse.x, ptMouse.y));
	}

	int iTextLen = GetWindowTextLength();

	CTitledMenu menu;
	menu.CreatePopupMenu();
	menu.AddMenuTitle(GetResString(IDS_LOGENTRY));
	menu.AppendMenu(MF_STRING | (lSelEnd > lSelStart ? MF_ENABLED : MF_GRAYED), MP_COPYSELECTED, GetResString(IDS_COPY));
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING | (iTextLen > 0 ? MF_ENABLED : MF_GRAYED), MP_SELECTALL, GetResString(IDS_SELECTALL));
	menu.AppendMenu(MF_STRING | (iTextLen > 0 ? MF_ENABLED : MF_GRAYED), MP_REMOVEALL, GetResString(IDS_PW_RESET));
	menu.AppendMenu(MF_STRING | (iTextLen > 0 ? MF_ENABLED : MF_GRAYED), MP_SAVELOG, GetResString(IDS_SAVELOG) + _T("..."));
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING | (m_bAutoScroll ? MF_CHECKED : MF_UNCHECKED), MP_AUTOSCROLL, GetResString(IDS_AUTOSCROLL));

	if (point.x == -1 && point.y == -1) {
		point.SetPoint(16, 32);
		ClientToScreen(&point);
	}

	// Cheap workaround for the "Text cursor is showing while context menu is open" glitch. It could be solved properly
	// with the RE's COM interface, but because the according messages are not routed with a unique control ID, it's not
	// really usable (e.g. if there are more RE controls in one window). Would to envelope each RE window to get a unique ID.
	m_bForceArrowCursor = true;
	menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
	m_bForceArrowCursor = false;

	VERIFY(menu.DestroyMenu());
}

BOOL CHTRichEditCtrl::OnCommand(WPARAM wParam, LPARAM)
{
	switch (wParam) {
	case MP_COPYSELECTED:
		CopySelectedItems();
		break;
	case MP_SELECTALL:
		SelectAllItems();
		break;
	case MP_REMOVEALL:
		Reset();
		break;
	case MP_SAVELOG:
		SaveLog();
		break;
	case MP_AUTOSCROLL:
		m_bAutoScroll = !m_bAutoScroll;
	}
	return TRUE;
}

bool CHTRichEditCtrl::SaveLog(LPCTSTR pszDefName)
{
	bool bResult = false;
	const CString &fname(pszDefName ? pszDefName : m_strTitle);
	CString strLogPath;
	if (DialogBrowseFile(strLogPath, _T("Log Files (*.log)|*.log||"), ValidFilename(fname), OFN_PATHMUSTEXIST, false, m_hWnd, NULL, _T("log"))) {
		FILE *fp = LongPathSeams::OpenFileStreamDenyWriteLongPath(strLogPath, _T("wb"));
		if (fp) {
			// write Unicode byte order mark 0xFEFF
			fputwc(u'\xFEFF', fp);

			const CString strText(GetAllLogEntries());
			fwrite(strText, sizeof(TCHAR), strText.GetLength(), fp);
			if (ferror(fp)) {
				CString strError;
				strError.Format(_T("Failed to write log file \"%s\" - %s"), (LPCTSTR)strLogPath, (LPCTSTR)GetCrtErrorString(errno));
				AfxMessageBox(strError, MB_ICONERROR);
			} else
				bResult = true;
			fclose(fp);
		} else {
			CString strError;
			strError.Format(_T("Failed to create log file \"%s\" - %s"), (LPCTSTR)strLogPath, (LPCTSTR)GetCrtErrorString(errno));
			AfxMessageBox(strError, MB_ICONERROR);
		}
	}
	return bResult;
}

CString CHTRichEditCtrl::GetLastLogEntry()
{
	CString strLog;
	if (m_bRollingLogWindow && !m_aRollingEntries.empty()) {
		strLog = m_aRollingEntries.back().strText;
		while (!strLog.IsEmpty() && (strLog.Right(1) == _T("\r") || strLog.Right(1) == _T("\n")))
			strLog.Truncate(strLog.GetLength() - 1);
		return strLog;
	}

	int iLastLine = GetLineCount() - 2;
	if (iLastLine >= 0) {
		GetLine(iLastLine, strLog.GetBuffer(1024), 1024);
		strLog.ReleaseBuffer();
	}
	return strLog;
}

CString CHTRichEditCtrl::GetAllLogEntries()
{
	if (m_bRollingLogWindow) {
		CString strLog;
		for (const RollingLogWindowSeams::SRollingLogEntry &rEntry : m_aRollingEntries)
			strLog += rEntry.strText;
		return strLog;
	}

	CString strLog;
	GetWindowText(strLog);
	return strLog;
}

void CHTRichEditCtrl::SelectAllItems()
{
	EnsureRollingWindowMaterialized();
	SetSel(0, -1);
}

void CHTRichEditCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (GetKeyState(VK_CONTROL) < 0) { //Ctrl is down
		//////////////////////////////////////////////////////////////////
		// Ctrl+A: Select all items
		if (nChar == 'A')
			SelectAllItems();
		else if (nChar == 'C')
		//////////////////////////////////////////////////////////////////
		// Ctrl+C: Copy listview items to clipboard
			CopySelectedItems();
	} else if (nChar == VK_ESCAPE)
		// don't minimize CHTRichEditCtrl
		return;

	CRichEditCtrl::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CHTRichEditCtrl::AppendText(LPCTSTR sText)
{
	LPCTSTR psz = sText;
	LPCTSTR pszStart = psz;
	while (*psz != _T('\0')) {
		bool bFoundScheme = false;
		for (unsigned i = 0; s_apszSchemes[i].pszScheme; ++i) {
			if (_tcsncmp(psz, s_apszSchemes[i].pszScheme, s_apszSchemes[i].iLen) == 0) {
				// output everything before the URL
				int iLen = (int)(psz - pszStart);
				if (iLen > 0)
					AddLine(pszStart, iLen);

				// search next space or EOL
				iLen = (int)_tcscspn(psz, _T(" \t\r\n"));
				if (iLen <= 0) {
					iLen = -1;
					psz += _tcslen(psz);
				} else
					psz += iLen;
				AddLine(psz, iLen, true);
				pszStart = psz;
				bFoundScheme = true;
				break;
			}
		}
		if (!bFoundScheme)
			psz = _tcsinc(psz);
	}

	if (*pszStart != _T('\0'))
		AddLine(pszStart, -1);
}

void CHTRichEditCtrl::AppendHyperLink(LPCTSTR pszText, LPCTSTR pszTitle, const CString &sCommand, LPCTSTR pszDirectory)
{
	UNREFERENCED_PARAMETER(pszTitle);
	UNREFERENCED_PARAMETER(pszDirectory);
	ASSERT(!pszTitle || !*pszTitle);
	ASSERT(!pszDirectory || !*pszDirectory);
	if (!pszText || !*pszText) //link only
		AddLine(sCommand, sCommand.GetLength(), true);
	else { //friendly name
		CString sRTF;
		sRTF.Format(_T("{\\rtf1{\\field{\\*\\fldinst{HYPERLINK \"%s\"}{\\fldrslt{\\ul %s}}}}}"), (LPCTSTR)sCommand, pszText);
		AddLine(sRTF, sRTF.GetLength(), true);
	}
}

void CHTRichEditCtrl::AppendColoredText(LPCTSTR pszText, COLORREF cr, COLORREF bk, DWORD mask)
{
	AddLine(pszText, -1, false, cr, bk, mask);
}

void CHTRichEditCtrl::AppendKeyWord(LPCTSTR str, COLORREF cr)
{
	AppendColoredText(str, cr);
}

BOOL CHTRichEditCtrl::OnEnLink(LPNMHDR pNMHDR, LRESULT *pResult)
{
	ENLINK *pEnLink = reinterpret_cast<ENLINK*>(pNMHDR);
	if (pEnLink && pEnLink->msg == WM_LBUTTONDOWN) {
		CString strUrl;
		GetTextRange(pEnLink->chrg.cpMin, pEnLink->chrg.cpMax, strUrl);

		// Check if this link has a valid URL scheme.
		// If it does not, pass that notification up to the parent window
		// which may interpret this "URL" in some other way.
		for (unsigned i = 0; s_apszSchemes[i].pszScheme; ++i)
			if (_tcsncmp(strUrl, s_apszSchemes[i].pszScheme, s_apszSchemes[i].iLen) == 0) {
				BrowserOpen(strUrl, NULL);
				return (BOOL)(*pResult = 1); // do not pass this message to any parent
			}
	}
	return (BOOL)(*pResult = 0);
}

CString CHTRichEditCtrl::GetText() const
{
	CString strText;
	GetWindowText(strText);
	return strText;
}

void CHTRichEditCtrl::SetFont(CFont *pFont, BOOL bRedraw)
{
	// Use the 'ScrollInfo' only if there is a scrollbar available, otherwise
	// this scrollinfo points to the top and we would thus stay at the top.
	bool bAtEndOfScroll;
	SCROLLINFO si;
	si.cbSize = (UINT)sizeof si;
	si.fMask = SIF_ALL;
	if ((GetStyle() & WS_VSCROLL) && GetScrollInfo(SB_VERT, &si))
		bAtEndOfScroll = (si.nPos >= (int)(si.nMax - si.nPage));
	else
		bAtEndOfScroll = true;

	LOGFONT lf;
	pFont->GetLogFont(&lf);

	CHARFORMAT cf = {};
	cf.cbSize = (UINT)sizeof cf;

	cf.dwMask |= CFM_BOLD;
	if (lf.lfWeight == FW_BOLD)
		cf.dwEffects |= CFE_BOLD;

	cf.dwMask |= CFM_ITALIC;
	if (lf.lfItalic)
		cf.dwEffects |= CFE_ITALIC;

	cf.dwMask |= CFM_UNDERLINE;
	if (lf.lfUnderline)
		cf.dwEffects |= CFE_UNDERLINE;

	cf.dwMask |= CFM_STRIKEOUT;
	if (lf.lfStrikeOut)
		cf.dwEffects |= CFE_STRIKEOUT;

	cf.dwMask |= CFM_SIZE;
	HDC hDC = ::GetDC(HWND_DESKTOP);
	int iPointSize = -::MulDiv(lf.lfHeight, 72, ::GetDeviceCaps(hDC, LOGPIXELSY));
	cf.yHeight = iPointSize * 20;
	::ReleaseDC(NULL, hDC);

	cf.dwMask |= CFM_FACE;
	cf.bPitchAndFamily = lf.lfPitchAndFamily;
	_tcsncpy_s(cf.szFaceName, _countof(cf.szFaceName), lf.lfFaceName, _TRUNCATE);

	// although this should work correctly (according to SDK) it may give false results (e.g. the "click here..." text
	// which is shown in the server info window may not be entirely used as a hyperlink???)
	//cf.dwMask |= CFM_CHARSET;
	//cf.bCharSet = lf.lfCharSet;

	cf.yOffset = 0;
	VERIFY(SetDefaultCharFormat(cf));

	// copy everything except the color
	m_cfDefault.dwMask = (cf.dwMask & ~CFM_COLOR) | (m_cfDefault.dwMask & CFM_COLOR);
	m_cfDefault.dwEffects = cf.dwEffects;
	m_cfDefault.yHeight = cf.yHeight;
	m_cfDefault.yOffset = cf.yOffset;
	//m_cfDefault.crTextColor = cf.crTextColor;
	m_cfDefault.bCharSet = cf.bCharSet;
	m_cfDefault.bPitchAndFamily = cf.bPitchAndFamily;
	memcpy(m_cfDefault.szFaceName, cf.szFaceName, sizeof m_cfDefault.szFaceName);

	PurgeSmileyCaches();

	if (bAtEndOfScroll)
		ScrollToLastLine();

	if (bRedraw) {
		Invalidate();
		UpdateWindow();
	}
}

CFont* CHTRichEditCtrl::GetFont() const
{
	ASSERT(0);
	return NULL;
}

void CHTRichEditCtrl::OnSysColorChange()
{
	CRichEditCtrl::OnSysColorChange();
	ApplySkin();
}

void CHTRichEditCtrl::ApplySkin()
{
	if (m_strSkinKey.IsEmpty()) {
		m_bDfltForeground = m_crDfltForeground == CLR_DEFAULT;
		m_crForeground = m_bDfltForeground ? ::GetSysColor(COLOR_WINDOWTEXT) : m_crDfltForeground;

		m_bDfltBackground = m_crDfltBackground == CLR_DEFAULT;
		m_crBackground = m_bDfltBackground ? ::GetSysColor(COLOR_WINDOW) : m_crDfltBackground;

		VERIFY(GetSelectionCharFormat(m_cfDefault));
	} else {
		// Use the 'ScrollInfo' only, if there is a scrollbar available, otherwise we would
		// use a scrollinfo which points to the top and we would thus stay at the top.
		bool bAtEndOfScroll;
		SCROLLINFO si;
		si.cbSize = (UINT)sizeof si;
		si.fMask = SIF_ALL;
		if ((GetStyle() & WS_VSCROLL) && GetScrollInfo(SB_VERT, &si))
			bAtEndOfScroll = (si.nPos >= (int)(si.nMax - si.nPage));
		else
			bAtEndOfScroll = true;

		COLORREF cr;
		if (theApp.LoadSkinColor(m_strSkinKey + _T("Fg"), cr)) {
			m_bDfltForeground = false;
			m_crForeground = cr;
		} else {
			m_bDfltForeground = m_crDfltForeground == CLR_DEFAULT;
			m_crForeground = m_bDfltForeground ? ::GetSysColor(COLOR_WINDOWTEXT) : m_crDfltForeground;
		}

		bool bSetCharFormat = false;
		CHARFORMAT cf;
		GetDefaultCharFormat(cf);
		if (!m_bDfltForeground && (cf.dwEffects & CFE_AUTOCOLOR)) {
			cf.dwEffects &= ~CFE_AUTOCOLOR;
			bSetCharFormat = true;
		} else if (m_bDfltForeground && !(cf.dwEffects & CFE_AUTOCOLOR)) {
			cf.dwEffects |= CFE_AUTOCOLOR;
			bSetCharFormat = true;
		}
		if (bSetCharFormat) {
			cf.dwMask |= CFM_COLOR;
			cf.crTextColor = m_crForeground;
			VERIFY(SetDefaultCharFormat(cf));
			VERIFY(GetSelectionCharFormat(m_cfDefault));
		}

		if (theApp.LoadSkinColor(m_strSkinKey + _T("Bk"), cr)) {
			m_bDfltBackground = false;
			m_crBackground = cr;
			SetBackgroundColor(FALSE, m_crBackground);
		} else {
			m_bDfltBackground = m_crDfltBackground == CLR_DEFAULT;
			m_crBackground = m_bDfltBackground ? ::GetSysColor(COLOR_WINDOW) : m_crDfltBackground;
			SetBackgroundColor(m_bDfltBackground, m_crBackground);
		}

		if (bAtEndOfScroll)
			ScrollToLastLine();
	}
	PurgeSmileyCaches();
}

BOOL CHTRichEditCtrl::OnSetCursor(CWnd *pWnd, UINT nHitTest, UINT message)
{
	// Cheap workaround for the "Text cursor is showing while context menu is open" glitch. It could be solved properly
	// with the RE's COM interface, but because the according messages are not routed with a unique control ID, it's not
	// really usable (e.g. if there are more RE controls in one window). Would to envelope each RE window to get a unique ID.
	if (m_bForceArrowCursor && m_hArrowCursor) {
		::SetCursor(m_hArrowCursor);
		return TRUE;
	}
	return CRichEditCtrl::OnSetCursor(pWnd, nHitTest, message);
}


class CBitmapDataObject : public CCmdTarget
{
public:
	explicit CBitmapDataObject(HBITMAP hBitmap);

protected:
	DECLARE_INTERFACE_MAP();

	BEGIN_INTERFACE_PART(DataObject, IDataObject)
		virtual ~XDataObject() = default;
		STDMETHOD(GetData)(FORMATETC *pformatetcIn, STGMEDIUM *pmedium);
		STDMETHOD(GetDataHere)(FORMATETC *pformatetc, STGMEDIUM *pmedium);
		STDMETHOD(QueryGetData)(FORMATETC *pformatetc);
		STDMETHOD(GetCanonicalFormatEtc)(FORMATETC *pformatectIn, FORMATETC *pformatetcOut);
		STDMETHOD(SetData)(FORMATETC *pformatetc, STGMEDIUM *pmedium, BOOL fRelease);
		STDMETHOD(EnumFormatEtc)(DWORD dwDirection, IEnumFORMATETC **ppenumFormatEtc);
		STDMETHOD(DAdvise)(FORMATETC *pformatetc, DWORD advf, IAdviseSink *pAdvSink, DWORD *pdwConnection);
		STDMETHOD(DUnadvise)(DWORD dwConnection);
		STDMETHOD(EnumDAdvise)(IEnumSTATDATA **ppenumAdvise);
	END_INTERFACE_PART(DataObject)

	HBITMAP m_hBitmap;
};

BEGIN_INTERFACE_MAP(CBitmapDataObject, CCmdTarget)
	INTERFACE_PART(CBitmapDataObject, IID_IDataObject, DataObject)
END_INTERFACE_MAP()

CBitmapDataObject::CBitmapDataObject(HBITMAP hBitmap)
	:m_hBitmap(hBitmap)
{
}

#pragma warning(push)
#pragma warning(disable:4100) // unreferenced parameter
#pragma warning(disable:4555) // expression has no effect; expected expression with side-effect (because of the 'METHOD_PROLOGUE' macro)

STDMETHODIMP CBitmapDataObject::XDataObject::QueryInterface(REFIID iid, LPVOID *ppvObj) NOEXCEPT
{
	METHOD_PROLOGUE(CBitmapDataObject, DataObject);
	return (HRESULT)pThis->ExternalQueryInterface(&iid, ppvObj);
}

STDMETHODIMP_(ULONG) CBitmapDataObject::XDataObject::AddRef() NOEXCEPT
{
	METHOD_PROLOGUE(CBitmapDataObject, DataObject);
	return pThis->ExternalAddRef();
}

STDMETHODIMP_(ULONG) CBitmapDataObject::XDataObject::Release() NOEXCEPT
{
	METHOD_PROLOGUE(CBitmapDataObject, DataObject);
	return pThis->ExternalRelease();
}

STDMETHODIMP CBitmapDataObject::XDataObject::GetData(FORMATETC *pformatetcIn, STGMEDIUM *pmedium) noexcept
{
	METHOD_PROLOGUE(CBitmapDataObject, DataObject);
	pmedium->tymed = TYMED_GDI;
	pmedium->hBitmap = (HBITMAP)CopyImage(pThis->m_hBitmap, IMAGE_BITMAP, 0, 0, 0);
	pmedium->pUnkForRelease = NULL;
	return S_OK;
}

STDMETHODIMP CBitmapDataObject::XDataObject::GetDataHere(FORMATETC *pformatetc, STGMEDIUM *pmedium) noexcept
{
	METHOD_PROLOGUE(CBitmapDataObject, DataObject);
	return E_NOTIMPL;
}

STDMETHODIMP CBitmapDataObject::XDataObject::QueryGetData(FORMATETC *pformatetc) noexcept
{
	METHOD_PROLOGUE(CBitmapDataObject, DataObject);
	return E_NOTIMPL;
}

STDMETHODIMP CBitmapDataObject::XDataObject::GetCanonicalFormatEtc(FORMATETC *pformatectIn, FORMATETC *pformatetcOut) noexcept
{
	METHOD_PROLOGUE(CBitmapDataObject, DataObject);
	return E_NOTIMPL;
}

STDMETHODIMP CBitmapDataObject::XDataObject::SetData(FORMATETC *pformatetc, STGMEDIUM *pmedium, BOOL fRelease) noexcept
{
	METHOD_PROLOGUE(CBitmapDataObject, DataObject);
	return E_NOTIMPL;
}

STDMETHODIMP CBitmapDataObject::XDataObject::EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC **ppenumFormatEtc) noexcept
{
	METHOD_PROLOGUE(CBitmapDataObject, DataObject);
	return E_NOTIMPL;
}

STDMETHODIMP CBitmapDataObject::XDataObject::DAdvise(FORMATETC *pformatetc, DWORD advf, IAdviseSink *pAdvSink, DWORD *pdwConnection) noexcept
{
	METHOD_PROLOGUE(CBitmapDataObject, DataObject);
	return E_NOTIMPL;
}

STDMETHODIMP CBitmapDataObject::XDataObject::DUnadvise(DWORD dwConnection) noexcept
{
	METHOD_PROLOGUE(CBitmapDataObject, DataObject);
	return E_NOTIMPL;
}

STDMETHODIMP CBitmapDataObject::XDataObject::EnumDAdvise(IEnumSTATDATA **ppenumAdvise) noexcept
{
	METHOD_PROLOGUE(CBitmapDataObject, DataObject);
	return E_NOTIMPL;
}

#pragma warning(pop)

static const struct
{
	LPCTSTR pszSmiley;
	int iLen;
	LPCTSTR pszID;
} s_apszSmileys[] =
{
	// :) :))) ;) :D :/ :P :-x :( :'-( :-| :-* :ph34r: =) :] :[ :-O <_<
#define	S(str, id)	 { _T(str), _countof(str)-1, _T(id) }
	S(":)",         "smile"),
	S(":-)",        "smile"),

	S(":]",         "smileq"),
	S(":-]",        "smileq"),

	S(":))",		"happy"),
	S(":)))",		"happy"),
	S(":-))",		"happy"),
	S(":-)))",		"happy"),
	S("^_^",		"happy"),
	S("(^.^)",		"happy"),

	S(";)",         "wink"),
	S(";-)",        "wink"),

	S(":D",			"laugh"),
	S(":-D",		"laugh"),
	S("lol",		"laugh"),
	S("LOL",		"laugh"),
	S(":lol:",		"laugh"),
	S(":LOL:",		"laugh"),
	S("*lol*",		"laugh"),
	S("*LOL*",		"laugh"),

	S("=)",			"interest"),
	S("=-)",		"interest"),

	S(":/",			"skeptic"),
	S(":-/",		"skeptic"),
	S(":\\",		"skeptic"),
	S(":-\\",		"skeptic"),

	S("<.<",		"lookside"),
	S("<_<",		"lookside"),
	S(">.>",		"lookside"),
	S(">_>",		"lookside"),

	S(":P",			"tongue"),
	S(":-P",		"tongue"),
	S(":p",			"tongue"),
	S(":-p",		"tongue"),

	S(":-x",		"sealed"),
	S(":-X",		"sealed"),

	S(":-|",		"disgust"),

	S(":(",         "sad"),
	S(":-(",        "sad"),

	S(":[",         "sadq"),
	S(":-[",        "sadq"),

	S(":cry:",		"cry"),
	S(":'-(",		"cry"),
	S(":~-(",		"cry"),
	S(":~(~~~",		"cry"),
	S(":~(~~",		"cry"),
	S(":~(~",		"cry"),
	S(":,(",		"cry"),
	S(";(",			"cry"),
	S(";-(",		"cry"),
	S("&.(..",		"cry"),
	S(":'(",		"cry"),
	S(":,-(",		"cry"),

	S(":o",			"omg"),
	S(":O",			"omg"),
	S(":-o",		"omg"),
	S(":-O",		"omg"),

	S(":love:",		"love"),
	S("(*_*)",		"love"),
	S("<*_*>",		"love"),
	S(":kiss:",		"love"),
	S(":-*",		"love"),

	S("-_-",		"ph34r"),
	S(":ph34r:",	"ph34r"),
#undef S
};

void CHTRichEditCtrl::AddSmileys(LPCTSTR pszLine)
{
	int iPos = 0;
	LPCTSTR pszStart = pszLine;
	for (LPCTSTR psz = pszLine; *psz != _T('\0');) {
		bool bFoundSmiley = false;
		if (iPos == 0 || psz[-1] == _T(' ') || psz[-1] == _T('.')) {
			for (unsigned i = 0; i < _countof(s_apszSmileys); ++i) {
				if (_tcsncmp(psz, s_apszSmileys[i].pszSmiley, s_apszSmileys[i].iLen) == 0
					&& (psz[s_apszSmileys[i].iLen] == _T('\0')
						|| psz[s_apszSmileys[i].iLen] == _T('\r')
						|| psz[s_apszSmileys[i].iLen] == _T('\n')
						|| psz[s_apszSmileys[i].iLen] == _T(' '))) //
				{
					if (psz - pszStart > 0)
						ReplaceSel(CString(pszStart, (int)(psz - pszStart)));
					CHARFORMAT2 cf = {};
					cf.cbSize = (UINT)sizeof cf;
					cf.dwMask = CFM_COLOR;
					COLORREF bk = (GetSelectionCharFormat(cf) & CFM_BACKCOLOR) ? cf.crBackColor : m_crBackground;
					if (!InsertSmiley(s_apszSmileys[i].pszID, bk))
						ReplaceSel(s_apszSmileys[i].pszSmiley);
					psz += s_apszSmileys[i].iLen;
					iPos += s_apszSmileys[i].iLen;
					pszStart = psz;
					bFoundSmiley = true;
					break;
				}
			}
		}
		if (!bFoundSmiley) {
			psz = _tcsinc(psz);
			++iPos;
		}
	}
	if (*pszStart != _T('\0'))
		ReplaceSel(pszStart);
}

HBITMAP IconToBitmap(HICON hIcon, COLORREF crBackground, int cx = 16, int cy = 16)
{
	if (cx <= 0 || cy <= 0) {
		ICONINFO ii;
		if (!::GetIconInfo(hIcon, &ii))
			return NULL;
		BITMAP bmi;
		int iSize = ::GetObject(ii.hbmColor, sizeof bmi, &bmi);
		::DeleteObject(ii.hbmMask);
		::DeleteObject(ii.hbmColor);
		if ((size_t)iSize < sizeof bmi - sizeof bmi.bmBits)
			return NULL;
		cx = bmi.bmWidth;
		cy = bmi.bmHeight;
	}

	// Draw icon into bitmap for keeping any available transparency. Another way todo this
	// might be to use a metafile - would need to test.
	HBITMAP hBitmap = NULL;
	try {
		// Do *not* create a transparent bitmap
		//ULONG_PTR gdiplusToken = 0;
		//Gdiplus::GdiplusStartupInput gdiplusStartupInput;
		//if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL) == Gdiplus::OK) {
		//	Gdiplus::Bitmap bmp(hIcon);
		//	Gdiplus::Color colorBackground(255, GetRValue(crBackground), GetGValue(crBackground), GetBValue(crBackground));
		//	bmp.GetHBITMAP(colorBackground, &hBitmap);
		//}
		//Gdiplus::GdiplusShutdown(gdiplusToken);

		CClientDC dcScreen(CWnd::GetDesktopWindow());
		CDC dcMem;
		if (dcMem.CreateCompatibleDC(&dcScreen)) {
			CBitmap bmp;
			if (bmp.CreateCompatibleBitmap(&dcScreen, cx, cy)) {
				CBitmap *pbmpOld = dcMem.SelectObject(&bmp);
				dcMem.FillSolidRect(0, 0, cx, cy, crBackground);
				::DrawIconEx(dcMem, 0, 0, hIcon, cx, cy, 0, 0, DI_NORMAL);
				dcMem.SelectObject(pbmpOld);
				hBitmap = (HBITMAP)bmp.Detach();
			}
		}
	} catch (CException *ex) {
		ASSERT(0);
		ex->Delete();
	}

	return hBitmap;
}

HBITMAP CHTRichEditCtrl::GetSmileyBitmap(LPCTSTR pszSmileyID, COLORREF bk)
{
	CString sSmileyID;
	sSmileyID.Format(_T("%s%08lx"), pszSmileyID, bk);
	void *pvData;
	if (sm_aSmileyBitmaps.Lookup(sSmileyID, pvData))
		return (HBITMAP)pvData;

	int cx = 16, cy = 16;
	CHARFORMAT cf;
	GetDefaultCharFormat(cf);
	if (cf.cbSize == sizeof cf && cf.dwMask & CFM_SIZE) {
		HDC hDC = ::GetDC(HWND_DESKTOP);
		int iPixelFontSize = abs(-::MulDiv(cf.yHeight, ::GetDeviceCaps(hDC, LOGPIXELSY), 20) / 72);
		::ReleaseDC(NULL, hDC);
		//Point	Pixel	Icon
		//10	13		14
		//11	14		14
		//12	16		18
		//14	18		24
		//16	21		24
		//18	24		24
		//20	26		32
		if (iPixelFontSize <= 14)
			;//cx = cy = 16;
		else if (iPixelFontSize <= 17)
			cx = cy = 18;
		else if (iPixelFontSize <= 25)
			cx = cy = 24;
		else
			cx = cy = 32;
	}

	static const CString &strResourceName(_T("Smiley_"));
	HICON hIcon = theApp.LoadIcon(strResourceName + pszSmileyID, cx, cy, LR_DEFAULTCOLOR);
	if (hIcon == NULL)
		return (HBITMAP)NULL;

	// Don't specify an icon size for the bitmap creation, we could use 'cx' and 'cy' only for
	// the built-in icons because we know their sizes, but if there is a skin active, the
	// icons (which can also be GIF images) can have any size.
	HBITMAP hBitmap = IconToBitmap(hIcon, bk, 0, 0);
	if (hBitmap != NULL)
		sm_aSmileyBitmaps[sSmileyID] = hBitmap;
	return hBitmap;
}

bool CHTRichEditCtrl::InsertSmiley(LPCTSTR pszSmileyID, COLORREF bk)
{
	HBITMAP hbmp = GetSmileyBitmap(pszSmileyID, bk);
	if (hbmp == NULL)
		return false;

	CBitmapDataObject *pbdo = new CBitmapDataObject(hbmp);
	CComPtr<IDataObject> pIDataObject;
	pIDataObject.Attach((IDataObject*)pbdo->GetInterface(&IID_IDataObject));

	CComPtr<IRichEditOle> pIRichEditOle;
	pIRichEditOle.Attach(GetIRichEditOle());
	if (!pIRichEditOle)
		return false;

	CComPtr<IOleClientSite> pIOleClientSite;
	pIRichEditOle->GetClientSite(&pIOleClientSite);
	if (!pIOleClientSite)
		return false;

	if (sm_pIStorageSmileys == NULL) {
		CComPtr<ILockBytes> pILockBytes;
		if (CreateILockBytesOnHGlobal(NULL, TRUE, &pILockBytes) != S_OK)
			return false;
		if (StgCreateDocfileOnILockBytes(pILockBytes, STGM_SHARE_EXCLUSIVE | STGM_CREATE | STGM_READWRITE, 0, &sm_pIStorageSmileys) != S_OK)
			return false;
	}

	FORMATETC FormatEtc;
	FormatEtc.cfFormat = CF_BITMAP;
	FormatEtc.ptd = NULL;
	FormatEtc.dwAspect = DVASPECT_CONTENT;
	FormatEtc.lindex = -1;
	FormatEtc.tymed = TYMED_GDI;

	CComPtr<IOleObject> pIOleObject;
	if (S_OK != OleCreateStaticFromData(pIDataObject, __uuidof(pIOleObject), OLERENDER_FORMAT, &FormatEtc, pIOleClientSite, sm_pIStorageSmileys, (void**)&pIOleObject))
		return false;
	OleSetContainedObject(pIOleObject, TRUE);

	REOBJECT reobject = {};
	reobject.cbStruct = (DWORD)sizeof reobject;
	if (pIOleObject->GetUserClassID(&reobject.clsid) != S_OK)
		return false;
	reobject.cp = (LONG)REO_CP_SELECTION;
	reobject.dvaspect = DVASPECT_CONTENT;
	reobject.poleobj = pIOleObject;
	reobject.polesite = pIOleClientSite;
	reobject.pstg = sm_pIStorageSmileys;
	reobject.dwFlags = REO_BELOWBASELINE;

	return pIRichEditOle->InsertObject(&reobject) == S_OK;
}

bool CHTRichEditCtrl::AddCaptcha(HBITMAP hbmp)
{
	if (hbmp == NULL)
		return false;

	SetSel(GetWindowTextLength(), GetWindowTextLength());
	CBitmapDataObject *pbdo = new CBitmapDataObject(hbmp);
	CComPtr<IDataObject> pIDataObject;
	pIDataObject.Attach((IDataObject*)pbdo->GetInterface(&IID_IDataObject));

	CComPtr<IRichEditOle> pIRichEditOle;
	pIRichEditOle.Attach(GetIRichEditOle());
	if (!pIRichEditOle)
		return false;

	CComPtr<IOleClientSite> pIOleClientSite;
	pIRichEditOle->GetClientSite(&pIOleClientSite);
	if (!pIOleClientSite)
		return false;

	if (m_pIStorageCaptchas == NULL) {
		CComPtr<ILockBytes> pILockBytes;
		if (S_OK != CreateILockBytesOnHGlobal(NULL, TRUE, &pILockBytes))
			return false;
		if (S_OK != StgCreateDocfileOnILockBytes(pILockBytes, STGM_SHARE_EXCLUSIVE | STGM_CREATE | STGM_READWRITE, 0, &m_pIStorageCaptchas))
			return false;
	}

	FORMATETC FormatEtc;
	FormatEtc.cfFormat = CF_BITMAP;
	FormatEtc.ptd = NULL;
	FormatEtc.dwAspect = DVASPECT_CONTENT;
	FormatEtc.lindex = -1;
	FormatEtc.tymed = TYMED_GDI;

	CComPtr<IOleObject> pIOleObject;
	if (OleCreateStaticFromData(pIDataObject, __uuidof(pIOleObject), OLERENDER_FORMAT, &FormatEtc, pIOleClientSite, m_pIStorageCaptchas, (void**)&pIOleObject) != S_OK)
		return false;
	OleSetContainedObject(pIOleObject, TRUE);

	REOBJECT reobject = {};
	reobject.cbStruct = (DWORD)sizeof reobject;
	if (pIOleObject->GetUserClassID(&reobject.clsid) != S_OK)
		return false;
	reobject.cp = (LONG)REO_CP_SELECTION;
	reobject.dvaspect = DVASPECT_CONTENT;
	reobject.poleobj = pIOleObject;
	reobject.polesite = pIOleClientSite;
	reobject.pstg = m_pIStorageCaptchas;
	reobject.dwFlags = REO_BELOWBASELINE;

	if (pIRichEditOle->InsertObject(&reobject) != S_OK)
		return false;
	ReplaceSel(_T(""));

	return true;
}
