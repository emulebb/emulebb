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
#include "emule.h"
#include "ComInitializationSeams.h"
#include "KnownFileList.h"
#include "SharedFileList.h"
#include "SharedFileListSeams.h"
#include "Packets.h"
#ifndef CRYPTOPP_ENABLE_NAMESPACE_WEAK
#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1
#endif
#include "Kademlia/Kademlia/Kademlia.h"
#include "kademlia/kademlia/search.h"
#include "kademlia/kademlia/SearchManager.h"
#include "kademlia/kademlia/Tag.h"
#include "DownloadQueue.h"
#include "Statistics.h"
#include "Preferences.h"
#include "SharedFileIntakePolicy.h"
#include "OtherFunctions.h"
#include "ProtocolGuards.h"
#include "UpDownClient.h"
#include "ServerConnect.h"
#include "SafeFile.h"
#include "Server.h"
#include "PartFile.h"
#include "DisplayRefreshSeams.h"
#include "emuledlg.h"
#include "HelperThreadLaunchSeams.h"
#include "SharedFilesWnd.h"
#include "ClientList.h"
#include "Log.h"
#include "Collection.h"
#include "PathHelpers.h"
#include "kademlia/kademlia/UDPFirewallTester.h"
#include "LongPathSeams.h"
#include "MD5Sum.h"
#include "UserMsgs.h"
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
constexpr int kSharedHashCompletionPostRetries = 20;
constexpr DWORD kSharedHashCompletionPostRetryDelayMs = 25;
constexpr ULONGLONG kStartupHashProfileInitialSamples = 8;
constexpr ULONGLONG kStartupHashProfileSampleInterval = 256;
constexpr unsigned int kSharedHashUiDrainBatchMax = 100;
constexpr ULONGLONG kSharedHashUiDrainBudgetMs = 25;
constexpr ULONGLONG kMaxVisibleDuplicateSharedFileWarnings = 25;

bool ShouldEmitStartupHashProfileSample(const ULONGLONG ullCompletedFiles, const ULONGLONG ullFailedFiles, const INT_PTR iPendingFiles)
{
	const ULONGLONG ullObservedFiles = ullCompletedFiles + ullFailedFiles;
	return ullObservedFiles < kStartupHashProfileInitialSamples
		|| (ullObservedFiles % kStartupHashProfileSampleInterval) == 0
		|| iPendingFiles == 0;
}

bool ShouldLogStartupHashFile(const ULONGLONG ullCompletedFiles, const ULONGLONG ullFailedFiles, const bool bStartupDeferredHashingActive)
{
	return !bStartupDeferredHashingActive
		|| ShouldEmitStartupHashProfileSample(ullCompletedFiles, ullFailedFiles, 1);
}

CString NormalizeSharedFilePath(const CString &rstrPath)
{
	return PathHelpers::CanonicalizePathForComparison(rstrPath);
}

CString NormalizeSharedDirectoryPath(const CString &rstrPath)
{
	return PathHelpers::CanonicalizeDirectoryPath(rstrPath);
}

CString LexicallyNormalizeSharedDirectoryPath(const CString &rstrPath)
{
	return PathHelpers::EnsureTrailingSeparator(PathHelpers::CanonicalizePath(PathHelpers::StripExtendedLengthPrefix(rstrPath)));
}

CString LexicallyNormalizeSharedFilePath(const CString &rstrPath)
{
	return PathHelpers::CanonicalizePath(PathHelpers::StripExtendedLengthPrefix(rstrPath));
}

CString MakeSharedFileLookupKey(const CString &rstrPath)
{
	CString strKey(LexicallyNormalizeSharedFilePath(rstrPath));
	strKey.MakeLower();
	return strKey;
}

CString MakeSharedDirectoryLookupKey(const CString &rstrPath)
{
	CString strKey(LexicallyNormalizeSharedDirectoryPath(rstrPath));
	strKey.MakeLower();
	return strKey;
}

bool IsSingleFileRuleKeyWithinDirectory(const std::wstring &strDirectoryKey, const std::wstring &strFileKey)
{
	return strFileKey.length() > strDirectoryKey.length()
		&& strFileKey.compare(0, strDirectoryKey.length(), strDirectoryKey) == 0;
}

CString BuildSharedHashFilePath(const CString &strDirectory, const CString &strName)
{
	CString strFilePath(strDirectory);
	if (!strFilePath.IsEmpty() && strFilePath.Right(1) != _T("\\"))
		strFilePath.AppendChar(_T('\\'));
	strFilePath += strName;
	return strFilePath;
}

CString MakeSharedHashFilePathKey(const CString &strDirectory, const CString &strName)
{
	return LexicallyNormalizeSharedFilePath(BuildSharedHashFilePath(strDirectory, strName));
}

CString MakeSharedHashFilePathKey(const CKnownFile &file)
{
	return LexicallyNormalizeSharedFilePath(file.GetFilePath());
}

std::wstring MakeSharedHashInFlightLookupKey(const CString &strFilePathKey)
{
	CString strLookupKey(strFilePathKey);
	strLookupKey.MakeLower();
	return std::wstring(strLookupKey);
}

std::wstring MakeStartupCacheSnapshotKey(const CString &strDirectory)
{
	return std::wstring(LexicallyNormalizeSharedDirectoryPath(strDirectory));
}

bool HasSameDirectoryTextFast(const CString &strLeft, const CString &strRight)
{
	return MakeSharedDirectoryLookupKey(strLeft) == MakeSharedDirectoryLookupKey(strRight);
}

CKnownFileProgressTargetSnapshot MakePartFileProgressTargetSnapshot(const CPartFile &partFile)
{
	CKnownFileProgressTargetSnapshot snapshot{};
	md4cpy(snapshot.fileHash, partFile.GetFileHash());
	snapshot.fileSize = static_cast<uint64>(partFile.GetFileSize());
	snapshot.isPartFile = true;
	return snapshot;
}

void NotifyStartupSharedFilesModelChanged()
{
#if EMULE_COMPILED_STARTUP_PROFILING
	if (theApp.emuledlg != NULL && theApp.emuledlg->sharedfileswnd != NULL)
		theApp.emuledlg->sharedfileswnd->OnStartupSharedFilesModelChanged();
#endif
}
}


typedef CSimpleArray<CKnownFile*> CSimpleKnownFileArray;
#define	SHAREDFILES_FILE	_T("sharedfiles.dat")


///////////////////////////////////////////////////////////////////////////////
// CPublishKeyword

class CPublishKeyword
{
public:
	explicit CPublishKeyword(const Kademlia::CKadTagValueString &rstrKeyword)
		: m_strKeyword(rstrKeyword)
	{
		// min. keyword char is allowed to be < 3 in some cases (see also 'CSearchManager::GetWords')
		//ASSERT(rstrKeyword.GetLength() >= 3);
		ASSERT(!rstrKeyword.IsEmpty());
		KadGetKeywordHash(rstrKeyword, &m_nKadID);
		SetNextPublishTime(0);
		SetPublishedCount(0);
	}

	const Kademlia::CUInt128& GetKadID() const			{ return m_nKadID; }
	const Kademlia::CKadTagValueString &GetKeyword() const { return m_strKeyword; }
	int GetRefCount() const								{ return m_aFiles.GetSize(); }
	const CSimpleKnownFileArray &GetReferences() const	{ return m_aFiles; }

	time_t GetNextPublishTime() const					{ return m_tNextPublishTime; }
	void SetNextPublishTime(time_t tNextPublishTime)	{ m_tNextPublishTime = tNextPublishTime; }

	UINT GetPublishedCount() const						{ return m_uPublishedCount; }
	void SetPublishedCount(UINT uPublishedCount)		{ m_uPublishedCount = uPublishedCount; }
	void IncPublishedCount()							{ ++m_uPublishedCount; }

	BOOL AddRef(CKnownFile *pFile)
	{
		if (m_aFiles.Find(pFile) >= 0) {
			ASSERT(0);
			return FALSE;
		}
		return m_aFiles.Add(pFile);
	}

	int RemoveRef(CKnownFile *pFile)
	{
		m_aFiles.Remove(pFile);
		return m_aFiles.GetSize();
	}

	void RemoveAllReferences()
	{
		m_aFiles.RemoveAll();
	}

	void RotateReferences(int iRotateSize)
	{
		CKnownFile **ppRotated = reinterpret_cast<CKnownFile**>(malloc(m_aFiles.m_nAllocSize * sizeof(*m_aFiles.GetData())));
		if (ppRotated != NULL) {
			int i = m_aFiles.GetSize() - iRotateSize;
			ASSERT(i > 0);
			memcpy(ppRotated, m_aFiles.GetData() + iRotateSize, i * sizeof(*m_aFiles.GetData()));
			memcpy(ppRotated + i, m_aFiles.GetData(), iRotateSize * sizeof(*m_aFiles.GetData()));
			free(m_aFiles.GetData());
			m_aFiles.m_aT = ppRotated;
		}
	}

protected:
	CSimpleKnownFileArray m_aFiles;
	Kademlia::CKadTagValueString m_strKeyword;
	Kademlia::CUInt128 m_nKadID;
	time_t m_tNextPublishTime;
	UINT m_uPublishedCount;
};


///////////////////////////////////////////////////////////////////////////////
// CPublishKeywordList

class CPublishKeywordList
{
public:
	CPublishKeywordList();
	~CPublishKeywordList();

	void AddKeywords(CKnownFile *pFile);
	void RemoveKeywords(CKnownFile *pFile);
	void RemoveAllKeywords();

	void RemoveAllKeywordReferences();
	void PurgeUnreferencedKeywords();

	INT_PTR GetCount() const								{ return m_lstKeywords.GetCount(); }

	CPublishKeyword *GetNextKeyword();
	void ResetNextKeyword();

	time_t GetNextPublishTime() const						{ return m_tNextPublishKeywordTime; }
	void SetNextPublishTime(time_t tNextPublishKeywordTime)	{ m_tNextPublishKeywordTime = tNextPublishKeywordTime; }

#ifdef _DEBUG
	void Dump();
#endif

protected:
	// can't use a CMap - too many disadvantages in processing the 'list'
	//CTypedPtrMap<CMapStringToPtr, CString, CPublishKeyword*> m_lstKeywords;
	struct KeywordIndexEntry
	{
		CPublishKeyword *pKeyword;
		POSITION pos;
	};

	CTypedPtrList<CPtrList, CPublishKeyword*> m_lstKeywords;
	std::unordered_map<std::wstring, KeywordIndexEntry> m_mapKeywordIndex;
	POSITION m_posNextKeyword;
	time_t m_tNextPublishKeywordTime;

	static std::wstring MakeKeywordIndexKey(const CStringW &rstrKeyword);
	CPublishKeyword *FindKeyword(const CStringW &rstrKeyword, POSITION *ppos = NULL) const;
};

CPublishKeywordList::CPublishKeywordList()
{
	ResetNextKeyword();
	SetNextPublishTime(0);
}

CPublishKeywordList::~CPublishKeywordList()
{
	RemoveAllKeywords();
}

CPublishKeyword *CPublishKeywordList::GetNextKeyword()
{
	if (m_posNextKeyword == NULL) {
		m_posNextKeyword = m_lstKeywords.GetHeadPosition();
		if (m_posNextKeyword == NULL)
			return NULL;
	}
	return m_lstKeywords.GetNext(m_posNextKeyword);
}

void CPublishKeywordList::ResetNextKeyword()
{
	m_posNextKeyword = m_lstKeywords.GetHeadPosition();
}

std::wstring CPublishKeywordList::MakeKeywordIndexKey(const CStringW &rstrKeyword)
{
	return std::wstring(static_cast<LPCWSTR>(rstrKeyword), static_cast<size_t>(rstrKeyword.GetLength()));
}

CPublishKeyword *CPublishKeywordList::FindKeyword(const CStringW &rstrKeyword, POSITION *ppos) const
{
	const auto it = m_mapKeywordIndex.find(MakeKeywordIndexKey(rstrKeyword));
	if (it == m_mapKeywordIndex.end())
		return NULL;
	if (ppos)
		*ppos = it->second.pos;
	return it->second.pKeyword;
}

void CPublishKeywordList::AddKeywords(CKnownFile *pFile)
{
	const Kademlia::WordList &wordlist(pFile->GetKadKeywords());
	//ASSERT(!wordlist.empty());
	for (Kademlia::WordList::const_iterator it = wordlist.begin(); it != wordlist.end(); ++it) {
		const CStringW &strKeyword(*it);
		CPublishKeyword *pPubKw = FindKeyword(strKeyword);
		if (pPubKw == NULL) {
			pPubKw = new CPublishKeyword(Kademlia::CKadTagValueString(strKeyword));
			const POSITION posNew = m_lstKeywords.AddTail(pPubKw);
			m_mapKeywordIndex.emplace(MakeKeywordIndexKey(strKeyword), KeywordIndexEntry{pPubKw, posNew});
			SetNextPublishTime(0);
		}
		if (pPubKw->AddRef(pFile) && pPubKw->GetNextPublishTime() > MIN2S(30)) {
			// User may be adding and removing files, so if this is a keyword that
			// has already been published, we reduce the time, but still give the user
			// enough time to finish what they are doing.
			// If this is a hot node, the Load list will prevent from republishing.
			pPubKw->SetNextPublishTime(MIN2S(30));
		}
	}
}

void CPublishKeywordList::RemoveKeywords(CKnownFile *pFile)
{
	const Kademlia::WordList &wordlist = pFile->GetKadKeywords();
	//ASSERT(!wordlist.empty());
	for (Kademlia::WordList::const_iterator it = wordlist.begin(); it != wordlist.end(); ++it) {
		const CStringW &strKeyword(*it);
		POSITION pos;
		CPublishKeyword *pPubKw = FindKeyword(strKeyword, &pos);
		if (pPubKw != NULL && pPubKw->RemoveRef(pFile) == 0) {
			if (pos == m_posNextKeyword)
				(void)m_lstKeywords.GetNext(m_posNextKeyword);
			m_mapKeywordIndex.erase(MakeKeywordIndexKey(strKeyword));
			m_lstKeywords.RemoveAt(pos);
			delete pPubKw;
			SetNextPublishTime(0);
		}
	}
}

void CPublishKeywordList::RemoveAllKeywords()
{
	while (!m_lstKeywords.IsEmpty())
		delete m_lstKeywords.RemoveHead();
	m_mapKeywordIndex.clear();
	ResetNextKeyword();
	SetNextPublishTime(0);
}

void CPublishKeywordList::RemoveAllKeywordReferences()
{
	for (POSITION pos = m_lstKeywords.GetHeadPosition(); pos != NULL;)
		m_lstKeywords.GetNext(pos)->RemoveAllReferences();
}

void CPublishKeywordList::PurgeUnreferencedKeywords()
{
	for (POSITION pos = m_lstKeywords.GetHeadPosition(); pos != NULL;) {
		POSITION posLast = pos;
		const CPublishKeyword *pPubKw = m_lstKeywords.GetNext(pos);
		if (pPubKw->GetRefCount() == 0) {
			if (posLast == m_posNextKeyword)
				m_posNextKeyword = pos;
			m_mapKeywordIndex.erase(MakeKeywordIndexKey(pPubKw->GetKeyword()));
			m_lstKeywords.RemoveAt(posLast);
			delete pPubKw;
			SetNextPublishTime(0);
		}
	}
}

#ifdef _DEBUG
void CPublishKeywordList::Dump()
{
	unsigned i = 0;
	for (POSITION pos = m_lstKeywords.GetHeadPosition(); pos != NULL;) {
		CPublishKeyword *pPubKw = m_lstKeywords.GetNext(pos);
		TRACE(_T("%3u: %-10ls  ref=%u  %s\n"), i, (LPCTSTR)pPubKw->GetKeyword(), pPubKw->GetRefCount(), (LPCTSTR)CastSecondsToHM(pPubKw->GetNextPublishTime()));
		++i;
	}
}
#endif

///////////////////////////////////////////////////////////////////////////////
// CAddFileThread

IMPLEMENT_DYNCREATE(CAddFileThread, CWinThread)

CAddFileThread::CAddFileThread()
	: m_pOwner()
	, m_partfile()
{
}

void CAddFileThread::SetValues(CSharedFileList *pOwner, LPCTSTR directory, LPCTSTR filename, LPCTSTR strSharedDir, CPartFile *partfile)
{
	m_pOwner = pOwner;
	m_strDirectory = directory;
	m_strFilename = filename;
	m_partfile = partfile;
	m_strSharedDir = strSharedDir;
}

CAddFileThread *CreateSuspendedPartFileHashThread(LPCTSTR pszDirectory, LPCTSTR pszFilename, CPartFile *pPartFile)
{
	CAddFileThread *pAddFileThread = static_cast<CAddFileThread*>(AfxBeginThread(RUNTIME_CLASS(CAddFileThread), THREAD_PRIORITY_BELOW_NORMAL, 0, CREATE_SUSPENDED));
	if (pAddFileThread != NULL)
		pAddFileThread->SetValues(NULL, pszDirectory, pszFilename, _T(""), pPartFile);
	return pAddFileThread;
}

BOOL CAddFileThread::InitInstance()
{
	InitThreadLocale();
	return TRUE;
}

int CAddFileThread::Run()
{
	DbgSetThreadName("Hashing %s", (LPCTSTR)m_strFilename);
	if (!(m_pOwner || m_partfile) || m_strFilename.IsEmpty() || theApp.IsClosing())
		return 0;

	const ComInitializationSeams::CScopedComInitialize coInitialize;

	// Locking this hashing thread is needed because we may create a few of those threads
	// at startup when rehashing potentially corrupted downloading part files.
	// If all those hash threads would run concurrently, the I/O system would be under
	// very heavy load and slowly progressing
	CSingleLock hashingLock(&theApp.hashing_mut, TRUE); // hash only one file at a time

	CString strFilePath;
	strFilePath = m_strDirectory;
	if (!strFilePath.IsEmpty() && strFilePath[strFilePath.GetLength() - 1] != _T('\\'))
		strFilePath += _T('\\');
	strFilePath += m_strFilename;
#if EMULE_COMPILED_STARTUP_PROFILING
	const ULONGLONG ullHashStartUs = theApp.GetStartupProfileTimestampUs();
#endif
	if (m_partfile)
		Log(_T("%s \"%s\" \"%s\""), (LPCTSTR)GetResString(IDS_HASHINGFILE), (LPCTSTR)m_partfile->GetFileName(), (LPCTSTR)strFilePath);
	else
		Log(_T("%s \"%s\""), (LPCTSTR)GetResString(IDS_HASHINGFILE), (LPCTSTR)strFilePath);

	if (!theApp.IsClosing()) {
		CKnownFile *newKnown = new CKnownFile();
		CKnownFileProgressTargetSnapshot progressTarget{};
		const CKnownFileProgressTargetSnapshot *pProgressTarget = NULL;
		uint32 nHashLayoutGeneration = 0;
		if (m_partfile != NULL) {
			progressTarget = MakePartFileProgressTargetSnapshot(*m_partfile);
			pProgressTarget = &progressTarget;
			nHashLayoutGeneration = m_partfile->GetHashLayoutGeneration();
		}
		if (newKnown->CreateFromFile(m_strDirectory, m_strFilename, pProgressTarget)) { // SLUGFILLER: SafeHash - in case of shutdown while still hashing
			newKnown->SetPartFileHashLayoutGenerationSnapshot(nHashLayoutGeneration);
			newKnown->SetSharedDirectory(m_strSharedDir);
			if (m_partfile && m_partfile->GetFileOp() == PFOP_HASHING)
				m_partfile->SetFileOp(PFOP_NONE);
			if (!theApp.emuledlg->PostMessage(TM_FINISHEDHASHING, (m_pOwner ? 0 : (WPARAM)m_partfile), (LPARAM)newKnown))
				delete newKnown;
		} else {
			if (m_partfile && m_partfile->GetFileOp() == PFOP_HASHING)
				m_partfile->SetFileOp(PFOP_NONE);

			// SLUGFILLER: SafeHash - inform main program of hash failure
			if (m_pOwner) {
				UnknownFile_Struct *hashed = new UnknownFile_Struct;
				hashed->strDirectory = m_strDirectory;
				hashed->strName = m_strFilename;
				if (!theApp.emuledlg->PostMessage(TM_HASHFAILED, 0, (LPARAM)hashed))
					delete hashed;
			}
			// SLUGFILLER: SafeHash
			delete newKnown;
		}
	}

#if EMULE_COMPILED_STARTUP_PROFILING
	if (theApp.IsStartupProfilingEnabled()) {
		CString strPhase;
		strPhase.Format(_T("shared.hash.file.run (%s)"), (LPCTSTR)strFilePath);
		theApp.AppendStartupProfileLine(strPhase, theApp.GetStartupProfileElapsedUs(ullHashStartUs), ullHashStartUs);
	}
#endif

	hashingLock.Unlock();
	return 0;
}

///////////////////////////////////////////////////////////////////////////////
// CSharedFileHashThread

IMPLEMENT_DYNCREATE(CSharedFileHashThread, CWinThread)

CSharedFileHashThread::CSharedFileHashThread()
	: m_pOwner()
{
}

BOOL CSharedFileHashThread::InitInstance()
{
	InitThreadLocale();
	return TRUE;
}

int CSharedFileHashThread::Run()
{
	DbgSetThreadName("Shared file hashing");
	if (m_pOwner == NULL)
		return 0;

	const ComInitializationSeams::CScopedComInitialize coInitialize;
	CSharedFileList::SharedHashJob job;
	while (m_pOwner->WaitForSharedHashJob(job))
		m_pOwner->RunSharedHashJob(job);
	return 0;
}

///////////////////////////////////////////////////////////////////////////////
// CSharedFileList

void CSharedFileList::AddDirectory(const CString &strDir, CStringList &dirlist, std::unordered_set<std::wstring> &rAddedDirectoryKeys, const bool bAllowStartupCache)
{
	ASSERT(strDir.Right(1) == _T("\\"));
	CString strCanonicalDir;
	++m_startupScanStats.uRequestedDirectories;
	if (bAllowStartupCache) {
		const CString strLexicalDir(LexicallyNormalizeSharedDirectoryPath(strDir));
		const auto itRecord = m_startupCacheRecords.find(MakeStartupCacheKey(strLexicalDir));
		strCanonicalDir = (itRecord != m_startupCacheRecords.end()) ? itRecord->second.strDirectoryPath : NormalizeSharedDirectoryPath(strDir);
	} else
		strCanonicalDir = NormalizeSharedDirectoryPath(strDir);
	if (strCanonicalDir.GetLength() > 248)
		++m_startupScanStats.uDirectoriesOver248Chars;
	if (strCanonicalDir.GetLength() > 260)
		++m_startupScanStats.uPathsOver260Chars;
	if (!rAddedDirectoryKeys.insert(MakeStartupCacheKey(strCanonicalDir)).second) {
		++m_startupScanStats.uDuplicateDirectories;
		return;
	}
	++m_startupScanStats.uDedupedDirectories;

	dirlist.AddHead(strCanonicalDir);
	if (!bAllowStartupCache || !TryRehydrateSharedDirectoryFromCache(strCanonicalDir)) {
		++m_startupScanStats.uDirectoriesRescanned;
		AddFilesFromDirectory(strCanonicalDir);
	} else
		++m_startupScanStats.uDirectoriesFromCache;
}

CSharedFileList::CSharedFileList(CServerConnect *in_server)
	: server(in_server)
	, output()
	, m_currFileSrc()
	, m_currFileNotes()
	, m_lastPublishKadSrc()
	, m_lastPublishKadNotes()
	, m_lastPublishED2K()
	, m_lastPublishED2KFlag(true)
	, bHaveSingleSharedFiles()
	, m_bStartupCacheDirty(false)
	, m_bStartupDuplicateReuseActive(false)
	, m_bStartupDeferredHashingActive(false)
	, m_nLastStartupCacheSave()
	, m_nStartupCacheDirtyTick()
	, m_uStartupHashCompletedFiles()
	, m_uStartupHashFailedFiles()
	, m_uDuplicateSharedFileWarningsShown()
	, m_uDuplicateSharedFileWarningsSuppressed()
	, m_hSharedHashQueueEvent(::CreateEvent(NULL, FALSE, FALSE, NULL))
	, m_pSharedHashThread()
	, m_sharedHashQueue()
	, m_sharedHashPendingCompletions()
	, m_sharedHashInFlightKeys()
	, m_sharedHashActiveJob()
	, m_bSharedHashDrainPosted(false)
	, m_bSharedHashWorkerCanHash(false)
	, m_bSharedHashWorkerExitRequested(false)
	, m_bSharedHashActive(false)
	, m_bSharedHashShutdownSignaled(false)
	, m_bDuplicateSharedFileWarningSuppressionLogged(false)
	, m_bStartupCacheInvalidatedByInterruptedHashing(false)
	, m_bStartupCacheSaveShutdownAbandoned(false)
	, m_bStartupCacheFilePresent(false)
	, m_bStartupCacheLoaded(false)
	, m_bStartupCacheRejected(false)
	, m_bStartupCacheRemoved(false)
	, m_bDuplicatePathCacheLoaded(false)
	, m_bDuplicatePathCacheRejected(false)
	, m_bDuplicatePathCacheRemoved(false)
	, m_uStartupCacheRecordsLoaded()
	, m_uStartupCacheVolumesLoaded()
	, m_uDuplicatePathCacheRecordsLoaded()
	, m_bStartupCacheSaveRunning(false)
	, m_bStartupCacheSaveRunAfterCurrent(false)
	, m_pStartupCacheSaveOperation()
	, m_eStartupCacheSavePhase(StartupCacheSavePhase::Idle)
	, m_uStartupCacheSaveDirectoriesDone()
	, m_uStartupCacheSaveDirectoriesTotal()
{
	m_Files_map.InitHashTable(1031);
	m_keywords = new CPublishKeywordList;
#if defined(_DEVBUILD)
	// In development versions we create a test file which is published in order to make
	// testing easier by allowing easily find files which are published and shared by "new" nodes
	// Compose the name of the test file
	m_strBetaFileName.Format(_T("eMule%u.%u%c.%u Beta Testfile "), CemuleApp::m_nVersionMjr
		, CemuleApp::m_nVersionMin, _T('a') + CemuleApp::m_nVersionUpd, CemuleApp::m_nVersionBld);
	const MD5Sum md5(m_strBetaFileName + CemuleApp::m_sPlatform);
	m_strBetaFileName.AppendFormat(_T("%s.txt"), (LPCTSTR)md5.GetHashString().Left(6));
#endif
#if EMULE_COMPILED_STARTUP_PROFILING
	const ULONGLONG ullSingleSharedLoadStartUs = theApp.IsStartupProfilingEnabled() ? theApp.GetStartupProfileTimestampUs() : 0ui64;
#endif
	LoadSingleSharedFilesList();
#if EMULE_COMPILED_STARTUP_PROFILING
	if (theApp.IsStartupProfilingEnabled()) {
		theApp.AppendStartupProfileCounter(_T("shared.single_file_rules.loaded"), static_cast<ULONGLONG>(m_liSingleSharedFiles.GetCount()), _T("files"));
		theApp.AppendStartupProfileCounter(_T("shared.single_file_excludes.loaded"), static_cast<ULONGLONG>(m_liSingleExcludedFiles.GetCount()), _T("files"));
		theApp.AppendStartupProfileLine(_T("shared.single_file_rules.load"), theApp.GetStartupProfileElapsedUs(ullSingleSharedLoadStartUs), ullSingleSharedLoadStartUs);
	}
#endif
#if EMULE_COMPILED_STARTUP_PROFILING
	const ULONGLONG ullStartupCacheLoadStartUs = theApp.IsStartupProfilingEnabled() ? theApp.GetStartupProfileTimestampUs() : 0ui64;
#endif
	(void)TryLoadStartupCache();
	(void)TryLoadDuplicatePathCache();
#if EMULE_COMPILED_STARTUP_PROFILING
	if (theApp.IsStartupProfilingEnabled())
		theApp.AppendStartupProfileLine(_T("shared.startup_caches.load"), theApp.GetStartupProfileElapsedUs(ullStartupCacheLoadStartUs), ullStartupCacheLoadStartUs);
#endif
	m_nLastStartupCacheSave = ::GetTickCount64();
#if EMULE_COMPILED_STARTUP_PROFILING
	const ULONGLONG ullInitialScanStartUs = theApp.IsStartupProfilingEnabled() ? theApp.GetStartupProfileTimestampUs() : 0ui64;
#endif
	FindSharedFiles(true);
#if EMULE_COMPILED_STARTUP_PROFILING
	if (theApp.IsStartupProfilingEnabled())
		theApp.AppendStartupProfileLine(_T("shared.scan.initial_total"), theApp.GetStartupProfileElapsedUs(ullInitialScanStartUs), ullInitialScanStartUs);
#endif
	(void)EnsureSharedHashWorkerStarted();
}

CSharedFileList::~CSharedFileList()
{
	while (!ShutdownSharedHashWorkerStep(INFINITE)) {
	}
	if (HasPendingStartupCacheSaveWork())
		(void)AbandonStartupCacheSaveForShutdown();
	ASSERT(!HasPendingStartupCacheSaveWork());
	if (m_hSharedHashQueueEvent != NULL) {
		VERIFY(::CloseHandle(m_hSharedHashQueueEvent));
		m_hSharedHashQueueEvent = NULL;
	}
	delete m_keywords;

#if defined(_DEVBUILD)
	//Delete the test file
	CString sTest(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
	sTest += m_strBetaFileName;
	(void)LongPathSeams::DeleteFileIfExists(sTest);
#endif
}

void CSharedFileList::CopySharedFileMap(CKnownFilesMap &Files_Map)
{
	Files_Map.RemoveAll();
	CSingleLock listlock(&m_mutWriteList, TRUE);
	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair))
		Files_Map[pair->key] = pair->value;
}

CKnownFile* CSharedFileList::GetFileByPath(const CString &strFilePath)
{
	const CString strFilePathKey(MakeSharedFileLookupKey(strFilePath));
	CSingleLock listlock(&m_mutWriteList, TRUE);
	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair)) {
		if (pair->value != NULL && MakeSharedFileLookupKey(pair->value->GetFilePath()) == strFilePathKey)
			return pair->value;
	}
	return NULL;
}

bool CSharedFileList::EnsureSharedHashWorkerStarted()
{
	if (m_pSharedHashThread != NULL)
		return true;
	if (m_hSharedHashQueueEvent == NULL)
		return false;

	CSharedFileHashThread *pThread = static_cast<CSharedFileHashThread*>(
		AfxBeginThread(RUNTIME_CLASS(CSharedFileHashThread), THREAD_PRIORITY_BELOW_NORMAL, 0, CREATE_SUSPENDED));
	if (pThread == NULL)
		return false;

	pThread->SetOwner(this);
	DWORD dwResumeError = ERROR_SUCCESS;
	if (!HelperThreadLaunchSeams::OwnAndResumeSuspendedThread(m_pSharedHashThread, pThread, dwResumeError)) {
		DebugLogError(_T("Failed to resume shared-file hash worker thread - Error %lu"), dwResumeError);
		return false;
	}
	return true;
}

void CSharedFileList::SignalSharedHashWorker()
{
	bool bAllowStart = true;
	{
		CSingleLock lock(&m_mutSharedHashQueue, TRUE);
		bAllowStart = !m_bSharedHashShutdownSignaled;
	}
	if (bAllowStart)
		(void)EnsureSharedHashWorkerStarted();
	if (m_hSharedHashQueueEvent != NULL)
		::SetEvent(m_hSharedHashQueueEvent);
}

void CSharedFileList::QueueSharedFileForHash(const CString &strDirectory, const CString &strName, const CString &strSharedDirectory)
{
	SharedHashJob job = {};
	job.strDirectory = strDirectory;
	job.strName = strName;
	job.strSharedDirectory = strSharedDirectory;
	job.strFilePathKey = MakeSharedHashFilePathKey(strDirectory, strName);
#if EMULE_COMPILED_STARTUP_PROFILING
	job.ullQueuedTimestampUs = theApp.IsStartupProfilingEnabled() ? theApp.GetStartupProfileTimestampUs() : 0ui64;
#endif

	{
		CSingleLock lock(&m_mutSharedHashQueue, TRUE);
		if (m_bSharedHashWorkerExitRequested)
			return;
		if (!m_sharedHashInFlightKeys.insert(MakeSharedHashInFlightLookupKey(job.strFilePathKey)).second)
			return;
		m_sharedHashQueue.push_back(job);
	}
	SignalSharedHashWorker();
}

bool CSharedFileList::WaitForSharedHashJob(SharedHashJob &rJob)
{
	for (;;) {
		{
			CSingleLock lock(&m_mutSharedHashQueue, TRUE);
			if (m_bSharedHashWorkerExitRequested)
				return false;
			const SharedFileListSeams::SharedHashWorkerStartState startState = {
				m_bSharedHashWorkerCanHash,
				!m_sharedHashQueue.empty(),
				static_cast<unsigned int>(m_sharedHashPendingCompletions.size())
			};
			if (SharedFileListSeams::ShouldStartSharedHashJob(startState)) {
				rJob = m_sharedHashQueue.front();
				m_sharedHashQueue.pop_front();
				m_sharedHashActiveJob = rJob;
				m_bSharedHashActive = true;
#if EMULE_COMPILED_STARTUP_PROFILING
				const INT_PTR iPendingHashFiles = static_cast<INT_PTR>(m_sharedHashQueue.size() + m_sharedHashPendingCompletions.size() + 1);
				if (theApp.IsStartupProfilingEnabled()
					&& ShouldEmitStartupHashProfileSample(m_uStartupHashCompletedFiles, m_uStartupHashFailedFiles, iPendingHashFiles))
				{
					theApp.AppendStartupProfileCounter(_T("shared.hash.queue_depth_before_start"), static_cast<ULONGLONG>(m_sharedHashQueue.size() + 1), _T("files"));
					theApp.AppendStartupProfileCounter(_T("shared.hash.waiting_queue_depth"), static_cast<ULONGLONG>(m_sharedHashQueue.size()), _T("files"));
					theApp.AppendStartupProfileCounter(_T("shared.hash.currently_hashing"), 1, _T("files"));
				}
#endif
				return true;
			}
		}

		if (m_hSharedHashQueueEvent == NULL)
			return false;
		(void)::WaitForSingleObject(m_hSharedHashQueueEvent, INFINITE);
	}
}

void CSharedFileList::RunSharedHashJob(const SharedHashJob &rJob)
{
	CString strFilePath(BuildSharedHashFilePath(rJob.strDirectory, rJob.strName));
#if EMULE_COMPILED_STARTUP_PROFILING
	const bool bEmitStartupHashProfileSample = theApp.IsStartupProfilingEnabled()
		&& ShouldEmitStartupHashProfileSample(m_uStartupHashCompletedFiles, m_uStartupHashFailedFiles, GetHashingCount());
	if (bEmitStartupHashProfileSample && rJob.ullQueuedTimestampUs != 0) {
		const ULONGLONG ullHashStartUs = theApp.GetStartupProfileTimestampUs();
		CString strPhase;
		strPhase.Format(_T("shared.hash.file.queue_wait (%s)"), (LPCTSTR)strFilePath);
		theApp.AppendStartupProfileLine(
			strPhase,
			(ullHashStartUs >= rJob.ullQueuedTimestampUs) ? (ullHashStartUs - rJob.ullQueuedTimestampUs) : 0ui64,
			rJob.ullQueuedTimestampUs);
	}
	const ULONGLONG ullHashStartUs = theApp.GetStartupProfileTimestampUs();
#endif

	DbgSetThreadName("Shared hashing %s", (LPCTSTR)rJob.strName);
	CSingleLock hashingLock(&theApp.hashing_mut, TRUE);
	if (!theApp.IsClosing()
		&& ShouldLogStartupHashFile(m_uStartupHashCompletedFiles, m_uStartupHashFailedFiles, IsStartupDeferredHashingActive()))
	{
		Log(_T("%s \"%s\""), (LPCTSTR)GetResString(IDS_HASHINGFILE), (LPCTSTR)strFilePath);
	}

	CKnownFile *pKnownFile = NULL;
	bool bSuccess = false;
	if (!theApp.IsClosing()) {
		pKnownFile = new CKnownFile();
		if (pKnownFile->CreateFromFile(rJob.strDirectory, rJob.strName, NULL)) {
			pKnownFile->SetSharedDirectory(rJob.strSharedDirectory);
			bSuccess = true;
		} else {
			delete pKnownFile;
			pKnownFile = NULL;
		}
	}
#if EMULE_COMPILED_STARTUP_PROFILING
	if (bEmitStartupHashProfileSample) {
		CString strPhase;
		strPhase.Format(_T("shared.hash.file.run (%s)"), (LPCTSTR)strFilePath);
		theApp.AppendStartupProfileLine(strPhase, theApp.GetStartupProfileElapsedUs(ullHashStartUs), ullHashStartUs);
	}
#endif
	hashingLock.Unlock();

	MoveActiveSharedHashToPendingCompletion(rJob);

	CSharedFileHashResult *pResult = new CSharedFileHashResult;
	pResult->pOwner = this;
	pResult->pKnownFile = pKnownFile;
	pResult->bSuccess = bSuccess;
	pResult->strName = rJob.strName;
	pResult->strDirectory = rJob.strDirectory;
	pResult->strSharedDirectory = rJob.strSharedDirectory;
	pResult->strFilePathKey = rJob.strFilePathKey;
#if EMULE_COMPILED_STARTUP_PROFILING
	pResult->ullQueuedTimestampUs = rJob.ullQueuedTimestampUs;
#endif

	if (QueueDeferredSharedHashResult(pResult))
		return;

	if (!theApp.IsClosing())
		DebugLog(_T("Failed to deliver shared-file hash completion for \"%s\" during shutdown; discarding result."), (LPCTSTR)strFilePath);
	delete pKnownFile;
	delete pResult;
	CompleteSharedHashCompletion(rJob.strFilePathKey);
	if (GetHashingCount() == 0)
		NoteStartupHashingQueueDrained(::GetTickCount64());
}

void CSharedFileList::MoveActiveSharedHashToPendingCompletion(const SharedHashJob &rJob)
{
	CSingleLock lock(&m_mutSharedHashQueue, TRUE);
	if (m_bSharedHashActive && m_sharedHashActiveJob.strFilePathKey.CompareNoCase(rJob.strFilePathKey) == 0) {
		m_bSharedHashActive = false;
		m_sharedHashActiveJob = SharedHashJob{};
		m_sharedHashPendingCompletions.push_back(rJob);
	}
}

void CSharedFileList::CompleteSharedHashCompletion(const CString &strFilePathKey)
{
	CSingleLock lock(&m_mutSharedHashQueue, TRUE);
	for (auto it = m_sharedHashPendingCompletions.begin(); it != m_sharedHashPendingCompletions.end(); ++it) {
		if (it->strFilePathKey.CompareNoCase(strFilePathKey) == 0) {
			m_sharedHashPendingCompletions.erase(it);
			break;
		}
	}
	m_sharedHashInFlightKeys.erase(MakeSharedHashInFlightLookupKey(strFilePathKey));
}

bool CSharedFileList::QueueDeferredSharedHashResult(CSharedFileHashResult *pResult)
{
	if (pResult == NULL)
		return false;

	bool bShouldPost = false;
	{
		CSingleLock lock(&m_mutSharedHashQueue, TRUE);
		if (m_bSharedHashWorkerExitRequested)
			return false;
		m_sharedHashDeferredResults.push_back(pResult);
		if (!m_bSharedHashDrainPosted) {
			m_bSharedHashDrainPosted = true;
			bShouldPost = true;
		}
	}

	if (!bShouldPost || PostSharedHashResultsDrain())
		return true;

	CSingleLock lock(&m_mutSharedHashQueue, TRUE);
	for (auto it = m_sharedHashDeferredResults.begin(); it != m_sharedHashDeferredResults.end(); ++it) {
		if (*it == pResult) {
			m_sharedHashDeferredResults.erase(it);
			break;
		}
	}
	if (m_sharedHashDeferredResults.empty())
		m_bSharedHashDrainPosted = false;
	return false;
}

bool CSharedFileList::TakeDeferredSharedHashResult(CSharedFileHashResult *&rpResult)
{
	rpResult = NULL;

	CSingleLock lock(&m_mutSharedHashQueue, TRUE);
	if (m_sharedHashDeferredResults.empty())
		return false;

	rpResult = m_sharedHashDeferredResults.front();
	m_sharedHashDeferredResults.pop_front();
	return true;
}

bool CSharedFileList::PostSharedHashResultsDrain()
{
	for (int iRetry = 0; iRetry < kSharedHashCompletionPostRetries; ++iRetry) {
		HWND hTargetWnd = NULL;
		{
			CSingleLock lock(&m_mutSharedHashQueue, TRUE);
			if (m_bSharedHashWorkerExitRequested || theApp.IsClosing() || theApp.emuledlg == NULL || !::IsWindow(theApp.emuledlg->m_hWnd)) {
				m_bSharedHashDrainPosted = false;
				return false;
			}
			hTargetWnd = theApp.emuledlg->m_hWnd;
		}
		if (::PostMessage(hTargetWnd, TM_SHAREDHASHRESULTSAVAILABLE, 0, 0) != FALSE)
			return true;
		::Sleep(kSharedHashCompletionPostRetryDelayMs);
	}

	CSingleLock lock(&m_mutSharedHashQueue, TRUE);
	m_bSharedHashDrainPosted = false;
	return false;
}

void CSharedFileList::DrainDeferredSharedHashResults()
{
	for (;;) {
		const ULONGLONG ullDrainStart = ::GetTickCount64();
		unsigned int uProcessed = 0;
		CSharedFileHashResult *pResult = NULL;
		while (TakeDeferredSharedHashResult(pResult)) {
			if (pResult == NULL)
				continue;

			const bool bCanConsume = !theApp.IsClosing()
				&& theApp.sharedfiles != NULL
				&& pResult->pOwner == theApp.sharedfiles
				&& !IsSharedHashWorkerShuttingDown();
			if (pResult->bSuccess) {
				if (bCanConsume)
					FileHashingFinished(pResult);
				else
					delete pResult->pKnownFile;
			} else {
				if (bCanConsume)
					HashFailed(pResult);
				delete pResult->pKnownFile;
			}
			delete pResult;
			++uProcessed;
			if (uProcessed >= kSharedHashUiDrainBatchMax || (::GetTickCount64() - ullDrainStart) >= kSharedHashUiDrainBudgetMs)
				break;
		}

		bool bNeedsRepost = false;
		{
			CSingleLock lock(&m_mutSharedHashQueue, TRUE);
			bNeedsRepost = !m_sharedHashDeferredResults.empty() && !m_bSharedHashWorkerExitRequested && !theApp.IsClosing();
			if (!bNeedsRepost)
				m_bSharedHashDrainPosted = false;
		}

		if (!bNeedsRepost)
			break;

		const bool bPostSucceeded = PostSharedHashResultsDrain();
		const SharedFileListSeams::SharedHashDrainContinuationAction eContinuationAction =
			SharedFileListSeams::GetSharedHashDrainContinuationAction({
				true,
				IsSharedHashWorkerShuttingDown(),
				theApp.IsClosing(),
				bPostSucceeded
			});
		if (eContinuationAction == SharedFileListSeams::SharedHashDrainContinuationAction::WaitForPostedDrain)
			break;
		if (eContinuationAction == SharedFileListSeams::SharedHashDrainContinuationAction::Complete)
			break;

		DebugLog(_T("Failed to re-post shared-file hash completion drain; draining the next batch inline."));
	}
}

void CSharedFileList::OnSharedHashQueuePossiblyDrained()
{
	if (GetHashingCount() != 0)
		return;

	NoteStartupHashingQueueDrained(::GetTickCount64());
	if (output != NULL)
		output->FlushStartupDeferredReload();
	if (theApp.emuledlg != NULL && theApp.emuledlg->sharedfileswnd != NULL)
		theApp.emuledlg->sharedfileswnd->OnSharedHashingDrained();
}

void CSharedFileList::InvalidateStartupCachesAfterInterruptedHashing()
{
	{
		CSingleLock stateLock(&m_mutStartupCacheSave, TRUE);
		m_bStartupCacheInvalidatedByInterruptedHashing = true;
		m_bStartupCacheDirty = false;
		m_bStartupCacheSaveRunAfterCurrent = false;
	}
	m_startupCacheRecords.clear();
	m_startupCacheVolumes.clear();
	m_startupCacheVolumeValidation.clear();
	m_duplicateSharedPathRecords.clear();
	const bool bStartupCacheRemoved = LongPathSeams::DeleteFileIfExists(GetStartupCachePath());
	const bool bDuplicatePathCacheRemoved = LongPathSeams::DeleteFileIfExists(GetDuplicatePathCachePath());
	NoteStartupCacheLoadResult(false, false, true, bStartupCacheRemoved, "interrupted_hashing", 0, 0);
	NoteDuplicatePathCacheLoadResult(false, true, bDuplicatePathCacheRemoved, "interrupted_hashing", 0);
}

bool CSharedFileList::IsSharedHashInFlight(const CString &strDirectory, const CString &strName) const
{
	const CString strFilePathKey(MakeSharedHashFilePathKey(strDirectory, strName));
	CSingleLock lock(&m_mutSharedHashQueue, TRUE);
	return m_sharedHashInFlightKeys.find(MakeSharedHashInFlightLookupKey(strFilePathKey)) != m_sharedHashInFlightKeys.end();
}

void CSharedFileList::SignalSharedHashWorkerShutdown()
{
	bool bInvalidateStartupCaches = false;
	std::deque<CSharedFileHashResult*> deferredResults;
	{
		CSingleLock lock(&m_mutSharedHashQueue, TRUE);
		if (m_bSharedHashShutdownSignaled)
			return;
		const SharedFileListSeams::SharedHashShutdownCacheState shutdownState = {
			!m_sharedHashQueue.empty(),
			!m_sharedHashPendingCompletions.empty(),
			m_bSharedHashActive
		};
		bInvalidateStartupCaches = SharedFileListSeams::ShouldInvalidateStartupCacheAfterSharedHashShutdown(shutdownState);
		m_bSharedHashShutdownSignaled = true;
		m_bSharedHashWorkerExitRequested = true;
		m_bSharedHashWorkerCanHash = true;
		m_sharedHashQueue.clear();
		m_sharedHashPendingCompletions.clear();
		m_sharedHashInFlightKeys.clear();
		deferredResults.swap(m_sharedHashDeferredResults);
	}
	for (CSharedFileHashResult *pResult : deferredResults) {
		if (pResult == NULL)
			continue;
		delete pResult->pKnownFile;
		delete pResult;
	}
	if (bInvalidateStartupCaches)
		InvalidateStartupCachesAfterInterruptedHashing();
	SignalSharedHashWorker();
}

bool CSharedFileList::IsSharedHashWorkerShuttingDown() const
{
	CSingleLock lock(&m_mutSharedHashQueue, TRUE);
	return m_bSharedHashShutdownSignaled;
}

bool CSharedFileList::GetActiveSharedHashFile(CString &rstrLeafName, CString &rstrFullPath) const
{
	CSingleLock lock(&m_mutSharedHashQueue, TRUE);
	if (!m_bSharedHashActive)
		return false;

	rstrLeafName = m_sharedHashActiveJob.strName;
	rstrFullPath = BuildSharedHashFilePath(m_sharedHashActiveJob.strDirectory, m_sharedHashActiveJob.strName);
	return true;
}

void CSharedFileList::PurgeInterruptedHashStartupCaches()
{
	InvalidateStartupCachesAfterInterruptedHashing();
}

bool CSharedFileList::ShutdownSharedHashWorkerStep(DWORD dwWaitMilliseconds)
{
	SignalSharedHashWorkerShutdown();
	if (m_pSharedHashThread == NULL)
		return true;

	const DWORD dwWait = ::WaitForSingleObject(m_pSharedHashThread->m_hThread, dwWaitMilliseconds);
	if (dwWait == WAIT_OBJECT_0) {
		delete m_pSharedHashThread;
		m_pSharedHashThread = NULL;
		return true;
	}
	if (dwWait == WAIT_FAILED) {
		DebugLogError(_T("Failed to wait for shared-file hash worker shutdown - Error %lu"), ::GetLastError());
		delete m_pSharedHashThread;
		m_pSharedHashThread = NULL;
		ASSERT(0);
		return true;
	}
	return false;
}

INT_PTR CSharedFileList::GetHashingCount() const
{
	CSingleLock lock(&m_mutSharedHashQueue, TRUE);
	return static_cast<INT_PTR>(m_sharedHashQueue.size() + m_sharedHashPendingCompletions.size() + (m_bSharedHashActive ? 1 : 0));
}

bool CSharedFileList::IsStartupDeferredHashingActive() const
{
	CSingleLock stateLock(&m_mutStartupCacheSave, TRUE);
	return m_bStartupDeferredHashingActive;
}

void CSharedFileList::ResetStartupCacheSchedulingState(const ULONGLONG ullNowTick)
{
	CSingleLock stateLock(&m_mutStartupCacheSave, TRUE);
	m_bStartupDeferredHashingActive = false;
	m_nLastStartupCacheSave = ullNowTick;
	m_nStartupCacheDirtyTick = ullNowTick;
}

void CSharedFileList::SetStartupDeferredHashingActiveFlag(const bool bActive)
{
	CSingleLock stateLock(&m_mutStartupCacheSave, TRUE);
	m_bStartupDeferredHashingActive = bActive;
}

void CSharedFileList::NoteStartupHashingQueueDrained(const ULONGLONG ullNowTick)
{
	CSingleLock stateLock(&m_mutStartupCacheSave, TRUE);
	m_bStartupDeferredHashingActive = false;
	m_nLastStartupCacheSave = ullNowTick;
}

bool CSharedFileList::ShouldStartStartupCacheSaveNow(const ULONGLONG ullNowTick) const
{
	CSingleLock stateLock(&m_mutStartupCacheSave, TRUE);
	const SharedFileListSeams::StartupCacheSaveScheduleState scheduleState = {
		m_bStartupCacheDirty,
		theApp.IsClosing(),
		m_bStartupCacheSaveRunning,
		m_bStartupDeferredHashingActive,
		ullNowTick,
		m_nStartupCacheDirtyTick
	};
	return SharedFileListSeams::ShouldStartStartupCacheSave(scheduleState);
}

void CSharedFileList::FindSharedFiles(const bool bAllowStartupCache)
{
	m_startupScanStats = StartupScanStats();
	m_uStartupHashCompletedFiles = 0;
	m_uStartupHashFailedFiles = 0;
	m_bStartupDuplicateReuseActive = bAllowStartupCache;
	ResetStartupCacheSchedulingState(::GetTickCount64());
	if (!m_Files_map.IsEmpty() && theApp.downloadqueue) {
		CSingleLock listlock(&m_mutWriteList);

		CCKey key;
		for (POSITION pos = m_Files_map.GetStartPosition(); pos != NULL;) {
			CKnownFile *cur_file;
			m_Files_map.GetNextAssoc(pos, key, cur_file);
			if (!cur_file->IsKindOf(RUNTIME_CLASS(CPartFile))
				|| theApp.downloadqueue->IsPartFile(cur_file)
				|| theApp.knownfiles->IsFilePtrInList(cur_file)
				|| LongPathSeams::GetFileAttributes(cur_file->GetFilePath()) == INVALID_FILE_ATTRIBUTES)
			{
				m_UnsharedFiles_map[CSKey(cur_file->GetFileHash())] = true;
				listlock.Lock();
				m_Files_map.RemoveKey(key);
				listlock.Unlock();
			}
		}
		theApp.downloadqueue->AddPartFilesToShare(); // read partfiles
	}

	// khaos::kmod+ Fix: Shared files loaded multiple times.
	CStringList l_sAdded;
	std::unordered_set<std::wstring> addedDirectoryKeys;
	const CString &tempDir(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));

#if defined(_DEVBUILD)
	//Create the test file (before adding the Incoming directory)
	CSafeBufferedFile f;
	if (!LongPathSeams::OpenFile(f, tempDir + m_strBetaFileName, CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite))
		ASSERT(0);
	else {
		try {
			// do not translate the content!
			f.CStdioFile::WriteString(m_strBetaFileName); // guarantees a different hash on different versions
			f.CStdioFile::WriteString(_T("\nThis file is automatically created by eMule development versions to help the developers testing and debugging the new features.")
				_T("\neMule will delete this file when exiting, otherwise you can remove this file at any time.")
				_T("\nThanks for helping test eMule :)"));
			f.Close();
		} catch (CFileException *ex) {
			ASSERT(0);
			ex->Delete();
		}
	}
#endif
	AddDirectory(tempDir, l_sAdded, addedDirectoryKeys, bAllowStartupCache);

	for (INT_PTR i = 1; i < thePrefs.GetCatCount(); ++i)
		AddDirectory(thePrefs.GetCatPath(i), l_sAdded, addedDirectoryKeys, bAllowStartupCache);

	CStringList sharedDirs;
	thePrefs.CopySharedDirectoryList(sharedDirs);
	for (POSITION pos = sharedDirs.GetHeadPosition(); pos != NULL;)
		AddDirectory(sharedDirs.GetNext(pos), l_sAdded, addedDirectoryKeys, bAllowStartupCache);

	// add all single shared files
	for (POSITION pos = m_liSingleSharedFiles.GetHeadPosition(); pos != NULL;)
		CheckAndAddSingleFile(m_liSingleSharedFiles.GetNext(pos));

	// khaos::kmod-
	if (GetHashingCount() == 0)
		AddLogLine(false, GetResString(IDS_SHAREDFOUND), (unsigned)m_Files_map.GetCount());
	else
		AddLogLine(false, GetResString(IDS_SHAREDFOUNDHASHING), (unsigned)m_Files_map.GetCount(), (unsigned)GetHashingCount());

	MarkStartupCacheDirty();
	HashNextFile();
#if EMULE_COMPILED_STARTUP_PROFILING
	if (theApp.IsStartupProfilingEnabled()) {
		theApp.AppendStartupProfileCounter(_T("shared.scan.requested_directories"), m_startupScanStats.uRequestedDirectories, _T("directories"));
		theApp.AppendStartupProfileCounter(_T("shared.scan.deduped_directories"), m_startupScanStats.uDedupedDirectories, _T("directories"));
		theApp.AppendStartupProfileCounter(_T("shared.scan.duplicate_directories"), m_startupScanStats.uDuplicateDirectories, _T("directories"));
		theApp.AppendStartupProfileCounter(_T("shared.scan.directories_from_cache"), m_startupScanStats.uDirectoriesFromCache, _T("directories"));
		theApp.AppendStartupProfileCounter(_T("shared.scan.directories_rescanned"), m_startupScanStats.uDirectoriesRescanned, _T("directories"));
		theApp.AppendStartupProfileCounter(_T("shared.scan.inaccessible_directories"), m_startupScanStats.uInaccessibleDirectories, _T("directories"));
		theApp.AppendStartupProfileCounter(_T("shared.scan.directories_over_248_chars"), m_startupScanStats.uDirectoriesOver248Chars, _T("directories"));
		theApp.AppendStartupProfileCounter(_T("shared.scan.paths_over_260_chars"), m_startupScanStats.uPathsOver260Chars, _T("paths"));
		theApp.AppendStartupProfileCounter(_T("shared.scan.known_files_accepted"), m_startupScanStats.uKnownFilesAccepted, _T("files"));
		theApp.AppendStartupProfileCounter(_T("shared.scan.duplicate_paths_reused"), m_startupScanStats.uDuplicatePathsReused, _T("files"));
		theApp.AppendStartupProfileCounter(_T("shared.scan.files_queued_for_hash"), m_startupScanStats.uFilesQueuedForHash, _T("files"));
		theApp.AppendStartupProfileCounter(_T("shared.scan.files_ignored"), m_startupScanStats.uFilesIgnored, _T("files"));
		theApp.AppendStartupProfileCounter(_T("shared.scan.shared_files_after_scan"), static_cast<ULONGLONG>(m_Files_map.GetCount()), _T("files"));
		theApp.AppendStartupProfileCounter(_T("shared.scan.pending_hashes"), static_cast<ULONGLONG>(GetHashingCount()), _T("files"));
		theApp.AppendStartupProfileLine(_T("shared.scan.complete"), 0);
	}
#endif
	m_bStartupDuplicateReuseActive = false;
}

void CSharedFileList::AddFilesFromDirectory(const CString &rstrDirectory)
{
	const CString strDirectory(NormalizeSharedDirectoryPath(rstrDirectory));
	ASSERT(strDirectory.Right(1) == _T("\\"));

	DWORD dwError = ERROR_SUCCESS;
	if (!PathHelpers::ForEachDirectoryEntry(strDirectory, [&](const WIN32_FIND_DATA &findData) -> bool {
		CheckAndAddSingleFileFromNormalizedDirectory(strDirectory, findData);
		return true;
	}, &dwError) && dwError != ERROR_FILE_NOT_FOUND)
	{
		++m_startupScanStats.uInaccessibleDirectories;
		LogWarning(GetResString(IDS_ERR_SHARED_DIR), (LPCTSTR)strDirectory, (LPCTSTR)GetErrorMessage(dwError));
	}
}

bool CSharedFileList::AddSingleSharedFile(const CString &rstrFilePath, bool bNoUpdate)
{
	const CString strFilePath(NormalizeSharedFilePath(rstrFilePath));
	if (ShouldIgnoreSharedFileCandidate(strFilePath, SharedFileIntakePolicy::GetLeafName(strFilePath)))
		return false;

	bool bExclude = false;
	// first check if we are explicitly excluding this file
	const CString strFilePathKey(MakeSharedFileLookupKey(strFilePath));
	for (POSITION pos = m_liSingleExcludedFiles.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		const CString strExcluded(m_liSingleExcludedFiles.GetNext(pos));
		if (strFilePathKey == MakeSharedFileLookupKey(strExcluded)) {
			bExclude = true;
			if (strExcluded.CompareNoCase(strFilePath) != 0) {
				m_liSingleExcludedFiles.SetAt(pos2, strFilePath);
				DEBUG_ONLY(DebugLog(_T("Upgraded excluded shared-file spelling: \"%s\" -> \"%s\""), (LPCTSTR)strExcluded, (LPCTSTR)strFilePath));
			}
			m_liSingleExcludedFiles.RemoveAt(pos2);
			RemoveSingleExcludedFileRuleKey(strExcluded);
			break;
		}
	}

	// check if we share this file in general
	const CString strDirectory(PathHelpers::EnsureTrailingSeparator(PathHelpers::GetDirectoryPath(strFilePath)));
	bool bShared = ShouldBeShared(strDirectory, strFilePath, false);

	if (bShared && !bExclude)
		return false; // we should be sharing this file already
	if (!bShared) {
		m_liSingleSharedFiles.AddTail(strFilePath); // the directory is not shared, so we need a new entry
		AddSingleSharedFileRuleKey(strFilePath);
	}

	return bNoUpdate || CheckAndAddSingleFile(strFilePath);
}

bool CSharedFileList::CheckAndAddSingleFile(const CString &rstrFilePath)
{
	const CString strFilePath(NormalizeSharedFilePath(rstrFilePath));
	WIN32_FIND_DATA findData = {};
	DWORD dwError = ERROR_SUCCESS;
	if (!PathHelpers::TryGetPathEntryData(strFilePath, findData, &dwError)) {
		if (dwError != ERROR_FILE_NOT_FOUND)
			LogWarning(GetResString(IDS_ERR_SHARED_DIR), (LPCTSTR)strFilePath, (LPCTSTR)GetErrorMessage(dwError));
		return false;
	}

	const int iSlash = strFilePath.ReverseFind(_T('\\'));
	CheckAndAddSingleFile(iSlash >= 0 ? strFilePath.Left(iSlash + 1) : CString(), findData);
	HashNextFile();
	bHaveSingleSharedFiles = true;
	// GUI update to be done by the caller
	return true;
}

void CSharedFileList::CheckAndAddSingleFile(const CString &strDirectory, const WIN32_FIND_DATA &findData)
{
	CString strFoundDirectory(strDirectory);
	if (!strFoundDirectory.IsEmpty() && strFoundDirectory.Right(1) != _T("\\"))
		strFoundDirectory += _T("\\");
	strFoundDirectory = NormalizeSharedDirectoryPath(strFoundDirectory);
	CheckAndAddSingleFileFromNormalizedDirectory(strFoundDirectory, findData);
}

void CSharedFileList::CheckAndAddSingleFileFromNormalizedDirectory(const CString &strFoundDirectory, const WIN32_FIND_DATA &findData)
{
	if ((findData.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_TEMPORARY)) != 0) {
		++m_startupScanStats.uFilesIgnored;
		return;
	}

	const ULONGLONG ullFoundFileSize = (static_cast<ULONGLONG>(findData.nFileSizeHigh) << 32) | findData.nFileSizeLow;
	if (ullFoundFileSize == 0 || ullFoundFileSize > MAX_EMULE_FILE_SIZE) {
		++m_startupScanStats.uFilesIgnored;
		return;
	}

	ASSERT(strFoundDirectory.IsEmpty() || strFoundDirectory.Right(1) == _T("\\"));

	const CString strFoundFileName(findData.cFileName);
	const CString strFoundFilePath(NormalizeSharedFilePath(strFoundDirectory + strFoundFileName));
	if (strFoundFilePath.GetLength() > 260)
		++m_startupScanStats.uPathsOver260Chars;

	if (HasSingleExcludedFileRule(strFoundFilePath)) {
		++m_startupScanStats.uFilesIgnored;
		return;
	}

	if (ShouldIgnoreSharedFileCandidate(strFoundFilePath, strFoundFileName)) {
		TRACE(_T("%hs: Did not share file \"%s\" - not supported file type\n"), __FUNCTION__, (LPCTSTR)strFoundFilePath);
		++m_startupScanStats.uFilesIgnored;
		return;
	}

	time_t fdate = (time_t)FileTimeToUnixTime(findData.ftLastWriteTime);
	if (fdate <= 0)
		fdate = (time_t)-1;
	if (fdate == (time_t)-1) {
		if (thePrefs.GetVerbose())
			AddDebugLogLine(false, _T("Failed to get file date of \"%s\""), (LPCTSTR)strFoundFilePath);
	}

	CKnownFile *toadd = theApp.knownfiles->FindKnownFile(strFoundFileName, fdate, ullFoundFileSize);
	if (toadd) {
		if (AddKnownSharedFile(toadd, strFoundDirectory, strFoundFilePath))
			++m_startupScanStats.uKnownFilesAccepted;
		else
			++m_startupScanStats.uFilesIgnored;
	} else if (TryReuseRememberedDuplicateSharedPath(strFoundFilePath, static_cast<LONGLONG>(fdate), ullFoundFileSize)) {
		++m_startupScanStats.uDuplicatePathsReused;
		++m_startupScanStats.uFilesIgnored;
	} else {
		if (!IsSharedHashInFlight(strFoundDirectory, strFoundFileName) && !thePrefs.IsTempFile(strFoundDirectory, strFoundFileName)) {
			QueueSharedFileForHash(strFoundDirectory, strFoundFileName, CString());
			++m_startupScanStats.uFilesQueuedForHash;
		} else {
			TRACE(_T("%hs: Did not share file \"%s\" - already hashing or temp. file\n"), __FUNCTION__, (LPCTSTR)strFoundFilePath);
			++m_startupScanStats.uFilesIgnored;
		}
	}
}

bool CSharedFileList::AddKnownSharedFile(CKnownFile *pFile, const CString &strFoundDirectory, const CString &strFoundFilePath)
{
	if (pFile == NULL)
		return false;

	CCKey key(pFile->GetFileHash());
	CKnownFile *pFileInMap = NULL;
	if (m_Files_map.Lookup(key, pFileInMap)) {
		TRACE(_T("%hs: File already in shared file list: %s \"%s\"\n"), __FUNCTION__, (LPCTSTR)md4str(pFileInMap->GetFileHash()), (LPCTSTR)pFileInMap->GetFilePath());
		TRACE(_T("%hs: File to add:                      %s \"%s\"\n"), __FUNCTION__, (LPCTSTR)md4str(pFile->GetFileHash()), (LPCTSTR)strFoundFilePath);
		if (!pFileInMap->IsKindOf(RUNTIME_CLASS(CPartFile)) || theApp.downloadqueue->IsPartFile(pFileInMap)) {
			if (MakeSharedFileLookupKey(strFoundFilePath) != MakeSharedFileLookupKey(pFileInMap->GetFilePath())) {
				ReportDuplicateSharedFileWarning(pFileInMap->GetFilePath(), strFoundFilePath);
				LONGLONG utcCurrentFileDate = -1;
				ULONGLONG ullCurrentFileSize = 0;
				if (GetFileStartupState(strFoundFilePath, utcCurrentFileDate, ullCurrentFileSize))
					RememberDuplicateSharedPath(strFoundFilePath, pFileInMap->GetFileHash(), utcCurrentFileDate, ullCurrentFileSize);
			} else {
				if (pFileInMap->GetFilePath().CompareNoCase(strFoundFilePath) != 0) {
					const CString strOldFilePath(pFileInMap->GetFilePath());
					pFileInMap->SetPath(strFoundDirectory);
					pFileInMap->SetFilePath(strFoundFilePath);
					DEBUG_ONLY(DebugLog(_T("Upgraded shared-file spelling: \"%s\" -> \"%s\""), (LPCTSTR)strOldFilePath, (LPCTSTR)strFoundFilePath));
				}
				DebugLog(_T("File shared twice, might have been a single shared file before - %s"), (LPCTSTR)pFileInMap->GetFilePath());
			}
		}
		return false;
	}

	pFile->SetPath(strFoundDirectory);
	pFile->SetFilePath(strFoundFilePath);
	return AddFile(pFile);
}

bool CSharedFileList::SafeAddKFile(CKnownFile *toadd, bool bOnlyAdd)
{
	RemoveFromHashing(toadd);	// SLUGFILLER: SafeHash - hashed OK, remove from list if it was in
	bool bAdded = AddFile(toadd);
	if (!bOnlyAdd) {
		if (bAdded && output) {
			if (IsStartupDeferredHashingActive() || HasSharedHashingWork())
				output->ScheduleStartupDeferredReload();
			else
				output->AddFile(toadd);
			output->ShowFilesCount();
		}
		m_lastPublishED2KFlag = true;
	}
	return bAdded;
}

void CSharedFileList::RepublishFile(CKnownFile *pFile)
{
	CServer *pCurServer = server->GetCurrentServer();
	if (pCurServer && (pCurServer->GetTCPFlags() & SRV_TCPFLG_COMPRESSION)) {
		m_lastPublishED2KFlag = true;
		pFile->SetED2KRepublishPending(true);
	}
}

bool CSharedFileList::AddFile(CKnownFile *pFile)
{
	ASSERT(pFile->GetFileIdentifier().HasExpectedMD4HashCount());
	ASSERT(!pFile->IsKindOf(RUNTIME_CLASS(CPartFile)) || !static_cast<CPartFile*>(pFile)->m_bMD4HashsetNeeded);
	ASSERT(!pFile->IsShellLinked() || ShouldBeShared(pFile->GetSharedDirectory(), NULL, false));
	CCKey key(pFile->GetFileHash());
	CKnownFile *pFileInMap;
	if (m_Files_map.Lookup(key, pFileInMap)) {
		TRACE(_T("%hs: File already in shared file list: %s \"%s\" \"%s\"\n"), __FUNCTION__, (LPCTSTR)md4str(pFileInMap->GetFileHash()), (LPCTSTR)pFileInMap->GetFileName(), (LPCTSTR)pFileInMap->GetFilePath());
		TRACE(_T("%hs: File to add:                      %s \"%s\" \"%s\"\n"), __FUNCTION__, (LPCTSTR)md4str(pFile->GetFileHash()), (LPCTSTR)pFile->GetFileName(), (LPCTSTR)pFile->GetFilePath());
		const CString strExistingFileKey(MakeSharedFileLookupKey(pFileInMap->GetFilePath()));
		const CString strIncomingFileKey(MakeSharedFileLookupKey(pFile->GetFilePath()));
		if (strExistingFileKey == strIncomingFileKey
			&& pFileInMap->GetFilePath().CompareNoCase(pFile->GetFilePath()) != 0)
		{
			const CString strOldFilePath(pFileInMap->GetFilePath());
			pFileInMap->SetPath(pFile->GetPath());
			pFileInMap->SetFilePath(pFile->GetFilePath());
			DEBUG_ONLY(DebugLog(_T("Upgraded duplicate shared-file spelling: \"%s\" -> \"%s\""), (LPCTSTR)strOldFilePath, (LPCTSTR)pFileInMap->GetFilePath()));
		}
		if ((!pFileInMap->IsKindOf(RUNTIME_CLASS(CPartFile)) || theApp.downloadqueue->IsPartFile(pFileInMap))
			&& strExistingFileKey != strIncomingFileKey)
		{
			ReportDuplicateSharedFileWarning(pFileInMap->GetFilePath(), pFile->GetFilePath());
			RememberDuplicateSharedPath(pFile->GetFilePath(), pFileInMap->GetFileHash(), static_cast<LONGLONG>(pFile->GetUtcFileDate()), static_cast<ULONGLONG>(pFile->GetFileSize()));
		}
		return false;
	}
	m_UnsharedFiles_map.RemoveKey(CSKey(pFile->GetFileHash()));

	CSingleLock listlock(&m_mutWriteList, TRUE);
	m_Files_map[key] = pFile;
	listlock.Unlock();

	bool bKeywordsNeedUpdated = true;

	if (!pFile->IsPartFile() && !pFile->m_pCollection && CCollection::HasCollectionExtention(pFile->GetFileName())) {
		pFile->m_pCollection = new CCollection();
		if (!pFile->m_pCollection->InitCollectionFromFile(pFile->GetFilePath(), pFile->GetFileName())) {
			delete pFile->m_pCollection;
			pFile->m_pCollection = NULL;
		} else if (!pFile->m_pCollection->GetCollectionAuthorKeyString().IsEmpty()) {
			//If the collection has a key, resetting the file name will cause
			//the key to be added into the word list to be stored in Kad.
			pFile->SetFileName(pFile->GetFileName());
			//During the initial startup, shared files are not accessible
			//to SetFileName which will then not call AddKeywords.
			//But when it is accessible, we don't allow it to re-add them.
			if (theApp.sharedfiles)
				bKeywordsNeedUpdated = false;
		}
	}

	if (bKeywordsNeedUpdated)
		m_keywords->AddKeywords(pFile);

	pFile->SetLastSeen();

	theApp.knownfiles->m_nRequestedTotal += pFile->statistic.GetAllTimeRequests();
	theApp.knownfiles->m_nAcceptedTotal += pFile->statistic.GetAllTimeAccepts();
	theApp.knownfiles->m_nTransferredTotal += pFile->statistic.GetAllTimeTransferred();
	MarkStartupCacheDirty();

	return true;
}

void CSharedFileList::FileHashingFinished(CKnownFile *file)
{
	// File hashing finished for a shared file (none partfile)
	//	- reading shared directories at startup and hashing files which were not found in known.met
	//	- reading shared directories during runtime (user hit Reload button, added a shared directory, ...)

	ASSERT(!IsFilePtrInList(file));
	ASSERT(!theApp.knownfiles->IsFilePtrInList(file));

	CKnownFile *found_file = GetFileByID(file->GetFileHash());
	if (found_file == NULL) {
		// check if we still want to actually share this file, the user might have unshared it while hashing
		if (!ShouldBeShared(file->GetSharedDirectory(), file->GetFilePath(), false)) {
			RemoveFromHashing(file);
			if (!IsFilePtrInList(file) && !theApp.knownfiles->IsFilePtrInList(file))
				delete file;
			else
				ASSERT(0);
		} else {
			const CString strDuplicatePath(file->GetFilePath());
			const LONGLONG llDuplicateDate = static_cast<LONGLONG>(file->GetUtcFileDate());
			const ULONGLONG ullDuplicateSize = static_cast<ULONGLONG>(file->GetFileSize());
			uchar aucDuplicateHash[16];
			md4cpy(aucDuplicateHash, file->GetFileHash());
			const bool bSharedAdded = SafeAddKFile(file);
			if (!theApp.knownfiles->SafeAddKFile(file)) {
				TRACE(_T("%hs: Known file list kept an existing same-MD4 entry: %s \"%s\"\n"), __FUNCTION__, (LPCTSTR)md4str(aucDuplicateHash), (LPCTSTR)strDuplicatePath);
				RememberDuplicateSharedPath(strDuplicatePath, aucDuplicateHash, llDuplicateDate, ullDuplicateSize);
				if (bSharedAdded && IsFilePtrInList(file))
					RemoveFile(file);
				if (bSharedAdded
					&& !IsFilePtrInList(file)
					&& !theApp.knownfiles->IsFilePtrInList(file)
					&& (theApp.downloadqueue == NULL || !theApp.downloadqueue->IsPartFile(file)))
				{
					delete file;
				}
			}
			MarkStartupCacheDirty();
		}
	} else {
		TRACE(_T("%hs: File already in shared file list: %s \"%s\"\n"), __FUNCTION__, (LPCTSTR)md4str(found_file->GetFileHash()), (LPCTSTR)found_file->GetFilePath());
		TRACE(_T("%hs: File to add:                      %s \"%s\"\n"), __FUNCTION__, (LPCTSTR)md4str(file->GetFileHash()), (LPCTSTR)file->GetFilePath());
		ReportDuplicateSharedFileWarning(found_file->GetFilePath(), file->GetFilePath());
		RememberDuplicateSharedPath(
			file->GetFilePath(),
			found_file->GetFileHash(),
			static_cast<LONGLONG>(file->GetUtcFileDate()),
			static_cast<ULONGLONG>(file->GetFileSize()));

		RemoveFromHashing(file);
		if (!IsFilePtrInList(file) && !theApp.knownfiles->IsFilePtrInList(file))
			delete file;
		else
			ASSERT(0);
	}

	++m_uStartupHashCompletedFiles;
#if EMULE_COMPILED_STARTUP_PROFILING
	const INT_PTR iHashingCount = GetHashingCount();
	if (theApp.IsStartupProfilingEnabled()
		&& ShouldEmitStartupHashProfileSample(m_uStartupHashCompletedFiles, m_uStartupHashFailedFiles, iHashingCount))
	{
		theApp.AppendStartupProfileCounter(_T("shared.hash.completed_files"), m_uStartupHashCompletedFiles, _T("files"));
		theApp.AppendStartupProfileCounter(_T("shared.hash.failed_files"), m_uStartupHashFailedFiles, _T("files"));
		theApp.AppendStartupProfileCounter(_T("shared.hash.waiting_queue_depth"), static_cast<ULONGLONG>(iHashingCount), _T("files"));
		theApp.AppendStartupProfileCounter(_T("shared.hash.currently_hashing"), 0, _T("files"));
	}
#endif
	OnSharedHashQueuePossiblyDrained();
	NotifyStartupSharedFilesModelChanged();
}

void CSharedFileList::FileHashingFinished(CSharedFileHashResult *pResult)
{
	if (pResult == NULL)
		return;

	CompleteSharedHashCompletion(pResult->strFilePathKey);
	if (pResult->pKnownFile != NULL) {
		FileHashingFinished(pResult->pKnownFile);
		pResult->pKnownFile = NULL;
	} else {
		OnSharedHashQueuePossiblyDrained();
		NotifyStartupSharedFilesModelChanged();
	}
	SignalSharedHashWorker();
}

bool CSharedFileList::RemoveFile(CKnownFile *pFile, bool bDeleted)
{
	CSingleLock listlock(&m_mutWriteList, TRUE);
	bool bResult = (m_Files_map.RemoveKey(CCKey(pFile->GetFileHash())) != FALSE);
	listlock.Unlock();

	output->RemoveFile(pFile, bDeleted);
	m_keywords->RemoveKeywords(pFile);
	if (bResult) {
		m_UnsharedFiles_map[CSKey(pFile->GetFileHash())] = true;
		theApp.knownfiles->m_nRequestedTotal -= pFile->statistic.GetAllTimeRequests();
		theApp.knownfiles->m_nAcceptedTotal -= pFile->statistic.GetAllTimeAccepts();
		theApp.knownfiles->m_nTransferredTotal -= pFile->statistic.GetAllTimeTransferred();
		MarkStartupCacheDirty();
	}
	return bResult;
}

void CSharedFileList::Reload()
{
	ClearVolumeInfoCache();
	m_mapPseudoDirNames.RemoveAll();
	m_keywords->RemoveAllKeywordReferences();
	bHaveSingleSharedFiles = false;
	FindSharedFiles(false);
	m_keywords->PurgeUnreferencedKeywords();
	if (output)
		output->ReloadFileList();
}

void CSharedFileList::SetOutputCtrl(CSharedFilesCtrl *in_ctrl)
{
	output = in_ctrl;
}

void CSharedFileList::StartDeferredHashing()
{
	SetStartupDeferredHashingActiveFlag(GetHashingCount() > 0);
	{
		CSingleLock lock(&m_mutSharedHashQueue, TRUE);
		m_bSharedHashWorkerCanHash = true;
	}
	SignalSharedHashWorker();
	if (GetHashingCount() == 0)
		SetStartupDeferredHashingActiveFlag(false);
}

void CSharedFileList::SendListToServer()
{
	if (m_Files_map.IsEmpty() || !server->IsConnected())
		return;

	CServer *pCurServer = server->GetCurrentServer();
	CSafeMemFile files(1024);
	struct ServerOfferCandidate
	{
		CKnownFile *pFile;
		int iPriority;
		size_t uSequence;
	};
	std::vector<ServerOfferCandidate> offerCandidates;
	offerCandidates.reserve(static_cast<size_t>(m_Files_map.GetCount()));

	size_t uSequence = 0;
	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair)) {
		CKnownFile *cur_file = pair->value;
		if ((!cur_file->GetPublishedED2K() || cur_file->IsED2KRepublishPending()) && (!cur_file->IsLargeFile() || (pCurServer != NULL && pCurServer->SupportsLargeFilesTCP()))) {
			ServerOfferCandidate candidate = { cur_file, GetRealPrio(cur_file->GetUpPriority()), uSequence };
			offerCandidates.push_back(candidate);
		}
		++uSequence;
	}

	std::sort(offerCandidates.begin(), offerCandidates.end(),
		[](const ServerOfferCandidate &left, const ServerOfferCandidate &right) {
			if (left.iPriority != right.iPriority)
				return left.iPriority > right.iPriority;
			return left.uSequence > right.uSequence;
		});

	// add to packet
	uint32 limit = pCurServer ? pCurServer->GetSoftFiles() : 0;
	if (limit == 0 || limit > 200)
		limit = 200;

	if (offerCandidates.size() < limit) {
		limit = static_cast<uint32>(offerCandidates.size());
		if (limit == 0) {
			m_lastPublishED2KFlag = false;
			return;
		}
	}
	files.WriteUInt32(limit);
	for (uint32 i = 0; i < limit; ++i) {
		CKnownFile *file = offerCandidates[static_cast<size_t>(i)].pFile;
		CreateOfferedFilePacket(file, files, pCurServer);
		file->SetED2KRepublishPending(false);
		file->SetPublishedED2K(true);
	}
	Packet *packet = new Packet(files);
	packet->opcode = OP_OFFERFILES;
	// compress packet
	//   - this kind of data is highly compressible (N * (1 MD4 and at least 3 string meta data tags and 1 integer meta data tag))
	//   - the min. amount of data needed for one published file is ~100 bytes
	//   - this function is called once when connecting to a server and when a file becomes shareable - so, it's called rarely.
	//   - if the compressed size is still >= the original size, we send the uncompressed packet
	// therefore we always try to compress the packet
	if (pCurServer && pCurServer->GetTCPFlags() & SRV_TCPFLG_COMPRESSION) {
		UINT uUncomprSize = packet->size;
		packet->PackPacket();
		if (thePrefs.GetDebugServerTCPLevel() > 0)
			Debug(_T(">>> Sending OP_OfferFiles(compressed); uncompr size=%u  compr size=%u  files=%u\n"), uUncomprSize, packet->size, limit);
	} else if (thePrefs.GetDebugServerTCPLevel() > 0)
		Debug(_T(">>> Sending OP_OfferFiles; size=%u  files=%u\n"), packet->size, limit);

	theStats.AddUpDataOverheadServer(packet->size);
	if (thePrefs.GetVerbose())
		AddDebugLogLine(false, _T("Server, Sendlist: Packet size:%u"), packet->size);
	server->SendPacket(packet);
}

void CSharedFileList::ClearED2KPublishInfo()
{
	m_lastPublishED2KFlag = true;
	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair)) {
		pair->value->SetED2KRepublishPending(false);
		pair->value->SetPublishedED2K(false);
	}
}

void CSharedFileList::ClearKadSourcePublishInfo()
{
	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair))
		pair->value->SetLastPublishTimeKadSrc(0, 0);
}

void CSharedFileList::CreateOfferedFilePacket(CKnownFile *cur_file, CSafeMemFile &files
	, const CServer *pServer, const CUpDownClient *pClient)
{
	// NOTE: This function is used for creating the offered file packet for Servers _and_ for Clients.
	files.WriteHash16(cur_file->GetFileHash());

	// *) This function is used for offering files to the local server and for sending
	//    shared files to some other client. In each case we send our IP+Port only, if
	//    we have a HighID.
	// *) Newer eservers also support 2 special IP+port values which are used to hold basic file status info.
	uint32 uTCPflags;
	uint32 nClientID = 0;
	uint16 nClientPort = 0;
	if (pServer) {
		uTCPflags = pServer->GetTCPFlags();
		// we use the 'TCP-compression' server feature flag as indicator for a 'newer' server.
		if (uTCPflags & SRV_TCPFLG_COMPRESSION) {
			if (cur_file->IsPartFile()) {
				// publishing an incomplete file
				nClientID = 0xFCFCFCFC;
				nClientPort = 0xFCFC;
			} else {
				// publishing a complete file
				nClientID = 0xFBFBFBFB;
				nClientPort = 0xFBFB;
			}
		} else {
			// check eD2K ID state
			if (theApp.serverconnect->IsConnected() && !theApp.serverconnect->IsLowID()) {
				nClientID = theApp.GetID();
				nClientPort = thePrefs.GetPort();
			}
		}
	} else {
		uTCPflags = 0;
		if (theApp.IsConnected() && !theApp.IsFirewalled()) {
			nClientID = theApp.GetID();
			nClientPort = thePrefs.GetPort();
		}
	}
	files.WriteUInt32(nClientID);
	files.WriteUInt16(nClientPort);
	//TRACE(_T("Publishing file: Hash=%s  ClientIP=%s  ClientPort=%u\n"), (LPCTSTR)md4str(cur_file->GetFileHash()), (LPCTSTR)ipstr(nClientID), nClientPort);

	CSimpleArray<CTag*> tags;

	tags.Add(new CTag(FT_FILENAME, cur_file->GetFileName()));

	const uint64 uFileSize = (uint64)cur_file->GetFileSize();
	if (!cur_file->IsLargeFile())
		tags.Add(new CTag(FT_FILESIZE, LODWORD(uFileSize)));
	else {
		// we send two 32-bit tags to servers, but a 64-bit tag to other clients.
		if (pServer != NULL) {
			if (!pServer->SupportsLargeFilesTCP()) {
				ASSERT(0);
				tags.Add(new CTag(FT_FILESIZE, 0, false));
			} else {
				tags.Add(new CTag(FT_FILESIZE, LODWORD(uFileSize)));
				tags.Add(new CTag(FT_FILESIZE_HI, HIDWORD(uFileSize)));
			}
		} else if (pClient != NULL) {
			if (!pClient->SupportsLargeFiles()) {
				ASSERT(0);
				tags.Add(new CTag(FT_FILESIZE, 0, false));
			} else
				tags.Add(new CTag(FT_FILESIZE, uFileSize, true));
		}
	}

	// eserver 17.6+ supports eMule file rating tag. There is no TCP-capabilities bit available
	// to determine whether the server is really supporting it -- this is by intention (lug).
	// That's why we always send it.
	if (cur_file->GetFileRating()) {
		uint32 uRatingVal = cur_file->GetFileRating();
		if (pClient) {
			// eserver is sending the rating which it received in a different format (see
			// 'CSearchFile::CSearchFile'). If we are creating the packet for other client
			// we must use eserver's format.
			uRatingVal *= (255 / 5/*RatingExcellent*/);
		}
		tags.Add(new CTag(FT_FILERATING, uRatingVal));
	}

	// NOTE: Archives and CD-Images are published+searched with file type "Pro"
	bool bAddedFileType = false;
	if (uTCPflags & SRV_TCPFLG_TYPETAGINTEGER) {
		// Send integer file type tags to newer servers
		EED2KFileType eFileType = GetED2KFileTypeSearchID(GetED2KFileTypeID(cur_file->GetFileName()));
		if (eFileType >= ED2KFT_AUDIO && eFileType <= ED2KFT_CDIMAGE) {
			tags.Add(new CTag(FT_FILETYPE, (UINT)eFileType));
			bAddedFileType = true;
		}
	}
	if (!bAddedFileType) {
		// Send string file type tags to:
		//	- newer servers, in case there is no integer type available for the file type (e.g. emulecollection)
		//	- older servers
		//	- all clients
		LPCTSTR const pED2KFileType = GetED2KFileTypeSearchTerm(GetED2KFileTypeID(cur_file->GetFileName()));
		if (*pED2KFileType)
			tags.Add(new CTag(FT_FILETYPE, pED2KFileType));
	}

	UINT uEmuleVer = (pClient && pClient->IsEmuleClient()) ? pClient->GetVersion() : 0;
	// eserver 16.4+ does not need the FT_FILEFORMAT tag at all nor does any eMule client. This tag
	// was used for older (very old) eDonkey servers only. -> We send it only to non-eMule clients.
	if (pServer == NULL && uEmuleVer == 0) {
		LPCTSTR const pDot = ::PathFindExtension(cur_file->GetFileName());
		if (pDot[0] && pDot[1]) {
			CString strExt(&pDot[1]); //skip the dot
			tags.Add(new CTag(FT_FILEFORMAT, strExt.MakeLower())); // file extension without a "."
		}
	}

	// only send verified meta data to servers/clients
	if (cur_file->GetMetaDataVer() > 0) {
		static const struct
		{
			bool	bSendToServer;
			uint8	nName;
			uint8	nED2KType;
			LPCSTR	pszED2KName;
		} _aMetaTags[] =
		{
			// Artist, Album and Title are disabled because they should be already part of the filename
			// and would therefore be redundant information sent to the servers. and the servers count the
			// amount of sent data!
			{ false, FT_MEDIA_ARTIST,	TAGTYPE_STRING, FT_ED2K_MEDIA_ARTIST },
			{ false, FT_MEDIA_ALBUM,	TAGTYPE_STRING, FT_ED2K_MEDIA_ALBUM },
			{ false, FT_MEDIA_TITLE,	TAGTYPE_STRING, FT_ED2K_MEDIA_TITLE },
			{ true,  FT_MEDIA_LENGTH,	TAGTYPE_STRING, FT_ED2K_MEDIA_LENGTH },
			{ true,  FT_MEDIA_BITRATE,	TAGTYPE_UINT32, FT_ED2K_MEDIA_BITRATE },
			{ true,  FT_MEDIA_CODEC,	TAGTYPE_STRING, FT_ED2K_MEDIA_CODEC }
		};
		for (unsigned i = 0; i < _countof(_aMetaTags); ++i) {
			if (pServer != NULL && !_aMetaTags[i].bSendToServer)
				continue;
			CTag *pTag = cur_file->GetTag(_aMetaTags[i].nName);
			if (pTag != NULL) {
				// skip string tags with empty string values
				if (pTag->IsStr() && pTag->GetStr().IsEmpty())
					continue;

				// skip integer tags with '0' values
				if (pTag->IsInt() && pTag->GetInt() == 0)
					continue;

				if (_aMetaTags[i].nED2KType == TAGTYPE_STRING && pTag->IsStr()) {
					if (uTCPflags & SRV_TCPFLG_NEWTAGS)
						tags.Add(new CTag(_aMetaTags[i].nName, pTag->GetStr()));
					else
						tags.Add(new CTag(_aMetaTags[i].pszED2KName, pTag->GetStr()));
				} else if (_aMetaTags[i].nED2KType == TAGTYPE_UINT32 && pTag->IsInt()) {
					if (uTCPflags & SRV_TCPFLG_NEWTAGS)
						tags.Add(new CTag(_aMetaTags[i].nName, pTag->GetInt()));
					else
						tags.Add(new CTag(_aMetaTags[i].pszED2KName, pTag->GetInt()));
				} else if (_aMetaTags[i].nName == FT_MEDIA_LENGTH && pTag->IsInt()) {
					ASSERT(_aMetaTags[i].nED2KType == TAGTYPE_STRING);
					// All 'eserver' versions and eMule versions >= 0.42.4 support the media length tag with type 'integer'
					if ((uTCPflags & SRV_TCPFLG_COMPRESSION) || uEmuleVer >= MAKE_CLIENT_VERSION(0, 42, 4)) {
						if (uTCPflags & SRV_TCPFLG_NEWTAGS)
							tags.Add(new CTag(_aMetaTags[i].nName, pTag->GetInt()));
						else
							tags.Add(new CTag(_aMetaTags[i].pszED2KName, pTag->GetInt()));
					} else
						tags.Add(new CTag(_aMetaTags[i].pszED2KName, SecToTimeLength(pTag->GetInt())));
				} else
					ASSERT(0);
			}
		}
	}

	EUTF8str eStrEncode;
	if ((uTCPflags & SRV_TCPFLG_UNICODE) || !pClient || pClient->GetUnicodeSupport())
		eStrEncode = UTF8strRaw;
	else
		eStrEncode = UTF8strNone;

	files.WriteUInt32(tags.GetSize());
	for (int i = 0; i < tags.GetSize(); ++i) {
		const CTag *pTag = tags[i];
		//TRACE(_T("  %s\n"), pTag->GetFullInfo(DbgGetFileMetaTagName));
		if ((uTCPflags & SRV_TCPFLG_NEWTAGS) || (uEmuleVer >= MAKE_CLIENT_VERSION(0, 42, 7)))
			pTag->WriteNewEd2kTag(files, eStrEncode);
		else
			pTag->WriteTagToFile(files, eStrEncode);
		delete pTag;
	}
}

// -khaos--+++> New param:  pbytesLargest, pointer to uint64.
//				Various other changes to accommodate our new statistic...
//				Point of this is to find the largest file currently shared.
uint64 CSharedFileList::GetDatasize(uint64 &pbytesLargest) const
{
	pbytesLargest = 0;
	// <-----khaos-
	uint64 fsize = 0;

	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair)) {
		uint64 cur_size = (uint64)pair->value->GetFileSize();
		fsize += cur_size;
		// -khaos--+++> If this file is bigger than all the others...well duh.
		if (cur_size > pbytesLargest)
			pbytesLargest = cur_size;
		// <-----khaos-
	}
	return fsize;
}

CKnownFile* CSharedFileList::GetFileByID(const uchar *hash) const
{
	if (hash) {
		CKnownFile *found_file;
		if (m_Files_map.Lookup(CCKey(hash), found_file))
			return found_file;
	}
	return NULL;
}

CKnownFile* CSharedFileList::GetFileByIdentifier(const CFileIdentifierBase &rFileIdent, bool bStrict) const
{
	CKnownFile *pResult;
	if (m_Files_map.Lookup(CCKey(rFileIdent.GetMD4Hash()), pResult))
		if (bStrict) {
			if (pResult->GetFileIdentifier().CompareStrict(rFileIdent))
				return pResult;
		} else if (pResult->GetFileIdentifier().CompareRelaxed(rFileIdent))
			return pResult;
	return NULL;
}

CKnownFile* CSharedFileList::GetFileByIndex(INT_PTR index) const // slow
{
	ASSERT(!index || (index > 0 && index < m_Files_map.GetCount()));
	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair))
		if (--index < 0)
			return pair->value;
	return NULL;
}

CKnownFile* CSharedFileList::GetFileNext(POSITION &pos) const
{
	CKnownFile *cur_file = NULL;
	if (m_Files_map.IsEmpty()) //XP was crashing without this
		pos = NULL;
	else if (pos != NULL) {
		CCKey bufKey;
		m_Files_map.GetNextAssoc(pos, bufKey, cur_file);
	}
	return cur_file;
}

CKnownFile* CSharedFileList::GetFileByAICH(const CAICHHash &rHash) const // slow
{
	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair))
		if (pair->value->GetFileIdentifierC().HasAICHHash() && pair->value->GetFileIdentifierC().GetAICHHash() == rHash)
			return pair->value;

	return NULL;
}

bool CSharedFileList::IsFilePtrInList(const CKnownFile *file) const
{
	return file != NULL && GetFileByID(file->GetFileHash()) == file;
}

void CSharedFileList::HashNextFile()
{
	if (theApp.emuledlg == NULL || !::IsWindow(theApp.emuledlg->m_hWnd))	// wait for the dialog to open
		return;
	if (!theApp.IsClosing())
		theApp.emuledlg->sharedfileswnd->sharedfilesctrl.ShowFilesCount();
	SignalSharedHashWorker();
}

// SLUGFILLER: SafeHash
bool CSharedFileList::IsHashing(const CString &rstrDirectory, const CString &rstrName)
{
	return IsSharedHashInFlight(rstrDirectory, rstrName);
}

void CSharedFileList::RemoveFromHashing(const CKnownFile *hashed)
{
	if (hashed != NULL)
		CompleteSharedHashCompletion(MakeSharedHashFilePathKey(*hashed));
}

std::wstring CSharedFileList::MakeSingleFileRuleKey(const CString &strFilePath)
{
	return std::wstring(MakeSharedFileLookupKey(strFilePath));
}

bool CSharedFileList::HasSingleSharedFileRule(const CString &strFilePath) const
{
	return m_singleSharedFileRuleKeys.find(MakeSingleFileRuleKey(strFilePath)) != m_singleSharedFileRuleKeys.end();
}

bool CSharedFileList::HasSingleExcludedFileRule(const CString &strFilePath) const
{
	return m_singleExcludedFileRuleKeys.find(MakeSingleFileRuleKey(strFilePath)) != m_singleExcludedFileRuleKeys.end();
}

void CSharedFileList::AddSingleSharedFileRuleKey(const CString &strFilePath)
{
	const std::wstring strKey(MakeSingleFileRuleKey(strFilePath));
	if (!m_singleSharedFileRuleKeys.insert(strKey).second)
		return;
	m_sortedSingleSharedFileRuleKeys.insert(
		std::lower_bound(m_sortedSingleSharedFileRuleKeys.begin(), m_sortedSingleSharedFileRuleKeys.end(), strKey),
		strKey);
}

void CSharedFileList::RemoveSingleSharedFileRuleKey(const CString &strFilePath)
{
	const std::wstring strKey(MakeSingleFileRuleKey(strFilePath));
	if (m_singleSharedFileRuleKeys.erase(strKey) == 0)
		return;
	const std::vector<std::wstring>::iterator it = std::lower_bound(
		m_sortedSingleSharedFileRuleKeys.begin(),
		m_sortedSingleSharedFileRuleKeys.end(),
		strKey);
	if (it != m_sortedSingleSharedFileRuleKeys.end() && *it == strKey)
		m_sortedSingleSharedFileRuleKeys.erase(it);
}

void CSharedFileList::AddSingleExcludedFileRuleKey(const CString &strFilePath)
{
	m_singleExcludedFileRuleKeys.insert(MakeSingleFileRuleKey(strFilePath));
}

void CSharedFileList::RemoveSingleExcludedFileRuleKey(const CString &strFilePath)
{
	m_singleExcludedFileRuleKeys.erase(MakeSingleFileRuleKey(strFilePath));
}

void CSharedFileList::HashFailed(UnknownFile_Struct *hashed)
{
	if (hashed != NULL)
		CompleteSharedHashCompletion(MakeSharedHashFilePathKey(hashed->strDirectory, hashed->strName));
	delete hashed;
	MarkStartupCacheDirty();
	++m_uStartupHashFailedFiles;
#if EMULE_COMPILED_STARTUP_PROFILING
	const INT_PTR iHashingCount = GetHashingCount();
	if (theApp.IsStartupProfilingEnabled()
		&& ShouldEmitStartupHashProfileSample(m_uStartupHashCompletedFiles, m_uStartupHashFailedFiles, iHashingCount))
	{
		theApp.AppendStartupProfileCounter(_T("shared.hash.completed_files"), m_uStartupHashCompletedFiles, _T("files"));
		theApp.AppendStartupProfileCounter(_T("shared.hash.failed_files"), m_uStartupHashFailedFiles, _T("files"));
		theApp.AppendStartupProfileCounter(_T("shared.hash.waiting_queue_depth"), static_cast<ULONGLONG>(iHashingCount), _T("files"));
		theApp.AppendStartupProfileCounter(_T("shared.hash.currently_hashing"), 0, _T("files"));
	}
#endif
	OnSharedHashQueuePossiblyDrained();
	NotifyStartupSharedFilesModelChanged();
}

void CSharedFileList::HashFailed(CSharedFileHashResult *pResult)
{
	if (pResult == NULL)
		return;

	CompleteSharedHashCompletion(pResult->strFilePathKey);
	MarkStartupCacheDirty();
	++m_uStartupHashFailedFiles;
#if EMULE_COMPILED_STARTUP_PROFILING
	const INT_PTR iHashingCount = GetHashingCount();
	if (theApp.IsStartupProfilingEnabled()
		&& ShouldEmitStartupHashProfileSample(m_uStartupHashCompletedFiles, m_uStartupHashFailedFiles, iHashingCount))
	{
		theApp.AppendStartupProfileCounter(_T("shared.hash.completed_files"), m_uStartupHashCompletedFiles, _T("files"));
		theApp.AppendStartupProfileCounter(_T("shared.hash.failed_files"), m_uStartupHashFailedFiles, _T("files"));
		theApp.AppendStartupProfileCounter(_T("shared.hash.waiting_queue_depth"), static_cast<ULONGLONG>(iHashingCount), _T("files"));
		theApp.AppendStartupProfileCounter(_T("shared.hash.currently_hashing"), 0, _T("files"));
	}
#endif
	OnSharedHashQueuePossiblyDrained();
	NotifyStartupSharedFilesModelChanged();
	SignalSharedHashWorker();
}

void CSharedFileList::UpdateFile(const CKnownFile *toupdate)
{
	output->UpdateFile(toupdate);
}

void CSharedFileList::Process()
{
	DrainDeferredSharedHashResults();
	Publish();
	if (m_lastPublishED2KFlag && ::GetTickCount64() >= m_lastPublishED2K + ED2KREPUBLISHTIME) {
		SendListToServer();
		m_lastPublishED2K = ::GetTickCount64();
	}
	const ULONGLONG ullNowTick = ::GetTickCount64();
	if (ShouldStartStartupCacheSaveNow(ullNowTick))
		(void)RequestStartupCacheSave();
}

void CSharedFileList::Publish()
{
	if (!Kademlia::CKademlia::IsConnected()
		|| (theApp.IsFirewalled()
			&& theApp.clientlist->GetBuddyStatus() != Connected
			//direct callback
			&& (Kademlia::CUDPFirewallTester::IsFirewalledUDP(true) || !Kademlia::CUDPFirewallTester::IsVerified())
		   )
		|| !GetCount()
		|| !Kademlia::CKademlia::GetPublish())
	{
		return;
	}

	//We are connected to Kad. We are either open or have a buddy. And Kad is ready to start publishing.
	time_t tNow = time(NULL);
	if (Kademlia::CKademlia::GetTotalStoreKey() < KADEMLIATOTALSTOREKEY) {
		//We are not at the max simultaneous keyword publishes
		if (tNow >= m_keywords->GetNextPublishTime()) {
			//Enough time has passed since last keyword publish

			//Get the next keyword which has to be (re-)published
			CPublishKeyword *pPubKw = m_keywords->GetNextKeyword();
			if (pPubKw) {
				//We have the next keyword to check if it can be published

				//Debug check to make sure things are going well.
				ASSERT(pPubKw->GetRefCount() > 0);

				if (tNow >= pPubKw->GetNextPublishTime()) {
					//This keyword can be published.
					Kademlia::CSearch *pSearch = Kademlia::CSearchManager::PrepareLookup(Kademlia::CSearch::STOREKEYWORD, false, pPubKw->GetKadID());
					if (pSearch) {
						//pSearch was created. Which means no search was already being done with this HashID.
						//This also means that it was checked to see if network load wasn't a factor.

						//This sets the filename into the search object so we can show it in the GUI.
						pSearch->SetGUIName(pPubKw->GetKeyword());

						//Add all file IDs which relate to the current keyword to be published
						const CSimpleKnownFileArray &aFiles = pPubKw->GetReferences();
						uint32 count = 0;
						for (int f = 0; f < aFiles.GetSize(); ++f) {
							//Debug check to make sure things are working well.
							ASSERT_VALID(aFiles[f]);
							// JOHNTODO - Why is this happening. I think it may have to do with downloading a file
							// that is already in the known file list.
							//ASSERT(IsFilePtrInList(aFiles[f]));

							//Only publish complete files as someone else should have the full file to publish these keywords.
							//As a side effect, this may help reduce people finding incomplete files in the network.
							if (!aFiles[f]->IsPartFile() && IsFilePtrInList(aFiles[f])) {
								//We only publish up to 150 files per keyword, then rotate the list.
								if (++count >= 150) {
									pPubKw->RotateReferences(f);
									break;
								}
								pSearch->AddFileID(Kademlia::CUInt128(aFiles[f]->GetFileHash()));
							}
						}

						if (count) {
							//Start our keyword publish
							pPubKw->SetNextPublishTime(tNow + KADEMLIAREPUBLISHTIMEK);
							pPubKw->IncPublishedCount();
							Kademlia::CSearchManager::StartSearch(pSearch);
						} else
							//There were no valid files to publish with this keyword.
							delete pSearch;
					}
				}
			}
			m_keywords->SetNextPublishTime(tNow + KADEMLIAPUBLISHTIME);
		}
	}

	if (Kademlia::CKademlia::GetTotalStoreSrc() < KADEMLIATOTALSTORESRC) {
		if (tNow >= m_lastPublishKadSrc) {
			if (m_currFileSrc >= GetCount())
				m_currFileSrc = 0;
			CKnownFile *pCurKnownFile = GetFileByIndex(m_currFileSrc);
			if (pCurKnownFile && pCurKnownFile->PublishSrc())
				if (Kademlia::CSearchManager::PrepareLookup(Kademlia::CSearch::STOREFILE, true, Kademlia::CUInt128(pCurKnownFile->GetFileHash())) == NULL)
					pCurKnownFile->SetLastPublishTimeKadSrc(0, 0);

			++m_currFileSrc;

			// even if we did not publish a source, reset the timer so that this list is processed
			// only every KADEMLIAPUBLISHTIME seconds.
			m_lastPublishKadSrc = tNow + KADEMLIAPUBLISHTIME;
		}
	}

	if (Kademlia::CKademlia::GetTotalStoreNotes() < KADEMLIATOTALSTORENOTES) {
		if (tNow >= m_lastPublishKadNotes) {
			if (m_currFileNotes >= GetCount())
				m_currFileNotes = 0;
			CKnownFile *pCurKnownFile = GetFileByIndex(m_currFileNotes);
			if (pCurKnownFile && pCurKnownFile->PublishNotes())
				if (Kademlia::CSearchManager::PrepareLookup(Kademlia::CSearch::STORENOTES, true, Kademlia::CUInt128(pCurKnownFile->GetFileHash())) == NULL)
					pCurKnownFile->SetLastPublishTimeKadNotes(0);

			++m_currFileNotes;

			// even if we did not publish a source, reset the timer so that this list is processed
			// only every KADEMLIAPUBLISHTIME seconds.
			m_lastPublishKadNotes = tNow + KADEMLIAPUBLISHTIME;
		}
	}
}

void CSharedFileList::AddKeywords(CKnownFile *pFile)
{
	m_keywords->AddKeywords(pFile);
}

void CSharedFileList::RemoveKeywords(CKnownFile *pFile)
{
	m_keywords->RemoveKeywords(pFile);
}

void CSharedFileList::DeletePartFileInstances() const
{
	// this is allowed only in shutdown
	ASSERT(theApp.knownfiles && theApp.IsClosing());
	CCKey key;
	for (POSITION pos = m_Files_map.GetStartPosition(); pos != NULL;) {
		CKnownFile *cur_file;
		m_Files_map.GetNextAssoc(pos, key, cur_file);
		if (cur_file->IsKindOf(RUNTIME_CLASS(CPartFile))
			&& !theApp.downloadqueue->IsPartFile(cur_file)
			&& !theApp.knownfiles->IsFilePtrInList(cur_file))
		{
			delete cur_file; // only allowed during shut down
		}
	}
}

bool CSharedFileList::IsUnsharedFile(const uchar *auFileHash) const
{
	return auFileHash && m_UnsharedFiles_map.PLookup(CSKey(auFileHash));
}

void CSharedFileList::RebuildMetaData()
{
	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair))
		if (!pair->value->IsKindOf(RUNTIME_CLASS(CPartFile)))
			pair->value->UpdateMetaDataTags();
}

bool CSharedFileList::ShouldBeShared(const CString &sDirPath, LPCTSTR const pFilePath, bool bMustBeShared) const
{
	// see if a directory/file should be shared based on our preferences
	if (HasSameDirectoryTextFast(sDirPath, thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR)))
		return true;

	for (INT_PTR i = thePrefs.GetCatCount(); --i > 0;) //down to 1
		if (HasSameDirectoryTextFast(sDirPath, thePrefs.GetCatPath(i)))
			return true;

	if (!bMustBeShared && thePrefs.IsSharedDirectoryListed(sDirPath)) {
		// check if this file is explicitly unshared
		if (pFilePath != NULL) {
			if (HasSingleExcludedFileRule(pFilePath))
				return false;
		}
		return true;
	}

	if (bMustBeShared) //check only incoming & categories (cannot be unshared)
		return false;

	if (pFilePath) {
		// check if this file is explicitly unshared
		if (HasSingleExcludedFileRule(pFilePath))
			return false;
		// check if this file is explicitly shared (as single file)
		if (HasSingleSharedFileRule(pFilePath))
			return true;
	}

	return false;
}

bool CSharedFileList::ContainsSingleSharedFiles(const CString &strDirectory) const
{
	if (m_sortedSingleSharedFileRuleKeys.empty())
		return false;

	const std::wstring strDirectoryKey(MakeSharedDirectoryLookupKey(strDirectory));
	const std::vector<std::wstring>::const_iterator it = std::upper_bound(
		m_sortedSingleSharedFileRuleKeys.begin(),
		m_sortedSingleSharedFileRuleKeys.end(),
		strDirectoryKey);
	if (it != m_sortedSingleSharedFileRuleKeys.end())
		return IsSingleFileRuleKeyWithinDirectory(strDirectoryKey, *it);

	return false;
}

bool CSharedFileList::ExcludeFile(const CString &strFilePath)
{
	const CString strCanonicalFilePath(NormalizeSharedFilePath(strFilePath));
	const CString strCanonicalFilePathKey(MakeSharedFileLookupKey(strCanonicalFilePath));
	bool bShared = false;
	// first remove from explicitly shared files
	for (POSITION pos = m_liSingleSharedFiles.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		const CString strExisting(m_liSingleSharedFiles.GetNext(pos));
		if (strCanonicalFilePathKey == MakeSharedFileLookupKey(strExisting)) {
			bShared = true;
			if (strExisting.CompareNoCase(strCanonicalFilePath) != 0) {
				m_liSingleSharedFiles.SetAt(pos2, strCanonicalFilePath);
				DEBUG_ONLY(DebugLog(_T("Upgraded single-shared file spelling: \"%s\" -> \"%s\""), (LPCTSTR)strExisting, (LPCTSTR)strCanonicalFilePath));
			}
			m_liSingleSharedFiles.RemoveAt(pos2);
			RemoveSingleSharedFileRuleKey(strExisting);
			break;
		}
	}

	// if this file was not shared as single file, check if we implicitly share it
	const CString strCanonicalDirectory(PathHelpers::EnsureTrailingSeparator(PathHelpers::GetDirectoryPath(strCanonicalFilePath)));
	if (!bShared && !ShouldBeShared(strCanonicalDirectory, strCanonicalFilePath, false)) {
		// we don't actually share this file, can't be excluded
		return false;
	}
	if (ShouldBeShared(strCanonicalDirectory, strCanonicalFilePath, true)) {
		// we cannot unshare this file (incoming directories)
		ASSERT(0); // checks should have been done earlier
		return false;
	}

	// add to exclude list
	m_liSingleExcludedFiles.AddTail(strCanonicalFilePath);
	AddSingleExcludedFileRuleKey(strCanonicalFilePath);

	// check if the file is in the shared list (doesn't have to; for example, if it is hashing or not loaded yet) and remove
	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair))
		if (strCanonicalFilePathKey == MakeSharedFileLookupKey(pair->value->GetFilePath())) {
			RemoveFile(pair->value);
			break;
		}

	// GUI update to be done by the caller
	return true;
}

void CSharedFileList::MarkStartupCacheDirty()
{
	CSingleLock stateLock(&m_mutStartupCacheSave, TRUE);
	m_bStartupCacheDirty = true;
	m_nStartupCacheDirtyTick = ::GetTickCount64();
	if (m_bStartupCacheSaveRunning)
		m_bStartupCacheSaveRunAfterCurrent = true;
}

bool CSharedFileList::RequestStartupCacheSave(bool /*bImmediate*/)
{
	{
		CSingleLock stateLock(&m_mutStartupCacheSave, TRUE);
		if (!m_bStartupCacheDirty || m_bStartupCacheInvalidatedByInterruptedHashing || m_bStartupCacheSaveShutdownAbandoned)
			return false;
		if (m_bStartupCacheSaveRunning)
			return false;
		m_bStartupCacheSaveRunning = true;
		m_bStartupCacheSaveRunAfterCurrent = false;
		m_eStartupCacheSavePhase = StartupCacheSavePhase::BuildingRecords;
		m_uStartupCacheSaveDirectoriesDone = 0;
		m_uStartupCacheSaveDirectoriesTotal = 0;
	}

	StartupCacheSaveSnapshot snapshot = {};
	if (!CaptureStartupCacheSaveSnapshot(snapshot)) {
		CSingleLock stateLock(&m_mutStartupCacheSave, TRUE);
		m_bStartupCacheSaveRunning = false;
		m_eStartupCacheSavePhase = StartupCacheSavePhase::Idle;
		m_nStartupCacheDirtyTick = ::GetTickCount64();
		return false;
	}

	try {
		std::shared_ptr<StartupCacheSaveOperation> pOperation(new StartupCacheSaveOperation{});
		pOperation->strCachePath = snapshot.strCachePath;
		pOperation->strDuplicatePathCachePath = snapshot.strDuplicatePathCachePath;
		UpdateStartupCacheSaveOperationProgress(pOperation, StartupCacheSavePhase::BuildingRecords, 0, static_cast<ULONGLONG>(snapshot.directories.size()));
		{
			CSingleLock stateLock(&m_mutStartupCacheSave, TRUE);
			m_pStartupCacheSaveOperation = pOperation;
			m_eStartupCacheSavePhase = StartupCacheSavePhase::BuildingRecords;
			m_uStartupCacheSaveDirectoriesDone = 0;
			m_uStartupCacheSaveDirectoriesTotal = static_cast<ULONGLONG>(snapshot.directories.size());
		}

		std::unique_ptr<StartupCacheSaveThreadRequest> pRequest(new StartupCacheSaveThreadRequest{});
		pRequest->hNotifyWnd = (theApp.emuledlg != NULL) ? theApp.emuledlg->GetSafeHwnd() : NULL;
		pRequest->pOperation = pOperation;
		pRequest->snapshot = std::move(snapshot);

		CWinThread *pThread = AfxBeginThread(&CSharedFileList::StartupCacheSaveThreadProc, pRequest.get(), THREAD_PRIORITY_BELOW_NORMAL, 0, 0, NULL);
		if (pThread != NULL) {
			(void)pRequest.release();
			return true;
		}
	} catch (CMemoryException *ex) {
		ex->Delete();
		DebugLogError(_T("Failed to start startup cache save worker after a memory exception"));
	} catch (CException *ex) {
		ex->Delete();
		DebugLogError(_T("Failed to start startup cache save worker after an MFC exception"));
	} catch (...) {
		DebugLogError(_T("Failed to start startup cache save worker after an unknown exception"));
	}

	{
		CSingleLock stateLock(&m_mutStartupCacheSave, TRUE);
		m_bStartupCacheSaveRunning = false;
		m_eStartupCacheSavePhase = StartupCacheSavePhase::Idle;
		m_pStartupCacheSaveOperation.reset();
		m_nStartupCacheDirtyTick = ::GetTickCount64();
	}
	return false;
}

bool CSharedFileList::AbandonStartupCacheSaveForShutdown()
{
	CSingleLock stateLock(&m_mutStartupCacheSave, TRUE);
	m_bStartupCacheSaveShutdownAbandoned = true;
	m_bStartupCacheDirty = false;
	m_bStartupCacheSaveRunning = false;
	m_bStartupCacheSaveRunAfterCurrent = false;
	UpdateStartupCacheSaveOperationProgress(m_pStartupCacheSaveOperation, StartupCacheSavePhase::Idle, 0, 0);
	m_pStartupCacheSaveOperation.reset();
	m_eStartupCacheSavePhase = StartupCacheSavePhase::Idle;
	m_uStartupCacheSaveDirectoriesDone = 0;
	m_uStartupCacheSaveDirectoriesTotal = 0;
	return false;
}

bool CSharedFileList::HasPendingStartupCacheSaveWork() const
{
	CSingleLock stateLock(&m_mutStartupCacheSave, TRUE);
	return m_bStartupCacheDirty || m_bStartupCacheSaveRunning;
}

bool CSharedFileList::IsStartupCacheSaveRunning() const
{
	CSingleLock stateLock(&m_mutStartupCacheSave, TRUE);
	return m_bStartupCacheSaveRunning;
}

void CSharedFileList::GetStartupCacheSaveProgress(StartupCacheSaveProgress &rProgress) const
{
	CSingleLock stateLock(&m_mutStartupCacheSave, TRUE);
	rProgress.ePhase = m_eStartupCacheSavePhase;
	rProgress.uCompletedDirectories = m_uStartupCacheSaveDirectoriesDone;
	rProgress.uTotalDirectories = m_uStartupCacheSaveDirectoriesTotal;
	rProgress.bRunning = m_bStartupCacheSaveRunning;
	rProgress.bDirty = m_bStartupCacheDirty;
	rProgress.bWaitingForFollowUp = m_bStartupCacheSaveRunAfterCurrent;
	const std::shared_ptr<StartupCacheSaveOperation> pOperation = m_pStartupCacheSaveOperation;
	stateLock.Unlock();
	if (pOperation) {
		CSingleLock progressLock(&pOperation->mutProgress, TRUE);
		rProgress.ePhase = pOperation->ePhase;
		rProgress.uCompletedDirectories = pOperation->uCompletedDirectories;
		rProgress.uTotalDirectories = pOperation->uTotalDirectories;
	}
}

void CSharedFileList::GetStartupCacheStatus(StartupCacheStatus &rStatus) const
{
	rStatus = StartupCacheStatus{};
	rStatus.iHashingCount = GetHashingCount();
	{
		CSingleLock stateLock(&m_mutStartupCacheSave, TRUE);
		rStatus.bCacheFilePresent = m_bStartupCacheFilePresent;
		rStatus.bCacheLoaded = m_bStartupCacheLoaded;
		rStatus.bCacheRejected = m_bStartupCacheRejected;
		rStatus.bCacheRemoved = m_bStartupCacheRemoved;
		rStatus.strCacheRejectCode = m_strStartupCacheRejectCode;
		rStatus.bDuplicatePathCacheLoaded = m_bDuplicatePathCacheLoaded;
		rStatus.bDuplicatePathCacheRejected = m_bDuplicatePathCacheRejected;
		rStatus.bDuplicatePathCacheRemoved = m_bDuplicatePathCacheRemoved;
		rStatus.strDuplicatePathCacheRejectCode = m_strDuplicatePathCacheRejectCode;
		rStatus.uRecordsLoaded = m_uStartupCacheRecordsLoaded;
		rStatus.uVolumesLoaded = m_uStartupCacheVolumesLoaded;
		rStatus.uDuplicatePathRecordsLoaded = m_uDuplicatePathCacheRecordsLoaded;
		rStatus.scanStats = m_startupScanStats;
		rStatus.saveProgress.ePhase = m_eStartupCacheSavePhase;
		rStatus.saveProgress.uCompletedDirectories = m_uStartupCacheSaveDirectoriesDone;
		rStatus.saveProgress.uTotalDirectories = m_uStartupCacheSaveDirectoriesTotal;
		rStatus.saveProgress.bRunning = m_bStartupCacheSaveRunning;
		rStatus.saveProgress.bDirty = m_bStartupCacheDirty;
		rStatus.saveProgress.bWaitingForFollowUp = m_bStartupCacheSaveRunAfterCurrent;
		rStatus.bDeferredHashingActive = m_bStartupDeferredHashingActive;
		rStatus.bInterruptedHashingInvalidatedCache = m_bStartupCacheInvalidatedByInterruptedHashing;
	}
	rStatus.bReady = rStatus.iHashingCount <= 0 && !rStatus.bDeferredHashingActive;
}

CString CSharedFileList::GetStartupCachePath()
{
	return thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + SharedStartupCachePolicy::GetFileName();
}

CString CSharedFileList::GetDuplicatePathCachePath()
{
	return thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + SharedDuplicatePathCachePolicy::GetFileName();
}

std::wstring CSharedFileList::MakeStartupCacheKey(const CString &strDirectory)
{
	return std::wstring(NormalizeSharedDirectoryPath(strDirectory));
}

std::wstring CSharedFileList::MakeStartupCacheVolumeKey(const CString &strVolumeKey)
{
	const LongPathSeams::PathString strNormalized(LongPathSeams::NormalizeVolumeGuidPathForKey(static_cast<LPCTSTR>(strVolumeKey)));
	return std::wstring(strNormalized.c_str());
}

std::wstring CSharedFileList::MakeDuplicatePathCacheKey(const CString &strFilePath)
{
	return std::wstring(NormalizeSharedFilePath(strFilePath));
}

void CSharedFileList::CollectSharedDirectories(CStringList &dirlist) const
{
	dirlist.RemoveAll();
	std::unordered_set<std::wstring> addedDirectoryKeys;
	const auto addUniqueDirectory = [&dirlist, &addedDirectoryKeys](const CString &strDirectory) {
		const CString strCanonical(LexicallyNormalizeSharedDirectoryPath(strDirectory));
		if (strCanonical.IsEmpty())
			return;
		if (!addedDirectoryKeys.insert(std::wstring(strCanonical)).second)
			return;
		dirlist.AddTail(strCanonical);
	};

	addUniqueDirectory(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
	for (INT_PTR i = 1; i < thePrefs.GetCatCount(); ++i)
		addUniqueDirectory(thePrefs.GetCatPath(i));

	CStringList sharedDirs;
	thePrefs.CopySharedDirectoryList(sharedDirs);
	for (POSITION pos = sharedDirs.GetHeadPosition(); pos != NULL;)
		addUniqueDirectory(sharedDirs.GetNext(pos));
}

void CSharedFileList::NoteStartupCacheLoadResult(const bool bPresent, const bool bLoaded, const bool bRejected, const bool bRemoved, LPCSTR pszRejectCode, const ULONGLONG uRecordsLoaded, const ULONGLONG uVolumesLoaded)
{
	CSingleLock stateLock(&m_mutStartupCacheSave, TRUE);
	m_bStartupCacheFilePresent = bPresent;
	m_bStartupCacheLoaded = bLoaded;
	m_bStartupCacheRejected = bRejected;
	m_bStartupCacheRemoved = bRemoved;
	m_strStartupCacheRejectCode = pszRejectCode != NULL ? pszRejectCode : "";
	m_uStartupCacheRecordsLoaded = uRecordsLoaded;
	m_uStartupCacheVolumesLoaded = uVolumesLoaded;
}

void CSharedFileList::NoteDuplicatePathCacheLoadResult(const bool bLoaded, const bool bRejected, const bool bRemoved, LPCSTR pszRejectCode, const ULONGLONG uRecordsLoaded)
{
	CSingleLock stateLock(&m_mutStartupCacheSave, TRUE);
	m_bDuplicatePathCacheLoaded = bLoaded;
	m_bDuplicatePathCacheRejected = bRejected;
	m_bDuplicatePathCacheRemoved = bRemoved;
	m_strDuplicatePathCacheRejectCode = pszRejectCode != NULL ? pszRejectCode : "";
	m_uDuplicatePathCacheRecordsLoaded = uRecordsLoaded;
}

bool CSharedFileList::ReadStartupCacheString(CSafeBufferedFile &file, CString &rValue)
{
	constexpr uint32 kMaxStartupCacheChars = 32768u;
	const uint32 uCharCount = file.ReadUInt32();
	if (uCharCount > kMaxStartupCacheChars)
		return false;
	if (uCharCount == 0) {
		rValue.Empty();
		return true;
	}

	std::vector<WCHAR> buffer(uCharCount + 1u, L'\0');
	file.Read(buffer.data(), uCharCount * sizeof(WCHAR));
	buffer[uCharCount] = L'\0';
	rValue = CString(buffer.data());
	return true;
}

void CSharedFileList::WriteStartupCacheString(CSafeBufferedFile &file, const CString &strValue)
{
	const CStringW strWide(strValue);
	file.WriteUInt32(static_cast<uint32>(strWide.GetLength()));
	if (!strWide.IsEmpty())
		file.Write(static_cast<LPCWSTR>(strWide), strWide.GetLength() * sizeof(WCHAR));
}

bool CSharedFileList::GetDirectoryStartupState(const CString &strDirectory, DirectoryStartupState &rState)
{
	rState = DirectoryStartupState{};
	const CString strCanonicalDirectory(LexicallyNormalizeSharedDirectoryPath(strDirectory));
	const CString strDirectoryPath(PathHelpers::TrimTrailingSeparator(strCanonicalDirectory));

	WIN32_FIND_DATA findData = {};
	DWORD dwError = ERROR_SUCCESS;
	const HANDLE hFind = LongPathSeams::FindFirstFile(strDirectoryPath, &findData);
	if (hFind == INVALID_HANDLE_VALUE)
		return false;
	::FindClose(hFind);

	time_t tUtcDirectoryDate = (time_t)FileTimeToUnixTime(findData.ftLastWriteTime);
	if (tUtcDirectoryDate <= 0)
		tUtcDirectoryDate = (time_t)-1;
	rState.utcDirectoryDate = static_cast<LONGLONG>(tUtcDirectoryDate);

	rState.bHasIdentity = LongPathSeams::TryGetResolvedDirectoryIdentity(strDirectoryPath, rState.identity, &dwError);
	if (!rState.bHasIdentity)
		rState.identity = LongPathSeams::FileSystemObjectIdentity{};

	LongPathSeams::NtfsJournalVolumeState volumeState = {};
	LongPathSeams::NtfsDirectoryJournalState directoryJournalState = {};
	if (LongPathSeams::TryGetLocalNtfsJournalVolumeState(strDirectoryPath, volumeState, &dwError)
		&& LongPathSeams::TryGetNtfsDirectoryJournalState(strDirectoryPath, directoryJournalState, &dwError))
	{
		rState.bHasTrustedNtfsJournalState = true;
		rState.volumeRecord.strVolumeKey = CString(volumeState.strVolumeKey.c_str());
		rState.volumeRecord.ullVolumeSerialNumber = volumeState.ullVolumeSerialNumber;
		rState.volumeRecord.ullUsnJournalId = volumeState.ullUsnJournalId;
		rState.volumeRecord.llJournalCheckpointUsn = volumeState.llNextUsn;
		rState.directoryFileReference = directoryJournalState.fileReference;
	}
	return true;
}

bool CSharedFileList::GetFileStartupState(const CString &strFilePath, LONGLONG &rUtcFileDate, ULONGLONG &rullFileSize)
{
	const CString strCanonicalFilePath(LexicallyNormalizeSharedFilePath(strFilePath));
	WIN32_FIND_DATA findData = {};
	const HANDLE hFind = LongPathSeams::FindFirstFile(strCanonicalFilePath, &findData);
	if (hFind == INVALID_HANDLE_VALUE)
		return false;
	::FindClose(hFind);

	time_t tUtcFileDate = (time_t)FileTimeToUnixTime(findData.ftLastWriteTime);
	if (tUtcFileDate <= 0)
		tUtcFileDate = (time_t)-1;

	rUtcFileDate = static_cast<LONGLONG>(tUtcFileDate);
	rullFileSize = (static_cast<ULONGLONG>(findData.nFileSizeHigh) << 32) | findData.nFileSizeLow;
	return true;
}

bool CSharedFileList::HasPendingHashForDirectory(const CString &strDirectory) const
{
	const CString strDirectoryKey(MakeSharedDirectoryLookupKey(strDirectory));
	CSingleLock lock(&m_mutSharedHashQueue, TRUE);
	for (const SharedHashJob &job : m_sharedHashQueue)
		if (MakeSharedDirectoryLookupKey(job.strDirectory) == strDirectoryKey)
			return true;
	for (const SharedHashJob &job : m_sharedHashPendingCompletions)
		if (MakeSharedDirectoryLookupKey(job.strDirectory) == strDirectoryKey)
			return true;
	if (m_bSharedHashActive && MakeSharedDirectoryLookupKey(m_sharedHashActiveJob.strDirectory) == strDirectoryKey)
		return true;
	return false;
}

bool CSharedFileList::BuildStartupCacheRecord(const CString &strDirectory, SharedStartupCachePolicy::DirectoryRecord &rRecord, SharedStartupCacheVolumeRecordMap &rVolumeRecords) const
{
	const CString strCanonicalDirectory(NormalizeSharedDirectoryPath(strDirectory));
	if (!SharedStartupCachePolicy::CanPersistDirectorySnapshot(HasPendingHashForDirectory(strCanonicalDirectory)))
		return false;

	rRecord = SharedStartupCachePolicy::DirectoryRecord{};
	rRecord.strDirectoryPath = strCanonicalDirectory;
	DirectoryStartupState directoryState = {};
	if (!GetDirectoryStartupState(strCanonicalDirectory, directoryState))
		return false;
	rRecord.identity = directoryState.identity;
	rRecord.bHasIdentity = directoryState.bHasIdentity;
	rRecord.utcDirectoryDate = directoryState.utcDirectoryDate;
	if (directoryState.bHasTrustedNtfsJournalState) {
		rRecord.eValidationMode = SharedStartupCachePolicy::ValidationMode::LocalNtfsJournalFastPath;
		rRecord.volumeRecord = directoryState.volumeRecord;
		rRecord.directoryFileReference = directoryState.directoryFileReference;
		SharedStartupCachePolicy::VolumeRecord &rVolumeRecord = rVolumeRecords[MakeStartupCacheVolumeKey(rRecord.volumeRecord.strVolumeKey)];
		if (rVolumeRecord.strVolumeKey.IsEmpty())
			rVolumeRecord = rRecord.volumeRecord;
		else if (rVolumeRecord.strVolumeKey.CompareNoCase(rRecord.volumeRecord.strVolumeKey) == 0
			&& rVolumeRecord.ullUsnJournalId == rRecord.volumeRecord.ullUsnJournalId)
		{
			rVolumeRecord.llJournalCheckpointUsn = (rVolumeRecord.llJournalCheckpointUsn < rRecord.volumeRecord.llJournalCheckpointUsn)
				? rVolumeRecord.llJournalCheckpointUsn
				: rRecord.volumeRecord.llJournalCheckpointUsn;
		}
	}

	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair)) {
		CKnownFile *pFile = pair->value;
		if (pFile == NULL || pFile->IsKindOf(RUNTIME_CLASS(CPartFile)))
			continue;
		if (!HasSameDirectoryTextFast(pFile->GetPath(), strCanonicalDirectory))
			continue;

		SharedStartupCachePolicy::FileRecord fileRecord = {};
		fileRecord.strLeafName = pFile->GetFileName();
		fileRecord.utcFileDate = static_cast<LONGLONG>(pFile->GetUtcFileDate());
		fileRecord.ullFileSize = static_cast<ULONGLONG>(pFile->GetFileSize());
		rRecord.files.push_back(fileRecord);
	}

	rRecord.uCachedFileCount = static_cast<uint32>(rRecord.files.size());
	return SharedStartupCachePolicy::IsStructurallyValid(rRecord);
}

bool CSharedFileList::TryRehydrateSharedDirectoryFromCache(const CString &strDirectory)
{
	const auto it = m_startupCacheRecords.find(MakeStartupCacheKey(strDirectory));
	if (it == m_startupCacheRecords.end())
		return false;

	const SharedStartupCachePolicy::DirectoryRecord &rRecord = it->second;
	DirectoryStartupState directoryState = {};
	if (!GetDirectoryStartupState(strDirectory, directoryState))
		return false;

	const bool bIdentityMatches = !rRecord.bHasIdentity || (directoryState.bHasIdentity && directoryState.identity == rRecord.identity);
	if (!SharedStartupCachePolicy::MatchesVerifiedDirectoryState(rRecord, bIdentityMatches, true, directoryState.utcDirectoryDate))
		return false;

	if (SharedStartupCachePolicy::UsesTrustedNtfsFastPath(rRecord)) {
		if (!EnsureStartupCacheVolumeValidation(strDirectory, rRecord))
			return false;

		const auto itVolumeState = m_startupCacheVolumeValidation.find(MakeStartupCacheVolumeKey(rRecord.volumeRecord.strVolumeKey));
		if (itVolumeState == m_startupCacheVolumeValidation.end() || itVolumeState->second.bRescanAllDirectories)
			return false;

		if (itVolumeState->second.changedDirectoryFileReferences.find(rRecord.directoryFileReference) != itVolumeState->second.changedDirectoryFileReferences.end())
			return false;
	}

	struct RehydrateCandidate
	{
		CKnownFile *pKnownFile;
		CString strFoundFilePath;
	};
	std::vector<RehydrateCandidate> candidates;
	candidates.reserve(rRecord.files.size());

	for (const SharedStartupCachePolicy::FileRecord &rFileRecord : rRecord.files) {
		const CString strFoundFilePath(PathHelpers::AppendPathComponent(strDirectory, rFileRecord.strLeafName));
		if (!SharedStartupCachePolicy::UsesTrustedNtfsFastPath(rRecord)) {
			LONGLONG utcCurrentFileDate = -1;
			ULONGLONG ullCurrentFileSize = 0;
			if (!GetFileStartupState(strFoundFilePath, utcCurrentFileDate, ullCurrentFileSize))
				return false;

			if (!SharedStartupCachePolicy::MatchesVerifiedFileState(rFileRecord, true, utcCurrentFileDate, ullCurrentFileSize))
				return false;
		}

		CKnownFile *pKnownFile = theApp.knownfiles->FindKnownFile(rFileRecord.strLeafName, static_cast<time_t>(rFileRecord.utcFileDate), rFileRecord.ullFileSize);
		if (pKnownFile == NULL && SharedStartupCachePolicy::ShouldRescanDirectoryOnCachedLookupMiss())
			return false;
		if (pKnownFile != NULL)
			candidates.push_back({ pKnownFile, strFoundFilePath });
	}

	for (const RehydrateCandidate &candidate : candidates)
		(void)AddKnownSharedFile(candidate.pKnownFile, strDirectory, candidate.strFoundFilePath);

	return true;
}

bool CSharedFileList::TryLoadStartupCache()
{
	m_startupCacheRecords.clear();
	m_startupCacheVolumes.clear();
	m_startupCacheVolumeValidation.clear();

	const CString strFullPath(GetStartupCachePath());
	(void)LongPathSeams::DeleteFileIfExists(strFullPath + _T(".tmp"));
	const bool bCacheFilePresent = LongPathSeams::PathExists(strFullPath);
	NoteStartupCacheLoadResult(bCacheFilePresent, false, false, false, "", 0, 0);
	if (!bCacheFilePresent)
		return true;

	CSafeBufferedFile file;
	if (!LongPathSeams::OpenFile(file, strFullPath, CFile::modeRead | CFile::shareDenyWrite | CFile::typeBinary)) {
		NoteStartupCacheLoadResult(true, false, true, false, "open_failed", 0, 0);
		return false;
	}

	try {
		constexpr uint32 kMaxVolumeCount = 1024u;
		constexpr uint32 kMaxDirectoryCount = 100000u;
		constexpr uint32 kMaxFileCountPerDirectory = 1000000u;

		if (file.ReadUInt32() != SharedStartupCachePolicy::kMagic || file.ReadUInt16() != SharedStartupCachePolicy::kVersion)
			AfxThrowFileException(CFileException::genericException);

		const uint32 uVolumeCount = file.ReadUInt32();
		if (uVolumeCount > kMaxVolumeCount)
			AfxThrowFileException(CFileException::genericException);
		for (uint32 i = 0; i < uVolumeCount; ++i) {
			SharedStartupCachePolicy::VolumeRecord volumeRecord = {};
			if (!ReadStartupCacheString(file, volumeRecord.strVolumeKey))
				AfxThrowFileException(CFileException::genericException);
			volumeRecord.ullVolumeSerialNumber = file.ReadUInt64();
			volumeRecord.ullUsnJournalId = file.ReadUInt64();
			volumeRecord.llJournalCheckpointUsn = static_cast<LONGLONG>(file.ReadUInt64());
			if (volumeRecord.strVolumeKey.IsEmpty()
				|| volumeRecord.ullVolumeSerialNumber == 0
				|| volumeRecord.ullUsnJournalId == 0
				|| volumeRecord.llJournalCheckpointUsn <= 0
				|| m_startupCacheVolumes.find(MakeStartupCacheVolumeKey(volumeRecord.strVolumeKey)) != m_startupCacheVolumes.end())
			{
				AfxThrowFileException(CFileException::genericException);
			}
			m_startupCacheVolumes.emplace(MakeStartupCacheVolumeKey(volumeRecord.strVolumeKey), volumeRecord);
		}

		const uint32 uDirectoryCount = file.ReadUInt32();
		if (uDirectoryCount > kMaxDirectoryCount)
			AfxThrowFileException(CFileException::genericException);

		for (uint32 i = 0; i < uDirectoryCount; ++i) {
			SharedStartupCachePolicy::DirectoryRecord record = {};
			if (!ReadStartupCacheString(file, record.strDirectoryPath))
				AfxThrowFileException(CFileException::genericException);

			record.bHasIdentity = (file.ReadUInt8() != 0);
			record.identity.bHasExtendedFileId = (file.ReadUInt8() != 0);
			record.identity.ullVolumeSerialNumber = file.ReadUInt64();
			file.Read(record.identity.fileId.data(), static_cast<UINT>(record.identity.fileId.size()));
			record.utcDirectoryDate = static_cast<LONGLONG>(file.ReadUInt64());
			record.eValidationMode = static_cast<SharedStartupCachePolicy::ValidationMode>(file.ReadUInt8());
			if (!ReadStartupCacheString(file, record.volumeRecord.strVolumeKey))
				AfxThrowFileException(CFileException::genericException);
			file.Read(record.directoryFileReference.identifier.data(), static_cast<UINT>(record.directoryFileReference.identifier.size()));
			record.uCachedFileCount = file.ReadUInt32();
			if (record.uCachedFileCount > kMaxFileCountPerDirectory)
				AfxThrowFileException(CFileException::genericException);

			if (record.eValidationMode == SharedStartupCachePolicy::ValidationMode::LocalNtfsJournalFastPath) {
				const auto itVolume = m_startupCacheVolumes.find(MakeStartupCacheVolumeKey(record.volumeRecord.strVolumeKey));
				if (itVolume == m_startupCacheVolumes.end())
					AfxThrowFileException(CFileException::genericException);
				record.volumeRecord = itVolume->second;
			}

			record.files.reserve(record.uCachedFileCount);
			for (uint32 j = 0; j < record.uCachedFileCount; ++j) {
				SharedStartupCachePolicy::FileRecord fileRecord = {};
				if (!ReadStartupCacheString(file, fileRecord.strLeafName))
					AfxThrowFileException(CFileException::genericException);
				fileRecord.utcFileDate = static_cast<LONGLONG>(file.ReadUInt64());
				fileRecord.ullFileSize = file.ReadUInt64();
				record.files.push_back(fileRecord);
			}

			if (!SharedStartupCachePolicy::IsStructurallyValid(record)
				|| m_startupCacheRecords.find(MakeStartupCacheKey(record.strDirectoryPath)) != m_startupCacheRecords.end())
			{
				AfxThrowFileException(CFileException::genericException);
			}

			m_startupCacheRecords.emplace(MakeStartupCacheKey(record.strDirectoryPath), record);
		}
		file.Close();
		NoteStartupCacheLoadResult(true, true, false, false, "", static_cast<ULONGLONG>(m_startupCacheRecords.size()), static_cast<ULONGLONG>(m_startupCacheVolumes.size()));
		return true;
	} catch (CFileException *ex) {
		ex->Delete();
		file.Abort();
		m_startupCacheRecords.clear();
		m_startupCacheVolumes.clear();
		m_startupCacheVolumeValidation.clear();
		const bool bRemoved = LongPathSeams::DeleteFileIfExists(strFullPath);
		NoteStartupCacheLoadResult(true, false, true, bRemoved, "malformed", 0, 0);
		if (SharedStartupCachePolicy::ShouldRejectWholeCacheOnMalformedBlock()) {
			DebugLogWarning(_T("Ignoring malformed %s"), SharedStartupCachePolicy::GetFileName());
			if (!bRemoved)
				DebugLogWarning(_T("Unable to remove malformed %s"), SharedStartupCachePolicy::GetFileName());
		}
	}
	return false;
}

bool CSharedFileList::TryLoadDuplicatePathCache()
{
	m_duplicateSharedPathRecords.clear();

	const CString strFullPath(GetDuplicatePathCachePath());
	(void)LongPathSeams::DeleteFileIfExists(strFullPath + _T(".tmp"));
	if (!LongPathSeams::PathExists(strFullPath)) {
		NoteDuplicatePathCacheLoadResult(false, false, false, "", 0);
		return true;
	}

	CSafeBufferedFile file;
	if (!LongPathSeams::OpenFile(file, strFullPath, CFile::modeRead | CFile::shareDenyWrite | CFile::typeBinary)) {
		NoteDuplicatePathCacheLoadResult(false, true, false, "open_failed", 0);
		return false;
	}

	try {
		constexpr uint32 kMaxRecordCount = 1000000u;
		if (file.ReadUInt32() != SharedDuplicatePathCachePolicy::kMagic || file.ReadUInt16() != SharedDuplicatePathCachePolicy::kVersion)
			AfxThrowFileException(CFileException::genericException);

		const uint32 uRecordCount = file.ReadUInt32();
		if (uRecordCount > kMaxRecordCount)
			AfxThrowFileException(CFileException::genericException);

		for (uint32 i = 0; i < uRecordCount; ++i) {
			SharedDuplicatePathCachePolicy::PathRecord record = {};
			if (!ReadStartupCacheString(file, record.strFilePath))
				AfxThrowFileException(CFileException::genericException);
			record.utcFileDate = static_cast<LONGLONG>(file.ReadUInt64());
			record.ullFileSize = file.ReadUInt64();
			file.Read(record.canonicalFileHash.data(), static_cast<UINT>(record.canonicalFileHash.size()));
			if (!SharedDuplicatePathCachePolicy::IsStructurallyValid(record)
				|| m_duplicateSharedPathRecords.find(MakeDuplicatePathCacheKey(record.strFilePath)) != m_duplicateSharedPathRecords.end())
			{
				AfxThrowFileException(CFileException::genericException);
			}
			m_duplicateSharedPathRecords.emplace(MakeDuplicatePathCacheKey(record.strFilePath), record);
		}
		file.Close();
		NoteDuplicatePathCacheLoadResult(true, false, false, "", static_cast<ULONGLONG>(m_duplicateSharedPathRecords.size()));
		return true;
	} catch (CFileException *ex) {
		ex->Delete();
		file.Abort();
		m_duplicateSharedPathRecords.clear();
		const bool bRemoved = LongPathSeams::DeleteFileIfExists(strFullPath);
		NoteDuplicatePathCacheLoadResult(false, true, bRemoved, "malformed", 0);
		DebugLogWarning(_T("Ignoring malformed %s"), SharedDuplicatePathCachePolicy::GetFileName());
		if (!bRemoved)
			DebugLogWarning(_T("Unable to remove malformed %s"), SharedDuplicatePathCachePolicy::GetFileName());
	}
	return false;
}

void CSharedFileList::CollectTrackedStartupCacheDirectoryRefs(const SharedStartupCachePolicy::VolumeRecord &rVolumeRecord, std::unordered_set<LongPathSeams::UsnFileReference, LongPathSeams::UsnFileReferenceHasher> &rTrackedDirectoryRefs) const
{
	rTrackedDirectoryRefs.clear();
	for (const auto &entry : m_startupCacheRecords) {
		const SharedStartupCachePolicy::DirectoryRecord &rRecord = entry.second;
		if (!SharedStartupCachePolicy::UsesTrustedNtfsFastPath(rRecord))
			continue;
		if (MakeStartupCacheVolumeKey(rRecord.volumeRecord.strVolumeKey) != MakeStartupCacheVolumeKey(rVolumeRecord.strVolumeKey))
			continue;
		rTrackedDirectoryRefs.insert(rRecord.directoryFileReference);
	}
}

bool CSharedFileList::EnsureStartupCacheVolumeValidation(const CString &strDirectory, const SharedStartupCachePolicy::DirectoryRecord &rRecord)
{
	const std::wstring strVolumeKey(MakeStartupCacheVolumeKey(rRecord.volumeRecord.strVolumeKey));
	StartupCacheVolumeValidationState &rValidationState = m_startupCacheVolumeValidation[strVolumeKey];
	if (rValidationState.bInitialized)
		return !rValidationState.bRescanAllDirectories;

	rValidationState.bInitialized = true;
	LongPathSeams::NtfsJournalVolumeState currentVolumeState = {};
	DWORD dwError = ERROR_SUCCESS;
	if (!LongPathSeams::TryGetLocalNtfsJournalVolumeState(PathHelpers::TrimTrailingSeparator(strDirectory), currentVolumeState, &dwError)
		|| !SharedStartupCachePolicy::MatchesTrustedNtfsVolumeGuard(rRecord.volumeRecord, true, CString(currentVolumeState.strVolumeKey.c_str()), currentVolumeState.ullVolumeSerialNumber, currentVolumeState.ullUsnJournalId, currentVolumeState.llLowestValidUsn, currentVolumeState.llNextUsn))
	{
		rValidationState.bRescanAllDirectories = true;
		return false;
	}

	std::unordered_set<LongPathSeams::UsnFileReference, LongPathSeams::UsnFileReferenceHasher> trackedDirectoryRefs;
	CollectTrackedStartupCacheDirectoryRefs(rRecord.volumeRecord, trackedDirectoryRefs);
	if (!LongPathSeams::TryCollectChangedDirectoryFileReferences(
			PathHelpers::TrimTrailingSeparator(strDirectory),
			rRecord.volumeRecord.ullUsnJournalId,
			rRecord.volumeRecord.llJournalCheckpointUsn,
			trackedDirectoryRefs,
			rValidationState.changedDirectoryFileReferences,
			&dwError))
	{
		rValidationState.bRescanAllDirectories = true;
		rValidationState.changedDirectoryFileReferences.clear();
		return false;
	}

	return true;
}

bool CSharedFileList::CaptureStartupCacheSaveSnapshot(StartupCacheSaveSnapshot &rSnapshot) const
{
	rSnapshot = StartupCacheSaveSnapshot{};
	{
		CSingleLock stateLock(&m_mutStartupCacheSave, TRUE);
		if (m_bStartupCacheInvalidatedByInterruptedHashing)
			return false;
	}
	rSnapshot.strCachePath = GetStartupCachePath();
	rSnapshot.strDuplicatePathCachePath = GetDuplicatePathCachePath();

	CStringList sharedDirectories;
	CollectSharedDirectories(sharedDirectories);
	rSnapshot.directories.reserve(static_cast<size_t>(sharedDirectories.GetCount()));

	std::unordered_map<std::wstring, size_t> directoryIndex;
	const auto addSnapshotDirectory = [&rSnapshot, &directoryIndex](const CString &strDirectory) {
		const std::wstring strDirectoryKey(MakeStartupCacheSnapshotKey(strDirectory));
		if (directoryIndex.find(strDirectoryKey) != directoryIndex.end())
			return;

		StartupCacheSaveDirectorySnapshot directory = {};
		directory.strDirectoryPath = strDirectory;
		const size_t uIndex = rSnapshot.directories.size();
		directoryIndex[strDirectoryKey] = uIndex;
		rSnapshot.directories.push_back(std::move(directory));
	};

	for (POSITION pos = sharedDirectories.GetHeadPosition(); pos != NULL;)
		addSnapshotDirectory(sharedDirectories.GetNext(pos));

	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair)) {
		CKnownFile *pFile = pair->value;
		if (pFile == NULL || pFile->IsKindOf(RUNTIME_CLASS(CPartFile)))
			continue;
		addSnapshotDirectory(pFile->GetPath());
	}

	{
		CSingleLock lock(&m_mutSharedHashQueue, TRUE);
		for (const SharedHashJob &job : m_sharedHashQueue) {
			const auto it = directoryIndex.find(MakeStartupCacheSnapshotKey(job.strDirectory));
			if (it != directoryIndex.end())
				rSnapshot.directories[it->second].bHasPendingHash = true;
		}
		for (const SharedHashJob &job : m_sharedHashPendingCompletions) {
			const auto it = directoryIndex.find(MakeStartupCacheSnapshotKey(job.strDirectory));
			if (it != directoryIndex.end())
				rSnapshot.directories[it->second].bHasPendingHash = true;
		}
		if (m_bSharedHashActive) {
			const auto it = directoryIndex.find(MakeStartupCacheSnapshotKey(m_sharedHashActiveJob.strDirectory));
			if (it != directoryIndex.end())
				rSnapshot.directories[it->second].bHasPendingHash = true;
		}
	}

	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair)) {
		CKnownFile *pFile = pair->value;
		if (pFile == NULL || pFile->IsKindOf(RUNTIME_CLASS(CPartFile)))
			continue;

		const auto it = directoryIndex.find(MakeStartupCacheSnapshotKey(pFile->GetPath()));
		if (it == directoryIndex.end())
			continue;

		SharedStartupCachePolicy::FileRecord fileRecord = {};
		fileRecord.strLeafName = pFile->GetFileName();
		fileRecord.utcFileDate = static_cast<LONGLONG>(pFile->GetUtcFileDate());
		fileRecord.ullFileSize = static_cast<ULONGLONG>(pFile->GetFileSize());
		rSnapshot.directories[it->second].files.push_back(fileRecord);
	}
	return CaptureDuplicatePathCacheSnapshot(rSnapshot.duplicatePathRecords);
}

bool CSharedFileList::CaptureDuplicatePathCacheSnapshot(std::vector<SharedDuplicatePathCachePolicy::PathRecord> &rSnapshot) const
{
	rSnapshot.clear();
	rSnapshot.reserve(m_duplicateSharedPathRecords.size());
	for (const auto &entry : m_duplicateSharedPathRecords) {
		const SharedDuplicatePathCachePolicy::PathRecord &record = entry.second;
		if (!SharedDuplicatePathCachePolicy::IsStructurallyValid(record))
			continue;

		CKnownFile *pCanonicalSharedFile = GetFileByID(record.canonicalFileHash.data());
		if (pCanonicalSharedFile == NULL)
			continue;
		if (MakeSharedFileLookupKey(record.strFilePath) == MakeSharedFileLookupKey(pCanonicalSharedFile->GetFilePath()))
			continue;

		const CString strDuplicatePath(NormalizeSharedFilePath(record.strFilePath));
		const CString strDuplicateDirectory(PathHelpers::EnsureTrailingSeparator(PathHelpers::GetDirectoryPath(strDuplicatePath)));
		if (!ShouldBeShared(strDuplicateDirectory, strDuplicatePath, false))
			continue;

		LONGLONG utcCurrentFileDate = -1;
		ULONGLONG ullCurrentFileSize = 0;
		if (!GetFileStartupState(strDuplicatePath, utcCurrentFileDate, ullCurrentFileSize))
			continue;
		if (utcCurrentFileDate != record.utcFileDate || ullCurrentFileSize != record.ullFileSize)
			continue;

		rSnapshot.push_back(record);
	}
	return true;
}

void CSharedFileList::RememberDuplicateSharedPath(const CString &strFilePath, const uchar *pCanonicalFileHash, const LONGLONG utcFileDate, const ULONGLONG ullFileSize)
{
	if (pCanonicalFileHash == NULL || utcFileDate <= 0 || ullFileSize == 0)
		return;

	SharedDuplicatePathCachePolicy::PathRecord record = {};
	record.strFilePath = NormalizeSharedFilePath(strFilePath);
	record.utcFileDate = utcFileDate;
	record.ullFileSize = ullFileSize;
	md4cpy(record.canonicalFileHash.data(), pCanonicalFileHash);
	if (!SharedDuplicatePathCachePolicy::IsStructurallyValid(record))
		return;

	const std::wstring strKey(MakeDuplicatePathCacheKey(record.strFilePath));
	const auto it = m_duplicateSharedPathRecords.find(strKey);
	if (it != m_duplicateSharedPathRecords.end()
		&& it->second.strFilePath == record.strFilePath
		&& it->second.utcFileDate == record.utcFileDate
		&& it->second.ullFileSize == record.ullFileSize
		&& it->second.canonicalFileHash == record.canonicalFileHash)
	{
		return;
	}

	m_duplicateSharedPathRecords[strKey] = record;
	MarkStartupCacheDirty();
}

void CSharedFileList::ReportDuplicateSharedFileWarning(const CString &strCanonicalPath, const CString &strDuplicatePath)
{
	if (m_uDuplicateSharedFileWarningsShown < kMaxVisibleDuplicateSharedFileWarnings) {
		++m_uDuplicateSharedFileWarningsShown;
		LogWarning(GetResString(IDS_ERR_DUPL_FILES), (LPCTSTR)strCanonicalPath, (LPCTSTR)strDuplicatePath);
		return;
	}

	++m_uDuplicateSharedFileWarningsSuppressed;
	if (!m_bDuplicateSharedFileWarningSuppressionLogged) {
		m_bDuplicateSharedFileWarningSuppressionLogged = true;
		LogWarning(_T("Additional duplicate shared-file warnings suppressed; duplicate paths are still recorded in %s."),
			SharedDuplicatePathCachePolicy::GetFileName());
	}
}

bool CSharedFileList::TryReuseRememberedDuplicateSharedPath(const CString &strFilePath, const LONGLONG utcFileDate, const ULONGLONG ullFileSize)
{
	if (!m_bStartupDuplicateReuseActive || utcFileDate <= 0 || ullFileSize == 0)
		return false;

	const auto it = m_duplicateSharedPathRecords.find(MakeDuplicatePathCacheKey(strFilePath));
	if (it == m_duplicateSharedPathRecords.end())
		return false;

	const SharedDuplicatePathCachePolicy::PathRecord &record = it->second;
	if (record.utcFileDate != utcFileDate || record.ullFileSize != ullFileSize) {
		m_duplicateSharedPathRecords.erase(it);
		MarkStartupCacheDirty();
		return false;
	}

	CKnownFile *pCanonicalKnownFile = theApp.knownfiles->FindKnownFileByID(record.canonicalFileHash.data());
	if (pCanonicalKnownFile == NULL || MakeSharedFileLookupKey(record.strFilePath) == MakeSharedFileLookupKey(pCanonicalKnownFile->GetFilePath())) {
		m_duplicateSharedPathRecords.erase(it);
		MarkStartupCacheDirty();
		return false;
	}

	return true;
}

bool CSharedFileList::BuildStartupCacheRecordFromSnapshot(const StartupCacheSaveDirectorySnapshot &rDirectory, SharedStartupCachePolicy::DirectoryRecord &rRecord, SharedStartupCacheVolumeRecordMap &rVolumeRecords)
{
	const CString strCanonicalDirectory(NormalizeSharedDirectoryPath(rDirectory.strDirectoryPath));
	if (!SharedStartupCachePolicy::CanPersistDirectorySnapshot(rDirectory.bHasPendingHash))
		return false;

	rRecord = SharedStartupCachePolicy::DirectoryRecord{};
	rRecord.strDirectoryPath = strCanonicalDirectory;
	DirectoryStartupState directoryState = {};
	if (!GetDirectoryStartupState(strCanonicalDirectory, directoryState))
		return false;
	rRecord.identity = directoryState.identity;
	rRecord.bHasIdentity = directoryState.bHasIdentity;
	rRecord.utcDirectoryDate = directoryState.utcDirectoryDate;
	if (directoryState.bHasTrustedNtfsJournalState) {
		rRecord.eValidationMode = SharedStartupCachePolicy::ValidationMode::LocalNtfsJournalFastPath;
		rRecord.volumeRecord = directoryState.volumeRecord;
		rRecord.directoryFileReference = directoryState.directoryFileReference;
		SharedStartupCachePolicy::VolumeRecord &rVolumeRecord = rVolumeRecords[MakeStartupCacheVolumeKey(rRecord.volumeRecord.strVolumeKey)];
		if (rVolumeRecord.strVolumeKey.IsEmpty())
			rVolumeRecord = rRecord.volumeRecord;
		else if (rVolumeRecord.strVolumeKey.CompareNoCase(rRecord.volumeRecord.strVolumeKey) == 0
			&& rVolumeRecord.ullUsnJournalId == rRecord.volumeRecord.ullUsnJournalId)
		{
			rVolumeRecord.llJournalCheckpointUsn = (rVolumeRecord.llJournalCheckpointUsn < rRecord.volumeRecord.llJournalCheckpointUsn)
				? rVolumeRecord.llJournalCheckpointUsn
				: rRecord.volumeRecord.llJournalCheckpointUsn;
		}
	}

	rRecord.files = rDirectory.files;
	rRecord.uCachedFileCount = static_cast<uint32>(rRecord.files.size());
	return SharedStartupCachePolicy::IsStructurallyValid(rRecord);
}

void CSharedFileList::UpdateStartupCacheSaveProgress(StartupCacheSavePhase ePhase, ULONGLONG uCompletedDirectories, ULONGLONG uTotalDirectories)
{
	CSingleLock stateLock(&m_mutStartupCacheSave, TRUE);
	m_eStartupCacheSavePhase = ePhase;
	m_uStartupCacheSaveDirectoriesDone = uCompletedDirectories;
	m_uStartupCacheSaveDirectoriesTotal = uTotalDirectories;
}

void CSharedFileList::UpdateStartupCacheSaveOperationProgress(const std::shared_ptr<StartupCacheSaveOperation> &pOperation, StartupCacheSavePhase ePhase, ULONGLONG uCompletedDirectories, ULONGLONG uTotalDirectories)
{
	if (!pOperation)
		return;
	CSingleLock progressLock(&pOperation->mutProgress, TRUE);
	pOperation->ePhase = ePhase;
	pOperation->uCompletedDirectories = uCompletedDirectories;
	pOperation->uTotalDirectories = uTotalDirectories;
}

bool CSharedFileList::WriteStartupCacheFile(const CString &strFullPath, const SharedStartupCacheVolumeRecordMap &rVolumeRecords, const std::vector<SharedStartupCachePolicy::DirectoryRecord> &rRecords)
{
	const CString strTempPath(strFullPath + _T(".tmp"));
	CSafeBufferedFile file;
	if (!LongPathSeams::OpenFile(file, strTempPath, CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite | CFile::typeBinary))
		return false;

	try {
		file.WriteUInt32(SharedStartupCachePolicy::kMagic);
		file.WriteUInt16(SharedStartupCachePolicy::kVersion);
		file.WriteUInt32(static_cast<uint32>(rVolumeRecords.size()));
		for (const auto &entry : rVolumeRecords) {
			const SharedStartupCachePolicy::VolumeRecord &volumeRecord = entry.second;
			WriteStartupCacheString(file, volumeRecord.strVolumeKey);
			file.WriteUInt64(volumeRecord.ullVolumeSerialNumber);
			file.WriteUInt64(volumeRecord.ullUsnJournalId);
			file.WriteUInt64(static_cast<uint64>(volumeRecord.llJournalCheckpointUsn));
		}
		file.WriteUInt32(static_cast<uint32>(rRecords.size()));
		for (const SharedStartupCachePolicy::DirectoryRecord &record : rRecords) {
			WriteStartupCacheString(file, record.strDirectoryPath);
			file.WriteUInt8(record.bHasIdentity ? 1u : 0u);
			file.WriteUInt8(record.identity.bHasExtendedFileId ? 1u : 0u);
			file.WriteUInt64(record.identity.ullVolumeSerialNumber);
			file.Write(record.identity.fileId.data(), static_cast<UINT>(record.identity.fileId.size()));
			file.WriteUInt64(static_cast<uint64>(record.utcDirectoryDate));
			file.WriteUInt8(static_cast<uint8>(record.eValidationMode));
			WriteStartupCacheString(file, record.volumeRecord.strVolumeKey);
			file.Write(record.directoryFileReference.identifier.data(), static_cast<UINT>(record.directoryFileReference.identifier.size()));
			file.WriteUInt32(record.uCachedFileCount);
			for (const SharedStartupCachePolicy::FileRecord &fileRecord : record.files) {
				WriteStartupCacheString(file, fileRecord.strLeafName);
				file.WriteUInt64(static_cast<uint64>(fileRecord.utcFileDate));
				file.WriteUInt64(fileRecord.ullFileSize);
			}
		}
		CommitAndClose(file);
		if (!LongPathSeams::MoveFileEx(strTempPath, strFullPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			(void)LongPathSeams::DeleteFileIfExists(strTempPath);
			return false;
		}
		return true;
	} catch (CFileException *ex) {
		ex->Delete();
		(void)LongPathSeams::DeleteFileIfExists(strTempPath);
	}
	return false;
}

bool CSharedFileList::WriteDuplicatePathCacheFile(const CString &strFullPath, const std::vector<SharedDuplicatePathCachePolicy::PathRecord> &rRecords)
{
	const CString strTempPath(strFullPath + _T(".tmp"));
	CSafeBufferedFile file;
	if (!LongPathSeams::OpenFile(file, strTempPath, CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite | CFile::typeBinary))
		return false;

	try {
		file.WriteUInt32(SharedDuplicatePathCachePolicy::kMagic);
		file.WriteUInt16(SharedDuplicatePathCachePolicy::kVersion);
		file.WriteUInt32(static_cast<uint32>(rRecords.size()));
		for (const SharedDuplicatePathCachePolicy::PathRecord &record : rRecords) {
			WriteStartupCacheString(file, record.strFilePath);
			file.WriteUInt64(static_cast<uint64>(record.utcFileDate));
			file.WriteUInt64(record.ullFileSize);
			file.Write(record.canonicalFileHash.data(), static_cast<UINT>(record.canonicalFileHash.size()));
		}
		CommitAndClose(file);
		if (!LongPathSeams::MoveFileEx(strTempPath, strFullPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			(void)LongPathSeams::DeleteFileIfExists(strTempPath);
			return false;
		}
		return true;
	} catch (CFileException *ex) {
		ex->Delete();
		(void)LongPathSeams::DeleteFileIfExists(strTempPath);
	}
	return false;
}

void CSharedFileList::RunStartupCacheSaveWorker(const StartupCacheSaveSnapshot &rSnapshot, const std::shared_ptr<StartupCacheSaveOperation> &pOperation, StartupCacheSaveResult &rResult)
{
	std::vector<SharedStartupCachePolicy::DirectoryRecord> records;
	SharedStartupCacheVolumeRecordMap volumeRecords;
	records.reserve(rSnapshot.directories.size());
	UpdateStartupCacheSaveOperationProgress(pOperation, StartupCacheSavePhase::BuildingRecords, 0, static_cast<ULONGLONG>(rSnapshot.directories.size()));
	for (size_t i = 0; i < rSnapshot.directories.size(); ++i) {
		SharedStartupCachePolicy::DirectoryRecord record = {};
		if (BuildStartupCacheRecordFromSnapshot(rSnapshot.directories[i], record, volumeRecords))
			records.push_back(record);
		UpdateStartupCacheSaveOperationProgress(pOperation, StartupCacheSavePhase::BuildingRecords, static_cast<ULONGLONG>(i + 1), static_cast<ULONGLONG>(rSnapshot.directories.size()));
	}
	for (SharedStartupCachePolicy::DirectoryRecord &record : records) {
		if (!SharedStartupCachePolicy::UsesTrustedNtfsFastPath(record))
			continue;
		const auto itVolume = volumeRecords.find(MakeStartupCacheVolumeKey(record.volumeRecord.strVolumeKey));
		if (itVolume != volumeRecords.end())
			record.volumeRecord = itVolume->second;
	}

	UpdateStartupCacheSaveOperationProgress(pOperation, StartupCacheSavePhase::WritingFile, static_cast<ULONGLONG>(rSnapshot.directories.size()), static_cast<ULONGLONG>(rSnapshot.directories.size()));
	if (!WriteStartupCacheFile(rSnapshot.strCachePath, volumeRecords, records))
		return;
	rResult.bWriteSucceeded = true;

	if (WriteDuplicatePathCacheFile(rSnapshot.strDuplicatePathCachePath, rSnapshot.duplicatePathRecords)) {
		for (const SharedDuplicatePathCachePolicy::PathRecord &record : rSnapshot.duplicatePathRecords)
			rResult.duplicatePathRecords.emplace(MakeDuplicatePathCacheKey(record.strFilePath), record);
		rResult.bDuplicatePathWriteSucceeded = true;
	}

	for (const SharedStartupCachePolicy::DirectoryRecord &record : records)
		rResult.records.emplace(MakeStartupCacheKey(record.strDirectoryPath), record);
	rResult.volumeRecords = std::move(volumeRecords);
	rResult.ullCompletedTick = ::GetTickCount64();
}

UINT AFX_CDECL CSharedFileList::StartupCacheSaveThreadProc(LPVOID pParam)
{
	std::unique_ptr<StartupCacheSaveThreadRequest> pRequest(static_cast<StartupCacheSaveThreadRequest*>(pParam));
	std::unique_ptr<StartupCacheSaveResult> pResult(new StartupCacheSaveResult{});
	if (pRequest) {
		try {
			RunStartupCacheSaveWorker(pRequest->snapshot, pRequest->pOperation, *pResult);
		} catch (CMemoryException *ex) {
			ex->Delete();
			DebugLogError(_T("Startup cache save worker aborted after a memory exception"));
		} catch (CException *ex) {
			ex->Delete();
			DebugLogError(_T("Startup cache save worker aborted after an MFC exception"));
		} catch (...) {
			DebugLogError(_T("Startup cache save worker aborted after an unknown exception"));
		}
	}

	bool bPosted = false;
	std::unique_ptr<StartupCacheSaveThreadCompletion> pCompletion(new StartupCacheSaveThreadCompletion{});
	pCompletion->pOperation = pRequest ? pRequest->pOperation : std::shared_ptr<StartupCacheSaveOperation>();
	pCompletion->pResult = pResult.release();
	const HWND hNotifyWnd = pRequest ? pRequest->hNotifyWnd : NULL;
	if (hNotifyWnd != NULL && ::IsWindow(hNotifyWnd))
		bPosted = (::PostMessage(hNotifyWnd, UM_STARTUP_CACHE_SAVE_COMPLETE, 0, reinterpret_cast<LPARAM>(pCompletion.get())) != FALSE);

	if (!bPosted) {
		DiscardStartupCacheSaveCompletion(pCompletion.release());
	} else {
		(void)pCompletion.release();
	}

	return 0;
}

void CSharedFileList::HandleStartupCacheSaveCompletion(void *pResultVoid)
{
	std::unique_ptr<StartupCacheSaveThreadCompletion> pCompletion(static_cast<StartupCacheSaveThreadCompletion*>(pResultVoid));
	if (pCompletion == NULL)
		return;
	StartupCacheSaveResult *pResult = pCompletion->pResult;
	if (pResult == NULL)
		return;
	pCompletion->pResult = NULL;
	std::unique_ptr<StartupCacheSaveResult> resultGuard(pResult);

	bool bStartupCacheInvalidated = false;
	bool bStartupCacheSaveShutdownAbandoned = false;
	bool bRunAfterCurrent = false;
	{
		CSingleLock stateLock(&m_mutStartupCacheSave, TRUE);
		if (pCompletion->pOperation != m_pStartupCacheSaveOperation) {
			stateLock.Unlock();
			pCompletion->pResult = resultGuard.release();
			DiscardStartupCacheSaveCompletion(pCompletion.release());
			return;
		}
		bStartupCacheInvalidated = m_bStartupCacheInvalidatedByInterruptedHashing;
		bStartupCacheSaveShutdownAbandoned = m_bStartupCacheSaveShutdownAbandoned;
		bRunAfterCurrent = m_bStartupCacheSaveRunAfterCurrent;
	}
	const SharedFileListSeams::StartupCacheSaveCompletionAction eCompletionAction =
		SharedFileListSeams::GetStartupCacheSaveCompletionAction({
			bStartupCacheInvalidated,
			bStartupCacheSaveShutdownAbandoned,
			pResult->bWriteSucceeded,
			pResult->bDuplicatePathWriteSucceeded,
			bRunAfterCurrent
		});
	if (eCompletionAction == SharedFileListSeams::StartupCacheSaveCompletionAction::DiscardPersistedResultAndClearDirty) {
		if (pResult->bWriteSucceeded)
			(void)LongPathSeams::DeleteFileIfExists(GetStartupCachePath());
		if (pResult->bDuplicatePathWriteSucceeded)
			(void)LongPathSeams::DeleteFileIfExists(GetDuplicatePathCachePath());
		{
			CSingleLock stateLock(&m_mutStartupCacheSave, TRUE);
			m_bStartupCacheSaveRunning = false;
			m_bStartupCacheSaveRunAfterCurrent = false;
			m_pStartupCacheSaveOperation.reset();
			m_eStartupCacheSavePhase = StartupCacheSavePhase::Idle;
			m_uStartupCacheSaveDirectoriesDone = 0;
			m_uStartupCacheSaveDirectoriesTotal = 0;
			m_bStartupCacheDirty = false;
		}
		return;
	}

	UpdateStartupCacheSaveProgress(StartupCacheSavePhase::ApplyingResult, m_uStartupCacheSaveDirectoriesTotal, m_uStartupCacheSaveDirectoriesTotal);
	if (pResult->bWriteSucceeded) {
		m_startupCacheRecords = std::move(pResult->records);
		m_startupCacheVolumes = std::move(pResult->volumeRecords);
		m_startupCacheVolumeValidation.clear();
	}
	if (pResult->bDuplicatePathWriteSucceeded)
		m_duplicateSharedPathRecords = std::move(pResult->duplicatePathRecords);

	{
		CSingleLock stateLock(&m_mutStartupCacheSave, TRUE);
		if (pResult->bWriteSucceeded)
			m_nLastStartupCacheSave = pResult->ullCompletedTick;
		m_bStartupCacheSaveRunning = false;
		m_bStartupCacheSaveRunAfterCurrent = false;
		m_pStartupCacheSaveOperation.reset();
		m_eStartupCacheSavePhase = StartupCacheSavePhase::Idle;
		m_uStartupCacheSaveDirectoriesDone = 0;
		m_uStartupCacheSaveDirectoriesTotal = 0;
		if (eCompletionAction == SharedFileListSeams::StartupCacheSaveCompletionAction::ApplyResultAndClearDirty) {
			m_bStartupCacheDirty = false;
		} else {
			m_bStartupCacheDirty = true;
			m_nStartupCacheDirtyTick = ::GetTickCount64();
		}
	}
}

void CSharedFileList::DiscardStartupCacheSaveCompletion(void *pCompletionVoid)
{
	std::unique_ptr<StartupCacheSaveThreadCompletion> pCompletion(static_cast<StartupCacheSaveThreadCompletion*>(pCompletionVoid));
	if (pCompletion == NULL)
		return;
	std::unique_ptr<StartupCacheSaveResult> pResult(pCompletion->pResult);
	pCompletion->pResult = NULL;
	if (pResult != NULL && pCompletion->pOperation) {
		if (pResult->bWriteSucceeded)
			(void)LongPathSeams::DeleteFileIfExists(pCompletion->pOperation->strCachePath);
		if (pResult->bDuplicatePathWriteSucceeded)
			(void)LongPathSeams::DeleteFileIfExists(pCompletion->pOperation->strDuplicatePathCachePath);
		UpdateStartupCacheSaveOperationProgress(pCompletion->pOperation, StartupCacheSavePhase::Idle, 0, 0);
	}
}

void CSharedFileList::Save() const
{
	const CString &strFullPath(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + SHAREDFILES_FILE);
	CSafeBufferedFile file;
	if (LongPathSeams::OpenFile(file, strFullPath, CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite | CFile::typeBinary)) {
		try {
			// write Unicode byte order mark 0xFEFF
			static const WORD wBOM = u'\xFEFF';
			file.Write(&wBOM, sizeof(wBOM));

			for (POSITION pos = m_liSingleSharedFiles.GetHeadPosition(); pos != NULL;) {
				file.CStdioFile::WriteString(m_liSingleSharedFiles.GetNext(pos));
				file.Write(_T("\r\n"), 2 * sizeof(TCHAR));
			}
			for (POSITION pos = m_liSingleExcludedFiles.GetHeadPosition(); pos != NULL;) {
				file.CStdioFile::WriteString(_T('-') + m_liSingleExcludedFiles.GetNext(pos)); // a '-' prefix means excluded
				file.Write(_T("\r\n"), 2 * sizeof(TCHAR));
			}
			CommitAndClose(file);
		} catch (CFileException *ex) {
			DebugLogError(_T("Failed to save %s%s"), (LPCTSTR)strFullPath, (LPCTSTR)CExceptionStrDash(*ex));
			ex->Delete();
		}
	} else
		DebugLogError(_T("Failed to save %s"), (LPCTSTR)strFullPath);
}

void CSharedFileList::LoadSingleSharedFilesList()
{
	const CString &strFullPath(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + SHAREDFILES_FILE);
	bool bIsUnicodeFile = IsUnicodeFile(strFullPath); // check for BOM
	CSafeBufferedFile sdirfile;
	if (LongPathSeams::OpenFile(sdirfile, strFullPath, CFile::modeRead | CFile::shareDenyWrite | (bIsUnicodeFile ? CFile::typeBinary : 0))) {
		try {
			if (bIsUnicodeFile)
				sdirfile.Seek(sizeof(WORD), CFile::current); // skip BOM

			CString toadd;
			while (sdirfile.CStdioFile::ReadString(toadd)) {
				toadd.Trim(_T(" \t\r\n")); // need to trim '\r' in binary mode
				if (toadd.IsEmpty())
					continue;

				bool bExclude = (toadd[0] == _T('-')); // a '-' prefix means excluded
				if (bExclude)
					toadd.Delete(0, 1);
				toadd = NormalizeSharedFilePath(toadd);

				// Skip non-existing directories on fixed disks only
				if (DirAccsess(toadd))
					if (bExclude)
						ExcludeFile(toadd);
					else
						AddSingleSharedFile(toadd, true);
			}
			sdirfile.Close();
		} catch (CFileException *ex) {
			DebugLogError(_T("Failed to load %s%s"), (LPCTSTR)strFullPath, (LPCTSTR)CExceptionStrDash(*ex));
			ex->Delete();
		}
	} else
		DebugLogError(_T("Failed to load %s"), (LPCTSTR)strFullPath);
}

bool CSharedFileList::AddSingleSharedDirectory(const CString &rstrFilePath, bool bNoUpdate)
{
	const CString strCanonicalDirectory(NormalizeSharedDirectoryPath(rstrFilePath));
	// check if we share this dir already or are not allowed to
	if (ShouldBeShared(strCanonicalDirectory, NULL, false) || !thePrefs.IsShareableDirectory(strCanonicalDirectory))
		return false;

	// add the new directory as shared, GUI update to be done by the caller
	if (!thePrefs.AddSharedDirectoryIfAbsent(strCanonicalDirectory))
		return false;
	MarkStartupCacheDirty();
	if (!bNoUpdate) {
		AddFilesFromDirectory(strCanonicalDirectory);
		HashNextFile();
	}
	return true;
}

CString CSharedFileList::GetPseudoDirName(const CString &strDirectoryName)
{
	// Those pseudo names are sent to other clients when requesting shared files instead of
	// the full directory names to avoid giving away too much information about our local
	// file structure, which might be sensitive data in some cases.
	// But we still want to use a descriptive name so the information of files sorted by directories is not lost
	// In general we use only the name of the directory, shared subdirs keep the path up to
	// the highest shared dir. This way we never reveal the name of any indirectly shared directory.
	// Then we make sure it's unique.
	if (!ShouldBeShared(strDirectoryName, NULL, false)) {
		ASSERT(0);
		return CString();
	}
	// does the name already exist?
	CString strTmpPseudo, strTmpPath;
	const CString strDirectoryKey(MakeSharedDirectoryLookupKey(strDirectoryName));
	for (POSITION pos = m_mapPseudoDirNames.GetStartPosition(); pos != NULL;) {
		m_mapPseudoDirNames.GetNextAssoc(pos, strTmpPseudo, strTmpPath);
		if (MakeSharedDirectoryLookupKey(strTmpPath) == strDirectoryKey)
			return CString();	// not sending the same directory again
	}

	// create a new Pseudoname
	CString strDirectoryTmp(PathHelpers::TrimTrailingSeparatorForLeaf(strDirectoryName));

	CString strPseudoName;
	int iPos;
	while ((iPos = strDirectoryTmp.ReverseFind(_T('\\'))) >= 0) {
		strPseudoName = strDirectoryTmp.Right(strDirectoryTmp.GetLength() - iPos) + strPseudoName;
		strDirectoryTmp.Truncate(iPos);
		if (!ShouldBeShared(strDirectoryTmp, NULL, false))
			break;
	}
	if (strPseudoName.IsEmpty()) {
		// must be a root directory
		ASSERT(strDirectoryTmp.GetLength() == 2);
		strPseudoName = strDirectoryTmp;
	} else {
		// remove first backslash
		ASSERT(strPseudoName[0] == _T('\\'));
		strPseudoName.Delete(0, 1);
	}
	// we have the name, make sure it is unique
	if (m_mapPseudoDirNames.PLookup(strPseudoName)) {
		CString strUnique;
		for (iPos = 2; ; ++iPos) {
			strUnique.Format(_T("%s_%i"), (LPCTSTR)strPseudoName, iPos);
			if (!m_mapPseudoDirNames.PLookup(strUnique)) {
				strPseudoName = strUnique;
				break;
			}
			if (iPos > 200) {
				// wth?
				ASSERT(0);
				return CString();
			}
		}
	}

	DebugLog(_T("Using Pseudoname %s for directory %s"), (LPCTSTR)strPseudoName, (LPCTSTR)strDirectoryName);
	m_mapPseudoDirNames[strPseudoName] = strDirectoryName;
	return strPseudoName;
}

CString CSharedFileList::GetDirNameByPseudo(const CString &strPseudoName) const
{
	CString strResult;
	m_mapPseudoDirNames.Lookup(strPseudoName, strResult);
	return strResult;
}

bool CSharedFileList::GetPopularityRank(const CKnownFile *pFile, uint32 &rnOutSession, uint32 &rnOutTotal) const
{
	if (GetFileByIdentifier(pFile->GetFileIdentifierC()) == NULL) {
		rnOutSession = 0;
		rnOutTotal = 0;
		ASSERT(0);
		return false;
	}
	UINT uAllTimeReq = pFile->statistic.GetAllTimeRequests();
	UINT uReq = pFile->statistic.GetRequests();

	// we start at rank #1, not 0
	rnOutSession = 1;
	rnOutTotal = 1;
	// cycle all files, each file which has more requests than the given file lowers the rank
	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair))
		if (pair->value != pFile) {
			rnOutTotal += static_cast<uint32>(pair->value->statistic.GetAllTimeRequests() > uAllTimeReq);
			rnOutSession += static_cast<uint32>(pair->value->statistic.GetRequests() > uReq);
		}

	return true;
}
