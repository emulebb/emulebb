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
#include "StdAfx.h"
#include "collectionfile.h"
#include "CollectionSeams.h"
#include "Packets.h"
#include "Ed2kLink.h"
#include "FilenameTextRepairSeams.h"
#include "resource.h"
#include "Log.h"
#include "OtherFunctions.h"
#include "Kademlia/Kademlia/Entry.h"
#include "Kademlia/Kademlia/Tag.h"


#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

IMPLEMENT_DYNAMIC(CCollectionFile, CAbstractFile)

CCollectionFile::CCollectionFile(CFileDataIO &in_data)
{
	const uint32 nTagCount = in_data.ReadUInt32();
	if (!HasSaneCollectionFileTagCount(in_data.GetPosition(), in_data.GetLength(), nTagCount)) {
		DebugLogWarning(_T("Collection file import: rejected entry with malformed tag count %u"), nTagCount);
		throw CString(_T("malformed collection entry tag count"));
	}

	for (uint32 cnt = nTagCount; cnt > 0; --cnt)
		try {
			CTag *toadd = new CTag(in_data, true);
			m_taglist.Add(toadd);
		} catch (CFileException *ex) {
			DebugLogWarning(_T("Collection file import: skipped one unreadable entry tag - %s"), (LPCTSTR)CExceptionStr(*ex));
			ex->Delete();
		} catch (...) {
			DebugLogWarning(_T("Collection file import: skipped one malformed entry tag"));
			if (!ShouldSkipMalformedCollectionFileTag())
				throw;
		}

	CTag *pTagHash = GetTag(FT_FILEHASH);
	if (pTagHash)
		SetFileHash(pTagHash->GetHash());
	else {
		DebugLogWarning(_T("Collection file import: rejected entry without a file hash"));
		ASSERT(0);
		if (ShouldRejectCollectionFileWithoutHash())
			throw CString(_T("collection entry without file hash"));
	}

	pTagHash = GetTag(FT_AICH_HASH);
	if (pTagHash != NULL && pTagHash->IsStr()) {
		CAICHHash hash;
		if (DecodeBase32(pTagHash->GetStr(), hash) == CAICHHash::GetHashSize())
			m_FileIdentifier.SetAICHHash(hash);
		else {
			DebugLogWarning(_T("Collection file import: ignored invalid AICH hash tag"));
			ASSERT(0);
			if (!ShouldIgnoreInvalidCollectionAICHHash())
				throw CString(_T("invalid collection entry AICH hash"));
		}
	}

	// here we have two choices
	//	- if the server/client sent us a file type, we could use it (though it could be wrong)
	//	- we always trust our file type list and determine the file type by the file's extension
	//
	// if we received a file type from server, we use it.
	// if we did not receive a file type, we determine it by examining the file's extension.
	//
	// to avoid using 'wrong' file types for part files when adding a search result to the download queue,
	// in no case we will use the received file type (this has to be handled when creating the part files)
	const CString &rstrFileType(GetStrTagValue(FT_FILETYPE));
	const CString strOriginalFileName(GetStrTagValue(FT_FILENAME));
	const CString strRepairedFileName(FilenameTextRepairSeams::RepairIncomingCollectionFilename(strOriginalFileName));
	if (!strOriginalFileName.IsEmpty() && strRepairedFileName != strOriginalFileName)
		SetStrTagValue(FT_FILENAME, strRepairedFileName);
	CCollectionFile::SetAFileName(strRepairedFileName, false, rstrFileType.IsEmpty());
	CCollectionFile::SetFileSize(GetInt64TagValue(FT_FILESIZE));
	if (!rstrFileType.IsEmpty())
		if (rstrFileType == _T(ED2KFTSTR_PROGRAM)) {
			const CString &strDetailFileType(GetFileTypeByName(GetFileName()));
			CCollectionFile::SetFileType(strDetailFileType.IsEmpty() ? rstrFileType : strDetailFileType);
		} else
			CCollectionFile::SetFileType(rstrFileType);

	ASSERT((uint64)GetFileSize() && !GetFileName().IsEmpty());
}

CCollectionFile::CCollectionFile(CAbstractFile *pAbstractFile) : CAbstractFile(pAbstractFile)
{
	ClearTags();

	m_taglist.Add(new CTag(FT_FILEHASH, pAbstractFile->GetFileHash()));
	m_taglist.Add(new CTag(FT_FILESIZE, pAbstractFile->GetFileSize(), true));
	m_taglist.Add(new CTag(FT_FILENAME, pAbstractFile->GetFileName()));

	if (m_FileIdentifier.HasAICHHash())
		m_taglist.Add(new CTag(FT_AICH_HASH, m_FileIdentifier.GetAICHHash().GetString()));

	if (!pAbstractFile->GetFileComment().IsEmpty())
		m_taglist.Add(new CTag(FT_FILECOMMENT, pAbstractFile->GetFileComment()));

	if (pAbstractFile->GetFileRating())
		m_taglist.Add(new CTag(FT_FILERATING, pAbstractFile->GetFileRating()));

	CCollectionFile::UpdateFileRatingCommentAvail();
}

bool CCollectionFile::InitFromLink(const CString &sLink)
{
	CED2KLink *pLink = NULL;
	CED2KFileLink *pFileLink = NULL;
	try {
		pLink = CED2KLink::CreateLinkFromUrl(sLink);
		if (!pLink)
			throw GetResString(IDS_ERR_NOTAFILELINK);
		pFileLink = pLink->GetFileLink();
		if (!pFileLink)
			throw GetResString(IDS_ERR_NOTAFILELINK);
	} catch (const CString &error) {
		CString strBuffer;
		strBuffer.Format(GetResString(IDS_ERR_INVALIDLINK), (LPCTSTR)error);
		LogError(LOG_STATUSBAR, (LPCTSTR)GetResString(IDS_ERR_LINKERROR), (LPCTSTR)strBuffer);
		delete pLink;
		return false;
	}

	m_taglist.Add(new CTag(FT_FILEHASH, pFileLink->GetHashKey()));
	m_FileIdentifier.SetMD4Hash(pFileLink->GetHashKey());

	m_taglist.Add(new CTag(FT_FILESIZE, pFileLink->GetSize(), true));
	SetFileSize(pFileLink->GetSize());

	m_taglist.Add(new CTag(FT_FILENAME, pFileLink->GetName()));
	SetAFileName(pFileLink->GetName());

	if (pFileLink->HasValidAICHHash()) {
		m_taglist.Add(new CTag(FT_AICH_HASH, pFileLink->GetAICHHash().GetString()));
		m_FileIdentifier.SetAICHHash(pFileLink->GetAICHHash());
	}

	delete pLink;
	return true;
}

void CCollectionFile::WriteCollectionInfo(CFileDataIO &out_data)
{
	INT_PTR cnt = m_taglist.GetCount();
	out_data.WriteUInt32((uint32)cnt);

	for (INT_PTR i = 0; i < cnt; ++i) {
		CTag tempTag(*m_taglist[i]);
		tempTag.WriteNewEd2kTag(out_data, UTF8strRaw);
	}
}

void CCollectionFile::UpdateFileRatingCommentAvail(bool /*bForceUpdate*/)
{
	m_bHasComment = false;
	UINT uRatings = 0;
	UINT uUserRatings = 0;

	for (POSITION pos = m_kadNotes.GetHeadPosition(); pos != NULL;) {
		const Kademlia::CEntry *entry = m_kadNotes.GetNext(pos);
		if (!m_bHasComment && !entry->GetStrTagValue(Kademlia::CKadTagNameString(TAG_DESCRIPTION)).IsEmpty())
			m_bHasComment = true;
		UINT rating = (UINT)entry->GetIntTagValue(Kademlia::CKadTagNameString(TAG_FILERATING));
		if (rating != 0) {
			++uRatings;
			uUserRatings += rating;
		}
	}

	m_uUserRating = uRatings ? uUserRatings / uRatings : 0;
}
