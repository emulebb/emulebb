// parts of this file are based on work from pan One (http://home-3.tiscali.nl/~meost/pms/)
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
#include "SearchFile.h"
#include "OtherFunctions.h"
#include "opcodes.h"
#include "Packets.h"
#include "Preferences.h"
#include "FilenameTextRepairSeams.h"
#include "Kademlia/Kademlia/Entry.h"
#include "emule.h"
#include "emuledlg.h"
#include "Searchdlg.h"
#ifdef _DEBUG
#include "DebugHelpers.h"
#endif

#include <memory>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


static const uint32 MAX_SEARCH_RESULT_TAGS = 256;

bool IsValidSearchResultClientIPPort(uint32 nIP, uint16 nPort)
{
	return	(nIP & 0xFF) && nPort;
/*	return	   nIP != 0
			&& nPort != 0
			&& (ntohl(nIP) != nPort)		// this filters most of the false data
			&& ((nIP & 0x000000FF) != 0)
			&& ((nIP & 0x0000FF00) != 0)
			&& ((nIP & 0x00FF0000) != 0)
			&& ((nIP & 0xFF000000) != 0);*/
}

static bool HasSaneSearchResultTagCount(CFileDataIO &data, uint32 tagcount)
{
	const ULONGLONG uPosition = data.GetPosition();
	const ULONGLONG uLength = data.GetLength();
	return uPosition <= uLength
		&& tagcount <= MAX_SEARCH_RESULT_TAGS
		&& static_cast<ULONGLONG>(tagcount) <= (uLength - uPosition);
}

void ConvertED2KTag(CTag *&pTag)
{
	if (pTag->GetNameID() == 0 && pTag->HasName()) {
		static const struct
		{
			uint8	nID;
			uint8	nED2KType;
			LPCSTR	pszED2KName;
		} _aEmuleToED2KMetaTagsMap[] =
		{
			// Artist, Album and Title are disabled because they should be already part of the filename
			// and would therefore be redundant information sent to the servers. And the servers count the
			// amount of sent data!
			{ FT_MEDIA_ARTIST,  TAGTYPE_STRING, FT_ED2K_MEDIA_ARTIST },
			{ FT_MEDIA_ALBUM,   TAGTYPE_STRING, FT_ED2K_MEDIA_ALBUM },
			{ FT_MEDIA_TITLE,   TAGTYPE_STRING, FT_ED2K_MEDIA_TITLE },
			{ FT_MEDIA_LENGTH,  TAGTYPE_STRING, FT_ED2K_MEDIA_LENGTH },
			{ FT_MEDIA_LENGTH,  TAGTYPE_UINT32, FT_ED2K_MEDIA_LENGTH },
			{ FT_MEDIA_BITRATE, TAGTYPE_UINT32, FT_ED2K_MEDIA_BITRATE },
			{ FT_MEDIA_CODEC,   TAGTYPE_STRING, FT_ED2K_MEDIA_CODEC }
		};

		for (unsigned j = 0; j < _countof(_aEmuleToED2KMetaTagsMap); ++j) {
			if (    CmpED2KTagName(pTag->GetName(), _aEmuleToED2KMetaTagsMap[j].pszED2KName) == 0
				&& (	(pTag->IsStr() && _aEmuleToED2KMetaTagsMap[j].nED2KType == TAGTYPE_STRING)
					||	(pTag->IsInt() && _aEmuleToED2KMetaTagsMap[j].nED2KType == TAGTYPE_UINT32)))
			{
				CTag *tag = NULL;
				if (pTag->IsStr()) {
					if (_aEmuleToED2KMetaTagsMap[j].nID == FT_MEDIA_LENGTH) {
						UINT nMediaLength = 0;
						UINT hour = 0, min = 0, sec = 0;
						if (_stscanf_s(pTag->GetStr(), _T("%u : %u : %u"), &hour, &min, &sec) == 3)
							nMediaLength = HR2S(hour) + MIN2S(min) + sec;
						else if (_stscanf_s(pTag->GetStr(), _T("%u : %u"), &min, &sec) == 2)
							nMediaLength = MIN2S(min) + sec;
						else if (_stscanf_s(pTag->GetStr(), _T("%u"), &sec) == 1)
							nMediaLength = sec;

						if (nMediaLength != 0)
							tag = new CTag(_aEmuleToED2KMetaTagsMap[j].nID, nMediaLength);
					} else if (!pTag->GetStr().IsEmpty())
						tag = new CTag(_aEmuleToED2KMetaTagsMap[j].nID, pTag->GetStr());
					delete pTag;
					pTag = tag;
				} else if (pTag->IsInt()) {
					if (pTag->GetInt() != 0)
						tag = new CTag(_aEmuleToED2KMetaTagsMap[j].nID, pTag->GetInt());
					delete pTag;
					pTag = tag;
				}
				break;
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
// CSearchFile

IMPLEMENT_DYNAMIC(CSearchFile, CAbstractFile)

CSearchFile::CSearchFile(const CSearchFile *copyfrom)
	: CAbstractFile(copyfrom)
	, m_list_childcount()
	, m_bPreviewPossible()
	, m_list_bExpanded()
{
	CSearchFile::UpdateFileRatingCommentAvail();

	m_nClientServerIP = copyfrom->GetClientServerIP();
	m_nClientServerPort = copyfrom->GetClientServerPort();
	m_nClientID = copyfrom->GetClientID();
	m_nClientPort = copyfrom->GetClientPort();
	m_pszDirectory = copyfrom->GetDirectory() ? _tcsdup(copyfrom->GetDirectory()) : NULL;
	m_nSearchID = copyfrom->GetSearchID();
	m_bKademlia = copyfrom->IsKademlia();

	const CSimpleArray<SClient> &clients = copyfrom->GetClients();
	for (int i = 0; i < clients.GetSize(); ++i)
		AddClient(clients[i]);

	const CSimpleArray<SServer> &servers = copyfrom->GetServers();
	for (int i = 0; i < servers.GetSize(); ++i)
		AddServer(servers[i]);

	m_list_parent = const_cast<CSearchFile*>(copyfrom);
	m_eKnown = copyfrom->m_eKnown;
	m_strNameWithoutKeywords = copyfrom->GetNameWithoutKeyword();
	m_bServerUDPAnswer = copyfrom->m_bServerUDPAnswer;
	m_nSpamRating = copyfrom->GetSpamRating();
	m_nKadPublishInfo = copyfrom->GetKadPublishInfo();
	m_bMultipleAICHFound = copyfrom->m_bMultipleAICHFound;
	m_flags = copyfrom->m_flags;
	m_nSources = copyfrom->m_nSources;
	m_nCompleteSources = copyfrom->m_nCompleteSources;
	for (INT_PTR i = 0; i < copyfrom->m_astrObservedNames.GetCount(); ++i)
		AddObservedName(copyfrom->m_astrObservedNames[i]);
}

CSearchFile::CSearchFile(CFileDataIO &in_data, bool bOptUTF8, uint32 nSearchID, uint32 nServerIP,
			uint16 nServerPort, LPCTSTR pszDirectory, bool bKademlia, bool bServerUDPAnswer)
	: m_bMultipleAICHFound()
	, m_bKademlia(bKademlia)
	, m_bServerUDPAnswer(bServerUDPAnswer)
	, m_flags()
	, m_nSearchID(nSearchID)
	, m_nSources()
	, m_nCompleteSources()
	, m_nKadPublishInfo()
	, m_nSpamRating()
	, m_list_childcount()
	, m_list_parent()
	, m_pszDirectory()
	, m_eKnown(NotDetermined)
	, m_bPreviewPossible()
	, m_list_bExpanded()
{
	m_FileIdentifier.SetMD4Hash(in_data);
	m_nClientID = in_data.ReadUInt32();
	m_nClientPort = in_data.ReadUInt16();
	if (!IsValidSearchResultClientIPPort(m_nClientID, m_nClientPort)) {
		if (thePrefs.GetDebugServerSearchesLevel() > 1)
			Debug(_T("Filtered source from search result %s:%u\n"), (LPCTSTR)DbgGetClientID(m_nClientID), m_nClientPort);
		m_nClientID = 0;
		m_nClientPort = 0;
	}

	m_nClientServerIP = nServerIP;
	m_nClientServerPort = nServerPort;
	if (m_nClientServerIP && m_nClientServerPort) {
		SServer server(m_nClientServerIP, m_nClientServerPort, bServerUDPAnswer);
		server.m_uAvail = GetIntTagValue(FT_SOURCES);
		AddServer(server);
	}
	uint32 tagcount = in_data.ReadUInt32();
	if (!HasSaneSearchResultTagCount(in_data, tagcount)) {
		// WHY: tagcount is remote-controlled and each tag parser may allocate.
		// Bound both the count and the minimum possible remaining bytes before
		// entering the loop so hostile search results cannot drive unbounded tag
		// construction or parse past EOF.
		CString strError;
		strError.Format(_T("Malformed search result tag count: %u"), tagcount);
		throw strError;
	}
	std::unique_ptr<TCHAR, decltype(&free)> directoryOwner(NULL, &free);
	if (pszDirectory != NULL) {
		directoryOwner.reset(_tcsdup(pszDirectory));
		m_pszDirectory = directoryOwner.get();
	}
	// NSERVER2.EXE (lugdunum v16.38 patched for Win32) returns the ClientIP+Port of the client which offered that
	// file, even if that client has not filled the according fields in the OP_OFFERFILES packet with its IP+Port.
	//
	// 16.38.p73 (lugdunum) (propenprinz)
	//  *) does not return ClientIP+Port if the OP_OFFERFILES packet does not also contain it.
	//  *) if the OP_OFFERFILES packet does contain our HighID and Port the server returns that data at least when
	//     returning search results via TCP.
	if (thePrefs.GetDebugServerSearchesLevel() > 1)
		Debug(_T("Search Result: %s  Client=%u.%u.%u.%u:%u  Tags=%u\n"), (LPCTSTR)md4str(m_FileIdentifier.GetMD4Hash()), (uint8)m_nClientID, (uint8)(m_nClientID >> 8), (uint8)(m_nClientID >> 16), (uint8)(m_nClientID >> 24), m_nClientPort, tagcount);

	// Copy/Convert ED2K-server tags to local tags
	//
	for (uint32 i = 0; i < tagcount; ++i) {
		std::unique_ptr<CTag> tagOwner(new CTag(in_data, bOptUTF8));
		CTag *tag = tagOwner.get();
		if (thePrefs.GetDebugServerSearchesLevel() > 1)
			Debug(_T("  %s\n"), (LPCTSTR)tag->GetFullInfo(DbgGetFileMetaTagName));
		ConvertED2KTag(tag);
		if (tag != tagOwner.get()) {
			tagOwner.release();
			tagOwner.reset(tag);
		}
		if (tag) {
			// Convert ED2K-server file rating tag
			//
			// NOTE: Feel free to do more with the received numbers here, but please do not add that particular
			// received tag to the local tag list with the received tag format (packed rating). Either create
			// a local tag with an eMule known rating value and drop the percentage (which is currently done),
			// or add a second tag which holds the percentage in addition to the eMule-known rating value.
			// Be aware, that adding that tag in packed-rating format will create troubles in other code parts!
			switch (tag->GetNameID()) {
			case FT_FILERATING:
				if (tag->IsInt()) {
					uint16 nPackedRating = (uint16)tag->GetInt();

					// Percent of clients (related to 'Availability') which rated on that file
					UINT uPercentClientRatings = HIBYTE(nPackedRating);
					(void)uPercentClientRatings;

					// Average rating used by clients
					UINT uAvgRating = LOBYTE(nPackedRating);
					m_uUserRating = uAvgRating / (255 / 5/*RatingExcellent*/);

					tag->SetInt(m_uUserRating);
				}
				break;
			case FT_AICH_HASH:
				if (tag->IsStr()) {
					CAICHHash hash;
					if (DecodeBase32(tag->GetStr(), hash) == CAICHHash::GetHashSize())
						m_FileIdentifier.SetAICHHash(hash);
					else
						ASSERT(0);
					tagOwner.reset();

					continue;
				}
				break;
			case FT_SOURCES:
				if (tag->IsInt())
					m_nSources = tag->GetInt();
				break;
			case FT_COMPLETE_SOURCES:
				if (tag->IsInt())
					m_nCompleteSources = tag->GetInt();
				break;
			case FT_FOLDERNAME:
				if (!bKademlia && !m_pszDirectory && tag->IsStr() && !tag->GetStr().IsEmpty()) {
					directoryOwner.reset(_tcsdup(tag->GetStr()));
					m_pszDirectory = directoryOwner.get();
				}
			}
			// WHY: CArray::Add can allocate after the tag has been parsed and
			// possibly converted. Keep the tag locally owned until the array slot
			// exists so malformed or memory-pressure paths cannot leak CTag.
			m_taglist.Add(tagOwner.get());
			tagOwner.release();
		}
	}

	// here we have two choices
	//	- if the server/client sent us a file type, we could use it (though it could be wrong)
	//	- we always trust our file type list and determine the file type by the extension of the file
	//
	// if we received a file type from server, we use it.
	// if we did not receive a file type, we determine it by examining the file's extension.
	//
	// but, in no case, we will use the received file type when adding this search result to the download queue, to avoid
	// that we are using 'wrong' file types in part files. (this has to be handled when creating the part files)
	const CString &rstrFileType(GetStrTagValue(FT_FILETYPE));
	const CString strOriginalFileName(GetStrTagValue(FT_FILENAME));
	const CString strRepairedFileName(FilenameTextRepairSeams::RepairIncomingSearchFilename(strOriginalFileName));
	if (!strOriginalFileName.IsEmpty() && strRepairedFileName != strOriginalFileName)
		SetStrTagValue(FT_FILENAME, strRepairedFileName);
	SetAFileName(strRepairedFileName, false, rstrFileType.IsEmpty(), true);

	uint64 ui64FileSize = 0;
	CTag *pTagFileSize = GetTag(FT_FILESIZE);
	if (pTagFileSize) {
		if (pTagFileSize->IsInt()) {
			ui64FileSize = pTagFileSize->GetInt();
			CTag *pTagFileSizeHi = GetTag(FT_FILESIZE_HI);
			if (pTagFileSizeHi) {
				if (pTagFileSizeHi->IsInt())
					ui64FileSize |= (uint64)pTagFileSizeHi->GetInt() << 32;
				DeleteTag(pTagFileSizeHi);
			}
			pTagFileSize->SetInt64(ui64FileSize);
		} else if (pTagFileSize->IsInt64(false)) {
			ui64FileSize = pTagFileSize->GetInt64();
			DeleteTag(FT_FILESIZE_HI);
		}
	}
	CSearchFile::SetFileSize(ui64FileSize);

	if (!rstrFileType.IsEmpty())
		if (rstrFileType == _T(ED2KFTSTR_PROGRAM)) {
			const CString &strDetailFileType(GetFileTypeByName(GetFileName()));
			CSearchFile::SetFileType(strDetailFileType.IsEmpty() ? rstrFileType : strDetailFileType);
		} else
			CSearchFile::SetFileType(rstrFileType);
	AddObservedName(GetFileName());
	directoryOwner.release();
}

CSearchFile::~CSearchFile()
{
	free(m_pszDirectory);
	for (int i = m_listFrames.GetSize(); --i >= 0;)
		if (m_listFrames[i])
			::DeleteObject(m_listFrames[i]);
}

void CSearchFile::StoreToFile(CFileDataIO &rFile) const
{
	rFile.WriteHash16(m_FileIdentifier.GetMD4Hash());
	rFile.WriteUInt32(m_nClientID);
	rFile.WriteUInt16(m_nClientPort);
	bool bHasDirectory = m_pszDirectory && *m_pszDirectory;
	uint32 nTagCount = (uint32)m_taglist.GetCount();
	nTagCount += static_cast<uint32>(m_FileIdentifier.HasAICHHash());
	nTagCount += static_cast<uint32>(bHasDirectory);

	rFile.WriteUInt32(nTagCount);
	for (INT_PTR pos = 0; pos < m_taglist.GetCount(); ++pos) {
		const CTag *tag = m_taglist[pos];
		if (tag->GetNameID() == FT_FILERATING && tag->IsInt())
			CTag(FT_FILERATING, (tag->GetInt() * (255 / 5)) & 0xFF).WriteNewEd2kTag(rFile);
		else
			tag->WriteNewEd2kTag(rFile, UTF8strRaw);
	}
	if (m_FileIdentifier.HasAICHHash()) {
		CTag aichtag(FT_AICH_HASH, m_FileIdentifier.GetAICHHash().GetString());
		aichtag.WriteNewEd2kTag(rFile);
	}
	if (bHasDirectory)
		CTag(FT_FOLDERNAME, m_pszDirectory).WriteNewEd2kTag(rFile);
}

void CSearchFile::UpdateFileRatingCommentAvail(bool bForceUpdate)
{
	bool bOldHasComment = m_bHasComment;
	UINT uOldUserRatings = m_uUserRating;

	m_bHasComment = false;
	UINT uRatings = 0;
	UINT uUserRatings = 0;

	for (POSITION pos = m_kadNotes.GetHeadPosition(); pos != NULL;) {
		Kademlia::CEntry *entry = m_kadNotes.GetNext(pos);
		if (!m_bHasComment && !entry->GetStrTagValue(Kademlia::CKadTagNameString(TAG_DESCRIPTION)).IsEmpty())
			m_bHasComment = true;
		UINT rating = (UINT)entry->GetIntTagValue(Kademlia::CKadTagNameString(TAG_FILERATING));
		if (rating != 0) {
			++uRatings;
			uUserRatings += rating;
		}
	}

	// searchfile specific
	// the file might have had a server rating, don't change the rating if no kad ratings were found
	if (uRatings)
		m_uUserRating = (uint32)ROUND(uUserRatings / (float)uRatings);

	if (bOldHasComment != m_bHasComment || uOldUserRatings != m_uUserRating || bForceUpdate)
		theApp.emuledlg->searchwnd->UpdateSearch(this);
}

uint32 CSearchFile::AddSources(uint32 count)
{
	if (m_bKademlia) {
		if (count > m_nSources)
			m_nSources = count;
	} else
		m_nSources += count;
	return m_nSources;
}

uint32 CSearchFile::AddCompleteSources(uint32 count)
{
	if (m_bKademlia) {
		if (count > m_nCompleteSources)
			m_nCompleteSources = count;
	} else
		m_nCompleteSources += count;
	return m_nCompleteSources;
}

int CSearchFile::IsComplete() const
{
	return IsComplete(m_nSources, m_nCompleteSources);
}

int CSearchFile::IsComplete(UINT uSources, UINT uCompleteSources) const
{
	if (IsKademlia())
		return -1;	// unknown

	if (GetDirectory() != NULL && uSources == 1 && uCompleteSources == 0) {
		// If this 'search' result is from a remote client 'View Shared Files' answer, we don't yet have
		// any 'complete' information (could though be implemented some day) -> don't show the file as
		// incomplete though. Treat it as 'unknown'.
		return -1;	// unknown
	}
	if (uSources > 0 && uCompleteSources > 0)
		return 1;	// complete
	return 0;		// not complete
}

time_t CSearchFile::GetLastSeenComplete() const
{
	return GetIntTagValue(FT_LASTSEENCOMPLETE);
}

bool CSearchFile::IsConsideredSpam() const
{
	return GetSpamRating() >= SEARCH_SPAM_THRESHOLD;
}

void CSearchFile::AddObservedName(LPCTSTR pszName)
{
	CString strName(pszName != NULL ? pszName : _T(""));
	strName.Trim();
	if (strName.IsEmpty())
		return;
	for (INT_PTR i = 0; i < m_astrObservedNames.GetCount(); ++i) {
		if (m_astrObservedNames[i].CompareNoCase(strName) == 0)
			return;
	}
	m_astrObservedNames.Add(strName);
}
