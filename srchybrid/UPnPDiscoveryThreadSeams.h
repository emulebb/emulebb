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
}
