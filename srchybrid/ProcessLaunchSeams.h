#pragma once

#include <Windows.h>

namespace ProcessLaunchSeams
{
constexpr DWORD kElevatedPowerShellActionTimeoutMs = 30u * 60u * 1000u;
constexpr DWORD kArchiveRecoveryPreviewTimeoutMs = 30u * 60u * 1000u;
constexpr DWORD kTimedOutProcessTerminateWaitMs = 2u * 1000u;

enum class EProcessWaitResult
{
	Completed,
	TimedOut,
	Failed,
	Other
};

/**
 * @brief Classifies a Win32 process wait result for bounded external process launches.
 */
inline EProcessWaitResult ClassifyProcessWaitResult(DWORD dwWaitResult)
{
	switch (dwWaitResult) {
	case WAIT_OBJECT_0:
		return EProcessWaitResult::Completed;
	case WAIT_TIMEOUT:
		return EProcessWaitResult::TimedOut;
	case WAIT_FAILED:
		return EProcessWaitResult::Failed;
	default:
		return EProcessWaitResult::Other;
	}
}
}
