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
		pBuffer->flushed = PB_ERROR;
	} else {
		// Allocation sentinels are not tracked in the part-file buffer list.
		delete pBuffer;
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
#if EMULE_COMPILED_STARTUP_PROFILING
	const ULONGLONG ullPhaseStart = theApp.GetStartupProfileTimestampUs();
#endif
	CWinThread *pThread = AfxBeginThread(RunProc, (LPVOID)this, THREAD_PRIORITY_BELOW_NORMAL);
	m_bThreadStarted = HelperThreadLaunchSeams::DidStartThread(pThread);
	if (!m_bThreadStarted) {
		theApp.QueueDebugLogLineEx(LOG_ERROR, _T("Failed to start part-file write helper thread"));
		m_eventThreadEnded.SetEvent();
	}
#if EMULE_COMPILED_STARTUP_PROFILING
	theApp.AppendStartupProfileLine(_T("broadband.partfile_write.launch_thread"), theApp.GetStartupProfileElapsedUs(ullPhaseStart), ullPhaseStart);
#endif
}

CPartFileWriteThread::~CPartFileWriteThread()
{
	ASSERT(!m_hPort && HelperThreadLaunchSeams::GetState(m_Run) == RUN_STOP);
}

UINT AFX_CDECL CPartFileWriteThread::RunProc(LPVOID pParam)
{
#if EMULE_COMPILED_STARTUP_PROFILING
	if (theApp.IsStartupProfilingEnabled())
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
#if EMULE_COMPILED_STARTUP_PROFILING
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
#if EMULE_COMPILED_STARTUP_PROFILING
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
		if (!m_FlushList.IsEmpty()) {
			m_lockFlushList.Lock();
			while (!m_FlushList.IsEmpty())
				m_listToWrite.AddTail(m_FlushList.RemoveHead());
			HelperThreadLaunchSeams::ClearFlag(m_bNewData);
			m_lockFlushList.Unlock();
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
		const ToWrite &item = m_listToWrite.RemoveHead();
		PartFileBufferedData *pBuffer = item.pBuffer;
		ASSERT(pBuffer->end >= pBuffer->start && (pBuffer->data || pBuffer->end == pBuffer->start)); //verifies allocation requests too

		CPartFile *pFile = item.pFile;
		if (AddFile(pFile)) {
			//initiate write
			OverlappedWrite_Struct *pOvWrite = new OverlappedWrite_Struct;
			pOvWrite->oOverlap.Internal = 0;
			pOvWrite->oOverlap.InternalHigh = 0;
			//pOvWrite->oOverlap.Offset = LODWORD(currentblock->StartOffset);
			//pOvWrite->oOverlap.OffsetHigh = HIDWORD(currentblock->StartOffset);
			*(uint64*)&pOvWrite->oOverlap.Offset = pBuffer->start;
			pOvWrite->oOverlap.hEvent = 0;
			pOvWrite->pFile = pFile;
			pOvWrite->pBuffer = pBuffer;

			static const BYTE zero = 0;
			if (!::WriteFile(pFile->m_hWrite, pBuffer->data ? pBuffer->data : &zero, (DWORD)(pBuffer->end - pBuffer->start + 1), NULL, (LPOVERLAPPED)pOvWrite)) {
				DWORD dwError = ::GetLastError();
				if (dwError != ERROR_IO_PENDING) {
					delete pOvWrite;
					MarkWriteDispatchFailed(item.pBuffer, dwError);
					theApp.QueueDebugLogLineEx(LOG_WARNING, _T("WriteBuffers error: %s"), (LPCTSTR)GetErrorMessage(dwError, 1));
					RemFile(pFile);
					return;
				}
			}
			pOvWrite->pos = m_listPendingIO.AddTail(pOvWrite);
			++pFile->m_iWrites;
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
			--pFile->m_iWrites;
			if (pBuffer->data) { //write data
				ASSERT(pBuffer->flushed = PB_PENDING && pFile->m_iWrites >= 0);
				pBuffer->flushed = PB_WRITTEN;
			} else { //full file allocation
				ASSERT(dwBytesWritten == 1);
				::FlushFileBuffers(pFile->m_hWrite);
				pFile->m_hpartfile.SetLength(pBuffer->start); //truncate the extra byte
				delete pBuffer;
			}
		}
	} else {
		const DWORD dwEffectiveError = dwCompletionError != ERROR_SUCCESS ? dwCompletionError : ERROR_WRITE_FAULT;
		if (pFile)
			--pFile->m_iWrites;
		if (pBuffer->data) {
			pBuffer->dwError = dwEffectiveError;
			pBuffer->flushed = PB_ERROR;
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

void CPartFileWriteThread::DrainPendingWrites()
{
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
