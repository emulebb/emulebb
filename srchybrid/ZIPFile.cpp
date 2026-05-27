//
// ZIPFile.cpp
//
// Copyright (c) Shareaza Development Team, 2002-2004.
// This file is part of SHAREAZA (www.shareaza.com)
//
// Shareaza is free software; you can redistribute it
// and/or modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2 of
// the License, or (at your option) any later version.
//
// Shareaza is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Shareaza; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//

#include "StdAfx.h"
#include "ZIPFile.h"
#include "LongPathSeams.h"
#include "zlib.h"

#include <vector>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

static const DWORD kMaxZipCentralDirectoryBytes = 64u * 1024u * 1024u;

/////////////////////////////////////////////////////////////////////////////
// CZIPFile construction

CZIPFile::CZIPFile(HANDLE hAttach)
	: m_hFile(INVALID_HANDLE_VALUE)
	, m_pFile()
	, m_nFile()
	, m_bAttach()
{
	if (hAttach != INVALID_HANDLE_VALUE)
		Attach(hAttach);
}

CZIPFile::~CZIPFile()
{
	Close();
}

/////////////////////////////////////////////////////////////////////////////
// CZIPFile open

bool CZIPFile::Open(LPCTSTR pszFile)
{
	ASSERT(pszFile != NULL);

	Close();

	m_bAttach = false;
	m_hFile = LongPathSeams::CreateFile(pszFile, GENERIC_READ, FILE_SHARE_READ, NULL
						, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (m_hFile == INVALID_HANDLE_VALUE)
		return false;

	if (LocateCentralDirectory())
		return true;

	Close();
	return false;
}

/////////////////////////////////////////////////////////////////////////////
// CZIPFile attach

bool CZIPFile::Attach(HANDLE hFile)
{
	ASSERT(hFile != INVALID_HANDLE_VALUE);

	Close();

	m_bAttach = true;
	m_hFile = hFile;

	if (LocateCentralDirectory())
		return true;

	Close();
	return false;
}

/////////////////////////////////////////////////////////////////////////////
// CZIPFile open test

bool CZIPFile::IsOpen() const
{
	return m_hFile != INVALID_HANDLE_VALUE;
}

/////////////////////////////////////////////////////////////////////////////
// CZIPFile close

void CZIPFile::Close()
{
	if (IsOpen()) {
		if (!m_bAttach)
			::CloseHandle(m_hFile);
		m_hFile = INVALID_HANDLE_VALUE;
	}

	delete[] m_pFile;
	m_pFile = NULL;
	m_nFile = 0;
}

/////////////////////////////////////////////////////////////////////////////
// CZIPFile get a particular file

CZIPFile::File* CZIPFile::GetFile(int nFile) const
{
	return (nFile < 0 || nFile >= m_nFile) ? NULL : m_pFile + nFile;
}

/////////////////////////////////////////////////////////////////////////////
// CZIPFile lookup a file by name

CZIPFile::File* CZIPFile::GetFile(LPCTSTR pszFile, BOOL bPartial) const
{
	File *pFile = m_pFile;
	for (int nFile = m_nFile; --nFile >= 0;) {
		LPCTSTR pszName = bPartial ? _tcsrchr(pFile->m_sName, '/') : NULL;
		pszName = pszName ? pszName + 1 : (LPCTSTR)pFile->m_sName;
		if (_tcsicoll(pszName, pszFile) == 0)
			return pFile;
		++pFile;
	}
	return NULL;
}

/////////////////////////////////////////////////////////////////////////////
// CZIPFile locate the central directory

#pragma pack(push, 1)
typedef struct
{
	DWORD	nSignature;			// 0x06054b50
	WORD	nThisDisk;
	WORD	nDirectoryDisk;
	WORD	nFilesThisDisk;
	WORD	nTotalFiles;
	DWORD	nDirectorySize;
	DWORD	nDirectoryOffset;
	WORD	nCommentLen;
} ZIP_DIRECTORY_LOC;
#pragma pack(pop)

bool CZIPFile::LocateCentralDirectory()
{
	LARGE_INTEGER fileSize = {};
	if (!::GetFileSizeEx(m_hFile, &fileSize) || fileSize.QuadPart < static_cast<LONGLONG>(sizeof(ZIP_DIRECTORY_LOC)))
		return false;

	const ULONGLONG ullFileSize = static_cast<ULONGLONG>(fileSize.QuadPart);
	const ULONGLONG ullMaxSearchSize = sizeof(ZIP_DIRECTORY_LOC) + 0xffffull;
	const DWORD nSearchSize = static_cast<DWORD>(ullFileSize < ullMaxSearchSize ? ullFileSize : ullMaxSearchSize);

	LARGE_INTEGER searchOffset = {};
	searchOffset.QuadPart = fileSize.QuadPart - nSearchSize;
	if (!::SetFilePointerEx(m_hFile, searchOffset, NULL, FILE_BEGIN))
		return false;

	std::vector<BYTE> buffer(nSearchSize);
	DWORD nBuffer = 0;
	if (!::ReadFile(m_hFile, buffer.data(), nSearchSize, &nBuffer, NULL) || nBuffer < sizeof(ZIP_DIRECTORY_LOC))
		return false;

	const ZIP_DIRECTORY_LOC *pLoc = NULL;
	for (INT_PTR nScan = nBuffer - sizeof(ZIP_DIRECTORY_LOC) + 1; --nScan >= 0;) {
		const DWORD *pnSignature = reinterpret_cast<const DWORD*>(&buffer[static_cast<size_t>(nScan)]);
		if (*pnSignature == 0x06054b50) {
			pLoc = reinterpret_cast<const ZIP_DIRECTORY_LOC*>(pnSignature);
			break;
		}
	}
	if (pLoc == NULL)
		return false;
	ASSERT(pLoc->nSignature == 0x06054b50);

	if (pLoc->nThisDisk != 0 || pLoc->nDirectoryDisk != 0 || pLoc->nFilesThisDisk != pLoc->nTotalFiles)
		return false;

	const ULONGLONG ullDirectoryOffset = pLoc->nDirectoryOffset;
	const ULONGLONG ullDirectorySize = pLoc->nDirectorySize;
	if (ullDirectorySize > kMaxZipCentralDirectoryBytes
		|| ullDirectoryOffset > ullFileSize
		|| ullDirectorySize > ullFileSize - ullDirectoryOffset)
	{
		return false;
	}

	LARGE_INTEGER directoryOffset = {};
	directoryOffset.QuadPart = static_cast<LONGLONG>(ullDirectoryOffset);
	if (!::SetFilePointerEx(m_hFile, directoryOffset, NULL, FILE_BEGIN))
		return false;

	BYTE *pDirectory = new BYTE[pLoc->nDirectorySize];
	if (!::ReadFile(m_hFile, pDirectory, pLoc->nDirectorySize, &nBuffer, NULL))
		nBuffer = 0;

	if (nBuffer == pLoc->nDirectorySize) {
		m_nFile = (int)pLoc->nTotalFiles;
		m_pFile = new File[m_nFile];
		if (!ParseCentralDirectory(pDirectory, nBuffer)) {
			m_nFile = 0;
			delete[] m_pFile;
			m_pFile = NULL;
		}
	}
	delete[] pDirectory;

	return m_nFile > 0;
}

/////////////////////////////////////////////////////////////////////////////
// CZIPFile parse the central directory

#pragma pack(push, 1)
typedef struct
{
	DWORD	nSignature;		// 0x02014b50
	WORD	nWriteVersion;
	WORD	nReadVersion;
	WORD	nFlags;
	WORD	nCompression;
	WORD	nFileTime;
	WORD	nFileDate;
	DWORD	nCRC;
	DWORD	nCompressedSize;
	DWORD	nActualSize;
	WORD	nNameLen;
	WORD	nExtraLen;
	WORD	nCommentLen;
	WORD	nStartDisk;
	WORD	nInternalAttr;
	DWORD	nExternalAttr;
	DWORD	nLocalOffset;
} ZIP_CENTRAL_FILE;
#pragma pack(pop)

bool CZIPFile::ParseCentralDirectory(BYTE *pDirectory, DWORD nDirectory)
{
	for (int nFile = 0; nFile < m_nFile; ++nFile) {
		ZIP_CENTRAL_FILE *pRecord = (ZIP_CENTRAL_FILE*)pDirectory;

		if (nDirectory < sizeof *pRecord)
			return false;
		if (pRecord->nSignature != 0x02014b50)
			return false;

		pDirectory += sizeof *pRecord;
		nDirectory -= sizeof *pRecord;

		int nTailLen = (int)pRecord->nNameLen + (int)pRecord->nExtraLen + (int)pRecord->nCommentLen;
		if (nDirectory < (DWORD)nTailLen)
			return false;

		m_pFile[nFile].m_pZIP = this;
		m_pFile[nFile].m_nSize = pRecord->nActualSize;
		m_pFile[nFile].m_nLocalOffset = pRecord->nLocalOffset;
		m_pFile[nFile].m_nCompressedSize = pRecord->nCompressedSize;
		m_pFile[nFile].m_nCompression = pRecord->nCompression;

		LPTSTR pszName = m_pFile[nFile].m_sName.GetBuffer(pRecord->nNameLen);

		for (WORD nChar = 0; nChar < pRecord->nNameLen; ++nChar) {
			pszName[nChar] = (TCHAR)pDirectory[nChar];
			if (pszName[nChar] == '\\')
				pszName[nChar] = '/';
		}

		m_pFile[nFile].m_sName.ReleaseBuffer(pRecord->nNameLen);

		pDirectory += (DWORD)nTailLen;
		nDirectory -= (DWORD)nTailLen;
	}

	return true;
}

/////////////////////////////////////////////////////////////////////////////
// CZIPFile::File seek to a file

#pragma pack(push, 1)
typedef struct
{
	DWORD	nSignature;		// 0x04034b50
	WORD	nVersion;
	WORD	nFlags;
	WORD	nCompression;
	WORD	nFileTime;
	WORD	nFileDate;
	DWORD	nCRC;
	DWORD	nCompressedSize;
	DWORD	nActualSize;
	WORD	nNameLen;
	WORD	nExtraLen;
} ZIP_LOCAL_FILE;
#pragma pack(pop)

bool CZIPFile::SeekToFile(const File *pFile)
{
	ASSERT(pFile != NULL && pFile->m_pZIP == this);
	if (m_hFile == INVALID_HANDLE_VALUE)
		return false;

	if (SetFilePointer(m_hFile, (DWORD)pFile->m_nLocalOffset, NULL, FILE_BEGIN) != pFile->m_nLocalOffset)
		return false;

	ZIP_LOCAL_FILE pLocal;
	DWORD nRead;
	VERIFY(::ReadFile(m_hFile, &pLocal, sizeof pLocal, &nRead, NULL));
	if (nRead != sizeof pLocal)
		return false;

	if (pLocal.nSignature != 0x04034b50)
		return false;
	if (pLocal.nCompression != Z_DEFLATED && pLocal.nCompression != 0)
		return false;

	SetFilePointer(m_hFile, pLocal.nNameLen + pLocal.nExtraLen, NULL, FILE_CURRENT);

	return true;
}

/////////////////////////////////////////////////////////////////////////////
// CZIPFile::File prepare to decompress

bool CZIPFile::File::PrepareToDecompress(LPVOID pStream)
{
	memset(pStream, 0, sizeof z_stream);

	if (m_pZIP->SeekToFile(this)) {
		if (m_nCompression == 0)
			return m_nSize == m_nCompressedSize;

		if (m_nCompression == Z_DEFLATED)
			return Z_OK == inflateInit2((z_stream*)pStream, -MAX_WBITS);
	}
	return false;
}

/////////////////////////////////////////////////////////////////////////////
// CZIPFile::File decompress to memory

/*CBuffer* CZIPFile::File::Decompress()
{
	z_stream pStream;

	if (m_nSize > 32 * 1024 * 1024)
		return NULL;
	if (!PrepareToDecompress(&pStream))
		return NULL;

	if (m_nCompression == 0) {
		CBuffer *pTarget = new CBuffer();
		pTarget->EnsureBuffer((DWORD)m_nSize);
		::ReadFile(m_pZIP->m_hFile, pTarget->m_pBuffer, (DWORD)m_nSize, &pTarget->m_nLength, NULL);
		if (pTarget->m_nLength == (DWORD)m_nSize)
			return pTarget;
		delete pTarget;
		return NULL;
	}

	DWORD nSource = (DWORD)m_nCompressedSize;
	DWORD rSource;
	BYTE *pSource = new BYTE[nSource];
	::ReadFile(m_pZIP->m_hFile, pSource, nSource, &rSource, NULL);

	if (nSource != rSource) {
		inflateEnd(&pStream);
		delete[] pSource;
		return NULL;
	}

	CBuffer *pTarget = new CBuffer();
	pTarget->EnsureBuffer((DWORD)m_nSize);
	pTarget->m_nLength = (DWORD)m_nSize;

	pStream.next_in = pSource;
	pStream.avail_in = (DWORD)m_nCompressedSize;
	pStream.next_out = pTarget->m_pBuffer;
	pStream.avail_out = pTarget->m_nLength;

	inflate(&pStream, Z_FINISH);

	delete[] pSource;

	if (pStream.avail_out != 0) {
		delete pTarget;
		pTarget = NULL;
	}

	inflateEnd(&pStream);

	return pTarget;
}*/

/////////////////////////////////////////////////////////////////////////////
// CZIPFile::File decompress to disk

#define BUFFER_IN_SIZE		(64*1024)
#define BUFFER_OUT_SIZE		(128*1024)

bool CZIPFile::File::Extract(LPCTSTR pszFile)
{
	z_stream pStream;
	HANDLE hFile;

	hFile = LongPathSeams::CreateFile(pszFile, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	if (!PrepareToDecompress(&pStream)) {
		::CloseHandle(hFile);
		LongPathSeams::DeleteFileIfExists(pszFile);
		return false;
	}

	uint64 nUncompressed = 0;
	BYTE *pBufferOut = new BYTE[BUFFER_OUT_SIZE];

	if (m_nCompression == Z_DEFLATED) {
		BYTE *pBufferIn = new BYTE[BUFFER_IN_SIZE];
		bool bStreamComplete = false;

		for (uint64 nCompressed = 0; nCompressed < m_nCompressedSize || nUncompressed < m_nSize;) {
			if (pStream.avail_in == 0) {
				pStream.avail_in = (DWORD)min(m_nCompressedSize - nCompressed, BUFFER_IN_SIZE);
				pStream.next_in = pBufferIn;

				DWORD nRead;
				VERIFY(::ReadFile(m_pZIP->m_hFile, pBufferIn, pStream.avail_in, &nRead, NULL));
				if (nRead != pStream.avail_in)
					break;
				nCompressed += nRead;
			}

			pStream.avail_out = BUFFER_OUT_SIZE;
			pStream.next_out = pBufferOut;

			const uInt nAvailInBefore = pStream.avail_in;
			const int nInflateResult = inflate(&pStream, Z_SYNC_FLUSH);
			if (nInflateResult == Z_MEM_ERROR)
				break;

			if (pStream.avail_out < BUFFER_OUT_SIZE) {
				DWORD nWrite = BUFFER_OUT_SIZE - pStream.avail_out;
				DWORD nWritten;
				if (static_cast<uint64>(nWrite) > m_nSize - nUncompressed)
					break;
				if (!::WriteFile(hFile, pBufferOut, nWrite, &nWritten, NULL) || nWritten != nWrite)
					break;
				nUncompressed += nWrite;
			}

			if (nInflateResult == Z_STREAM_END) {
				bStreamComplete = true;
				break;
			}
			if (nInflateResult != Z_OK)
				break;
			if (nAvailInBefore == pStream.avail_in && pStream.avail_out == BUFFER_OUT_SIZE)
				break;
		}

		delete[] pBufferIn;

		inflateEnd(&pStream);
		if (!bStreamComplete)
			nUncompressed = 0;
	} else
		while (nUncompressed < m_nSize) {
			DWORD nChunk = (DWORD)min(m_nSize - nUncompressed, BUFFER_OUT_SIZE);
			DWORD nRead;
			DWORD nWritten;

			VERIFY(::ReadFile(m_pZIP->m_hFile, pBufferOut, nChunk, &nRead, NULL));
			if (nChunk != nRead)
				break;
			if (!::WriteFile(hFile, pBufferOut, nChunk, &nWritten, NULL) || nChunk != nWritten)
				break;
			nUncompressed += nChunk;
		}

	delete[] pBufferOut;
	::CloseHandle(hFile);

	if (nUncompressed >= m_nSize)
		return true;

	LongPathSeams::DeleteFileIfExists(pszFile);
	return false;
}
