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
#include "UpDownClient.h"
#include "Opcodes.h"
#include "Packets.h"
#include "UploadQueue.h"
#include "UploadQueueSeams.h"
#include "Statistics.h"
#include "ClientList.h"
#include "SharedFileList.h"
#include "KnownFileList.h"
#include "PartFile.h"
#include "ClientCredits.h"
#include "ListenSocket.h"
#include "SafeFile.h"
#include "DownloadQueue.h"
#include "emuledlg.h"
#include "TransferDlg.h"
#include "Log.h"
#include "Collection.h"
#include "UploadDiskIOThread.h"
#include "OtherFunctions.h"

#include <memory>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


//	members of CUpDownClient
//	which are mainly used for uploading functions

CBarShader CUpDownClient::s_UpStatusBar(16);

namespace
{
#if EMULEBB_HAS_BAD_PEER_DIAGNOSTICS
LPCTSTR GetClientBanScopeInstrumentationToken(const CUpDownClient *pClient, ClientBanScope eScope)
{
	const bool bBanHash = pClient != NULL
		&& pClient->HasValidHash()
		&& (eScope == clientBanScopeHash || eScope == clientBanScopeBoth);
	const bool bBanIP = eScope == clientBanScopeIP || eScope == clientBanScopeBoth || !bBanHash;
	return bBanIP ? _T("ip") : _T("hash");
}
#endif

#ifdef EMULEBB_ENABLE_UPLOAD_SLOT_DIAGNOSTICS
bool IsUploadReqBlockHighVolumeReason(LPCTSTR pszReason)
{
	return _tcscmp(pszReason, _T("accept-queued-block")) == 0
		|| _tcscmp(pszReason, _T("reject-duplicate-done-block")) == 0
		|| _tcscmp(pszReason, _T("reject-duplicate-queued-block")) == 0
		|| _tcscmp(pszReason, _T("request-packet-complete-signal")) == 0;
}

void CountUploadReqBlockInstrumentation(UploadingToClient_Struct *pUploadingClientStruct, LPCTSTR pszReason)
{
	if (pUploadingClientStruct == NULL || pszReason == NULL)
		return;

	const ULONGLONG curTick = ::GetTickCount64();
	pUploadingClientStruct->m_ullLastReqBlockTick.store(curTick);
	if (_tcscmp(pszReason, _T("accept-queued-block")) == 0) {
		pUploadingClientStruct->m_ullReqBlocksAccepted.fetch_add(1);
		pUploadingClientStruct->m_ullLastAcceptedReqBlockTick.store(curTick);
	} else if (_tcscmp(pszReason, _T("reject-duplicate-done-block")) == 0)
		pUploadingClientStruct->m_ullReqBlocksDuplicateDone.fetch_add(1);
	else if (_tcscmp(pszReason, _T("reject-duplicate-queued-block")) == 0)
		pUploadingClientStruct->m_ullReqBlocksDuplicateQueued.fetch_add(1);
	else if (_tcscmp(pszReason, _T("request-packet-complete-signal")) == 0)
		pUploadingClientStruct->m_ullReqBlockPacketSignals.fetch_add(1);
	else if (_tcscmp(pszReason, _T("accept-queued-request-direct-admit")) == 0)
		return;
	else
		pUploadingClientStruct->m_ullReqBlocksRejected.fetch_add(1);
}

LPCTSTR GetQueuedBlockRequestAdmissionInstrumentationReason(QueuedBlockRequestAdmissionResult eResult)
{
	switch (eResult) {
	case queuedBlockRequestAdmitted:
		return _T("accept-queued-request-direct-admit");
	case queuedBlockRequestCooldownNotCleared:
		return _T("reject-not-uploading-cooldown-not-cleared");
	case queuedBlockRequestNotOnQueue:
		return _T("reject-not-uploading-not-on-queue");
	case queuedBlockRequestAlreadyUploading:
		return _T("reject-not-uploading-already-uploading");
	case queuedBlockRequestCapFull:
		return _T("reject-not-uploading-cap-full");
	case queuedBlockRequestAdmissionDeferred:
		return _T("reject-not-uploading-admission-deferred");
	case queuedBlockRequestDirectAddFailed:
		return _T("reject-not-uploading-direct-add-failed");
	default:
		return _T("reject-not-uploading-unknown-admission-result");
	}
}

void LogUploadReqBlockInstrumentation(
	CUpDownClient *client,
	LPCTSTR pszReason,
	const Requested_Block_Struct *reqblock,
	UploadingToClient_Struct *pUploadingClientStruct,
	INT_PTR iReqBlocks,
	INT_PTR iDoneBlocks)
{
	ASSERT(client != NULL);
	ASSERT(pszReason != NULL);
	if (client == NULL || pszReason == NULL)
		return;

	CountUploadReqBlockInstrumentation(pUploadingClientStruct, pszReason);
	if (IsUploadReqBlockHighVolumeReason(pszReason))
		return;

	CEMSocket *sock = client->GetFileUploadSocket(false);
	const uint64 uStartOffset = reqblock != NULL ? reqblock->StartOffset : 0;
	const uint64 uEndOffset = reqblock != NULL ? reqblock->EndOffset : 0;
	const uint64 uLength = uEndOffset > uStartOffset ? uEndOffset - uStartOffset : 0;

	UploadSlotDiagnosticsLogLine(
		_T("UploadSlotDiagnostics: reqblock reason=%s client=%s state=%s socket=%p socketConnected=%u handshake=%u rateBytesPerSec=%u ageMs=%I64u sessionUp=%s queuePayload=%s queueAdded=%s payloadInBuffer=%s requestPresent=%u start=%I64u end=%I64u length=%I64u reqBlocks=%Id doneBlocks=%Id pendingIO=%ld socketStdQueue=%Id fileKnown=%u"),
		pszReason,
		(LPCTSTR)client->DbgGetClientInfo(),
		client->DbgGetUploadState(),
		static_cast<void*>(sock),
		static_cast<UINT>(sock != NULL && sock->IsConnected()),
		static_cast<UINT>(client->CheckHandshakeFinished()),
		client->GetUploadDatarate(),
		static_cast<uint64>(client->GetUpStartTimeDelay()),
		(LPCTSTR)CastItoXBytes(client->GetSessionUp()),
		(LPCTSTR)CastItoXBytes(client->GetQueueSessionPayloadUp()),
		(LPCTSTR)CastItoXBytes(client->GetQueueSessionUploadAdded()),
		(LPCTSTR)CastItoXBytes(client->GetPayloadInBuffer()),
		static_cast<UINT>(reqblock != NULL),
		uStartOffset,
		uEndOffset,
		uLength,
		iReqBlocks,
		iDoneBlocks,
		pUploadingClientStruct != NULL ? pUploadingClientStruct->m_nPendingIOBlocks.load() : 0L,
		sock != NULL ? sock->DbgGetStdQueueCount() : -1,
		static_cast<UINT>(reqblock != NULL && theApp.sharedfiles->GetFileByID(reqblock->FileID) != NULL));
}
#endif

LPCTSTR GetQueuedBlockRequestAdmissionBadPeerReason(QueuedBlockRequestAdmissionResult eResult)
{
	switch (eResult) {
	case queuedBlockRequestAdmitted:
		return _T("accept-queued-request-direct-admit");
	case queuedBlockRequestCooldownNotCleared:
		return _T("reject-not-uploading-cooldown-not-cleared");
	case queuedBlockRequestNotOnQueue:
		return _T("reject-not-uploading-not-on-queue");
	case queuedBlockRequestAlreadyUploading:
		return _T("reject-not-uploading-already-uploading");
	case queuedBlockRequestCapFull:
		return _T("reject-not-uploading-cap-full");
	case queuedBlockRequestAdmissionDeferred:
		return _T("reject-not-uploading-admission-deferred");
	case queuedBlockRequestDirectAddFailed:
		return _T("reject-not-uploading-direct-add-failed");
	default:
		return _T("reject-not-uploading-unknown-admission-result");
	}
}

COLORREF BlendUploadBarColor(COLORREF crColor, COLORREF crTarget, UINT uColorWeight)
{
	const UINT uTargetWeight = 100U - min(uColorWeight, 100U);
	return RGB(
		(GetRValue(crColor) * uColorWeight + GetRValue(crTarget) * uTargetWeight) / 100U,
		(GetGValue(crColor) * uColorWeight + GetGValue(crTarget) * uTargetWeight) / 100U,
		(GetBValue(crColor) * uColorWeight + GetBValue(crTarget) * uTargetWeight) / 100U);
}

struct SUploadBarColors
{
	COLORREF crNeither;
	COLORREF crNextSending;
	COLORREF crBoth;
	COLORREF crSending;
};

SUploadBarColors BuildUploadBarColors(bool bFlat, bool bActive)
{
	SUploadBarColors colors = {};
	if (g_bLowColorDesktop) {
		colors.crNeither = bActive ? RGB(224, 224, 224) : RGB(248, 248, 248);
		colors.crNextSending = bActive ? RGB(255, 208, 0) : RGB(255, 244, 191);
		colors.crBoth = bActive ? (bFlat ? RGB(0, 0, 0) : RGB(104, 104, 104)) : RGB(191, 191, 191);
		colors.crSending = bActive ? RGB(0, 150, 0) : RGB(191, 229, 191);
		return colors;
	}

	colors.crNeither = bActive ? RGB(224, 224, 224) : RGB(248, 248, 248);
	colors.crNextSending = bActive ? RGB(255, 208, 0) : RGB(255, 244, 191);
	colors.crBoth = bActive ? (bFlat ? RGB(0, 0, 0) : RGB(104, 104, 104)) : RGB(191, 191, 191);
	colors.crSending = bActive ? RGB(0, 150, 0) : RGB(191, 229, 191);

	theApp.LoadSkinColor(_T("UploadBarBackground"), colors.crNeither);
	theApp.LoadSkinColor(_T("UploadBarHave"), colors.crBoth);
	theApp.LoadSkinColor(_T("UploadBarSending"), colors.crSending);
	theApp.LoadSkinColor(_T("UploadBarNext"), colors.crNextSending);

	if (!bActive) {
		colors.crNextSending = BlendUploadBarColor(colors.crNextSending, colors.crNeither, 55);
		colors.crBoth = BlendUploadBarColor(colors.crBoth, colors.crNeither, 55);
		colors.crSending = BlendUploadBarColor(colors.crSending, colors.crNeither, 55);
	}
	return colors;
}
}

void CUpDownClient::DrawUpStatusBar(CDC &dc, const CRect &rect, bool onlygreyrect, bool  bFlat) const
{
	const bool bActive = GetSlotNumber() <= (UINT)theApp.uploadqueue->GetActiveUploadsCount()
		|| (GetUploadState() != US_UPLOADING && GetUploadState() != US_CONNECTING);
	const SUploadBarColors colors = BuildUploadBarColors(bFlat, bActive);

	// wistily: UpStatusFix
	CKnownFile *currequpfile = theApp.sharedfiles->GetFileByID(requpfileid);
	EMFileSize filesize = currequpfile ? currequpfile->GetFileSize() : PARTSIZE * m_nUpPartCount;
	// wistily: UpStatusFix

	if (filesize > 0ull) {
		s_UpStatusBar.SetFileSize(filesize);
		s_UpStatusBar.SetHeight(rect.Height());
		s_UpStatusBar.SetWidth(rect.Width());
		s_UpStatusBar.Fill(colors.crNeither);
		if (!onlygreyrect && m_abyUpPartStatus)
			for (UINT i = 0; i < m_nUpPartCount; ++i)
				if (m_abyUpPartStatus[i])
					s_UpStatusBar.FillRange(i * PARTSIZE, i * PARTSIZE + PARTSIZE, colors.crBoth);

		UploadingToClient_Struct *pUpClientStruct = theApp.uploadqueue->GetUploadingClientStructByClient(this);
		//ASSERT(pUpClientStruct != NULL || theApp.uploadqueue->IsOnUploadQueue((CUpDownClient*)this) != NULL);
		if (pUpClientStruct != NULL) {
			CSingleLock lockBlockLists(&pUpClientStruct->m_csBlockListsLock, TRUE);
			ASSERT(lockBlockLists.IsLocked());
			const Requested_Block_Struct *block;
			if (!pUpClientStruct->m_BlockRequests_queue.IsEmpty()) {
				block = pUpClientStruct->m_BlockRequests_queue.GetHead();
				if (block) {
					uint64 start = (block->StartOffset / PARTSIZE) * PARTSIZE;
					s_UpStatusBar.FillRange(start, start + PARTSIZE, colors.crNextSending);
				}
			}
			if (!pUpClientStruct->m_DoneBlocks_list.IsEmpty()) {
				block = pUpClientStruct->m_DoneBlocks_list.GetHead();
				if (block) {
					uint64 start = (block->StartOffset / PARTSIZE) * PARTSIZE;
					s_UpStatusBar.FillRange(start, start + PARTSIZE, colors.crNextSending);
				}
				for (POSITION pos = pUpClientStruct->m_DoneBlocks_list.GetHeadPosition();pos != 0;) {
					block = pUpClientStruct->m_DoneBlocks_list.GetNext(pos);
					s_UpStatusBar.FillRange(block->StartOffset, block->EndOffset + 1, colors.crSending);
				}
			}
			lockBlockLists.Unlock();
		}
		s_UpStatusBar.Draw(dc, rect.left, rect.top, bFlat);
	}
}

uint16 CUpDownClient::GetUpAvailablePartCount() const
{
	uint16 uAvailableParts = 0;
	if (m_abyUpPartStatus == NULL)
		return uAvailableParts;

	for (UINT uPart = 0; uPart < m_nUpPartCount; ++uPart) {
		if (m_abyUpPartStatus[uPart] != 0)
			++uAvailableParts;
	}
	return uAvailableParts;
}

void CUpDownClient::SetUploadState(EUploadState eNewState)
{
	if (eNewState != m_eUploadState) {
		if (m_eUploadState == US_UPLOADING) {
			// Reset upload data rate computation
			m_nUpDatarate = 0;
			m_nSumForAvgUpDataRate = 0;
			m_AverageUDR_hist.RemoveAll();
		}
		if (eNewState == US_UPLOADING)
			m_fSentOutOfPartReqs = 0;

		// don't add any final cleanups for US_NONE here
		m_eUploadState = eNewState;
		QueueDisplayUpdate(DISPLAY_REFRESH_CLIENT_LIST);
	}
}

/*
 * Gets the queue score multiplier for this client, taking into consideration client's credits
 * and the requested file's priority.
 */
float CUpDownClient::GetCombinedFilePrioAndCredit()
{
	if (!credits)
		return 0.0F;

	return UploadScoreSeams::ComputeCombinedFilePrioAndCredit(credits->GetScoreRatio(GetIP()), GetFilePrioAsNumber());
}

/*
 * Gets the file multiplier for the file this client has requested.
 */
int CUpDownClient::GetFilePrioAsNumber() const
{
	const CKnownFile *currequpfile = theApp.sharedfiles->GetFileByID(requpfileid);
	if (!currequpfile)
		return 0;

	// TODO coded by tecxx & herbert, one yet unsolved problem here:
	// sometimes a client asks for 2 files and there is no way to decide, which file the
	// client finally gets. So it could happen that he is queued first because of a
	// high prio file, but then asks for something completely different.
	switch (currequpfile->GetUpPriority()) {
	case PR_VERYHIGH:
		return 18;
	case PR_HIGH:
		return 9;
	case PR_LOW:
		return 6;
	case PR_VERYLOW:
		return 2;
	//case PR_NORMAL:
	//default:
	//	break;
	}
	return 7;
}

/*
 * Gets the current waiting score for this client, taking into consideration
 * waiting time, priority of requested file, and the client's credits.
 */
uint32 CUpDownClient::GetScore(bool sysvalue, bool isdownloading, bool onlybasevalue) const
{
	return static_cast<uint32>(GetScoreBreakdown(sysvalue, isdownloading, onlybasevalue).uEffectiveScore);
}

UploadScoreSeams::UploadScoreBreakdown CUpDownClient::GetScoreBreakdown(bool sysvalue, bool isdownloading, bool onlybasevalue) const
{
	UploadScoreSeams::UploadScoreBreakdown breakdown = {};
	if (!m_pszUsername)
		return breakdown;

	if (!credits)
		return breakdown;

	const CKnownFile *pRequestedFile = theApp.sharedfiles->GetFileByID(requpfileid);
	if (pRequestedFile == NULL) //is any file requested?
		return breakdown;

	// bad clients (see note in function)
	if (credits->GetCurrentIdentState(GetIP()) == IS_IDBADGUY)
		return breakdown;
	// friend slot
	if (IsFriend() && GetFriendSlot() && !HasLowID()) {
		breakdown.eAvailability = UploadScoreSeams::uploadScoreFriendSlot;
		breakdown.uEffectiveScore = 0x0FFFFFFFu;
		return breakdown;
	}

	if (IsBanned() || m_bGPLEvildoer)
		return breakdown;

	if (sysvalue && HasLowID() && !(socket && socket->IsConnected()))
		return breakdown;

	// calculate score, based on waiting time and other factors
	ULONGLONG ullBaseValue;
	if (onlybasevalue)
		ullBaseValue = SEC2MS(100);
	else if (!isdownloading)
		ullBaseValue = ::GetTickCount64() - GetWaitStartTime();
	else {
		// we don't want one client to download forever
		// the first 15 min download time counts as 15 min waiting time and you get
		// a 15 min bonus while you are in the first 15 min :)
		// (to avoid 20 sec downloads) after this the score won't rise any more
		ullBaseValue = m_dwUploadTime - GetWaitStartTime();
		ullBaseValue += MIN2MS(::GetTickCount64() >= m_dwUploadTime + MIN2MS(15) ? 15 : 30);
		//ASSERT ( m_dwUploadTime - GetWaitStartTime() >= 0 ); //oct 28, 02: changed this from "> 0" to ">= 0" -> // 02-Okt-2006 []: ">=0" is always true!
	}

	const bool bApplyOldClientPenalty = (IsEmuleClient() || GetClientSoft() < 10) && m_byEmuleVersion <= 0x19;
	const UINT uLowIdDivisor = max(1u, thePrefs.GetLowIDDivisor());

	UploadScoreSeams::UploadScoreInputs inputs = {};
	inputs.uBaseValueMs = ullBaseValue;
	inputs.fCreditRatio = credits->GetScoreRatio(GetIP());
	inputs.iFilePrioNumber = GetFilePrioAsNumber();
	inputs.bUseCreditSystem = thePrefs.UseCreditSystem();
	inputs.bApplyPriority = !onlybasevalue;
	inputs.bApplyLowRatioBonus = !onlybasevalue
		&& thePrefs.IsLowRatioBoostEnabled()
		&& pRequestedFile->GetAllTimeUploadRatio() < thePrefs.GetLowRatioThreshold();
	inputs.uLowRatioBonus = thePrefs.GetLowRatioBonus();
	inputs.bApplyLowIdDivisor = !onlybasevalue && HasLowID() && uLowIdDivisor > 1;
	inputs.uLowIdDivisor = uLowIdDivisor;
	inputs.bApplyOldClientPenalty = bApplyOldClientPenalty;
	inputs.bCooldownSuppressed = IsInSlowUploadCooldown();
	return UploadScoreSeams::BuildUploadScoreBreakdown(inputs);
}

bool CUpDownClient::ProcessExtendedInfo(CSafeMemFile &data, CKnownFile *tempreqfile)
{
	delete[] m_abyUpPartStatus;
	m_abyUpPartStatus = NULL;
	m_bUpPartStatusReported = false;
	m_nUpPartStatusSessionUpBaseline = 0;
	m_nUpPartCount = 0;
	m_nUpCompleteSourcesCount = 0;
	if (GetExtendedRequestsVersion() == 0)
		return true;

	uint16 nED2KUpPartCount = data.ReadUInt16();
	if (!nED2KUpPartCount) {
		m_nUpPartCount = tempreqfile->GetPartCount();
		if (!m_nUpPartCount)
			return false;
		m_abyUpPartStatus = new uint8[m_nUpPartCount]{};
	} else {
		if (tempreqfile->GetED2KPartCount() != nED2KUpPartCount) {
			//We already checked if we are talking about the same file. So if we get here, something really strange happened!
			m_nUpPartCount = 0;
			return false;
		}
		m_nUpPartCount = tempreqfile->GetPartCount();
		m_abyUpPartStatus = new uint8[m_nUpPartCount];
		m_bUpPartStatusReported = true;
		m_nUpPartStatusSessionUpBaseline = GetSessionUp();
		for (UINT done = 0; done < m_nUpPartCount;) {
			uint8 toread = data.ReadUInt8();
			for (UINT i = 0; i < 8; ++i) {
				m_abyUpPartStatus[done] = (toread >> i) & 1;
				//We may want to use this for another feature.
				//if (m_abyUpPartStatus[done] && !tempreqfile->IsComplete((uint16)done))
				//	bPartsNeeded = true;
				if (++done >= m_nUpPartCount)
					break;
			}
		}
	}
	if (GetExtendedRequestsVersion() > 1) {
		uint16 nCompleteCountLast = GetUpCompleteSourcesCount();
		uint16 nCompleteCountNew = data.ReadUInt16();
		SetUpCompleteSourcesCount(nCompleteCountNew);
		if (nCompleteCountLast != nCompleteCountNew)
			tempreqfile->UpdatePartsInfo();
	}
	QueueDisplayUpdate(DISPLAY_REFRESH_QUEUE_LIST);
	return true;
}

void CUpDownClient::SetUploadFileID(CKnownFile *newreqfile)
{
	CKnownFile *oldreqfile = theApp.downloadqueue->GetFileByID(requpfileid);
	//We use the knownfile list because we may have unshared the file.
	//But we always check the download list first because that person may re-download
	//this file, which will replace the object in the knownfile list if completed.
	if (oldreqfile == NULL)
		oldreqfile = theApp.knownfiles->FindKnownFileByID(requpfileid);
	else {
		// In some _very_ rare cases it is possible that we have different files with the same hash
		// in the downloads list as well as in the shared list (re-downloading an unshared file,
		// then re-sharing it before the first part has been downloaded)
		// to make sure that in no case a deleted client object remains on the list, we do double check
		// TODO: Fix the whole issue properly
		CKnownFile *pCheck = theApp.sharedfiles->GetFileByID(requpfileid);
		if (pCheck != NULL && pCheck != oldreqfile) {
			ASSERT(0);
			pCheck->RemoveUploadingClient(this);
		}
	}

	if (newreqfile == oldreqfile)
		return;

	// clear old status
	delete[] m_abyUpPartStatus;
	m_abyUpPartStatus = NULL;
	m_bUpPartStatusReported = false;
	m_nUpPartStatusSessionUpBaseline = 0;
	m_nUpPartCount = 0;
	m_nUpCompleteSourcesCount = 0;

	if (newreqfile) {
		newreqfile->AddUploadingClient(this);
		md4cpy(requpfileid, newreqfile->GetFileHash());
	} else
		md4clr(requpfileid);

	if (oldreqfile)
		oldreqfile->RemoveUploadingClient(this);
}

static INT_PTR dbgLastQueueCount = 0;
void CUpDownClient::AddReqBlock(Requested_Block_Struct *reqblock, bool bSignalIOThread)
{
	// do _all_ sanity checks on the requested block here, than put it on the block list for the client
	// UploadDiskIOThread will handle those later on

	if (reqblock != NULL) {
		std::unique_ptr<Requested_Block_Struct> reqblockOwner(reqblock);
		CKnownFile *srcfile = theApp.sharedfiles->GetFileByID(reqblock->FileID);
		const bool bRequestRangeValid = srcfile != NULL
			&& reqblock->StartOffset < reqblock->EndOffset
			&& reqblock->EndOffset <= srcfile->GetFileSize()
			&& reqblock->EndOffset - reqblock->StartOffset <= EMBLOCKSIZE * 3
			&& (!srcfile->IsPartFile() || static_cast<CPartFile*>(srcfile)->IsCompleteBDSafe(reqblock->StartOffset, reqblock->EndOffset - 1));
		if (GetUploadState() != US_UPLOADING) {
			QueuedBlockRequestAdmissionResult eQueuedRequestAdmissionResult =
				GetUploadState() == US_ONUPLOADQUEUE
					? queuedBlockRequestCooldownNotCleared
					: queuedBlockRequestNotOnQueue;
			LPCTSTR pszCooldownClearInstrumentationReason = NULL;
			if (ShouldAttemptUploadRetryCooldownClearOnQueuedRequest(
					GetUploadState() == US_ONUPLOADQUEUE,
					srcfile != NULL,
					bRequestRangeValid))
			{
				// WHY: a no-request recycle or IP-level retry cooldown can race a
				// late OP_REQUESTPARTS and leave a now-requesting peer locally
				// suppressed. Keep the stock rule that queued clients do not
				// accumulate block requests unless the normal broadband cap can
				// immediately reopen the slot.
				const bool bCooldownCleared = theApp.uploadqueue->ClearUploadRetryCooldown(this, &pszCooldownClearInstrumentationReason);
				eQueuedRequestAdmissionResult = theApp.uploadqueue->TryAdmitQueuedBlockRequestClient(this, bCooldownCleared);
				if (bCooldownCleared && thePrefs.GetLogUlDlEvents())
					AddDebugLogLine(DLP_LOW, false, _T("%s: Upload retry cooldown cleared after queued block request."), GetUserName());
			}
			if (eQueuedRequestAdmissionResult == queuedBlockRequestAdmitted && GetUploadState() == US_UPLOADING) {
#ifdef EMULEBB_ENABLE_UPLOAD_SLOT_DIAGNOSTICS
				LogUploadReqBlockInstrumentation(this, _T("accept-queued-request-direct-admit"), reqblock, theApp.uploadqueue->GetUploadingClientStructByClient(this), -1, -1);
#endif
#if EMULEBB_HAS_BAD_PEER_DIAGNOSTICS
				CString strEvidence;
				strEvidence.Format(
					_T("{\"cooldown_cleared\":true,\"request_range_valid\":%s,\"start_offset\":%I64u,\"end_offset\":%I64u}"),
					bRequestRangeValid ? _T("true") : _T("false"),
					static_cast<uint64>(reqblock->StartOffset),
					static_cast<uint64>(reqblock->EndOffset));
				EMULEBB_BAD_PEER_LOG_CLIENT_EVENT(_T("upload_queued_request_direct_admit"), _T("low"), this, _T("admit_upload_slot"), _T("Queued block request cleared retry cooldown and reopened upload slot"), srcfile, strEvidence);
#endif
			} else {
#ifdef EMULEBB_ENABLE_UPLOAD_SLOT_DIAGNOSTICS
				LogUploadReqBlockInstrumentation(this,
					eQueuedRequestAdmissionResult == queuedBlockRequestCooldownNotCleared && pszCooldownClearInstrumentationReason != NULL
						? pszCooldownClearInstrumentationReason
						: GetQueuedBlockRequestAdmissionInstrumentationReason(eQueuedRequestAdmissionResult),
					reqblock, NULL, -1, -1);
#endif
#if EMULEBB_HAS_BAD_PEER_DIAGNOSTICS
				const LPCTSTR pszAdmissionReason =
					eQueuedRequestAdmissionResult == queuedBlockRequestCooldownNotCleared && pszCooldownClearInstrumentationReason != NULL
						? pszCooldownClearInstrumentationReason
						: GetQueuedBlockRequestAdmissionBadPeerReason(eQueuedRequestAdmissionResult);
				CString strEvidence;
				strEvidence.Format(
					_T("{\"file_known\":%s,\"request_range_valid\":%s,\"admission_reason\":%s,\"start_offset\":%I64u,\"end_offset\":%I64u}"),
					srcfile != NULL ? _T("true") : _T("false"),
					bRequestRangeValid ? _T("true") : _T("false"),
					(LPCTSTR)BadPeerInstrumentationSeams::EvidenceJsonString(pszAdmissionReason),
					static_cast<uint64>(reqblock->StartOffset),
					static_cast<uint64>(reqblock->EndOffset));
				EMULEBB_BAD_PEER_LOG_CLIENT_EVENT(_T("upload_queued_request_rejected"), _T("medium"), this, _T("reject_block_request"), _T("Queued block request could not reopen upload slot"), srcfile, strEvidence);
#endif
				if (thePrefs.GetLogUlDlEvents())
					AddDebugLogLine(DLP_LOW, false, _T("UploadClient: Client tried to add req block when not in upload slot! Prevented req blocks from being added. %s"), (LPCTSTR)DbgGetClientInfo());
				return;
			}
		}

		if (HasCollectionUploadSlot()) {
			// This is a correctness guard only. Collection traffic no longer gets
			// a scheduling exception, but a client that started a collection
			// upload must not switch block requests to an unrelated file mid-slot.
			CKnownFile *pDownloadingFile = theApp.sharedfiles->GetFileByID(reqblock->FileID);
			if (pDownloadingFile != NULL) {
				if (!CCollection::HasCollectionExtention(pDownloadingFile->GetFileName()) || pDownloadingFile->GetFileSize() > (uint64)MAXPRIORITYCOLL_SIZE) {
#ifdef EMULEBB_ENABLE_UPLOAD_SLOT_DIAGNOSTICS
					LogUploadReqBlockInstrumentation(this, _T("reject-collection-slot-file-switch"), reqblock, NULL, -1, -1);
#endif
					AddDebugLogLine(DLP_HIGH, false, _T("UploadClient: Client tried to add req block for non-collection while having a collection slot! Prevented req blocks from being added. %s"), (LPCTSTR)DbgGetClientInfo());
					return;
				}
			} else
				ASSERT(0);
		}

		if (srcfile == NULL) {
#ifdef EMULEBB_ENABLE_UPLOAD_SLOT_DIAGNOSTICS
			LogUploadReqBlockInstrumentation(this, _T("reject-file-not-shared"), reqblock, NULL, -1, -1);
#endif
			DebugLogWarning(GetResString(IDS_ERR_REQ_FNF));
			return;
		}

		UploadingToClient_Struct *pUploadingClientStruct = theApp.uploadqueue->GetUploadingClientStructByClient(this);
		if (pUploadingClientStruct == NULL) {
#ifdef EMULEBB_ENABLE_UPLOAD_SLOT_DIAGNOSTICS
			LogUploadReqBlockInstrumentation(this, _T("reject-upload-struct-missing"), reqblock, NULL, -1, -1);
#endif
			DebugLogError(_T("AddReqBlock: Uploading client not found in Uploadlist, %s, %s"), (LPCTSTR)DbgGetClientInfo(), (LPCTSTR)FormatDisplayFileName(srcfile->GetFileName()));
			return;
		}

		if (pUploadingClientStruct->m_bIOError) {
#ifdef EMULEBB_ENABLE_UPLOAD_SLOT_DIAGNOSTICS
			LogUploadReqBlockInstrumentation(this, _T("reject-pending-io-error"), reqblock, pUploadingClientStruct, -1, -1);
#endif
			DebugLogWarning(_T("AddReqBlock: Uploading client has pending IO Error, %s, %s"), (LPCTSTR)DbgGetClientInfo(), (LPCTSTR)FormatDisplayFileName(srcfile->GetFileName()));
			return;
		}

		if (srcfile->IsPartFile() && !static_cast<CPartFile*>(srcfile)->IsCompleteBDSafe(reqblock->StartOffset, reqblock->EndOffset - 1)) {
#ifdef EMULEBB_ENABLE_UPLOAD_SLOT_DIAGNOSTICS
			LogUploadReqBlockInstrumentation(this, _T("reject-incomplete-local-block"), reqblock, pUploadingClientStruct, -1, -1);
#endif
			DebugLogWarning(_T("AddReqBlock: %s, %s"), (LPCTSTR)GetResString(IDS_ERR_INCOMPLETEBLOCK), (LPCTSTR)DbgGetClientInfo(), (LPCTSTR)FormatDisplayFileName(srcfile->GetFileName()));
			return;
		}

		if (reqblock->StartOffset >= reqblock->EndOffset || reqblock->EndOffset > srcfile->GetFileSize()) {
#ifdef EMULEBB_ENABLE_UPLOAD_SLOT_DIAGNOSTICS
			LogUploadReqBlockInstrumentation(this, _T("reject-invalid-range"), reqblock, pUploadingClientStruct, -1, -1);
#endif
			DebugLogError(_T("AddReqBlock: Invalid Block requests (negative or bytes to read, read after EOF), %s, %s"), (LPCTSTR)DbgGetClientInfo(), (LPCTSTR)FormatDisplayFileName(srcfile->GetFileName()));
			return;
		}

		if (reqblock->EndOffset - reqblock->StartOffset > EMBLOCKSIZE * 3) {
#ifdef EMULEBB_ENABLE_UPLOAD_SLOT_DIAGNOSTICS
			LogUploadReqBlockInstrumentation(this, _T("reject-too-large"), reqblock, pUploadingClientStruct, -1, -1);
#endif
			DebugLogWarning(_T("AddReqBlock: %s, %s"), (LPCTSTR)GetResString(IDS_ERR_LARGEREQBLOCK), (LPCTSTR)DbgGetClientInfo(), (LPCTSTR)FormatDisplayFileName(srcfile->GetFileName()));
			return;
		}

		CSingleLock lockBlockLists(&pUploadingClientStruct->m_csBlockListsLock, TRUE);
		if (!lockBlockLists.IsLocked()) {
#ifdef EMULEBB_ENABLE_UPLOAD_SLOT_DIAGNOSTICS
			LogUploadReqBlockInstrumentation(this, _T("reject-block-list-lock-failed"), reqblock, pUploadingClientStruct, -1, -1);
#endif
			ASSERT(0);
			return;
		}

		const UploadBlockRequestSeams::SUploadBlockRequestKey requestKey =
			UploadBlockRequestSeams::BuildUploadBlockRequestKey(reqblock->StartOffset, reqblock->EndOffset, reqblock->FileID);
		if (pUploadingClientStruct->m_DoneBlocks_keys.find(requestKey) != pUploadingClientStruct->m_DoneBlocks_keys.end()) {
#ifdef EMULEBB_ENABLE_UPLOAD_SLOT_DIAGNOSTICS
			LogUploadReqBlockInstrumentation(this, _T("reject-duplicate-done-block"), reqblock, pUploadingClientStruct, pUploadingClientStruct->m_BlockRequests_queue.GetCount(), pUploadingClientStruct->m_DoneBlocks_list.GetCount());
#endif
			return;
		}
		if (pUploadingClientStruct->m_BlockRequests_keys.find(requestKey) != pUploadingClientStruct->m_BlockRequests_keys.end()) {
#ifdef EMULEBB_ENABLE_UPLOAD_SLOT_DIAGNOSTICS
			LogUploadReqBlockInstrumentation(this, _T("reject-duplicate-queued-block"), reqblock, pUploadingClientStruct, pUploadingClientStruct->m_BlockRequests_queue.GetCount(), pUploadingClientStruct->m_DoneBlocks_list.GetCount());
#endif
			return;
		}
		// WHY: m_BlockRequests_queue is an MFC list. This function receives
		// ownership of reqblock from packet parsing; keep it locally owned until
		// AddTail succeeds so a low-memory list-node failure cannot leak the
		// requested upload block. Roll the duplicate key back if list insertion
		// fails so later requests are not falsely classified as queued.
		pUploadingClientStruct->m_BlockRequests_keys.insert(requestKey);
		try {
			pUploadingClientStruct->m_BlockRequests_queue.AddTail(reqblockOwner.get());
		} catch (...) {
			pUploadingClientStruct->m_BlockRequests_keys.erase(requestKey);
			throw;
		}
		reqblockOwner.release();
		dbgLastQueueCount = pUploadingClientStruct->m_BlockRequests_queue.GetCount();
		pUploadingClientStruct->m_ullLastAcceptedReqBlockTick.store(::GetTickCount64());
#ifdef EMULEBB_ENABLE_UPLOAD_SLOT_DIAGNOSTICS
		LogUploadReqBlockInstrumentation(this, _T("accept-queued-block"), pUploadingClientStruct->m_BlockRequests_queue.GetTail(), pUploadingClientStruct, pUploadingClientStruct->m_BlockRequests_queue.GetCount(), pUploadingClientStruct->m_DoneBlocks_list.GetCount());
#endif
		lockBlockLists.Unlock(); // not needed, just to make it visible
	}
	if (bSignalIOThread && theApp.m_pUploadDiskIOThread != NULL) {
#ifdef EMULEBB_ENABLE_UPLOAD_SLOT_DIAGNOSTICS
		if (reqblock == NULL) {
			UploadingToClient_Struct *pUploadingClientStruct = theApp.uploadqueue->GetUploadingClientStructByClient(this);
			if (pUploadingClientStruct != NULL) {
				CSingleLock lockBlockLists(&pUploadingClientStruct->m_csBlockListsLock, TRUE);
				LogUploadReqBlockInstrumentation(this, _T("request-packet-complete-signal"), NULL, pUploadingClientStruct, pUploadingClientStruct->m_BlockRequests_queue.GetCount(), pUploadingClientStruct->m_DoneBlocks_list.GetCount());
			} else
				LogUploadReqBlockInstrumentation(this, _T("request-packet-complete-signal-no-upload-struct"), NULL, NULL, -1, -1);
		}
#endif
		/*DebugLog(_T("BlockRequest Packet received, we have currently %u waiting requests and %s data in buffer (%u in ready packets, %s in pending IO Disk read), socket busy: %s")
			, dbgLastQueueCount
			, (LPCTSTR)CastItoXBytes(GetQueueSessionUploadAdded() - (GetQueueSessionPayloadUp() + socket->GetSentPayloadSinceLastCall(false)), false, false, 2)
			, socket->DbgGetStdQueueCount()
			, (LPCTSTR)CastItoXBytes((uint32)theApp.m_pUploadDiskIOThread->dbgDataReadPending, false, false, 2)
			,_T('?')); */
		theApp.m_pUploadDiskIOThread->WakeUpCall();
	}
}

void CUpDownClient::UpdateUploadingStatisticsData()
{
	const ULONGLONG curTick = ::GetTickCount64();
	uint32 sentBytesFile;
	CEMSocket *sock = GetFileUploadSocket();
	if (sock) {
		// Extended statistics information based on which client software and which port we sent this data to...
		// This also updates the grand total for sent bytes, etc.  And where this data came from.
		uint32 sentBytesCompleteFile = (uint32)sock->GetSentBytesCompleteFileSinceLastCallAndReset();
		uint32 sentBytesPartFile = (uint32)sock->GetSentBytesPartFileSinceLastCallAndReset();
		sentBytesFile = sentBytesCompleteFile + sentBytesPartFile;
		thePrefs.Add2SessionTransferData(GetClientSoft(), GetUserPort(), false, true, sentBytesCompleteFile, (IsFriend() && GetFriendSlot()));
		thePrefs.Add2SessionTransferData(GetClientSoft(), GetUserPort(), true, true, sentBytesPartFile, (IsFriend() && GetFriendSlot()));

		m_nTransferredUp += sentBytesFile;
		credits->AddUploaded(sentBytesFile, GetIP());

		uint32 sentBytesPayload = sock->GetSentPayloadSinceLastCall(true);
		m_nCurQueueSessionPayloadUp += sentBytesPayload;

		// in some rare cases (namely, switching upload files while data still is in the send queue),
		// we count some bytes for the wrong file, but fixing it (and not counting data only based on
		// what was put into the queue and not sent yet) isn't really worth it
		CKnownFile *pCurrentUploadFile = theApp.sharedfiles->GetFileByID(GetUploadFileID());
		if (pCurrentUploadFile != NULL)
			pCurrentUploadFile->statistic.AddTransferred(sentBytesPayload);
		//else
		//	ASSERT(0); //fired after deleting shared files which had uploads in the current eMule session. Closing this messagebox caused no issues.
	} else
		sentBytesFile = 0;

	if (sentBytesFile > 0 || m_AverageUDR_hist.IsEmpty() || curTick >= m_AverageUDR_hist.Tail().timestamp + SEC2MS(1)) {
		// Store how much data we've transferred in this round,
		// to be able to calculate average speed later
		// keep up to date the sum of all values in the list
		m_AverageUDR_hist.AddTail(TransferredData{sentBytesFile, curTick});
		m_nSumForAvgUpDataRate += sentBytesFile;
	}

	// remove old entries from the list and adjust the sum of all values
	while (!m_AverageUDR_hist.IsEmpty() && curTick >= m_AverageUDR_hist.Head().timestamp + SEC2MS(10)) {
		m_nSumForAvgUpDataRate -= m_AverageUDR_hist.Head().datalen;
		m_AverageUDR_hist.RemoveHead();
	}

	// Calculate average speed for this slot
	if (!m_AverageUDR_hist.IsEmpty() && curTick > m_AverageUDR_hist.Head().timestamp && GetUpStartTimeDelay() > SEC2MS(2))
		m_nUpDatarate = (UINT)(SEC2MS(m_nSumForAvgUpDataRate) / (curTick - m_AverageUDR_hist.Head().timestamp));
	else
		m_nUpDatarate = 0; // not enough data to calculate trustworthy speed

	QueueDisplayUpdate(DISPLAY_REFRESH_UPLOAD_LIST | DISPLAY_REFRESH_CLIENT_LIST);
}

void CUpDownClient::ResetSlowUploadTracking()
{
	m_ullSlowUploadAccumulatedMs = 0;
	m_ullZeroUploadAccumulatedMs = 0;
	m_ullLastSlowUploadSampleTick = 0;
}

void CUpDownClient::UpdateSlowUploadTracking(ULONGLONG curTick, uint32 slowThresholdBytesPerSec)
{
	if (m_ullLastSlowUploadSampleTick == 0 || curTick <= m_ullLastSlowUploadSampleTick) {
		m_ullLastSlowUploadSampleTick = curTick;
		return;
	}

	const ULONGLONG ullDelta = curTick - m_ullLastSlowUploadSampleTick;
	m_ullLastSlowUploadSampleTick = curTick;

	// Slow-slot accounting is deliberately local and monotonic: the queue owns
	// the policy decision, while the client only accumulates "slow" and "zero"
	// time for an already-active slot. Callers reset these counters whenever the
	// slot leaves upload, is still warming up, or no longer meets recycle
	// preconditions.
	if (GetUploadState() != US_UPLOADING || GetUpStartTimeDelay() < SEC2MS(2)) {
		m_ullSlowUploadAccumulatedMs = 0;
		m_ullZeroUploadAccumulatedMs = 0;
		return;
	}

	const uint32 uRate = GetUploadDatarate();
	if (uRate == 0) {
		m_ullZeroUploadAccumulatedMs += ullDelta;
		m_ullSlowUploadAccumulatedMs += ullDelta;
		return;
	}

	if (uRate < slowThresholdBytesPerSec) {
		m_ullSlowUploadAccumulatedMs += ullDelta;
		if (m_ullZeroUploadAccumulatedMs > ullDelta)
			m_ullZeroUploadAccumulatedMs -= ullDelta;
		else
			m_ullZeroUploadAccumulatedMs = 0;
		return;
	}

	if (m_ullSlowUploadAccumulatedMs > ullDelta)
		m_ullSlowUploadAccumulatedMs -= ullDelta;
	else
		m_ullSlowUploadAccumulatedMs = 0;

	if (m_ullZeroUploadAccumulatedMs > ullDelta)
		m_ullZeroUploadAccumulatedMs -= ullDelta;
	else
		m_ullZeroUploadAccumulatedMs = 0;
}

bool CUpDownClient::ShouldRecycleSlowUpload(UINT slowGraceMs, UINT zeroGraceMs) const
{
	return m_ullZeroUploadAccumulatedMs >= zeroGraceMs
		|| m_ullSlowUploadAccumulatedMs >= slowGraceMs;
}

bool CUpDownClient::IsInSlowUploadCooldown() const
{
	return GetSlowUploadCooldownRemaining() != 0;
}

ULONGLONG CUpDownClient::GetSlowUploadCooldownRemaining() const
{
	const ULONGLONG curTick = ::GetTickCount64();
	return (m_ullSlowUploadCooldownUntil > curTick) ? m_ullSlowUploadCooldownUntil - curTick : 0;
}

void CUpDownClient::SendOutOfPartReqsAndAddToWaitingQueue()
{
	//OP_OUTOFPARTREQS will tell the downloading client to go back to OnQueue.
	//The main reason for this is that if we put the client back on queue and it goes
	//back to the upload before the socket times out... We get a situation where the
	//downloader thinks it already sent the requested blocks and the uploader thinks
	//the downloader didn't send any block requests. Then the connection times out.
	//I did some tests with eDonkey also and it seems to work well with them also.
	if (thePrefs.GetDebugClientTCPLevel() > 0)
		DebugSend("OP_OutOfPartReqs", this);
	Packet *pPacket = new Packet(OP_OUTOFPARTREQS, 0);
	theStats.AddUpDataOverheadFileRequest(pPacket->size);
	SendPacket(pPacket);
	m_fSentOutOfPartReqs = 1;
	theApp.uploadqueue->AddClientToQueue(this, true);
}

/*
 * See description for CEMSocket::TruncateQueues().
 */
void CUpDownClient::FlushSendBlocks() // call this when you stop upload, or the socket might be not able to send
{
	if (socket) //socket may be NULL...
		socket->TruncateQueues();
}

void CUpDownClient::SendHashsetPacket(const uchar *pData, uint32 nSize, bool bFileIdentifiers)
{
	Packet *packet;
	CSafeMemFile fileResponse(1024);
	if (bFileIdentifiers) {
		CSafeMemFile data(pData, nSize);
		CFileIdentifierSA fileIdent;
		if (!fileIdent.ReadIdentifier(data))
			throw _T("Bad FileIdentifier (OP_HASHSETREQUEST2)");
		CKnownFile *file = theApp.sharedfiles->GetFileByIdentifier(fileIdent, false);
		if (file == NULL) {
			CheckFailedFileIdReqs(fileIdent.GetMD4Hash());
			throw GetResString(IDS_ERR_REQ_FNF) + _T(" (SendHashsetPacket2)");
		}
		uint8 byOptions = data.ReadUInt8();
		bool bMD4 = (byOptions & 0x01) > 0;
		bool bAICH = (byOptions & 0x02) > 0;
		if (!bMD4 && !bAICH) {
			DebugLogWarning(_T("Client sent HashSet request with none or unknown HashSet type requested (%u) - file: %s, client %s")
				, byOptions, (LPCTSTR)FormatDisplayFileName(file->GetFileName()), (LPCTSTR)DbgGetClientInfo());
			return;
		}
		const CFileIdentifier &fileid = file->GetFileIdentifier();
		fileid.WriteIdentifier(fileResponse);
		// even if we don't happen to have an AICH hashset yet for some reason we send a proper (possibly empty) response
		fileid.WriteHashSetsToPacket(fileResponse, bMD4, bAICH);
		if (thePrefs.GetDebugClientTCPLevel() > 0)
			DebugSend("OP_HashSetAnswer", this, fileid.GetMD4Hash());
		packet = new Packet(fileResponse, OP_EMULEPROT, OP_HASHSETANSWER2);
	} else {
		if (nSize != 16) {
			ASSERT(0);
			return;
		}
		CKnownFile *file = theApp.sharedfiles->GetFileByID(pData);
		if (!file) {
			CheckFailedFileIdReqs(pData);
			throw GetResString(IDS_ERR_REQ_FNF) + _T(" (SendHashsetPacket)");
		}
		file->GetFileIdentifier().WriteMD4HashsetToFile(fileResponse);
		if (thePrefs.GetDebugClientTCPLevel() > 0)
			DebugSend("OP_HashSetAnswer", this, pData);
		packet = new Packet(fileResponse, OP_EDONKEYPROT, OP_HASHSETANSWER);
	}
	theStats.AddUpDataOverheadFileRequest(packet->size);
	SendPacket(packet);
}

void CUpDownClient::SendRankingInfo()
{
	if (!ExtProtocolAvailable())
		return;
	UINT nRank = theApp.uploadqueue->GetWaitingPosition(this);
	if (!nRank)
		return;
	Packet *packet = new Packet(OP_QUEUERANKING, 12, OP_EMULEPROT);
	PokeUInt16(packet->pBuffer, (uint16)nRank);
	memset(packet->pBuffer + 2, 0, 10);
	if (thePrefs.GetDebugClientTCPLevel() > 0)
		DebugSend("OP_QueueRank", this);
	theStats.AddUpDataOverheadFileRequest(packet->size);
	SendPacket(packet);
}

void CUpDownClient::SendCommentInfo(/*const */CKnownFile *file)
{
	if (!m_bCommentDirty || file == NULL || !ExtProtocolAvailable() || m_byAcceptCommentVer < 1)
		return;
	m_bCommentDirty = false;

	UINT rating = file->GetFileRating();
	const CString &desc(file->GetFileComment());
	if (rating == 0 && desc.IsEmpty())
		return;

	CSafeMemFile data(256);
	data.WriteUInt8((uint8)rating);
	data.WriteLongString(desc, GetUnicodeSupport());
	if (thePrefs.GetDebugClientTCPLevel() > 0)
		DebugSend("OP_FileDesc", this, file->GetFileHash());
	Packet *packet = new Packet(data, OP_EMULEPROT);
	packet->opcode = OP_FILEDESC;
	theStats.AddUpDataOverheadFileRequest(packet->size);
	SendPacket(packet);
}

void CUpDownClient::AddRequestCount(const uchar *fileid)
{
	const ULONGLONG curTick = ::GetTickCount64();

	for (POSITION pos = m_RequestedFiles_list.GetHeadPosition(); pos != NULL;) {
		Requested_File_Struct *cur_struct = m_RequestedFiles_list.GetNext(pos);
		if (md4equ(cur_struct->fileid, fileid)) {
			if (curTick < cur_struct->lastasked + MIN_REQUESTTIME && !GetFriendSlot()) {
				cur_struct->badrequests += static_cast<uint8>(GetDownloadState() != DS_DOWNLOADING);
				if (cur_struct->badrequests == BADCLIENTBAN)
					Ban();
			} else
				cur_struct->badrequests -= static_cast<uint8>(cur_struct->badrequests > 0);

			cur_struct->lastasked = curTick;
			return;
		}
	}
	Requested_File_Struct *new_struct = new Requested_File_Struct;
	md4cpy(new_struct->fileid, fileid);
	new_struct->lastasked = curTick;
	new_struct->badrequests = 0;
	m_RequestedFiles_list.AddHead(new_struct);
}

void  CUpDownClient::UnBan()
{
	theApp.clientlist->AddTrackClient(this);
	theApp.clientlist->RemoveBannedClient(this);
	SetUploadState(US_NONE);
	ClearWaitStartTime();
	theApp.emuledlg->transferwnd->ShowQueueCount(theApp.uploadqueue->GetWaitingUserCount());
	for (POSITION pos = m_RequestedFiles_list.GetHeadPosition(); pos != NULL;) {
		Requested_File_Struct *cur_struct = m_RequestedFiles_list.GetNext(pos);
		cur_struct->badrequests = 0;
		cur_struct->lastasked = 0;
	}
}

void CUpDownClient::Ban(LPCTSTR pszReason, ClientBanScope eScope)
{
	SetChatState(MS_NONE);
	theApp.clientlist->AddTrackClient(this);
	const bool bRequestedScopeAlreadyBanned = theApp.clientlist->IsBannedClient(this, eScope);
#if EMULEBB_HAS_BAD_PEER_DIAGNOSTICS
	CString strBanEvidence;
	strBanEvidence.Format(_T("{\"scope\":\"%s\"}"), GetClientBanScopeInstrumentationToken(this, eScope));
	EMULEBB_BAD_PEER_LOG_CLIENT_EVENT(_T("client_ban"), _T("high"), this, _T("ban"), pszReason == NULL ? _T("Aggressive behaviour") : pszReason, NULL, strBanEvidence);
#endif
	if (!bRequestedScopeAlreadyBanned) {
		if (thePrefs.GetLogBannedClients())
			AddDebugLogLine(false, _T("Banned: %s; %s"), pszReason == NULL ? _T("Aggressive behaviour") : pszReason, (LPCTSTR)DbgGetClientInfo());
	}
#ifdef _DEBUG
	else {
		if (thePrefs.GetLogBannedClients())
			AddDebugLogLine(false, _T("Banned: (refreshed): %s; %s"), pszReason == NULL ? _T("Aggressive behaviour") : pszReason, (LPCTSTR)DbgGetClientInfo());
	}
#endif
	theApp.clientlist->AddBannedClient(this, eScope);
	if (theApp.uploadqueue != NULL && theApp.uploadqueue->IsOnUploadQueue(this)) {
		// WHY: the waiting queue stores raw CUpDownClient pointers and does not own
		// a separate lifetime token.  If a queued peer is banned, keeping that queue
		// entry while marking the client US_BANNED lets unrelated download cleanup
		// paths conclude that no upload-side owner remains and delete the object.
		// Drop the waiting-list edge before setting US_BANNED so the final state is
		// still visible as banned and no queue entry can later dereference freed memory.
		theApp.uploadqueue->RemoveFromWaitingQueue(this, true);
	}
	SetUploadState(US_BANNED);
	theApp.emuledlg->transferwnd->ShowQueueCount(theApp.uploadqueue->GetWaitingUserCount());
	QueueDisplayUpdate(DISPLAY_REFRESH_QUEUE_LIST);
	if (socket != NULL && socket->IsConnected())
		socket->ShutDown(CAsyncSocket::receives); // let the socket timeout, since we don't want to risk to delete the client right now. This isn't actually perfect, could be changed later
}

ULONGLONG CUpDownClient::GetWaitStartTime() const
{
	if (credits == NULL) {
		ASSERT(0);
		return 0;
	}
	ULONGLONG dwResult = credits->GetSecureWaitStartTime(GetIP());
	if (dwResult > m_dwUploadTime && IsDownloading()) {
		//this happens only if two clients with invalid securehash are in the queue - if at all
		dwResult = m_dwUploadTime - 1;

		if (thePrefs.GetVerbose())
			DEBUG_ONLY(AddDebugLogLine(false, _T("Warning: CUpDownClient::GetWaitStartTime() waittime Collision (%s)"), GetUserName()));
	}
	return dwResult;
}

ULONGLONG CUpDownClient::GetWaitTime() const
{
	const ULONGLONG ullWaitStart = GetWaitStartTime();
	if (ullWaitStart == 0)
		return 0;

	const ULONGLONG ullWaitEnd = IsDownloading() ? m_dwUploadTime : ::GetTickCount64();
	// WHY: queued clients do not have an upload-start timestamp yet. Using
	// m_dwUploadTime for them underflows REST/UI wait-time reporting and hides
	// the real queue age while upload-capacity tuning relies on that signal.
	return ullWaitEnd >= ullWaitStart ? ullWaitEnd - ullWaitStart : 0;
}

void CUpDownClient::SetWaitStartTime()
{
	if (credits != NULL)
		credits->SetSecWaitStartTime(GetIP());
}

void CUpDownClient::ClearWaitStartTime()
{
	if (credits != NULL)
		credits->ClearWaitStartTime();
}

bool CUpDownClient::GetFriendSlot() const
{
	if (credits && theApp.clientcredits->CryptoAvailable())
		switch (credits->GetCurrentIdentState(GetIP())) {
		case IS_IDFAILED:
		case IS_IDNEEDED:
		case IS_IDBADGUY:
			return false;
		}

	return m_bFriendSlot;
}

CEMSocket* CUpDownClient::GetFileUploadSocket(bool bLog)
{
	if (bLog && thePrefs.GetVerbose())
		AddDebugLogLine(false, _T("%s got normal socket."), (LPCTSTR)DbgGetClientInfo());
	return socket;
}

void CUpDownClient::SetCollectionUploadSlot(bool bValue)
{
	ASSERT(!IsDownloading() || bValue == m_bCollectionUploadSlot);
	m_bCollectionUploadSlot = bValue;
}
