//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.

#pragma once

#include <windows.h>

namespace HelperThreadLaunchSeams
{
	constexpr DWORD kHelperThreadShutdownWaitMs = 30000;

	inline void SetFlag(volatile LONG& rnFlag);
	inline void SetState(volatile LONG& rnState, LONG nState);

	enum class IocpShutdownAction
	{
		NoOp,
		WaitOnly,
		SignalAndWait
	};

	enum class ShutdownWaitAction
	{
		Finished,
		TimedOut,
		Failed
	};

	enum class SuspendedThreadResumeAction
	{
		LaunchFailed,
		Resumed,
		ResumeFailed
	};

	/**
	 * @brief Reports whether a helper-thread launch returned a live thread object.
	 */
	inline bool DidStartThread(const void *pThread)
	{
		return pThread != nullptr;
	}

	/**
	 * @brief Reports whether ResumeThread made a suspended helper runnable.
	 */
	inline bool DidResumeThread(DWORD dwResumeResult)
	{
		return dwResumeResult != static_cast<DWORD>(-1);
	}

	/**
	 * @brief Classifies a suspended helper launch after its resume attempt.
	 */
	inline SuspendedThreadResumeAction ClassifySuspendedThreadResume(const void *pThread, DWORD dwResumeResult)
	{
		if (!DidStartThread(pThread))
			return SuspendedThreadResumeAction::LaunchFailed;
		return DidResumeThread(dwResumeResult) ? SuspendedThreadResumeAction::Resumed : SuspendedThreadResumeAction::ResumeFailed;
	}

	/**
	 * @brief Resumes a suspended MFC auto-delete worker without changing its ownership policy.
	 */
	template <typename TThread>
	inline bool ResumeAutoDeleteSuspendedThread(TThread *pThread, DWORD &rdwLastError)
	{
		rdwLastError = ERROR_SUCCESS;
		if (pThread == nullptr)
			return false;

		if (!DidResumeThread(pThread->ResumeThread())) {
			rdwLastError = ::GetLastError();
			return false;
		}
		return true;
	}

	/**
	 * @brief Owns and resumes a suspended MFC worker, releasing it if resume fails.
	 */
	template <typename TThread>
	inline bool OwnAndResumeSuspendedThread(TThread *&rpOwnedThread, TThread *pThread, DWORD &rdwLastError)
	{
		rdwLastError = ERROR_SUCCESS;
		rpOwnedThread = nullptr;
		if (pThread == nullptr)
			return false;

		pThread->m_bAutoDelete = FALSE;
		rpOwnedThread = pThread;
		if (!DidResumeThread(pThread->ResumeThread())) {
			rdwLastError = ::GetLastError();
			delete rpOwnedThread;
			rpOwnedThread = nullptr;
			return false;
		}
		return true;
	}

	/**
	 * @brief Classifies shutdown for workers that are controlled through an IOCP.
	 */
	inline IocpShutdownAction ClassifyIocpShutdown(bool bThreadStarted, bool bPortReady)
	{
		if (!bThreadStarted)
			return IocpShutdownAction::NoOp;
		return bPortReady ? IocpShutdownAction::SignalAndWait : IocpShutdownAction::WaitOnly;
	}

	/**
	 * @brief Reports whether an IOCP helper can accept wake or file work.
	 */
	inline bool CanPostIocpWork(bool bThreadStarted, bool bStopRequested, bool bPortReady, bool bWorkerRunning)
	{
		return bThreadStarted && !bStopRequested && bPortReady && bWorkerRunning;
	}

	/**
	 * @brief Requests IOCP helper shutdown and posts the stop completion when the port is ready.
	 */
	inline IocpShutdownAction RequestIocpShutdown(volatile LONG& rnStopRequested, volatile LONG& rnRunState, LONG nStopState, bool bThreadStarted, HANDLE hPort)
	{
		SetFlag(rnStopRequested);
		const IocpShutdownAction action = ClassifyIocpShutdown(bThreadStarted, hPort != NULL);
		if (action == IocpShutdownAction::NoOp)
			return action;

		SetState(rnRunState, nStopState);
		if (action == IocpShutdownAction::SignalAndWait)
			(void)::PostQueuedCompletionStatus(hPort, 0, 0, NULL);
		return action;
	}

	/**
	 * @brief Creates the private IOCP used by helper threads.
	 */
	inline bool TryCreateIocpPort(HANDLE& rhPort, DWORD& rdwLastError)
	{
		rdwLastError = ERROR_SUCCESS;
		rhPort = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 1);
		if (rhPort != NULL)
			return true;
		rdwLastError = ::GetLastError();
		return false;
	}

	/**
	 * @brief Closes and clears a helper IOCP handle if one is owned.
	 */
	inline void CloseIocpPort(HANDLE& rhPort)
	{
		if (rhPort != NULL) {
			(void)::CloseHandle(rhPort);
			rhPort = NULL;
		}
	}

	/**
	 * @brief Reports whether an IOCP helper should enter the blocking completion wait.
	 */
	inline bool ShouldWaitForIocpWorkerCompletion(bool bStopRequested, LONG nRunState, LONG nStopState)
	{
		return !bStopRequested && nRunState != nStopState;
	}

	/**
	 * @brief Reports whether an IOCP helper should process the received completion.
	 */
	inline bool IsIocpStopCompletion(BOOL bCompletionReceived, ULONG_PTR nCompletionKey, const void *pOverlapped)
	{
		return bCompletionReceived != FALSE && nCompletionKey == 0 && pOverlapped == NULL;
	}

	/**
	 * @brief Reports whether an IOCP result contains a completion packet.
	 *
	 * GetQueuedCompletionStatus returns FALSE both for "no packet available"
	 * and for a dequeued overlapped operation that completed with an error.
	 * Failed operation completions still carry a non-null OVERLAPPED pointer
	 * and must be processed; otherwise the owner keeps the buffer pending
	 * forever.
	 */
	inline bool ShouldProcessIocpWorkerCompletion(BOOL bCompletionReceived, ULONG_PTR nCompletionKey, const void *pOverlapped)
	{
		if (IsIocpStopCompletion(bCompletionReceived, nCompletionKey, pOverlapped))
			return false;
		return nCompletionKey != 0 || pOverlapped != NULL;
	}

	/**
	 * @brief Reports whether deferred new data should wake an idle IOCP helper.
	 */
	inline bool ShouldPostIocpWakeAfterNewData(LONG nPreviousNewDataFlag, bool bPendingIoEmpty)
	{
		return nPreviousNewDataFlag != 0 && bPendingIoEmpty;
	}

	/**
	 * @brief Reports whether an event-driven helper should be waited during shutdown.
	 */
	inline bool ShouldWaitForEventThreadShutdown(bool bThreadStarted)
	{
		return bThreadStarted;
	}

	/**
	 * @brief Classifies a bounded helper-thread shutdown wait.
	 */
	inline ShutdownWaitAction ClassifyShutdownWait(DWORD dwWait)
	{
		if (dwWait == WAIT_OBJECT_0)
			return ShutdownWaitAction::Finished;
		if (dwWait == WAIT_TIMEOUT)
			return ShutdownWaitAction::TimedOut;
		return ShutdownWaitAction::Failed;
	}

	/**
	 * @brief Performs the common bounded wait and final cooperative wait for event-ended helpers.
	 */
	template <typename TEvent, typename TTimedOutFn, typename TFailedFn>
	inline ShutdownWaitAction WaitForEventThreadShutdown(
		TEvent& rThreadEndedEvent,
		bool bThreadStarted,
		DWORD dwWaitMilliseconds,
		TTimedOutFn timedOutFn,
		TFailedFn failedFn)
	{
		if (!ShouldWaitForEventThreadShutdown(bThreadStarted))
			return ShutdownWaitAction::Finished;

		const DWORD dwWait = ::WaitForSingleObject(rThreadEndedEvent, dwWaitMilliseconds);
		const ShutdownWaitAction waitAction = ClassifyShutdownWait(dwWait);
		if (waitAction == ShutdownWaitAction::TimedOut) {
			timedOutFn();
			rThreadEndedEvent.Lock();
		} else if (waitAction == ShutdownWaitAction::Failed) {
			const DWORD dwLastError = (dwWait == WAIT_FAILED) ? ::GetLastError() : ERROR_SUCCESS;
			failedFn(dwLastError);
			rThreadEndedEvent.Lock();
		}
		return waitAction;
	}

	/**
	 * @brief Atomically marks a helper-thread flag as true.
	 */
	inline void SetFlag(volatile LONG& rnFlag)
	{
		::InterlockedExchange(&rnFlag, 1);
	}

	/**
	 * @brief Atomically marks a helper-thread flag as false.
	 */
	inline void ClearFlag(volatile LONG& rnFlag)
	{
		::InterlockedExchange(&rnFlag, 0);
	}

	/**
	 * @brief Atomically reads a helper-thread flag.
	 */
	inline bool IsFlagSet(volatile LONG& rnFlag)
	{
		return ::InterlockedCompareExchange(&rnFlag, 0, 0) != 0;
	}

	/**
	 * @brief Atomically writes a helper-thread state value.
	 */
	inline void SetState(volatile LONG& rnState, LONG nState)
	{
		::InterlockedExchange(&rnState, nState);
	}

	/**
	 * @brief Atomically reads a helper-thread state value.
	 */
	inline LONG GetState(volatile LONG& rnState)
	{
		return ::InterlockedCompareExchange(&rnState, 0, 0);
	}

	/**
	 * @brief Atomically reads a helper-thread state value from const methods.
	 */
	inline LONG GetState(const volatile LONG& rnState)
	{
		return GetState(const_cast<volatile LONG&>(rnState));
	}

	/**
	 * @brief Atomically exchanges a helper-thread state value.
	 */
	inline LONG ExchangeState(volatile LONG& rnState, LONG nState)
	{
		return ::InterlockedExchange(&rnState, nState);
	}
}
