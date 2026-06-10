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
#include "StdAfx.h"
#include <timeapi.h>
#include "updownclient.h"
#include "uploaddiskiothread.h"
#include "emule.h"
#include "UploadQueue.h"
#include "sharedfilelist.h"
#include "partfile.h"
#include "log.h"
#include "preferences.h"
#include "safefile.h"
#include "listensocket.h"
#include "LockScopeSeams.h"
#include "HelperThreadLaunchSeams.h"
#include "UploadDiskIOThreadSeams.h"
#include "packets.h"
#include "Statistics.h"
#include "UploadBandwidthThrottler.h"
#include "zlib.h"

#include <memory>
#include <new>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define RUN_STOP	0
#define RUN_IDLE	1
#define RUN_WORK	2
#define WAKEUP		((ULONG_PTR)(~0))

namespace
{
void DeletePacketList(CPacketList &rPacketList)
{
	while (!rPacketList.IsEmpty())
		delete rPacketList.RemoveHead();
}

class CUploadDiskSocketDeliveryRef
{
public:
	~CUploadDiskSocketDeliveryRef()
	{
		Release();
	}

	void Attach(CClientReqSocket *pSocket)
	{
		Release();
		m_pSocket = pSocket;
	}

	void Release()
	{
		if (m_pSocket != NULL) {
			theApp.listensocket->ReleaseUploadDiskPacketDeliveryRef(m_pSocket);
			m_pSocket = NULL;
		}
	}

	CClientReqSocket *Get() const
	{
		return m_pSocket;
	}

private:
	CClientReqSocket *m_pSocket = NULL;
};
}

IMPLEMENT_DYNCREATE(CUploadDiskIOThread, CWinThread)

CUploadDiskIOThread::CUploadDiskIOThread()
	: m_eventThreadEnded(FALSE, TRUE)
	, m_hPort()
#ifdef _DEBUG
	, dbgDataReadPending()
#endif
	, m_bThreadStarted()
	, m_bStopRequested()
	, m_Run(RUN_STOP)
	, m_bNewData()
	, m_nPendingIoCount()
	, m_bSignalThrottler()
{
	ASSERT(theApp.uploadqueue != NULL);
#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
	const ULONGLONG ullPhaseStart = theApp.GetStartupDiagnosticsTimestampUs();
#endif
	CWinThread *pThread = AfxBeginThread(RunProc, (LPVOID)this);
	m_bThreadStarted = HelperThreadLaunchSeams::DidStartThread(pThread);
	if (!m_bThreadStarted) {
		theApp.QueueDebugLogLineEx(LOG_ERROR, _T("Failed to start upload disk I/O helper thread"));
		m_eventThreadEnded.SetEvent();
	}
#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
	theApp.AppendStartupDiagnosticsLine(_T("broadband.upload_disk_io.launch_thread"), theApp.GetStartupDiagnosticsElapsedUs(ullPhaseStart), ullPhaseStart);
#endif
}

CUploadDiskIOThread::~CUploadDiskIOThread()
{
	ASSERT(!m_hPort && HelperThreadLaunchSeams::GetState(m_Run) == RUN_STOP);
}

UINT AFX_CDECL CUploadDiskIOThread::RunProc(LPVOID pParam)
{
#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
	theApp.AppendStartupDiagnosticsLine(_T("broadband.upload_disk_io.thread_enter"), 0);
#endif
	DbgSetThreadName("UploadDiskIOThread");
	InitThreadLocale();
	return pParam ? static_cast<CUploadDiskIOThread*>(pParam)->RunInternal() : 1;
}

void CUploadDiskIOThread::EndThread()
{
	const HelperThreadLaunchSeams::IocpShutdownAction action =
		HelperThreadLaunchSeams::RequestIocpShutdown(m_bStopRequested, m_Run, RUN_STOP, m_bThreadStarted, m_hPort);
	if (action == HelperThreadLaunchSeams::IocpShutdownAction::NoOp)
		return;

	HelperThreadLaunchSeams::WaitForEventThreadShutdown(
		m_eventThreadEnded,
		m_bThreadStarted,
		HelperThreadLaunchSeams::kHelperThreadShutdownWaitMs,
		[]() {
			theApp.QueueDebugLogLineEx(LOG_ERROR, _T("Timed out waiting for upload disk I/O helper thread; waiting for cooperative exit"));
		},
		[](DWORD dwLastError) {
			theApp.QueueDebugLogLineEx(LOG_ERROR, _T("Failed waiting for upload disk I/O helper thread: %s"), (LPCTSTR)GetErrorMessage(dwLastError, 1));
		});
}

UINT CUploadDiskIOThread::RunInternal()
{
#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
	const ULONGLONG ullThreadStartUs = theApp.GetStartupDiagnosticsTimestampUs();
#endif
	DWORD dwError = ERROR_SUCCESS;
	if (!HelperThreadLaunchSeams::TryCreateIocpPort(m_hPort, dwError)) {
		theApp.QueueDebugLogLineEx(LOG_ERROR, _T("Failed to create upload disk I/O completion port: %s"), (LPCTSTR)GetErrorMessage(dwError, 1));
		HelperThreadLaunchSeams::SetState(m_Run, RUN_STOP);
		m_eventThreadEnded.SetEvent();
		return dwError;
	}
	if (HelperThreadLaunchSeams::IsFlagSet(m_bStopRequested)) {
		HelperThreadLaunchSeams::CloseIocpPort(m_hPort);
		HelperThreadLaunchSeams::SetState(m_Run, RUN_STOP);
		m_eventThreadEnded.SetEvent();
		return 0;
	}

	DWORD dwRead = 0;
	ULONG_PTR completionKey = 0;
	OverlappedRead_Struct *pCurIO = NULL;
	HelperThreadLaunchSeams::SetState(m_Run, RUN_IDLE);
#if EMULEBB_HAS_STARTUP_DIAGNOSTICS
	theApp.AppendStartupDiagnosticsLine(_T("broadband.upload_disk_io.thread_ready"), theApp.GetStartupDiagnosticsElapsedUs(ullThreadStartUs), ullThreadStartUs);
#endif
	while (HelperThreadLaunchSeams::ShouldWaitForIocpWorkerCompletion(
		HelperThreadLaunchSeams::IsFlagSet(m_bStopRequested),
		HelperThreadLaunchSeams::GetState(m_Run),
		RUN_STOP))
	{
		const BOOL bCompletionReceived = ::GetQueuedCompletionStatus(m_hPort, &dwRead, &completionKey, (LPOVERLAPPED*)&pCurIO, INFINITE);
		DWORD dwCompletionError = bCompletionReceived != FALSE ? ERROR_SUCCESS : ::GetLastError();
		if (HelperThreadLaunchSeams::IsIocpStopCompletion(bCompletionReceived, completionKey, pCurIO)
			|| !HelperThreadLaunchSeams::ShouldProcessIocpWorkerCompletion(bCompletionReceived, completionKey, pCurIO))
			break;

		HelperThreadLaunchSeams::SetState(m_Run, RUN_WORK);
		//start new I/O
		CCriticalSection *pcsUploadListRead = NULL;
		const CUploadingPtrList &rUploadList = theApp.uploadqueue->GetUploadListTS(&pcsUploadListRead);
		try {
			CSingleLock lockUploadListRead(pcsUploadListRead, TRUE);
			ASSERT(lockUploadListRead.IsLocked());
			// WHY: StartCreateNextBlockPackage is expected to catch normal disk
			// dispatch errors, but it still performs heap/list work. RAII keeps
			// the upload-list read lock balanced if a low-memory exception slips
			// through while the disk helper is walking active upload entries.
			for (POSITION pos = rUploadList.GetHeadPosition(); pos != NULL;)
				StartCreateNextBlockPackage(rUploadList.GetNext(pos));
			HelperThreadLaunchSeams::ClearFlag(m_bNewData);
		} catch (CException *pException) {
			if (pException != NULL)
				pException->Delete();
			theApp.QueueDebugLogLineEx(LOG_ERROR, _T("Upload disk I/O helper aborted dispatch pass by MFC exception"));
		} catch (const std::bad_alloc&) {
			theApp.QueueDebugLogLineEx(LOG_ERROR, _T("Upload disk I/O helper ran out of memory during dispatch pass"));
		}

		//completed I/O
		bool bStopCompletion = false;
		do {
			if (HelperThreadLaunchSeams::IsIocpStopCompletion(bCompletionReceived, completionKey, pCurIO)) {
				bStopCompletion = true;
				break;
			}
			if (completionKey != WAKEUP) //ignore wakeups
				ReadCompletionRoutine(dwRead, pCurIO, dwCompletionError);

			completionKey = 0;
			pCurIO = NULL;
			dwRead = 0;
			const BOOL bNextCompletionReceived = ::GetQueuedCompletionStatus(m_hPort, &dwRead, &completionKey, (LPOVERLAPPED*)&pCurIO, 0);
			dwCompletionError = bNextCompletionReceived != FALSE ? ERROR_SUCCESS : ::GetLastError();
			if (!HelperThreadLaunchSeams::ShouldProcessIocpWorkerCompletion(bNextCompletionReceived, completionKey, pCurIO)) {
				if (HelperThreadLaunchSeams::IsIocpStopCompletion(bNextCompletionReceived, completionKey, pCurIO))
					bStopCompletion = true;
				break;
			}
		} while (true);

		if (bStopCompletion) //thread termination
			break;
		HelperThreadLaunchSeams::SetState(m_Run, RUN_IDLE);
		// if we have put a new data on any socket, tell the throttler
		if (m_bSignalThrottler && theApp.uploadBandwidthThrottler != NULL) {
			theApp.uploadBandwidthThrottler->NewUploadDataAvailable();
			m_bSignalThrottler = false;
		}
		if (HelperThreadLaunchSeams::ShouldPostIocpWakeAfterNewData(HelperThreadLaunchSeams::ExchangeState(m_bNewData, 0), m_listPendingIO.IsEmpty()))
			PostQueuedCompletionStatus(m_hPort, 0, WAKEUP, NULL);
	}
	HelperThreadLaunchSeams::SetState(m_Run, RUN_STOP);

	DrainPendingReads();

	HelperThreadLaunchSeams::CloseIocpPort(m_hPort);

	m_eventThreadEnded.SetEvent();
	return 0;
}

bool CUploadDiskIOThread::AssociateFile(CKnownFile *pFile)
{
	if (!HelperThreadLaunchSeams::CanPostIocpWork(m_bThreadStarted, HelperThreadLaunchSeams::IsFlagSet(m_bStopRequested), m_hPort != NULL, HelperThreadLaunchSeams::GetState(m_Run) != RUN_STOP))
		return false;
	ASSERT(m_hPort && HelperThreadLaunchSeams::GetState(m_Run) != RUN_STOP);
	if (pFile && pFile->m_hRead == INVALID_HANDLE_VALUE && !pFile->bNoNewReads) {
		CString fullname = (pFile->IsPartFile())
			? RemoveFileExtension(static_cast<const CPartFile*>(pFile)->GetFullName())
			: pFile->GetFilePath();
		pFile->m_hRead = LongPathSeams::CreateFile(fullname, GENERIC_READ, FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		if (pFile->m_hRead == INVALID_HANDLE_VALUE) {
			theApp.QueueDebugLogLineEx(LOG_ERROR, _T("Failed to open \"%s\" for overlapped read: %s"), (LPCTSTR)fullname, (LPCTSTR)GetErrorMessage(::GetLastError(), 1));
			return false;
		}
		if (m_hPort != ::CreateIoCompletionPort(pFile->m_hRead, m_hPort, (ULONG_PTR)pFile, 0)) {
			theApp.QueueDebugLogLineEx(LOG_ERROR, _T("Failed to associate \"%s\" with reading IOCP: %s"), (LPCTSTR)fullname, (LPCTSTR)GetErrorMessage(::GetLastError(), 1));
			DissociateFile(pFile);
			return false;
		}
		pFile->bCompress = ShouldCompressBasedOnFilename(fullname);
	}
	return true;
}

void CUploadDiskIOThread::DissociateFile(CKnownFile *pFile)
{
	ASSERT(pFile);
	if (pFile->m_hRead != INVALID_HANDLE_VALUE) {
		VERIFY(::CloseHandle(pFile->m_hRead));
		pFile->m_hRead = INVALID_HANDLE_VALUE;
	}
}

void CUploadDiskIOThread::AddPendingIo(OverlappedRead_Struct *pOvRead)
{
	// AddTail may throw before the counter moves, so increment only after it
	// links the node; this keeps m_nPendingIoCount equal to list membership.
	m_listPendingIO.AddTail(pOvRead);
	::InterlockedIncrement(&m_nPendingIoCount);
}

bool CUploadDiskIOThread::RemovePendingIoIfPresent(OverlappedRead_Struct *pOvRead)
{
	const POSITION posPending = m_listPendingIO.Find(pOvRead);
	if (posPending == NULL)
		return false;
	m_listPendingIO.RemoveAt(posPending);
	::InterlockedDecrement(&m_nPendingIoCount);
	return true;
}

void CUploadDiskIOThread::StartCreateNextBlockPackage(UploadingToClient_Struct *pUploadClientStruct)
{
	// when calling this function we already have a lock on the uploadlist
	// (so pUploadClientStruct is stable), but retired entries must still be treated as inert.

	// now also get a lock on the Block lists
	CSingleLock lockBlockLists(&pUploadClientStruct->m_csBlockListsLock, TRUE);
	if (pUploadClientStruct->m_bRetired || pUploadClientStruct->m_bIOError || pUploadClientStruct->m_pClient == NULL || pUploadClientStruct->m_BlockRequests_queue.IsEmpty())
		return;

	CUpDownClient *pClient = pUploadClientStruct->m_pClient;
	CClientReqSocket *pSock = pClient->socket;
	if (pSock == NULL || !pSock->IsConnected())
		return;
	// See if we can do an early return.
	// There may be no new blocks to load from disk and add to buffer, or buffer may be large enough already.

	uint64 nCurQueueSessionPayloadUp = pClient->GetQueueSessionPayloadUp();
	// GetQueueSessionPayloadUp is probably outdated so also add the value reported by the sockets as sent
	nCurQueueSessionPayloadUp += pSock->GetSentPayloadSinceLastCall(false);
	uint64 addedPayloadQueueSession = pClient->GetQueueSessionUploadAdded();
	// Scale read-ahead with the broadband target so a fast slot stays fed without over-buffering weak slots.
	const uint32 nBufferBlocks = theApp.uploadqueue->GetUploadBufferBlockCount(pClient->GetUploadDatarate());
	const uint32 nBufferLimit = nBufferBlocks * EMBLOCKSIZE + 1;

	if (addedPayloadQueueSession > nCurQueueSessionPayloadUp && addedPayloadQueueSession - nCurQueueSessionPayloadUp >= nBufferLimit)
		return; // the buffered data is large enough already

	// WHY: packet construction and socket queueing below allocate after the
	// kernel has already completed the read and while this routine still owns
	// the CKnownFile read reference, upload pending-block count, read buffer,
	// and OVERLAPPED object. Keep a hard exception boundary so those lifetime
	// guards are released even if packet/list allocation fails.
	try {
		// Get more data if currently buffered was less than nBufferLimit Bytes
		while (!pUploadClientStruct->m_BlockRequests_queue.IsEmpty()
			&& (addedPayloadQueueSession <= nCurQueueSessionPayloadUp || addedPayloadQueueSession - nCurQueueSessionPayloadUp < nBufferLimit))
		{
			const LONG nClientPendingReadLimit = UploadDiskIOThreadSeams::GetBroadbandPendingReadBlocksPerClient(
				theApp.uploadqueue != NULL ? theApp.uploadqueue->GetTargetClientDataRateBroadband() : 0u);
			const INT_PTR nThreadPendingReadLimit = UploadDiskIOThreadSeams::GetBroadbandPendingReadBlocksPerThread(
				nClientPendingReadLimit,
				theApp.uploadqueue != NULL ? theApp.uploadqueue->GetBroadbandSlotCap() : 0);
			if (!UploadDiskIOThreadSeams::CanIssuePendingUploadRead(
					pUploadClientStruct->m_nPendingIOBlocks.load(),
					m_listPendingIO.GetCount(),
					nClientPendingReadLimit,
					nThreadPendingReadLimit))
			{
				return;
			}

			Requested_Block_Struct *currentblock = pUploadClientStruct->m_BlockRequests_queue.GetHead();
			if (!md4equ(currentblock->FileID, pClient->GetUploadFileID())) {
				// the UploadFileID differs. That's normally not a problem, we just switch it, but
				// we don't want to do so in this thread for synchronization issues. So return and wait
				// until the main thread which checks the block request periodically does so.
				// This should happen very rarely anyway so no problem performance wise
				theApp.QueueDebugLogLine(false, _T("CUploadDiskIOThread::StartCreateNextBlockPackage: Switched fileid, waiting for the main thread"));
				return;
			}

			CKnownFile *pFile = theApp.sharedfiles->GetFileByID(currentblock->FileID);
			if (pFile == NULL)
				throwCStr(_T("CUploadDiskIOThread::StartCreateNextBlockPackage: shared file not found"));
			if (pFile->bNoNewReads) //should be moving to incoming
				return;

			// we already have done all important sanity checks for the block request in the main thread when adding it; just redo some quick important ones
			if (currentblock->StartOffset >= currentblock->EndOffset || currentblock->EndOffset > pFile->GetFileSize())
				throwCStr(_T("Invalid Block Offsets"));
			uint64 uTogo = currentblock->EndOffset - currentblock->StartOffset;
			if (uTogo > EMBLOCKSIZE * 3)
				throw GetResString(IDS_ERR_LARGEREQBLOCK);

			if (!AssociateFile(pFile))
				throwCStr(_T("StartCreateNextBlockPackage: cannot open CKnownFile"));

			// initiate read
			OverlappedRead_Struct *pOverlappedRead = NULL;
			bool bReadReferenceAdded = false;
			bool bPendingBlockCounted = false;
			bool bPendingListLinked = false;
			auto cleanupFailedReadDispatch = [&]() {
				// WHY: The upload read submission path has three separate raw
				// lifetime contracts: the pending IO list owns completion
				// discovery, the per-client counter gates teardown, and the
				// shared file read reference prevents completion from racing a
				// close. Any allocation failure before ReadFile succeeds must
				// unwind those contracts in reverse order, or the upload helper
				// can leave stale OVERLAPPED state or pin a file indefinitely.
				if (bPendingListLinked) {
					RemovePendingIoIfPresent(pOverlappedRead);
					bPendingListLinked = false;
				}
				if (bPendingBlockCounted) {
					pUploadClientStruct->m_nPendingIOBlocks.fetch_sub(1);
					bPendingBlockCounted = false;
				}
				if (bReadReferenceAdded) {
					pFile->ReleaseUploadReadReference();
					bReadReferenceAdded = false;
				}
				if (pOverlappedRead != NULL)
					delete[] pOverlappedRead->pBuffer;
				delete pOverlappedRead;
			};

			try {
				pOverlappedRead = new OverlappedRead_Struct;
				pOverlappedRead->oOverlap.Internal = 0;
				pOverlappedRead->oOverlap.InternalHigh = 0;
				//pOverlappedRead->oOverlap.Offset = LODWORD(currentblock->StartOffset);
				//pOverlappedRead->oOverlap.OffsetHigh = HIDWORD(currentblock->StartOffset);
				*(uint64*)&pOverlappedRead->oOverlap.Offset = currentblock->StartOffset;
				pOverlappedRead->oOverlap.hEvent = 0;
				pOverlappedRead->pFile = pFile;
				pOverlappedRead->pUploadClientStruct = pUploadClientStruct;
				pOverlappedRead->uStartOffset = currentblock->StartOffset;
				pOverlappedRead->uEndOffset = currentblock->EndOffset;
				pOverlappedRead->pBuffer = NULL;
				pOverlappedRead->pBuffer = new byte[(size_t)uTogo];

				// WHY: ReadFile captures pOverlappedRead, which contains raw
				// CKnownFile and upload-entry pointers. The lifetime reference,
				// per-client pending count, and IOCP tracking list must all be
				// established before kernel submission; otherwise a low-memory
				// failure in CList after ReadFile would leave completion holding
				// an untracked OVERLAPPED and mismatched deletion guards.
				pFile->AddUploadReadReference();
				bReadReferenceAdded = true;
				pUploadClientStruct->m_nPendingIOBlocks.fetch_add(1);
				bPendingBlockCounted = true;
				AddPendingIo(pOverlappedRead);
				bPendingListLinked = true;

				if (!::ReadFile(pFile->m_hRead, pOverlappedRead->pBuffer, (DWORD)uTogo, NULL, (LPOVERLAPPED)pOverlappedRead)) {
					DWORD dwError = ::GetLastError();
					if (dwError != ERROR_IO_PENDING) {
						RemovePendingIoIfPresent(pOverlappedRead);
						bPendingListLinked = false;
						pUploadClientStruct->m_nPendingIOBlocks.fetch_sub(1);
						bPendingBlockCounted = false;
						pFile->ReleaseUploadReadReference();
						bReadReferenceAdded = false;
						delete[] pOverlappedRead->pBuffer;
						delete pOverlappedRead;

						if (dwError == ERROR_INVALID_USER_BUFFER || dwError == ERROR_NOT_ENOUGH_MEMORY || dwError == ERROR_NOT_ENOUGH_QUOTA) {
							theApp.QueueDebugLogLineEx(LOG_WARNING, _T("ReadFile failed, possibly too many pending requests, trying again later"));
							return; // make this a recoverable error, as it might just be that we have too many requests in which case we just need to wait
						}
						throw _T("ReadFile Error: ") + GetErrorMessage(dwError, 1);
					}
				}
			} catch (CMemoryException *ex) {
				cleanupFailedReadDispatch();
				if (ex != NULL)
					ex->Delete();
				throwCStr(_T("Upload disk read dispatch ran out of memory"));
			} catch (const std::bad_alloc&) {
				cleanupFailedReadDispatch();
				throwCStr(_T("Upload disk read dispatch ran out of memory"));
			}
			DEBUG_ONLY(dbgDataReadPending += uTogo);

			// WHY: AddHead can allocate. Link the request into the done list
			// before removing it from the pending queue so a low-memory throw
			// cannot lose the block while an already submitted read completes.
			Requested_Block_Struct *pDoneBlock = pUploadClientStruct->m_BlockRequests_queue.GetHead();
			const UploadBlockRequestSeams::SUploadBlockRequestKey doneKey =
				UploadBlockRequestSeams::BuildUploadBlockRequestKey(pDoneBlock->StartOffset, pDoneBlock->EndOffset, pDoneBlock->FileID);
			pUploadClientStruct->m_DoneBlocks_keys.insert(doneKey);
			try {
				pUploadClientStruct->m_DoneBlocks_list.AddHead(pDoneBlock);
			} catch (...) {
				pUploadClientStruct->m_DoneBlocks_keys.erase(doneKey);
				throw;
			}
			pUploadClientStruct->m_BlockRequests_keys.erase(doneKey);
			pUploadClientStruct->m_BlockRequests_queue.RemoveHead();
			addedPayloadQueueSession += uTogo;
			pClient->SetQueueSessionUploadAdded(addedPayloadQueueSession);
		}
		return; //no errors
	} catch (const CString &ex) {
		theApp.QueueDebugLogLineEx(LOG_ERROR, GetResString(IDS_ERR_CLIENTERRORED), pClient->GetUserName(), (LPCTSTR)ex);
	} catch (CMemoryException *ex) {
		if (ex != NULL)
			ex->Delete();
		theApp.QueueDebugLogLineEx(LOG_ERROR, GetResString(IDS_ERR_CLIENTERRORED), pClient->GetUserName(), _T("upload disk read dispatch ran out of memory"));
	} catch (CFileException *ex) {
		theApp.QueueDebugLogLineEx(LOG_ERROR, _T("Failed to create upload package for %s%s"), pClient->GetUserName(), (LPCTSTR)CExceptionStrDash(*ex));
		ex->Delete();
	}
	pUploadClientStruct->m_bIOError = true; // will let remove this client from the list in the main thread
}

void CUploadDiskIOThread::ReadCompletionRoutine(DWORD dwRead, const OverlappedRead_Struct *pOvRead, DWORD dwCompletionError)
{
	if (pOvRead == NULL) {
		ASSERT(0);
		return;
	}

	CKnownFile *pKnownFile = pOvRead->pFile;
	ASSERT(pKnownFile != NULL);
	UploadingToClient_Struct *pStruct = pOvRead->pUploadClientStruct;
	ASSERT(pStruct != NULL);
	CPacketList packetsList;

	if (!RemovePendingIoIfPresent(const_cast<OverlappedRead_Struct*>(pOvRead))
		&& HelperThreadLaunchSeams::GetState(m_Run) != RUN_STOP)
		theApp.QueueDebugLogLineEx(LOG_WARNING, _T("ReadCompletionRoutine: completed upload read was not present in the pending I/O list"));

	try {
	if (HelperThreadLaunchSeams::GetState(m_Run) != RUN_STOP) {
		bool bReadError = dwCompletionError != ERROR_SUCCESS || !dwRead;
		if (bReadError) {
			const DWORD dwEffectiveError = dwCompletionError != ERROR_SUCCESS ? dwCompletionError : ERROR_READ_FAULT;
			theApp.QueueDebugLogLineEx(LOG_ERROR, _T("Upload overlapped read failed: read %lu bytes, error %lu (%s)")
				, dwRead, dwEffectiveError, (LPCTSTR)GetErrorMessage(dwEffectiveError, 1));
		}
		else if (dwRead != pOvRead->uEndOffset - pOvRead->uStartOffset) {
			theApp.QueueDebugLogLineEx(LOG_ERROR, _T("ReadCompletionRoutine: Didn't read requested data count - wanted: %lu, read: %lu")
				, (DWORD)(pOvRead->uEndOffset - pOvRead->uStartOffset), dwRead);
			bReadError = true;
		}
		if (pKnownFile != NULL && pKnownFile->m_hRead != INVALID_HANDLE_VALUE && pStruct != NULL) { //discard data from closed files
			DEBUG_ONLY(dbgDataReadPending -= pOvRead->uEndOffset - pOvRead->uStartOffset);
			CUploadDiskSocketDeliveryRef socketDeliveryRef;
			UploadDiskReadCompletionAction completionAction = uploadDiskReadCompletionDiscard;
			bool bLoggedCompletionDiscard = false;
			{
				CCriticalSection *pcsUploadListRead = NULL;
				const CUploadingPtrList &rUploadList = theApp.uploadqueue->GetUploadListTS(&pcsUploadListRead);
				CSingleLock lockUploadListRead(pcsUploadListRead, TRUE);
				ASSERT(lockUploadListRead.IsLocked());
				const bool bFound = (rUploadList.Find(pStruct) != NULL);
				const UploadQueueEntryAccessState entryState = ClassifyUploadQueueEntryAccess(bFound, pStruct->m_bRetired, pStruct->m_pClient != NULL);
				// WHY: dispatch-side bookkeeping can fail after ReadFile has
				// accepted the OVERLAPPED. Once the upload entry is marked I/O
				// errored, late completions must drain their references without
				// delivering packets for a request the queue is already retiring.
				completionAction = pStruct->m_bIOError
					? uploadDiskReadCompletionDiscard
					: ClassifyUploadDiskReadCompletion(entryState, bReadError);

				if (completionAction == uploadDiskReadCompletionSendPackets) {
					CUpDownClient *pClient = pStruct->m_pClient;
					CClientReqSocket *pSocket = pClient != NULL ? pClient->socket : NULL;
					if (pClient != NULL && theApp.listensocket->TryAddUploadDiskPacketDeliveryRef(pSocket)) {
						socketDeliveryRef.Attach(pSocket);
						if (pKnownFile->bCompress)
							CreatePackedPackets(*pOvRead, packetsList);
						else
							CreateStandardPackets(*pOvRead, packetsList);

						m_bSignalThrottler = true;
					} else if (pClient != NULL) {
						theApp.QueueDebugLogLineEx(LOG_ERROR, _T("ReadCompletionRoutine: Client has no connected socket, %s"), (LPCTSTR)pClient->DbgGetClientInfo(true));
						completionAction = uploadDiskReadCompletionDiscard;
						bLoggedCompletionDiscard = true;
					} else {
						theApp.QueueDebugLogLineEx(LOG_WARNING, _T("ReadCompletionRoutine: Upload entry retired before packet delivery; discarding block"));
						completionAction = uploadDiskReadCompletionDiscard;
						bLoggedCompletionDiscard = true;
					}
				}

				if (completionAction == uploadDiskReadCompletionMarkIoError)
					pStruct->m_bIOError = true;
				else if (completionAction == uploadDiskReadCompletionDiscard && !bLoggedCompletionDiscard) {
					if (pStruct->m_bIOError)
						theApp.QueueDebugLogLineEx(LOG_WARNING, _T("ReadCompletionRoutine: upload entry already has a disk I/O error; discarding block"));
					else
						theApp.QueueDebugLogLineEx(LOG_WARNING, _T("ReadCompletionRoutine: Client not found in uploadlist when reading finished; discarding block"));
				}
			}

			CClientReqSocket *pSocket = socketDeliveryRef.Get();
			while (!packetsList.IsEmpty() && pSocket != NULL) {
				Packet *packet = packetsList.RemoveHead();
				if (!pSocket->CanAcceptUploadDiskPacketDelivery()) {
					delete packet;
					continue;
				}
				// WHY: packet delivery must not run under the upload-list lock;
				// queue-limit errors can disconnect the peer and re-enter upload
				// queue teardown. socketDeliveryRef is the lifetime bridge that
				// keeps the Safe_Delete timed-reclaim path from freeing pSocket
				// after the helper releases the upload-list lock.
				pSocket->SendPacket(packet, false, packet->uStatsPayLoad);
			}
		} else if (pStruct == NULL)
			theApp.QueueDebugLogLineEx(LOG_WARNING, _T("ReadCompletionRoutine: completed upload read has no upload entry; discarding block"));
	} else if (pKnownFile)
		DissociateFile(pKnownFile);
	} catch (CException *pException) {
		if (pException != NULL)
			pException->Delete();
		DeletePacketList(packetsList);
		if (pStruct != NULL)
			pStruct->m_bIOError = true;
		theApp.QueueDebugLogLineEx(LOG_ERROR, _T("ReadCompletionRoutine: upload read completion aborted by MFC exception; discarding block and marking upload entry failed"));
	} catch (const std::bad_alloc&) {
		DeletePacketList(packetsList);
		if (pStruct != NULL)
			pStruct->m_bIOError = true;
		theApp.QueueDebugLogLineEx(LOG_ERROR, _T("ReadCompletionRoutine: upload read completion ran out of memory; discarding block and marking upload entry failed"));
	}
	if (pKnownFile != NULL) {
		// Keep nInUse raised until every completion-path access to pKnownFile is done.
		// Delete paths use this counter as their release-build lifetime barrier.
		ASSERT(pKnownFile->GetUploadReadReferenceCount() > 0);
		pKnownFile->ReleaseUploadReadReference();
	}
	if (pStruct != NULL)
		pStruct->m_nPendingIOBlocks.fetch_sub(1);

	// cleanup
	delete[] pOvRead->pBuffer;
	delete pOvRead;
}

void CUploadDiskIOThread::CancelPendingReads()
{
	for (POSITION pos = m_listPendingIO.GetHeadPosition(); pos != NULL;) {
		const OverlappedRead_Struct *pOvRead = m_listPendingIO.GetNext(pos);
		CKnownFile *pKnownFile = pOvRead != NULL ? pOvRead->pFile : NULL;
		if (pKnownFile == NULL || pKnownFile->m_hRead == INVALID_HANDLE_VALUE)
			continue;

		if (!::CancelIoEx(pKnownFile->m_hRead, const_cast<LPOVERLAPPED>(&pOvRead->oOverlap))) {
			const DWORD dwError = ::GetLastError();
			if (dwError != ERROR_NOT_FOUND)
				theApp.QueueDebugLogLineEx(LOG_WARNING, _T("Failed to cancel upload overlapped read during shutdown: %s"), (LPCTSTR)GetErrorMessage(dwError, 1));
		}
	}
}

void CUploadDiskIOThread::DrainPendingReads()
{
	CancelPendingReads();

	while (!m_listPendingIO.IsEmpty()) {
		DWORD dwRead = 0;
		ULONG_PTR completionKey = 0;
		OverlappedRead_Struct *pCurIO = NULL;
		const BOOL bCompletionReceived = ::GetQueuedCompletionStatus(m_hPort, &dwRead, &completionKey, (LPOVERLAPPED*)&pCurIO, INFINITE);
		const DWORD dwCompletionError = bCompletionReceived != FALSE ? ERROR_SUCCESS : ::GetLastError();
		if (pCurIO != NULL)
			ReadCompletionRoutine(dwRead, pCurIO, dwCompletionError);
	}
}

bool CUploadDiskIOThread::ShouldCompressBasedOnFilename(const CString &strFileName)
{
	LPCTSTR const pDot = ::PathFindExtension(strFileName);
	if (!pDot[0] || !pDot[1])
		return true; //no extension
	CString strExt(&pDot[1]);
	strExt.MakeLower();

	static LPCTSTR const exts[] = {
		_T("7z"), _T("aac"), _T("ace"), _T("apk"), _T("avi"), _T("bz2"), _T("cab"), _T("cbr"), _T("cbz"),
		_T("docx"), _T("flac"), _T("flv"), _T("gif"), _T("gz"), _T("jar"), _T("jpeg"), _T("jpg"), _T("lz"),
		_T("lzma"), _T("m2ts"), _T("m4a"), _T("m4v"), _T("mkv"), _T("mov"), _T("mp3"), _T("mp4"), _T("mpeg"),
		_T("mpg"), _T("mts"), _T("odp"), _T("ods"), _T("odt"), _T("ogg"), _T("ogm"), _T("opus"), _T("pdf"),
		_T("png"), _T("pptx"), _T("rar"), _T("ts"), _T("vob"), _T("webm"), _T("webp"), _T("wma"), _T("wmv"),
		_T("xlsx"), _T("xz"), _T("zip"), _T("zst"), NULL
	};
	for (LPCTSTR ext = *exts; *ext; ++ext)
		if (strExt == ext)
			return false;
	return true;
}

void CUploadDiskIOThread::CreateStandardPackets(const OverlappedRead_Struct &OverlappedRead, CPacketList &rOutPacketList)
{
	const uchar *pucMD4FileHash = OverlappedRead.pFile->GetFileHash();
	bool bIsPartFile = OverlappedRead.pFile->IsPartFile();
	CString sDbgClientInfo;
	if (thePrefs.GetDebugClientTCPLevel() > 0)
		sDbgClientInfo = OverlappedRead.pUploadClientStruct->m_pClient->DbgGetClientInfo(true);

	uint32 togo = (uint32)(OverlappedRead.uEndOffset - OverlappedRead.uStartOffset);
	CMemFile memfile((BYTE*)OverlappedRead.pBuffer, togo);
	while (togo) {
		uint32 nPacketSize = (togo < 13000) ? togo : 10240u;
		togo -= nPacketSize;

		uint64 endpos = OverlappedRead.uEndOffset - togo;
		uint64 startpos = endpos - nPacketSize;

		LPCTSTR sOp;
		std::unique_ptr<Packet> packet;
		if (endpos > _UI32_MAX) {
			packet.reset(new Packet(OP_SENDINGPART_I64, nPacketSize + 32, OP_EMULEPROT, bIsPartFile));
			md4cpy(&packet->pBuffer[0], pucMD4FileHash);
			PokeUInt64(&packet->pBuffer[16], startpos);
			PokeUInt64(&packet->pBuffer[24], endpos);
			memfile.Read(&packet->pBuffer[32], nPacketSize);
			theStats.AddUpDataOverheadFileRequest(32);
			sOp = _T("OP_SendingPart_I64");
		} else {
			packet.reset(new Packet(OP_SENDINGPART, nPacketSize + 24, OP_EDONKEYPROT, bIsPartFile));
			md4cpy(&packet->pBuffer[0], pucMD4FileHash);
			PokeUInt32(&packet->pBuffer[16], (uint32)startpos);
			PokeUInt32(&packet->pBuffer[20], (uint32)endpos);
			memfile.Read(&packet->pBuffer[24], nPacketSize);
			theStats.AddUpDataOverheadFileRequest(24);
			sOp = _T("OP_SendingPart");
		}
		if (thePrefs.GetDebugClientTCPLevel() > 0) {
			Debug(_T(">>> %-20s to   %s; %s\n"), sOp, (LPCTSTR)sDbgClientInfo, (LPCTSTR)md4str(pucMD4FileHash));
			Debug(_T("  Start=%I64u  End=%I64u  Size=%u\n"), startpos, endpos, nPacketSize);
		}
		packet->uStatsPayLoad = nPacketSize;
		// WHY: CPacketList is an MFC list and AddTail can allocate after the
		// packet buffer is filled. Keep packet locally owned until the list node
		// exists so a low-memory exception cannot leak the upload block packet.
		rOutPacketList.AddTail(packet.get());
		packet.release();
	}
}

void CUploadDiskIOThread::CreatePackedPackets(const OverlappedRead_Struct &OverlappedRead, CPacketList &rOutPacketList)
{
	const uint64 uStartOffset = OverlappedRead.uStartOffset;
	const uint64 uEndOffset = OverlappedRead.uEndOffset;
	const uchar *pucMD4FileHash = OverlappedRead.pFile->GetFileHash();
	bool bIsPartFile = OverlappedRead.pFile->IsPartFile();

	uint32 togo = (uint32)(uEndOffset - uStartOffset);
	uLongf newsize = togo + 300;
	std::unique_ptr<BYTE[]> output(new BYTE[newsize]);

	// Use the lowest compression level 1 instead of the highest 9 because typically for 10240 blocks:
	// - compressed size difference is usually small enough (~4% for .exe, .avi, .pdf and 12% for .c text)
	// - time was 1.5-2.5 better
	// Of course, throughput of the deflate() routine depends on processor and data bytes,
	// but should not be the bottleneck because 50-70 MB/s rates were seen when using one CPU core.
	if (compress2(output.get(), &newsize, OverlappedRead.pBuffer, togo, 1) != Z_OK || togo <= newsize) {
		CreateStandardPackets(OverlappedRead, rOutPacketList);
		return;
	}
	CString sDbgClientInfo;
	if (thePrefs.GetDebugClientTCPLevel() > 0)
		sDbgClientInfo = OverlappedRead.pUploadClientStruct->m_pClient->DbgGetClientInfo(true);

	CMemFile memfile(output.get(), newsize);
	uint32 oldSize = togo;
	togo = newsize;

	uint32 totalPayloadSize = 0;

	while (togo) {
		uint32 nPacketSize = (togo < 13000) ? togo : 10240u;
		togo -= nPacketSize;
		std::unique_ptr<Packet> packet;
		LPCTSTR sOp;
		if (uEndOffset > UINT32_MAX) {
			packet.reset(new Packet(OP_COMPRESSEDPART_I64, nPacketSize + 28, OP_EMULEPROT, bIsPartFile));
			md4cpy(&packet->pBuffer[0], pucMD4FileHash);
			PokeUInt64(&packet->pBuffer[16], uStartOffset);
			PokeUInt32(&packet->pBuffer[24], newsize);
			memfile.Read(&packet->pBuffer[28], nPacketSize);
			sOp = _T("OP_CompressedPart_I64");
		} else {
			packet.reset(new Packet(OP_COMPRESSEDPART, nPacketSize + 24, OP_EMULEPROT, bIsPartFile));
			md4cpy(&packet->pBuffer[0], pucMD4FileHash);
			PokeUInt32(&packet->pBuffer[16], (uint32)uStartOffset);
			PokeUInt32(&packet->pBuffer[20], newsize);
			memfile.Read(&packet->pBuffer[24], nPacketSize);
			sOp = _T("OP_CompressedPart");
		}

		if (thePrefs.GetDebugClientTCPLevel() > 0) {
			Debug(_T(">>> %-20s to   %s; %s\n"), sOp, (LPCTSTR)sDbgClientInfo, (LPCTSTR)md4str(pucMD4FileHash));
			Debug(_T("  Start=%I64u  BlockSize=%u  Size=%u\n"), uStartOffset, newsize, nPacketSize);
		}
		// approximate payload size
		uint32 payloadSize = togo ? nPacketSize * oldSize / newsize : oldSize - totalPayloadSize;

		totalPayloadSize += payloadSize;

		theStats.AddUpDataOverheadFileRequest(24);
		packet->uStatsPayLoad = payloadSize;
		// WHY: the compressed packet owns heap storage while MFC links a list
		// node. Keep it in a local owner until AddTail succeeds; output remains
		// separately owned by output so both buffers unwind cleanly on failure.
		rOutPacketList.AddTail(packet.get());
		packet.release();
	}
	memfile.Close();
}

void CUploadDiskIOThread::WakeUpCall()
{
	if (!HelperThreadLaunchSeams::CanPostIocpWork(m_bThreadStarted, HelperThreadLaunchSeams::IsFlagSet(m_bStopRequested), m_hPort != NULL, HelperThreadLaunchSeams::GetState(m_Run) != RUN_STOP))
		return;
	//pending I/O makes posting unnecessary
	// WHY: WakeUpCall runs on foreground threads; read the worker-maintained
	// atomic count instead of the unlocked m_listPendingIO CList so this cannot
	// race the worker's add/remove of list nodes.
	if (HelperThreadLaunchSeams::GetState(m_Run) == RUN_IDLE && ::InterlockedCompareExchange(&m_nPendingIoCount, 0, 0) == 0)
		PostQueuedCompletionStatus(m_hPort, 0, WAKEUP, NULL);
	else
		HelperThreadLaunchSeams::SetFlag(m_bNewData);
}
