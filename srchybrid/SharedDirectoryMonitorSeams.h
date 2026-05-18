#pragma once

#include <windows.h>

#define EMULE_TESTS_HAS_SHARED_DIRECTORY_MONITOR_SEAMS 1

/**
 * @brief Testable policy helpers for shared-directory monitor shutdown and persistence ownership.
 */
namespace SharedDirectoryMonitorSeams
{
	constexpr DWORD kMonitorShutdownWaitMs = 30000;

	enum class EMonitorWaitAction
	{
		Stop,
		Wake,
		Watcher,
		Ignore,
		Failed
	};

	struct StopWaitResult
	{
		bool bHadThread = false;
		DWORD dwWaitResult = WAIT_OBJECT_0;
	};

	struct MonitorWaitResult
	{
		EMonitorWaitAction eAction = EMonitorWaitAction::Ignore;
		size_t uWatcherIndex = 0;
		bool bDirectoryEvent = false;
	};

	/**
	 * @brief Returns the maximum root watcher count that still fits in WaitForMultipleObjects.
	 */
	inline size_t GetMonitorWatcherCapacity()
	{
		return (MAXIMUM_WAIT_OBJECTS - 2u) / 2u;
	}

	/**
	 * @brief Reports whether the stop/wake events are both owned before the monitor thread starts.
	 */
	inline bool AreStartupEventsReady(HANDLE hStopEvent, HANDLE hWakeEvent)
	{
		return hStopEvent != NULL && hWakeEvent != NULL;
	}

	/**
	 * @brief Reports whether a suspended monitor thread was resumed successfully.
	 */
	inline bool DidResumeMonitorThread(DWORD dwResumeResult)
	{
		return dwResumeResult != static_cast<DWORD>(-1);
	}

	/**
	 * @brief Reports whether the wait-handle list is valid for the monitor thread.
	 */
	inline bool CanWaitForMonitorHandles(size_t uHandleCount)
	{
		return uHandleCount >= 2 && uHandleCount <= MAXIMUM_WAIT_OBJECTS;
	}

	/**
	 * @brief Classifies a WaitForMultipleObjects result from the shared-directory monitor loop.
	 */
	inline MonitorWaitResult ClassifyMonitorWaitResult(DWORD dwWait, size_t uHandleCount)
	{
		MonitorWaitResult result;
		if (!CanWaitForMonitorHandles(uHandleCount) || dwWait == WAIT_FAILED) {
			result.eAction = EMonitorWaitAction::Failed;
			return result;
		}

		if (dwWait == WAIT_OBJECT_0) {
			result.eAction = EMonitorWaitAction::Stop;
			return result;
		}
		if (dwWait == WAIT_OBJECT_0 + 1) {
			result.eAction = EMonitorWaitAction::Wake;
			return result;
		}
		if (dwWait < WAIT_OBJECT_0 + 2 || dwWait >= WAIT_OBJECT_0 + static_cast<DWORD>(uHandleCount)) {
			result.eAction = EMonitorWaitAction::Ignore;
			return result;
		}

		const DWORD dwTriggeredHandleIndex = dwWait - WAIT_OBJECT_0 - 2;
		result.eAction = EMonitorWaitAction::Watcher;
		result.uWatcherIndex = static_cast<size_t>(dwTriggeredHandleIndex / 2u);
		result.bDirectoryEvent = (dwTriggeredHandleIndex % 2u) != 0u;
		return result;
	}

	/**
	 * @brief Closes and clears one owned monitor event handle.
	 */
	inline void CloseMonitorEvent(HANDLE& rhEvent)
	{
		if (rhEvent != NULL) {
			(void)::CloseHandle(rhEvent);
			rhEvent = NULL;
		}
	}

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
