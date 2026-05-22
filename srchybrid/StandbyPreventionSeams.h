#pragma once

#include <windows.h>

namespace StandbyPreventionSeams
{
/**
 * @brief Returns true when eMuleBB should hold a Windows system-sleep assertion.
 */
inline bool ShouldPreventSystemSleep(bool bPreventStandbyEnabled, bool bIsConnected, unsigned int uUploadQueueLength, unsigned int uDownloadDatarate)
{
	return bPreventStandbyEnabled && (bIsConnected || uUploadQueueLength > 0 || uDownloadDatarate > 0);
}

/**
 * @brief Flags used while transfers or connections need the machine to stay awake.
 */
inline EXECUTION_STATE GetPreventSystemSleepFlags()
{
	return static_cast<EXECUTION_STATE>(ES_SYSTEM_REQUIRED | ES_CONTINUOUS);
}

/**
 * @brief Flags used to release a prior continuous execution-state assertion.
 */
inline EXECUTION_STATE GetReleaseSystemSleepFlags()
{
	return static_cast<EXECUTION_STATE>(ES_CONTINUOUS);
}
}
