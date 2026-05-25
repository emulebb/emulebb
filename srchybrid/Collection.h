//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( merkur-@users.sourceforge.net / https://www.emule-project.net )
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
#pragma once
#include "MapKey.h"
#include <rsa.h>
#include <base64.h>
#include <osrng.h>
#include <files.h>

#define COLLECTION_FILEEXTENSION	_T(".emulecollection")

class CAbstractFile;
class CCollectionFile;

typedef CMap<CSKey, const CSKey&, CCollectionFile*, CCollectionFile*> CCollectionFilesMap;

class CCollection
{
	friend class CCollectionCreateDialog;
	friend class CCollectionViewDialog;
public:
	CCollection();
	explicit CCollection(const CCollection *pCollection);
	~CCollection();
	CCollection(const CCollection&) = delete;
	CCollection& operator=(const CCollection&) = delete;
	bool	InitCollectionFromFile(const CString &sFilePath, const CString &sFileName);
	CCollectionFile* AddFileToCollection(CAbstractFile *pAbstractFile, bool bCreateClone);
	void	RemoveFileFromCollection(const CAbstractFile *pAbstractFile);
	bool	ContainsFilePointer(const CAbstractFile *pAbstractFile) const;
	void	WriteToFileAddShared(CryptoPP::RSASSA_PKCS1v15_SHA_Signer *pSignKey = NULL);
	void	SetCollectionAuthorKey(const byte *abyCollectionAuthorKey, uint32 nSize);
	CString	GetCollectionAuthorKeyString();
	CString	GetAuthorKeyHashString() const;
	static bool HasCollectionExtention(const CString &sFileName);

	CString m_sCollectionName;
	CString m_sCollectionAuthorName;

	bool m_bTextFormat;

private:
	/**
	 * @brief Adopts an imported collection file only after it is accepted by the collection map.
	 */
	bool	AddImportedFileToCollection(CCollectionFile *pCollectionFile);

	uint32	m_nKeySize;
	CCollectionFilesMap m_CollectionFilesMap;
	byte	*m_pabyCollectionAuthorKey;
};
