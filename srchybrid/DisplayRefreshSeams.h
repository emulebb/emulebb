#pragma once

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <memory>

#define EMULE_TEST_HAVE_DISPLAY_REFRESH_OWNED_POST 1

enum EDisplayRefreshMask : uint32
{
	DISPLAY_REFRESH_NONE = 0,
	DISPLAY_REFRESH_CLIENT_LIST = 1u << 0,
	DISPLAY_REFRESH_DOWNLOAD_LIST = 1u << 1,
	DISPLAY_REFRESH_DOWNLOAD_CLIENTS = 1u << 2,
	DISPLAY_REFRESH_UPLOAD_LIST = 1u << 3,
	DISPLAY_REFRESH_QUEUE_LIST = 1u << 4
};

struct CPartFileDisplayUpdateRequest
{
	unsigned char fileHash[16];
};

struct CPartFileProgressUpdateRequest
{
	unsigned char fileHash[16];
	uint64_t fileSize;
	uint32_t progress;
};

struct CClientDisplayUpdateRequest
{
	unsigned char userHash[16];
	DWORD connectIP;
	USHORT userPort;
	USHORT reserved;
};

/**
 * @brief Reports whether the caller must marshal a display refresh back to the main thread.
 */
inline bool ShouldQueueDisplayRefresh(UINT uCurrentThreadId, UINT uMainThreadId)
{
	return uMainThreadId != 0 && uCurrentThreadId != uMainThreadId;
}

/**
 * @brief Applies the existing randomized throttling window used by UI refresh helpers.
 */
inline bool ShouldRunDisplayRefresh(bool bForce, ULONGLONG dwCurrentTick, ULONGLONG dwLastRefreshTick, ULONGLONG dwMinimumWait, ULONGLONG dwRandomWait = 0)
{
	return bForce || dwCurrentTick >= dwLastRefreshTick + dwMinimumWait + dwRandomWait;
}

/**
 * @brief Atomically merges a refresh mask and returns the previous value.
 */
inline LONG AccumulatePendingDisplayMask(std::atomic<LONG> &rnPendingMask, LONG nMask)
{
	LONG nCurrent = rnPendingMask.load();
	for (;;) {
		const LONG nUpdated = nCurrent | nMask;
		if (rnPendingMask.compare_exchange_weak(nCurrent, nUpdated))
			return nCurrent;
	}
}

/**
 * @brief Posts a heap-owned display refresh request and releases it only after successful delivery.
 */
template <typename TRequest>
inline bool PostOwnedDisplayRefreshRequest(HWND hTargetWnd, UINT uMessage, std::unique_ptr<TRequest> &rpRequest)
{
	if (hTargetWnd == NULL || rpRequest == NULL) {
		rpRequest.reset();
		return false;
	}

	if (::PostMessage(hTargetWnd, uMessage, reinterpret_cast<WPARAM>(rpRequest.get()), 0) == FALSE) {
		rpRequest.reset();
		return false;
	}

	(void)rpRequest.release();
	return true;
}
