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
 * @brief Public lifecycle snapshot derived from the native app state and
 * startup readiness milestones.
 */
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

/**
 * @brief Returns the stable lowercase compact REST lifecycle token for one
 * native application state.
 */
inline const char *GetAppLifecycleStateName(const AppState eAppState)
{
	switch (eAppState) {
	case APP_STATE_STARTING:
		return "starting";
	case APP_STATE_RUNNING:
	case APP_STATE_ASKCLOSE:
		return "running";
	case APP_STATE_SHUTTINGDOWN:
		return "shuttingdown";
	case APP_STATE_DONE:
		return "done";
	default:
		return "done";
	}
}

/**
 * @brief Builds the public lifecycle snapshot used by REST policy and
 * diagnostics. The exit confirmation dialog deliberately remains public
 * running state.
 */
inline SAppLifecycleStatus BuildAppLifecycleStatus(
	const AppState eAppState,
	const bool bStartupComplete,
	const bool bSharedFilesReady)
{
	const bool bRunning = IsAppStateRunning(eAppState);
	const bool bClosing = IsAppStateClosing(eAppState);
	return SAppLifecycleStatus{
		GetAppLifecycleStateName(eAppState),
		bStartupComplete,
		bRunning,
		bSharedFilesReady,
		!bClosing,
		bRunning,
		bClosing
	};
}

/**
 * @brief Reports whether one REST command should be rejected for the current
 * lifecycle and HTTP method category.
 */
inline bool ShouldRejectRestCommandForLifecycle(const SAppLifecycleStatus &rLifecycle, const bool bMutation)
{
	return !rLifecycle.bAcceptingRest || (bMutation && !rLifecycle.bAcceptingMutations);
}
