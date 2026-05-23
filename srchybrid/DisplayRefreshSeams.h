#pragma once

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <memory>

#define EMULE_TEST_HAVE_DISPLAY_REFRESH_OWNED_POST 1

enum EDesktopUiRefreshIntervalMs : uint32_t
{
	DESKTOP_UI_REFRESH_PAUSED_MS = 0,
	DESKTOP_UI_REFRESH_FAST_MS = 500,
	DESKTOP_UI_REFRESH_NORMAL_MS = 1000,
	DESKTOP_UI_REFRESH_BELOWNORMAL_MS = 2000,
	DESKTOP_UI_REFRESH_SLOW_MS = 5000,
	DESKTOP_UI_REFRESH_VERYSLOW_MS = 10000
};

enum EDisplayRefreshMask : uint32_t
{
	DISPLAY_REFRESH_NONE = 0,
	DISPLAY_REFRESH_CLIENT_LIST = 1u << 0,
	DISPLAY_REFRESH_DOWNLOAD_LIST = 1u << 1,
	DISPLAY_REFRESH_DOWNLOAD_CLIENTS = 1u << 2,
	DISPLAY_REFRESH_UPLOAD_LIST = 1u << 3,
	DISPLAY_REFRESH_QUEUE_LIST = 1u << 4,
	DISPLAY_REFRESH_TRANSFER_SUMMARY = 1u << 5
};

enum ETransferDisplayRefreshState : uint32_t
{
	TRANSFER_DISPLAY_REFRESH_PAUSED = 0,
	TRANSFER_DISPLAY_REFRESH_RUNNING = 1
};

enum ETransferDisplayListKind : uint32_t
{
	TRANSFER_DISPLAY_LIST_DOWNLOADS = 0,
	TRANSFER_DISPLAY_LIST_UPLOADS,
	TRANSFER_DISPLAY_LIST_DOWNLOAD_CLIENTS,
	TRANSFER_DISPLAY_LIST_QUEUE,
	TRANSFER_DISPLAY_LIST_CLIENTS
};

struct CPartFileDisplayUpdateRequest
{
	unsigned char fileHash[16];
	bool force;
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
	bool force;
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
 * @brief Normalizes the desktop list refresh interval to the supported System Informer-style values.
 */
inline UINT NormalizeDesktopUiRefreshIntervalMs(UINT uIntervalMs)
{
	switch (uIntervalMs) {
	case DESKTOP_UI_REFRESH_PAUSED_MS:
	case DESKTOP_UI_REFRESH_FAST_MS:
	case DESKTOP_UI_REFRESH_NORMAL_MS:
	case DESKTOP_UI_REFRESH_BELOWNORMAL_MS:
	case DESKTOP_UI_REFRESH_SLOW_MS:
	case DESKTOP_UI_REFRESH_VERYSLOW_MS:
		return uIntervalMs;
	default:
		return DESKTOP_UI_REFRESH_BELOWNORMAL_MS;
	}
}

/**
 * @brief Applies the configured desktop UI refresh interval without per-row jitter.
 */
inline bool ShouldRunPreferenceAlignedDisplayRefresh(bool bForce, ULONGLONG dwCurrentTick, ULONGLONG dwLastRefreshTick, UINT uDesktopUiRefreshIntervalMs)
{
	const UINT uNormalizedIntervalMs = NormalizeDesktopUiRefreshIntervalMs(uDesktopUiRefreshIntervalMs);
	if (uNormalizedIntervalMs == DESKTOP_UI_REFRESH_PAUSED_MS)
		return bForce;
	return ShouldRunDisplayRefresh(bForce, dwCurrentTick, dwLastRefreshTick, uNormalizedIntervalMs, 0);
}

/**
 * @brief Returns the transfer-window presentation timer cadence.
 */
inline UINT GetTransferDisplayRefreshTimerDelayMs(UINT uDesktopUiRefreshIntervalMs)
{
	return NormalizeDesktopUiRefreshIntervalMs(uDesktopUiRefreshIntervalMs);
}

/**
 * @brief Returns the lightweight transfer-rate presentation timer cadence.
 */
inline UINT GetTransferRateDisplayRefreshTimerDelayMs()
{
	return DESKTOP_UI_REFRESH_NORMAL_MS;
}

/**
 * @brief Resolves whether routine transfer-window presentation refreshes may run.
 */
inline ETransferDisplayRefreshState ResolveTransferDisplayRefreshState(
	bool bAppClosing,
	bool bDesktopUiRefreshPaused,
	bool bTransferWindowActive,
	bool bMainWindowVisible,
	bool bMainWindowMinimized,
	bool bForegroundOwnedByMainWindow)
{
	if (bAppClosing || bDesktopUiRefreshPaused || !bTransferWindowActive || !bMainWindowVisible || bMainWindowMinimized || !bForegroundOwnedByMainWindow)
		return TRANSFER_DISPLAY_REFRESH_PAUSED;
	return TRANSFER_DISPLAY_REFRESH_RUNNING;
}

/**
 * @brief Resolves whether lightweight transfer-rate presentation may update.
 */
inline bool ShouldRefreshTransferRatePresentation(bool bAppClosing, bool bMainWindowVisible)
{
	return !bAppClosing && bMainWindowVisible;
}

/**
 * @brief Keeps only transfer-window refresh work whose target list is currently visible.
 */
inline uint32_t FilterVisibleTransferDisplayRefreshMask(
	uint32_t uPendingMask,
	ETransferDisplayRefreshState eRefreshState,
	bool bTransferWindowActive,
	bool bTransferWindowVisible,
	bool bDownloadListVisible,
	bool bUploadListVisible,
	bool bDownloadClientsVisible,
	bool bQueueListVisible,
	bool bClientListVisible)
{
	if (eRefreshState != TRANSFER_DISPLAY_REFRESH_RUNNING || !bTransferWindowActive || !bTransferWindowVisible)
		return DISPLAY_REFRESH_NONE;

	uint32_t uVisibleMask = DISPLAY_REFRESH_NONE;
	if (bDownloadListVisible)
		uVisibleMask |= DISPLAY_REFRESH_DOWNLOAD_LIST;
	if (bUploadListVisible)
		uVisibleMask |= DISPLAY_REFRESH_UPLOAD_LIST;
	if (bDownloadClientsVisible)
		uVisibleMask |= DISPLAY_REFRESH_DOWNLOAD_CLIENTS;
	if (bQueueListVisible)
		uVisibleMask |= DISPLAY_REFRESH_QUEUE_LIST;
	if (bClientListVisible)
		uVisibleMask |= DISPLAY_REFRESH_CLIENT_LIST;

	uVisibleMask |= DISPLAY_REFRESH_TRANSFER_SUMMARY;
	return uPendingMask & uVisibleMask;
}

/**
 * @brief Builds the explicit user-requested refresh mask for currently visible transfer UI.
 */
inline uint32_t BuildExplicitTransferDisplayRefreshMask(
	ETransferDisplayRefreshState eRefreshState,
	bool bTransferWindowActive,
	bool bTransferWindowVisible,
	bool bDownloadListVisible,
	bool bUploadListVisible,
	bool bDownloadClientsVisible,
	bool bQueueListVisible,
	bool bClientListVisible)
{
	const uint32_t uAllTransferDisplayMask =
		DISPLAY_REFRESH_DOWNLOAD_LIST
		| DISPLAY_REFRESH_UPLOAD_LIST
		| DISPLAY_REFRESH_DOWNLOAD_CLIENTS
		| DISPLAY_REFRESH_QUEUE_LIST
		| DISPLAY_REFRESH_CLIENT_LIST
		| DISPLAY_REFRESH_TRANSFER_SUMMARY;
	return FilterVisibleTransferDisplayRefreshMask(
		uAllTransferDisplayMask,
		eRefreshState,
		bTransferWindowActive,
		bTransferWindowVisible,
		bDownloadListVisible,
		bUploadListVisible,
		bDownloadClientsVisible,
		bQueueListVisible,
		bClientListVisible);
}

/**
 * @brief Keeps producer-driven transfer refreshes aligned to the shared transfer timer.
 */
inline uint32_t BuildQueuedTransferDisplayRefreshMask(uint32_t uRequestMask, bool)
{
	return uRequestMask;
}

/**
 * @brief Keeps producer-driven "force" updates on the shared transfer timer.
 */
inline bool ShouldFlushForcedTransferDisplayRefresh(bool bForce, uint32_t uVisibleMask)
{
	UNREFERENCED_PARAMETER(bForce);
	UNREFERENCED_PARAMETER(uVisibleMask);
	return false;
}

inline bool IsTransferRefreshSensitiveSortColumn(ETransferDisplayListKind eListKind, int iSortColumn)
{
	switch (eListKind) {
	case TRANSFER_DISPLAY_LIST_DOWNLOADS:
		switch (iSortColumn) {
		case 0:  // filename
		case 1:  // size
		case 14: // added on
			return false;
		default:
			return iSortColumn >= 0;
		}
	case TRANSFER_DISPLAY_LIST_UPLOADS:
		switch (iSortColumn) {
		case 0:  // user name
		case 1:  // file name
		case 13: // client software
		case 15: // IP
		case 17: // client hash
		case 19: // file size
		case 21: // folder
			return false;
		default:
			return iSortColumn >= 0;
		}
	case TRANSFER_DISPLAY_LIST_DOWNLOAD_CLIENTS:
		switch (iSortColumn) {
		case 0: // user name
		case 1: // client software
		case 2: // file name
		case 7: // source origin
			return false;
		default:
			return iSortColumn >= 0;
		}
	case TRANSFER_DISPLAY_LIST_QUEUE:
		switch (iSortColumn) {
		case 0:  // user name
		case 1:  // file name
		case 15: // client software
		case 17: // IP
		case 19: // client hash
		case 20: // file size
		case 21: // folder
			return false;
		default:
			return iSortColumn >= 0;
		}
	case TRANSFER_DISPLAY_LIST_CLIENTS:
		switch (iSortColumn) {
		case 0: // user name
		case 5: // client software
		case 7: // client hash
			return false;
		default:
			return iSortColumn >= 0;
		}
	default:
		return false;
	}
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
 * @brief Atomically removes selected pending bits and returns the bits that were present.
 */
inline LONG DrainPendingDisplayMask(std::atomic<LONG> &rnPendingMask, LONG nMask)
{
	LONG nCurrent = rnPendingMask.load();
	for (;;) {
		const LONG nDrained = nCurrent & nMask;
		if (nDrained == 0)
			return 0;
		const LONG nUpdated = nCurrent & ~nMask;
		if (rnPendingMask.compare_exchange_weak(nCurrent, nUpdated))
			return nDrained;
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
