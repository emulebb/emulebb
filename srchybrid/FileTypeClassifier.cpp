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
#include "FileTypeClassifierSeams.h"
#include "PartFile.h"
#include "SafeFile.h"
#include "ShareableFile.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

EFileType GetFileTypeEx(CShareableFile *kfile, bool checkextention, bool checkfileheader, bool nocached)
{
	if (!nocached && kfile->GetVerifiedFileType() != FILETYPE_UNKNOWN)
		return kfile->GetVerifiedFileType();

	EFileType eResult = FILETYPE_UNKNOWN;
	CPartFile *pPartFile = static_cast<CPartFile*>(kfile);
	const size_t uHeaderCheckSize = FileTypeClassifierSeams::kHeaderCheckSize;

	const bool bTestIso = !kfile->IsPartFile()
		|| (static_cast<uint64>(pPartFile->GetCompletedSize()) > FileTypeClassifierSeams::kIsoHeaderOffset + uHeaderCheckSize
			&& pPartFile->IsCompleteBD(FileTypeClassifierSeams::kIsoHeaderOffset, FileTypeClassifierSeams::kIsoHeaderOffset + uHeaderCheckSize));
	if (checkfileheader && (!kfile->IsPartFile() || pPartFile->IsCompleteBDSafe(0, uHeaderCheckSize) || bTestIso)) {
		try {
			CSafeFile inFile;
			if (LongPathSeams::OpenFile(inFile, kfile->GetFilePath(), CFile::modeRead | CFile::shareDenyNone)) {
				BYTE aucHeader[FileTypeClassifierSeams::kHeaderCheckSize] = {};
				if (!kfile->IsPartFile() || pPartFile->IsCompleteBDSafe(0, uHeaderCheckSize)) {
					const int iRead = inFile.Read(aucHeader, static_cast<UINT>(uHeaderCheckSize));
					if (static_cast<size_t>(iRead) == uHeaderCheckSize)
						eResult = FileTypeClassifierSeams::DetectFileTypeFromHeader(aucHeader, uHeaderCheckSize, kfile->GetFileName());
				}
				if (eResult == FILETYPE_UNKNOWN && bTestIso) {
					inFile.Seek(FileTypeClassifierSeams::kIsoHeaderOffset, CFile::begin);
					const int iRead = inFile.Read(aucHeader, static_cast<UINT>(uHeaderCheckSize));
					if (static_cast<size_t>(iRead) == uHeaderCheckSize)
						eResult = FileTypeClassifierSeams::DetectIsoTypeFromOffsetHeader(aucHeader, uHeaderCheckSize);
				}
				inFile.Close();
			}
		} catch (...) {
			ASSERT(0);
			return FILETYPE_UNKNOWN;
		}

		if (eResult != FILETYPE_UNKNOWN) {
			kfile->SetVerifiedFileType(eResult);
			return eResult;
		}
	}

	if (!checkextention)
		return eResult;
	return FileTypeClassifierSeams::GetFileTypeFromExtension(kfile->GetFileName());
}

CString GetFileTypeName(EFileType ftype)
{
	return FileTypeClassifierSeams::GetFileTypeLabel(ftype);
}

int IsExtensionTypeOf(EFileType ftype, LPCTSTR const pszExt)
{
	return FileTypeClassifierSeams::IsExtensionTypeOf(ftype, pszExt);
}
