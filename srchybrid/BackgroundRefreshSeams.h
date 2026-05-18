#pragma once

#include <memory>

#include "DirectDownload.h"

namespace BackgroundRefreshSeams
{
/**
 * @brief Shared queue/cancellation state for one owner-managed background refresh worker.
 */
struct SRefreshState
{
	SRefreshState()
		: lQueued(0)
	{
	}

	volatile LONG lQueued;
	std::shared_ptr<DirectDownload::CDownloadCancellation> pCancellation;
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
 * @brief Clears queued state and releases any owner-visible cancellation handle.
 */
inline void ClearRefreshQueued(SRefreshState& rState)
{
	(void)::InterlockedExchange(&rState.lQueued, 0);
	rState.pCancellation.reset();
}

/**
 * @brief Cancels any live transfer and clears queued state during owner teardown.
 */
inline void CancelAndClearRefresh(SRefreshState& rState)
{
	if (rState.pCancellation)
		rState.pCancellation->Cancel();
	ClearRefreshQueued(rState);
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
