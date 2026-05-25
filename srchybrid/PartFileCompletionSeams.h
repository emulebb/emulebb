#pragma once

#include <atlstr.h>
#include <windows.h>

namespace PartFileCompletionSeams
{
/**
 * @brief Result category for delivering the file-completion worker result.
 */
enum class EWorkerCompletionDelivery
{
	Delivered,
	SkippedAppClosing,
	Failed
};

/**
 * @brief Result of handing a part-file completion worker result to the UI thread.
 */
struct SWorkerCompletionPostResult
{
	EWorkerCompletionDelivery eDelivery = EWorkerCompletionDelivery::Failed;
	DWORD dwLastError = ERROR_SUCCESS;
};

/**
 * @brief Reports whether a part-file completion worker was created.
 */
inline bool DidStartCompletionThread(const void *pThread) noexcept
{
	return pThread != nullptr;
}

/**
 * @brief Classifies the result of a worker-to-UI completion post.
 */
inline EWorkerCompletionDelivery ClassifyWorkerCompletionPostResult(const bool bAppClosing, const bool bPostSucceeded) noexcept
{
	if (bAppClosing)
		return EWorkerCompletionDelivery::SkippedAppClosing;
	return bPostSucceeded ? EWorkerCompletionDelivery::Delivered : EWorkerCompletionDelivery::Failed;
}

/**
 * @brief Posts a file-completion worker result unless the app is already closing.
 */
inline SWorkerCompletionPostResult PostWorkerCompletion(
	const bool bAppClosing,
	const HWND hNotifyWnd,
	const UINT uMessage,
	const WPARAM wParam,
	const LPARAM lParam)
{
	SWorkerCompletionPostResult result;
	if (bAppClosing) {
		result.eDelivery = EWorkerCompletionDelivery::SkippedAppClosing;
		return result;
	}

	if (hNotifyWnd == NULL) {
		result.eDelivery = EWorkerCompletionDelivery::Failed;
		result.dwLastError = ERROR_INVALID_WINDOW_HANDLE;
		return result;
	}

	::SetLastError(ERROR_SUCCESS);
	const bool bPosted = ::PostMessage(hNotifyWnd, uMessage, wParam, lParam) != FALSE;
	result.eDelivery = ClassifyWorkerCompletionPostResult(false, bPosted);
	result.dwLastError = bPosted ? ERROR_SUCCESS : ::GetLastError();
	return result;
}

/**
 * @brief Reports whether completion has no mutable target-name buffer to work with.
 */
inline bool IsMissingCompletedNameBuffer(const void *pCompletedNameBuffer) noexcept
{
	return pCompletedNameBuffer == nullptr;
}

inline bool ShouldWarnAboutDisabledLongPathSupport(const DWORD dwMoveResult, const CString &rstrDestinationPath, const bool bWin32LongPathsEnabled)
{
	if (bWin32LongPathsEnabled || rstrDestinationPath.GetLength() < MAX_PATH)
		return false;

	switch (dwMoveResult) {
		case ERROR_DIRECTORY:
		case ERROR_FILENAME_EXCED_RANGE:
		case ERROR_INVALID_NAME:
		case ERROR_PATH_NOT_FOUND:
			return true;
		default:
			return false;
	}
}
}
