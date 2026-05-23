#pragma once

#include <cstdint>

enum AppState : uint8_t
{
	APP_STATE_STARTING = 0,	// initialization phase
	APP_STATE_RUNNING,
	APP_STATE_ASKCLOSE,		// exit dialog is on screen
	APP_STATE_SHUTTINGDOWN,
	APP_STATE_DONE			// shutdown has completed
};

/**
 * @brief Reports whether the current application state should still be treated as running.
 */
inline bool IsAppStateRunning(const AppState eAppState)
{
	return eAppState == APP_STATE_RUNNING || eAppState == APP_STATE_ASKCLOSE;
}

/**
 * @brief Reports whether the current application state has entered shutdown or finished closing.
 */
inline bool IsAppStateClosing(const AppState eAppState)
{
	return eAppState == APP_STATE_SHUTTINGDOWN || eAppState == APP_STATE_DONE;
}

struct SAppLifecycleStatus
{
	const char *pszState;
	bool bStartupComplete;
	bool bCoreReady;
	bool bSharedFilesReady;
	bool bAcceptingRest;
	bool bAcceptingMutations;
	bool bShutdownInProgress;
};

/**
 * @brief Builds the externally visible app lifecycle snapshot from app-state flags.
 */
inline SAppLifecycleStatus BuildAppLifecycleStatus(
	const AppState eAppState,
	const bool bStartupComplete,
	const bool bSharedFilesReady)
{
	const bool bClosing = IsAppStateClosing(eAppState);
	const bool bCoreReady = IsAppStateRunning(eAppState) || bClosing;
	const bool bDone = eAppState == APP_STATE_DONE;
	return SAppLifecycleStatus{
		bDone ? "done" : (bClosing ? "shuttingdown" : (bStartupComplete ? "running" : "starting")),
		bStartupComplete,
		bCoreReady,
		bSharedFilesReady,
		!bDone,
		!bClosing && bStartupComplete && bSharedFilesReady,
		bClosing,
	};
}

/**
 * @brief Reports whether a REST command should be rejected for the current lifecycle state.
 */
inline bool ShouldRejectRestCommandForLifecycle(const SAppLifecycleStatus &rLifecycle, const bool bMutation)
{
	return !rLifecycle.bAcceptingRest || (bMutation && !rLifecycle.bAcceptingMutations);
}
