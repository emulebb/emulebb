#pragma once

#include <cstdint>

namespace PartFilePauseResumeSeams
{
enum class RuntimeStatus : uint8_t
{
	Active = 0,
	Error,
	Completing,
	Complete
};

enum class VisibleStatus : uint8_t
{
	Active = 0,
	Paused,
	Insufficient,
	Error,
	Completing,
	Complete
};

struct State
{
	RuntimeStatus Status;
	bool Paused;
	bool Insufficient;
	bool Stopped;
};

struct TransitionResult
{
	State NextState;
	bool ShouldNotifyStatusChange;
	bool ShouldSavePartFile;
	bool UsesCompletionErrorPath;
	bool IsNoOp;
};

inline bool IsPauseBlockedByCompletion(const RuntimeStatus eStatus)
{
	return eStatus == RuntimeStatus::Completing || eStatus == RuntimeStatus::Complete;
}

inline VisibleStatus ResolveVisibleStatus(const RuntimeStatus eStatus, const bool bPaused, const bool bInsufficient, const bool bIgnorePause = false)
{
	if ((!bPaused && !bInsufficient) || bIgnorePause) {
		switch (eStatus) {
		case RuntimeStatus::Error:
			return VisibleStatus::Error;
		case RuntimeStatus::Completing:
			return VisibleStatus::Completing;
		case RuntimeStatus::Complete:
			return VisibleStatus::Complete;
		default:
			return VisibleStatus::Active;
		}
	}

	if (eStatus == RuntimeStatus::Error)
		return VisibleStatus::Error;
	if (eStatus == RuntimeStatus::Completing)
		return VisibleStatus::Completing;
	if (eStatus == RuntimeStatus::Complete)
		return VisibleStatus::Complete;
	return bPaused ? VisibleStatus::Paused : VisibleStatus::Insufficient;
}

inline TransitionResult ApplyPauseTransition(const State &rState, const bool bInsufficientPause)
{
	TransitionResult result = { rState, false, false, false, true };
	if ((bInsufficientPause && rState.Insufficient) || IsPauseBlockedByCompletion(rState.Status))
		return result;

	result.NextState = rState;
	if (!bInsufficientPause)
		result.NextState.Paused = true;
	result.NextState.Insufficient = bInsufficientPause;
	result.ShouldNotifyStatusChange = true;
	result.ShouldSavePartFile = !bInsufficientPause;
	result.IsNoOp = false;
	return result;
}

inline bool UsesCompletionErrorResumePath(const RuntimeStatus eStatus, const bool bCompletionError)
{
	return eStatus == RuntimeStatus::Error && bCompletionError;
}

inline TransitionResult ApplyNormalResumeTransition(const State &rState)
{
	TransitionResult result = { rState, false, false, false, true };
	if (IsPauseBlockedByCompletion(rState.Status))
		return result;

	result.NextState = rState;
	result.NextState.Paused = false;
	result.NextState.Stopped = false;
	result.ShouldNotifyStatusChange = true;
	result.ShouldSavePartFile = true;
	result.IsNoOp = false;
	return result;
}

inline TransitionResult ApplyInsufficientResumeTransition(const State &rState)
{
	TransitionResult result = { rState, false, false, false, true };
	if (IsPauseBlockedByCompletion(rState.Status) || !rState.Insufficient)
		return result;

	result.NextState = rState;
	result.NextState.Insufficient = false;
	result.IsNoOp = false;
	return result;
}

inline TransitionResult ApplyStopTransition(const State &rState)
{
	TransitionResult result = { rState, !IsPauseBlockedByCompletion(rState.Status), !IsPauseBlockedByCompletion(rState.Status), false, false };
	result.NextState = rState;
	result.NextState.Paused = true;
	result.NextState.Insufficient = false;
	result.NextState.Stopped = true;
	return result;
}
}

#define EMULEBB_TEST_HAVE_PART_FILE_PAUSE_RESUME_SEAMS 1
