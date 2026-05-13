#pragma once

#include <windows.h>

#define EMULE_TESTS_HAS_SHARED_DIRECTORY_MONITOR_SEAMS 1

/**
 * @brief Testable policy helpers for shared-directory monitor shutdown and persistence ownership.
 */
namespace SharedDirectoryMonitorSeams
{
	struct StopWaitResult
	{
		bool bHadThread = false;
		DWORD dwWaitResult = WAIT_OBJECT_0;
	};

	/**
	 * @brief Reports whether the monitor thread is known to have stopped.
	 */
	inline bool DidMonitorThreadExit(const StopWaitResult &rWaitResult)
	{
		return !rWaitResult.bHadThread || rWaitResult.dwWaitResult == WAIT_OBJECT_0;
	}

	/**
	 * @brief Reports whether owned monitor resources may be released without racing the worker thread.
	 */
	inline bool ShouldReleaseMonitorResources(const StopWaitResult &rWaitResult)
	{
		return DidMonitorThreadExit(rWaitResult);
	}

	/**
	 * @brief Reports whether shutdown should log that the live monitor resources were intentionally abandoned.
	 */
	inline bool ShouldLogAbandonedMonitorResources(const StopWaitResult &rWaitResult)
	{
		return rWaitResult.bHadThread && rWaitResult.dwWaitResult != WAIT_OBJECT_0;
	}

	/**
	 * @brief Returns the atomic journal replacement flags used after writing the scratch file.
	 */
	inline DWORD GetJournalReplaceFlags()
	{
		return MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH;
	}

	/**
	 * @brief Reports whether a failed save path must delete the scratch journal file.
	 */
	inline bool ShouldDeleteJournalTempFile(bool bTempFileCreated, bool bSaveSucceeded)
	{
		return bTempFileCreated && !bSaveSucceeded;
	}
}
