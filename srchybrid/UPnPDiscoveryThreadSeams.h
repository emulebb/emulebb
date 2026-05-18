#pragma once

#include <windows.h>

#define EMULE_TEST_HAVE_UPNP_DISCOVERY_THREAD_SEAMS 1

namespace UPnPDiscoveryThreadSeams
{
/**
 * @brief Action selected after probing a discovery worker without blocking.
 */
enum class ENonblockingWaitAction
{
	KeepWaiting,
	ReleaseFinished,
	ReleaseAfterWaitFailure
};

/**
 * @brief Action selected after asking a discovery worker to stop cooperatively.
 */
enum class EStopWaitAction
{
	ReleaseFinished,
	WaitCooperatively,
	ReleaseAfterWaitFailure
};

/**
 * @brief Timeout used when first waiting for a cooperative discovery-thread stop.
 */
constexpr DWORD kCooperativeStopWaitMs = 7000;

/**
 * @brief Atomically requests cooperative cancellation for discovery workers.
 */
inline void RequestAbort(volatile LONG& rnAbortFlag)
{
	::InterlockedExchange(&rnAbortFlag, 1);
}

/**
 * @brief Atomically clears cooperative cancellation after a worker has stopped.
 */
inline void ClearAbort(volatile LONG& rnAbortFlag)
{
	::InterlockedExchange(&rnAbortFlag, 0);
}

/**
 * @brief Atomically reads whether cooperative cancellation was requested.
 */
inline bool IsAbortRequested(volatile LONG& rnAbortFlag)
{
	return ::InterlockedCompareExchange(&rnAbortFlag, 0, 0) != 0;
}

/**
 * @brief Releases an owned non-auto-deleting discovery thread wrapper.
 */
template <typename TThread>
inline void ReleaseDiscoveryThread(TThread *&rpThread)
{
	delete rpThread;
	rpThread = NULL;
}

/**
 * @brief Classifies a nonblocking WaitForSingleObject result for an owned discovery worker.
 */
inline ENonblockingWaitAction ClassifyNonblockingWait(DWORD dwWait)
{
	if (dwWait == WAIT_OBJECT_0)
		return ENonblockingWaitAction::ReleaseFinished;
	if (dwWait == WAIT_FAILED)
		return ENonblockingWaitAction::ReleaseAfterWaitFailure;
	return ENonblockingWaitAction::KeepWaiting;
}

/**
 * @brief Classifies the first bounded wait after cooperative cancellation is requested.
 */
inline EStopWaitAction ClassifyStopWait(DWORD dwWait)
{
	if (dwWait == WAIT_OBJECT_0)
		return EStopWaitAction::ReleaseFinished;
	if (dwWait == WAIT_FAILED)
		return EStopWaitAction::ReleaseAfterWaitFailure;
	return EStopWaitAction::WaitCooperatively;
}

/**
 * @brief Probes and releases a finished or stale owned discovery thread wrapper.
 */
template <typename TThread>
inline ENonblockingWaitAction ReapDiscoveryThreadIfFinished(TThread *&rpThread, DWORD &rdwLastError)
{
	rdwLastError = ERROR_SUCCESS;
	if (rpThread == NULL)
		return ENonblockingWaitAction::KeepWaiting;

	const DWORD dwWait = ::WaitForSingleObject(rpThread->m_hThread, 0);
	const ENonblockingWaitAction eAction = ClassifyNonblockingWait(dwWait);
	if (eAction == ENonblockingWaitAction::ReleaseAfterWaitFailure)
		rdwLastError = ::GetLastError();
	if (eAction == ENonblockingWaitAction::ReleaseFinished || eAction == ENonblockingWaitAction::ReleaseAfterWaitFailure)
		ReleaseDiscoveryThread(rpThread);
	return eAction;
}

/**
 * @brief Requests cooperative stop and performs the first bounded discovery-thread wait.
 */
template <typename TThread>
inline EStopWaitAction RequestDiscoveryThreadStop(TThread *pThread, volatile LONG &rnAbortFlag, DWORD &rdwLastError)
{
	rdwLastError = ERROR_SUCCESS;
	if (pThread == NULL)
		return EStopWaitAction::ReleaseFinished;

	RequestAbort(rnAbortFlag);
	const DWORD dwWait = ::WaitForSingleObject(pThread->m_hThread, kCooperativeStopWaitMs);
	const EStopWaitAction eAction = ClassifyStopWait(dwWait);
	if (eAction == EStopWaitAction::ReleaseAfterWaitFailure)
		rdwLastError = ::GetLastError();
	return eAction;
}

/**
 * @brief Reports whether ResumeThread successfully made a suspended discovery worker runnable.
 */
inline bool DidResumeDiscoveryThread(DWORD dwResumeResult)
{
	return dwResumeResult != static_cast<DWORD>(-1);
}

/**
 * @brief Takes ownership of a suspended discovery worker and releases it if ResumeThread fails.
 */
template <typename TThread, typename TOwner>
inline bool OwnAndResumeDiscoveryThread(TThread *&rpOwnedThread, TThread *pThread, TOwner *pOwner, DWORD &rdwLastError)
{
	rdwLastError = ERROR_SUCCESS;
	if (pThread == NULL)
		return false;

	pThread->m_bAutoDelete = FALSE;
	pThread->SetValues(pOwner);
	rpOwnedThread = pThread;
	if (!DidResumeDiscoveryThread(pThread->ResumeThread())) {
		rdwLastError = ::GetLastError();
		ReleaseDiscoveryThread(rpOwnedThread);
		return false;
	}
	return true;
}
}
