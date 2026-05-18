#pragma once

#include <atlstr.h>
#include <windows.h>

#include "LongPathSeams.h"

namespace AtomicFileSaveSeams
{
/**
 * @brief Builds the conventional sibling temp path for an atomic file save.
 */
inline CString BuildDefaultTempPath(const CString &rstrTargetPath)
{
	return rstrTargetPath + _T(".tmp");
}

/**
 * @brief Returns the MoveFileEx flags used when installing a completed temp file.
 */
inline DWORD GetReplaceFlags(const bool bWriteThrough) noexcept
{
	return MOVEFILE_REPLACE_EXISTING | (bWriteThrough ? MOVEFILE_WRITE_THROUGH : 0u);
}

/**
 * @brief Reports whether a temp file should be removed after an atomic save attempt.
 */
inline bool ShouldDeleteTempFileAfterSaveAttempt(const bool bTempFileCreated, const bool bSaveSucceeded) noexcept
{
	return bTempFileCreated && !bSaveSucceeded;
}

/**
 * @brief Replaces the target with a completed temp file and reports the Win32 failure code.
 */
inline bool TryReplaceTempFile(const CString &rstrTempPath, const CString &rstrTargetPath, const DWORD dwReplaceFlags, DWORD *pdwLastError = NULL)
{
	if (pdwLastError != NULL)
		*pdwLastError = ERROR_SUCCESS;

	if (rstrTempPath.IsEmpty() || rstrTargetPath.IsEmpty()) {
		if (pdwLastError != NULL)
			*pdwLastError = ERROR_INVALID_PARAMETER;
		return false;
	}

	if (LongPathSeams::MoveFileEx(rstrTempPath, rstrTargetPath, dwReplaceFlags))
		return true;

	if (pdwLastError != NULL)
		*pdwLastError = ::GetLastError();
	return false;
}

/**
 * @brief Deletes a failed temp file when policy says the worker still owns it.
 */
inline bool DeleteTempFileAfterSaveAttemptIfNeeded(const CString &rstrTempPath, const bool bTempFileCreated, const bool bSaveSucceeded)
{
	return !ShouldDeleteTempFileAfterSaveAttempt(bTempFileCreated, bSaveSucceeded)
		|| LongPathSeams::DeleteFileIfExists(rstrTempPath) != FALSE;
}
}
