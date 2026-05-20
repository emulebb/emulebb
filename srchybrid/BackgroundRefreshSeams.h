#pragma once

#include <memory>

namespace BackgroundRefreshSeams
{
/**
 * @brief Shared queue state for one owner-managed background refresh worker.
 */
struct SRefreshState
{
	SRefreshState()
		: lQueued(0)
	{
	}

	volatile LONG lQueued;
};

/**
 * @brief Records a refresh attempt only after the background worker actually starts.
 */
inline bool ShouldRecordRefreshAttempt(bool bThreadCreated, bool bThreadResumed)
{
	return bThreadCreated && bThreadResumed;
}

/**
 * @brief Returns whether a refresh worker is currently queued or running.
 */
inline bool IsRefreshQueued(SRefreshState& rState)
{
	return ::InterlockedCompareExchange(&rState.lQueued, 0, 0) != 0;
}

/**
 * @brief Atomically marks a refresh worker as queued when no worker is active.
 */
inline bool TryMarkRefreshQueued(SRefreshState& rState)
{
	return ::InterlockedCompareExchange(&rState.lQueued, 1, 0) == 0;
}

/**
 * @brief Clears queued state after the worker has finished or failed to start.
 */
inline void ClearRefreshQueued(SRefreshState& rState)
{
	(void)::InterlockedExchange(&rState.lQueued, 0);
}

/**
 * @brief Clears queued state during owner teardown without interrupting an in-flight worker.
 */
inline void ClearRefreshOnOwnerTeardown(SRefreshState& rState)
{
	ClearRefreshQueued(rState);
}

/**
 * @brief Starts an owner-managed refresh worker after atomically claiming queued state.
 */
template <typename Context, typename StartWorkerFn, typename CleanupContextFn>
inline bool StartQueuedRefreshWorker(
	SRefreshState& rState,
	std::unique_ptr<Context>& pContext,
	StartWorkerFn startWorkerFn,
	CleanupContextFn cleanupContextFn)
{
	if (!TryMarkRefreshQueued(rState)) {
		if (pContext)
			cleanupContextFn(*pContext);
		return false;
	}

	Context *pThreadContext = pContext.release();
	if (!startWorkerFn(pThreadContext)) {
		std::unique_ptr<Context> pCleanupContext(pThreadContext);
		ClearRefreshQueued(rState);
		if (pCleanupContext)
			cleanupContextFn(*pCleanupContext);
		return false;
	}

	return true;
}

struct SRefreshCompletionPostResult
{
	bool bDelivered = false;
	DWORD dwLastError = ERROR_SUCCESS;
};

/**
 * @brief Posts refresh completion and clears queued state when the owner window is unavailable.
 */
inline SRefreshCompletionPostResult PostRefreshCompletion(HWND hNotifyWnd, UINT uMessage, bool bUpdated, const std::shared_ptr<SRefreshState>& pRefreshState)
{
	SRefreshCompletionPostResult result;
	if (hNotifyWnd != NULL && ::PostMessage(hNotifyWnd, uMessage, bUpdated ? 1u : 0u, 0)) {
		result.bDelivered = true;
		return result;
	}

	result.dwLastError = ::GetLastError();
	if (pRefreshState)
		(void)::InterlockedExchange(&pRefreshState->lQueued, 0);
	return result;
}
}
