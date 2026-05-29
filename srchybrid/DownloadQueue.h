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
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#include "ring.h"

class CSafeMemFile;
class CSearchFile;
class CUpDownClient;
class CServer;
class CPartFile;
class CSharedFileList;
class CKnownFile;
class CED2KFileLink;
struct SUnresolvedHostname;

namespace Kademlia
{
	class CUInt128;
};

/**
 * @brief Resolves ed2k source hostnames away from the UI thread and drains results on the download queue.
 */
class CSourceHostnameResolver
{
public:
	CSourceHostnameResolver();
	~CSourceHostnameResolver();

	void AddToResolve(const uchar *fileid, LPCSTR pszHostname, uint16 port);
	void DrainResolved(class CDownloadQueue &downloadQueue);
	void Stop();

private:
	struct SharedState;

	struct HostnameResolveRequest
	{
		uchar fileid[MDX_DIGEST_SIZE];
		CStringA strHostname;
		uint16 port;
	};

	struct HostnameResolveResult
	{
		uchar fileid[MDX_DIGEST_SIZE];
		uint32 nIP;
		uint16 port;
		bool bLookupSucceeded;
		bool bHasIpv4Address;
	};

	static bool TryResolveHostnameIPv4(const CStringA &rstrHostname, uint32 &rnAddress);
	static void WorkerMain(std::shared_ptr<SharedState> pState);

	std::shared_ptr<SharedState> m_pState;
	std::thread m_worker;
};


class CDownloadQueue
{
	friend class CAddFileThread;
	friend class CServerSocket;

public:
	CDownloadQueue();
	~CDownloadQueue();

	void	Process();
	void	Init();

	// add/remove entries
	void	AddPartFilesToShare();
	void	DrainFileCompletionWorkersForShutdown();
	void	AddDownload(CPartFile *newfile, bool paused);
	void	AddSearchToDownload(CSearchFile *toadd, uint8 paused = 2, int cat = 0);
	void	AddSearchToDownload(const CString &link, uint8 paused = 2, int cat = 0);
	void	AddFileLinkToDownload(const CED2KFileLink &Link, int cat = 0, uint8 paused = 2);
	/**
	 * @brief Defers per-file protected disk-space rescans while several downloads are added as one user action.
	 */
	void	BeginBulkAddDownloads();
	/**
	 * @brief Ends a bulk-add section and runs one deferred disk-space check if files were queued.
	 */
	void	EndBulkAddDownloads();
	/**
	 * @brief Returns the next available numeric part-file slot for a temp directory.
	 */
	int		GetNextAvailablePartFileIndex(LPCTSTR pszTempDir) const;
	void	RemoveFile(CPartFile *toremove);
	void	DeleteAll();

	INT_PTR	GetFileCount() const							{ return filelist.GetCount(); }
	UINT	GetDownloadingFileCount() const;
	UINT	GetPausedFileCount() const;

	bool	IsFileExisting(const uchar *fileid, bool bLogWarnings = true) const;
	bool	IsPartFile(const CKnownFile *file) const;

	CPartFile* GetFileByID(const uchar *filehash) const;
	CPartFile* GetFileNext(POSITION &pos) const; //trivial iterator
	CPartFile* GetFileByKadFileSearchID(uint32 id) const;

	void	StartNextFileIfPrefs(int cat);
	void	StartNextFile(int cat = -1, bool force = false);

	void	RefilterAllComments();

	// sources
	CUpDownClient* GetDownloadClientByIP(uint32 dwIP);
	CUpDownClient* GetDownloadClientByIP_UDP(uint32 dwIP, uint16 nUDPPort, bool bIgnorePortOnUniqueIP, bool *pbMultipleIPs = NULL);
	bool	IsInList(const CUpDownClient *client) const;

	bool	CheckAndAddSource(CPartFile *sender, CUpDownClient *source);
	bool	CheckAndAddKnownSource(CPartFile *sender, CUpDownClient *source, bool bIgnoreGlobDeadList = false);
	bool	RemoveSource(CUpDownClient *toremove, bool bDoStatsUpdate = true);

	// statistics
	typedef struct
	{
		unsigned a[23];
	} SDownloadStats;
	void	GetDownloadSourcesStats(SDownloadStats &results);
	int		GetDownloadFilesStats(uint64 &rui64TotalFileSize, uint64 &rui64TotalLeftToTransfer, uint64 &rui64TotalAdditionalNeededSpace);
	uint32	GetDatarate() const								{ return m_datarate; }
	uint64	GetBufferedDownloadBytes() const					{ return m_uBufferedDownloadBytesSnapshot; }
	UINT	GetBufferedDownloadFileCount() const				{ return m_uBufferedDownloadFileCountSnapshot; }
	uint64	GetEffectiveFileBufferSizeBytes() const;

	void	AddUDPFileReasks()								{ ++m_nUDPFileReasks; }
	uint32	GetUDPFileReasks() const						{ return m_nUDPFileReasks; }
	void	AddFailedUDPFileReasks()						{ ++m_nFailedUDPFileReasks; }
	uint32	GetFailedUDPFileReasks() const					{ return m_nFailedUDPFileReasks; }

	// categories
	void	ResetCatParts(UINT cat);
	void	SetCatPrio(UINT cat, uint8 newprio);
	void	RemoveAutoPrioInCat(UINT cat, uint8 newprio); // ZZ:DownloadManager
	void	SetCatStatus(UINT cat, int newstatus);
	void	MoveCat(UINT from, UINT to);
	static void	SetAutoCat(CPartFile *newfile);

	// searching on local server
	void	SendLocalSrcRequest(CPartFile *sender);
	void	RemoveLocalServerRequest(CPartFile *pFile);
	void	ResetLocalServerRequests();

	// searching in Kad
	void	SetLastKademliaFileRequest()					{ m_lastkademliafilerequest = ::GetTickCount64(); }
	bool	DoKademliaFileRequest() const;
	void	KademliaSearchFile(uint32 nSearchID, const Kademlia::CUInt128 *pcontactID, const Kademlia::CUInt128 *pbuddyID, uint8 type, uint32 ip, uint16 tcp, uint16 udp, uint32 dwBuddyIP, uint16 dwBuddyPort, uint8 byCryptOptions);

	// searching on global servers
	void	StopUDPRequests();

	// check disk space
	void	SortByPriority();
	void	CheckDiskspace(bool bNotEnoughSpaceLeft = false);
	void	CheckDiskspaceTimed();
	/**
	 * @brief Returns whether any protected volume is currently below its effective disk-space threshold.
	 */
	bool	IsProtectedDiskSpaceBlocked() const;
	/**
	 * @brief Returns the effective required free bytes for the protected volume hosting the given path.
	 */
	ULONGLONG GetRequiredFreeDiskSpaceForPath(LPCTSTR pszPath) const;

	void	ExportPartMetFilesOverview() const;
	void	OnConnectionState(bool bConnected);

	/**
	 * @brief Returns the best valid temp directory for a new part file, or an empty string if no placement satisfies the protected-volume policy.
	 */
	CString	GetOptimalTempDir(UINT nCat, EMFileSize nFileSize);

	CServer	*cur_udpserver;

protected:
	bool	SendNextUDPPacket();
	void	ProcessLocalRequests();
	bool	IsMaxFilesPerUDPServerPacketReached(uint32 nFiles, uint32 nIncludedLargeFiles) const;
	bool	SendGlobGetSourcesUDPPacket(CSafeMemFile &data, bool bExt2Packet, uint32 nFiles, uint32 nIncludedLargeFiles);

private:
	struct ProtectedVolumeStatus
	{
		CString VolumeId;
		uint64 FreeBytes;
		uint64 FloorBytes;
		uint64 CompletionBytes;
		uint64 RequiredBytes;
		UINT RoleMask;
	};

	/**
	 * @brief Caches path-to-volume identity lookups within one queue operation.
	 */
	struct VolumeIdentityPathCacheEntry
	{
		CString Path;
		bool bResolved;
		CString VolumeId;
	};

	/**
	 * @brief Resolves paths through the shared Windows volume identity helper without duplicating volume policy.
	 */
	struct VolumeIdentityPathCache
	{
		bool Resolve(LPCTSTR pszPath, CString &rstrVolumeId);

		CArray<VolumeIdentityPathCacheEntry, const VolumeIdentityPathCacheEntry&> Entries;
	};

	/**
	 * @brief Caches the required free bytes for paths already resolved against the current protected-volume snapshot.
	 */
	struct RequiredFreeDiskSpacePathCacheEntry
	{
		CString Path;
		ULONGLONG RequiredBytes;
	};
	/**
	 * @brief Tracks the next part-file number to probe for a temp directory during one bulk add.
	 */
	struct BulkPartFileNumberCacheEntry
	{
		CString TempDir;
		int NextIndex;
	};

	bool	CompareParts(POSITION pos1, POSITION pos2);
	void	SwapParts(POSITION pos1, POSITION pos2);
	void	HeapSort(UINT first, UINT last);
	void	CollectProtectedVolumeStatuses(CArray<ProtectedVolumeStatus, const ProtectedVolumeStatus&> *paStatuses, bool bNotEnoughSpaceLeft) const;
	/**
	 * @brief Returns the current protected-volume disk-space snapshot, refreshing it at most once per download-queue tick or bulk-add section unless forced.
	 */
	const CArray<ProtectedVolumeStatus, const ProtectedVolumeStatus&>& GetProtectedVolumeStatusSnapshot(bool bNotEnoughSpaceLeft, bool bForceRefresh) const;
	/**
	 * @brief Adds a newly admitted file's placement demand to the current protected-volume snapshot.
	 */
	bool	ReserveProtectedVolumeStatusSnapshotDemand(LPCTSTR pszTempPath, LPCTSTR pszIncomingPath, EMFileSize nFileSize) const;
	void	RefreshBroadbandIoBufferSnapshot();
	CString	BuildProtectedDiskSpaceBreachSignature(const CArray<ProtectedVolumeStatus, const ProtectedVolumeStatus&> &aStatuses) const;
	void	ForceSaveAllPartMetFilesForDiskSpace();
	void	StopAllDownloadsForDiskSpace();
	CTypedPtrList<CPtrList, CPartFile*> filelist;
	CTypedPtrList<CPtrList, CPartFile*> m_localServerReqQueue;

	// By BadWolf - Accurate Speed Measurement
	CRing<TransferredData> average_dr_hist;
	// END By BadWolf - Accurate Speed Measurement

	CSourceHostnameResolver m_hostnameResolver;
	uint64	m_datarateMS;
	CPartFile *m_lastfile;
	ULONGLONG m_dwLastA4AFtime; // ZZ:DownloadManager
	ULONGLONG m_ullDownloadQueueProcessTick;
	bool	m_bDownloadQueueProcessActive;
	ULONGLONG m_lastcheckdiskspacetime;
	ULONGLONG m_lastudpsearchtime;
	ULONGLONG m_lastudpstattime;
	ULONGLONG m_lastkademliafilerequest;
	ULONGLONG m_dwNextTCPSrcReq;
	UINT	m_udcounter;
	UINT	m_cRequestsSentToServer;
	int		m_iSearchedServers;

	uint32	m_nUDPFileReasks;
	uint32	m_nFailedUDPFileReasks;
	uint32	m_datarate;
	uint64	m_uBufferedDownloadBytesSnapshot;
	UINT	m_uBufferedDownloadFileCountSnapshot;
	UINT	m_uBulkAddDownloadsDepth;
	bool	m_bBulkAddDownloadsNeedDiskspaceCheck;
	bool	m_bBulkAddDownloadsNeedOverviewExport;
	bool	m_bProtectedDiskSpaceBlocked;
	CString	m_strProtectedDiskSpaceBreachSignature;
	mutable CArray<ProtectedVolumeStatus, const ProtectedVolumeStatus&> m_aProtectedVolumeStatusSnapshot;
	mutable CArray<RequiredFreeDiskSpacePathCacheEntry, const RequiredFreeDiskSpacePathCacheEntry&> m_aRequiredFreeDiskSpacePathCache;
	mutable CArray<BulkPartFileNumberCacheEntry, const BulkPartFileNumberCacheEntry&> m_aBulkPartFileNumberCache;
	mutable ULONGLONG m_ullProtectedVolumeStatusSnapshotTick;
	mutable bool m_bProtectedVolumeStatusSnapshotValid;
	mutable bool m_bProtectedVolumeStatusSnapshotNotEnoughSpaceLeft;
};
