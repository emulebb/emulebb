//this file is part of eMule
//Copyright (C)2020-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
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
#include "updownclient.h"
#include "PartFileWriteThread.h"
#include "emule.h"
#include "partfile.h"
#include "log.h"
#include "HelperThreadLaunchSeams.h"

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
void MarkWriteDispatchFailed(PartFileBufferedData *pBuffer, DWORD dwError)
{
	if (pBuffer == NULL)
		return;
	if (pBuffer->data != NULL) {
		// The write thread already removed this item from its queue; mark it
		// failed so the part file does not wait forever on PB_PENDING.
		pBuffer->dwError = dwError;
		SetPartFileBufferedDataFlushState(*pBuffer, PB_ERROR);
	} else {
		// Allocation sentinels are not tracked in the part-file buffer list.
		delete pBuffer;
	}
}

void ReleaseQueuedWriteListForShutdown(CList<ToWrite> &rWriteList)
{
	while (!rWriteList.IsEmpty()) {
		const ToWrite item = rWriteList.RemoveHead();
		// WHY: queued-but-undispatched items were already marked PB_PENDING by
		// FlushBuffer, but they never incremented m_iWrites or reached IOCP.
		// Marking them aborted wakes part-file cleanup without fabricating a
		// completion for a write that the kernel never owned.
		MarkWriteDispatchFailed(item.pBuffer, ERROR_OPERATION_ABORTED);
	}
}
}

IMPLEMENT_DYNCREATE(CPartFileWriteThread, CWinThread)

CPartFileWriteThread::CPartFileWriteThread()
	: m_eventThreadEnded(FALSE, TRUE)
	, m_hPort()
	, m_bThreadStarted()
	, m_bStopRequested()
	, m_Run(RUN_STOP)
	, m_bNewData()
{
#if EMULEBB_HAS_STARTUP_PROFILING
	const ULONGLONG ullPhaseStart = theApp.GetStartupProfileTimestampUs();
#endif
	CWinThread *pThread = AfxBeginThread(RunProc, (LPVOID)this, THREAD_PRIORITY_BELOW_NORMAL);
	m_bThreadStarted = HelperThreadLaunchSeams::DidStartThread(pThread);
	if (!m_bThreadStarted) {
		theApp.QueueDebugLogLineEx(LOG_ERROR, _T("Failed to start part-file write helper thread"));
		m_eventThreadEnded.SetEvent();
	}
#if EMULEBB_HAS_STARTUP_PROFILING
	theApp.AppendStartupProfileLine(_T("broadband.partfile_write.launch_thread"), theApp.GetStartupProfileElapsedUs(ullPhaseStart), ullPhaseStart);
#endif
}

CPartFileWriteThread::~CPartFileWriteThread()
{
	ASSERT(!m_hPort && HelperThreadLaunchSeams::GetState(m_Run) == RUN_STOP);
}

UINT AFX_CDECL CPartFileWriteThread::RunProc(LPVOID pParam)
{
#if EMULEBB_HAS_STARTUP_PROFILING
	theApp.AppendStartupProfileLine(_T("broadband.partfile_write.thread_enter"), 0);
#endif
	DbgSetThreadName("PartWriteThread");
	InitThreadLocale();
	return pParam ? static_cast<CPartFileWriteThread*>(pParam)->RunInternal() : 1;
}

void CPartFileWriteThread::EndThread()
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
			theApp.QueueDebugLogLineEx(LOG_ERROR, _T("Timed out waiting for part-file write helper thread; waiting for cooperative exit"));
		},
		[](DWORD dwLastError) {
			theApp.QueueDebugLogLineEx(LOG_ERROR, _T("Failed waiting for part-file write helper thread: %s"), (LPCTSTR)GetErrorMessage(dwLastError, 1));
		});
}

UINT CPartFileWriteThread::RunInternal()
{
#if EMULEBB_HAS_STARTUP_PROFILING
	const ULONGLONG ullThreadStartUs = theApp.GetStartupProfileTimestampUs();
#endif
	DWORD dwError = ERROR_SUCCESS;
	if (!HelperThreadLaunchSeams::TryCreateIocpPort(m_hPort, dwError)) {
		theApp.QueueDebugLogLineEx(LOG_ERROR, _T("Failed to create part-file write completion port: %s"), (LPCTSTR)GetErrorMessage(dwError, 1));
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

	DWORD dwWrite = 0;
	ULONG_PTR completionKey = 0;
	OverlappedWrite_Struct *pCurIO = NULL;
	HelperThreadLaunchSeams::SetState(m_Run, RUN_IDLE);
#if EMULEBB_HAS_STARTUP_PROFILING
	theApp.AppendStartupProfileLine(_T("broadband.partfile_write.thread_ready"), theApp.GetStartupProfileElapsedUs(ullThreadStartUs), ullThreadStartUs);
#endif
	while (HelperThreadLaunchSeams::ShouldWaitForIocpWorkerCompletion(
		HelperThreadLaunchSeams::IsFlagSet(m_bStopRequested),
		HelperThreadLaunchSeams::GetState(m_Run),
		RUN_STOP))
	{
		const BOOL bCompletionReceived = ::GetQueuedCompletionStatus(m_hPort, &dwWrite, &completionKey, (LPOVERLAPPED*)&pCurIO, INFINITE);
		DWORD dwCompletionError = bCompletionReceived != FALSE ? ERROR_SUCCESS : ::GetLastError();
		if (HelperThreadLaunchSeams::IsIocpStopCompletion(bCompletionReceived, completionKey, pCurIO)
			|| !HelperThreadLaunchSeams::ShouldProcessIocpWorkerCompletion(bCompletionReceived, completionKey, pCurIO))
			break;

		HelperThreadLaunchSeams::SetState(m_Run, RUN_WORK);
		//move buffer lists into the local storage
		try {
			CSingleLock lockFlushList(&m_lockFlushList, TRUE);
			bool bMovedQueuedWrites = false;
			while (!m_FlushList.IsEmpty()) {
				// WHY: CList::AddTail can allocate. Copy into the private list
				// before removing from the shared queue so a low-memory throw
				// leaves the write request visible for a later wake instead of
				// dropping a PB_PENDING buffer on the floor.
				const ToWrite item = m_FlushList.GetHead();
				m_listToWrite.AddTail(item);
				m_FlushList.RemoveHead();
				bMovedQueuedWrites = true;
			}
			if (bMovedQueuedWrites)
				HelperThreadLaunchSeams::ClearFlag(m_bNewData);
		} catch (CMemoryException *ex) {
			if (ex != NULL)
				ex->Delete();
			theApp.QueueDebugLogLineEx(LOG_WARNING, _T("Part-file write helper could not move queued writes because memory is low; retrying on the next wake"));
		}
		//start new I/O
		WriteBuffers();
		//completed I/O
		bool bStopCompletion = false;
		do {
			if (HelperThreadLaunchSeams::IsIocpStopCompletion(bCompletionReceived, completionKey, pCurIO)) {
				bStopCompletion = true;
				break;
			}
			if (completionKey != WAKEUP) //ignore wakeups
				WriteCompletionRoutine(dwWrite, pCurIO, dwCompletionError);

			completionKey = 0;
			pCurIO = NULL;
			dwWrite = 0;
			const BOOL bNextCompletionReceived = ::GetQueuedCompletionStatus(m_hPort, &dwWrite, &completionKey, (LPOVERLAPPED*)&pCurIO, 0);
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
		if (HelperThreadLaunchSeams::ShouldPostIocpWakeAfterNewData(HelperThreadLaunchSeams::ExchangeState(m_bNewData, 0), m_listPendingIO.IsEmpty()))
			PostQueuedCompletionStatus(m_hPort, 0, WAKEUP, NULL);
	}
	HelperThreadLaunchSeams::SetState(m_Run, RUN_STOP);

	DrainPendingWrites();

	HelperThreadLaunchSeams::CloseIocpPort(m_hPort);

	m_eventThreadEnded.SetEvent();
	return 0;
}

void CPartFileWriteThread::WriteBuffers()
{
	//process internal list
	while (!m_listToWrite.IsEmpty() && HelperThreadLaunchSeams::GetState(m_Run) != RUN_STOP) {
		const ToWrite item = m_listToWrite.RemoveHead();
		PartFileBufferedData *pBuffer = item.pBuffer;
		ASSERT(pBuffer->end >= pBuffer->start && (pBuffer->data || pBuffer->end == pBuffer->start)); //verifies allocation requests too

		CPartFile *pFile = item.pFile;
		if (AddFile(pFile)) {
			OverlappedWrite_Struct *pOvWrite = NULL;
			try {
				//initiate write
				pOvWrite = new OverlappedWrite_Struct;
				pOvWrite->oOverlap.Internal = 0;
				pOvWrite->oOverlap.InternalHigh = 0;
				//pOvWrite->oOverlap.Offset = LODWORD(currentblock->StartOffset);
				//pOvWrite->oOverlap.OffsetHigh = HIDWORD(currentblock->StartOffset);
				*(uint64*)&pOvWrite->oOverlap.Offset = pBuffer->start;
				pOvWrite->oOverlap.hEvent = 0;
				pOvWrite->pFile = pFile;
				pOvWrite->pBuffer = pBuffer;
				pOvWrite->pos = NULL;

				// WHY: once WriteFile sees this OVERLAPPED, the IOCP completion
				// can return it even if the call completes synchronously or later
				// fails. Register the object and raise the file's delete guard
				// before submission so completion, cancellation, and part-file
				// lifetime checks all observe the same ownership state.
				pOvWrite->pos = m_listPendingIO.AddTail(pOvWrite);
				pFile->IncrementAsyncWriteCount();

				static const BYTE zero = 0;
				if (!::WriteFile(pFile->m_hWrite, pBuffer->data ? pBuffer->data : &zero, (DWORD)(pBuffer->end - pBuffer->start + 1), NULL, (LPOVERLAPPED)pOvWrite)) {
					DWORD dwError = ::GetLastError();
					if (dwError != ERROR_IO_PENDING) {
						m_listPendingIO.RemoveAt(pOvWrite->pos);
						pOvWrite->pos = NULL;
						pFile->DecrementAsyncWriteCount();
						delete pOvWrite;
						MarkWriteDispatchFailed(item.pBuffer, dwError);
						theApp.QueueDebugLogLineEx(LOG_WARNING, _T("WriteBuffers error: %s"), (LPCTSTR)GetErrorMessage(dwError, 1));
						RemFile(pFile);
						return;
					}
				}
			} catch (CMemoryException *ex) {
				if (ex != NULL)
					ex->Delete();
				delete pOvWrite;
				MarkWriteDispatchFailed(item.pBuffer, ERROR_NOT_ENOUGH_MEMORY);
				theApp.QueueDebugLogLineEx(LOG_WARNING, _T("WriteBuffers error: not enough memory while registering overlapped write"));
				return;
			} catch (const std::bad_alloc&) {
				// WHY: MSVC throws std::bad_alloc for plain C++ new while MFC
				// containers throw CMemoryException. pBuffer was already removed
				// from the private write queue and marked PB_PENDING by FlushBuffer;
				// mark it failed here so no part-file waits forever on a write the
				// kernel never received.
				delete pOvWrite;
				MarkWriteDispatchFailed(item.pBuffer, ERROR_NOT_ENOUGH_MEMORY);
				theApp.QueueDebugLogLineEx(LOG_WARNING, _T("WriteBuffers error: not enough memory while allocating overlapped write"));
				return;
			}
		} else {
			MarkWriteDispatchFailed(item.pBuffer, ERROR_INVALID_HANDLE);
			theApp.QueueDebugLogLineEx(LOG_ERROR, _T("WriteBuffers error: CPartFile cannot be written"));
		}
	}
}

void CPartFileWriteThread::WriteCompletionRoutine(DWORD dwBytesWritten, const OverlappedWrite_Struct *pOvWrite, DWORD dwCompletionError)
{
	if (pOvWrite == NULL) {
		ASSERT(0);
		return;
	}
	CPartFile *pFile = pOvWrite->pFile;
	PartFileBufferedData *pBuffer = pOvWrite->pBuffer;
	const DWORD dwWrite = (DWORD)(pBuffer->end - pBuffer->start + 1);

	if (pOvWrite->pos != NULL)
		m_listPendingIO.RemoveAt(pOvWrite->pos);
	else
		ASSERT(HelperThreadLaunchSeams::GetState(m_Run) == RUN_STOP);

	if (dwCompletionError == ERROR_SUCCESS && dwBytesWritten && dwWrite == dwBytesWritten) {
		if (pFile) {
			const LONG nRemainingWrites = pFile->DecrementAsyncWriteCount();
			if (pBuffer->data) { //write data
				// WHY: completion must only observe buffers already handed to the
				// write thread. Assigning here hides state-machine bugs in Debug
				// and can make later cleanup believe an invalid buffer was pending.
				ASSERT(GetPartFileBufferedDataFlushState(*pBuffer) == PB_PENDING && nRemainingWrites >= 0);
				SetPartFileBufferedDataFlushState(*pBuffer, PB_WRITTEN);
			} else { //full file allocation
				ASSERT(dwBytesWritten == 1);
				DWORD dwAllocationError = ERROR_SUCCESS;
				if (!::FlushFileBuffers(pFile->m_hWrite))
					dwAllocationError = ::GetLastError();
				try {
					// WHY: allocation sentinels are not tracked in the normal
					// part-buffer list, but they still decrement the async-write
					// count above. SetLength can throw a CFileException at this
					// completion boundary; contain it here so the write thread can
					// delete the sentinel, retire the OVERLAPPED record, and keep
					// shutdown signaling balanced.
					pFile->m_hpartfile.SetLength(pBuffer->start); //truncate the extra byte
				} catch (CFileException *ex) {
					dwAllocationError = ex->m_lOsError != 0 ? static_cast<DWORD>(ex->m_lOsError) : ERROR_WRITE_FAULT;
					ex->Delete();
				} catch (CException *ex) {
					dwAllocationError = ERROR_WRITE_FAULT;
					ex->Delete();
				}
				if (dwAllocationError != ERROR_SUCCESS)
					theApp.QueueDebugLogLineEx(LOG_ERROR, _T("Part-file allocation completion failed while truncating sentinel byte: %s"), (LPCTSTR)GetErrorMessage(dwAllocationError, 1));
				delete pBuffer;
			}
		}
	} else {
		const DWORD dwEffectiveError = dwCompletionError != ERROR_SUCCESS ? dwCompletionError : ERROR_WRITE_FAULT;
		if (pFile)
			pFile->DecrementAsyncWriteCount();
		if (pBuffer->data) {
			pBuffer->dwError = dwEffectiveError;
			SetPartFileBufferedDataFlushState(*pBuffer, PB_ERROR);
		} else
			delete pBuffer;
		theApp.QueueDebugLogLineEx(LOG_ERROR, _T("Part-file overlapped write failed: expected %lu, written %lu, error %lu (%s)")
			, dwWrite, dwBytesWritten, dwEffectiveError, (LPCTSTR)GetErrorMessage(dwEffectiveError, 1));
	}

	if (HelperThreadLaunchSeams::GetState(m_Run) == RUN_STOP && pFile)
		RemFile(pFile);

	delete pOvWrite;
}

void CPartFileWriteThread::CancelPendingWrites()
{
	for (POSITION pos = m_listPendingIO.GetHeadPosition(); pos != NULL;) {
		const OverlappedWrite_Struct *pOvWrite = m_listPendingIO.GetNext(pos);
		CPartFile *pFile = pOvWrite != NULL ? pOvWrite->pFile : NULL;
		if (pFile == NULL || pFile->m_hWrite == INVALID_HANDLE_VALUE)
			continue;

		if (!::CancelIoEx(pFile->m_hWrite, const_cast<LPOVERLAPPED>(&pOvWrite->oOverlap))) {
			const DWORD dwError = ::GetLastError();
			if (dwError != ERROR_NOT_FOUND)
				theApp.QueueDebugLogLineEx(LOG_WARNING, _T("Failed to cancel part-file overlapped write during shutdown: %s"), (LPCTSTR)GetErrorMessage(dwError, 1));
		}
	}
}

void CPartFileWriteThread::ReleaseQueuedWritesForShutdown()
{
	ReleaseQueuedWriteListForShutdown(m_listToWrite);

	CSingleLock lockFlushList(&m_lockFlushList, TRUE);
	// WHY: shutdown may race a foreground FlushBuffer enqueue. RAII keeps the
	// flush-list lock balanced if CList cleanup or future diagnostics throw
	// while we are converting queued PB_PENDING buffers back to error state.
	ReleaseQueuedWriteListForShutdown(m_FlushList);
	HelperThreadLaunchSeams::ClearFlag(m_bNewData);
}

void CPartFileWriteThread::DrainPendingWrites()
{
	ReleaseQueuedWritesForShutdown();
	CancelPendingWrites();

	while (!m_listPendingIO.IsEmpty()) {
		DWORD dwBytesWritten = 0;
		ULONG_PTR completionKey = 0;
		OverlappedWrite_Struct *pCurIO = NULL;
		const BOOL bCompletionReceived = ::GetQueuedCompletionStatus(m_hPort, &dwBytesWritten, &completionKey, (LPOVERLAPPED*)&pCurIO, INFINITE);
		const DWORD dwCompletionError = bCompletionReceived != FALSE ? ERROR_SUCCESS : ::GetLastError();
		if (pCurIO != NULL)
			WriteCompletionRoutine(dwBytesWritten, pCurIO, dwCompletionError);
	}
}

bool CPartFileWriteThread::AddFile(CPartFile *pFile)
{
	if (!HelperThreadLaunchSeams::CanPostIocpWork(m_bThreadStarted, HelperThreadLaunchSeams::IsFlagSet(m_bStopRequested), m_hPort != NULL, HelperThreadLaunchSeams::GetState(m_Run) != RUN_STOP))
		return false;
	ASSERT(m_hPort && HelperThreadLaunchSeams::GetState(m_Run) != RUN_STOP);
	if (pFile && pFile->m_hWrite == INVALID_HANDLE_VALUE) {
		const CString sPartFile(RemoveFileExtension(pFile->GetFullName()));
		pFile->m_hWrite = LongPathSeams::CreateFile(sPartFile, GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		if (pFile->m_hWrite == INVALID_HANDLE_VALUE) {
			theApp.QueueDebugLogLineEx(LOG_ERROR, _T("Failed to open \"%s\" for overlapped write: %s"), (LPCTSTR)sPartFile, (LPCTSTR)GetErrorMessage(::GetLastError(), 1));
			pFile->SetStatus(PS_ERROR);
			return false;
		}
		if (m_hPort != ::CreateIoCompletionPort(pFile->m_hWrite, m_hPort, (ULONG_PTR)pFile, 0)) {
			theApp.QueueDebugLogLineEx(LOG_ERROR, _T("Failed to associate \"%s\" with IOCP: %s"), (LPCTSTR)sPartFile, (LPCTSTR)GetErrorMessage(::GetLastError(), 1));
			RemFile(pFile);
			pFile->SetStatus(PS_ERROR);
			return false;
		}
	}
	return true;
}

void CPartFileWriteThread::RemFile(CPartFile *pFile)
{
	ASSERT(pFile);
	if (pFile->m_hWrite != INVALID_HANDLE_VALUE) {
		VERIFY(::CloseHandle(pFile->m_hWrite));
		pFile->m_hWrite = INVALID_HANDLE_VALUE;
	}
}

void CPartFileWriteThread::WakeUpCall()
{
	if (!HelperThreadLaunchSeams::CanPostIocpWork(m_bThreadStarted, HelperThreadLaunchSeams::IsFlagSet(m_bStopRequested), m_hPort != NULL, HelperThreadLaunchSeams::GetState(m_Run) != RUN_STOP))
		return;
	//pending I/O makes posting unnecessary
	if (HelperThreadLaunchSeams::GetState(m_Run) == RUN_IDLE && m_listPendingIO.IsEmpty())
		PostQueuedCompletionStatus(m_hPort, 0, WAKEUP, NULL);
	else
		HelperThreadLaunchSeams::SetFlag(m_bNewData);
}
