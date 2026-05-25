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
#include <atomic>
#include "ring.h"

struct Requested_Block_Struct;
class CUpDownClient;
typedef CTypedPtrList<CPtrList, CUpDownClient*> CUpDownClientPtrList;

struct UploadingToClient_Struct
{
	UploadingToClient_Struct()
		: m_pClient()
		, m_bIOError()
		, m_bDisableCompression()
		, m_bRetired()
		, m_nPendingIOBlocks()
		, m_ullRetiredTick()
		, m_ullLastRetiredPendingIOLogTick()
	{
	}
	~UploadingToClient_Struct();

	CUpDownClient										*m_pClient;
	CTypedPtrList<CPtrList, Requested_Block_Struct*>	m_BlockRequests_queue;
	CTypedPtrList<CPtrList, Requested_Block_Struct*>	m_DoneBlocks_list;
	CCriticalSection									m_csBlockListsLock; // don't acquire other locks while having this one in any thread other than UploadDiskIOThread or make sure deadlocks are impossible
	bool												m_bIOError;
	bool												m_bDisableCompression;
	bool												m_bRetired;
	std::atomic<LONG>									m_nPendingIOBlocks;
	ULONGLONG											m_ullRetiredTick;
	ULONGLONG											m_ullLastRetiredPendingIOLogTick;
};
typedef CTypedPtrList<CPtrList, UploadingToClient_Struct*> CUploadingPtrList;

typedef struct
{
	uint64	upBytes;
	uint64	upFriendBytes;
	ULONGLONG timestamp;
} AverageUploadRate;

typedef struct
{
	INT_PTR	slots;
	ULONGLONG timestamp;
} ActiveClientsData;

struct UploadTimerRuntimeStats
{
	uint32 uLastDurationMs;
	uint32 uMaxDurationMs;
	uint32 uSlowLoopCount;
	uint32 uSlowLoopThresholdMs;
};


class CUploadQueue
{

public:
	CUploadQueue();
	~CUploadQueue();

	void	Process();
	void	AddClientToQueue(CUpDownClient *client, bool bIgnoreTimelimit = false);
	bool	RemoveFromUploadQueue(CUpDownClient *client, LPCTSTR pszReason = NULL, bool updatewindow = true, bool earlyabort = false);
	bool	RemoveFromWaitingQueue(CUpDownClient *client, bool updatewindow = true);
	bool	IsOnUploadQueue(CUpDownClient *client)	const	{ return (waitinglist.Find(client) != 0); }
	bool	IsDownloading(const CUpDownClient *client)	const { return (GetUploadingClientStructByClient(client) != NULL); }

	void	UpdateDatarates();
	uint32	GetDatarate() const								{ return datarate; }
	uint32  GetToNetworkDatarate() const;

	bool	CheckForTimeOver(CUpDownClient *client, CString *pstrReason = NULL, bool *pbRequeue = NULL);
	INT_PTR	GetWaitingUserCount() const						{ return waitinglist.GetCount(); }
	INT_PTR	GetUploadQueueLength() const					{ return uploadinglist.GetCount(); }
	INT_PTR	GetActiveUploadsCount()	const					{ return m_MaxActiveClientsShortTime; }
	uint32	GetWaitingUserForFileCount(const CSimpleArray<CObject*> &raFiles, bool bOnlyIfChanged);
	uint32	GetDatarateForFile(const CSimpleArray<CObject*> &raFiles) const;
	uint32	GetTargetClientDataRate(bool bMinDatarate) const;
	/** Returns the configured broadband upload budget used to size the startup slot policy. */
	uint32	GetConfiguredUploadBudgetBytesPerSec() const;
	INT_PTR	GetBroadbandSlotCap() const						{ return GetSoftMaxUploadSlots(); }
	/** Returns the fixed broadband per-slot target derived from the configured upload budget. */
	uint32	GetTargetClientDataRateBroadband() const;
	/** Returns the preferred queued block count for a client at the given upload rate. */
	uint32	GetUploadBufferBlockCount(uint32 uClientDatarate) const;
	/** Returns whether a client at the given upload rate should use the large send buffer. */
	bool	ShouldUseBigSendBuffer(uint32 uClientDatarate) const;

	POSITION GetFirstFromUploadList() const					{ return uploadinglist.GetHeadPosition(); }
	CUpDownClient* GetNextFromUploadList(POSITION &curpos) const;
	CUpDownClient* GetQueueClientAt(POSITION &curpos) const;

	POSITION GetFirstFromWaitingList() const				{ return waitinglist.GetHeadPosition(); }
	CUpDownClient* GetNextFromWaitingList(POSITION &curpos) const;
	CUpDownClient* GetWaitClientAt(POSITION &curpos) const;

	CUpDownClient* GetWaitingClientByIP_UDP(uint32 dwIP, uint16 nUDPPort, bool bIgnorePortOnUniqueIP, bool *pbMultipleIPs = NULL);
	CUpDownClient* GetWaitingClientByIP(uint32 dwIP) const;
	CUpDownClient* GetNextClient(const CUpDownClient *lastclient) const;

	UploadingToClient_Struct* GetUploadingClientStructByClient(const CUpDownClient *pClient) const;

	const CUploadingPtrList& GetUploadListTS(CCriticalSection **outUploadListReadLock);

	void	DeleteAll();
	UINT	GetWaitingPosition(CUpDownClient *client);

	uint32	GetSuccessfullUpCount() const					{ return successfullupcount; }
	uint32	GetFailedUpCount() const						{ return failedupcount; }
	uint32	GetAverageUpTime() const;

	CUpDownClient* FindBestClientInQueue();
	static UploadTimerRuntimeStats GetUploadTimerRuntimeStats();

	CUpDownClientPtrList waitinglist;

protected:
	void		RemoveFromWaitingQueue(POSITION pos, bool updatewindow);
	bool		AcceptNewClient(INT_PTR curUploadSlots) const;
	bool		ForceNewClient(bool allowEmptyWaitingQueue = false);
	bool		AddUpNextClient(LPCTSTR pszReason, CUpDownClient *directadd = NULL);

	static VOID CALLBACK UploadTimer(HWND hWnd, UINT nMsg, UINT_PTR nId, DWORD dwTime) noexcept;

private:
	struct RetiredUploadClientStructContext
	{
		UploadingToClient_Struct *pUploadClientStruct;
	};
	INT_PTR	GetSoftMaxUploadSlots() const;
	/** Returns the minimum headroom required before underfill may recycle a weak slot. */
	uint32	GetBroadbandUnderfillMarginBytesPerSec() const;
	/** Returns whether the current upload rate is materially below the configured upload budget. */
	bool	IsBroadbandUploadUnderfilled() const;
	/** Updates the sustained-underfill timer used by conservative slow-slot recycling. */
	void	UpdateBroadbandUnderfillState(ULONGLONG curTick);
	/** Returns whether broadband underfill has persisted long enough to recycle a weak slot. */
	bool	HasSustainedBroadbandUnderfill(ULONGLONG curTick) const;
	/** Returns whether a slot has aged enough to be judged for broadband slow-slot recycling. */
	bool	HasCompletedSlowUploadWarmup(const CUpDownClient *client) const;
	uint32	GetSlowUploadRateThreshold() const;
	bool	ShouldTrackSlowUploadSlots() const;
	void	UpdateMaxClientScore();
	uint32	GetMaxClientScore() const						{ return m_imaxscore; }
	void	UpdateActiveClientsInfo(ULONGLONG curTick);
	void	RetireUploadClientStruct(POSITION pos, UploadingToClient_Struct *pUploadClientStruct, CUpDownClient *pClient);
	void	RetireStaleUploadClientStruct(POSITION pos, UploadingToClient_Struct *pUploadClientStruct);
	void	ReclaimRetiredUploadClientStructs();
	void	InvalidateUploadClientStruct(UploadingToClient_Struct *pUploadClientStruct, CUpDownClient *pClient);
	void	InvalidateUploadClientStructWithoutClient(UploadingToClient_Struct *pUploadClientStruct);
	RetiredUploadClientStructContext RemoveUploadClientStructFromActiveList(POSITION pos, UploadingToClient_Struct *pUploadClientStruct);
	void	RemoveStaleWaitingClient(POSITION pos);

	void InsertInUploadingList(CUpDownClient *newclient, bool bNoLocking);
	void InsertInUploadingList(UploadingToClient_Struct *pNewClientUploadStruct, bool bNoLocking);
	float GetAverageCombinedFilePrioAndCredit();

	CUploadingPtrList	uploadinglist;
	CUploadingPtrList	m_retiredUploadingList;
	// This lock ensures that only the main thread writes the uploading list,
	// other threads need to fetch the lock if they want to read (but are not allowed to write).
	// Don't acquire other locks while having this one in any thread
	// other than UploadDiskIOThread or make sure deadlocks are impossible
	CCriticalSection	m_csUploadListMainThrdWriteOtherThrdsRead;

	// By BadWolf - Accurate Speed Measurement
	CRing<AverageUploadRate> average_ur_hist;
	CRing<ActiveClientsData> activeClients_hist;
	uint32	datarate;		//data rate sent to network (including friends)
	uint32  friendDatarate;	//data rate sent to friends (included in above total)
	// By BadWolf - Accurate Speed Measurement

	UINT_PTR h_timer; //100 ms
	uint32	successfullupcount;
	uint32	failedupcount;
	uint32	totaluploadtime;
	ULONGLONG m_nLastStartUpload;
	ULONGLONG m_dwRemovedClientByScore;
	uint32	m_imaxscore;

	ULONGLONG m_dwLastCalculatedAverageCombinedFilePrioAndCredit;
	float	m_fAverageCombinedFilePrioAndCredit;
	ULONGLONG m_ullBroadbandUnderfillSince;
	INT_PTR	m_iHighestNumberOfFullyActivatedSlotsSinceLastCall;
	INT_PTR	m_MaxActiveClients;
	INT_PTR	m_MaxActiveClientsShortTime;

	uint64	m_sendingBytes;
	uint64	m_average_ur_sum;
	ULONGLONG m_lastCalculatedDataRateTick;

	ULONGLONG m_dwLastResortedUploadSlots;
	bool	m_bStatisticsWaitingListDirty;
};
