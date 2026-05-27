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
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#ifdef _DEBUG
#include "DebugHelpers.h"
#endif
#include "emule.h"
#include "KnownFileList.h"
#include "KnownFileListSeams.h"
#include "KnownFileHashOpenSeams.h"
#include "SharedFileList.h"
#include "UpDownClient.h"
#include "ClientList.h"
#include "opcodes.h"
#include "ini2.h"
#include "Preview.h"
#include "Preferences.h"
#include "PartFile.h"
#include "Packets.h"
#include "ProtocolGuards.h"
#include "DisplayRefreshSeams.h"
#include "HelperThreadLaunchSeams.h"
#include "SourceExchangeSeams.h"
#include "OtherFunctions.h"
#include "PathHelpers.h"
#include "UserMsgs.h"
#include "EmuleMD4.h"
#include "Kademlia/Kademlia/SearchManager.h"
#include "Kademlia/Kademlia/Entry.h"
#include "kademlia/kademlia/UDPFirewallTester.h"
#include "SafeFile.h"
#include "shahashset.h"
#include "Log.h"
#include "Collection.h"
#include "emuledlg.h"
#include "SharedFilesWnd.h"
#include "KnownFileMetadataSeams.h"
#include "FileSizeSeams.h"
#include "MediaInfo.h"
#include "id3/tag.h"
#include "uploaddiskiothread.h"
extern wchar_t* ID3_GetStringW(const ID3_Frame *frame, ID3_FieldID fldName);

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// Meta data version
// -----------------
//	0	untrusted meta data which was received via search results
//	1	trusted meta data, Unicode (strings where not stored correctly)
//	2	0.49c: trusted meta data, Unicode
#define	META_DATA_VER	2

namespace
{
/**
 * @brief Logs a hashing open failure using the real Win32 reason from the long-path open helper.
 */
void LogKnownFileOpenFailure(LPCTSTR pszFilePath, DWORD dwError)
{
	if (dwError == ERROR_SUCCESS)
		dwError = ERROR_OPEN_FAILED;
	LogError(GetResString(IDS_ERR_FILEOPEN), pszFilePath, (LPCTSTR)GetErrorMessage(dwError));
}

FILE *OpenFileStreamSharedReadForHashing(const CString &strFilePath)
{
	DWORD dwLastOpenError = ERROR_SUCCESS;
	for (unsigned uAttempt = 0; uAttempt < KnownFileHashOpenSeams::kHashOpenRetryAttempts; ++uAttempt) {
		FILE *pFile = OpenFileStreamSharedReadLongPath(strFilePath, false);
		if (pFile != NULL)
			return pFile;

		dwLastOpenError = ::GetLastError();
		if (theApp.IsClosing() || !KnownFileHashOpenSeams::ShouldRetryHashOpen(dwLastOpenError, uAttempt))
			break;
		::Sleep(KnownFileHashOpenSeams::kHashOpenRetryDelayMs);
	}

	::SetLastError(dwLastOpenError);
	return NULL;
}

bool QueuePartFileProgressUpdate(const CKnownFileProgressTargetSnapshot *pProgressTarget, uint32 uProgress)
{
	if (pProgressTarget == NULL || !pProgressTarget->isPartFile || theApp.emuledlg == NULL)
		return false;

	std::unique_ptr<CPartFileProgressUpdateRequest> pRequest(new CPartFileProgressUpdateRequest{});
	memcpy(pRequest->fileHash, pProgressTarget->fileHash, sizeof pRequest->fileHash);
	pRequest->fileSize = pProgressTarget->fileSize;
	pRequest->progress = uProgress;
	if (!PostOwnedDisplayRefreshRequest(theApp.emuledlg->GetSafeHwnd(), UM_PARTFILE_PROGRESS_UPDATE, pRequest))
		return false;
	return true;
}
}

IMPLEMENT_DYNAMIC(CKnownFile, CShareableFile)

CKnownFile::CKnownFile()
	: m_tUtcLastModified((time_t)-1)
	, m_tCompleteSourcesTime() //(time(NULL))
	, m_nCompleteSourcesCount(1)
	, m_nCompleteSourcesCountLo(1)
	, m_nCompleteSourcesCountHi(1)
	, m_pCollection()
	, m_hRead(INVALID_HANDLE_VALUE)
	, nInUse()
	, bCompress()
	, bNoNewReads()
	, m_timeLastSeen()
	, m_lastPublishTimeKadSrc()
	, m_lastPublishTimeKadNotes()
	, kadFileSearchID()
	, m_lastBuddyIP()
	, m_uMetaDataVer()
	, m_iPartCount()
	, m_iED2KPartCount()
	, m_iUpPriority()
	, m_bAutoUpPriority(thePrefs.GetNewAutoUp())
	, m_PublishedED2K()
	, m_bED2KRepublishPending()
	, m_bAICHRecoverHashSetAvailable()
	, m_nPartFileHashLayoutGenerationSnapshot()
{
	m_iUpPriority = m_bAutoUpPriority ? PR_HIGH : PR_NORMAL;
	statistic.fileParent = this;
	SetLastPublishTimeKadSrc(0, 0);
}

CKnownFile::~CKnownFile()
{
	ASSERT(!nInUse);
	CUploadDiskIOThread::DissociateFile(this);
	delete m_pCollection;
}

#ifdef _DEBUG
void CKnownFile::AssertValid() const
{
	CAbstractFile::AssertValid();

	(void)m_tUtcLastModified;
	(void)statistic;
	(void)m_tCompleteSourcesTime;
	(void)m_nCompleteSourcesCount;
	(void)m_nCompleteSourcesCountLo;
	(void)m_nCompleteSourcesCountHi;
	m_ClientUploadList.AssertValid();
	m_AvailPartFrequency.AssertValid();
	(void)m_strDirectory;
	(void)m_strFilePath;
	(void)m_iPartCount;
	(void)m_iED2KPartCount;
	ASSERT(m_iUpPriority == PR_VERYLOW || m_iUpPriority == PR_LOW || m_iUpPriority == PR_NORMAL || m_iUpPriority == PR_HIGH || m_iUpPriority == PR_VERYHIGH);
	CHECK_BOOL(m_bAutoUpPriority);
	(void)s_ShareStatusBar;
	CHECK_BOOL(m_PublishedED2K);
	CHECK_BOOL(m_bED2KRepublishPending);
	(void)kadFileSearchID;
	(void)m_lastPublishTimeKadSrc;
	(void)m_lastPublishTimeKadNotes;
	(void)m_lastBuddyIP;
	(void)wordlist;
}

void CKnownFile::Dump(CDumpContext &dc) const
{
	CAbstractFile::Dump(dc);
}
#endif

CBarShader CKnownFile::s_ShareStatusBar(16);

void CKnownFile::DrawShareStatusBar(CDC &dc, LPCRECT rect, bool onlygreyrect, bool  bFlat) const
{
	s_ShareStatusBar.SetFileSize(GetFileSize());
	s_ShareStatusBar.SetRect(rect);

	if (!m_ClientUploadList.IsEmpty() || m_nCompleteSourcesCountHi > 1) {
		// We have info about chunk frequency in the net, so we will color the chunks we have after perceived availability.
		const COLORREF crMissing = RGB(255, 0, 0);
		s_ShareStatusBar.Fill(crMissing);

		if (!onlygreyrect) {
			uint32 tempCompleteSources = m_nCompleteSourcesCountLo ? m_nCompleteSourcesCountLo - 1 : 0;

			for (INT_PTR i = GetPartCount(); --i >= 0;) {
				uint32 frequency = tempCompleteSources;
				if (!m_AvailPartFrequency.IsEmpty())
					frequency = max(m_AvailPartFrequency[i], tempCompleteSources);

				if (frequency > 0) {
					COLORREF color = RGB(0, (22 * (frequency - 1) >= 210) ? 0 : 210 - (22 * (frequency - 1)), 255);
					uint64 uBegin = PARTSIZE * i;
					s_ShareStatusBar.FillRange(uBegin, uBegin + PARTSIZE, color);
				}
			}
		}
	} else {
		// We have no info about chunk frequency in the net, so just color the chunk we have as black.
		COLORREF crNooneAsked = bFlat ? RGB(0, 0, 0) : RGB(104, 104, 104);
		s_ShareStatusBar.Fill(crNooneAsked);
	}

	s_ShareStatusBar.Draw(dc, rect->left, rect->top, bFlat);
}

void CKnownFile::UpdateFileRatingCommentAvail(bool bForceUpdate)
{
	bool bOldHasComment = m_bHasComment;
	UINT uOldUserRatings = m_uUserRating;

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

	m_uUserRating = uRatings ? (uint32)ROUND(static_cast<float>(uUserRatings) / static_cast<float>(uRatings)) : 0;

	if (bOldHasComment != m_bHasComment || uOldUserRatings != m_uUserRating || bForceUpdate)
		theApp.emuledlg->sharedfileswnd->sharedfilesctrl.UpdateFile(this);
}

float CKnownFile::GetSessionUploadRatio() const
{
	const uint64 nFileSize = (uint64)GetFileSize();
	if (nFileSize == 0)
		return 0.0f;
	return static_cast<float>(static_cast<double>(statistic.GetTransferred()) / static_cast<double>(nFileSize));
}

float CKnownFile::GetAllTimeUploadRatio() const
{
	const uint64 nFileSize = (uint64)GetFileSize();
	if (nFileSize == 0)
		return 0.0f;
	return static_cast<float>(static_cast<double>(statistic.GetAllTimeTransferred()) / static_cast<double>(nFileSize));
}

void CKnownFile::UpdatePartsInfo()
{
	time_t tNow = time(NULL);
	bool bRefresh = (tNow - m_tCompleteSourcesTime > 0);

	// Reset part counters
	if (m_AvailPartFrequency.GetCount() < GetPartCount())
		m_AvailPartFrequency.SetSize(GetPartCount());
	if (GetPartCount())
		memset(&m_AvailPartFrequency[0], 0, GetPartCount() * sizeof m_AvailPartFrequency[0]);

	CArray<uint16, uint16> acount;
	if (bRefresh)
		acount.SetSize(0, m_ClientUploadList.GetCount());
	for (POSITION pos = m_ClientUploadList.GetHeadPosition(); pos != NULL;) {
		const POSITION posCurrent = pos;
		CUpDownClient *cur_src = m_ClientUploadList.GetNext(pos);
		if (!IsLiveUploadingClient(cur_src)) {
			RemoveStaleUploadingClient(posCurrent, cur_src, _T("upload parts-info pass"));
			continue;
		}
		//This could be a partfile that just completed. Many of these clients will not have this information.
		if (cur_src->m_abyUpPartStatus && cur_src->GetUpPartCount() == GetPartCount()) {
			for (INT_PTR i = GetPartCount(); --i > 0;)
				m_AvailPartFrequency[i] += static_cast<uint16>(cur_src->IsUpPartAvailable((UINT)i));

			if (bRefresh)
				acount.Add(cur_src->GetUpCompleteSourcesCount());
		}
	}

	if (bRefresh) {
		m_nCompleteSourcesCountLo = m_nCompleteSourcesCountHi = 0;

		if (GetPartCount() > 0) {
			m_nCompleteSourcesCount = m_AvailPartFrequency[0];
			for (INT_PTR i = GetPartCount(); --i >= 0;)
				if (m_nCompleteSourcesCount > m_AvailPartFrequency[i])
					m_nCompleteSourcesCount = m_AvailPartFrequency[i];
		} else
			m_nCompleteSourcesCount = 0;
		acount.Add(m_nCompleteSourcesCount + 1); // plus 1 since we have the complete file too

		int n = (int)acount.GetCount();
		if (n > 0) {
			// SLUGFILLER: heapsortCompletesrc
			for (int r = n / 2; r--;)
				HeapSort(acount, r, n - 1);
			for (int r = n; --r;) {
				uint16 t = acount[r];
				acount[r] = acount[0];
				acount[0] = t;
				HeapSort(acount, 0, r - 1);
			}
			// SLUGFILLER: heapsortCompletesrc

			// calculate range
			int j = (n * 3) >> 2;	// (n * 3) / 4

			//For complete files, trust people you had uploaded more...

			if (n < 20) {
				//For low guess and normal guess count
				//	If we see more sources then the guessed low and normal, use what we see.
				//	If we see less sources then the guessed low, adjust network accounts for 100%,
				//	we account for 0% with what we see and make sure we are still above the normal.
				//For high guess
				//	Adjust 100% network and 0% what we see.
				int i = n >> 1;			// (n / 2)
				if (acount[i] < m_nCompleteSourcesCount)
					m_nCompleteSourcesCountLo = m_nCompleteSourcesCount;
				else
					m_nCompleteSourcesCountLo = acount[i];
				m_nCompleteSourcesCount = m_nCompleteSourcesCountLo;
				m_nCompleteSourcesCountHi = acount[j];
			} else {
				//Many sources.
				//For low guess
				//	Use what we see.
				//For normal guess
				//	Adjust network accounts for 100%, we account for 0% with what we see and make sure we are still above the low.
				//For high guess
				//  Adjust network accounts for 100%, we account for 0% with what we see and make sure we are still above the normal.
				m_nCompleteSourcesCountLo = m_nCompleteSourcesCount;
				m_nCompleteSourcesCount = acount[j];
				if (m_nCompleteSourcesCount < m_nCompleteSourcesCountLo)
					m_nCompleteSourcesCount = m_nCompleteSourcesCountLo;
				int k = (n * 7) >> 3;	// (n * 7) / 8
				m_nCompleteSourcesCountHi = acount[k];
			}
			if (m_nCompleteSourcesCountHi < m_nCompleteSourcesCount)
				m_nCompleteSourcesCountHi = m_nCompleteSourcesCount;
		}
		m_tCompleteSourcesTime = tNow + MIN2S(1);
	}
	if (theApp.emuledlg->sharedfileswnd->m_hWnd && ::GetCurrentThreadId() == g_uMainThreadId)
		theApp.emuledlg->sharedfileswnd->sharedfilesctrl.UpdateFile(this);
}

void CKnownFile::AddUploadingClient(CUpDownClient *client)
{
	if (m_ClientUploadList.Find(client) == NULL) { // to be sure
		m_ClientUploadList.AddTail(client);
		UpdateAutoUpPriority();
	}
}

void CKnownFile::RemoveUploadingClient(CUpDownClient *client)
{
	POSITION pos = m_ClientUploadList.Find(client);
	if (pos != NULL) {
		m_ClientUploadList.RemoveAt(pos);
		UpdateAutoUpPriority();
	}
	if (m_ClientUploadList.IsEmpty()) {
		ASSERT(!nInUse);
		CUploadDiskIOThread::DissociateFile(this);
	}
}

bool CKnownFile::IsLiveUploadingClient(const CUpDownClient *pClient) const
{
	if (pClient == NULL)
		return false;
	if (theApp.clientlist == NULL || !theApp.clientlist->ContainsClientPointer(pClient))
		return false;

	return md4equ(pClient->GetUploadFileID(), GetFileHash());
}

void CKnownFile::RemoveStaleUploadingClient(POSITION pos, const CUpDownClient *pClient, LPCTSTR pszContext) const
{
	ASSERT(pos != NULL);
	if (pos == NULL)
		return;

	CKnownFile *pMutableFile = const_cast<CKnownFile*>(this);
	pMutableFile->m_ClientUploadList.RemoveAt(pos);
	pMutableFile->UpdateAutoUpPriority();
	if (pMutableFile->m_ClientUploadList.IsEmpty() && pMutableFile->nInUse == 0)
		CUploadDiskIOThread::DissociateFile(pMutableFile);
	DebugLogWarning(_T("Removed stale uploading client pointer %p from file \"%s\" (%s)"),
		static_cast<const void*>(pClient),
		(LPCTSTR)GetFileName(),
		pszContext != NULL ? pszContext : _T("unknown context"));
}

#ifdef _DEBUG
void Dump(const Kademlia::WordList &wordlist)
{
	for (Kademlia::WordList::const_iterator it = wordlist.begin(); it != wordlist.end(); ++it) {
		const CStringW &rstrKeyword(*it);
		TRACE("  %ls\n", (LPCWSTR)rstrKeyword);
	}
}
#endif

void CKnownFile::SetFileName(LPCTSTR pszFileName, bool bReplaceInvalidFileSystemChars, bool bRemoveControlChars)
{
	// The shared files object may not exist early in startup sequence.
	bool bSharedFiles = theApp.sharedfiles && theApp.sharedfiles->GetFileByID(GetFileHash()) == this;
	const bool bKnownFile = theApp.knownfiles && theApp.knownfiles->IsKnownFile(this);

	if (bSharedFiles)
		theApp.sharedfiles->RemoveKeywords(this);

	SetAFileName(pszFileName, bReplaceInvalidFileSystemChars, true, bRemoveControlChars);
	m_verifiedFileType = FILETYPE_UNKNOWN;

	wordlist.clear();
	if (m_pCollection) {
		CStringW sKeyWords(m_pCollection->GetCollectionAuthorKeyString());
		sKeyWords.AppendFormat(_T(" %s"), (LPCTSTR)GetFileName());
		Kademlia::CSearchManager::GetWords(sKeyWords, wordlist);
	} else
		Kademlia::CSearchManager::GetWords((CStringW)GetFileName(), wordlist); //make sure that it is a CStringW

	if (bKnownFile)
		theApp.knownfiles->InvalidateLookupIndex();
	if (bSharedFiles)
		theApp.sharedfiles->AddKeywords(this);
}

bool CKnownFile::CreateFromFile(LPCTSTR in_directory, LPCTSTR in_filename, const CKnownFileProgressTargetSnapshot *pProgressTarget)
{
	SetPath(in_directory);
	SetFileName(in_filename);

	// open file
	CString strFilePath(in_directory);
	strFilePath += in_filename;
	if (strFilePath.IsEmpty()) {
		LogError(GetResString(IDS_ERR_FILEOPEN), in_filename, _T(""));
		return false;
	}
	SetFilePath(strFilePath);
	FILE *file = OpenFileStreamSharedReadForHashing(strFilePath); // can not use _SH_DENYWR because we may access a completing part file
	if (!file) {
		const DWORD dwOpenError = ::GetLastError();
		LogKnownFileOpenFailure(strFilePath, dwOpenError);
		return false;
	}

	// set file size. Zero size is valid for .part files
	__int64 llFileSize = _filelengthi64(_fileno(file));
	if (!FileSizeSeams::IsSupportedNetworkFileSize(llFileSize)) {
		if (llFileSize <= 0)
			LogError(_T("Failed to hash file \"%s\" - %s"), (LPCTSTR)strFilePath, (LPCTSTR)GetCrtErrorString(errno));
		else
			LogError(_T("Skipped hashing file \"%s\" - File size exceeds limit."), (LPCTSTR)strFilePath);
		fclose(file);
		return false; // not supported by network
	}
	SetFileSize(FileSizeSeams::FromSignedFileLength(llFileSize));

	// we are reading the file data later in 8K blocks, adjust the internal file stream buffer accordingly
	::setvbuf(file, NULL, _IOFBF, 1024 * 8 * 2);

	m_AvailPartFrequency.SetSize(GetPartCount());
	if (GetPartCount())
		memset(&m_AvailPartFrequency[0], 0, GetPartCount() * sizeof m_AvailPartFrequency[0]);

	// create hashset
	CAICHRecoveryHashSet cAICHHashSet(this, m_nFileSize);
	uint64 togo = FileSizeSeams::ToUInt64(m_nFileSize);
	UINT hashcount;
	for (hashcount = 0; ; ++hashcount) {
		UINT uSize = (UINT)min(togo, PARTSIZE);
		CAICHHashTree *pBlockAICHHashTree;
		if (togo) {
			pBlockAICHHashTree = cAICHHashSet.m_pHashTree.FindHash(hashcount * PARTSIZE, uSize);
			ASSERT(pBlockAICHHashTree != NULL);
			if (pBlockAICHHashTree == NULL) {
				fclose(file);
				return false;
			}
		} else
			pBlockAICHHashTree = NULL; // SHA hash tree doesn't take hash of zero-sized data

		uchar *newhash = new uchar[MDX_DIGEST_SIZE];
		if (!CreateHash(file, uSize, newhash, pBlockAICHHashTree)) {
			if (theApp.IsClosing()) {
				fclose(file);
				delete[] newhash;
				return false;
			}
			LogError(_T("Failed to hash file \"%s\" - %s"), (LPCTSTR)strFilePath, (LPCTSTR)GetCrtErrorString(errno));
			fclose(file);
			delete[] newhash;
			return false;
		}

		if (theApp.IsClosing()) { // in case of shutdown while still hashing
			fclose(file);
			delete[] newhash;
			return false;
		}

		if (!hashcount && uSize < PARTSIZE) {
			m_FileIdentifier.SetMD4Hash(newhash); //one and only part
			delete[] newhash;
		} else
			m_FileIdentifier.GetRawMD4HashSet().Add(newhash);

		togo -= uSize;
		if (!togo)
			if (uSize == PARTSIZE)
				continue;
			else
				break;

		if (theApp.IsClosing()) {
			LogError(_T("Hashing cancelled (closing eMule), file \"%s\""), (LPCTSTR)strFilePath);
			fclose(file);
			return false;
		}
		if (pProgressTarget != NULL && pProgressTarget->isPartFile) {
			const uint64 nFileSize = static_cast<uint64>(GetFileSize());
			const uint64 nBytesHashed = nFileSize >= togo ? nFileSize - togo : 0;
			const uint32 uProgress = static_cast<uint32>(CalculateProgressPercent(nBytesHashed, nFileSize));
			ASSERT(uProgress <= 100);
			QueuePartFileProgressUpdate(pProgressTarget, uProgress);
		}
	}

	if (hashcount)
		m_FileIdentifier.CalculateMD4HashByHashSet(false);

	cAICHHashSet.ReCalculateHash(false);
	if (cAICHHashSet.VerifyHashTree(true)) {
		cAICHHashSet.SetStatus(AICH_HASHSETCOMPLETE);
		m_FileIdentifier.SetAICHHash(cAICHHashSet.GetMasterHash());
		if (!m_FileIdentifier.SetAICHHashSet(cAICHHashSet)) {
			ASSERT(0);
			DebugLogError(_T("CreateFromFile() - failed to create AICH PartHashSet out of RecoveryHashSet - %s"), (LPCTSTR)GetFileName());
		}
		if (!cAICHHashSet.SaveHashSet())
			LogError(LOG_STATUSBAR, GetResString(IDS_SAVEACFAILED));
		else
			SetAICHRecoverHashSetAvailable(true);
	} else
		// now something went pretty wrong
		DebugLogError(LOG_STATUSBAR, _T("Failed to calculate AICH Hashset from file %s"), (LPCTSTR)GetFileName());

	if (pProgressTarget != NULL && pProgressTarget->isPartFile && !theApp.IsClosing()) {
		const uint32 uProgress = 100;
		ASSERT(uProgress <= 100);
		QueuePartFileProgressUpdate(pProgressTarget, uProgress);
	}

	// set last write date
	struct _stat64 st;
	if (statUTC((HANDLE)_get_osfhandle(_fileno(file)), st) == 0) {
		m_tUtcLastModified = (time_t)st.st_mtime;
	}

	fclose(file);

	// Add file tags
	UpdateMetaDataTags();

	UpdatePartsInfo();

	return true;
}

bool CKnownFile::CreateAICHHashSetOnly()
{
	ASSERT(!IsPartFile());

	FILE *file = OpenFileStreamSharedReadForHashing(GetFilePath()); // can not use _SH_DENYWR because we may access a completing part file
	if (!file) {
		const DWORD dwOpenError = ::GetLastError();
		LogKnownFileOpenFailure(GetFilePath(), dwOpenError);
		return false;
	}
	// we are reading the file data later in 8K blocks, adjust the internal file stream buffer accordingly
	::setvbuf(file, NULL, _IOFBF, 1024 * 8 * 2);

	// create aich hashset
	CAICHRecoveryHashSet cAICHHashSet(this, m_nFileSize);
	uint64 togo = FileSizeSeams::ToUInt64(m_nFileSize);
	for (UINT hashcount = 0; togo; ++hashcount) {
		uint64 uSize = min(togo, PARTSIZE);
		CAICHHashTree *pBlockAICHHashTree = cAICHHashSet.m_pHashTree.FindHash(hashcount * PARTSIZE, uSize);
		ASSERT(pBlockAICHHashTree != NULL);
		if (pBlockAICHHashTree == NULL) {
			fclose(file);
			return false;
		}
		if (!CreateHash(file, uSize, NULL, pBlockAICHHashTree)) {
			if (theApp.IsClosing()) {
				fclose(file);
				return false;
			}
			LogError(_T("Failed to hash file \"%s\" - %s"), (LPCTSTR)GetFilePath(), (LPCTSTR)GetCrtErrorString(errno));
			fclose(file);
			return false;
		}
		if (theApp.IsClosing()) { // in case of shutdown while still hashing
			fclose(file);
			return false;
		}
		togo -= uSize;
	}
	fclose(file);

	cAICHHashSet.ReCalculateHash(false);
	if (cAICHHashSet.VerifyHashTree(true)) {
		cAICHHashSet.SetStatus(AICH_HASHSETCOMPLETE);
		const CAICHHash *pHash = (m_FileIdentifier.HasAICHHash() && m_FileIdentifier.GetAICHHash() != cAICHHashSet.GetMasterHash())
			? &m_FileIdentifier.GetAICHHash() : NULL;
		theApp.knownfiles->AICHHashChanged(pHash, cAICHHashSet.GetMasterHash(), this);
		m_FileIdentifier.SetAICHHash(cAICHHashSet.GetMasterHash());
		if (!m_FileIdentifier.SetAICHHashSet(cAICHHashSet)) {
			ASSERT(0);
			DebugLogError(_T("CreateAICHHashSetOnly() - failed to create AICH PartHashSet out of RecoveryHashSet - %s"), (LPCTSTR)GetFileName());
		}
		if (!cAICHHashSet.SaveHashSet())
			LogError(LOG_STATUSBAR, GetResString(IDS_SAVEACFAILED));
		else
			SetAICHRecoverHashSetAvailable(true);
	} else {
		// now something went pretty wrong
		DebugLogError(LOG_STATUSBAR, _T("Failed to calculate AICH Hashset from file %s"), (LPCTSTR)GetFileName());
	}

	return true;
}

void CKnownFile::SetFileSize(EMFileSize nFileSize)
{
	const bool bKnownFile = theApp.knownfiles && theApp.knownfiles->IsKnownFile(this);
	CAbstractFile::SetFileSize(nFileSize);

	// Examples of part hashes, hash sets and file hashes for different file sizes
	// according the ed2k protocol
	//----------------------------------------------------------------------
	//
	//File size: 3 bytes
	//File hash: 2D55E87D0E21F49B9AD25F98531F3724
	//Nr. hashes: 0
	//
	//
	//File size: 1*PARTSIZE
	//File hash: A72CA8DF7F07154E217C236C89C17619
	//Nr. hashes: 2
	//Hash[  0]: 4891ED2E5C9C49F442145A3A5F608299
	//Hash[  1]: 31D6CFE0D16AE931B73C59D7E0C089C0	*special part hash*
	//
	//
	//File size: 1*PARTSIZE + 1 byte
	//File hash: 2F620AE9D462CBB6A59FE8401D2B3D23
	//Nr. hashes: 2
	//Hash[  0]: 121795F0BEDE02DDC7C5426D0995F53F
	//Hash[  1]: C329E527945B8FE75B3C5E8826755747
	//
	//
	//File size: 2*PARTSIZE
	//File hash: A54C5E562D5E03CA7D77961EB9A745A4
	//Nr. hashes: 3
	//Hash[  0]: B3F5CE2A06BF403BFB9BFFF68BDDC4D9
	//Hash[  1]: 509AA30C9EA8FC136B1159DF2F35B8A9
	//Hash[  2]: 31D6CFE0D16AE931B73C59D7E0C089C0	*special part hash*
	//
	//
	//File size: 3*PARTSIZE
	//File hash: 5E249B96F9A46A18FC2489B005BF2667
	//Nr. hashes: 4
	//Hash[  0]: 5319896A2ECAD43BF17E2E3575278E72
	//Hash[  1]: D86EF157D5E49C5ED502EDC15BB5F82B
	//Hash[  2]: 10F2D5B1FCB95C0840519C58D708480F
	//Hash[  3]: 31D6CFE0D16AE931B73C59D7E0C089C0	*special part hash*
	//
	//
	//File size: 3*PARTSIZE + 1 byte
	//File hash: 797ED552F34380CAFF8C958207E40355
	//Nr. hashes: 4
	//Hash[  0]: FC7FD02CCD6987DCF1421F4C0AF94FB8
	//Hash[  1]: 2FE466AF8A7C06DA3365317B75A5ACFE
	//Hash[  2]: 873D3BF52629F7C1527C6E8E473C1C30
	//Hash[  3]: BCE50BEE7877BB07BB6FDA56BFE142FB
	//

	// File size       Data parts      ED2K parts      ED2K part hashes		AICH part hashes
	// -------------------------------------------------------------------------------------------
	// 1..PARTSIZE-1   1               1               0(!)					0 (!)
	// PARTSIZE        1               2(!)            2(!)					0 (!)
	// PARTSIZE+1      2               2               2					2
	// PARTSIZE*2      2               3(!)            3(!)					2
	// PARTSIZE*2+1    3               3               3					3

	if ((uint64)nFileSize == 0) {
		ASSERT(0);
		m_iPartCount = 0;
		m_iED2KPartCount = 0;
		return;
	}

	// nr. of data parts
	ASSERT(((uint64)nFileSize + (PARTSIZE - 1)) / PARTSIZE <= _UI16_MAX);
	m_iPartCount = (uint16)(((uint64)nFileSize + (PARTSIZE - 1)) / PARTSIZE);

	// nr. of parts to be used with OP_FILESTATUS
	m_iED2KPartCount = (uint16)((uint64)nFileSize / PARTSIZE + 1);
	if (bKnownFile)
		theApp.knownfiles->InvalidateLookupIndex();
}

bool CKnownFile::LoadTagsFromFile(CFileDataIO &file)
{
	bool bHadAICHHashSetTag = false;
	for (uint32 j = file.ReadUInt32(); j > 0; --j) {
		CTag *newtag = new CTag(file, false);
		switch (newtag->GetNameID()) {
		case FT_FILENAME:
			ASSERT(newtag->IsStr());
			if (newtag->IsStr() && GetFileName().IsEmpty())
				SetFileName(newtag->GetStr());
			break;
		case FT_FILESIZE:
			ASSERT(newtag->IsInt64(true));
			if (newtag->IsInt64(true)) {
				SetFileSize(newtag->GetInt64());
				m_AvailPartFrequency.SetSize(GetPartCount());
				if (GetPartCount())
					memset(&m_AvailPartFrequency[0], 0, GetPartCount() * sizeof m_AvailPartFrequency[0]);
			}
			break;
		case FT_ATTRANSFERRED:
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				statistic.SetAllTimeTransferred(newtag->GetInt());
			break;
		case FT_ATTRANSFERREDHI:
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				statistic.SetAllTimeTransferred(((uint64)newtag->GetInt() << 32) | (UINT)statistic.GetAllTimeTransferred());
			break;
		case FT_ATREQUESTED:
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				statistic.SetAllTimeRequests(newtag->GetInt());
			break;
		case FT_ATACCEPTED:
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				statistic.SetAllTimeAccepts(newtag->GetInt());
			break;
		case FT_ULPRIORITY:
			ASSERT(newtag->IsInt());
			if (newtag->IsInt()) {
				m_iUpPriority = (uint8)newtag->GetInt();
				if (m_iUpPriority == PR_AUTO) {
					m_iUpPriority = PR_HIGH;
					m_bAutoUpPriority = true;
				} else {
					if (m_iUpPriority != PR_VERYLOW && m_iUpPriority != PR_LOW && m_iUpPriority != PR_NORMAL && m_iUpPriority != PR_HIGH && m_iUpPriority != PR_VERYHIGH)
						m_iUpPriority = PR_NORMAL;
					m_bAutoUpPriority = false;
				}
			}
			break;
		case FT_KADLASTPUBLISHSRC:
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				SetLastPublishTimeKadSrc(newtag->GetInt(), 0);
			if (GetLastPublishTimeKadSrc() > time(NULL) + KADEMLIAREPUBLISHTIMES) {
				//There is a possibility of an older client that saved a random number here.
				SetLastPublishTimeKadSrc(0, 0); //the fix
			}
			break;
		case FT_KADLASTPUBLISHNOTES:
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				SetLastPublishTimeKadNotes(newtag->GetInt());
			break;
		case FT_FLAGS:
			// Misc. Flags
			// ------------------------------------------------------------------------------
			// Bits  3-0: Meta data version
			//				0	untrusted meta data which was received via search results
			//				1	trusted meta data, Unicode (strings where not stored correctly)
			//				2	0.49c: trusted meta data, Unicode
			// Bits 31-4: Reserved
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				m_uMetaDataVer = newtag->GetInt() & 0x0F;
			break;
		case FT_PERMISSIONS:  // old tags: as they are not needed, take a chance to purge
		case FT_KADLASTPUBLISHKEY:
			ASSERT(newtag->IsInt());
			break;
		case FT_AICH_HASH:
			if (newtag->IsStr()) {
				CAICHHash hash;
				if (DecodeBase32(newtag->GetStr(), hash) == CAICHHash::GetHashSize())
					m_FileIdentifier.SetAICHHash(hash);
				else
					ASSERT(0);
			} else
				ASSERT(0);
			break;
		case FT_LASTSHARED:
			if (newtag->IsInt())
				m_timeLastSeen = newtag->GetInt();
			else
				ASSERT(0);
			break;
		case FT_AICHHASHSET:
			if (newtag->IsBlob()) {
				CSafeMemFile aichHashSetFile(newtag->GetBlob(), newtag->GetBlobSize());
				m_FileIdentifier.LoadAICHHashsetFromFile(aichHashSetFile, false);
				aichHashSetFile.Detach();
				bHadAICHHashSetTag = true;
			} else
				ASSERT(0);
			break;
		default:
			if (newtag->HasName() && CmpED2KTagName(newtag->GetName(), kLastRequestTagName) == 0) {
				ASSERT(newtag->IsInt64(true));
				if (newtag->IsInt64(true))
					statistic.SetAllTimeLastRequest(newtag->GetInt64());
			} else {
				ConvertED2KTag(newtag);
				if (newtag) {
					m_taglist.Add(newtag);
					newtag = NULL;
				}
			}
		}
		delete newtag;
	}
	if (bHadAICHHashSetTag)
		if (!m_FileIdentifier.VerifyAICHHashSet())
			DebugLogError(_T("Failed to load AICH Part HashSet for file %s"), (LPCTSTR)GetFileName());
		//else
		//	DebugLog(_T("Succeeded to load AICH Part HashSet for file %s"), (LPCTSTR)GetFileName());

	// 05-Jan-2004 [bc]: ed2k and Kad are already full of totally wrong and/or not properly
	// attached meta data. Take the chance to clean any available meta data tags and provide
	// only tags which were determined by us.
	// It's a brute force method, but that wrong meta data is driving me crazy because
	// wrong meta data is even worse than missing meta data.
	if (m_uMetaDataVer == 0)
		RemoveMetaDataTags();
	else if (m_uMetaDataVer == 1) {
		// Meta data tags v1 did not store Unicode strings correctly.
		// Remove broken Unicode string meta data tags from v1, but keep the integer tags.
		RemoveBrokenUnicodeMetaDataTags();
		m_uMetaDataVer = META_DATA_VER;
	}

	return true;
}

bool CKnownFile::LoadDateFromFile(CFileDataIO &file)
{
	m_tUtcLastModified = (time_t)file.ReadUInt32();
	return true;
}

bool CKnownFile::LoadFromFile(CFileDataIO &file)
{
	// SLUGFILLER: SafeHash - load first, verify later
	bool ret = LoadDateFromFile(file);
	ret &= m_FileIdentifier.LoadMD4HashsetFromFile(file, false);
	ret &= LoadTagsFromFile(file);
	UpdatePartsInfo();
	return ret && m_FileIdentifier.HasExpectedMD4HashCount(); //Final hash-count verification, needs to be done after the tags are loaded.
	// SLUGFILLER: SafeHash
}

bool CKnownFile::WriteToFile(CFileDataIO &file)
{
	// date
	file.WriteUInt32((uint32)m_tUtcLastModified);

	// hashset
	m_FileIdentifier.WriteMD4HashsetToFile(file);

	uint32 uTagCount = 0;
	ULONGLONG uTagCountFilePos = file.GetPosition();
	file.WriteUInt32(uTagCount);

	CTag nametag(FT_FILENAME, GetFileName());
	nametag.WriteTagToFile(file, UTF8strOptBOM);
	++uTagCount;

	CTag sizetag(FT_FILESIZE, m_nFileSize, IsLargeFile());
	sizetag.WriteTagToFile(file);
	++uTagCount;

	//AICH file hash
	if (m_FileIdentifier.HasAICHHash()) {
		CTag aichtag(FT_AICH_HASH, m_FileIdentifier.GetAICHHash().GetString());
		aichtag.WriteTagToFile(file);
		++uTagCount;
	}

	// last shared
	static bool bDbgWarnedOnZero = false;
	if (!bDbgWarnedOnZero && m_timeLastSeen == 0) {
		DebugLog(_T("Unknown last seen date on stored file(s), upgrading from old version?"));
		bDbgWarnedOnZero = true;
	}
	time_t tNow = time(NULL);
	ASSERT(m_timeLastSeen <= tNow);
	time_t timeLastShared = (m_timeLastSeen > 0 && m_timeLastSeen <= tNow) ? m_timeLastSeen : tNow;
	CTag lastSharedTag(FT_LASTSHARED, (uint32)timeLastShared);
	lastSharedTag.WriteTagToFile(file);
	++uTagCount;

	// to tidy up known.met and known2.met do not store the tags for long time not seen/shared known files
	bool keep = !ShouldPartiallyPurgeFile();
	if (keep) { //"may be purged" tags
		// AICH Part HashSet
		// no point in permanently storing the AICH part hashset if we need to rehash the file anyway to fetch the full recovery hashset
		// Also the tag will make the known.met incompatible with emule version prior 0.44a - but that one is nearly 6 years old
		if (m_FileIdentifier.HasAICHHash() && m_FileIdentifier.HasExpectedAICHHashCount()) {
			uint32 nAICHHashSetSize = (CAICHHash::GetHashSize() * (m_FileIdentifier.GetAvailableAICHPartHashCount() + 1)) + 2;
			BYTE *pHashBuffer = new BYTE[nAICHHashSetSize];
			CSafeMemFile hashSetFile(pHashBuffer, nAICHHashSetSize);
			bool bWriteHashSet = false;
			try {
				m_FileIdentifier.WriteAICHHashsetToFile(hashSetFile);
				bWriteHashSet = true;
			} catch (CFileException *ex) {
				ASSERT(0);
				DebugLogError(_T("Memfile Error while storing AICH Part HashSet"));
				delete[] hashSetFile.Detach();
				ex->Delete();
			}
			if (bWriteHashSet) {
				CTag tagAICHHashSet(FT_AICHHASHSET, hashSetFile.Detach(), nAICHHashSetSize);
				tagAICHHashSet.WriteTagToFile(file);
				++uTagCount;
			}
		}
	}

	// statistics - never purge
	if (statistic.GetAllTimeTransferred()) {
		CTag attag1(FT_ATTRANSFERRED, (uint32)statistic.GetAllTimeTransferred());
		attag1.WriteTagToFile(file);
		++uTagCount;

		CTag attag4(FT_ATTRANSFERREDHI, (uint32)(statistic.GetAllTimeTransferred() >> 32));
		attag4.WriteTagToFile(file);
		++uTagCount;
	}

	if (statistic.GetAllTimeRequests()) {
		CTag attag2(FT_ATREQUESTED, statistic.GetAllTimeRequests());
		attag2.WriteTagToFile(file);
		++uTagCount;
	}

	if (statistic.GetAllTimeLastRequest()) {
		CTag tagLastRequest(kLastRequestTagName, statistic.GetAllTimeLastRequest(), true);
		tagLastRequest.WriteTagToFile(file);
		++uTagCount;
	}

	if (statistic.GetAllTimeAccepts()) {
		CTag attag3(FT_ATACCEPTED, statistic.GetAllTimeAccepts());
		attag3.WriteTagToFile(file);
		++uTagCount;
	}

	// "may be purged" tags, part 2
	if (keep) {
		// priority N permission
		CTag priotag(FT_ULPRIORITY, IsAutoUpPriority() ? PR_AUTO : m_iUpPriority);
		priotag.WriteTagToFile(file);
		++uTagCount;


		if (m_lastPublishTimeKadSrc) {
			CTag kadLastPubSrc(FT_KADLASTPUBLISHSRC, (uint32)m_lastPublishTimeKadSrc);
			kadLastPubSrc.WriteTagToFile(file);
			++uTagCount;
		}

		if (m_lastPublishTimeKadNotes) {
			CTag kadLastPubNotes(FT_KADLASTPUBLISHNOTES, (uint32)m_lastPublishTimeKadNotes);
			kadLastPubNotes.WriteTagToFile(file);
			++uTagCount;
		}

		if (m_uMetaDataVer > 0) {
			// Misc. Flags
			// ------------------------------------------------------------------------------
			// Bits  3-0: Meta data version
			//				0	untrusted meta data which was received via search results
			//				1	trusted meta data, Unicode (strings where not stored correctly)
			//				2	0.49c: trusted meta data, Unicode
			// Bits 31-4: Reserved
			ASSERT(m_uMetaDataVer <= 0x0F);
			uint32 uFlags = m_uMetaDataVer & 0x0F;
			CTag tagFlags(FT_FLAGS, uFlags);
			tagFlags.WriteTagToFile(file);
			++uTagCount;
		}

		// other tags
		for (INT_PTR i = 0; i < m_taglist.GetCount(); ++i) {
			if (m_taglist[i]->IsStr() || m_taglist[i]->IsInt()) {
				m_taglist[i]->WriteTagToFile(file, UTF8strOptBOM);
				++uTagCount;
			}
		}
	}

	file.Seek(uTagCountFilePos, CFile::begin);
	file.WriteUInt32(uTagCount);
	file.Seek(0, CFile::end);

	return true;
}

bool CKnownFile::CreateHash(CFile *pFile, uint64 Length, uchar *pMd4HashOut, CAICHHashTree *pShaHashOut)
{
	ASSERT(!Length || pFile);
	ASSERT(pMd4HashOut != NULL || pShaHashOut != NULL);
	if ((Length != 0 && pFile == NULL) || (pMd4HashOut == NULL && pShaHashOut == NULL))
		return false;

	static const UINT kHashReadBufferBytes = 64u * 1024u;
	static_assert(kHashReadBufferBytes < EMBLOCKSIZE, "hash read buffer must not span multiple AICH blocks");

	uchar   X[kHashReadBufferBytes];
	uint64	posCurrentEMBlock = 0;
	uint64	nIACHPos = 0;
	CMD4	md4;
	std::unique_ptr<CAICHHashAlgo> pHashAlg((pShaHashOut != NULL) ? CAICHRecoveryHashSet::GetNewHashAlgo() : NULL);

	for (uint64 Required = Length; Required;) {
		if (theApp.IsClosing())
			return false;
		UINT len = (UINT)(min(Required, (uint64)_countof(X)) / 64);
		UINT uRead = len ? len * 64 : (UINT)Required;
		const UINT uActualRead = pFile->Read(X, uRead);
		if (uActualRead != uRead)
			AfxThrowFileException(CFileException::endOfFile, 0, pFile->GetFilePath());

		// SHA hash needs 180KB blocks
		if (pShaHashOut != NULL) { // && pHashAlg != NULL - do not check again
			if (nIACHPos + uRead >= EMBLOCKSIZE) {
				uint64 nToComplete = EMBLOCKSIZE - nIACHPos;
				pHashAlg->Add(X, (DWORD)nToComplete);
				ASSERT(nIACHPos + nToComplete == EMBLOCKSIZE);
				pShaHashOut->SetBlockHash(EMBLOCKSIZE, posCurrentEMBlock, pHashAlg.get());
				posCurrentEMBlock += EMBLOCKSIZE;
				pHashAlg->Reset();
				nIACHPos = uRead - nToComplete;
				pHashAlg->Add(X + nToComplete, (DWORD)nIACHPos);
			} else {
				pHashAlg->Add(X, uRead);
				nIACHPos += uRead;
			}
		}

		if (pMd4HashOut != NULL)
			md4.Add(X, uRead);

		Required -= uRead;
	}

	if (pShaHashOut != NULL) {
		if (nIACHPos > 0) {
			pShaHashOut->SetBlockHash(nIACHPos, posCurrentEMBlock, pHashAlg.get());
			posCurrentEMBlock += nIACHPos;
		}
		ASSERT(posCurrentEMBlock == Length);
		VERIFY(pShaHashOut->ReCalculateHash(pHashAlg.get(), false));
	}

	if (pMd4HashOut != NULL) {
		md4.Finish();
		md4cpy(pMd4HashOut, md4.GetHash());
	}
	return true;
}

bool CKnownFile::CreateHash(FILE *fp, uint64 uSize, uchar *pucHash, CAICHHashTree *pShaHashOut)
{
	ASSERT(uSize == 0 || fp != NULL);
	if (uSize != 0 && fp == NULL)
		return false;

	try {
		CStdioFile file(fp);
		return CreateHash(&file, uSize, pucHash, pShaHashOut);
	} catch (CFileException *ex) {
		DebugLogWarning(_T("CreateHash failed while reading stdio-backed data%s"), (LPCTSTR)CExceptionStrDash(*ex));
		ex->Delete();
	}
	return false;
}

bool CKnownFile::CreateHash(const uchar *pucData, uint32 uSize, uchar *pucHash, CAICHHashTree *pShaHashOut)
{
	ASSERT(uSize == 0 || pucData != NULL);
	if (uSize != 0 && pucData == NULL)
		return false;

	try {
		CMemFile file(const_cast<uchar*>(pucData), uSize);
		return CreateHash(&file, uSize, pucHash, pShaHashOut);
	} catch (CFileException *ex) {
		DebugLogWarning(_T("CreateHash failed while reading memory-backed data%s"), (LPCTSTR)CExceptionStrDash(*ex));
		ex->Delete();
	}
	return false;
}

Packet*	CKnownFile::CreateSrcInfoPacket(const CUpDownClient *forClient, uint8 byRequestedVersion, uint16 nRequestedOptions) const
{
	if (m_ClientUploadList.IsEmpty())
		return NULL;

	if (!md4equ(forClient->GetUploadFileID(), GetFileHash())) {
		// should never happen
		DEBUG_ONLY(DebugLogError(_T("*** %hs - client (%s) upload file \"%s\" does not match file \"%s\""), __FUNCTION__, (LPCTSTR)forClient->DbgGetClientInfo(), (LPCTSTR)DbgGetFileInfo(forClient->GetUploadFileID()), (LPCTSTR)GetFileName()));
		ASSERT(0);
		return NULL;
	}

	// check whether client has either no download status at all or a download status which is valid for this file
	if (!(forClient->GetUpPartCount() == 0 && forClient->GetUpPartStatus() == NULL)
		&& !(forClient->GetUpPartCount() == GetPartCount() && forClient->GetUpPartStatus() != NULL))
	{
		// should never happen
		DEBUG_ONLY(DebugLogError(_T("*** %hs - part count (%u) of client (%s) does not match part count (%u) of file \"%s\""), __FUNCTION__, forClient->GetUpPartCount(), (LPCTSTR)forClient->DbgGetClientInfo(), GetPartCount(), (LPCTSTR)GetFileName()));
		ASSERT(0);
		return NULL;
	}

	CSafeMemFile data(1024);

	const SourceExchangeSeams::ResponsePlan plan = SourceExchangeSeams::ResolveSourceExchangeResponsePlan(forClient->SupportsSourceExchange2(), byRequestedVersion);
	if (!plan.bShouldSend)
		return NULL;

	const uint8 byUsedVersion = plan.byUsedVersion;
	data.WriteUInt8(byUsedVersion);

	// We keep accepting the options field for forward compatibility, but only SX2 packets are valid.
	if (nRequestedOptions != 0)
		DebugLogWarning(_T("Client requested unknown options for SourceExchange2: %u (%s)"), nRequestedOptions, (LPCTSTR)forClient->DbgGetClientInfo());

	uint16 nCount = 0;
	data.WriteHash16(forClient->GetUploadFileID());
	data.WriteUInt16(nCount);
	uint32 cDbgNoSrc = 0;
	for (POSITION pos = m_ClientUploadList.GetHeadPosition(); pos != NULL;) {
		const POSITION posCurrent = pos;
		const CUpDownClient *cur_src = m_ClientUploadList.GetNext(pos);
		if (!IsLiveUploadingClient(cur_src)) {
			RemoveStaleUploadingClient(posCurrent, cur_src, _T("source-exchange upload list pass"));
			continue;
		}
		if (cur_src->HasLowID() || cur_src == forClient || !(cur_src->GetUploadState() == US_UPLOADING || cur_src->GetUploadState() == US_ONUPLOADQUEUE))
			continue;
		if (!cur_src->IsEd2kClient())
			continue;

		bool bNeeded = false;
		const uint8 *rcvstatus = forClient->GetUpPartStatus();
		if (rcvstatus) {
			ASSERT(forClient->GetUpPartCount() == GetPartCount());
			const uint8 *srcstatus = cur_src->GetUpPartStatus();
			if (srcstatus) {
				ASSERT(cur_src->GetUpPartCount() == GetPartCount());
				if (cur_src->GetUpPartCount() == forClient->GetUpPartCount()) {
					for (INT_PTR x = GetPartCount(); --x >= 0;)
						if (srcstatus[x] && !rcvstatus[x]) {
							// We know the receiving client needs a chunk from this client.
							bNeeded = true;
							break;
						}
				} else {
					// should never happen
					//if (thePrefs.GetVerbose())
					DEBUG_ONLY(DebugLogError(_T("*** %hs - found source (%s) with wrong part count (%u) attached to file \"%s\" (partcount=%u)"), __FUNCTION__, (LPCTSTR)cur_src->DbgGetClientInfo(), cur_src->GetUpPartCount(), (LPCTSTR)GetFileName(), GetPartCount()));
				}
			} else {
				++cDbgNoSrc;
				// This client doesn't support upload chunk status. So just send it and hope for the best.
				bNeeded = true;
			}
		} else {
			ASSERT(forClient->GetUpPartCount() == 0);
			TRACE(_T("%hs, requesting client has no chunk status - %s"), __FUNCTION__, (LPCTSTR)forClient->DbgGetClientInfo());
			// remote client does not support upload chunk status, search sources which have at least one complete part
			// we could even sort the list of sources by available chunks to return as much sources as possible which
			// have the most available chunks. but this could be a noticeable performance problem.
			const uint8 *srcstatus = cur_src->GetUpPartStatus();
			if (srcstatus) {
				ASSERT(cur_src->GetUpPartCount() == GetPartCount());
				for (INT_PTR x = GetPartCount(); --x >= 0;)
					if (srcstatus[x]) {
						// this client has at least one chunk
						bNeeded = true;
						break;
					}
			} else {
				// This client doesn't support upload chunk status. So just send it and hope for the best.
				bNeeded = true;
			}
		}

		if (bNeeded) {
			++nCount;
			uint32 dwID = (byUsedVersion >= 3) ? cur_src->GetUserIDHybrid() : cur_src->GetIP();
			data.WriteUInt32(dwID);
			data.WriteUInt16(cur_src->GetUserPort());
			data.WriteUInt32(cur_src->GetServerIP());
			data.WriteUInt16(cur_src->GetServerPort());
			if (byUsedVersion >= 2)
				data.WriteHash16(cur_src->GetUserHash());
			if (byUsedVersion >= 4) {
				// ConnectSettings - SourceExchange V4
				// 4 Reserved (!)
				// 1 DirectCallback Supported/Available
				// 1 CryptLayer Required
				// 1 CryptLayer Requested
				// 1 CryptLayer Supported
				const uint8 uSupportsCryptLayer = static_cast<uint8>(cur_src->SupportsCryptLayer());
				const uint8 uRequestsCryptLayer = static_cast<uint8>(cur_src->RequestsCryptLayer());
				const uint8 uRequiresCryptLayer = static_cast<uint8>(cur_src->RequiresCryptLayer());
				//const uint8 uDirectUDPCallback = static_cast<uint8>(cur_src->SupportsDirectUDPCallback());
				const uint8 byCryptOptions = /*(uDirectUDPCallback << 3) |*/ (uRequiresCryptLayer << 2) | (uRequestsCryptLayer << 1) | (uSupportsCryptLayer << 0);
				data.WriteUInt8(byCryptOptions);
			}
			if (nCount > 500)
				break;
		}
	}
	TRACE(_T("%hs: Out of %u clients, %u had no valid chunk status\n"), __FUNCTION__, m_ClientUploadList.GetCount(), cDbgNoSrc);
	if (!nCount)
		return 0;
	data.Seek(plan.nCountSeekOffset, CFile::begin);
	data.WriteUInt16((uint16)nCount);

	Packet *result = new Packet(data, OP_EMULEPROT);
	result->opcode = plan.byAnswerOpcode;
	// (1+)16+2+501*(4+2+4+2+16+1) = 14547 (14548) bytes max.
	if (result->size > 354)
		result->PackPacket();
	if (thePrefs.GetDebugSourceExchange())
		AddDebugLogLine(false, _T("SXSend: Client source response SX2 Version=%u; Count=%u, %s, File=\"%s\""), byUsedVersion, nCount, (LPCTSTR)forClient->DbgGetClientInfo(), (LPCTSTR)GetFileName());
	return result;
}

void CKnownFile::SetFileComment(LPCTSTR pszComment)
{
	if (m_strComment.Compare(pszComment) != 0) {
		SetLastPublishTimeKadNotes(0);
		CIni ini(thePrefs.GetFileCommentsFilePath(), md4str(GetFileHash()));
		ini.WriteString(_T("Comment"), pszComment);
		m_strComment = pszComment;

		for (POSITION pos = m_ClientUploadList.GetHeadPosition(); pos != NULL;) {
			const POSITION posCurrent = pos;
			CUpDownClient *pClient = m_ClientUploadList.GetNext(pos);
			if (!IsLiveUploadingClient(pClient)) {
				RemoveStaleUploadingClient(posCurrent, pClient, _T("comment update pass"));
				continue;
			}
			pClient->SetCommentDirty();
		}
	}
}

void CKnownFile::SetFileRating(UINT uRating)
{
	if (m_uRating != uRating) {
		SetLastPublishTimeKadNotes(0);
		CIni ini(thePrefs.GetFileCommentsFilePath(), md4str(GetFileHash()));
		ini.WriteInt(_T("Rate"), uRating);
		m_uRating = uRating;

		for (POSITION pos = m_ClientUploadList.GetHeadPosition(); pos != NULL;) {
			const POSITION posCurrent = pos;
			CUpDownClient *pClient = m_ClientUploadList.GetNext(pos);
			if (!IsLiveUploadingClient(pClient)) {
				RemoveStaleUploadingClient(posCurrent, pClient, _T("rating update pass"));
				continue;
			}
			pClient->SetCommentDirty();
		}
	}
}

void CKnownFile::UpdateAutoUpPriority()
{
	if (IsAutoUpPriority()) {
		uint8 curPri = GetUpPriority();
		uint8 newPri;
		if (GetQueuedCount() > 20)
			newPri = PR_LOW;
		else if (GetQueuedCount() > 1)
			newPri = PR_NORMAL;
		else
			newPri = PR_HIGH;
		if (newPri != curPri) {
			SetUpPriority(newPri);
			theApp.emuledlg->sharedfileswnd->sharedfilesctrl.UpdateFile(this);
		}
	}
}

void CKnownFile::SetUpPriority(uint8 iNewUpPriority, bool bSave)
{
	m_iUpPriority = iNewUpPriority;
	ASSERT(m_iUpPriority == PR_VERYLOW || m_iUpPriority == PR_LOW || m_iUpPriority == PR_NORMAL || m_iUpPriority == PR_HIGH || m_iUpPriority == PR_VERYHIGH);

	if (IsPartFile() && bSave)
		static_cast<CPartFile*>(this)->SavePartFile();
}

void CKnownFile::RemoveMetaDataTags(UINT uTagType)
{
	static const struct
	{
		uint8	nID;
		uint8	nType;
	} _aEmuleMetaTags[] =
	{
		{ FT_MEDIA_ARTIST,  TAGTYPE_STRING },
		{ FT_MEDIA_ALBUM,   TAGTYPE_STRING },
		{ FT_MEDIA_TITLE,   TAGTYPE_STRING },
		{ FT_MEDIA_LENGTH,  TAGTYPE_UINT32 },
		{ FT_MEDIA_BITRATE, TAGTYPE_UINT32 },
		{ FT_MEDIA_CODEC,   TAGTYPE_STRING }
	};

	// 05-Jän-2004 [bc]: ed2k and Kad are already full of totally wrong and/or not properly attached meta data.
	// Take the chance to clean any available meta data tags and provide only tags which were determined by us.
	// Remove all meta tags. Never ever trust the meta tags received from other clients or servers.
	for (unsigned j = 0; j < _countof(_aEmuleMetaTags); ++j)
		if (uTagType == 0 || (uTagType == _aEmuleMetaTags[j].nType))
			for (INT_PTR i = m_taglist.GetCount(); --i >= 0;) {
				const CTag *pTag = m_taglist[i];
				if (pTag->GetNameID() == _aEmuleMetaTags[j].nID) {
					delete pTag;
					m_taglist.RemoveAt(i);
				}
			}

	m_uMetaDataVer = 0;
}

void CKnownFile::RemoveBrokenUnicodeMetaDataTags()
{
	static const struct
	{
		uint8	nID;
		uint8	nType;
	} _aEmuleMetaTags[] =
	{
		{ FT_MEDIA_ARTIST,  TAGTYPE_STRING },
		{ FT_MEDIA_ALBUM,   TAGTYPE_STRING },
		{ FT_MEDIA_TITLE,   TAGTYPE_STRING },
		{ FT_MEDIA_CODEC,   TAGTYPE_STRING }	// This one actually contains only ASCII
	};

	for (int j = (int)_countof(_aEmuleMetaTags); --j >= 0;) {
		const uint8 nameID = _aEmuleMetaTags[j].nID;
		for (INT_PTR i = m_taglist.GetCount(); --i >= 0;) {
			// Meta data strings of older eMule versions did store Unicode strings as MBCS strings,
			// which means that - depending on the Unicode string content - particular characters
			// got lost. Unicode characters which cannot get converted into the local codepage
			// will get replaced by Windows with a '?' character. So, to estimate if we have a
			// broken Unicode string (due to the conversion between Unicode/MBCS), we search the
			// strings for '?' characters. This is not 100% perfect, as it would also give
			// false results for strings which do contain the '?' character by intention. It also
			// would give wrong results for particular characters which got mapped to ASCII chars
			// due to the conversion from Unicode->MBCS. But at least it prevents us from deleting
			// all the existing meta data strings.
			const CTag *pTag = m_taglist[i];
			if (pTag->GetNameID() == nameID && pTag->IsStr() && _tcschr(pTag->GetStr(), _T('?')) != NULL) {
				delete pTag;
				m_taglist.RemoveAt(i);
			}
		}
	}
}

CStringA GetED2KAudioCodec(WORD wFormatTag)
{
	CStringA strCodec(GetAudioFormatCodecId(wFormatTag));
	return strCodec.Trim().MakeLower();
}

CStringA GetED2KVideoCodec(DWORD biCompression)
{
	LPCSTR p;
	switch (biCompression) {
	case BI_RGB:
		p = "rgb";
		break;
	case BI_RLE8:
		p = "rle8";
		break;
	case BI_RLE4:
		p = "rle4";
		break;
	case BI_BITFIELDS:
		p = "bitfields";
		break;
	case BI_JPEG:
		p = "jpeg";
		break;
	case BI_PNG:
		p = "png";
		break;
	default:
		p = NULL;
	}
	if (p)
		return CStringA(p);

	LPCSTR pszCompression = (LPCSTR)&biCompression;
	for (int i = 0; i < 4; ++i)
		if (!__iscsym(pszCompression[i]) && pszCompression[i] != '.' && pszCompression[i] != ' ')
			return CStringA();

	CStringA strCodec((char*)&biCompression, 4);
	if (strCodec.Trim().GetLength() < 2)
		return CStringA();
	return strCodec.MakeLower();
}

SMediaInfo* GetRIFFMediaInfo(LPCTSTR pszFullPath)
{
	bool bIsAVI;
	SMediaInfo *mi = new SMediaInfo;
	if (!GetRIFFHeaders(pszFullPath, mi, bIsAVI)) {
		delete mi;
		return NULL;
	}
	return mi;
}

SMediaInfo* GetRMMediaInfo(LPCTSTR pszFullPath)
{
	bool bIsRM;
	SMediaInfo *mi = new SMediaInfo;
	if (!GetRMHeaders(pszFullPath, mi, bIsRM)) {
		delete mi;
		return NULL;
	}
	return mi;
}

SMediaInfo* GetWMMediaInfo(LPCTSTR pszFullPath)
{
#ifdef HAVE_WMSDK_H
	bool bIsWM;
	SMediaInfo *mi = new SMediaInfo;
	if (!GetWMHeaders(pszFullPath, mi, bIsWM)) {
		delete mi;
		return NULL;
	}
	return mi;
#else//HAVE_WMSDK_H
	UNREFERENCED_PARAMETER(pszFullPath);
	return NULL;
#endif//HAVE_WMSDK_H
}

// Max. string length which is used for string meta tags like TAG_MEDIA_TITLE, TAG_MEDIA_ARTIST, ...
#define	MAX_METADATA_STR_LEN	80

void TruncateED2KMetaData(CString &rstrData)
{
	if (rstrData.Trim().GetLength() > MAX_METADATA_STR_LEN) {
		rstrData.Truncate(MAX_METADATA_STR_LEN);
		rstrData.Trim();
	}
}

void CKnownFile::UpdateMetaDataTags()
{
	// 05-Jän-2004 [bc]: ed2k and Kad are already full of totally wrong and/or improperly
	// attached meta data. Take the chance to clean any available meta data tags
	// and provide only tags which were determined by us.
	RemoveMetaDataTags();

	if (thePrefs.GetExtractMetaData() == 0)
		return;

	const EED2KFileType eFileType = GetED2KFileTypeID(GetFileName());
	if (eFileType != ED2KFT_AUDIO && eFileType != ED2KFT_VIDEO)
		return;

	const CString strFullPath = KnownFileMetadataSeams::BuildMetadataFilePath(GetPath(), GetFileName());
	const bool bIsMpegAudio = KnownFileMetadataSeams::IsMpegAudioMetadataExtension(GetFileName());
	bool bMetaDataApplied = false;

	auto ApplyMediaInfoTags = [&](SMediaInfo &mi)
	{
		mi.InitFileLength();
		const UINT uLengthSec = (UINT)mi.fFileLengthSec;
		CString strCodec;
		uint32 uBitrate = 0;

		if (mi.iVideoStreams > 0) {
			strCodec = mi.strVideoFormat;
			if (strCodec.IsEmpty())
				strCodec = CString(GetED2KVideoCodec(mi.video.bmiHeader.biCompression));
			if (mi.video.dwBitRate == _UI32_MAX) {
				if (uLengthSec != 0)
					uBitrate = (uint32)((((uint64)GetFileSize() / uLengthSec) * 8ull + 500) / 1000);
			} else if (mi.video.dwBitRate != 0) {
				uBitrate = (mi.video.dwBitRate + 500) / 1000;
			}
		} else if (mi.iAudioStreams > 0) {
			strCodec = mi.strAudioFormat;
			if (strCodec.IsEmpty())
				strCodec = CString(GetED2KAudioCodec(mi.audio.wFormatTag));
			if (mi.audio.nAvgBytesPerSec == _UI32_MAX) {
				if (uLengthSec != 0)
					uBitrate = (uint32)((((uint64)GetFileSize() / uLengthSec) * 8ull + 500) / 1000);
			} else if (mi.audio.nAvgBytesPerSec != 0) {
				uBitrate = (uint32)(((uint64)mi.audio.nAvgBytesPerSec * 8ull + 500) / 1000);
			}
		}

		if (uLengthSec) {
			AddTagUnique(new CTag(FT_MEDIA_LENGTH, (uint32)uLengthSec));
			m_uMetaDataVer = META_DATA_VER;
		}

		if (!strCodec.IsEmpty()) {
			AddTagUnique(new CTag(FT_MEDIA_CODEC, strCodec));
			m_uMetaDataVer = META_DATA_VER;
		}

		if (uBitrate) {
			AddTagUnique(new CTag(FT_MEDIA_BITRATE, uBitrate));
			m_uMetaDataVer = META_DATA_VER;
		}

		TruncateED2KMetaData(mi.strTitle);
		if (!mi.strTitle.IsEmpty()) {
			AddTagUnique(new CTag(FT_MEDIA_TITLE, mi.strTitle));
			m_uMetaDataVer = META_DATA_VER;
		}

		TruncateED2KMetaData(mi.strAuthor);
		if (!mi.strAuthor.IsEmpty()) {
			AddTagUnique(new CTag(FT_MEDIA_ARTIST, mi.strAuthor));
			m_uMetaDataVer = META_DATA_VER;
		}

		TruncateED2KMetaData(mi.strAlbum);
		if (!mi.strAlbum.IsEmpty()) {
			AddTagUnique(new CTag(FT_MEDIA_ALBUM, mi.strAlbum));
			m_uMetaDataVer = META_DATA_VER;
		}
	};

	try {
		SMediaInfo mediaInfo;
		mediaInfo.strFileName = GetFileName();
		bool bMediaInfoAvailable = false;
		if (GetMediaInfoDllInfo(strFullPath, GetFileSize(), &mediaInfo, false, true, &bMediaInfoAvailable)) {
			ApplyMediaInfoTags(mediaInfo);
			bMetaDataApplied = true;
		}
	} catch (CException *ex) {
		DebugLogWarning(_T("Unhandled exception while extracting file meta data through MediaInfo.dll from \"%s\"%s"), (LPCTSTR)strFullPath, (LPCTSTR)CExceptionStrDash(*ex));
		ex->Delete();
		ASSERT(0);
	} catch (...) {
		DebugLogWarning(_T("Unhandled exception while extracting file meta data through MediaInfo.dll from \"%s\" - unexpected exception"), (LPCTSTR)strFullPath);
		ASSERT(0);
	}

	if (!bMetaDataApplied) {
		SMediaInfo *mi = NULL;
		try {
			mi = GetRIFFMediaInfo(strFullPath);
			if (mi == NULL) {
				mi = GetRMMediaInfo(strFullPath);
				if (mi == NULL)
					mi = GetWMMediaInfo(strFullPath);
			}
			if (mi != NULL) {
				ApplyMediaInfoTags(*mi);
				bMetaDataApplied = true;
			}
		} catch (CException *ex) {
			DebugLogWarning(_T("Unhandled exception while extracting built-in file meta data from \"%s\"%s"), (LPCTSTR)strFullPath, (LPCTSTR)CExceptionStrDash(*ex));
			ex->Delete();
			ASSERT(0);
		} catch (...) {
			DebugLogWarning(_T("Unhandled exception while extracting built-in file meta data from \"%s\" - unexpected exception"), (LPCTSTR)strFullPath);
			ASSERT(0);
		}
		delete mi;
	}

	if (!bMetaDataApplied) {
		try {
			SMediaInfo mediaInfo;
			mediaInfo.strFileName = GetFileName();
			if (GetFallbackMediaInfo(strFullPath, GetFileSize(), &mediaInfo, false, true)) {
				ApplyMediaInfoTags(mediaInfo);
				bMetaDataApplied = true;
			}
		} catch (CException *ex) {
			DebugLogWarning(_T("Unhandled exception while extracting fallback media metadata from \"%s\"%s"), (LPCTSTR)strFullPath, (LPCTSTR)CExceptionStrDash(*ex));
			ex->Delete();
			ASSERT(0);
		} catch (...) {
			DebugLogWarning(_T("Unhandled exception while extracting fallback media metadata from \"%s\" - unexpected exception"), (LPCTSTR)strFullPath);
			ASSERT(0);
		}
	}

	if (!bMetaDataApplied && bIsMpegAudio) {
		wchar_t *pszText = NULL;
		try {
				// ID3LIB BUG: If there are ID3v2 _and_ ID3v1 tags available, id3lib
				// destroys (actually corrupts) the Unicode strings from ID3v2 tags due to
				// converting Unicode to ASCII and then conversion back from ASCII to Unicode.
				// To prevent this, we force the reading of ID3v2 tags only, in case there are
				// also ID3v1 tags available.
				ID3_Tag myTag;
				CStringA strFilePathA(strFullPath);
				size_t id3Size = myTag.Link(strFilePathA, ID3TT_ID3V2);
				if (id3Size == 0) {
					myTag.Clear();
					myTag.Link(strFilePathA, ID3TT_ID3V1);
				}

				const Mp3_Headerinfo *mp3info;
				mp3info = myTag.GetMp3HeaderInfo();
				if (mp3info) {
					// length
					if (mp3info->time) {
						AddTagUnique(new CTag(FT_MEDIA_LENGTH, (uint32)mp3info->time));
						m_uMetaDataVer = META_DATA_VER;
					}

					// here we could also create a "codec" ed2k meta tag. Though it probably would not
					// worth the extra bytes which would have to be sent to the servers.

					// bit rate
//no vbr bit rate	UINT uBitrate = (mp3info->vbr_bitrate ? mp3info->vbr_bitrate : mp3info->bitrate) / 1000;
					UINT uBitrate = mp3info->bitrate / 1000;
					if (uBitrate) {
						AddTagUnique(new CTag(FT_MEDIA_BITRATE, (uint32)uBitrate));
						m_uMetaDataVer = META_DATA_VER;
					}
				}

				ID3_Tag::Iterator *iter = myTag.CreateIterator();
				const ID3_Frame *frame;
				CString strText;
				while ((frame = iter->GetNext()) != NULL) {
					ID3_FrameID eFrameID = frame->GetID();
					switch (eFrameID) {
					case ID3FID_LEADARTIST:
						pszText = ID3_GetStringW(frame, ID3FN_TEXT);
						strText = pszText;
						TruncateED2KMetaData(strText);
						if (!strText.IsEmpty()) {
							AddTagUnique(new CTag(FT_MEDIA_ARTIST, strText));
							m_uMetaDataVer = META_DATA_VER;
						}
						break;
					case ID3FID_ALBUM:
						pszText = ID3_GetStringW(frame, ID3FN_TEXT);
						strText = pszText;
						TruncateED2KMetaData(strText);
						if (!strText.IsEmpty()) {
							AddTagUnique(new CTag(FT_MEDIA_ALBUM, strText));
							m_uMetaDataVer = META_DATA_VER;
						}
						break;
					case ID3FID_TITLE:
						pszText = ID3_GetStringW(frame, ID3FN_TEXT);
						strText = pszText;
						TruncateED2KMetaData(strText);
						if (!strText.IsEmpty()) {
							AddTagUnique(new CTag(FT_MEDIA_TITLE, strText));
							m_uMetaDataVer = META_DATA_VER;
						}
						break;
					default:
						continue;
					}
					delete[] pszText;
					pszText = NULL;
				}
				delete iter;
		} catch (CException *ex) {
			DebugLogWarning(_T("Unhandled exception while extracting MP3 file meta data from \"%s\"%s"), (LPCTSTR)strFullPath, (LPCTSTR)CExceptionStrDash(*ex));
			delete[] pszText;
			ex->Delete();
			ASSERT(0);
		} catch (...) {
			DebugLogWarning(_T("Unhandled exception while extracting MP3 file meta data from \"%s\" - unexpected exception"), (LPCTSTR)strFullPath);
			delete[] pszText;
			ASSERT(0);
		}
	}
}

bool CKnownFile::SetPublishedED2K(bool val, bool bUpdateUi)
{
	if (!ShouldNotifyPublishedED2KChange(m_PublishedED2K, val))
		return false;
	m_PublishedED2K = val;
	if (bUpdateUi)
		theApp.emuledlg->sharedfileswnd->sharedfilesctrl.UpdateFile(this);
	return true;
}

bool CKnownFile::PublishNotes()
{
	time_t tNow = time(NULL);
	if (tNow >= m_lastPublishTimeKadNotes && (!GetFileComment().IsEmpty() || GetFileRating() > 0)) {
		m_lastPublishTimeKadNotes = tNow + KADEMLIAREPUBLISHTIMEN;
		return true;
	}
	return false;
}

bool CKnownFile::PublishSrc()
{
	uint32 lastBuddyIP;
	time_t tNow = time(NULL);
	if (theApp.IsFirewalled()
		&& (Kademlia::CUDPFirewallTester::IsFirewalledUDP(true) || !Kademlia::CUDPFirewallTester::IsVerified()))
	{
		CUpDownClient *buddy = theApp.clientlist->GetBuddy();
		if (!buddy)
			return false;
		lastBuddyIP = buddy->GetIP();
		if (lastBuddyIP != m_lastBuddyIP) {
			SetLastPublishTimeKadSrc(tNow + KADEMLIAREPUBLISHTIMES, lastBuddyIP);
			return true;
		}
	} else
		lastBuddyIP = 0;

	if (tNow < m_lastPublishTimeKadSrc)
		return false;

	SetLastPublishTimeKadSrc(tNow + KADEMLIAREPUBLISHTIMES, lastBuddyIP);
	return true;
}

bool CKnownFile::IsMovie() const
{
	return GetED2KFileTypeID(GetFileName()) == ED2KFT_VIDEO;
}

// function assumes that this file is shared and that any needed permission to preview exists. checks have to be done before calling!
bool CKnownFile::StartPeerPreviewFrames(CUpDownClient *pSender, HWND hNotifyWnd)
{
	if (!IsMovie() || !PartFilePreviewSeams::ShouldAllowPeerPreview(
		thePrefs.IsPeerPreviewAllowed(),
		thePrefs.CanSeeShares() != vsfaNobody,
		PartFilePreviewSeams::IsValidConfiguredFfmpegPath(thePrefs.GetVideoThumbnailFfmpegPath())))
		return false;
	CPeerPreviewThread *pThread = static_cast<CPeerPreviewThread*>(AfxBeginThread(RUNTIME_CLASS(CPeerPreviewThread), THREAD_PRIORITY_BELOW_NORMAL, 0, CREATE_SUSPENDED));
	if (pThread == NULL)
		return false;
	pThread->SetValues(this, pSender, thePrefs.GetVideoThumbnailFfmpegPath(), hNotifyWnd);
	DWORD dwResumeError = ERROR_SUCCESS;
	if (!HelperThreadLaunchSeams::ResumeAutoDeleteSuspendedThread(pThread, dwResumeError)) {
		AddDebugLogLine(false, _T("Failed to resume peer-preview worker thread - Error %lu"), dwResumeError);
		return false;
	}
	return true;
}

void CKnownFile::PeerPreviewFinished(HBITMAP *imgResults, uint8 nFramesGrabbed, CUpDownClient *pSender)
{
	// continue processing
	if (theApp.clientlist != NULL && theApp.clientlist->ContainsClientPointer(pSender))
		pSender->SendPreviewAnswer(this, imgResults, nFramesGrabbed);
	else if (thePrefs.GetVerbose()) //probably the client got deleted while grabbing the frames
		AddDebugLogLine(false, _T("Couldn't find sender of peer preview request"));

	//cleanup
	for (int i = nFramesGrabbed; --i >= 0;)
		if (imgResults[i])
			::DeleteObject(imgResults[i]);
	delete[] imgResults;
}

CString CKnownFile::GetInfoSummary(bool bNoFormatCommands) const
{
	CString strFolder(PathHelpers::TrimTrailingSeparator(GetPath()));

	CString strAccepts, strRequests;
	strRequests.Format(_T("%u (%u)"), statistic.GetRequests(), statistic.GetAllTimeRequests());
	strAccepts.Format(_T("%u (%u)"), statistic.GetAccepts(), statistic.GetAllTimeAccepts());
	CString strTransferred(CastItoXBytes(statistic.GetTransferred()));
	strTransferred.AppendFormat(_T(" (%s)"), (LPCTSTR)CastItoXBytes(statistic.GetAllTimeTransferred()));
	CString strType(GetFileTypeDisplayStr());
	if (strType.IsEmpty())
		strType += _T('-');
	CString dbgInfo;
#ifdef _DEBUG
	dbgInfo.Format(_T("\nAICH Part HashSet: %s\nAICH Rec HashSet: %s"), m_FileIdentifier.HasExpectedAICHHashCount() ? _T("Yes") : _T("No")
		, IsAICHRecoverHashSetAvailable() ? _T("Yes") : _T("No"));
#endif

	CString info(GetFileName());
	info.AppendFormat(_T("\n")
		_T("eD2K %s %s\n")
		_T("%s: %s\n")
		_T("%s %s\n")
		_T("%s\n")
		_T("%s: %s\n")
		_T("%s: %s\n\n")
		_T("%s: %s\n")
		_T("%s: %s\n")
		_T("%s: %s\n")
		_T("%s: %s%s")
		, (LPCTSTR)GetResString(IDS_FD_HASH), (LPCTSTR)md4str(GetFileHash())
		, (LPCTSTR)GetResString(IDS_AICHHASH), (LPCTSTR)m_FileIdentifier.GetAICHHash().GetString()
		, (LPCTSTR)GetResString(IDS_FD_SIZE), (LPCTSTR)CastItoXBytes(GetFileSize())
		, bNoFormatCommands ? _T("") : _T("<br_head>")
		, (LPCTSTR)GetResString(IDS_TYPE), (LPCTSTR)strType
		, (LPCTSTR)GetResString(IDS_FOLDER), (LPCTSTR)strFolder
		, (LPCTSTR)GetResString(IDS_PRIORITY), (LPCTSTR)GetUpPriorityDisplayString()
		, (LPCTSTR)GetResString(IDS_SF_REQUESTS), (LPCTSTR)strRequests
		, (LPCTSTR)GetResString(IDS_SF_ACCEPTS), (LPCTSTR)strAccepts
		, (LPCTSTR)GetResString(IDS_SF_TRANSFERRED), (LPCTSTR)strTransferred, (LPCTSTR)dbgInfo
	);
	return info;
}

CString CKnownFile::GetUpPriorityDisplayString() const
{
	UINT uid;
	switch (GetUpPriority()) {
	case PR_VERYLOW:
		uid = IDS_PRIOVERYLOW;
		break;
	case PR_LOW:
		uid = IsAutoUpPriority() ? IDS_PRIOAUTOLOW : IDS_PRIOLOW;
		break;
	case PR_NORMAL:
		uid = IsAutoUpPriority() ? IDS_PRIOAUTONORMAL : IDS_PRIONORMAL;
		break;
	case PR_HIGH:
		uid = IsAutoUpPriority() ? IDS_PRIOAUTOHIGH : IDS_PRIOHIGH;
		break;
	case PR_VERYHIGH:
		uid = IDS_PRIORELEASE;
		break;
	default:
		uid = 0;
	}
	return uid ? GetResString(uid) : CString();
}

bool CKnownFile::ShouldPartiallyPurgeFile() const
{
	return thePrefs.DoPartiallyPurgeOldKnownFiles()
		&& m_timeLastSeen > 0
		&& time(NULL) >= m_timeLastSeen + OLDFILES_PARTIALLYPURGE;
}
