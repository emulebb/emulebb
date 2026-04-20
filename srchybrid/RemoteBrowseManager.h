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

#include <map>
#include <memory>
#include <vector>

class CSearchFile;
class CUpDownClient;

/**
 * @brief Schedules, caches, and replays remote shared-file browse sessions.
 */
class CRemoteBrowseManager
{
public:
	enum class EBrowseOutcome : uint8
	{
		None = 0,
		Queued,
		InProgress,
		Success,
		Failed,
		Denied,
		Unsupported,
		Corrupt
	};

	struct Snapshot
	{
		CString strKey;
		CString strDisplayName;
		CString strIdentity;
		CString strStatus;
		CString strOutcome;
		time_t tLastAttempt = 0;
		time_t tLastSuccess = 0;
		uint64 ullCachedBytes = 0;
		uint64 ullCachedTotalSize = 0;
		UINT uCachedFileCount = 0;
		bool bQueued = false;
		bool bInProgress = false;
		bool bHasLiveClient = false;
		bool bCanRefresh = false;
	};

	CRemoteBrowseManager();
	~CRemoteBrowseManager();

	/** Drives queueing, timeout handling, and opportunistic auto-browse launches. */
	void Process();
	void OnClientRemoved(const CUpDownClient *pClient);
	void HandleBrowseTransportFailure(const CUpDownClient *pClient);
	void HandleBrowseDenied(const CUpDownClient *pClient);
	bool HandleSharedFileListPacket(CUpDownClient &client, const uchar *pPacket, uint32 nSize, LPCTSTR pszDirectory);
	bool ShouldRequestDirectory(const CUpDownClient &client, const CString &strDirectory) const;
	void NotifyDirectoryEnumerationComplete(const CUpDownClient &client);

	void StartManualBrowse(CUpDownClient *pClient);
	void OpenCachedOnlyByClient(const CUpDownClient *pClient);
	void OpenCachedOnlyByKey(const CString &strKey);
	bool CanRefreshKey(const CString &strKey) const;
	void RefreshKey(const CString &strKey);

	void EnsureCacheIndexLoaded();
	/** Collects the current cached-client rows for the Transfer window. */
	void GetSnapshots(std::vector<Snapshot> &rSnapshots);
	UINT GetCachedClientCount();
	static CString FormatTimeForList(time_t tValue);

private:
	enum class ESessionKind : uint8
	{
		Auto = 0,
		Manual
	};

	struct Entry
	{
		CString strKey;
		CString strCachePath;
		CString strDisplayName;
		CString strIdentity;
		CString strLastLiveUserName;
		EBrowseOutcome eOutcome = EBrowseOutcome::None;
		time_t tLastAttempt = 0;
		time_t tLastSuccess = 0;
		time_t tLastDenied = 0;
		uint64 ullCachedBytes = 0;
		uint64 ullCachedTotalSize = 0;
		UINT uCachedFileCount = 0;
		bool bPermanentSkip = false;
		bool bQueued = false;
		bool bInProgress = false;
		bool bCacheIndexed = false;
		bool bCacheCorrupt = false;
		bool bHasAnyCache = false;
		bool bDirectoryEnumerationSeen = false;
		CUpDownClient *pLiveClient = NULL;
		ESessionKind eSessionKind = ESessionKind::Auto;
		ULONGLONG ullQueueAfterTick = 0;
		ULONGLONG ullSessionStartTick = 0;
		ULONGLONG ullLastProgressTick = 0;
		uint32 dwVisibleSearchID = 0;
		uint32 dwReplaceSearchID = 0;
		std::map<CString, std::unique_ptr<CSearchFile>> activeFiles;
	};

	struct CacheHeader
	{
		CString strDisplayName;
		CString strIdentity;
		time_t tLastAttempt = 0;
		time_t tLastSuccess = 0;
		time_t tLastDenied = 0;
		uint64 ullCachedTotalSize = 0;
		UINT uCachedFileCount = 0;
		EBrowseOutcome eOutcome = EBrowseOutcome::None;
		bool bPermanentSkip = false;
	};

	Entry& EnsureEntryForClient(const CUpDownClient &client);
	Entry& EnsureEntryForKey(const CString &strKey);
	Entry* FindEntryByClient(const CUpDownClient &client);
	const Entry* FindEntryByClient(const CUpDownClient &client) const;
	Entry* FindEntryByKey(const CString &strKey);
	const Entry* FindEntryByKey(const CString &strKey) const;

	void RefreshLiveClientBindings();
	void QueueEligibleAutoBrowses(ULONGLONG ullNowTick, time_t tNow);
	void StartDueAutoBrowses(ULONGLONG ullNowTick);
	bool StartBrowseSession(Entry &entry, ESessionKind eKind, uint32 dwReplaceSearchID = 0);
	void AbortSession(Entry &entry, EBrowseOutcome eOutcome, LPCTSTR pszReason, bool bKeepPermanentSkip = false);
	void CompleteSession(Entry &entry);
	void MergeSessionIntoCache(Entry &entry, std::map<CString, std::unique_ptr<CSearchFile>> &rMergedFiles) const;
	bool LoadCacheFiles(const Entry &entry, uint32 dwSearchID, std::vector<std::unique_ptr<CSearchFile>> &rFiles) const;
	bool LoadCacheHeader(const CString &strPath, CacheHeader &rHeader, uint64 &rFileBytes) const;
	bool PersistCache(Entry &entry, const std::map<CString, std::unique_ptr<CSearchFile>> &rFiles);
	bool OpenCachedInternal(Entry &entry, uint32 dwReplaceSearchID);
	void NotifyCachedClientsChanged() const;
	void UpdateTransferCount() const;
	void MarkCacheCorrupt(Entry &entry);

	static CString BuildCacheKeyForClient(const CUpDownClient &client);
	static CString BuildEndpointCacheKey(const CUpDownClient &client);
	static CString BuildHashCacheKey(const uchar *pHash);
	static CString BuildCacheFilePath(const CString &strKey);
	static CString BuildIdentityLabel(const CUpDownClient &client);
	static CString BuildResultKey(const CSearchFile &file);
	static CString BuildStatusText(const Entry &entry);
	static CString BuildOutcomeText(EBrowseOutcome eOutcome);
	static time_t GetCurrentTime();
	static int GetDirectoryDepth(LPCTSTR pszDirectory);

	void ScanCacheDirectory();

	std::map<CString, Entry> m_entries;
	bool m_bCacheIndexLoaded;
	ULONGLONG m_ullStartupSettleUntil;
	ULONGLONG m_ullNextSweepTick;
};
