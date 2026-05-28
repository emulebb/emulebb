//this file is part of eMule
//Copyright (C)2005-2026 Merkur ( devs@emule-project.net / https://www.emule-project.net )
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
#pragma once

#define ERAR_END_ARCHIVE	10
#define ERAR_NO_MEMORY		11
#define ERAR_BAD_DATA		12
#define ERAR_BAD_ARCHIVE	13
#define ERAR_UNKNOWN_FORMAT	14
#define ERAR_EOPEN			15
#define ERAR_ECREATE		16
#define ERAR_ECLOSE			17
#define ERAR_EREAD			18
#define ERAR_EWRITE			19
#define ERAR_SMALL_BUF		20
#define ERAR_UNKNOWN		21
#define ERAR_MISSING_PASSWORD	22
#define ERAR_EREFERENCE		23
#define ERAR_BAD_PASSWORD	24
#define ERAR_LARGE_DICT		25

#define RAR_OM_LIST			0
#define RAR_OM_EXTRACT		1
#define RAR_OM_LIST_INCSPLIT	2

#define RAR_SKIP			0
#define RAR_TEST			1
#define RAR_EXTRACT			2

#define RAR_VOL_ASK			0
#define RAR_VOL_NOTIFY		1

#define RAR_DLL_VERSION		9

#define RAR_HASH_NONE		0
#define RAR_HASH_CRC32		1
#define RAR_HASH_BLAKE2		2

#define RHDF_SPLITBEFORE	0x01
#define RHDF_SPLITAFTER		0x02
#define RHDF_ENCRYPTED		0x04
#define RHDF_SOLID			0x10
#define RHDF_DIRECTORY		0x20

#define ROADF_VOLUME		0x0001
#define ROADF_COMMENT		0x0002
#define ROADF_LOCK			0x0004
#define ROADF_SOLID			0x0008
#define ROADF_NEWNUMBERING	0x0010
#define ROADF_SIGNED		0x0020
#define ROADF_RECOVERY		0x0040
#define ROADF_ENCHEADERS	0x0080
#define ROADF_FIRSTVOLUME	0x0100

#define ROADOF_KEEPBROKEN	0x0001

#ifdef _WIN64
#define UNRAR_DLL_NAME		_T("UnRAR64.dll")
#define UNRAR_DLL_INSTALL_HINT	_T("%ProgramFiles(x86)%\\UnrarDLL\\x64\\UnRAR64.dll")
#else
#define UNRAR_DLL_NAME		_T("UnRAR.dll")
#define UNRAR_DLL_INSTALL_HINT	_T("%ProgramFiles(x86)%\\UnrarDLL\\UnRAR.dll")
#endif

typedef int (CALLBACK *UNRARCALLBACK)(UINT msg, LPARAM UserData, LPARAM P1, LPARAM P2);

#pragma pack(push, 1)
struct RAROpenArchiveDataEx
{
	char   *ArcName;
	WCHAR  *ArcNameW;
	DWORD	OpenMode;
	DWORD	OpenResult;
	char   *CmtBuf;
	DWORD	CmtBufSize;
	DWORD	CmtSize;
	DWORD	CmtState;
	DWORD	Flags;
	UNRARCALLBACK Callback;
	LPARAM	UserData;
	DWORD	OpFlags;
	WCHAR	*CmtBufW;
	WCHAR	*MarkOfTheWeb;
	DWORD	Reserved[23];
};
#pragma pack(pop)

#pragma pack(push, 1)
struct RARHeaderDataEx
{
	char	ArcName[1024];
	WCHAR	ArcNameW[1024];
	char	FileName[1024];
	WCHAR	FileNameW[1024];
	DWORD	Flags;
	DWORD	PackSize;
	DWORD	PackSizeHigh;
	DWORD	UnpSize;
	DWORD	UnpSizeHigh;
	DWORD	HostOS;
	DWORD	FileCRC;
	DWORD	FileTime;
	DWORD	UnpVer;
	DWORD	Method;
	DWORD	FileAttr;
	char	*CmtBuf;
	DWORD	CmtBufSize;
	DWORD	CmtSize;
	DWORD	CmtState;
	DWORD	DictSize;
	DWORD	HashType;
	char	Hash[32];
	DWORD	RedirType;
	WCHAR	*RedirName;
	DWORD	RedirNameSize;
	DWORD	DirTarget;
	DWORD	MtimeLow;
	DWORD	MtimeHigh;
	DWORD	CtimeLow;
	DWORD	CtimeHigh;
	DWORD	AtimeLow;
	DWORD	AtimeHigh;
	WCHAR	*ArcNameEx;
	DWORD	ArcNameExSize;
	WCHAR	*FileNameEx;
	DWORD	FileNameExSize;
	DWORD	Reserved[982];
};
#pragma pack(pop)

class CRARFile
{
public:
	static constexpr ULONGLONG kDefaultMaxExtractBytes = 512ull * 1024ull * 1024ull;

	CRARFile();
	~CRARFile();

	bool Open(LPCTSTR pszArchiveFilePath);
	void Close();
	bool GetNextFile(CString &strFile) const;
	bool Extract(LPCTSTR pszDstFilePath, ULONGLONG ullMaxOutputBytes = kDefaultMaxExtractBytes) const;
	bool Skip() const;

	static LPCTSTR sUnrar_download;

protected:
	CString m_strArchiveFilePath;
	HMODULE m_hLibUnRar;
	HANDLE m_hArchive;
	mutable ULONGLONG m_ullCurrentFileUnpackedSize;

	bool InitUnRarLib();
	HANDLE(WINAPI *m_pfnRAROpenArchiveEx)(struct RAROpenArchiveDataEx *ArchiveData) noexcept(false);
	int (WINAPI *m_pfnRARCloseArchive)(HANDLE hArcData) noexcept(false);
	int (WINAPI *m_pfnRARReadHeaderEx)(HANDLE hArcData, struct RARHeaderDataEx *HeaderData) noexcept(false);
	int (WINAPI *m_pfnRARProcessFileW)(HANDLE hArcData, int iOperation, const WCHAR *pszDestFolder, const WCHAR *pszDestName) noexcept(false);
	int (WINAPI *m_pfnRARGetDllVersion)() noexcept(false);
};
