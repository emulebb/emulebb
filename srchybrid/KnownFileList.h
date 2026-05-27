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
#pragma once
#include "KnownFileLookupIndex.h"
#include "MapKey.h"
#include "SHAHashset.h"
#include <unordered_set>

class CKnownFile;
typedef CMap<CCKey, const CCKey&, CKnownFile*, CKnownFile*> CKnownFilesMap;
typedef CMap<CSKey, const CSKey&, int, int> CancelledFilesMap;
typedef CMap<CAICHHash, const CAICHHash&, const CKnownFile*, const CKnownFile*> KnonwFilesByAICHMap;

class CKnownFileList
{
	friend class CFileDetailDlgStatistics;
	friend class CStatisticFile;
public:
	CKnownFileList();
	~CKnownFileList();

	bool	SafeAddKFile(CKnownFile *toadd);
	bool	Init();
	void	Save();
	void	Clear();
	void	Process();

	CKnownFile* FindKnownFile(LPCTSTR filename, time_t date, uint64 size) const;
	CKnownFile* FindKnownFileByID(const uchar *hash) const;
	CKnownFile* FindKnownFileByPath(const CString &sFilePath) const;
	bool	IsKnownFile(const CKnownFile *file) const;
	bool	IsFilePtrInList(const CKnownFile *file) const;
	bool	ContainsFilePointer(const CKnownFile *file) const;
	void	InvalidateLookupIndex();

	void	AddCancelledFileID(const uchar *hash);
	bool	IsCancelledFileByID(const uchar *hash) const;

	const CKnownFilesMap& GetKnownFiles() const		{ return m_Files_map; }
	void	CopyKnownFileMap(CKnownFilesMap &Files_Map);

	bool	ShouldPurgeAICHHashset(const CAICHHash &rAICHHash) const;
	void	AICHHashChanged(const CAICHHash *pOldAICHHash, const CAICHHash &rNewAICHHash, CKnownFile *pFile);

	uint64	m_nTransferredTotal;
	uint32	m_nRequestedTotal;
	uint32	m_nAcceptedTotal;

private:
	bool	LoadKnownFiles();
	bool	LoadCancelledFiles();
	void	AddToLookupIndex(CKnownFile *pFile);
	void	RemoveFromLookupIndex(const CKnownFile *pFile);
	void	RebuildLookupIndex();

	uint64	transferred;
	CKnownFilesMap		m_Files_map;
	std::unordered_set<const CKnownFile*> m_filePointers;
	CancelledFilesMap	m_mapCancelledFiles;
	// map of files is indexed by AICH-hash for faster access,
	// not guaranteed to be complete at this point (!)
	// (files which got AICH hashed later will not be added yet, because we don't need them,
	// make sure to change this if needed)
	KnonwFilesByAICHMap m_mapKnownFilesByAICH;
	TKnownFileLookupIndex<CKnownFile*> m_lookupIndex;
	bool	m_bLookupIndexDirty;
	uint32	m_dwCancelledFilesSeed;
	ULONGLONG m_nLastSaved;
	uint16	requested;
	uint16	accepted;
};
