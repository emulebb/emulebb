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
#include "OtherFunctions.h"
#include <intshcut.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define FILEEXT_INETSHRTCUTA		"url"						// ANSI string
#define FILEEXT_INETSHRTCUTW		L"url"						// Unicode string
#define FILEEXT_INETSHRTCUT			_T(FILEEXT_INETSHRTCUTA)
#define FILEEXTDOT_INETSHRTCUTA		"." FILEEXT_INETSHRTCUTA	// ANSI string
#define FILEEXTDOT_INETSHRTCUTW		L"." FILEEXT_INETSHRTCUTW	// Unicode string
#define FILEEXTDOT_INETSHRTCUT		_T(".") FILEEXT_INETSHRTCUT
//#define FILETYPE_INETSHRTCUT		_T("Internet Shortcut File")
//#define FILEFLT_INETSHRTCUT		FILETYPE_INETSHRTCUT _T("s (*") FILEEXTDOT_INETSHRTCUT _T(")|*") FILEEXTDOT_INETSHRTCUT _T("|")

BOOL IsUrlSchemeSupportedW(LPCWSTR pszUrl)
{
	static const struct SCHEME
	{
		LPCWSTR pszPrefix;
		int iLen;
	} _aSchemes[] =
	{
#define SCHEME_ENTRY(prefix)	{ prefix, _countof(prefix)-1 }
		SCHEME_ENTRY(L"ed2k://"),
		SCHEME_ENTRY(L"magnet:?")
#undef SCHEME_ENTRY
	};

	for (unsigned i = 0; i < _countof(_aSchemes); ++i)
		if (wcsncmp(pszUrl, _aSchemes[i].pszPrefix, _aSchemes[i].iLen) == 0)
			return TRUE;
	return FALSE;
}

// GetFileExtA -- ANSI version
//
// This function is thought to be used only for filenames which have been
// validated by 'GetFullPathName' or similar functions.
LPCSTR GetFileExtA(LPCSTR pszPathA, int iLen /*= -1*/)
{
	// Just search the last '.'-character which comes after an optionally
	// available last '\'-char.
	int iPos = iLen >= 0 ? iLen : (int)strlen(pszPathA);
	while (iPos-- > 0) {
		if (pszPathA[iPos] == '.')
			return &pszPathA[iPos];
		if (pszPathA[iPos] == '\\')
			break;
	}

	return NULL;
}

// GetFileExtW -- Unicode version
//
// This function is thought to be used only for filenames which have been
// validated by 'GetFullPathName' or similar functions.
LPCWSTR GetFileExtW(LPCWSTR pszPathW, int iLen /*= -1*/)
{
	// Just search the last '.'-character which comes after an optionally
	// available last '\'-char.
	int iPos = iLen >= 0 ? iLen : (int)wcslen(pszPathW);
	while (iPos-- > 0) {
		if (pszPathW[iPos] == L'.')
			return &pszPathW[iPos];
		if (pszPathW[iPos] == L'\\')
			break;
	}

	return NULL;
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
				if (_wcsnicmp(pszUrlW, L"ed2k://|", 8) == 0 || _wcsnicmp(pszUrlW, L"magnet:?", 8) == 0) {
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
				if (_strnicmp(pszUrlA, "ed2k://|", 8) == 0 || _strnicmp(pszUrlA, "magnet:?", 8) == 0) {
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

HRESULT CMainFrameDropTarget::AddUrlFileContents(LPCTSTR pszFileName)
{
	HRESULT hrResult = S_FALSE;

	if (ExtensionIs(pszFileName, FILEEXTDOT_INETSHRTCUT)) {
		CComPtr<IUniformResourceLocatorW> pIUrl;
		hrResult = CoCreateInstance(CLSID_InternetShortcut, NULL, CLSCTX_INPROC_SERVER, IID_IUniformResourceLocatorW, (LPVOID*)&pIUrl);
		if (SUCCEEDED(hrResult)) {
			CComPtr<IPersistFile> pIFile;
			hrResult = pIUrl.QueryInterface(&pIFile);
			if (SUCCEEDED(hrResult)) {
				hrResult = pIFile->Load(CComBSTR(pszFileName), STGM_READ | STGM_SHARE_DENY_WRITE);
				if (SUCCEEDED(hrResult)) {
					LPWSTR pwszUrl;
					hrResult = pIUrl->GetURL(&pwszUrl);
					if (hrResult == S_OK) {
						if (pwszUrl != NULL && pwszUrl[0] != L'\0' && IsUrlSchemeSupportedW(pwszUrl))
							theApp.emuledlg->ProcessED2KLink(pwszUrl);
						else
							hrResult = S_FALSE;
						::CoTaskMemFree(pwszUrl);
					}
				}
			}
		}
	}

	return hrResult;
}

HRESULT CMainFrameDropTarget::PasteHDROP(COleDataObject &data)
{
	HRESULT hrPasteResult = E_FAIL;
	HANDLE hMem = data.GetGlobalData(CF_HDROP);
	if (hMem != NULL) {
		LPDROPFILES lpDrop = (LPDROPFILES)::GlobalLock(hMem);
		if (lpDrop != NULL) {
			if (lpDrop->fWide) {
				LPCWSTR pszFileNameW = (LPCWSTR)((LPBYTE)lpDrop + lpDrop->pFiles);
				while (*pszFileNameW != L'\0') {
					if (FAILED(AddUrlFileContents(pszFileNameW)))
						break;
					hrPasteResult = S_OK;
					pszFileNameW += wcslen(pszFileNameW) + 1;
				}
			} else {
				LPCSTR pszFileNameA = (LPCSTR)((LPBYTE)lpDrop + lpDrop->pFiles);
				while (*pszFileNameA != '\0') {
					if (FAILED(AddUrlFileContents(CString(pszFileNameA))))
						break;
					hrPasteResult = S_OK;
					pszFileNameA += strlen(pszFileNameA) + 1;
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

	// If the data is in 'UniformResourceLocator', there is no need to check the contents.
	if (m_cfShellURL && pDataObject->IsDataAvailable(m_cfShellURL))
		return TRUE;

	BOOL bResult = FALSE; // Unknown data format
	if (pDataObject->IsDataAvailable(CF_UNICODETEXT)) {
		//
		// Check text data
		//
		HANDLE hMem = pDataObject->GetGlobalData(CF_UNICODETEXT);
		if (hMem != NULL) {
			LPCWSTR lpszUrl = (LPCWSTR)::GlobalLock(hMem);
			if (lpszUrl != NULL) {
				// skip white space
				while (iswspace(*lpszUrl))
					++lpszUrl;
				bResult = IsUrlSchemeSupportedW(lpszUrl);
				::GlobalUnlock(hMem);
			}
			::GlobalFree(hMem);
		}
	} else if (pDataObject->IsDataAvailable(CF_HDROP)) {
		//
		// Check HDROP data
		//
		HANDLE hMem = pDataObject->GetGlobalData(CF_HDROP);
		if (hMem != NULL) {
			LPDROPFILES lpDrop = (LPDROPFILES)::GlobalLock(hMem);
			if (lpDrop != NULL) {
				// Just check, if there's at least one file we can import
				if (lpDrop->fWide) {
					LPCWSTR pszFileW = (LPCWSTR)((LPBYTE)lpDrop + lpDrop->pFiles);
					while (*pszFileW != L'\0') {
						size_t iLen = wcslen(pszFileW);
						LPCWSTR pszExtW = GetFileExtW(pszFileW, (int)iLen);
						if (pszExtW != NULL && _wcsicmp(pszExtW, FILEEXTDOT_INETSHRTCUTW) == 0) {
							bResult = TRUE;
							break;
						}
						pszFileW += iLen + 1;
					}
				} else {
					LPCSTR pszFileA = (LPCSTR)((LPBYTE)lpDrop + lpDrop->pFiles);
					while (*pszFileA != '\0') {
						size_t iLen = strlen(pszFileA);
						LPCSTR pszExtA = GetFileExtA(pszFileA, (int)iLen);
						if (pszExtA != NULL && _stricmp(pszExtA, FILEEXTDOT_INETSHRTCUTA) == 0) {
							bResult = TRUE;
							break;
						}
						pszFileA += iLen + 1;
					}
				}
				::GlobalUnlock(hMem);
			}
			::GlobalFree(hMem);
		}
	}
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
		else if (pDataObject->IsDataAvailable(CF_HDROP))
			return PasteHDROP(*pDataObject) == S_OK;
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
