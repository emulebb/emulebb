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
#include "DropTarget.h"
#include "DropTargetSeams.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	bool IsSupportedDropText(CLIPFORMAT cfData, COleDataObject &data)
	{
		bool bResult = false;
		HANDLE hMem = data.GetGlobalData(cfData);
		if (hMem != NULL) {
			const void *pvData = ::GlobalLock(hMem);
			if (pvData != NULL) {
				if (cfData == CF_UNICODETEXT)
					bResult = DropTargetSeams::IsSupportedTextDrop(static_cast<LPCWSTR>(pvData));
				else
					bResult = DropTargetSeams::IsSupportedTextDrop(static_cast<LPCSTR>(pvData));
				::GlobalUnlock(hMem);
			}
			::GlobalFree(hMem);
		}
		return bResult;
	}
}


//////////////////////////////////////////////////////////////////////////////
// CMainFrameDropTarget

CMainFrameDropTarget::CMainFrameDropTarget()
	: m_bDropDataValid()
	, m_cfShellURL((CLIPFORMAT)RegisterClipboardFormat(CFSTR_SHELLURL))
{
	ASSERT(m_cfShellURL);
}

HRESULT CMainFrameDropTarget::PasteText(CLIPFORMAT cfData, COleDataObject &data)
{
	HRESULT hrPasteResult = E_FAIL;
	HANDLE hMem = data.GetGlobalData(cfData);
	if (hMem != NULL) {
		const void *pvData = ::GlobalLock(hMem);
		if (pvData != NULL) {
			if (cfData == CF_UNICODETEXT) {
				LPCWSTR pszUrlW = static_cast<LPCWSTR>(pvData);
				while (iswspace(*pszUrlW))
					++pszUrlW;

				hrPasteResult = S_FALSE; // default: nothing was pasted
				if (DropTargetSeams::IsSupportedTextDrop(pszUrlW)) {
					const CString strData(pszUrlW);
					for (int iPos = 0; iPos >= 0;) {
						CString sLink(strData.Tokenize(_T("\r\n"), iPos));
						sLink.Trim();
						if (!sLink.IsEmpty()) {
							theApp.emuledlg->ProcessED2KLink(sLink);
							hrPasteResult = S_OK;
						}
					}
				}
			} else {
				LPCSTR pszUrlA = static_cast<LPCSTR>(pvData);
				while (isspace(*pszUrlA))
					++pszUrlA;

				hrPasteResult = S_FALSE; // default: nothing was pasted
				if (DropTargetSeams::IsSupportedTextDrop(pszUrlA)) {
					const CString strData(pszUrlA);
					for (int iPos = 0; iPos >= 0;) {
						CString sLink(strData.Tokenize(_T("\r\n"), iPos));
						sLink.Trim();
						if (!sLink.IsEmpty()) {
							theApp.emuledlg->ProcessED2KLink(sLink);
							hrPasteResult = S_OK;
						}
					}
				}
			}
			::GlobalUnlock(hMem);
		}
		::GlobalFree(hMem);
	}
	return hrPasteResult;
}

BOOL CMainFrameDropTarget::IsSupportedDropData(COleDataObject *pDataObject)
{
	//************************************************************************
	//*** THIS FUNCTION HAS TO BE AS FAST AS POSSIBLE!!!
	//************************************************************************

	BOOL bResult = FALSE; // Unknown data format
	if (m_cfShellURL && pDataObject->IsDataAvailable(m_cfShellURL))
		bResult = IsSupportedDropText(m_cfShellURL, *pDataObject);
	if (!bResult && pDataObject->IsDataAvailable(CF_UNICODETEXT))
		bResult = IsSupportedDropText(CF_UNICODETEXT, *pDataObject);
	if (!bResult && pDataObject->IsDataAvailable(CF_TEXT))
		bResult = IsSupportedDropText(CF_TEXT, *pDataObject);
	return bResult;
}

DROPEFFECT CMainFrameDropTarget::OnDragEnter(CWnd*, COleDataObject *pDataObject, DWORD, CPoint)
{
	m_bDropDataValid = IsSupportedDropData(pDataObject);
	return m_bDropDataValid ? DROPEFFECT_COPY : DROPEFFECT_NONE;
}

DROPEFFECT CMainFrameDropTarget::OnDragOver(CWnd*, COleDataObject*, DWORD, CPoint)
{
	return m_bDropDataValid ? DROPEFFECT_COPY : DROPEFFECT_NONE;
}

BOOL CMainFrameDropTarget::OnDrop(CWnd*, COleDataObject *pDataObject, DROPEFFECT /*dropEffect*/, CPoint /*point*/)
{
	if (m_bDropDataValid) {
		if (m_cfShellURL && pDataObject->IsDataAvailable(m_cfShellURL))
			PasteText(m_cfShellURL, *pDataObject);
		else if (pDataObject->IsDataAvailable(CF_UNICODETEXT))
			PasteText(CF_UNICODETEXT, *pDataObject);
		else if (pDataObject->IsDataAvailable(CF_TEXT))
			PasteText(CF_TEXT, *pDataObject);
		else if (m_cfShellURL && pDataObject->IsDataAvailable(m_cfShellURL))
			PasteText(m_cfShellURL, *pDataObject);
		return TRUE;
	}
	return FALSE;
}

void CMainFrameDropTarget::OnDragLeave(CWnd*)
{
	// Do *NOT* set m_bDropDataValid to FALSE!
	// 'OnDragLeave' may be called from MFC when scrolling!
	// In that case it's not really a "leave".
	//m_bDropDataValid = FALSE;
}
