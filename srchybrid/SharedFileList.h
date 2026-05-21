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
#include <deque>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include "SharedDuplicatePathCachePolicy.h"
#include "SharedStartupCachePolicy.h"
#include "MapKey.h"
#include "FileIdentifier.h"

#ifndef EMULE_COMPILED_STARTUP_PROFILING
	#if defined(_DEBUG) || defined(EMULE_ENABLE_STARTUP_PROFILING)
	#define EMULE_COMPILED_STARTUP_PROFILING 1
	#else
	#define EMULE_COMPILED_STARTUP_PROFILING 0
	#endif
#endif

class CKnownFileList;
class CServerConnect;
class CPartFile;
class CKnownFile;
class CPublishKeywordList;
class CSafeMemFile;
class CServer;
class CCollection;
class CSharedFileList;
class CSharedFileHashThread;
typedef CMap<CCKey, const CCKey&, CKnownFile*, CKnownFile*> CKnownFilesMap;

struct UnknownFile_Struct
{
	CString strName;
	CString strDirectory;
	CString strSharedDirectory;
#if EMULE_COMPILED_STARTUP_PROFILING
	ULONGLONG ullQueuedTimestampUs = 0;
#endif
};

/**
 * @brief Owns one shared-file hash completion posted from the worker thread to the UI thread.
 */
struct CSharedFileHashResult
{
	CSharedFileList *pOwner = NULL;
	CKnownFile *pKnownFile = NULL;
	bool bSuccess = false;
	CString strName;
	CString strDirectory;
	CString strSharedDirectory;
	CString strFilePathKey;
#if EMULE_COMPILED_STARTUP_PROFILING
	ULONGLONG ullQueuedTimestampUs = 0;
#endif
};

class CSharedFileList
{
	friend class CSharedFilesCtrl;
	friend class CClientReqSocket;
	friend class CSharedFileHashThread;

public:
	/**
	 * @brief Exposes the current background startup-cache saver phase for shutdown UI and diagnostics.
	 */
	enum class StartupCacheSavePhase : uint8
	{
		Idle,
		BuildingRecords,
		WritingFile,
		ApplyingResult
	};

	/**
	 * @brief Reports the latest background startup-cache save progress snapshot.
	 */
	struct StartupCacheSaveProgress
	{
		StartupCacheSavePhase ePhase = StartupCacheSavePhase::Idle;
		ULONGLONG uCompletedDirectories = 0;
		ULONGLONG uTotalDirectories = 0;
		bool bRunning = false;
		bool bDirty = false;
		bool bWaitingForFollowUp = false;
	};

	/**
	 * @brief Aggregates one shared-directory scan pass into stable machine-readable counters.
	 */
	struct StartupScanStats
	{
		ULONGLONG uRequestedDirectories = 0;
		ULONGLONG uDedupedDirectories = 0;
		ULONGLONG uDuplicateDirectories = 0;
		ULONGLONG uDirectoriesFromCache = 0;
		ULONGLONG uDirectoriesRescanned = 0;
		ULONGLONG uInaccessibleDirectories = 0;
		ULONGLONG uDirectoriesOver248Chars = 0;
		ULONGLONG uPathsOver260Chars = 0;
		ULONGLONG uKnownFilesAccepted = 0;
		ULONGLONG uDuplicatePathsReused = 0;
		ULONGLONG uFilesQueuedForHash = 0;
		ULONGLONG uFilesIgnored = 0;
	};

	/**
	 * @brief Captures the shared-files startup-cache load, scan, and save state for diagnostics.
	 */
	struct StartupCacheStatus
	{
		bool bCacheFilePresent = false;
		bool bCacheLoaded = false;
		bool bCacheRejected = false;
		bool bCacheRemoved = false;
		CStringA strCacheRejectCode;
		bool bDuplicatePathCacheLoaded = false;
		bool bDuplicatePathCacheRejected = false;
		bool bDuplicatePathCacheRemoved = false;
		CStringA strDuplicatePathCacheRejectCode;
		ULONGLONG uRecordsLoaded = 0;
		ULONGLONG uVolumesLoaded = 0;
		ULONGLONG uDuplicatePathRecordsLoaded = 0;
		StartupScanStats scanStats;
		StartupCacheSaveProgress saveProgress;
		INT_PTR iHashingCount = 0;
		bool bDeferredHashingActive = false;
		bool bInterruptedHashingInvalidatedCache = false;
		bool bReady = true;
	};

	explicit CSharedFileList(CServerConnect *in_server);
	~CSharedFileList();
	CSharedFileList(const CSharedFileList&) = delete;
	CSharedFileList& operator=(const CSharedFileList&) = delete;

	void	SendListToServer();
	void	Reload();
	void	Save() const;
	void	Process();
	void	Publish();
	void	RebuildMetaData();
	void	DeletePartFileInstances() const;
	void	PublishNextTurn()						{ m_lastPublishED2KFlag = true; }
	void	ClearED2KPublishInfo();
	void	ClearKadSourcePublishInfo();

	static void	CreateOfferedFilePacket(CKnownFile *cur_file, CSafeMemFile &files, const CServer *pServer, const CUpDownClient *pClient = NULL);

	bool	SafeAddKFile(CKnownFile *toadd, bool bOnlyAdd = false);
	void	RepublishFile(CKnownFile *pFile);
	void	SetOutputCtrl(CSharedFilesCtrl *in_ctrl);
	/**
	 * @brief Starts deferred shared-file hashing after startup UI initialization finishes.
	 */
	void	StartDeferredHashing();
	/**
	 * @brief Reports whether the initial startup-only deferred shared hash queue is still draining.
	 */
	bool	IsStartupDeferredHashingActive() const;
	/**
	 * @brief Advances or starts shutdown of the shared-file hash worker and reports whether it has stopped.
	 */
	bool	ShutdownSharedHashWorkerStep(DWORD dwWaitMilliseconds);
	/**
	 * @brief Replays retained hash completions on the UI thread in bounded batches.
	 */
	void	DrainDeferredSharedHashResults();
	/**
	 * @brief Reports whether shared-hash shutdown has been signaled and queued completions should be ignored.
	 */
	bool	IsSharedHashWorkerShuttingDown() const;
	/**
	 * @brief Reports whether queued, active, or posted shared-file hash work still exists.
	 */
	bool	HasSharedHashingWork() const				{ return GetHashingCount() > 0; }
	/**
	 * @brief Copies the active shared-file hash leaf and full path for shutdown UI.
	 */
	bool	GetActiveSharedHashFile(CString &rstrLeafName, CString &rstrFullPath) const;
	/**
	 * @brief Drops persisted startup-cache sidecars after shutdown interrupted shared hashing.
	 */
	void	PurgeInterruptedHashStartupCaches();
	/**
	 * @brief Starts one background startup-cache save when the cache is dirty and no worker is active.
	 */
	bool	RequestStartupCacheSave(bool bImmediate = false);
	/**
	 * @brief Reports whether startup-cache persistence still has dirty or in-flight work left.
	 */
	bool	HasPendingStartupCacheSaveWork() const;
	/**
	 * @brief Reports whether the background startup-cache saver is currently running.
	 */
	bool	IsStartupCacheSaveRunning() const;
	/**
	 * @brief Copies the latest startup-cache save progress snapshot for shutdown UI polling.
	 */
	void	GetStartupCacheSaveProgress(StartupCacheSaveProgress &rProgress) const;
	/**
	 * @brief Copies the latest shared-files startup-cache diagnostics for REST and operator status.
	 */
	void	GetStartupCacheStatus(StartupCacheStatus &rStatus) const;
	/**
	 * @brief Applies one worker-produced startup-cache save result on the UI thread.
	 */
	void	HandleStartupCacheSaveCompletion(void *pResult);
	/**
	 * @brief Drops a worker-produced startup-cache save completion without applying it to an owner.
	 */
	static void	DiscardStartupCacheSaveCompletion(void *pCompletion);
	/**
	 * @brief Stops waiting for startup-cache persistence during shutdown and reports whether this object must stay alive.
	 */
	bool	AbandonStartupCacheSaveForShutdown();
	bool	RemoveFile(CKnownFile *pFile, bool bDeleted = false);	// removes a specific shared file from the list
	void	UpdateFile(const CKnownFile *toupdate);
	void	AddFileFromNewlyCreatedCollection(const CString &rstrFilePath)	{ CheckAndAddSingleFile(rstrFilePath); }

	// GUI is not initially updated
	bool	AddSingleSharedFile(const CString &rstrFilePath, bool bNoUpdate = false); // includes updating sharing preferences, calls CheckAndAddSingleSharedFile afterwards
	bool	AddSingleSharedDirectory(const CString &rstrFilePath, bool bNoUpdate = false);
	bool	ExcludeFile(const CString &strFilePath);	// excludes a specific file from being shared and removes it from the list if it exists

	void	AddKeywords(CKnownFile *pFile);
	void	RemoveKeywords(CKnownFile *pFile);

	void	CopySharedFileMap(CKnownFilesMap &Files_Map);
	/**
	 * @brief Finds one shared file by canonical full-path key without copying the shared-file map.
	 */
	CKnownFile*	GetFileByPath(const CString &strFilePath);

	CKnownFile*	GetFileByID(const uchar *hash) const;
	CKnownFile*	GetFileByIdentifier(const CFileIdentifierBase &rFileIdent, bool bStrict = false) const;
	CKnownFile*	GetFileByIndex(INT_PTR index) const; // slow
	CKnownFile*	GetFileNext(POSITION &pos) const;
	CKnownFile*	GetFileByAICH(const CAICHHash &rHash) const; // slow

	bool	IsFilePtrInList(const CKnownFile *file) const;
	bool	IsUnsharedFile(const uchar *auFileHash) const;
	bool	ShouldBeShared(const CString &sDirPath, LPCTSTR const pFilePath, bool bMustBeShared) const;
	bool	ContainsSingleSharedFiles(const CString &strDirectory) const; // includes subdirs
	CString	GetPseudoDirName(const CString &strDirectoryName);
	CString	GetDirNameByPseudo(const CString &strPseudoName) const;

	uint64	GetDatasize(uint64 &pbytesLargest) const;
	INT_PTR	GetCount()								{ return m_Files_map.GetCount(); }
	INT_PTR	GetHashingCount() const;
	bool	ProbablyHaveSingleSharedFiles() const	{ return bHaveSingleSharedFiles && !m_liSingleSharedFiles.IsEmpty(); } // might not be always up-to-date, could give false "true"s, not a problem currently

	void	HashFailed(UnknownFile_Struct *hashed);	// SLUGFILLER: SafeHash
	void	FileHashingFinished(CKnownFile *file);
	void	HashFailed(CSharedFileHashResult *pResult);
	void	FileHashingFinished(CSharedFileHashResult *pResult);

	bool	GetPopularityRank(const CKnownFile *pFile, uint32 &rnOutSession, uint32 &rnOutTotal) const;

	CCriticalSection m_mutWriteList; // don't acquire other locks while having this one in the main thread or make sure deadlocks are impossible
	static uint8 GetRealPrio(uint8 in)				{ return (in < 4) ? in + 1 : 0; }
	void	ResetPseudoDirNames()					{ m_mapPseudoDirNames.RemoveAll(); }

protected:
	bool	AddFile(CKnownFile *pFile);
	void	AddFilesFromDirectory(const CString &rstrDirectory);
	void	FindSharedFiles(bool bAllowStartupCache = true);
	bool	AddKnownSharedFile(CKnownFile *pFile, const CString &strFoundDirectory, const CString &strFoundFilePath);

	void	HashNextFile();
	bool	IsHashing(const CString &rstrDirectory, const CString &rstrName);
	void	RemoveFromHashing(const CKnownFile *hashed);
	void	LoadSingleSharedFilesList();

	void	CheckAndAddSingleFile(const CString &strDirectory, const WIN32_FIND_DATA &findData);
	void	CheckAndAddSingleFileFromNormalizedDirectory(const CString &strFoundDirectory, const WIN32_FIND_DATA &findData);
	bool	CheckAndAddSingleFile(const CString &rstrFilePath); // add specific files without editing sharing preferences

private:
	using SharedStartupCacheRecordMap = std::unordered_map<std::wstring, SharedStartupCachePolicy::DirectoryRecord>;
	using SharedStartupCacheVolumeRecordMap = std::unordered_map<std::wstring, SharedStartupCachePolicy::VolumeRecord>;
	using SharedDuplicatePathRecordMap = std::unordered_map<std::wstring, SharedDuplicatePathCachePolicy::PathRecord>;

	/**
	 * @brief Describes one queued shared-file hash job consumed by the app-lifetime worker thread.
	 */
	struct SharedHashJob
	{
		CString strName;
		CString strDirectory;
		CString strSharedDirectory;
		CString strFilePathKey;
#if EMULE_COMPILED_STARTUP_PROFILING
		ULONGLONG ullQueuedTimestampUs = 0;
#endif
	};

	/**
	 * @brief Captures the current startup-validation state for one shared directory.
	 */
	struct DirectoryStartupState
	{
		LongPathSeams::FileSystemObjectIdentity identity = {};
		bool bHasIdentity = false;
		LONGLONG utcDirectoryDate = -1;
		bool bHasTrustedNtfsJournalState = false;
		SharedStartupCachePolicy::VolumeRecord volumeRecord = {};
		LongPathSeams::UsnFileReference directoryFileReference = {};
	};

	/**
	 * @brief Memoizes the journal-delta decision for one cached NTFS volume during startup.
	 */
	struct StartupCacheVolumeValidationState
	{
		bool bInitialized = false;
		bool bRescanAllDirectories = false;
		std::unordered_set<LongPathSeams::UsnFileReference, LongPathSeams::UsnFileReferenceHasher> changedDirectoryFileReferences;
	};

	/**
	 * @brief Captures one shared directory block for worker-side startup-cache persistence.
	 */
	struct StartupCacheSaveDirectorySnapshot
	{
		CString strDirectoryPath;
		DirectoryStartupState directoryState = {};
		bool bHasDirectoryState = false;
		std::vector<SharedStartupCachePolicy::FileRecord> files;
		bool bHasPendingHash = false;
	};

	/**
	 * @brief Owns one immutable worker-side startup-cache persistence request.
	 */
	struct StartupCacheSaveSnapshot
	{
		CString strCachePath;
		CString strDuplicatePathCachePath;
		std::vector<StartupCacheSaveDirectorySnapshot> directories;
		std::vector<SharedDuplicatePathCachePolicy::PathRecord> duplicatePathRecords;
	};

	/**
	 * @brief Owns one completed worker-side startup-cache save result until the UI thread applies it.
	 */
	struct StartupCacheSaveResult
	{
		bool bWriteSucceeded = false;
		bool bDuplicatePathWriteSucceeded = false;
		ULONGLONG ullCompletedTick = 0;
		SharedStartupCacheRecordMap records;
		SharedStartupCacheVolumeRecordMap volumeRecords;
		SharedDuplicatePathRecordMap duplicatePathRecords;
	};

	/**
	 * @brief Holds owner-independent progress for one background startup-cache save.
	 */
	struct StartupCacheSaveOperation
	{
		mutable CCriticalSection mutProgress;
		StartupCacheSavePhase ePhase = StartupCacheSavePhase::Idle;
		ULONGLONG uCompletedDirectories = 0;
		ULONGLONG uTotalDirectories = 0;
		CString strCachePath;
		CString strDuplicatePathCachePath;
	};

	/**
	 * @brief Carries one immutable snapshot into the background save worker.
	 */
	struct StartupCacheSaveThreadRequest
	{
		HWND hNotifyWnd = NULL;
		std::shared_ptr<StartupCacheSaveOperation> pOperation;
		StartupCacheSaveSnapshot snapshot;
	};

	/**
	 * @brief Transfers one owner-independent worker completion to the UI thread.
	 */
	struct StartupCacheSaveThreadCompletion
	{
		std::shared_ptr<StartupCacheSaveOperation> pOperation;
		StartupCacheSaveResult *pResult = NULL;
	};

	void	AddDirectory(const CString &strDir, CStringList &dirlist, std::unordered_set<std::wstring> &rAddedDirectoryKeys, bool bAllowStartupCache);
	void	CollectSharedDirectories(CStringList &dirlist) const;
	bool	TryLoadStartupCache();
	void	NoteStartupCacheLoadResult(bool bPresent, bool bLoaded, bool bRejected, bool bRemoved, LPCSTR pszRejectCode, ULONGLONG uRecordsLoaded, ULONGLONG uVolumesLoaded);
	void	NoteDuplicatePathCacheLoadResult(bool bLoaded, bool bRejected, bool bRemoved, LPCSTR pszRejectCode, ULONGLONG uRecordsLoaded);
	void	MarkStartupCacheDirty();
	bool	TryRehydrateSharedDirectoryFromCache(const CString &strDirectory);
	/**
	 * @brief Builds one startup-cache directory block and records any shared NTFS volume guard it depends on.
	 */
	bool	BuildStartupCacheRecord(const CString &strDirectory, SharedStartupCachePolicy::DirectoryRecord &rRecord, SharedStartupCacheVolumeRecordMap &rVolumeRecords) const;
	bool	HasPendingHashForDirectory(const CString &strDirectory) const;
	/**
	 * @brief Resolves the current directory state used by both generic and NTFS startup-cache validation.
	 */
	static bool	GetDirectoryStartupState(const CString &strDirectory, DirectoryStartupState &rState);
	static bool	GetFileStartupState(const CString &strFilePath, LONGLONG &rUtcFileDate, ULONGLONG &rullFileSize);
	/**
	 * @brief Computes the one-scan-per-volume NTFS journal verdict for cached directories on the same local volume.
	 */
	bool	EnsureStartupCacheVolumeValidation(const CString &strDirectory, const SharedStartupCachePolicy::DirectoryRecord &rRecord);
	/**
	 * @brief Collects every cached directory reference guarded by one shared NTFS volume record.
	 */
	void	CollectTrackedStartupCacheDirectoryRefs(const SharedStartupCachePolicy::VolumeRecord &rVolumeRecord, std::unordered_set<LongPathSeams::UsnFileReference, LongPathSeams::UsnFileReferenceHasher> &rTrackedDirectoryRefs) const;
	static CString GetStartupCachePath();
	/**
	 * @brief Returns the config-directory path for the shared duplicate-path sidecar.
	 */
	static CString GetDuplicatePathCachePath();
	static std::wstring MakeStartupCacheKey(const CString &strDirectory);
	static std::wstring MakeStartupCacheVolumeKey(const CString &strVolumeKey);
	/**
	 * @brief Canonicalizes one duplicate shared-file path into the sidecar key format.
	 */
	static std::wstring MakeDuplicatePathCacheKey(const CString &strFilePath);
	static bool ReadStartupCacheString(CSafeBufferedFile &file, CString &rValue);
	static void WriteStartupCacheString(CSafeBufferedFile &file, const CString &strValue);
	static UINT AFX_CDECL StartupCacheSaveThreadProc(LPVOID pParam);
	bool	CaptureStartupCacheSaveSnapshot(StartupCacheSaveSnapshot &rSnapshot) const;
	/**
	 * @brief Captures the currently valid duplicate shared-path records for sidecar persistence.
	 */
	bool	CaptureDuplicatePathCacheSnapshot(std::vector<SharedDuplicatePathCachePolicy::PathRecord> &rSnapshot) const;
	static bool	BuildStartupCacheRecordFromSnapshot(const StartupCacheSaveDirectorySnapshot &rDirectory, SharedStartupCachePolicy::DirectoryRecord &rRecord, SharedStartupCacheVolumeRecordMap &rVolumeRecords);
	static void	RunStartupCacheSaveWorker(const StartupCacheSaveSnapshot &rSnapshot, const std::shared_ptr<StartupCacheSaveOperation> &pOperation, StartupCacheSaveResult &rResult);
	static bool	WriteStartupCacheFile(const CString &strFullPath, const SharedStartupCacheVolumeRecordMap &rVolumeRecords, const std::vector<SharedStartupCachePolicy::DirectoryRecord> &rRecords);
	/**
	 * @brief Writes the duplicate shared-path sidecar atomically.
	 */
	static bool	WriteDuplicatePathCacheFile(const CString &strFullPath, const std::vector<SharedDuplicatePathCachePolicy::PathRecord> &rRecords);
	void	UpdateStartupCacheSaveProgress(StartupCacheSavePhase ePhase, ULONGLONG uCompletedDirectories, ULONGLONG uTotalDirectories);
	static void	UpdateStartupCacheSaveOperationProgress(const std::shared_ptr<StartupCacheSaveOperation> &pOperation, StartupCacheSavePhase ePhase, ULONGLONG uCompletedDirectories, ULONGLONG uTotalDirectories);
	/**
	 * @brief Loads the duplicate shared-path sidecar if it is present and well-formed.
	 */
	bool	TryLoadDuplicatePathCache();
	/**
	 * @brief Remembers one duplicate shared-file path for future startup hash skipping.
	 */
	void	RememberDuplicateSharedPath(const CString &strFilePath, const uchar *pCanonicalFileHash, LONGLONG utcFileDate, ULONGLONG ullFileSize);
	/**
	 * @brief Reports duplicate shared files without flooding the visible UI log during large startup scans.
	 */
	void	ReportDuplicateSharedFileWarning(const CString &strCanonicalPath, const CString &strDuplicatePath);
	/**
	 * @brief Reuses one remembered duplicate shared-file path during the startup scan when its canonical MD4 is still known.
	 */
	bool	TryReuseRememberedDuplicateSharedPath(const CString &strFilePath, LONGLONG utcFileDate, ULONGLONG ullFileSize);
	/**
	 * @brief Creates the app-lifetime shared-file hash worker thread if it is not already running.
	 */
	bool	EnsureSharedHashWorkerStarted();
	/**
	 * @brief Queues one shared-file hash job and wakes the worker when hashing is enabled.
	 */
	void	QueueSharedFileForHash(const CString &strDirectory, const CString &strName, const CString &strSharedDirectory);
	/**
	 * @brief Wakes the shared-file hash worker after new work or a state transition.
	 */
	void	SignalSharedHashWorker();
	/**
	 * @brief Waits until one job can be consumed by the shared-file hash worker.
	 */
	bool	WaitForSharedHashJob(SharedHashJob &rJob);
	/**
	 * @brief Runs one shared-file hash job on the worker thread.
	 */
	void	RunSharedHashJob(const SharedHashJob &rJob);
	/**
	 * @brief Tracks one worker-posted hash completion until the UI thread consumes it.
	 */
	void	MoveActiveSharedHashToPendingCompletion(const SharedHashJob &rJob);
	/**
	 * @brief Clears one worker-posted hash completion after the UI thread consumed or discarded it.
	 */
	void	CompleteSharedHashCompletion(const CString &strFilePathKey);
	/**
	 * @brief Retains one hash completion for bounded batched UI-thread delivery.
	 */
	bool	QueueDeferredSharedHashResult(CSharedFileHashResult *pResult);
	/**
	 * @brief Pops the next retained hash completion for UI-thread delivery.
	 */
	bool	TakeDeferredSharedHashResult(CSharedFileHashResult *&rpResult);
	/**
	 * @brief Posts or re-posts the bounded UI-thread drain for queued shared hash completions.
	 */
	bool	PostSharedHashResultsDrain();
	/**
	 * @brief Handles shared hash queue drain side effects on the UI thread.
	 */
	void	OnSharedHashQueuePossiblyDrained();
	void	ResetStartupCacheSchedulingState(ULONGLONG ullNowTick);
	void	SetStartupDeferredHashingActiveFlag(bool bActive);
	void	NoteStartupHashingQueueDrained(ULONGLONG ullNowTick);
	bool	ShouldStartStartupCacheSaveNow(ULONGLONG ullNowTick) const;
	/**
	 * @brief Invalidates warm shared startup caches after shutdown discards queued or active hashing work.
	 */
	void	InvalidateStartupCachesAfterInterruptedHashing();
	/**
	 * @brief Reports whether the given file path is currently being hashed or awaiting completion.
	 */
	bool	IsSharedHashInFlight(const CString &strDirectory, const CString &strName) const;
	/**
	 * @brief Signals shutdown, discards queued jobs, and wakes the worker.
	 */
	void	SignalSharedHashWorkerShutdown();
	/**
	 * @brief Builds the canonical case-folded lookup key for one explicit single-file rule.
	 */
	static std::wstring MakeSingleFileRuleKey(const CString &strFilePath);
	bool	HasSingleSharedFileRule(const CString &strFilePath) const;
	bool	HasSingleExcludedFileRule(const CString &strFilePath) const;
	void	AddSingleSharedFileRuleKey(const CString &strFilePath);
	void	RemoveSingleSharedFileRuleKey(const CString &strFilePath);
	void	AddSingleExcludedFileRuleKey(const CString &strFilePath);
	void	RemoveSingleExcludedFileRuleKey(const CString &strFilePath);

	CKnownFilesMap m_Files_map;
	CMap<CSKey, const CSKey&, bool, bool>		 m_UnsharedFiles_map;
	CMapStringToString m_mapPseudoDirNames;
	CPublishKeywordList *m_keywords;
	CServerConnect	 *server;
	CSharedFilesCtrl *output;
	CStringList		 m_liSingleSharedFiles;
	CStringList		 m_liSingleExcludedFiles;
	std::unordered_set<std::wstring> m_singleSharedFileRuleKeys;
	std::unordered_set<std::wstring> m_singleExcludedFileRuleKeys;
	std::vector<std::wstring> m_sortedSingleSharedFileRuleKeys;
#if defined(_DEVBUILD)
	CString			m_strBetaFileName; //beta test file name
#endif

	INT_PTR	m_currFileSrc;
	INT_PTR	m_currFileNotes;
	time_t	m_lastPublishKadSrc;
	time_t	m_lastPublishKadNotes;
	ULONGLONG m_lastPublishED2K;
	bool	m_lastPublishED2KFlag;
	bool	bHaveSingleSharedFiles;
	bool	m_bStartupCacheDirty;
	bool	m_bStartupDuplicateReuseActive;
	bool	m_bStartupDeferredHashingActive;
	ULONGLONG m_nLastStartupCacheSave;
	ULONGLONG m_nStartupCacheDirtyTick;
	ULONGLONG m_uStartupHashCompletedFiles;
	ULONGLONG m_uStartupHashFailedFiles;
	ULONGLONG m_uDuplicateSharedFileWarningsShown;
	ULONGLONG m_uDuplicateSharedFileWarningsSuppressed;
	HANDLE m_hSharedHashQueueEvent;
	CSharedFileHashThread *m_pSharedHashThread;
	mutable CCriticalSection m_mutSharedHashQueue;
	std::deque<SharedHashJob> m_sharedHashQueue;
	std::deque<SharedHashJob> m_sharedHashPendingCompletions;
	std::deque<CSharedFileHashResult*> m_sharedHashDeferredResults;
	std::unordered_set<std::wstring> m_sharedHashInFlightKeys;
	SharedHashJob m_sharedHashActiveJob;
	bool m_bSharedHashDrainPosted;
	bool m_bSharedHashWorkerCanHash;
	bool m_bSharedHashWorkerExitRequested;
	bool m_bSharedHashActive;
	bool m_bSharedHashShutdownSignaled;
	bool	m_bDuplicateSharedFileWarningSuppressionLogged;
	bool	m_bStartupCacheInvalidatedByInterruptedHashing;
	bool	m_bStartupCacheSaveShutdownAbandoned;
	StartupScanStats m_startupScanStats;
	bool	m_bStartupCacheFilePresent;
	bool	m_bStartupCacheLoaded;
	bool	m_bStartupCacheRejected;
	bool	m_bStartupCacheRemoved;
	CStringA m_strStartupCacheRejectCode;
	bool	m_bDuplicatePathCacheLoaded;
	bool	m_bDuplicatePathCacheRejected;
	bool	m_bDuplicatePathCacheRemoved;
	CStringA m_strDuplicatePathCacheRejectCode;
	ULONGLONG m_uStartupCacheRecordsLoaded;
	ULONGLONG m_uStartupCacheVolumesLoaded;
	ULONGLONG m_uDuplicatePathCacheRecordsLoaded;
	SharedStartupCacheRecordMap m_startupCacheRecords;
	SharedStartupCacheVolumeRecordMap m_startupCacheVolumes;
	SharedDuplicatePathRecordMap m_duplicateSharedPathRecords;
	std::unordered_map<std::wstring, StartupCacheVolumeValidationState> m_startupCacheVolumeValidation;
	mutable CCriticalSection m_mutStartupCacheSave;
	bool	m_bStartupCacheSaveRunning;
	bool	m_bStartupCacheSaveRunAfterCurrent;
	std::shared_ptr<StartupCacheSaveOperation> m_pStartupCacheSaveOperation;
	StartupCacheSavePhase m_eStartupCacheSavePhase;
	ULONGLONG m_uStartupCacheSaveDirectoriesDone;
	ULONGLONG m_uStartupCacheSaveDirectoriesTotal;
};

class CAddFileThread : public CWinThread
{
	DECLARE_DYNCREATE(CAddFileThread)
protected:
	CAddFileThread();
public:
	virtual BOOL InitInstance();
	virtual int	Run();
	void	SetValues(CSharedFileList *pOwner, LPCTSTR directory, LPCTSTR filename, LPCTSTR strSharedDir, CPartFile *partfile = NULL);
private:
	CSharedFileList	*m_pOwner;
	CPartFile	*m_partfile;
	CString		m_strDirectory;
	CString		m_strFilename;
	CString		m_strSharedDir;
};

/**
 * @brief Creates a suspended part-file hash worker and binds it to the target file.
 */
CAddFileThread *CreateSuspendedPartFileHashThread(LPCTSTR pszDirectory, LPCTSTR pszFilename, CPartFile *pPartFile);

/**
 * @brief App-lifetime worker that serially drains shared-file hash jobs without per-file thread churn.
 */
class CSharedFileHashThread : public CWinThread
{
	DECLARE_DYNCREATE(CSharedFileHashThread)
protected:
	CSharedFileHashThread();
public:
	virtual BOOL InitInstance();
	virtual int	Run();
	void	SetOwner(CSharedFileList *pOwner)		{ m_pOwner = pOwner; }
private:
	CSharedFileList *m_pOwner;
};
