#pragma once

#include "AICHMaintenanceSeams.h"
#include "WorkerUiMessageSeams.h"

#define EMULE_TEST_HAVE_AICH_SYNC_PROGRESS_DELIVERY_ACTION 1

/**
 * @brief UI delivery outcome for AICH background progress updates.
 */
enum class EAICHSyncProgressDeliveryAction
{
	IgnoreInvalidCount,
	DropUnavailableTarget,
	Delivered,
	Failed
};

enum class EAICHSyncThreadShutdownWaitAction
{
	Finished,
	TimedOut,
	Failed
};

constexpr DWORD kAICHSyncThreadShutdownWaitMs = 5000u;
constexpr DWORD kAICHSyncHashingLockPollMs = 100u;

/**
 * @brief Reports whether AICH background hashing should wait for foreground hash work to finish.
 */
inline bool ShouldWaitForAICHSyncForegroundHashing(const AICHSyncForegroundHashState &state)
{
	const AICHSyncForegroundWaitAction action = AICHMaintenanceSeams::GetForegroundHashWaitAction(state);
	return !action.bShouldExit && action.dwSleepMilliseconds != 0u;
}

/**
 * @brief Reports whether the current AICH sync candidate should still be hashed.
 */
inline bool ShouldCreateAICHSyncHash(bool bIsClosing, bool bIsStillShared)
{
	return !bIsClosing && bIsStillShared;
}

/**
 * @brief Validates the queued AICH sync progress count before it is forwarded to the UI thread.
 */
inline bool HasValidAICHSyncProgressCount(INT_PTR nRemainingHashes)
{
	return nRemainingHashes >= 0;
}

/**
 * @brief Classifies the result of forwarding an AICH progress update to the UI thread.
 */
inline EAICHSyncProgressDeliveryAction GetAICHSyncProgressDeliveryAction(INT_PTR nRemainingHashes, EWorkerUiMessageDelivery eDelivery)
{
	if (!HasValidAICHSyncProgressCount(nRemainingHashes))
		return EAICHSyncProgressDeliveryAction::IgnoreInvalidCount;

	switch (eDelivery) {
	case EWorkerUiMessageDelivery::Delivered:
		return EAICHSyncProgressDeliveryAction::Delivered;
	case EWorkerUiMessageDelivery::InvalidWindow:
		return EAICHSyncProgressDeliveryAction::DropUnavailableTarget;
	default:
		return EAICHSyncProgressDeliveryAction::Failed;
	}
}

inline EAICHSyncThreadShutdownWaitAction GetAICHSyncThreadShutdownWaitAction(DWORD dwWait)
{
	if (dwWait == WAIT_OBJECT_0)
		return EAICHSyncThreadShutdownWaitAction::Finished;
	if (dwWait == WAIT_TIMEOUT)
		return EAICHSyncThreadShutdownWaitAction::TimedOut;
	return EAICHSyncThreadShutdownWaitAction::Failed;
}
