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
#include <io.h>
#include "zlib.h"
#include "GZipFile.h"
#include "LongPathSeams.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

CGZIPFile::CGZIPFile()
	: m_gzFile()
{
}

bool CGZIPFile::Open(LPCTSTR pszFilePath)
{
	ASSERT(m_gzFile == 0);
	Close();

	const int fdIn = LongPathSeams::OpenCrtReadOnlyLongPath(pszFilePath);
	if (fdIn == -1)
		return false;

	m_gzFile = gzdopen(fdIn, "rb");
	if (m_gzFile == 0)
		_close(fdIn);
	if (m_gzFile) {
		// Use gzip-uncompress only for real gzip-compressed files and do not let handle it also uncompressed files.
		// This way the 'Open' function can be used to check if that file is a 'gzip' file at all.
		if (gzdirect(m_gzFile) != 0)
			Close();
		else
			m_strGzFilePath = pszFilePath;
	}
	return m_gzFile != 0;
}

void CGZIPFile::Close()
{
	if (m_gzFile) {
		VERIFY(gzclose(m_gzFile) == Z_OK);
		m_gzFile = 0;
	}
	m_strGzFilePath.Empty();
}

CString CGZIPFile::GetUncompressedFilePath() const
{
	// return path of input file without ".gz" extension
	LPCTSTR pDot = ::PathFindExtension(m_strGzFilePath);
	if (_tcsicmp(pDot, _T(".gz")) != 0)
		pDot = m_strGzFilePath; //return an empty string if not .gz
	return m_strGzFilePath.Left((int)((LPCTSTR)m_strGzFilePath - pDot));
}

CString CGZIPFile::GetUncompressedFileName() const
{
	// return name (without path) of input file without ".gz" extension
	const CString &strUncompressedFileName = GetUncompressedFilePath();
	if (!strUncompressedFileName.IsEmpty()) {
		// skip any possible available directories
		LPCTSTR pszFileName = ::PathFindFileName(strUncompressedFileName);
		if (pszFileName)
			return CString(pszFileName);
	}
	return strUncompressedFileName;
}

bool CGZIPFile::Extract(LPCTSTR pszFilePath, ULONGLONG ullMaxOutputBytes)
{
	const int fdOut = LongPathSeams::OpenCrtWriteOnlyLongPath(pszFilePath, CREATE_ALWAYS, FILE_SHARE_READ);
	if (fdOut == -1)
		return false;

	bool bResult = true;
	ULONGLONG ullWrittenTotal = 0;
	const int iBuffSize = 32768;
	BYTE *pucBuff = new BYTE[iBuffSize];
	while (!gzeof(m_gzFile)) {
		int iRead = gzread(m_gzFile, pucBuff, iBuffSize);
		if (iRead == 0)
			break;
		if (iRead < 0) {
			bResult = false;
			break;
		}
		if (ullMaxOutputBytes != 0
			&& static_cast<ULONGLONG>(iRead) > ullMaxOutputBytes - ullWrittenTotal)
		{
			bResult = false;
			break;
		}
		if (_write(fdOut, pucBuff, iRead) != iRead) {
			bResult = false;
			break;
		}
		ullWrittenTotal += static_cast<ULONGLONG>(iRead);
	}
	delete[] pucBuff;
	_close(fdOut);
	if (!bResult)
		VERIFY(LongPathSeams::DeleteFileIfExists(pszFilePath));
	return bResult;
}
