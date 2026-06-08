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
#include "BadPeerInstrumentationSeams.h"
#include "UploadQueue.h"
#include "Packets.h"
#include "PartFile.h"
#include "ListenSocket.h"
#include "Exceptions.h"
#include "Scheduler.h"
#include "PerfLog.h"
#include "UploadBandwidthThrottler.h"
#include "ClientList.h"
#include "DownloadQueue.h"
#include "FriendList.h"
#include "Statistics.h"
#include "UpDownClient.h"
#include "SharedFileList.h"
#include "KnownFileList.h"
#include "ServerConnect.h"
#include "ClientCredits.h"
#include "ServerList.h"
#include "WebServer.h"
#include "emuledlg.h"
#include "ServerWnd.h"
#include "TransferDlg.h"
#include "StatisticsDlg.h"
#include "EMSocketSendSeams.h"
#include "SocketIoSeams.h"
#include "Kademlia/Kademlia/Kademlia.h"
#include "Kademlia/Kademlia/Prefs.h"
#include "Log.h"
#include "collection.h"
#include "UploadQueueSeams.h"
#include "UpDownClientDeleteSeams.h"
#include "Win32CallbackTimerSeams.h"

#include <memory>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


static uint32 i1sec, i2sec, i5sec, i60sec;
static UINT s_uSaveStatistics = 0;
static uint32 igraph, istats;
static uint32 s_uUploadTimerLastDurationMs = 0;
static uint32 s_uUploadTimerMaxDurationMs = 0;
static uint32 s_uUploadTimerSlowLoopCount = 0;
static ULONGLONG s_ullLastBroadbandRetainedSlotLogTick = 0;
static UINT s_uSuppressedBroadbandRetainedSlotLogs = 0;

#define HIGHSPEED_UPLOADRATE_START	(500*1024)
#define HIGHSPEED_UPLOADRATE_END	(300*1024)

namespace
{
	void QueueUploadListDisplayRefresh()
	{
		if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL)
			theApp.emuledlg->transferwnd->QueueDisplayRefresh(DISPLAY_REFRESH_UPLOAD_LIST);
	}

	void QueueWaitingListDisplayRefresh()
	{
		if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL)
			theApp.emuledlg->transferwnd->QueueDisplayRefresh(DISPLAY_REFRESH_QUEUE_LIST);
	}

	bool IsLiveUploadQueueClient(const CUpDownClient *client)
	{
		return client != NULL
			&& theApp.clientlist != NULL
			&& theApp.clientlist->ContainsClientPointer(client);
	}

	void RecordUploadTimerDuration(const uint32 uDurationMs) noexcept
	{
		s_uUploadTimerLastDurationMs = uDurationMs;
		if (uDurationMs > s_uUploadTimerMaxDurationMs)
			s_uUploadTimerMaxDurationMs = uDurationMs;
		if (ShouldCountSlowUploadTimerLoop(uDurationMs))
			++s_uUploadTimerSlowLoopCount;
	}

	bool ShouldLogBroadbandRetainedSlot(UINT &uSuppressedLogs)
	{
		constexpr ULONGLONG ullLogIntervalMs = SEC2MS(30);
		const ULONGLONG ullNow = ::GetTickCount64();
		if (s_ullLastBroadbandRetainedSlotLogTick != 0 && ullNow < s_ullLastBroadbandRetainedSlotLogTick + ullLogIntervalMs) {
			++s_uSuppressedBroadbandRetainedSlotLogs;
			return false;
		}

		uSuppressedLogs = s_uSuppressedBroadbandRetainedSlotLogs;
		s_uSuppressedBroadbandRetainedSlotLogs = 0;
		s_ullLastBroadbandRetainedSlotLogTick = ullNow;
		return true;
	}

	class CUploadTimerDurationScope
	{
	public:
		CUploadTimerDurationScope() noexcept
			: m_ullStartTick(::GetTickCount64())
		{
		}

		~CUploadTimerDurationScope() noexcept
		{
			const ULONGLONG ullDuration = ::GetTickCount64() - m_ullStartTick;
			RecordUploadTimerDuration(ullDuration > _UI32_MAX ? _UI32_MAX : static_cast<uint32>(ullDuration));
		}

	private:
		ULONGLONG m_ullStartTick;
	};

	uint64 ResolveSessionTransferLimitBytes(const CKnownFile *pUploadingFile)
	{
		switch (thePrefs.GetSessionTransferLimitMode()) {
		case ESessionTransferLimitMode::PercentOfFile:
			if (pUploadingFile == NULL)
				return 0;
			return ((uint64)pUploadingFile->GetFileSize() * thePrefs.GetSessionTransferLimitValue() + 99u) / 100u;
		case ESessionTransferLimitMode::AbsoluteMiB:
			return (uint64)thePrefs.GetSessionTransferLimitValue() * 1024ui64 * 1024ui64;
		default:
			return 0;
		}
	}
}


CUploadQueue::CUploadQueue()
	: average_ur_hist(512, 512)
	, activeClients_hist(512, 512)
	, datarate()
	, friendDatarate()
	, successfullupcount()
	, failedupcount()
	, totaluploadtime()
	, m_nLastStartUpload()
	, m_dwRemovedClientByScore(::GetTickCount64())
	, m_imaxscore()
	, m_dwLastCalculatedAverageCombinedFilePrioAndCredit()
	, m_fAverageCombinedFilePrioAndCredit()
	, m_ullLastNoRequestRepeatCleanup()
	, m_ullBroadbandUnderfillSince()
	, m_iHighestNumberOfFullyActivatedSlotsSinceLastCall()
	, m_MaxActiveClients()
	, m_MaxActiveClientsShortTime()
	, m_average_ur_sum()
	, m_lastCalculatedDataRateTick()
	, m_dwLastResortedUploadSlots()
	, m_bStatisticsWaitingListDirty(true)
{
	i1sec = i2sec = i5sec = i60sec = 0;
#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
	const ULONGLONG ullPhaseStart = theApp.GetStartupProfileTimestampUs();
#endif
	VERIFY(Win32CallbackTimerSeams::TryStartNullWindowCallbackTimer(h_timer, SEC2MS(1)/10, UploadTimer));
#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
	theApp.AppendStartupProfileLine(_T("broadband.upload_queue.timer_ready"), theApp.GetStartupProfileElapsedUs(ullPhaseStart), ullPhaseStart);
#endif
	if (thePrefs.GetVerbose() && !h_timer)
		AddDebugLogLine(true, _T("Failed to create 'upload queue' timer - %s"), (LPCTSTR)GetErrorMessage(::GetLastError()));
}

CUploadQueue::~CUploadQueue()
{
	(void)Win32CallbackTimerSeams::StopNullWindowCallbackTimer(h_timer);
}

/**
 * Finds the highest ranking waiting client without any reconnect-side reservation.
 *
 * The stabilization branch intentionally uses one admission path: the best queue
 * candidate is selected by score, then the normal connect/upload transition
 * decides whether the slot becomes active. Broadband policy does not reserve
 * future slots for special reconnect cases.
 */
CUpDownClient* CUploadQueue::FindBestClientInQueue()
{
	uint32 bestscore = 0;
	uint32 bestCooldownProbeScore = 0;
	ULONGLONG ullBestCooldownProbeRemaining = _UI64_MAX;
	CUpDownClient *newclient = NULL;
	CUpDownClient *cooldownProbeClient = NULL;
	const ULONGLONG curTick = ::GetTickCount64();
	const bool bAllowCooldownProbe = ShouldProbeUploadCooldownCandidate(
		HasSustainedBroadbandUnderfill(curTick),
		uploadinglist.GetCount(),
		GetSoftMaxUploadSlots());

	for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		CUpDownClient *cur_client = waitinglist.GetNext(pos);
		if (!IsLiveUploadQueueClient(cur_client)) {
			RemoveStaleWaitingClient(pos2);
			continue;
		}
		//While we are going through this list. Lets check if a client appears to have left the network.
		ASSERT(cur_client->GetLastUpRequest());
		if ((curTick >= cur_client->GetLastUpRequest() + MAX_PURGEQUEUETIME) || !theApp.sharedfiles->GetFileByID(cur_client->GetUploadFileID())) {
			//This client has either not been seen in a long time, or we no longer share the file he wanted any more.
			cur_client->ClearWaitStartTime();
			RemoveFromWaitingQueue(pos2, true);
		} else {
			const bool bCooldownActive = ApplyUploadRetryCooldown(cur_client, curTick) || cur_client->IsInSlowUploadCooldown();
			if (!IsUploadQueueAdmissionCandidate(bCooldownActive)) {
				// WHY: recycled or short-failed peers stay on the protocol queue,
				// but should only be retried early when spare broadband capacity
				// would otherwise stay idle behind a cooldown-only waiting list.
				const ULONGLONG ullCooldownRemaining = cur_client->GetSlowUploadCooldownRemaining();
				const uint32 cur_score = cur_client->GetScore(false);
				if (bAllowCooldownProbe
					&& CanProbeUploadCooldownCandidate(cur_client, curTick)
					&& (ullCooldownRemaining < ullBestCooldownProbeRemaining
						|| (ullCooldownRemaining == ullBestCooldownProbeRemaining && PreferHigherUploadQueueScore(cur_score, bestCooldownProbeScore))))
				{
					ullBestCooldownProbeRemaining = ullCooldownRemaining;
					bestCooldownProbeScore = cur_score;
					cooldownProbeClient = cur_client;
				}
				continue;
			}

			// finished clearing
			uint32 cur_score = cur_client->GetScore(false);

			if (PreferHigherUploadQueueScore(cur_score, bestscore)) {
				bestscore = cur_score;
				newclient = cur_client;
			}
		}
	}

	return newclient != NULL ? newclient : cooldownProbeClient;
}

void CUploadQueue::InsertInUploadingList(CUpDownClient *newclient, bool bNoLocking)
{
	std::unique_ptr<UploadingToClient_Struct> pNewClientUploadStruct(new UploadingToClient_Struct);
	pNewClientUploadStruct->m_pClient = newclient;
	InsertInUploadingList(pNewClientUploadStruct.release(), bNoLocking);
}

void CUploadQueue::InsertInUploadingList(UploadingToClient_Struct *pNewClientUploadStruct, bool bNoLocking)
{
	std::unique_ptr<UploadingToClient_Struct> pOwnedUploadStruct(pNewClientUploadStruct);
	CEMSocket *pSocket = pOwnedUploadStruct->m_pClient->GetFileUploadSocket();
	bool bAddedToThrottler = false;
	// Add it last
	theApp.uploadBandwidthThrottler->AddToStandardList(uploadinglist.GetCount(), pSocket);
	bAddedToThrottler = true;
	try {
		CSingleLock lockUploadList(&m_csUploadListMainThrdWriteOtherThrdsRead);
		if (!bNoLocking)
			lockUploadList.Lock();
		// WHY: CList::AddTail can allocate while publishing a new active upload
		// entry. Keep ownership local until the list node exists, and let
		// CSingleLock balance the upload-list lock if MFC reports low memory.
		uploadinglist.AddTail(pOwnedUploadStruct.get());
		pOwnedUploadStruct.release();
		if (!bNoLocking)
			lockUploadList.Unlock();
	} catch (...) {
		// WHY: the throttler stores only the socket pointer. If publishing the
		// matching upload-list entry fails, roll that side effect back so the
		// helper thread cannot later drive a slot that uploadqueue does not own.
		if (bAddedToThrottler)
			theApp.uploadBandwidthThrottler->RemoveFromStandardList(pSocket);
		throw;
	}

	pNewClientUploadStruct->m_pClient->SetSlotNumber((UINT)uploadinglist.GetCount());
}

bool CUploadQueue::AddUpNextClient(LPCTSTR pszReason, CUpDownClient *directadd)
{
	CUpDownClient *newclient = directadd;
	// select next client or use given client
	if (newclient == NULL) {
		newclient = FindBestClientInQueue();
		if (newclient == NULL)
			return false;
	}
	RemoveFromWaitingQueue(newclient, true);
	theApp.emuledlg->transferwnd->ShowQueueCount(waitinglist.GetCount());

	if (!thePrefs.TransferFullChunks())
		UpdateMaxClientScore(); // refresh score caching, now that the highest score is removed

	if (IsDownloading(newclient))
		return false;

	if (pszReason && thePrefs.GetLogUlDlEvents())
		AddDebugLogLine(false, _T("Adding client to upload list: %s Client: %s"), pszReason, (LPCTSTR)newclient->DbgGetClientInfo());

	if (newclient->HasCollectionUploadSlot() && directadd == NULL) {
		// Collection requests no longer use a separate scheduler path on this
		// branch. If a stale marker reaches the normal admission path, clear it
		// before the fixed-cap slot controller continues.
		newclient->SetCollectionUploadSlot(false);
	}

	// tell the client that we are now ready to upload
	if (!newclient->socket || !newclient->socket->IsConnected() || !newclient->CheckHandshakeFinished()) {
		newclient->SetUploadState(US_CONNECTING);
		if (!newclient->TryToConnect(true)) {
			if (ShouldCooldownFailedUploadAdmission(true, newclient->GetFriendSlot(), GetUploadRetryCooldownIP(newclient))) {
				// WHY: a peer whose upload admission cannot even establish the
				// upload connection can repeatedly consume broadband slot-open
				// attempts. Keep the suppression local and temporary by reusing
				// the upload retry cooldown keyed by peer IP.
				const ULONGLONG ullCooldownUntil = ::GetTickCount64() + SEC2MS(GetUploadChurnRetryCooldownSecondsForBudget(
					thePrefs.GetSlowUploadCooldownSeconds(),
					GetConfiguredUploadBudgetBytesPerSec()));
				newclient->SetSlowUploadCooldownUntil(ullCooldownUntil);
				SetUploadRetryCooldown(newclient, ullCooldownUntil, uploadRetryCooldownFailedAdmission);
				EMULEBB_BAD_PEER_LOG_CLIENT_EVENT(_T("upload_failed_admission_cooldown"), _T("low"), newclient, _T("cooldown"), _T("Failed upload admission"), theApp.sharedfiles->GetFileByID(newclient->GetUploadFileID()));
				if (thePrefs.GetLogUlDlEvents())
					AddDebugLogLine(DLP_LOW, false, _T("%s: Upload retry cooled down after failed upload admission."), newclient->GetUserName());
			}
			UpDownClientDeleteSeams::AssertReadyToDelete(newclient, _T("CUploadQueue::AddUpNextClient TryToConnect"));
			delete newclient;
			return false;
		}
	} else {
		if (thePrefs.GetDebugClientTCPLevel() > 0)
			DebugSend("OP_AcceptUploadReq", newclient);
		Packet *packet = new Packet(OP_ACCEPTUPLOADREQ, 0);
		theStats.AddUpDataOverheadFileRequest(packet->size);
		newclient->SendPacket(packet);
		newclient->SetUploadState(US_UPLOADING);
	}
	newclient->SetUpStartTime();
	newclient->ResetSessionUp();
	newclient->ClearSlowUploadCooldown();
	newclient->ResetSlowUploadTracking();

	InsertInUploadingList(newclient, false);

	m_nLastStartUpload = ::GetTickCount64();

	// statistic
	CKnownFile *reqfile = theApp.sharedfiles->GetFileByID((uchar*)newclient->GetUploadFileID());
	if (reqfile)
		reqfile->statistic.AddAccepted();

	QueueUploadListDisplayRefresh();

	return true;
}

void CUploadQueue::UpdateActiveClientsInfo(ULONGLONG curTick)
{
	// Save number of active clients for statistics
	INT_PTR tempHighest = theApp.uploadBandwidthThrottler->GetHighestNumberOfFullyActivatedSlotsSinceLastCallAndReset();

	//if(thePrefs.GetLogUlDlEvents() && theApp.uploadBandwidthThrottler->GetStandardListSize() > uploadinglist.GetCount())
		// debug info, will remove this when I'm done.
	//	AddDebugLogLine(false, _T("UploadQueue: Error! Throttler has more slots than UploadQueue! Throttler: %i UploadQueue: %i Tick: %i"), theApp.uploadBandwidthThrottler->GetStandardListSize(), uploadinglist.GetCount(), ::GetTickCount64());

	tempHighest = min(tempHighest, uploadinglist.GetCount() + 1);
	m_iHighestNumberOfFullyActivatedSlotsSinceLastCall = tempHighest;

	// save some data about the number of fully active clients
	INT_PTR tempMaxRemoved = 0;
	while (!activeClients_hist.IsEmpty() && curTick >= activeClients_hist.Head().timestamp + SEC2MS(20)) {
		tempMaxRemoved = max(tempMaxRemoved, activeClients_hist.Head().slots);
		activeClients_hist.RemoveHead();
	}

	activeClients_hist.AddTail(ActiveClientsData{tempHighest, curTick});

	if (activeClients_hist.Count() <= 1)
		m_MaxActiveClients = m_MaxActiveClientsShortTime = m_iHighestNumberOfFullyActivatedSlotsSinceLastCall;
	else {
		INT_PTR tempMax, tempMaxShortTime;
		tempMax = tempMaxShortTime = m_iHighestNumberOfFullyActivatedSlotsSinceLastCall;
		for (UINT_PTR ix = activeClients_hist.Count(); ix-- > 0;) {
			const ActiveClientsData &d = activeClients_hist[ix];
			if (curTick >= d.timestamp + SEC2MS(10) && (tempMaxRemoved <= tempMax || tempMaxRemoved < m_MaxActiveClients))
				break;
			if (d.slots > tempMax)
				tempMax = d.slots;
			if (d.slots > tempMaxShortTime && curTick < d.timestamp + SEC2MS(10))
				tempMaxShortTime = d.slots;
		}
		if (tempMaxRemoved >= m_MaxActiveClients || tempMax > m_MaxActiveClients)
			m_MaxActiveClients = tempMax;
		m_MaxActiveClientsShortTime = tempMaxShortTime;
	}
}

#ifdef EMULEBB_ENABLE_UPLOAD_SLOT_DIAGNOSTICS
void CUploadQueue::LogUploadSlotInstrumentation(ULONGLONG curTick) const
{
	static ULONGLONG s_ullLastUploadSlotInstrumentationLogTick = 0;
	if (s_ullLastUploadSlotInstrumentationLogTick != 0 && curTick < s_ullLastUploadSlotInstrumentationLogTick + SEC2MS(10))
		return;
	s_ullLastUploadSlotInstrumentationLogTick = curTick;

	const INT_PTR iThrottlerSlots = theApp.uploadBandwidthThrottler != NULL
		? theApp.uploadBandwidthThrottler->GetStandardListSize()
		: -1;
	const ULONGLONG ullUnderfillAgeMs = m_ullBroadbandUnderfillSince != 0 && curTick >= m_ullBroadbandUnderfillSince
		? curTick - m_ullBroadbandUnderfillSince
		: 0;
	INT_PTR iEligibleWaitingClients = 0;
	INT_PTR iCooldownWaitingClients = 0;
	INT_PTR iRetryCooldownWaitingClients = 0;
	INT_PTR iNoRequestCooldownWaitingClients = 0;
	INT_PTR iProductiveNoRequestCooldownWaitingClients = 0;
	INT_PTR iUnproductiveNoRequestCooldownWaitingClients = 0;
	INT_PTR iClientOnlyCooldownWaitingClients = 0;
	INT_PTR iRetryNoRequestWaitingClients = 0;
	INT_PTR iRetryChurnWaitingClients = 0;
	INT_PTR iRetryStalledWaitingClients = 0;
	INT_PTR iRetrySlowWaitingClients = 0;
	INT_PTR iRetryUnknownWaitingClients = 0;
	INT_PTR iActiveZeroRateClients = 0;
	INT_PTR iActiveNoRequestClients = 0;
	INT_PTR iActiveNoRequestDrainedClients = 0;
	INT_PTR iActiveNoRequestDrainedZeroRateClients = 0;
	INT_PTR iActiveNoRequestDrainedNonzeroRateClients = 0;
	INT_PTR iActiveNoRequestPendingIOClients = 0;
	INT_PTR iActiveNoRequestBufferedPayloadClients = 0;
	INT_PTR iActiveNoRequestSocketBacklogClients = 0;
	INT_PTR iActiveQueuedRequestClients = 0;
	INT_PTR iActivePendingIOClients = 0;
	INT_PTR iActiveBufferedPayloadClients = 0;
	INT_PTR iActiveSocketBacklogClients = 0;
	INT_PTR iActiveNoRequestNeverAcceptedClients = 0;
	INT_PTR iActiveNoRequestRecycleEligibleClients = 0;
	INT_PTR iActiveNoRequestRecycleGraceBlockedClients = 0;
	INT_PTR iActiveNoRequestRecycleUnderfillBlockedClients = 0;
	ULONGLONG ullActiveNoRequestAgeSumMs = 0;
	ULONGLONG ullActiveNoRequestAgeMaxMs = 0;
	ULONGLONG ullActiveNoRequestLastAcceptedAgeMaxMs = 0;
	ULONGLONG ullActiveNoRequestZeroMaxMs = 0;
	ULONGLONG ullCooldownMinMs = 0;
	ULONGLONG ullCooldownMaxMs = 0;
	ULONGLONG ullCooldownSumMs = 0;
	const bool bSustainedBroadbandUnderfill = HasSustainedBroadbandUnderfill(curTick);
	const uint32 uBudgetBytesPerSec = GetConfiguredUploadBudgetBytesPerSec();
	const ULONGLONG ullNoRequestGraceMs = GetNoRequestUploadRecycleGraceMs(GetZeroUploadGraceSecondsForBudget(thePrefs.GetZeroUploadRateGraceSeconds(), uBudgetBytesPerSec));
	const uint32 uDrainedNoRequestRateThreshold = GetSlowUploadRateThreshold();
	for (POSITION uploadPos = uploadinglist.GetHeadPosition(); uploadPos != NULL;) {
		const UploadingToClient_Struct *pUploadingClient = uploadinglist.GetNext(uploadPos);
		CUpDownClient *pActiveClient = pUploadingClient != NULL ? pUploadingClient->m_pClient : NULL;
		if (pActiveClient == NULL)
			continue;
		INT_PTR iReqBlocks = 0;
		LONG nPendingIOBlocks = 0;
		bool bHasAcceptedReqBlock = false;
		ULONGLONG ullLastAcceptedReqBlockAgeMs = 0;
		{
			CSingleLock lockBlockLists(&const_cast<UploadingToClient_Struct*>(pUploadingClient)->m_csBlockListsLock, TRUE);
			ASSERT(lockBlockLists.IsLocked());
			iReqBlocks = pUploadingClient->m_BlockRequests_queue.GetCount();
			nPendingIOBlocks = pUploadingClient->m_nPendingIOBlocks.load();
			if (iReqBlocks == 0) {
				const ULONGLONG ullAgeMs = pActiveClient->GetUpStartTimeDelay();
				ullActiveNoRequestAgeSumMs += ullAgeMs;
				if (ullAgeMs > ullActiveNoRequestAgeMaxMs)
					ullActiveNoRequestAgeMaxMs = ullAgeMs;
				if (pUploadingClient->m_ullLastAcceptedReqBlockTick.load() == 0)
					++iActiveNoRequestNeverAcceptedClients;
				else {
					const ULONGLONG ullLastAcceptedReqBlockTick = pUploadingClient->m_ullLastAcceptedReqBlockTick.load();
					bHasAcceptedReqBlock = true;
					ullLastAcceptedReqBlockAgeMs = curTick >= ullLastAcceptedReqBlockTick ? curTick - ullLastAcceptedReqBlockTick : 0;
					if (ullLastAcceptedReqBlockAgeMs > ullActiveNoRequestLastAcceptedAgeMaxMs)
						ullActiveNoRequestLastAcceptedAgeMaxMs = ullLastAcceptedReqBlockAgeMs;
				}
				const ULONGLONG ullZeroUploadMs = pActiveClient->GetAccumulatedZeroUploadMs();
				if (ullZeroUploadMs > ullActiveNoRequestZeroMaxMs)
					ullActiveNoRequestZeroMaxMs = ullZeroUploadMs;
			}
		}
		CEMSocket *pUploadSocket = pActiveClient->GetFileUploadSocket(false);
		const INT_PTR iSocketQueue = pUploadSocket != NULL
			? pUploadSocket->DbgGetStdQueueCount()
			: -1;
		if (pActiveClient->GetUploadDatarate() == 0)
			++iActiveZeroRateClients;
		if (iReqBlocks == 0) {
			++iActiveNoRequestClients;
			if (nPendingIOBlocks > 0)
				++iActiveNoRequestPendingIOClients;
			if (pActiveClient->GetPayloadInBuffer() > 0)
				++iActiveNoRequestBufferedPayloadClients;
			if (iSocketQueue > 0)
				++iActiveNoRequestSocketBacklogClients;
			if (nPendingIOBlocks == 0 && pActiveClient->GetPayloadInBuffer() == 0 && iSocketQueue == 0) {
				++iActiveNoRequestDrainedClients;
				const bool bNoRequestRecycleCandidate = ShouldRecycleNoRequestBroadbandUploadSlot(
					bSustainedBroadbandUnderfill,
					pActiveClient->GetFriendSlot(),
					pActiveClient->GetUploadDatarate(),
					uDrainedNoRequestRateThreshold,
					pActiveClient->GetPayloadInBuffer(),
					iReqBlocks,
					nPendingIOBlocks,
					iSocketQueue,
					pActiveClient->GetUpStartTimeDelay(),
					bHasAcceptedReqBlock,
					ullLastAcceptedReqBlockAgeMs,
					ullNoRequestGraceMs);
				if (pActiveClient->GetUploadDatarate() == 0) {
					++iActiveNoRequestDrainedZeroRateClients;
					if (bNoRequestRecycleCandidate)
						++iActiveNoRequestRecycleEligibleClients;
					else if (!bSustainedBroadbandUnderfill)
						++iActiveNoRequestRecycleUnderfillBlockedClients;
					else if (!pActiveClient->GetFriendSlot())
						++iActiveNoRequestRecycleGraceBlockedClients;
				} else {
					++iActiveNoRequestDrainedNonzeroRateClients;
					if (bNoRequestRecycleCandidate)
						++iActiveNoRequestRecycleEligibleClients;
				}
			}
		} else
			++iActiveQueuedRequestClients;
		if (nPendingIOBlocks > 0)
			++iActivePendingIOClients;
		if (pActiveClient->GetPayloadInBuffer() > 0)
			++iActiveBufferedPayloadClients;
		if (iSocketQueue > 0)
			++iActiveSocketBacklogClients;
	}
	for (POSITION waitPos = waitinglist.GetHeadPosition(); waitPos != NULL;) {
		const CUpDownClient *pWaitingClient = waitinglist.GetNext(waitPos);
		if (!IsLiveUploadQueueClient(pWaitingClient))
			continue;
		const ULONGLONG ullCooldownRemainingMs = pWaitingClient->GetSlowUploadCooldownRemaining();
		if (IsUploadQueueAdmissionCandidate(ullCooldownRemainingMs != 0))
			++iEligibleWaitingClients;
		else {
			++iCooldownWaitingClients;
			ullCooldownSumMs += ullCooldownRemainingMs;
			if (ullCooldownMinMs == 0 || ullCooldownRemainingMs < ullCooldownMinMs)
				ullCooldownMinMs = ullCooldownRemainingMs;
			if (ullCooldownRemainingMs > ullCooldownMaxMs)
				ullCooldownMaxMs = ullCooldownRemainingMs;
			const uint32 dwCooldownIP = GetUploadRetryCooldownIP(pWaitingClient);
			const std::map<uint32, UploadRetryCooldownState>::const_iterator itRetryCooldown =
				dwCooldownIP != 0 ? m_uploadRetryCooldownByIP.find(dwCooldownIP) : m_uploadRetryCooldownByIP.end();
			const std::map<uint32, NoRequestUploadRetryCooldownState>::const_iterator itNoRequestCooldown =
				dwCooldownIP != 0 ? m_noRequestUploadRetryCooldownByIP.find(dwCooldownIP) : m_noRequestUploadRetryCooldownByIP.end();
			const bool bRetryCooldownActive = itRetryCooldown != m_uploadRetryCooldownByIP.end()
				&& itRetryCooldown->second.ullCooldownUntil > curTick;
			const bool bNoRequestCooldownActive = itNoRequestCooldown != m_noRequestUploadRetryCooldownByIP.end()
				&& itNoRequestCooldown->second.ullCooldownUntil > curTick;
			if (bRetryCooldownActive) {
				++iRetryCooldownWaitingClients;
				switch (itRetryCooldown->second.eReason) {
				case uploadRetryCooldownNoRequest:
					++iRetryNoRequestWaitingClients;
					break;
				case uploadRetryCooldownFailedAdmission:
				case uploadRetryCooldownNoSocket:
				case uploadRetryCooldownShortFailed:
					++iRetryChurnWaitingClients;
					break;
				case uploadRetryCooldownIdle:
				case uploadRetryCooldownStalled:
					++iRetryStalledWaitingClients;
					break;
				case uploadRetryCooldownZeroUpload:
				case uploadRetryCooldownSlowUpload:
					++iRetrySlowWaitingClients;
					break;
				default:
					++iRetryUnknownWaitingClients;
					break;
				}
			}
			if (bNoRequestCooldownActive) {
				++iNoRequestCooldownWaitingClients;
				if (itNoRequestCooldown->second.bProductiveRecycle)
					++iProductiveNoRequestCooldownWaitingClients;
				else
					++iUnproductiveNoRequestCooldownWaitingClients;
			}
			if (!bRetryCooldownActive && !bNoRequestCooldownActive)
				++iClientOnlyCooldownWaitingClients;
		}
	}
	const ULONGLONG ullCooldownAvgMs = iCooldownWaitingClients > 0
		? ullCooldownSumMs / static_cast<ULONGLONG>(iCooldownWaitingClients)
		: 0;
	const ULONGLONG ullActiveNoRequestAgeAvgMs = iActiveNoRequestClients > 0
		? ullActiveNoRequestAgeSumMs / static_cast<ULONGLONG>(iActiveNoRequestClients)
		: 0;
	CSharedFileList::SharedPublishInstrumentationSnapshot sharedPublish = {};
	if (theApp.sharedfiles != NULL)
		theApp.sharedfiles->GetPublishInstrumentationSnapshot(sharedPublish);

	UploadSlotDiagnosticsLogLine(
		_T("UploadSlotDiagnostics: summary uploadSlots=%Id retiredSlots=%Id waiting=%Id waitingEligible=%Id waitingCooldown=%Id waitingRetryCooldown=%Id waitingNoRequestCooldown=%Id waitingNoRequestProductive=%Id waitingNoRequestUnproductive=%Id waitingClientOnlyCooldown=%Id waitingRetryNoRequest=%Id waitingRetryChurn=%Id waitingRetryStalled=%Id waitingRetrySlow=%Id waitingRetryUnknown=%Id activeZeroRate=%Id activeNoRequest=%Id activeNoRequestDrained=%Id activeNoRequestDrainedZeroRate=%Id activeNoRequestDrainedNonzeroRate=%Id activeNoRequestPendingIO=%Id activeNoRequestBufferedPayload=%Id activeNoRequestSocketBacklog=%Id activeNoRequestNeverAccepted=%Id activeNoRequestRecycleEligible=%Id activeNoRequestRecycleGraceBlocked=%Id activeNoRequestRecycleUnderfillBlocked=%Id activeNoRequestAgeAvgMs=%I64u activeNoRequestAgeMaxMs=%I64u activeNoRequestLastAcceptedAgeMaxMs=%I64u activeNoRequestZeroMaxMs=%I64u activeQueuedRequests=%Id activePendingIO=%Id activeBufferedPayload=%Id activeSocketBacklog=%Id waitingCooldownMinMs=%I64u waitingCooldownAvgMs=%I64u waitingCooldownMaxMs=%I64u retryCooldowns=%u noRequestCooldowns=%u sharedFiles=%Id ed2kPublishedFiles=%u ed2kPendingFiles=%u ed2kPendingLargeUnsupportedFiles=%u ed2kOfferLimit=%u kadPublishReady=%u kadSourceDueFiles=%u kadSourceBackoffFiles=%u kadSourceSearches=%u kadSourceSearchCap=%u kadKeywordSearches=%u kadKeywordSearchCap=%u kadNotesSearches=%u kadNotesSearchCap=%u throttlerSlots=%Id activeSlots=%Id cap=%Id configuredBudgetBytesPerSec=%u targetPerSlotBytesPerSec=%u toNetworkBytesPerSec=%u datarateBytesPerSec=%u underfilled=%u underfillAgeMs=%I64u slowTracking=%u"),
		uploadinglist.GetCount(),
		m_retiredUploadingList.GetCount(),
		waitinglist.GetCount(),
		iEligibleWaitingClients,
		iCooldownWaitingClients,
		iRetryCooldownWaitingClients,
		iNoRequestCooldownWaitingClients,
		iProductiveNoRequestCooldownWaitingClients,
		iUnproductiveNoRequestCooldownWaitingClients,
		iClientOnlyCooldownWaitingClients,
		iRetryNoRequestWaitingClients,
		iRetryChurnWaitingClients,
		iRetryStalledWaitingClients,
		iRetrySlowWaitingClients,
		iRetryUnknownWaitingClients,
		iActiveZeroRateClients,
		iActiveNoRequestClients,
		iActiveNoRequestDrainedClients,
		iActiveNoRequestDrainedZeroRateClients,
		iActiveNoRequestDrainedNonzeroRateClients,
		iActiveNoRequestPendingIOClients,
		iActiveNoRequestBufferedPayloadClients,
		iActiveNoRequestSocketBacklogClients,
		iActiveNoRequestNeverAcceptedClients,
		iActiveNoRequestRecycleEligibleClients,
		iActiveNoRequestRecycleGraceBlockedClients,
		iActiveNoRequestRecycleUnderfillBlockedClients,
		static_cast<uint64>(ullActiveNoRequestAgeAvgMs),
		static_cast<uint64>(ullActiveNoRequestAgeMaxMs),
		static_cast<uint64>(ullActiveNoRequestLastAcceptedAgeMaxMs),
		static_cast<uint64>(ullActiveNoRequestZeroMaxMs),
		iActiveQueuedRequestClients,
		iActivePendingIOClients,
		iActiveBufferedPayloadClients,
		iActiveSocketBacklogClients,
		static_cast<uint64>(ullCooldownMinMs),
		static_cast<uint64>(ullCooldownAvgMs),
		static_cast<uint64>(ullCooldownMaxMs),
		static_cast<UINT>(m_uploadRetryCooldownByIP.size()),
		static_cast<UINT>(m_noRequestUploadRetryCooldownByIP.size()),
		sharedPublish.iSharedFiles,
		sharedPublish.uED2KPublishedFiles,
		sharedPublish.uED2KPendingFiles,
		sharedPublish.uED2KPendingLargeUnsupportedFiles,
		sharedPublish.uED2KOfferLimit,
		sharedPublish.uKadPublishReady,
		sharedPublish.uKadSourceDueFiles,
		sharedPublish.uKadSourceBackoffFiles,
		sharedPublish.uKadSourceSearches,
		sharedPublish.uKadSourceSearchCap,
		sharedPublish.uKadKeywordSearches,
		sharedPublish.uKadKeywordSearchCap,
		sharedPublish.uKadNotesSearches,
		sharedPublish.uKadNotesSearchCap,
		iThrottlerSlots,
		m_MaxActiveClientsShortTime,
		GetBroadbandSlotCap(),
		GetConfiguredUploadBudgetBytesPerSec(),
		GetTargetClientDataRateBroadband(),
		GetToNetworkDatarate(),
		GetDatarate(),
		static_cast<UINT>(IsBroadbandUploadUnderfilled()),
		static_cast<uint64>(ullUnderfillAgeMs),
		static_cast<UINT>(ShouldTrackSlowUploadSlots()));

	UINT uSlot = 0;
	for (POSITION pos = uploadinglist.GetHeadPosition(); pos != NULL;) {
		++uSlot;
		const UploadingToClient_Struct *pCurClientStruct = uploadinglist.GetNext(pos);
		CUpDownClient *client = pCurClientStruct != NULL ? pCurClientStruct->m_pClient : NULL;
		if (!IsLiveUploadQueueClient(client)) {
			UploadSlotDiagnosticsLogLine(
				_T("UploadSlotDiagnostics: slot=%u live=0 client=%p struct=%p retired=%u pendingIO=%ld"),
				uSlot,
				static_cast<const void*>(client),
				static_cast<const void*>(pCurClientStruct),
				pCurClientStruct != NULL ? static_cast<UINT>(pCurClientStruct->m_bRetired) : 0,
				pCurClientStruct != NULL ? pCurClientStruct->m_nPendingIOBlocks.load() : 0L);
			continue;
		}

		INT_PTR iReqBlocks = 0;
		INT_PTR iDoneBlocks = 0;
		LONG nPendingIOBlocks = 0;
		ULONGLONG ullReqBlocksAccepted = 0;
		ULONGLONG ullReqBlocksDuplicateDone = 0;
		ULONGLONG ullReqBlocksDuplicateQueued = 0;
		ULONGLONG ullReqBlocksRejected = 0;
		ULONGLONG ullReqBlockPacketSignals = 0;
		ULONGLONG ullLastReqBlockAgeMs = 0;
		ULONGLONG ullLastAcceptedReqBlockAgeMs = 0;
		if (pCurClientStruct != NULL) {
			CSingleLock lockBlockLists(&const_cast<UploadingToClient_Struct*>(pCurClientStruct)->m_csBlockListsLock, TRUE);
			ASSERT(lockBlockLists.IsLocked());
			iReqBlocks = pCurClientStruct->m_BlockRequests_queue.GetCount();
			iDoneBlocks = pCurClientStruct->m_DoneBlocks_list.GetCount();
			nPendingIOBlocks = pCurClientStruct->m_nPendingIOBlocks.load();
			ullReqBlocksAccepted = pCurClientStruct->m_ullReqBlocksAccepted.load();
			ullReqBlocksDuplicateDone = pCurClientStruct->m_ullReqBlocksDuplicateDone.load();
			ullReqBlocksDuplicateQueued = pCurClientStruct->m_ullReqBlocksDuplicateQueued.load();
			ullReqBlocksRejected = pCurClientStruct->m_ullReqBlocksRejected.load();
			ullReqBlockPacketSignals = pCurClientStruct->m_ullReqBlockPacketSignals.load();
			const ULONGLONG ullLastReqBlockTick = pCurClientStruct->m_ullLastReqBlockTick.load();
			const ULONGLONG ullLastAcceptedReqBlockTick = pCurClientStruct->m_ullLastAcceptedReqBlockTick.load();
			ullLastReqBlockAgeMs = ullLastReqBlockTick != 0 && curTick >= ullLastReqBlockTick ? curTick - ullLastReqBlockTick : 0;
			ullLastAcceptedReqBlockAgeMs = ullLastAcceptedReqBlockTick != 0 && curTick >= ullLastAcceptedReqBlockTick ? curTick - ullLastAcceptedReqBlockTick : 0;
		}

		CEMSocket *sock = client->GetFileUploadSocket(false);
		const INT_PTR iSocketQueue = sock != NULL ? sock->DbgGetStdQueueCount() : -1;
		const bool bSocketConnected = sock != NULL && sock->IsConnected();
		const ULONGLONG ullAgeMs = client->GetUpStartTimeDelay();

		UploadSlotDiagnosticsLogLine(
			_T("UploadSlotDiagnostics: slot=%u live=1 client=%s state=%s socket=%p socketConnected=%u handshake=%u rateBytesPerSec=%u ageMs=%I64u sessionUp=%s queuePayload=%s queueAdded=%s payloadInBuffer=%s reqBlocks=%Id doneBlocks=%Id pendingIO=%ld socketStdQueue=%Id reqAccepted=%I64u reqDupDone=%I64u reqDupQueued=%I64u reqRejected=%I64u reqSignals=%I64u reqLastAgeMs=%I64u reqLastAcceptedAgeMs=%I64u slowMs=%I64u zeroMs=%I64u cooldownMs=%I64u fileKnown=%u"),
			uSlot,
			(LPCTSTR)client->DbgGetClientInfo(),
			client->DbgGetUploadState(),
			static_cast<void*>(sock),
			static_cast<UINT>(bSocketConnected),
			static_cast<UINT>(client->CheckHandshakeFinished()),
			client->GetUploadDatarate(),
			static_cast<uint64>(ullAgeMs),
			(LPCTSTR)CastItoXBytes(client->GetSessionUp()),
			(LPCTSTR)CastItoXBytes(client->GetQueueSessionPayloadUp()),
			(LPCTSTR)CastItoXBytes(client->GetQueueSessionUploadAdded()),
			(LPCTSTR)CastItoXBytes(client->GetPayloadInBuffer()),
			iReqBlocks,
			iDoneBlocks,
			nPendingIOBlocks,
			iSocketQueue,
			static_cast<uint64>(ullReqBlocksAccepted),
			static_cast<uint64>(ullReqBlocksDuplicateDone),
			static_cast<uint64>(ullReqBlocksDuplicateQueued),
			static_cast<uint64>(ullReqBlocksRejected),
			static_cast<uint64>(ullReqBlockPacketSignals),
			static_cast<uint64>(ullLastReqBlockAgeMs),
			static_cast<uint64>(ullLastAcceptedReqBlockAgeMs),
			static_cast<uint64>(client->GetAccumulatedSlowUploadMs()),
			static_cast<uint64>(client->GetAccumulatedZeroUploadMs()),
			static_cast<uint64>(client->GetSlowUploadCooldownRemaining()),
			static_cast<UINT>(theApp.sharedfiles->GetFileByID(client->GetUploadFileID()) != NULL));
	}
}
#endif

/**
 * Maintenance method for the uploading slots. It adds and removes clients to the
 * uploading list. It also makes sure that all the uploading slots' Sockets
 * always have enough packets in their queues, etc.
 *
 * This method is called approximately once every 100 milliseconds.
 */
void CUploadQueue::Process()
{
	const ULONGLONG curTick = ::GetTickCount64();
	PurgeExpiredUploadRetryCooldowns(curTick);
	UpdateActiveClientsInfo(curTick);
	UpdateBroadbandUnderfillState(curTick);
#ifdef EMULEBB_ENABLE_UPLOAD_SLOT_DIAGNOSTICS
	LogUploadSlotInstrumentation(curTick);
#endif

	if (ForceNewClient())
		// There's not enough open uploads. Open another one.
		AddUpNextClient(_T("Not enough open upload slots for the current speed"));

	// The loop that feeds the upload slots with data.
	for (POSITION pos = uploadinglist.GetHeadPosition(); pos != NULL;) {
		POSITION curPos = pos;
		// Get the client. Note! Also updates pos as a side effect.
		UploadingToClient_Struct *pCurClientStruct = uploadinglist.GetNext(pos);
		CUpDownClient *cur_client = pCurClientStruct->m_pClient;
		if (!IsLiveUploadQueueClient(cur_client)) {
			RetireStaleUploadClientStruct(curPos, pCurClientStruct);
			continue;
		}
		if (thePrefs.m_iDbgHeap >= 2)
			ASSERT_VALID(cur_client);
		// Any active uploader without a live socket is already outside the normal
		// slot lifecycle and must be retired before broadband recycle/rotation
		// logic evaluates the remaining active slots.
		if (cur_client->socket == NULL) {
			if (ShouldCooldownNoSocketUploadSlot(
					true,
					cur_client->GetFriendSlot(),
					GetUploadRetryCooldownIP(cur_client),
					cur_client->GetUpStartTimeDelay(),
					cur_client->GetQueueSessionPayloadUp()))
			{
				// WHY: peers which reach the upload list without a socket are
				// removed immediately, but without a retry cooldown they can keep
				// re-consuming broadband admission attempts while contributing no
				// upload capacity.
				const ULONGLONG ullCooldownUntil = curTick + SEC2MS(GetUploadChurnRetryCooldownSecondsForBudget(
					thePrefs.GetSlowUploadCooldownSeconds(),
					GetConfiguredUploadBudgetBytesPerSec()));
				cur_client->SetSlowUploadCooldownUntil(ullCooldownUntil);
				SetUploadRetryCooldown(cur_client, ullCooldownUntil, uploadRetryCooldownNoSocket);
				EMULEBB_BAD_PEER_LOG_CLIENT_EVENT(_T("upload_no_socket_cooldown"), _T("low"), cur_client, _T("cooldown"), _T("Upload slot reached without socket"), theApp.sharedfiles->GetFileByID(cur_client->GetUploadFileID()));
				if (thePrefs.GetLogUlDlEvents())
					AddDebugLogLine(DLP_LOW, false, _T("%s: Upload retry cooled down after no-socket upload slot removal."), cur_client->GetUserName());
			}
			RemoveFromUploadQueue(cur_client, _T("Uploading to client without socket? (CUploadQueue::Process)"));
			if (cur_client->Disconnected(_T("CUploadQueue::Process"))) {
				UpDownClientDeleteSeams::AssertReadyToDelete(cur_client, _T("CUploadQueue::Process"));
				delete cur_client;
			}
		} else {
			cur_client->UpdateUploadingStatisticsData();
			if (pCurClientStruct->m_bIOError) {
				RemoveFromUploadQueue(cur_client, _T("IO/Other Error while creating data packet (see earlier log entries)"), true);
				continue;
			}
			CString strRemovalReason;
			bool bRequeue = true;
			if (CheckForTimeOver(cur_client, &strRemovalReason, &bRequeue)) {
				RemoveFromUploadQueue(cur_client, strRemovalReason.IsEmpty() ? _T("Completed transfer") : (LPCTSTR)strRemovalReason, true);
				if (bRequeue)
					cur_client->SendOutOfPartReqsAndAddToWaitingQueue();
				continue;
			}
			// Increase the sockets buffer for fast uploads (was in UpdateUploadingStatisticsData()).
			// This should be done in the throttling thread, but the throttler
			// does not have access to the client's download rate
			if (ShouldUseBigSendBuffer(cur_client->GetUploadDatarate())) {
				CEMSocket *sock = cur_client->GetFileUploadSocket();
				if (sock)
					sock->UseBigSendBuffer(GetUploadSocketSendBufferBytes(), GetUploadSocketStandardQueueBytes());
			}

			// check if the file id of the topmost block request matches the current upload file, otherwise
			// the IO thread will wait for us (only for this client of course) to fix it for cross-thread sync reasons
			CSingleLock lockBlockLists(&pCurClientStruct->m_csBlockListsLock, TRUE);
			ASSERT(lockBlockLists.IsLocked());
			// be careful what functions to call while having locks, RemoveFromUploadQueue could,
			// for example, lead to a deadlock here because it tries to get the uploadlist lock,
			// while the IO thread tries to fetch the uploadlist lock and then the blocklist lock
			if (!pCurClientStruct->m_BlockRequests_queue.IsEmpty()) {
				const Requested_Block_Struct *pHeadBlock = pCurClientStruct->m_BlockRequests_queue.GetHead();
				if (!md4equ(pHeadBlock->FileID, cur_client->GetUploadFileID())) {
					uchar aucNewID[MDX_DIGEST_SIZE];
					md4cpy(aucNewID, pHeadBlock->FileID);

					lockBlockLists.Unlock();

					CKnownFile *pCurrentUploadFile = theApp.sharedfiles->GetFileByID(aucNewID);
					if (pCurrentUploadFile != NULL)
						cur_client->SetUploadFileID(pCurrentUploadFile);
					else
						RemoveFromUploadQueue(cur_client, _T("Requested FileID in block request not found in shared files"), true);
				}
			}
		}
	}

	// Save used bandwidth for speed calculations
	(void)theApp.uploadBandwidthThrottler->GetNumberOfSentBytesOverheadSinceLastCallAndReset(); //reset only
	uint64 sentBytes = theApp.uploadBandwidthThrottler->GetNumberOfSentBytesSinceLastCallAndReset();
	m_average_ur_sum += sentBytes;

	average_ur_hist.AddTail(AverageUploadRate{sentBytes, theStats.sessionSentBytesToFriend, curTick});
	// keep no more than 30 secs of data
	while (average_ur_hist.Count() > 3 && curTick >= average_ur_hist.Head().timestamp + SEC2MS(30)) {
		m_average_ur_sum -= average_ur_hist.Head().upBytes;
		average_ur_hist.RemoveHead();
	}

	ReclaimRetiredUploadClientStructs();
};

// check if we can allow a new client to start downloading from us
bool CUploadQueue::AcceptNewClient(INT_PTR curUploadSlots) const
{
	// Broadband stabilization keeps one fixed-cap admission path:
	// maintain at least the historic safety minimum, never exceed the absolute
	// hard ceiling, and otherwise stop growth at the configured broadband cap.
	if (curUploadSlots < MIN_UP_CLIENTS_ALLOWED)
		return true;
	if (curUploadSlots >= MAX_UP_CLIENTS_ALLOWED)
		return false;

	const INT_PTR iSoftMaxSlots = GetSoftMaxUploadSlots();
	return curUploadSlots < iSoftMaxSlots;
}

uint32 CUploadQueue::GetTargetClientDataRate(bool bMinDatarate) const
{
	const uint32 uBroadbandTarget = GetTargetClientDataRateBroadband();
	return bMinDatarate ? max(3u * 1024u, uBroadbandTarget * 3 / 4) : uBroadbandTarget;
}

bool CUploadQueue::ForceNewClient(bool allowEmptyWaitingQueue)
{
	const ULONGLONG curTick = ::GetTickCount64();
	const bool bHasAdmissionCandidate = HasUploadAdmissionCandidate(curTick);
	const bool bHasCooldownProbeCandidate = !bHasAdmissionCandidate && HasUploadCooldownProbeCandidate(curTick);
	if (!ShouldAttemptUploadSlotAdmission(allowEmptyWaitingQueue, waitinglist.IsEmpty(), bHasAdmissionCandidate || bHasCooldownProbeCandidate))
		return false;

	INT_PTR curUploadSlots = uploadinglist.GetCount();
	if (curUploadSlots < MIN_UP_CLIENTS_ALLOWED)
		return true;

	if (curTick < m_nLastStartUpload + SEC2MS(1) && datarate < 102400)
		return false;

	// Underfill no longer opens overflow slots on this branch. It only helps
	// justify replacing an already-open weak slot elsewhere in the controller.
	if (!AcceptNewClient(curUploadSlots))
		return false;

	return true;
}

uint32 CUploadQueue::GetConfiguredUploadBudgetBytesPerSec() const
{
	// This stabilization branch always works from one finite configured upload
	// limit. There is no separate "capacity" or "unlimited upload" mode left in
	// slot control, so every admission and recycle decision derives from this.
	return thePrefs.GetMaxUpload() * 1024u;
}

INT_PTR CUploadQueue::GetSoftMaxUploadSlots() const
{
	return (INT_PTR)max((INT_PTR)MIN_UP_CLIENTS_ALLOWED, (INT_PTR)thePrefs.GetMaxUploadClientsAllowed());
}

uint32 CUploadQueue::GetTargetClientDataRateBroadband() const
{
	const INT_PTR iSoftMaxSlots = GetSoftMaxUploadSlots();
	const uint32 uBudgetBytesPerSec = GetConfiguredUploadBudgetBytesPerSec();
	return max(3u * 1024u, uBudgetBytesPerSec / static_cast<uint32>(iSoftMaxSlots));
}

/**
 * Returns the datarate gap that must remain before broadband policy opens or recycles a slot.
 */
uint32 CUploadQueue::GetBroadbandUnderfillMarginBytesPerSec() const
{
	const uint32 uBudgetBytesPerSec = GetConfiguredUploadBudgetBytesPerSec();
	return ::GetBroadbandUnderfillMarginBytesPerSec(uBudgetBytesPerSec);
}

/**
 * Returns whether the configured upload budget is underfilled enough to justify slot churn.
 */
bool CUploadQueue::IsBroadbandUploadUnderfilled() const
{
	const uint32 uBudgetBytesPerSec = GetConfiguredUploadBudgetBytesPerSec();
	return GetToNetworkDatarate() + GetBroadbandUnderfillMarginBytesPerSec() < uBudgetBytesPerSec;
}

void CUploadQueue::UpdateBroadbandUnderfillState(ULONGLONG curTick)
{
	if (IsBroadbandUploadUnderfilled()) {
		if (m_ullBroadbandUnderfillSince == 0)
			m_ullBroadbandUnderfillSince = curTick;
	} else {
		m_ullBroadbandUnderfillSince = 0;
	}
}

bool CUploadQueue::HasSustainedBroadbandUnderfill(ULONGLONG curTick) const
{
	return m_ullBroadbandUnderfillSince != 0 && curTick >= m_ullBroadbandUnderfillSince + SEC2MS(2);
}

bool CUploadQueue::HasCompletedSlowUploadWarmup(const CUpDownClient *client) const
{
	if (client == NULL || client->GetUploadState() != US_UPLOADING)
		return false;

	// Warm-up protects fresh slots from being judged on startup noise; callers
	// reset the accumulated slow counters until this window has elapsed.
	const UINT uWarmupSeconds = GetSlowUploadWarmupSecondsForBudget(
		thePrefs.GetSlowUploadWarmupSeconds(),
		GetConfiguredUploadBudgetBytesPerSec());
	return uWarmupSeconds == 0 || client->GetUpStartTimeDelay() >= SEC2MS(uWarmupSeconds);
}

uint32 CUploadQueue::GetSlowUploadRateThreshold() const
{
	const uint32 uTargetPerSlot = GetTargetClientDataRateBroadband();
	if (uTargetPerSlot == 0)
		return 3 * 1024;

	const float fFactor = max(0.05f, thePrefs.GetSlowUploadThresholdFactor());
	return max(1024u, static_cast<uint32>(static_cast<float>(uTargetPerSlot) * fFactor));
}

uint32 CUploadQueue::GetUploadBufferBlockCount(uint32 uClientDatarate) const
{
	const uint32 uTargetPerSlot = GetTargetClientDataRateBroadband();
	return GetBroadbandUploadBufferBlockCount(uTargetPerSlot, uClientDatarate);
}

bool CUploadQueue::ShouldUseBigSendBuffer(uint32 uClientDatarate) const
{
	const uint32 uTargetPerSlot = GetTargetClientDataRateBroadband();
	return ShouldUseBroadbandBigSendBuffer(uTargetPerSlot, uClientDatarate);
}

uint32 CUploadQueue::GetUploadSocketSendBufferBytes() const
{
	return GetBroadbandTcpUploadSendBufferBytes(GetTargetClientDataRateBroadband());
}

uint64 CUploadQueue::GetUploadSocketStandardQueueBytes() const
{
	return GetBroadbandEMSocketQueuedStandardBytes(GetTargetClientDataRateBroadband());
}

bool CUploadQueue::ShouldTrackSlowUploadSlots() const
{
	if (waitinglist.IsEmpty())
		return false;
	const INT_PTR iSoftMaxSlots = GetSoftMaxUploadSlots();
	if (uploadinglist.GetCount() < iSoftMaxSlots)
		return false;
	if (m_iHighestNumberOfFullyActivatedSlotsSinceLastCall < min(uploadinglist.GetCount(), iSoftMaxSlots))
		return false;

	// Weak-slot recycling is intentionally narrow: only once the cap is already
	// filled with real uploads and the line still stays materially underfilled.
	return HasSustainedBroadbandUnderfill(::GetTickCount64());
}

uint32 CUploadQueue::GetUploadRetryCooldownIP(const CUpDownClient *client)
{
	if (client == NULL)
		return 0;
	const uint32 dwConnectIP = client->GetConnectIP();
	return dwConnectIP != 0 ? dwConnectIP : client->GetIP();
}

bool CUploadQueue::ApplyUploadRetryCooldown(CUpDownClient *client, ULONGLONG curTick)
{
	const uint32 dwCooldownIP = GetUploadRetryCooldownIP(client);
	if (dwCooldownIP == 0)
		return false;

	ULONGLONG ullRetryCooldownUntil = 0;
	std::map<uint32, UploadRetryCooldownState>::iterator itCooldown = m_uploadRetryCooldownByIP.find(dwCooldownIP);
	if (itCooldown != m_uploadRetryCooldownByIP.end()) {
		ullRetryCooldownUntil = itCooldown->second.ullCooldownUntil;
		if (!ShouldApplyUploadRetryCooldown(client->GetFriendSlot(), dwCooldownIP, curTick, ullRetryCooldownUntil)
			&& itCooldown->second.ullTrackUntil <= curTick)
		{
			m_uploadRetryCooldownByIP.erase(itCooldown);
			ullRetryCooldownUntil = 0;
		}
	}

	ULONGLONG ullNoRequestCooldownUntil = 0;
	// WHY: a repeated no-request peer may reconnect through a fresh client
	// object. Apply the IP-scoped no-request map here so direct admission cannot
	// bypass the cooldown seeded by the previous drained slot.
	std::map<uint32, NoRequestUploadRetryCooldownState>::iterator itNoRequest = m_noRequestUploadRetryCooldownByIP.find(dwCooldownIP);
	if (itNoRequest != m_noRequestUploadRetryCooldownByIP.end()) {
		ullNoRequestCooldownUntil = itNoRequest->second.ullCooldownUntil;
		if (!ShouldApplyUploadRetryCooldown(client->GetFriendSlot(), dwCooldownIP, curTick, ullNoRequestCooldownUntil)
			&& itNoRequest->second.ullTrackUntil <= curTick)
		{
			m_noRequestUploadRetryCooldownByIP.erase(itNoRequest);
			ullNoRequestCooldownUntil = 0;
		}
	}

	const ULONGLONG ullSelectedCooldownUntil = SelectUploadRetryCooldownUntil(
		client->GetFriendSlot(),
		dwCooldownIP,
		curTick,
		ullRetryCooldownUntil,
		ullNoRequestCooldownUntil);
	if (ullSelectedCooldownUntil == 0)
		return false;

	client->SetSlowUploadCooldownUntil(ullSelectedCooldownUntil);
	return true;
}

bool CUploadQueue::HasUploadAdmissionCandidate(ULONGLONG curTick)
{
	for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		CUpDownClient *cur_client = waitinglist.GetNext(pos);
		if (!IsLiveUploadQueueClient(cur_client)) {
			RemoveStaleWaitingClient(pos2);
			continue;
		}
		if (IsUploadQueueAdmissionCandidate(ApplyUploadRetryCooldown(cur_client, curTick) || cur_client->IsInSlowUploadCooldown()))
			return true;
	}
	return false;
}

bool CUploadQueue::HasUploadCooldownProbeCandidate(ULONGLONG curTick)
{
	if (!ShouldProbeUploadCooldownCandidate(
			HasSustainedBroadbandUnderfill(curTick),
			uploadinglist.GetCount(),
			GetSoftMaxUploadSlots()))
	{
		return false;
	}

	for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		CUpDownClient *cur_client = waitinglist.GetNext(pos);
		if (!IsLiveUploadQueueClient(cur_client)) {
			RemoveStaleWaitingClient(pos2);
			continue;
		}
		if (!IsUploadQueueAdmissionCandidate(ApplyUploadRetryCooldown(cur_client, curTick) || cur_client->IsInSlowUploadCooldown())
			&& CanProbeUploadCooldownCandidate(cur_client, curTick))
		{
			return true;
		}
	}
	return false;
}

bool CUploadQueue::CanProbeUploadCooldownCandidate(CUpDownClient *client, ULONGLONG curTick) const
{
	if (client == NULL || client->GetSlowUploadCooldownRemaining() == 0)
		return false;

	const uint32 dwCooldownIP = GetUploadRetryCooldownIP(client);
	if (dwCooldownIP == 0)
		return true;

	const std::map<uint32, NoRequestUploadRetryCooldownState>::const_iterator itNoRequest =
		m_noRequestUploadRetryCooldownByIP.find(dwCooldownIP);
	if (itNoRequest != m_noRequestUploadRetryCooldownByIP.end()
		&& itNoRequest->second.ullCooldownUntil > curTick)
	{
		const ULONGLONG ullCooldownRemainingMs = itNoRequest->second.ullCooldownUntil - curTick;
		// WHY: productive no-request peers have already contributed payload and
		// often ask for another burst shortly after draining. Under severe
		// broadband underfill, allow a near-expiry productive probe while keeping
		// unproductive repeat no-request cooldowns as hard admission gates.
		return ShouldProbeNoRequestCooldownCandidate(
			itNoRequest->second.bProductiveRecycle,
			ullCooldownRemainingMs,
			kProductiveNoRequestCooldownProbeRemainingMs,
			ShouldProbeUploadCooldownCandidate(HasSustainedBroadbandUnderfill(curTick), uploadinglist.GetCount(), GetSoftMaxUploadSlots()));
	}

	return true;
}

void CUploadQueue::SetUploadRetryCooldown(CUpDownClient *client, ULONGLONG ullCooldownUntil, UploadRetryCooldownReason eReason)
{
	const uint32 dwCooldownIP = GetUploadRetryCooldownIP(client);
	const ULONGLONG curTick = ::GetTickCount64();
	if (!ShouldApplyUploadRetryCooldown(client != NULL && client->GetFriendSlot(), dwCooldownIP, curTick, ullCooldownUntil))
		return;

	bool bQueuedRequestClearUsed = false;
	std::map<uint32, UploadRetryCooldownState>::const_iterator itCooldown = m_uploadRetryCooldownByIP.find(dwCooldownIP);
	if (itCooldown != m_uploadRetryCooldownByIP.end() && itCooldown->second.ullTrackUntil > curTick)
		bQueuedRequestClearUsed = itCooldown->second.bQueuedRequestClearUsed;

	// WHY: stalled, no-socket, and failed-admission peers can otherwise clear a
	// retry cooldown with one valid queued request, immediately churn another
	// bad slot, and repeat. Preserve the clear-used marker for the active
	// cooldown window while still allowing one proof-of-demand retry.
	UploadRetryCooldownState state = {};
	state.ullCooldownUntil = ullCooldownUntil;
	state.ullTrackUntil = ullCooldownUntil;
	state.bQueuedRequestClearUsed = bQueuedRequestClearUsed;
	state.eReason = eReason;
	m_uploadRetryCooldownByIP[dwCooldownIP] = state;
}

bool CUploadQueue::HasRecentNoRequestUploadRetryCooldown(CUpDownClient *client, ULONGLONG curTick) const
{
	const uint32 dwCooldownIP = GetUploadRetryCooldownIP(client);
	if (dwCooldownIP == 0)
		return false;

	std::map<uint32, NoRequestUploadRetryCooldownState>::const_iterator itNoRequest = m_noRequestUploadRetryCooldownByIP.find(dwCooldownIP);
	return itNoRequest != m_noRequestUploadRetryCooldownByIP.end()
		&& itNoRequest->second.ullTrackUntil > curTick;
}

void CUploadQueue::SetNoRequestUploadRetryCooldown(CUpDownClient *client, ULONGLONG ullCooldownUntil, ULONGLONG ullTrackUntil, bool bProductiveRecycle)
{
	const uint32 dwCooldownIP = GetUploadRetryCooldownIP(client);
	if (dwCooldownIP == 0)
		return;

	const ULONGLONG curTick = ::GetTickCount64();
	bool bQueuedRequestClearUsed = false;
	std::map<uint32, NoRequestUploadRetryCooldownState>::const_iterator itNoRequest = m_noRequestUploadRetryCooldownByIP.find(dwCooldownIP);
	if (itNoRequest != m_noRequestUploadRetryCooldownByIP.end() && itNoRequest->second.ullTrackUntil > curTick)
		bQueuedRequestClearUsed = itNoRequest->second.bQueuedRequestClearUsed;

	NoRequestUploadRetryCooldownState state = {};
	state.ullCooldownUntil = ullCooldownUntil;
	state.ullTrackUntil = ullTrackUntil;
	state.bQueuedRequestClearUsed = bQueuedRequestClearUsed;
	state.bProductiveRecycle = bProductiveRecycle;
	m_noRequestUploadRetryCooldownByIP[dwCooldownIP] = state;
}

CUploadQueue::NoRequestRepeatHashKey CUploadQueue::GetNoRequestRepeatHashKey(const CUpDownClient *client)
{
	NoRequestRepeatHashKey key = {};
	key.aucHash.fill(0);
	if (client != NULL && client->HasValidHash())
		md4cpy(key.aucHash.data(), client->GetUserHash());
	return key;
}

CUploadQueue::NoRequestRepeatPenalty CUploadQueue::TrackNoRequestRepeatOffender(CUpDownClient *client, ULONGLONG curTick, UINT uBaseCooldownSeconds)
{
	NoRequestRepeatPenalty penalty = {};
	const uint32 dwCooldownIP = GetUploadRetryCooldownIP(client);
	const ULONGLONG ullWindowUntil = curTick + SEC2MS(kNoRequestRepeatStrikeWindowSeconds);
	if (client != NULL && client->HasValidHash()) {
		penalty.bHashScoped = true;
		const NoRequestRepeatHashKey key = GetNoRequestRepeatHashKey(client);
		NoRequestRepeatOffenderState &state = m_noRequestRepeatOffendersByHash[key];
		if (state.ullWindowUntil <= curTick)
			state.uStrikes = 0;
		state.ullWindowUntil = ullWindowUntil;
		penalty.uStrikes = ++state.uStrikes;

		if (dwCooldownIP != 0) {
			NoRequestRepeatIPHashState &ipHashState = m_noRequestRepeatHashesByIP[dwCooldownIP];
			if (ipHashState.ullWindowUntil <= curTick) {
				ipHashState.hashKeys.clear();
				ipHashState.uTotalStrikes = 0;
			}
			ipHashState.ullWindowUntil = ullWindowUntil;
			++ipHashState.uTotalStrikes;
			ipHashState.hashKeys.insert(key);
			penalty.uDistinctIPHashes = static_cast<UINT>(ipHashState.hashKeys.size());
			penalty.uIPRotationStrikes = ipHashState.uTotalStrikes;
			penalty.bShouldIPBan = penalty.uDistinctIPHashes >= kNoRequestRepeatHashRotationBanThreshold
				&& penalty.uIPRotationStrikes >= kNoRequestRepeatHashRotationStrikeThreshold;
		}
	} else if (dwCooldownIP != 0) {
		NoRequestRepeatOffenderState &state = m_noRequestRepeatOffendersByIP[dwCooldownIP];
		if (state.ullWindowUntil <= curTick)
			state.uStrikes = 0;
		state.ullWindowUntil = ullWindowUntil;
		penalty.uStrikes = ++state.uStrikes;
	}

	penalty.bShouldBan = ShouldBanNoRequestRepeatOffender(penalty.uStrikes);
	penalty.uCooldownSeconds = penalty.bShouldBan || penalty.bShouldIPBan
		? 0
		: GetNoRequestRepeatCooldownSeconds(uBaseCooldownSeconds, penalty.uStrikes);
	return penalty;
}

bool CUploadQueue::ClearUploadRetryCooldown(CUpDownClient *client, LPCTSTR *ppszInstrumentationReason)
{
	if (ppszInstrumentationReason != NULL)
		*ppszInstrumentationReason = NULL;

	const uint32 dwCooldownIP = GetUploadRetryCooldownIP(client);
	const bool bHadClientCooldown = client != NULL && client->IsInSlowUploadCooldown();
	bool bHadIPCooldown = false;
	bool bHadNoRequestCooldown = false;
	bool bClearedProductiveNoRequestCooldown = false;
	if (dwCooldownIP != 0) {
		const ULONGLONG curTick = ::GetTickCount64();
		std::map<uint32, NoRequestUploadRetryCooldownState>::iterator itNoRequest = m_noRequestUploadRetryCooldownByIP.find(dwCooldownIP);
		if (itNoRequest != m_noRequestUploadRetryCooldownByIP.end()) {
			if (itNoRequest->second.ullTrackUntil <= curTick) {
				m_noRequestUploadRetryCooldownByIP.erase(itNoRequest);
			} else if (itNoRequest->second.ullCooldownUntil > curTick) {
				bHadNoRequestCooldown = true;
				const bool bProductiveNoRequestRecycle = itNoRequest->second.bProductiveRecycle;
				if (!ShouldAllowNoRequestCooldownClear(true, itNoRequest->second.bQueuedRequestClearUsed)) {
					if (ppszInstrumentationReason != NULL)
						*ppszInstrumentationReason = _T("reject-not-uploading-no-request-clear-used");
					return false;
				}
				itNoRequest->second.bQueuedRequestClearUsed = true;
				if (bProductiveNoRequestRecycle) {
					itNoRequest->second.ullCooldownUntil = curTick;
					bClearedProductiveNoRequestCooldown = true;
				}
			}
		}
		if (ShouldBlockQueuedRequestRetryClearForActiveNoRequest(bHadNoRequestCooldown, bClearedProductiveNoRequestCooldown)) {
			// WHY: unproductive no-request slots have not proven renewed upload
			// demand. Do not let a queued block request or open-cap underfill
			// bypass the still-active repeat no-request throttle.
			if (ppszInstrumentationReason != NULL)
				*ppszInstrumentationReason = _T("reject-not-uploading-unproductive-no-request-active");
			return false;
		}
		std::map<uint32, UploadRetryCooldownState>::iterator itCooldown = m_uploadRetryCooldownByIP.find(dwCooldownIP);
		if (itCooldown != m_uploadRetryCooldownByIP.end()) {
			if (itCooldown->second.ullTrackUntil <= curTick) {
				m_uploadRetryCooldownByIP.erase(itCooldown);
			} else if (itCooldown->second.ullCooldownUntil > curTick) {
				if (!ShouldAllowUploadRetryCooldownClear(true, itCooldown->second.bQueuedRequestClearUsed)) {
					if (ppszInstrumentationReason != NULL)
						*ppszInstrumentationReason = _T("reject-not-uploading-retry-clear-used");
					return false;
				}
				itCooldown->second.bQueuedRequestClearUsed = true;
				itCooldown->second.ullCooldownUntil = curTick;
				bHadIPCooldown = true;
			}
		}
	}
	if (client != NULL)
		client->ClearSlowUploadCooldown();
	if (bHadClientCooldown || bHadIPCooldown || bClearedProductiveNoRequestCooldown)
		return true;
	if (ppszInstrumentationReason != NULL)
		*ppszInstrumentationReason = bHadNoRequestCooldown ? _T("reject-not-uploading-no-request-only-cooldown") : _T("reject-not-uploading-no-active-cooldown");
	return false;
}

QueuedBlockRequestAdmissionResult CUploadQueue::TryAdmitQueuedBlockRequestClient(CUpDownClient *client, bool bQueuedRequestCooldownCleared)
{
	const bool bOnUploadQueue = client != NULL && IsOnUploadQueue(client);
	const bool bAlreadyUploading = client != NULL && IsDownloading(client);
	const bool bCanOpenUploadSlot = AcceptNewClient(uploadinglist.GetCount());
	if (!ShouldAdmitQueuedBlockRequestToUploadSlot(
			bQueuedRequestCooldownCleared,
			bOnUploadQueue,
			bAlreadyUploading,
			bCanOpenUploadSlot))
		return ClassifyQueuedBlockRequestAdmission(
			bQueuedRequestCooldownCleared,
			bOnUploadQueue,
			bAlreadyUploading,
			bCanOpenUploadSlot,
			false,
			false);

	if (!ForceNewClient(true))
		return queuedBlockRequestAdmissionDeferred;

	return ClassifyQueuedBlockRequestAdmission(
		bQueuedRequestCooldownCleared,
		bOnUploadQueue,
		bAlreadyUploading,
		bCanOpenUploadSlot,
		true,
		AddUpNextClient(_T("Direct add after queued block request."), client));
}

void CUploadQueue::PurgeExpiredUploadRetryCooldowns(ULONGLONG curTick)
{
	for (std::map<uint32, UploadRetryCooldownState>::iterator itCooldown = m_uploadRetryCooldownByIP.begin(); itCooldown != m_uploadRetryCooldownByIP.end();) {
		if (itCooldown->second.ullTrackUntil <= curTick)
			itCooldown = m_uploadRetryCooldownByIP.erase(itCooldown);
		else
			++itCooldown;
	}
	for (std::map<uint32, NoRequestUploadRetryCooldownState>::iterator itCooldown = m_noRequestUploadRetryCooldownByIP.begin(); itCooldown != m_noRequestUploadRetryCooldownByIP.end();) {
		if (itCooldown->second.ullTrackUntil <= curTick)
			itCooldown = m_noRequestUploadRetryCooldownByIP.erase(itCooldown);
		else
			++itCooldown;
	}
	if (m_ullLastNoRequestRepeatCleanup != 0
		&& curTick < m_ullLastNoRequestRepeatCleanup + SEC2MS(kNoRequestRepeatCleanupIntervalSeconds))
	{
		return;
	}
	m_ullLastNoRequestRepeatCleanup = curTick;

	for (std::map<NoRequestRepeatHashKey, NoRequestRepeatOffenderState>::iterator itOffender = m_noRequestRepeatOffendersByHash.begin(); itOffender != m_noRequestRepeatOffendersByHash.end();) {
		if (itOffender->second.ullWindowUntil <= curTick)
			itOffender = m_noRequestRepeatOffendersByHash.erase(itOffender);
		else
			++itOffender;
	}
	for (std::map<uint32, NoRequestRepeatOffenderState>::iterator itOffender = m_noRequestRepeatOffendersByIP.begin(); itOffender != m_noRequestRepeatOffendersByIP.end();) {
		if (itOffender->second.ullWindowUntil <= curTick)
			itOffender = m_noRequestRepeatOffendersByIP.erase(itOffender);
		else
			++itOffender;
	}
	for (std::map<uint32, NoRequestRepeatIPHashState>::iterator itIPHashes = m_noRequestRepeatHashesByIP.begin(); itIPHashes != m_noRequestRepeatHashesByIP.end();) {
		if (itIPHashes->second.ullWindowUntil <= curTick)
			itIPHashes = m_noRequestRepeatHashesByIP.erase(itIPHashes);
		else
			++itIPHashes;
	}
}

bool CUploadQueue::ShouldRecycleIdleUploadSlot(CUpDownClient *client, ULONGLONG curTick, CString *pstrReason)
{
	if (client == NULL || client->GetUploadState() != US_UPLOADING)
		return false;
	if (client->GetFriendSlot())
		return false;

	if (!HasSustainedBroadbandUnderfill(curTick)) {
		client->ResetSlowUploadTracking();
		return false;
	}

	UploadingToClient_Struct *pUploadingClientStruct = GetUploadingClientStructByClient(client);
	if (pUploadingClientStruct == NULL) {
		client->ResetSlowUploadTracking();
		return false;
	}

	INT_PTR iReqBlocks = 0;
	LONG nPendingIOBlocks = 0;
	bool bHasAcceptedReqBlock = false;
	ULONGLONG ullLastAcceptedReqBlockAgeMs = 0;
	{
		CSingleLock lockBlockLists(&pUploadingClientStruct->m_csBlockListsLock, TRUE);
		ASSERT(lockBlockLists.IsLocked());
		iReqBlocks = pUploadingClientStruct->m_BlockRequests_queue.GetCount();
		nPendingIOBlocks = pUploadingClientStruct->m_nPendingIOBlocks.load();
		const ULONGLONG ullLastAcceptedReqBlockTick = pUploadingClientStruct->m_ullLastAcceptedReqBlockTick.load();
		bHasAcceptedReqBlock = ullLastAcceptedReqBlockTick != 0;
		ullLastAcceptedReqBlockAgeMs = ullLastAcceptedReqBlockTick != 0 && curTick >= ullLastAcceptedReqBlockTick ? curTick - ullLastAcceptedReqBlockTick : 0;
	}

	CEMSocket *sock = client->GetFileUploadSocket(false);
	const INT_PTR iSocketQueue = sock != NULL ? sock->DbgGetStdQueueCount() : -1;
	const uint32 uBudgetBytesPerSec = GetConfiguredUploadBudgetBytesPerSec();
	const UINT uEffectiveZeroGraceSeconds = GetZeroUploadGraceSecondsForBudget(thePrefs.GetZeroUploadRateGraceSeconds(), uBudgetBytesPerSec);
	const ULONGLONG ullZeroGraceMs = SEC2MS(uEffectiveZeroGraceSeconds);
	const ULONGLONG ullNoRequestGraceMs = GetNoRequestUploadRecycleGraceMs(uEffectiveZeroGraceSeconds);
	const ULONGLONG ullStalledGraceMs = GetStalledUploadRecycleGraceMs(uEffectiveZeroGraceSeconds);
	if (ShouldRecycleNoRequestBroadbandUploadSlot(
			true,
			false,
			client->GetUploadDatarate(),
			GetSlowUploadRateThreshold(),
			client->GetPayloadInBuffer(),
			iReqBlocks,
			nPendingIOBlocks,
			iSocketQueue,
			client->GetUpStartTimeDelay(),
			bHasAcceptedReqBlock,
			ullLastAcceptedReqBlockAgeMs,
			ullNoRequestGraceMs))
	{
		// WHY: a peer that drains its local send pipeline and stops asking for
		// blocks during broadband underfill cannot fill capacity. Recycle that
		// no-request slot promptly without weakening the broader slow-upload
		// warmup used for peers that still have local work queued.
		const bool bProductiveNoRequestRecycle = IsProductiveNoRequestUploadRecycle(
			client->GetQueueSessionPayloadUp(),
			GetProductiveNoRequestCooldownPayloadBytes(GetTargetClientDataRateBroadband()));
		if (ShouldDeferProductiveNoRequestUploadRecycle(
				bProductiveNoRequestRecycle,
				client->GetUpStartTimeDelay(),
				SEC2MS(GetSlowUploadWarmupSecondsForBudget(thePrefs.GetSlowUploadWarmupSeconds(), uBudgetBytesPerSec))))
		{
			// WHY: productive peers often drain a burst before their next request
			// arrives. Keep them through the normal broadband warmup so fast
			// clients can keep carrying upload bandwidth instead of re-entering
			// immediately after a premature no-request recycle.
			return false;
		}
		if (ShouldCooldownNoRequestUploadRecycle(false)) {
			// WHY: no-request sessions can make an initial burst, drain their
			// pipeline, then immediately re-enter as 0-byte slots. Seed a short
			// retry cooldown for every drained no-request recycle so other
			// candidates get a chance to fill the broadband line.
			// WHY: no-request peers are already constrained to one queued-request
			// cooldown clear per window. Keep their retry suppression short so a
			// sparse public queue can refill capacity, but escalate repeat
			// unproductive no-request recycles to the configured slow-upload
			// cooldown so persistent 0-byte peers stop consuming refill probes.
			const UINT uConfiguredCooldownSeconds = thePrefs.GetSlowUploadCooldownSeconds();
			const uint32 uBudgetBytesPerSec = GetConfiguredUploadBudgetBytesPerSec();
			const UINT uProductiveCooldownSeconds = GetNoRequestUploadRetryCooldownSeconds(
				uConfiguredCooldownSeconds,
				HasRecentNoRequestUploadRetryCooldown(client, curTick),
				true,
				GetNoRequestUploadCooldownMaxSecondsForBudget(uBudgetBytesPerSec),
				GetProductiveNoRequestUploadCooldownMaxSecondsForBudget(uBudgetBytesPerSec),
				GetRepeatedNoRequestUploadCooldownMaxSecondsForBudget(uBudgetBytesPerSec));
			const UINT uBaseCooldownSeconds = GetNoRequestRepeatBaseCooldownSeconds(
				uConfiguredCooldownSeconds,
				GetNoRequestUploadCooldownMaxSecondsForBudget(uBudgetBytesPerSec));
			const UINT uInitialCooldownSeconds = bProductiveNoRequestRecycle ? uProductiveCooldownSeconds : uBaseCooldownSeconds;
			UINT uCooldownSeconds = uInitialCooldownSeconds;
			NoRequestRepeatPenalty repeatPenalty = {};
			if (!bProductiveNoRequestRecycle) {
				repeatPenalty = TrackNoRequestRepeatOffender(client, curTick, uBaseCooldownSeconds);
				if (repeatPenalty.uStrikes != 0 && !repeatPenalty.bShouldBan && !repeatPenalty.bShouldIPBan)
					uCooldownSeconds = repeatPenalty.uCooldownSeconds;
#if EMULEBB_HAS_BAD_PEER_DIAGNOSTICS
				CString strRepeatEvidence;
				LPCTSTR pszRepeatScope = repeatPenalty.bShouldIPBan || !repeatPenalty.bHashScoped ? _T("ip") : _T("hash");
				strRepeatEvidence.Format(
					_T("{\"strikes\":%u,\"threshold\":%u,\"cooldown_seconds\":%u,\"base_cooldown_seconds\":%u,\"window_seconds\":%u,\"key_type\":\"%s\",\"scope\":\"%s\",\"distinct_ip_hashes\":%u,\"ip_rotation_strikes\":%u,\"ip_rotation_strike_threshold\":%u}"),
					repeatPenalty.uStrikes,
					kNoRequestRepeatBanThreshold,
					uCooldownSeconds,
					uBaseCooldownSeconds,
					kNoRequestRepeatStrikeWindowSeconds,
					repeatPenalty.bHashScoped ? _T("hash") : _T("ip"),
					pszRepeatScope,
					repeatPenalty.uDistinctIPHashes,
					repeatPenalty.uIPRotationStrikes,
					kNoRequestRepeatHashRotationStrikeThreshold);
				EMULEBB_BAD_PEER_LOG_CLIENT_EVENT(
					repeatPenalty.bShouldBan || repeatPenalty.bShouldIPBan ? _T("upload_no_request_repeat_ban") : _T("upload_no_request_repeat_cooldown"),
					repeatPenalty.bShouldBan || repeatPenalty.bShouldIPBan ? _T("high") : _T("medium"),
					client,
					repeatPenalty.bShouldBan || repeatPenalty.bShouldIPBan ? _T("ban") : _T("cooldown"),
					repeatPenalty.bShouldIPBan ? _T("Repeated zero-request upload slot abuse with rotating hashes") : _T("Repeated zero-request upload slot abuse"),
					theApp.sharedfiles->GetFileByID(client->GetUploadFileID()),
					strRepeatEvidence);
#endif
				if (repeatPenalty.bShouldBan || repeatPenalty.bShouldIPBan) {
					client->Ban(repeatPenalty.bShouldIPBan
						? _T("Repeated zero-request upload slot abuse with rotating hashes")
						: _T("Repeated zero-request upload slot abuse"),
						repeatPenalty.bShouldIPBan ? clientBanScopeBoth : clientBanScopeHash);
					if (pstrReason != NULL)
						*pstrReason = repeatPenalty.bShouldIPBan
							? _T("Repeated zero-request upload slot abuse with rotating hashes")
							: _T("Repeated zero-request upload slot abuse");
					client->ResetSlowUploadTracking();
					return true;
				}
			}
			const ULONGLONG ullCooldownUntil = curTick + SEC2MS(uCooldownSeconds);
			const ULONGLONG ullTrackUntil = curTick + SEC2MS(GetNoRequestUploadRetryTrackSeconds(uCooldownSeconds, uConfiguredCooldownSeconds));
			client->SetSlowUploadCooldownUntil(ullCooldownUntil);
			SetUploadRetryCooldown(client, ullCooldownUntil, uploadRetryCooldownNoRequest);
			SetNoRequestUploadRetryCooldown(client, ullCooldownUntil, ullTrackUntil, bProductiveNoRequestRecycle);
		}
#if EMULEBB_HAS_BAD_PEER_DIAGNOSTICS
		CString strBadPeerEvidence;
		strBadPeerEvidence.Format(
			_T("{\"productive\":%s,\"request_blocks\":%Id,\"pending_io\":%ld,\"payload_in_buffer\":%I64u,\"socket_queue\":%Id}"),
			bProductiveNoRequestRecycle ? _T("true") : _T("false"),
			iReqBlocks,
			nPendingIOBlocks,
			client->GetPayloadInBuffer(),
			iSocketQueue);
		EMULEBB_BAD_PEER_LOG_CLIENT_EVENT(
			_T("upload_no_request_recycle"),
			bProductiveNoRequestRecycle ? _T("low") : _T("medium"),
			client,
			_T("cooldown"),
			bProductiveNoRequestRecycle ? _T("Broadband productive no-request recycle") : _T("Broadband unproductive no-request recycle"),
			theApp.sharedfiles->GetFileByID(client->GetUploadFileID()),
			strBadPeerEvidence);
#endif
		if (thePrefs.GetLogUlDlEvents())
			AddDebugLogLine(DLP_LOW, false, _T("%s: Upload slot recycled because the peer stopped requesting parts during broadband underfill (%s)."),
				client->GetUserName(),
				bProductiveNoRequestRecycle ? _T("productive") : _T("unproductive"));
		if (pstrReason != NULL)
			*pstrReason = bProductiveNoRequestRecycle ? _T("Broadband productive no-request recycle") : _T("Broadband unproductive no-request recycle");
		client->ResetSlowUploadTracking();
		return true;
	}

	const bool bLocalSendPipelineIdle = client->GetUploadDatarate() == 0
		&& client->GetPayloadInBuffer() == 0
		&& iReqBlocks == 0
		&& nPendingIOBlocks == 0
		&& iSocketQueue == 0;
	const bool bLocalSendPipelineStalled = client->GetUploadDatarate() == 0
		&& nPendingIOBlocks == 0
		&& (client->GetPayloadInBuffer() > 0 || iReqBlocks > 0 || iSocketQueue > 0);
	// WHY: under sustained broadband underfill, a zero-rate slot with either no
	// local work or unsent queued work is not contributing capacity. Keep disk IO
	// in flight out of this path. Stalled queued data still waits for the normal
	// broadband warmup so slow-but-productive peers are not churned while the
	// scheduler is trying to stabilize around the configured slot cap.
	if (bLocalSendPipelineIdle || bLocalSendPipelineStalled)
		client->UpdateSlowUploadTracking(curTick, GetSlowUploadRateThreshold());
	// WHY: nonzero but slow slots are evaluated by the broader slow-rate
	// recycle path below. Resetting the shared accumulator here prevents those
	// long-lived slow slots from ever accruing slow time under broadband
	// underfill.

	const bool bSlowUploadWarmupComplete = HasCompletedSlowUploadWarmup(client);
	const bool bShouldRecycleIdle = ShouldRecycleIdleBroadbandUploadSlot(
		true,
		bSlowUploadWarmupComplete,
		false,
		client->GetUploadDatarate(),
		client->GetPayloadInBuffer(),
		iReqBlocks,
		nPendingIOBlocks,
		iSocketQueue,
		client->GetAccumulatedZeroUploadMs(),
		ullZeroGraceMs);
	const bool bShouldRecycleStalled = ShouldRecycleStalledBroadbandUploadSlot(
		true,
		bSlowUploadWarmupComplete,
		false,
		HasStalledUploadReplacementPressure(!waitinglist.IsEmpty(), uploadinglist.GetCount(), GetSoftMaxUploadSlots()),
		client->GetUploadDatarate(),
		client->GetPayloadInBuffer(),
		iReqBlocks,
		nPendingIOBlocks,
		iSocketQueue,
		client->GetAccumulatedZeroUploadMs(),
		ullStalledGraceMs);
	if (!bShouldRecycleIdle && !bShouldRecycleStalled)
		return false;

	const ULONGLONG ullCooldownUntil = curTick + SEC2MS(GetUploadChurnRetryCooldownSecondsForBudget(
		thePrefs.GetSlowUploadCooldownSeconds(),
		GetConfiguredUploadBudgetBytesPerSec()));
	client->SetSlowUploadCooldownUntil(ullCooldownUntil);
	SetUploadRetryCooldown(client, ullCooldownUntil, bShouldRecycleIdle ? uploadRetryCooldownIdle : uploadRetryCooldownStalled);
#if EMULEBB_HAS_BAD_PEER_DIAGNOSTICS
	CString strBadPeerEvidence;
	strBadPeerEvidence.Format(
		_T("{\"idle\":%s,\"stalled\":%s,\"request_blocks\":%Id,\"pending_io\":%ld,\"payload_in_buffer\":%I64u,\"socket_queue\":%Id,\"zero_ms\":%I64u}"),
		bShouldRecycleIdle ? _T("true") : _T("false"),
		bShouldRecycleStalled ? _T("true") : _T("false"),
		iReqBlocks,
		nPendingIOBlocks,
		client->GetPayloadInBuffer(),
		iSocketQueue,
		client->GetAccumulatedZeroUploadMs());
	EMULEBB_BAD_PEER_LOG_CLIENT_EVENT(
		bShouldRecycleIdle ? _T("upload_idle_no_request_recycle") : _T("upload_stalled_zero_rate_recycle"),
		_T("medium"),
		client,
		_T("cooldown"),
		bShouldRecycleIdle ? _T("Broadband idle no-request recycle") : _T("Broadband stalled zero-rate recycle"),
		theApp.sharedfiles->GetFileByID(client->GetUploadFileID()),
		strBadPeerEvidence);
#endif
	if (thePrefs.GetLogUlDlEvents()) {
		AddDebugLogLine(DLP_LOW, false,
			bShouldRecycleIdle
				? _T("%s: Upload slot recycled because the peer stopped requesting parts during broadband underfill.")
				: _T("%s: Upload slot recycled because queued upload data made no progress during broadband underfill."),
			client->GetUserName());
	}
	if (pstrReason != NULL)
		*pstrReason = bShouldRecycleIdle ? _T("Broadband idle no-request recycle") : _T("Broadband stalled zero-rate recycle");
	client->ResetSlowUploadTracking();
	return true;
}

CUpDownClient* CUploadQueue::GetWaitingClientByIP_UDP(uint32 dwIP, uint16 nUDPPort, bool bIgnorePortOnUniqueIP, bool *pbMultipleIPs)
{
	CUpDownClient *pMatchingIPClient = NULL;
	uint32 cMatches = 0;
	for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		CUpDownClient *cur_client = waitinglist.GetNext(pos);
		if (!IsLiveUploadQueueClient(cur_client)) {
			RemoveStaleWaitingClient(pos2);
			continue;
		}
		if (dwIP == cur_client->GetIP() && nUDPPort == cur_client->GetUDPPort())
			return cur_client;
		if (dwIP == cur_client->GetIP() && bIgnorePortOnUniqueIP && cur_client != pMatchingIPClient) {
			pMatchingIPClient = cur_client;
			++cMatches;
		}
	}
	if (pbMultipleIPs != NULL)
		*pbMultipleIPs = cMatches > 1;

	if (pMatchingIPClient != NULL && cMatches == 1)
		return pMatchingIPClient;
	return NULL;
}

CUpDownClient* CUploadQueue::GetWaitingClientByIP(uint32 dwIP) const
{
	for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;) {
		CUpDownClient *cur_client = waitinglist.GetNext(pos);
		if (!IsLiveUploadQueueClient(cur_client))
			continue;
		if (dwIP == cur_client->GetIP())
			return cur_client;
	}
	return NULL;
}

/**
 * Add a client to the waiting queue for uploads.
 *
 * @param client address of the client that should be added to the waiting queue
 *
 * @param bIgnoreTimelimit don't check time limit to possibly ban the client.
 */
void CUploadQueue::AddClientToQueue(CUpDownClient *client, bool bIgnoreTimelimit)
{
	//This is to keep users from abusing the limits we put on lowID callbacks.
	//1)Check if we are connected to any network and that we are a lowID.
	//  (Although this check shouldn't matter as they wouldn't have found us.
	//  But, maybe I'm missing something, so it's best to check as a precaution.)
	//2)Check if the user is connected to Kad. We do allow all Kad Callbacks.
	//3)Check if the user is in our download list or a friend.
	//  We give these users a special pass as they are helping us.
	//4)Are we connected to a server? If we are, is the user on the same server?
	//  TCP lowID callbacks are also allowed.
	//5)If the queue is very short, allow anyone in as we want to make sure
	//  our upload is always used.
	if (   theApp.IsConnected()
		&& theApp.IsFirewalled()
		&& !client->GetKadPort()
		&& client->GetDownloadState() == DS_NONE
		&& !client->IsFriend()
		&& theApp.serverconnect
		&& !theApp.serverconnect->IsLocalServer(client->GetServerIP(), client->GetServerPort())
		&& GetWaitingUserCount() > 50)
	{
		return;
	}
	client->IncrementAskedCount();
	client->SetLastUpRequest();
	if (!bIgnoreTimelimit)
		client->AddRequestCount(client->GetUploadFileID());
	if (client->IsBanned())
		return;
	uint16 cSameIP = 0;
	// check for duplicates
	for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		CUpDownClient *cur_client = waitinglist.GetNext(pos);
		if (!IsLiveUploadQueueClient(cur_client)) {
			RemoveStaleWaitingClient(pos2);
			continue;
		}
		if (cur_client == client) {
			client->SendRankingInfo();
			client->QueueDisplayUpdate(DISPLAY_REFRESH_QUEUE_LIST);
			return;
		}
		if (client->Compare(cur_client)) {
			theApp.clientlist->AddTrackClient(client); // in any case keep track of this client

			// another client with same ip:port or hash
			// this happens only in rare cases, because same userhash / ip:ports are assigned to the right client on connecting in most cases
			if (cur_client->credits != NULL && cur_client->credits->GetCurrentIdentState(cur_client->GetIP()) == IS_IDENTIFIED) {
				//cur_client has a valid secure hash, don't remove him
				if (thePrefs.GetVerbose())
					AddDebugLogLine(false, (LPCTSTR)GetResString(IDS_SAMEUSERHASH), client->GetUserName(), cur_client->GetUserName(), client->GetUserName());
				return;
			}
			if (client->credits == NULL || client->credits->GetCurrentIdentState(client->GetIP()) != IS_IDENTIFIED) {
				// remove both since we do not know who the bad one is
				if (thePrefs.GetVerbose())
					AddDebugLogLine(false, (LPCTSTR)GetResString(IDS_SAMEUSERHASH), client->GetUserName(), cur_client->GetUserName(), _T("Both"));
				RemoveFromWaitingQueue(pos2, true);
				if (!cur_client->socket && cur_client->Disconnected(_T("AddClientToQueue - same userhash 2"))) {
					UpDownClientDeleteSeams::AssertReadyToDelete(cur_client, _T("CUploadQueue::AddClientToQueue same userhash 2"));
					delete cur_client;
				}
				return;
			}
			//client has a valid secure hash, add him and remove the other one
			if (thePrefs.GetVerbose())
				AddDebugLogLine(false, (LPCTSTR)GetResString(IDS_SAMEUSERHASH), client->GetUserName(), cur_client->GetUserName(), cur_client->GetUserName());
			RemoveFromWaitingQueue(pos2, true);
			if (!cur_client->socket && cur_client->Disconnected(_T("AddClientToQueue - same userhash 1"))) {
				UpDownClientDeleteSeams::AssertReadyToDelete(cur_client, _T("CUploadQueue::AddClientToQueue same userhash 1"));
				delete cur_client;
			}
		} else if (client->GetIP() == cur_client->GetIP()) {
			// same IP, different port, different userhash
			++cSameIP;
		}
	}
	if (cSameIP >= 3) {
		// do not accept more than 3 clients from the same IP
		if (thePrefs.GetVerbose())
			DEBUG_ONLY(AddDebugLogLine(false, _T("%s's (%s) request to enter the queue was rejected, because of too many clients with the same IP"), client->GetUserName(), (LPCTSTR)ipstr(client->GetConnectIP())));
		return;
	}
	if (theApp.clientlist->GetClientsFromIP(client->GetIP()) >= 3) {
		if (thePrefs.GetVerbose())
			DEBUG_ONLY(AddDebugLogLine(false, _T("%s's (%s) request to enter the queue was rejected, because of too many clients with the same IP (found in TrackedClientsList)"), client->GetUserName(), (LPCTSTR)ipstr(client->GetConnectIP())));
		return;
	}
	// done

	// statistic values
	// TODO: Maybe we should change this to count each request for a file only once and ignore re-asks
	CKnownFile *reqfile = theApp.sharedfiles->GetFileByID((uchar*)client->GetUploadFileID());
	if (reqfile)
		reqfile->statistic.AddRequest();

	client->SetCollectionUploadSlot(false);

	// cap the list
	// the queue limit in prefs is only a soft limit. Hard limit is higher up to 25% to accept
	// powershare clients and other high ranking clients after soft limit has been reached
	INT_PTR softQueueLimit = thePrefs.GetQueueSize();
	INT_PTR hardQueueLimit = softQueueLimit + max(softQueueLimit, 800) / 4;

	// if soft queue limit has been reached, only let in high ranking clients
	if (RejectSoftQueueCandidateByCombinedScore(
			waitinglist.GetCount() >= hardQueueLimit,
			waitinglist.GetCount() >= softQueueLimit,
			client->IsFriend() && client->GetFriendSlot(),
			client->GetCombinedFilePrioAndCredit(),
			GetAverageCombinedFilePrioAndCredit()))
	{
		// block client from getting on queue
		return;
	}
	if (client->IsDownloading()) {
		// he's already downloading and probably only wants another file
		if (thePrefs.GetDebugClientTCPLevel() > 0)
			DebugSend("OP_AcceptUploadReq", client);
		Packet *packet = new Packet(OP_ACCEPTUPLOADREQ, 0);
		theStats.AddUpDataOverheadFileRequest(packet->size);
		client->SendPacket(packet);
		return;
	}
	ApplyUploadRetryCooldown(client, ::GetTickCount64());
	if (client->IsInSlowUploadCooldown()) {
		if (thePrefs.GetLogUlDlEvents())
			AddDebugLogLine(DLP_LOW, false, _T("%s: Broadband direct admission blocked by slow-upload cooldown."), client->GetUserName());
		m_bStatisticsWaitingListDirty = true;
		waitinglist.AddTail(client);
		client->SetUploadState(US_ONUPLOADQUEUE);
		client->SetWaitStartTime();
		client->SetAskedCount(1);
		QueueWaitingListDisplayRefresh();
		theApp.emuledlg->transferwnd->ShowQueueCount(waitinglist.GetCount());
		client->SendRankingInfo();
		return;
	}
	const bool bHasWaitingAdmissionCandidate = HasUploadAdmissionCandidate(::GetTickCount64());
	const bool bWaitingListEmpty = waitinglist.IsEmpty();
	const bool bQueueHadNoAdmissionCandidate = ShouldDirectAdmitBehindCooldownOnlyWaitingList(
		bWaitingListEmpty,
		bHasWaitingAdmissionCandidate);
	if ((bWaitingListEmpty || bQueueHadNoAdmissionCandidate) && ForceNewClient(true)) {
		client->SetWaitStartTime();
		AddUpNextClient(
			bQueueHadNoAdmissionCandidate ? _T("Direct add behind cooldown-only queue.") : _T("Direct add with empty queue."),
			client);
	} else {
		if (waitinglist.IsEmpty() && thePrefs.GetLogUlDlEvents() && !AcceptNewClient(uploadinglist.GetCount()))
			AddDebugLogLine(DLP_LOW, false, _T("%s: Broadband direct admission denied because the fixed slot cap is full."), client->GetUserName());
		m_bStatisticsWaitingListDirty = true;
		waitinglist.AddTail(client);
		client->SetUploadState(US_ONUPLOADQUEUE);
		client->SetWaitStartTime();
		client->SetAskedCount(1);
		QueueWaitingListDisplayRefresh();
		theApp.emuledlg->transferwnd->ShowQueueCount(waitinglist.GetCount());
		client->SendRankingInfo();
	}
}

float CUploadQueue::GetAverageCombinedFilePrioAndCredit()
{
	const ULONGLONG curTick = ::GetTickCount64();

	if (curTick >= m_dwLastCalculatedAverageCombinedFilePrioAndCredit + SEC2MS(5)) {
		m_dwLastCalculatedAverageCombinedFilePrioAndCredit = curTick;

		// TODO: is there a risk of overflow? I don't think so...
		float sum = 0;
		UINT count = 0;
		for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;) {
			POSITION pos2 = pos;
			CUpDownClient *cur_client = waitinglist.GetNext(pos);
			if (!IsLiveUploadQueueClient(cur_client)) {
				RemoveStaleWaitingClient(pos2);
				continue;
			}
			if (!IsUploadQueueAdmissionCandidate(ApplyUploadRetryCooldown(cur_client, curTick) || cur_client->IsInSlowUploadCooldown()))
				continue;
			sum += cur_client->GetCombinedFilePrioAndCredit();
			++count;
		}

		m_fAverageCombinedFilePrioAndCredit = count != 0 ? sum / static_cast<float>(count) : 0;
	}

	return m_fAverageCombinedFilePrioAndCredit;
}

void CUploadQueue::InvalidateUploadClientStruct(UploadingToClient_Struct *pUploadClientStruct, CUpDownClient *pClient)
{
	ASSERT(pUploadClientStruct != NULL);
	ASSERT(pClient != NULL);
	if (pUploadClientStruct == NULL || pClient == NULL)
		return;

	pClient->FlushSendBlocks();
	InvalidateUploadClientStructWithoutClient(pUploadClientStruct);
}

void CUploadQueue::InvalidateUploadClientStructWithoutClient(UploadingToClient_Struct *pUploadClientStruct)
{
	ASSERT(pUploadClientStruct != NULL);
	if (pUploadClientStruct == NULL)
		return;

	CSingleLock lockBlockLists(&pUploadClientStruct->m_csBlockListsLock, TRUE);
	ASSERT(lockBlockLists.IsLocked());

	while (!pUploadClientStruct->m_BlockRequests_queue.IsEmpty())
		delete pUploadClientStruct->m_BlockRequests_queue.RemoveHead();
	while (!pUploadClientStruct->m_DoneBlocks_list.IsEmpty())
		delete pUploadClientStruct->m_DoneBlocks_list.RemoveHead();
	pUploadClientStruct->m_BlockRequests_keys.clear();
	pUploadClientStruct->m_DoneBlocks_keys.clear();
	pUploadClientStruct->m_pClient = NULL;
}

CUploadQueue::RetiredUploadClientStructContext CUploadQueue::RemoveUploadClientStructFromActiveList(POSITION pos, UploadingToClient_Struct *pUploadClientStruct)
{
	ASSERT(pUploadClientStruct != NULL);
	ASSERT(pos != NULL);
	if (pUploadClientStruct == NULL || pos == NULL)
		return {NULL};

	CSingleLock lockUploadList(&m_csUploadListMainThrdWriteOtherThrdsRead, TRUE);
	ASSERT(lockUploadList.IsLocked());

	// WHY: upload disk completions keep raw UploadingToClient_Struct pointers
	// and rely on either uploadinglist or m_retiredUploadingList to find/reclaim
	// them. Link the retired owner first because AddTail can allocate; RemoveAt
	// cannot. A low-memory failure must leave the entry discoverable in the
	// active list instead of in no list at all.
	m_retiredUploadingList.AddTail(pUploadClientStruct);
	uploadinglist.RemoveAt(pos);
	pUploadClientStruct->m_bRetired = true;
	pUploadClientStruct->m_ullRetiredTick = ::GetTickCount64();
	pUploadClientStruct->m_ullLastRetiredPendingIOLogTick = 0;
	return {pUploadClientStruct};
}

void CUploadQueue::RetireUploadClientStruct(POSITION pos, UploadingToClient_Struct *pUploadClientStruct, CUpDownClient *pClient)
{
	ASSERT(pUploadClientStruct != NULL);
	ASSERT(pClient != NULL);
	ASSERT(pos != NULL);
	if (pUploadClientStruct == NULL || pClient == NULL || pos == NULL)
		return;

	const RetiredUploadClientStructContext retiredContext = RemoveUploadClientStructFromActiveList(pos, pUploadClientStruct);
	if (retiredContext.pUploadClientStruct != NULL)
		InvalidateUploadClientStruct(retiredContext.pUploadClientStruct, pClient);
}

void CUploadQueue::RetireStaleUploadClientStruct(POSITION pos, UploadingToClient_Struct *pUploadClientStruct)
{
	ASSERT(pUploadClientStruct != NULL);
	ASSERT(pos != NULL);
	if (pUploadClientStruct == NULL || pos == NULL)
		return;

	if (pUploadClientStruct->m_pClient != NULL)
		QueueUploadListDisplayRefresh();
	const RetiredUploadClientStructContext retiredContext = RemoveUploadClientStructFromActiveList(pos, pUploadClientStruct);
	if (retiredContext.pUploadClientStruct != NULL) {
		InvalidateUploadClientStructWithoutClient(retiredContext.pUploadClientStruct);
		m_iHighestNumberOfFullyActivatedSlotsSinceLastCall = 0;
	}
}

void CUploadQueue::ReclaimRetiredUploadClientStructs()
{
	CSingleLock lockUploadList(&m_csUploadListMainThrdWriteOtherThrdsRead, TRUE);
	ASSERT(lockUploadList.IsLocked());

	const ULONGLONG ullCurrentTick = ::GetTickCount64();
	for (POSITION pos = m_retiredUploadingList.GetHeadPosition(); pos != NULL;) {
		POSITION curPos = pos;
		UploadingToClient_Struct *pUploadClientStruct = m_retiredUploadingList.GetNext(pos);
		ASSERT(pUploadClientStruct->m_bRetired);
		const LONG nPendingIOBlocks = pUploadClientStruct->m_nPendingIOBlocks.load();
		if (CanReclaimUploadQueueEntry(pUploadClientStruct->m_bRetired, nPendingIOBlocks)) {
			m_retiredUploadingList.RemoveAt(curPos);
			delete pUploadClientStruct;
		} else if (ShouldWarnRetiredUploadEntryPendingIo(
			pUploadClientStruct->m_bRetired,
			nPendingIOBlocks,
			ullCurrentTick,
			pUploadClientStruct->m_ullRetiredTick,
			pUploadClientStruct->m_ullLastRetiredPendingIOLogTick))
		{
			const ULONGLONG ullRetiredAgeMs = ullCurrentTick >= pUploadClientStruct->m_ullRetiredTick ? ullCurrentTick - pUploadClientStruct->m_ullRetiredTick : 0;
			pUploadClientStruct->m_ullLastRetiredPendingIOLogTick = ullCurrentTick;
			AddDebugLogLine(DLP_HIGH, false, _T("UploadQueue: retired upload entry is still waiting for %ld pending disk I/O block(s) after %I64u ms; delaying reclaim"),
				nPendingIOBlocks,
				static_cast<uint64>(ullRetiredAgeMs));
		}
	}
}

bool CUploadQueue::RemoveFromUploadQueue(CUpDownClient *client, LPCTSTR pszReason, bool updatewindow, bool earlyabort)
{
	bool result = false;
	uint32 slotCounter = 1;
	for (POSITION pos = uploadinglist.GetHeadPosition(); pos != NULL;) {
		POSITION curPos = pos;
		UploadingToClient_Struct *curClientStruct = uploadinglist.GetNext(pos);
		if (client == curClientStruct->m_pClient) {
			if (updatewindow)
				QueueUploadListDisplayRefresh();

			if (thePrefs.GetLogUlDlEvents()) {
				AddDebugLogLine(DLP_DEFAULT, true, _T("Removing client from upload list: %s Client: %s Transferred: %s SessionUp: %s QueueSessionPayload: %s In buffer: %s Req blocks: %i File: %s")
					, pszReason == NULL ? _T("") : pszReason
					, (LPCTSTR)client->DbgGetClientInfo()
					, (LPCTSTR)CastSecondsToHM(client->GetUpStartTimeDelay() / SEC2MS(1))
					, (LPCTSTR)CastItoXBytes(client->GetSessionUp())
					, (LPCTSTR)CastItoXBytes(client->GetQueueSessionPayloadUp())
					, (LPCTSTR)CastItoXBytes(client->GetPayloadInBuffer()), curClientStruct->m_BlockRequests_queue.GetCount()
					, theApp.sharedfiles->GetFileByID(client->GetUploadFileID()) ? (LPCTSTR)theApp.sharedfiles->GetFileByID(client->GetUploadFileID())->GetFileName() : _T(""));
			}
			RetireUploadClientStruct(curPos, curClientStruct, client);

			//if (thePrefs.GetLogUlDlEvents() && !theApp.uploadBandwidthThrottler->RemoveFromStandardList(client->socket))
			//	AddDebugLogLine(false, _T("UploadQueue: Didn't find socket to delete. Address: 0x%x"), client->socket);
			theApp.uploadBandwidthThrottler->RemoveFromStandardList(client->socket);

			if (client->GetSessionUp() > 0) {
				++successfullupcount;
				const ULONGLONG ullSessionUpTimeSeconds = client->GetUpStartTimeDelay() / SEC2MS(1);
				const ULONGLONG ullRemainingUploadTimeBudget = static_cast<ULONGLONG>(UINT_MAX - totaluploadtime);
				totaluploadtime += static_cast<uint32>(ullSessionUpTimeSeconds < ullRemainingUploadTimeBudget ? ullSessionUpTimeSeconds : ullRemainingUploadTimeBudget);
			} else
				failedupcount += static_cast<uint32>(!earlyabort);

			CKnownFile *requestedFile = theApp.sharedfiles->GetFileByID(client->GetUploadFileID());
			if (requestedFile != NULL)
				requestedFile->UpdatePartsInfo();

			theApp.clientlist->AddTrackClient(client); // Keep track of this client
			const bool bDisconnectedRemoval = pszReason != NULL
				&& (_tcsstr(pszReason, _T("CUpDownClient::Disconnected:")) != NULL
					|| _tcsstr(pszReason, _T("Remote client cancelled transfer")) != NULL);
			const bool bRemoteCancelledRemoval = pszReason != NULL
				&& _tcsstr(pszReason, _T("Remote client cancelled transfer")) != NULL;
			if (ShouldCooldownShortFailedUploadSlot(
					bDisconnectedRemoval,
					bRemoteCancelledRemoval,
					client->GetFriendSlot(),
					client->GetUpStartTimeDelay(),
					client->GetQueueSessionPayloadUp()))
			{
				// WHY: a peer which repeatedly disconnects seconds after admission can
				// keep winning queue selection and consume replacement attempts while
				// broadband upload stays underfilled. Reusing the local upload cooldown
				// suppresses that peer's score without changing protocol messages. Remote
				// cancels get a slightly longer low-payload window because they otherwise
				// re-enter as bad replacements after burning most of the warmup period.
				const ULONGLONG ullCooldownUntil = ::GetTickCount64() + SEC2MS(GetUploadChurnRetryCooldownSecondsForBudget(
					thePrefs.GetSlowUploadCooldownSeconds(),
					GetConfiguredUploadBudgetBytesPerSec()));
				client->SetSlowUploadCooldownUntil(ullCooldownUntil);
				SetUploadRetryCooldown(client, ullCooldownUntil, uploadRetryCooldownShortFailed);
#if EMULEBB_HAS_BAD_PEER_DIAGNOSTICS
				CString strBadPeerEvidence;
				strBadPeerEvidence.Format(_T("{\"session_up\":%I64u,\"queue_session_payload\":%I64u,\"up_time_ms\":%I64u,\"early_abort\":%s}"),
					client->GetSessionUp(),
					client->GetQueueSessionPayloadUp(),
					client->GetUpStartTimeDelay(),
					earlyabort ? _T("true") : _T("false"));
				EMULEBB_BAD_PEER_LOG_CLIENT_EVENT(_T("upload_short_failed_slot_cooldown"), _T("medium"), client, _T("cooldown"), _T("Short failed upload slot"), theApp.sharedfiles->GetFileByID(client->GetUploadFileID()), strBadPeerEvidence);
#endif
				if (thePrefs.GetLogUlDlEvents())
					AddDebugLogLine(DLP_LOW, false, _T("%s: Upload retry cooled down after a short failed upload slot."), client->GetUserName());
			}
			client->ResetSlowUploadTracking();
			client->SetUploadState(US_NONE);
			client->SetCollectionUploadSlot(false);

			m_iHighestNumberOfFullyActivatedSlotsSinceLastCall = 0;

			result = true;
		} else {
			if (!IsLiveUploadQueueClient(curClientStruct->m_pClient)) {
				RetireStaleUploadClientStruct(curPos, curClientStruct);
				continue;
			}
			curClientStruct->m_pClient->SetSlotNumber(slotCounter++);
		}
	}
	return result;
}

uint32 CUploadQueue::GetAverageUpTime() const
{
	return successfullupcount ? (totaluploadtime / successfullupcount) : 0;
}

bool CUploadQueue::RemoveFromWaitingQueue(CUpDownClient *client, bool updatewindow)
{
	POSITION pos = waitinglist.Find(client);
	if (pos) {
		RemoveFromWaitingQueue(pos, updatewindow);
		return true;
	}
	return false;
}

void CUploadQueue::RemoveFromWaitingQueue(POSITION pos, bool updatewindow)
{
	ASSERT(pos != NULL);
	if (pos == NULL)
		return;

	m_bStatisticsWaitingListDirty = true;
	CUpDownClient *todelete = waitinglist.GetAt(pos);
	waitinglist.RemoveAt(pos);
	if (updatewindow) {
		QueueWaitingListDisplayRefresh();
		theApp.emuledlg->transferwnd->ShowQueueCount(waitinglist.GetCount());
	}
	if (IsLiveUploadQueueClient(todelete))
		todelete->SetUploadState(US_NONE);
}

void CUploadQueue::RemoveStaleWaitingClient(POSITION pos)
{
	ASSERT(pos != NULL);
	if (pos == NULL)
		return;

	m_bStatisticsWaitingListDirty = true;
	CUpDownClient *staleClient = waitinglist.GetAt(pos);
	waitinglist.RemoveAt(pos);
	QueueWaitingListDisplayRefresh();
	theApp.emuledlg->transferwnd->ShowQueueCount(waitinglist.GetCount());
}

void CUploadQueue::UpdateMaxClientScore()
{
	m_imaxscore = 0;
	const ULONGLONG curTick = ::GetTickCount64();
	for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		CUpDownClient *cur_client = waitinglist.GetNext(pos);
		if (!IsLiveUploadQueueClient(cur_client)) {
			RemoveStaleWaitingClient(pos2);
			continue;
		}
		if (!IsUploadQueueAdmissionCandidate(ApplyUploadRetryCooldown(cur_client, curTick) || cur_client->IsInSlowUploadCooldown()))
			continue;
		uint32 score = cur_client->GetScore(true, false);
		UpdateUploadQueueMaxScore(m_imaxscore, score);
	}
}

bool CUploadQueue::CheckForTimeOver(CUpDownClient *client, CString *pstrReason, bool *pbRequeue)
{
	if (pstrReason != NULL)
		pstrReason->Empty();
	if (pbRequeue != NULL)
		*pbRequeue = true;

	if (client->GetFriendSlot())
		return false;

	// WHY: stock session rotation waits for queue pressure before replacing a
	// slot, but a peer with an empty local send pipeline and no fresh part
	// requests is not useful work. Recycle it under sustained broadband
	// underfill even when the waiting list is empty or below the normal cap.
	const ULONGLONG curTick = ::GetTickCount64();
	const bool bShouldTrackSlowUploadSlots = ShouldTrackSlowUploadSlots();
	if (ShouldRecycleIdleUploadSlot(client, curTick, pstrReason)) {
		if (pbRequeue != NULL && client->IsBanned())
			*pbRequeue = false;
		return true;
	}

	if (waitinglist.IsEmpty())
		return false;

	// Friend slots remain the one deliberate scheduling exception on this
	// branch. Collection handling is reduced to correctness checks only: reject
	// a file switch, otherwise let the normal broadband recycle/rotation path
	// decide what to do with the slot.
	if (client->HasCollectionUploadSlot()) {
		const CKnownFile *pDownloadingFile = theApp.sharedfiles->GetFileByID(client->requpfileid);
		if (pDownloadingFile == NULL)
			return true;
		if (CCollection::HasCollectionExtention(pDownloadingFile->GetFileName()) && pDownloadingFile->GetFileSize() < (uint64)MAXPRIORITYCOLL_SIZE) {
			// Valid collection traffic continues through the normal broadband
			// recycle and session-rotation rules below.
		} else {
			if (thePrefs.GetLogUlDlEvents())
				AddDebugLogLine(DLP_HIGH, false, _T("%s: Upload session ended - client with Collection Slot tried to request blocks from another file"), client->GetUserName());
			if (pstrReason != NULL)
				*pstrReason = _T("Collection slot file switched");
			return true;
		}
	}

	if (bShouldTrackSlowUploadSlots) {
		client->UpdateSlowUploadTracking(curTick, GetSlowUploadRateThreshold());
		if (!HasCompletedSlowUploadWarmup(client)) {
			client->ResetSlowUploadTracking();
		} else {
			const uint32 uBudgetBytesPerSec = GetConfiguredUploadBudgetBytesPerSec();
			const UINT uEffectiveSlowGraceSeconds = GetSlowUploadGraceSecondsForBudget(thePrefs.GetSlowUploadGraceSeconds(), uBudgetBytesPerSec);
			const UINT uEffectiveZeroGraceSeconds = GetZeroUploadGraceSecondsForBudget(thePrefs.GetZeroUploadRateGraceSeconds(), uBudgetBytesPerSec);
			const ULONGLONG ullEffectiveZeroGraceMs = SEC2MS(uEffectiveZeroGraceSeconds);
			if (client->ShouldRecycleSlowUpload(SEC2MS(uEffectiveSlowGraceSeconds), ullEffectiveZeroGraceMs)) {
				const UINT uCooldownSeconds = GetSlowUploadRetryCooldownSecondsForBudget(
					thePrefs.GetSlowUploadCooldownSeconds(),
					GetConfiguredUploadBudgetBytesPerSec());
				const ULONGLONG ullCooldownUntil = ::GetTickCount64() + SEC2MS(uCooldownSeconds);
				client->SetSlowUploadCooldownUntil(ullCooldownUntil);
				SetUploadRetryCooldown(client, ullCooldownUntil,
					client->GetAccumulatedZeroUploadMs() >= ullEffectiveZeroGraceMs
						? uploadRetryCooldownZeroUpload
						: uploadRetryCooldownSlowUpload);
#if EMULEBB_HAS_BAD_PEER_DIAGNOSTICS
				CString strBadPeerEvidence;
				strBadPeerEvidence.Format(_T("{\"zero_ms\":%I64u,\"zero_grace_ms\":%I64u,\"upload_rate_bytes_per_sec\":%u}"),
					client->GetAccumulatedZeroUploadMs(),
					static_cast<uint64>(ullEffectiveZeroGraceMs),
					client->GetUploadDatarate());
				EMULEBB_BAD_PEER_LOG_CLIENT_EVENT(
					client->GetAccumulatedZeroUploadMs() >= ullEffectiveZeroGraceMs ? _T("upload_zero_rate_recycle") : _T("upload_slow_rate_recycle"),
					_T("medium"),
					client,
					_T("cooldown"),
					client->GetAccumulatedZeroUploadMs() >= ullEffectiveZeroGraceMs ? _T("Broadband zero-rate recycle") : _T("Broadband slow-rate recycle"),
					theApp.sharedfiles->GetFileByID(client->GetUploadFileID()),
					strBadPeerEvidence);
#endif
				if (thePrefs.GetLogUlDlEvents()) {
					if (client->GetAccumulatedZeroUploadMs() >= ullEffectiveZeroGraceMs)
						AddDebugLogLine(DLP_LOW, false, _T("%s: Upload slot recycled due to zero upload during broadband underfill."), client->GetUserName());
					else
						AddDebugLogLine(DLP_LOW, false, _T("%s: Upload slot recycled due to slow upload during broadband underfill."), client->GetUserName());
				}
				if (pstrReason != NULL) {
					*pstrReason = (client->GetAccumulatedZeroUploadMs() >= ullEffectiveZeroGraceMs)
						? _T("Broadband zero-rate recycle")
						: _T("Broadband slow-rate recycle");
				}
				client->ResetSlowUploadTracking();
				return true;
			}
		}
	}
	// The drained no-request recycle path above owns its own tracking/reset
	// decisions. Resetting here would erase zero-rate progress whenever the
	// waiting list is non-empty, leaving drained slots stuck during underfill.

	const CKnownFile *pUploadingFile = theApp.sharedfiles->GetFileByID(client->GetUploadFileID());
	const uint64 uSessionTransferLimit = ResolveSessionTransferLimitBytes(pUploadingFile);
	if (uSessionTransferLimit > 0) {
		// Allow the client to download a specified amount per session, but only rotate when another slot is needed.
		if (client->GetQueueSessionPayloadUp() > uSessionTransferLimit) {
			const bool bNeedsReplacement = ForceNewClient();
			// WHY: rotating a productive slot during broadband underfill can
			// trade known upload throughput for a cooldown-only/bad replacement
			// pool. Keep productive sessions until capacity is filled again.
			if (ShouldRotateBroadbandLimitedUploadSession(
					bNeedsReplacement,
					IsBroadbandUploadUnderfilled(),
					client->GetUploadDatarate(),
					GetSlowUploadRateThreshold()))
			{
				if (thePrefs.GetLogUlDlEvents())
					AddDebugLogLine(DLP_DEFAULT, false, _T("%s: Upload session ended due to broadband transfer limit (%s)"), client->GetUserName(), (LPCTSTR)CastItoXBytes(uSessionTransferLimit));
				if (pstrReason != NULL)
					*pstrReason = _T("Broadband session transfer limit");
				return true;
			} else if (thePrefs.GetLogUlDlEvents()) {
				UINT uSuppressedLogs = 0;
				if (!ShouldLogBroadbandRetainedSlot(uSuppressedLogs))
					return false;
				AddDebugLogLine(DLP_LOW, false,
					bNeedsReplacement
						? _T("%s: Broadband transfer limit reached but productive slot retained during underfill. Suppressed retained-slot logs: %u.")
						: _T("%s: Broadband transfer limit reached but slot retained because no replacement is needed. Suppressed retained-slot logs: %u."),
					client->GetUserName(), uSuppressedLogs);
			}
		}
	}

	const UINT uSessionTimeLimitSeconds = thePrefs.GetSessionTimeLimitSeconds();
	if (uSessionTimeLimitSeconds > 0 && client->GetUpStartTimeDelay() > SEC2MS(uSessionTimeLimitSeconds)) {
		const bool bNeedsReplacement = ForceNewClient();
		// WHY: time-limit fairness should not evict an active contributor while
		// the broadband upload line is already underfilled and replacements are
		// likely to reduce capacity further.
		if (ShouldRotateBroadbandLimitedUploadSession(
				bNeedsReplacement,
				IsBroadbandUploadUnderfilled(),
				client->GetUploadDatarate(),
				GetSlowUploadRateThreshold()))
		{
			if (thePrefs.GetLogUlDlEvents())
				AddDebugLogLine(DLP_LOW, false, _T("%s: Upload session ended due to broadband time limit %s."), client->GetUserName(), (LPCTSTR)CastSecondsToHM(uSessionTimeLimitSeconds));
			if (pstrReason != NULL)
				*pstrReason = _T("Broadband session time limit");
			return true;
		} else if (thePrefs.GetLogUlDlEvents()) {
			UINT uSuppressedLogs = 0;
			if (!ShouldLogBroadbandRetainedSlot(uSuppressedLogs))
				return false;
			AddDebugLogLine(DLP_LOW, false,
				bNeedsReplacement
					? _T("%s: Broadband time limit reached but productive slot retained during underfill. Suppressed retained-slot logs: %u.")
					: _T("%s: Broadband time limit reached but slot retained because no replacement is needed. Suppressed retained-slot logs: %u."),
				client->GetUserName(), uSuppressedLogs);
		}
	}

	return false;
}

void CUploadQueue::DeleteAll()
{
	waitinglist.RemoveAll();
	m_uploadRetryCooldownByIP.clear();
	m_noRequestUploadRetryCooldownByIP.clear();
	m_noRequestRepeatOffendersByHash.clear();
	m_noRequestRepeatOffendersByIP.clear();
	m_noRequestRepeatHashesByIP.clear();
	m_ullLastNoRequestRepeatCleanup = 0;
	CUploadingPtrList deletingList;
	CSingleLock lockUploadList(&m_csUploadListMainThrdWriteOtherThrdsRead, TRUE);
	ASSERT(lockUploadList.IsLocked());

	while (!uploadinglist.IsEmpty()) {
		UploadingToClient_Struct *pUploadClientStruct = uploadinglist.RemoveHead();
		pUploadClientStruct->m_bRetired = true;
		pUploadClientStruct->m_ullRetiredTick = ::GetTickCount64();
		pUploadClientStruct->m_ullLastRetiredPendingIOLogTick = 0;
		deletingList.AddTail(pUploadClientStruct);
	}
	while (!m_retiredUploadingList.IsEmpty())
		deletingList.AddTail(m_retiredUploadingList.RemoveHead());
	lockUploadList.Unlock();

	while (!deletingList.IsEmpty()) {
		UploadingToClient_Struct *pUploadClientStruct = deletingList.RemoveHead();
		CUpDownClient *pClient = pUploadClientStruct->m_pClient;
		if (pClient != NULL)
			InvalidateUploadClientStruct(pUploadClientStruct, pClient);
		const LONG nPendingIOBlocks = pUploadClientStruct->m_nPendingIOBlocks.load();
		if (!CanReclaimUploadQueueEntry(pUploadClientStruct->m_bRetired, nPendingIOBlocks)) {
			AddDebugLogLine(DLP_HIGH, false, _T("UploadQueue: shutdown retained retired upload entry with %ld pending disk I/O block(s); delaying reclaim"),
				nPendingIOBlocks);
			CSingleLock lockRetiredUploadList(&m_csUploadListMainThrdWriteOtherThrdsRead, TRUE);
			ASSERT(lockRetiredUploadList.IsLocked());
			m_retiredUploadingList.AddTail(pUploadClientStruct);
			continue;
		}
		delete pUploadClientStruct;
	}
	// Normal slot teardown detaches sockets from the throttler. DeleteAll only
	// runs during shutdown, after upload processing has stopped.
}

UINT CUploadQueue::GetWaitingPosition(CUpDownClient *client)
{
	if (!IsLiveUploadQueueClient(client) || !IsOnUploadQueue(client))
		return 0;
	const ULONGLONG curTick = ::GetTickCount64();
	UINT rank = 1;
	UINT myscore = client->GetScore(false);
	for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		CUpDownClient *cur_client = waitinglist.GetNext(pos);
		if (!IsLiveUploadQueueClient(cur_client)) {
			RemoveStaleWaitingClient(pos2);
			continue;
		}
		if (cur_client != client && !IsUploadQueueAdmissionCandidate(ApplyUploadRetryCooldown(cur_client, curTick) || cur_client->IsInSlowUploadCooldown()))
			continue;
		rank = AddHigherUploadQueueScoreToRank(rank, cur_client->GetScore(false), myscore);
	}

	return rank;
}

VOID CALLBACK CUploadQueue::UploadTimer(HWND /*hwnd*/, UINT /*uMsg*/, UINT_PTR /*idEvent*/, DWORD /*dwTime*/) noexcept
{
	const CUploadTimerDurationScope timerDurationScope;

	// NOTE: Always handle all type of MFC exceptions in TimerProcs - otherwise we'll get mem leaks
	try {
		// Barry - Don't do anything if the app is shutting down - can cause unhandled exceptions
		if (!Win32CallbackTimerSeams::ShouldDispatchUploadQueueTimer(theApp.IsClosing()))
			return;

		// Elandal:ThreadSafeLogging -->
		// other threads may have queued up log lines. This prints them.
		theApp.HandleDebugLogQueue();
		theApp.HandleLogQueue();
		// Elandal: ThreadSafeLogging <--

		theApp.uploadqueue->Process();
		theApp.downloadqueue->Process();
		if (thePrefs.ShowOverhead()) {
			theStats.CompUpDatarateOverhead();
			theStats.CompDownDatarateOverhead();
		}

		if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL) {
			uint32 nDisplayMask = DISPLAY_REFRESH_TRANSFER_SUMMARY;
			if (thePrefs.GetUpdateQueueList())
				nDisplayMask |= DISPLAY_REFRESH_QUEUE_LIST;
			theApp.emuledlg->transferwnd->QueueDisplayRefresh(nDisplayMask);
		}

		// one second
		if (++i1sec >= 10) {
			i1sec = 0;

			// try to use different time intervals here to avoid disk I/O congestion by saving all files at once
			theApp.clientcredits->Process();	// 13 minutes
			theApp.serverlist->Process();		// 17 minutes
			theApp.knownfiles->Process();		// 11 minutes
			theApp.friendlist->Process();		// 19 minutes
			theApp.clientlist->Process();
			theApp.sharedfiles->Process();
			if (Kademlia::CKademlia::IsRunning()) {
				Kademlia::CKademlia::Process();
				if (Kademlia::CKademlia::GetPrefs()->HasLostConnection()) {
					Kademlia::CKademlia::Stop();
					theApp.emuledlg->ShowConnectionState();
				}
			}
			if (theApp.serverconnect->IsConnecting() && !theApp.serverconnect->IsSingleConnect())
				theApp.serverconnect->TryAnotherConnectionRequest();

			theApp.listensocket->UpdateConnectionsStatus();
			if (thePrefs.WatchClipboard4ED2KLinks()) {
				// TODO: Remove this from here. This has to be done with a clipboard chain
				// and *not* with a timer!!
				theApp.SearchClipboard();
			}

			if (theApp.serverconnect->IsConnecting())
				theApp.serverconnect->CheckForTimeout();

			// 2 seconds
			if (++i2sec >= 2) {
				i2sec = 0;

				// Update connection stats...
				theStats.UpdateConnectionStats(static_cast<float>(theApp.uploadqueue->GetDatarate()) / 1024.0f, static_cast<float>(theApp.downloadqueue->GetDatarate()) / 1024.0f);

#ifdef HAVE_WIN7_SDK_H
				if (thePrefs.IsWin7TaskbarGoodiesEnabled())
					theApp.emuledlg->UpdateStatusBarProgress();
#endif
			}

			// display graphs
			if (thePrefs.GetTrafficOMeterInterval() > 0 && ++igraph >= (uint32)thePrefs.GetTrafficOMeterInterval()) {
				igraph = 0;
				theApp.emuledlg->statisticswnd->SetCurrentRate(static_cast<float>(theApp.uploadqueue->GetDatarate()) / 1024.0f, static_cast<float>(theApp.downloadqueue->GetDatarate()) / 1024.0f);
			}
			if (theApp.emuledlg->activewnd == theApp.emuledlg->statisticswnd
				&& theApp.emuledlg->IsWindowVisible()
				&& thePrefs.GetStatsInterval() > 0	// display is on
				&& ++istats >= (uint32)thePrefs.GetStatsInterval())
			{
				istats = 0;
				theApp.emuledlg->statisticswnd->ShowStatistics();
			}

			theApp.uploadqueue->UpdateDatarates();

			//save rates every second
			theStats.RecordRate();

			if (theApp.emuledlg->IsTrayIconToFlash())
				theApp.emuledlg->ShowTransferRate(true);

			// *** 5 seconds **********************************************
			if (++i5sec >= 5) {
				i5sec = 0;
#ifdef _DEBUG
				if (thePrefs.m_iDbgHeap > 0 && !AfxCheckMemory())
					AfxDebugBreak();
#endif
				theApp.listensocket->Process();
				theApp.OnlineSig(); // Added By Bouc7

				if (!thePrefs.TransferFullChunks())
					theApp.uploadqueue->UpdateMaxClientScore();

				if (thePrefs.IsSchedulerEnabled())
					theApp.scheduler->Check();
			}

			// *** 60 seconds *********************************************
			if (++i60sec >= 60) {
				i60sec = 0;

				if (thePrefs.GetWSIsEnabled())
					theApp.webserver->UpdateSessionCount();

				theApp.serverconnect->KeepConnectionAlive();

				theApp.UpdateStandbyPrevention();
			}

			if (++s_uSaveStatistics >= thePrefs.GetStatsSaveInterval()) {
				s_uSaveStatistics = 0;
				thePrefs.SaveStats();
			}
		}

		// need more accuracy here; do not rely on the 'i5sec' and 'i60sec' helpers.
		thePerfLog.LogSamples();
	}
	CATCH_DFLT_EXCEPTIONS(_T("CUploadQueue::UploadTimer"))
}

UploadTimerRuntimeStats CUploadQueue::GetUploadTimerRuntimeStats()
{
	return UploadTimerRuntimeStats{
		s_uUploadTimerLastDurationMs,
		s_uUploadTimerMaxDurationMs,
		s_uUploadTimerSlowLoopCount,
		kUploadTimerSlowLoopThresholdMs
	};
}

CUpDownClient* CUploadQueue::GetNextClient(const CUpDownClient *lastclient) const
{
	if (waitinglist.IsEmpty())
		return NULL;
	if (!lastclient) {
		for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;) {
			CUpDownClient *cur_client = waitinglist.GetNext(pos);
			if (IsLiveUploadQueueClient(cur_client))
				return cur_client;
		}
		return NULL;
	}
	POSITION pos = waitinglist.Find(const_cast<CUpDownClient*>(lastclient));
	if (!pos) {
		TRACE("Error: CUploadQueue::GetNextClient");
		for (POSITION fallbackPos = waitinglist.GetHeadPosition(); fallbackPos != NULL;) {
			CUpDownClient *cur_client = waitinglist.GetNext(fallbackPos);
			if (IsLiveUploadQueueClient(cur_client))
				return cur_client;
		}
		return NULL;
	}
	waitinglist.GetNext(pos);
	while (pos != NULL) {
		CUpDownClient *cur_client = waitinglist.GetNext(pos);
		if (IsLiveUploadQueueClient(cur_client))
			return cur_client;
	}
	return NULL;
}

CUpDownClient* CUploadQueue::GetNextFromUploadList(POSITION &curpos) const
{
	while (curpos != NULL) {
		const UploadingToClient_Struct *pCurClientStruct = uploadinglist.GetNext(curpos);
		CUpDownClient *client = pCurClientStruct != NULL ? pCurClientStruct->m_pClient : NULL;
		if (IsLiveUploadQueueClient(client))
			return client;
	}
	return NULL;
}

CUpDownClient* CUploadQueue::GetQueueClientAt(POSITION &curpos) const
{
	if (curpos == NULL)
		return NULL;

	const UploadingToClient_Struct *pCurClientStruct = uploadinglist.GetAt(curpos);
	CUpDownClient *client = pCurClientStruct != NULL ? pCurClientStruct->m_pClient : NULL;
	return IsLiveUploadQueueClient(client) ? client : NULL;
}

CUpDownClient* CUploadQueue::GetNextFromWaitingList(POSITION &curpos) const
{
	while (curpos != NULL) {
		CUpDownClient *client = waitinglist.GetNext(curpos);
		if (IsLiveUploadQueueClient(client))
			return client;
	}
	return NULL;
}

CUpDownClient* CUploadQueue::GetWaitClientAt(POSITION &curpos) const
{
	if (curpos == NULL)
		return NULL;

	CUpDownClient *client = waitinglist.GetAt(curpos);
	return IsLiveUploadQueueClient(client) ? client : NULL;
}

void CUploadQueue::UpdateDatarates()
{
	// Calculate average data rate
	const ULONGLONG curTick = ::GetTickCount64();
	if (curTick < m_lastCalculatedDataRateTick + (SEC2MS(1) / 2))
		return;
	m_lastCalculatedDataRateTick = curTick;
	if (average_ur_hist.Count() > 1 && average_ur_hist.Tail().timestamp > average_ur_hist.Head().timestamp) {
		ULONGLONG duration = average_ur_hist.Tail().timestamp - average_ur_hist.Head().timestamp;
		datarate = (uint32)(SEC2MS(m_average_ur_sum - average_ur_hist.Head().upBytes) / duration);
		friendDatarate = (uint32)(SEC2MS(average_ur_hist.Tail().upFriendBytes - average_ur_hist.Head().upFriendBytes) / duration);
	}
}

uint32 CUploadQueue::GetToNetworkDatarate() const
{
	return (datarate > friendDatarate) ? datarate - friendDatarate : 0;
}

uint32 CUploadQueue::GetWaitingUserForFileCount(const CSimpleArray<CObject*> &raFiles, bool bOnlyIfChanged)
{
	if (bOnlyIfChanged && !m_bStatisticsWaitingListDirty)
		return _UI32_MAX;

	m_bStatisticsWaitingListDirty = false;
	uint32 nResult = 0;
	for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		const CUpDownClient *cur_client = waitinglist.GetNext(pos);
		if (!IsLiveUploadQueueClient(cur_client)) {
			RemoveStaleWaitingClient(pos2);
			continue;
		}
		for (int i = raFiles.GetSize(); --i >= 0;)
			nResult += static_cast<uint32>(md4equ(static_cast<CKnownFile*>(raFiles[i])->GetFileHash(), cur_client->GetUploadFileID()));
	}
	return nResult;
}

uint32 CUploadQueue::GetDatarateForFile(const CSimpleArray<CObject*> &raFiles) const
{
	uint32 nResult = 0;
	for (POSITION pos = uploadinglist.GetHeadPosition(); pos != NULL;) {
		const CUpDownClient *cur_client = uploadinglist.GetNext(pos)->m_pClient;
		if (!IsLiveUploadQueueClient(cur_client))
			continue;
		for (int i = raFiles.GetSize(); --i >= 0;)
			if (md4equ(static_cast<CKnownFile*>(raFiles[i])->GetFileHash(), cur_client->GetUploadFileID()))
				nResult += cur_client->GetUploadDatarate();
	}
	return nResult;
}

const CUploadingPtrList& CUploadQueue::GetUploadListTS(CCriticalSection **outUploadListReadLock)
{
	ASSERT(*outUploadListReadLock == NULL);
	*outUploadListReadLock = &m_csUploadListMainThrdWriteOtherThrdsRead;
	return uploadinglist;
}

UploadingToClient_Struct* CUploadQueue::GetUploadingClientStructByClient(const CUpDownClient *pClient) const
{
	//TODO: Check if this function is too slow for its usage (esp. when rendering the GUI bars)
	//		if necessary we will have to speed it up with an additional map
	for (POSITION pos = uploadinglist.GetHeadPosition(); pos != NULL;) {
		UploadingToClient_Struct *pCurClientStruct = uploadinglist.GetNext(pos);
		if (pCurClientStruct->m_pClient == pClient)
			return pCurClientStruct;
	}
	return NULL;
}

UploadingToClient_Struct::~UploadingToClient_Struct()
{
	if (m_pClient != NULL)
		m_pClient->FlushSendBlocks();

	m_csBlockListsLock.Lock();
	while (!m_BlockRequests_queue.IsEmpty())
		delete m_BlockRequests_queue.RemoveHead();

	while (!m_DoneBlocks_list.IsEmpty())
		delete m_DoneBlocks_list.RemoveHead();
	m_BlockRequests_keys.clear();
	m_DoneBlocks_keys.clear();
	m_csBlockListsLock.Unlock();
}
