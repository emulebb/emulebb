#pragma once

#include <cstdint>

#include "DisplayRefreshSeams.h"

#define EMULEBB_TEST_HAVE_RETIRED_UPLOAD_ENTRY_PENDING_IO_WARNING_SEAM 1

inline constexpr std::uint32_t kUploadTimerSlowLoopThresholdMs = 100u;
inline constexpr std::uint64_t kRetiredUploadEntryPendingIoWarningMs = 30000u;
inline constexpr std::uint64_t kRetiredUploadEntryPendingIoWarningRepeatMs = 30000u;
inline constexpr std::uint64_t kShortFailedUploadCooldownMaxAgeMs = 30000u;
inline constexpr std::uint64_t kShortFailedUploadCooldownMaxPayloadBytes = 1024u * 1024u;

enum UploadQueueEntryAccessState
{
	uploadQueueEntryMissing,
	uploadQueueEntryRetired,
	uploadQueueEntryLive
};

/**
 * @brief Classifies an upload entry snapshot while the upload-list read lock is held.
 */
inline UploadQueueEntryAccessState ClassifyUploadQueueEntryAccess(bool bFoundInActiveList, bool bRetired, bool bHasClient)
{
	if (!bFoundInActiveList)
		return uploadQueueEntryMissing;
	if (bRetired || !bHasClient)
		return uploadQueueEntryRetired;
	return uploadQueueEntryLive;
}

/**
 * @brief Reports whether a retired upload entry can be reclaimed safely.
 */
inline bool CanReclaimUploadQueueEntry(bool bRetired, int nPendingIOBlocks)
{
	return bRetired && nPendingIOBlocks == 0;
}

inline bool ShouldWarnRetiredUploadEntryPendingIo(
	bool bRetired,
	int nPendingIOBlocks,
	std::uint64_t ullCurrentTick,
	std::uint64_t ullRetiredTick,
	std::uint64_t ullLastWarningTick,
	std::uint64_t ullWarningThresholdMs = kRetiredUploadEntryPendingIoWarningMs,
	std::uint64_t ullWarningRepeatMs = kRetiredUploadEntryPendingIoWarningRepeatMs)
{
	if (!bRetired || nPendingIOBlocks <= 0 || ullRetiredTick == 0 || ullWarningThresholdMs == 0)
		return false;

	const std::uint64_t ullRetiredAgeMs = ullCurrentTick >= ullRetiredTick ? ullCurrentTick - ullRetiredTick : 0;
	if (ullRetiredAgeMs < ullWarningThresholdMs)
		return false;

	if (ullLastWarningTick == 0)
		return true;

	const std::uint64_t ullSinceLastWarningMs = ullCurrentTick >= ullLastWarningTick ? ullCurrentTick - ullLastWarningTick : 0;
	return ullSinceLastWarningMs >= ullWarningRepeatMs;
}

inline bool PreferHigherUploadQueueScore(std::uint32_t uCandidateScore, std::uint32_t uBestScore)
{
	return uCandidateScore > uBestScore;
}

inline void UpdateUploadQueueMaxScore(std::uint32_t &uMaxScore, std::uint32_t uCandidateScore)
{
	if (uCandidateScore > uMaxScore)
		uMaxScore = uCandidateScore;
}

inline std::uint32_t AddHigherUploadQueueScoreToRank(std::uint32_t uRank, std::uint32_t uOtherScore, std::uint32_t uMyScore)
{
	return uRank + static_cast<std::uint32_t>(uOtherScore > uMyScore);
}

inline bool RejectSoftQueueCandidateByCombinedScore(bool bHardQueueLimitReached, bool bSoftQueueLimitReached, bool bHasFriendSlot, float fClientCombinedFilePrioAndCredit, float fAverageCombinedFilePrioAndCredit)
{
	return bHardQueueLimitReached
		|| (bSoftQueueLimitReached && !bHasFriendSlot && fClientCombinedFilePrioAndCredit < fAverageCombinedFilePrioAndCredit);
}

/**
 * @brief Reports whether a waiting client may influence upload-slot admission decisions.
 */
inline bool IsUploadQueueAdmissionCandidate(bool bSlowUploadCooldownActive)
{
	return !bSlowUploadCooldownActive;
}

/**
 * @brief Reports whether an IP-scoped upload retry cooldown should suppress a peer.
 */
inline bool ShouldApplyUploadRetryCooldown(bool bFriendSlot, std::uint32_t uPeerIP, std::uint64_t ullCurrentTick, std::uint64_t ullCooldownUntil)
{
	return !bFriendSlot
		&& uPeerIP != 0
		&& ullCooldownUntil > ullCurrentTick;
}

inline bool ShouldCountSlowUploadTimerLoop(std::uint32_t uDurationMs, std::uint32_t uSlowThresholdMs = kUploadTimerSlowLoopThresholdMs)
{
	return uSlowThresholdMs > 0 && uDurationMs > uSlowThresholdMs;
}

inline bool ShouldRecycleIdleBroadbandUploadSlot(
	bool bSustainedUnderfill,
	bool bWarmupComplete,
	bool bFriendSlot,
	std::uint32_t uRateBytesPerSec,
	std::uint64_t ullPayloadInBuffer,
	std::int64_t iRequestBlocks,
	std::int64_t iPendingIOBlocks,
	std::int64_t iSocketQueueEntries,
	std::uint64_t ullAccumulatedZeroUploadMs,
	std::uint64_t ullZeroGraceMs)
{
	return bSustainedUnderfill
		&& bWarmupComplete
		&& !bFriendSlot
		&& uRateBytesPerSec == 0
		&& ullPayloadInBuffer == 0
		&& iRequestBlocks == 0
		&& iPendingIOBlocks == 0
		&& iSocketQueueEntries == 0
		&& ullAccumulatedZeroUploadMs >= ullZeroGraceMs;
}

inline bool HasStalledUploadReplacementPressure(bool bHasWaitingClients, std::int64_t iUploadSlots, std::int64_t iSoftMaxUploadSlots)
{
	return bHasWaitingClients || iUploadSlots < iSoftMaxUploadSlots;
}

inline bool ShouldRecycleStalledBroadbandUploadSlot(
	bool bSustainedUnderfill,
	bool bWarmupComplete,
	bool bFriendSlot,
	bool bCanReplaceOrKeepOpenCapacity,
	std::uint32_t uRateBytesPerSec,
	std::uint64_t ullPayloadInBuffer,
	std::int64_t iRequestBlocks,
	std::int64_t iPendingIOBlocks,
	std::int64_t iSocketQueueEntries,
	std::uint64_t ullAccumulatedZeroUploadMs,
	std::uint64_t ullZeroGraceMs)
{
	return bSustainedUnderfill
		&& bWarmupComplete
		&& !bFriendSlot
		&& bCanReplaceOrKeepOpenCapacity
		&& uRateBytesPerSec == 0
		&& iPendingIOBlocks == 0
		&& (ullPayloadInBuffer > 0 || iRequestBlocks > 0 || iSocketQueueEntries > 0)
		&& ullAccumulatedZeroUploadMs >= ullZeroGraceMs;
}

inline bool ShouldCooldownShortFailedUploadSlot(
	bool bDisconnectedRemoval,
	bool bFriendSlot,
	std::uint64_t ullSessionAgeMs,
	std::uint64_t ullQueueSessionPayloadBytes,
	std::uint64_t ullMaxAgeMs = kShortFailedUploadCooldownMaxAgeMs,
	std::uint64_t ullMaxPayloadBytes = kShortFailedUploadCooldownMaxPayloadBytes)
{
	return bDisconnectedRemoval
		&& !bFriendSlot
		&& ullSessionAgeMs <= ullMaxAgeMs
		&& ullQueueSessionPayloadBytes <= ullMaxPayloadBytes;
}
