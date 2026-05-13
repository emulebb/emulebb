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
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#include "stdafx.h"
#include "RARFile.h"
#include "RARFileSeams.h"
#include "OtherFunctions.h"
#include "Log.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

LPCTSTR CRARFile::sUnrar_download = _T("Install RARLAB UnRAR DLL from https://www.rarlab.com. eMule checks ") UNRAR_DLL_INSTALL_HINT _T(".");

namespace
{
CString GetProgramFilesX86Folder()
{
	CString strProgramFilesX86(PathHelpers::GetShellFolderPath(CSIDL_PROGRAM_FILESX86));
	if (strProgramFilesX86.IsEmpty())
		strProgramFilesX86 = PathHelpers::GetShellFolderPath(CSIDL_PROGRAM_FILES);
	return strProgramFilesX86;
}

CString GetInstalledUnRarDllPath()
{
	CString strDllPath(RARFileSeams::BuildInstalledDllPath(GetProgramFilesX86Folder()));
	if (!strDllPath.IsEmpty())
		canonical(strDllPath);
	return strDllPath;
}

}

CRARFile::CRARFile()
	: m_hLibUnRar()
	, m_hArchive()
	, m_pfnRAROpenArchiveEx()
	, m_pfnRARCloseArchive()
	, m_pfnRARReadHeaderEx()
	, m_pfnRARProcessFileW()
	, m_pfnRARGetDllVersion()
{
}

CRARFile::~CRARFile()
{
	Close();
	if (m_hLibUnRar)
		VERIFY(::FreeLibrary(m_hLibUnRar));
}

bool CRARFile::InitUnRarLib()
{
	if (m_hLibUnRar == NULL) {
		CString strFailure;
		const CString strDllPath(GetInstalledUnRarDllPath());
		if (!RARFileSeams::IsAbsoluteLoadCandidate(strDllPath)) {
			strFailure = _T("could not resolve the installed DLL path");
		} else if (!LongPathSeams::PathExists(strDllPath)) {
			strFailure.Format(_T("expected DLL was not found at \"%s\""), (LPCTSTR)strDllPath);
		} else {
			const CString strLoadPath(LongPathSeams::PreparePathForLongPath(strDllPath).c_str());
			m_hLibUnRar = ::LoadLibraryEx(strLoadPath, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
			if (m_hLibUnRar == NULL)
				strFailure.Format(_T("LoadLibraryEx failed for \"%s\": %s"), (LPCTSTR)strDllPath, (LPCTSTR)GetErrorMessage(::GetLastError()));
		}

		if (m_hLibUnRar) {
			(FARPROC&)m_pfnRAROpenArchiveEx = ::GetProcAddress(m_hLibUnRar, "RAROpenArchiveEx");
			(FARPROC&)m_pfnRARCloseArchive = ::GetProcAddress(m_hLibUnRar, "RARCloseArchive");
			(FARPROC&)m_pfnRARReadHeaderEx = ::GetProcAddress(m_hLibUnRar, "RARReadHeaderEx");
			(FARPROC&)m_pfnRARProcessFileW = ::GetProcAddress(m_hLibUnRar, "RARProcessFileW");
			(FARPROC&)m_pfnRARGetDllVersion = ::GetProcAddress(m_hLibUnRar, "RARGetDllVersion");
			if (m_pfnRAROpenArchiveEx == NULL
				|| m_pfnRARCloseArchive == NULL
				|| m_pfnRARReadHeaderEx == NULL
				|| m_pfnRARProcessFileW == NULL)
			{
				strFailure = _T("required UnRAR exports are missing");
				::FreeLibrary(m_hLibUnRar);
				m_hLibUnRar = NULL;
				m_pfnRAROpenArchiveEx = NULL;
				m_pfnRARCloseArchive = NULL;
				m_pfnRARReadHeaderEx = NULL;
				m_pfnRARProcessFileW = NULL;
				m_pfnRARGetDllVersion = NULL;
			} else if (m_pfnRARGetDllVersion != NULL) {
				int iDllVersion = 0;
				try {
					iDllVersion = (*m_pfnRARGetDllVersion)();
				} catch (...) {
					iDllVersion = 0;
				}
				if (!RARFileSeams::IsCompatibleDllApiVersion(iDllVersion)) {
					strFailure.Format(_T("UnRAR DLL API version %d is below required %d"), iDllVersion, RARFileSeams::GetMinimumDllApiVersion());
					::FreeLibrary(m_hLibUnRar);
					m_hLibUnRar = NULL;
					m_pfnRAROpenArchiveEx = NULL;
					m_pfnRARCloseArchive = NULL;
					m_pfnRARReadHeaderEx = NULL;
					m_pfnRARProcessFileW = NULL;
					m_pfnRARGetDllVersion = NULL;
				}
			}
		}

		if (m_hLibUnRar == NULL && !strFailure.IsEmpty())
			LogWarning(LOG_STATUSBAR, _T("Failed to initialize ") UNRAR_DLL_NAME _T(". %s. %s"), (LPCTSTR)strFailure, sUnrar_download);
	}

	return m_hLibUnRar != NULL;
}

bool CRARFile::Open(LPCTSTR pszArchiveFilePath)
{
	if (!InitUnRarLib())
		return false;
	Close();

	m_strArchiveFilePath = pszArchiveFilePath;
	RAROpenArchiveDataEx OpenArchiveData = {};
	OpenArchiveData.ArcNameW = const_cast<LPWSTR>((LPCWSTR)m_strArchiveFilePath);
	OpenArchiveData.OpenMode = RAR_OM_EXTRACT;
	try {
		m_hArchive = (*m_pfnRAROpenArchiveEx)(&OpenArchiveData);
	} catch (...) {
		m_hArchive = NULL;
	}

	if (m_hArchive == NULL || OpenArchiveData.OpenResult != 0) {
		Close();
		return false;
	}
	return true;
}

void CRARFile::Close()
{
	if (m_hArchive) {
		ASSERT(m_pfnRARCloseArchive);
		if (m_pfnRARCloseArchive) {
			try {
				VERIFY((*m_pfnRARCloseArchive)(m_hArchive) == 0);
			} catch (...) {
			}
		}
		m_hArchive = NULL;
	}
	m_strArchiveFilePath.Empty();
}

bool CRARFile::GetNextFile(CString &strFile) const
{
	if (m_hLibUnRar == NULL || m_pfnRARReadHeaderEx == NULL || m_hArchive == NULL) {
		ASSERT(0);
		return false;
	}

	struct RARHeaderDataEx HeaderData = {};
	int iReadHeaderResult;
	try {
		iReadHeaderResult = (*m_pfnRARReadHeaderEx)(m_hArchive, &HeaderData);
	} catch (...) {
		return false;
	}
	if (iReadHeaderResult != 0)
		return false;
	strFile = HeaderData.FileNameW;
	return !strFile.IsEmpty();
}

bool CRARFile::Extract(LPCTSTR pszDstFilePath) const
{
	if (m_hLibUnRar == NULL || m_pfnRARProcessFileW == NULL || m_hArchive == NULL) {
		ASSERT(0);
		return false;
	}

	int iProcessFileResult;
	try {
		iProcessFileResult = (*m_pfnRARProcessFileW)(m_hArchive, RAR_EXTRACT, NULL, pszDstFilePath);
	} catch (...) {
		return false;
	}
	return !iProcessFileResult;
}

bool CRARFile::Skip() const
{
	if (m_hLibUnRar == NULL || m_pfnRARProcessFileW == NULL || m_hArchive == NULL) {
		ASSERT(0);
		return false;
	}

	int iProcessFileResult;
	try {
		iProcessFileResult = (*m_pfnRARProcessFileW)(m_hArchive, RAR_SKIP, NULL, NULL);
	} catch (...) {
		return false;
	}
	return !iProcessFileResult;
}
