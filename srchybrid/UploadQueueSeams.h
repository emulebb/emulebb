#pragma once

#include <cstdint>

#include "DisplayRefreshSeams.h"

#define EMULEBB_TEST_HAVE_RETIRED_UPLOAD_ENTRY_PENDING_IO_WARNING_SEAM 1

inline constexpr std::uint32_t kUploadTimerSlowLoopThresholdMs = 100u;
inline constexpr std::uint64_t kRetiredUploadEntryPendingIoWarningMs = 30000u;
inline constexpr std::uint64_t kRetiredUploadEntryPendingIoWarningRepeatMs = 30000u;
inline constexpr std::uint64_t kShortFailedUploadCooldownMaxAgeMs = 30000u;
inline constexpr std::uint64_t kRemoteCancelledUploadCooldownMaxAgeMs = 90000u;
inline constexpr std::uint64_t kShortFailedUploadCooldownMaxPayloadBytes = 1024u * 1024u;
inline constexpr std::uint32_t kNoRequestUploadCooldownMaxSeconds = 60u;
inline constexpr std::uint32_t kProductiveNoRequestUploadCooldownMaxSeconds = 10u;
inline constexpr std::uint32_t kNoRequestUploadRecycleGraceMaxSeconds = 5u;
inline constexpr std::uint32_t kStalledUploadRecycleGraceMaxSeconds = 15u;
inline constexpr std::uint32_t kUploadChurnRetryCooldownMaxSeconds = 120u;
inline constexpr std::uint32_t kRepeatedNoRequestUploadCooldownMaxSeconds = 360u;
inline constexpr std::uint64_t kUnproductiveNoRequestCooldownProbeRemainingMs = 1000u;
inline constexpr std::uint64_t kProductiveNoRequestCooldownProbeRemainingMs = 5000u;
inline constexpr std::uint64_t kProductiveNoRequestCooldownPayloadBytes = 184320u;

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
 * @brief Reports whether the scheduler should try to open an upload slot from the waiting queue.
 */
inline bool ShouldAttemptUploadSlotAdmission(bool bAllowEmptyWaitingQueue, bool bWaitingListEmpty, bool bHasAdmissionCandidate)
{
	return bAllowEmptyWaitingQueue || (!bWaitingListEmpty && bHasAdmissionCandidate);
}

/**
 * @brief Reports whether sustained underfill may probe a cooldown-only waiting queue.
 */
inline bool ShouldProbeUploadCooldownCandidate(bool bSustainedUnderfill, std::int64_t iUploadSlots, std::int64_t iSoftMaxUploadSlots)
{
	return bSustainedUnderfill
		&& iUploadSlots < iSoftMaxUploadSlots;
}

/**
 * @brief Reports whether a no-request peer may be probed as a last-resort underfill refill.
 */
inline bool ShouldProbeUnproductiveNoRequestCooldownCandidate(
	bool bUnproductiveNoRequestCooldown,
	std::uint64_t ullCooldownRemainingMs,
	std::uint64_t ullMaxProbeRemainingMs = kUnproductiveNoRequestCooldownProbeRemainingMs)
{
	return bUnproductiveNoRequestCooldown
		&& ullCooldownRemainingMs <= ullMaxProbeRemainingMs;
}

/**
 * @brief Reports whether a drained no-request peer may be probed as a last-resort underfill refill.
 */
inline bool ShouldProbeNoRequestCooldownCandidate(
	bool bProductiveNoRequestCooldown,
	std::uint64_t ullCooldownRemainingMs,
	std::uint64_t ullMaxProductiveProbeRemainingMs = kProductiveNoRequestCooldownProbeRemainingMs,
	std::uint64_t ullMaxUnproductiveProbeRemainingMs = kUnproductiveNoRequestCooldownProbeRemainingMs)
{
	return bProductiveNoRequestCooldown
		? ullCooldownRemainingMs <= ullMaxProductiveProbeRemainingMs
		: ShouldProbeUnproductiveNoRequestCooldownCandidate(true, ullCooldownRemainingMs, ullMaxUnproductiveProbeRemainingMs);
}

/**
 * @brief Reports whether a new upload requester may bypass a queue which has no locally admissible peers.
 */
inline bool ShouldDirectAdmitBehindCooldownOnlyWaitingList(bool bWaitingListEmpty, bool bHasAdmissionCandidate)
{
	return !bWaitingListEmpty && !bHasAdmissionCandidate;
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

/**
 * @brief Returns the strongest active upload retry cooldown across retry domains.
 */
inline std::uint64_t SelectUploadRetryCooldownUntil(
	bool bFriendSlot,
	std::uint32_t uPeerIP,
	std::uint64_t ullCurrentTick,
	std::uint64_t ullRetryCooldownUntil,
	std::uint64_t ullNoRequestCooldownUntil)
{
	std::uint64_t ullSelectedCooldownUntil = 0;
	if (ShouldApplyUploadRetryCooldown(bFriendSlot, uPeerIP, ullCurrentTick, ullRetryCooldownUntil))
		ullSelectedCooldownUntil = ullRetryCooldownUntil;
	if (ShouldApplyUploadRetryCooldown(bFriendSlot, uPeerIP, ullCurrentTick, ullNoRequestCooldownUntil)
		&& ullNoRequestCooldownUntil > ullSelectedCooldownUntil)
	{
		ullSelectedCooldownUntil = ullNoRequestCooldownUntil;
	}
	return ullSelectedCooldownUntil;
}

/**
 * @brief Reports whether a failed upload admission should seed retry cooldown.
 */
inline bool ShouldCooldownFailedUploadAdmission(bool bConnectionAttemptFailed, bool bFriendSlot, std::uint32_t uPeerIP)
{
	return bConnectionAttemptFailed
		&& !bFriendSlot
		&& uPeerIP != 0;
}

inline std::uint32_t GetUploadChurnRetryCooldownSeconds(
	std::uint32_t uConfiguredCooldownSeconds,
	std::uint32_t uMaxChurnCooldownSeconds = kUploadChurnRetryCooldownMaxSeconds)
{
	return uConfiguredCooldownSeconds < uMaxChurnCooldownSeconds
		? uConfiguredCooldownSeconds
		: uMaxChurnCooldownSeconds;
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

inline bool ShouldRecycleNoRequestBroadbandUploadSlot(
	bool bSustainedUnderfill,
	bool bFriendSlot,
	std::uint32_t uRateBytesPerSec,
	std::uint64_t ullPayloadInBuffer,
	std::int64_t iRequestBlocks,
	std::int64_t iPendingIOBlocks,
	std::int64_t iSocketQueueEntries,
	std::uint64_t ullSessionAgeMs,
	bool bHasAcceptedRequest,
	std::uint64_t ullLastAcceptedRequestAgeMs,
	std::uint64_t ullNoRequestGraceMs)
{
	return bSustainedUnderfill
		&& !bFriendSlot
		&& uRateBytesPerSec == 0
		&& ullPayloadInBuffer == 0
		&& iRequestBlocks == 0
		&& iPendingIOBlocks == 0
		&& iSocketQueueEntries == 0
		&& ullSessionAgeMs >= ullNoRequestGraceMs
		&& (!bHasAcceptedRequest || ullLastAcceptedRequestAgeMs >= ullNoRequestGraceMs);
}

inline bool ShouldCooldownNoRequestUploadRecycle(bool bFriendSlot)
{
	return !bFriendSlot;
}

inline std::uint64_t GetNoRequestUploadRecycleGraceMs(
	std::uint32_t uConfiguredGraceSeconds,
	std::uint32_t uMaxNoRequestGraceSeconds = kNoRequestUploadRecycleGraceMaxSeconds)
{
	return static_cast<std::uint64_t>(uConfiguredGraceSeconds < uMaxNoRequestGraceSeconds
		? uConfiguredGraceSeconds
		: uMaxNoRequestGraceSeconds) * 1000ui64;
}

inline std::uint64_t GetStalledUploadRecycleGraceMs(
	std::uint32_t uConfiguredGraceSeconds,
	std::uint32_t uMaxStalledGraceSeconds = kStalledUploadRecycleGraceMaxSeconds)
{
	return static_cast<std::uint64_t>(uConfiguredGraceSeconds < uMaxStalledGraceSeconds
		? uConfiguredGraceSeconds
		: uMaxStalledGraceSeconds) * 1000ui64;
}

inline bool IsProductiveNoRequestUploadRecycle(std::uint64_t ullQueueSessionPayloadBytes, std::uint64_t ullProductivePayloadBytes = kProductiveNoRequestCooldownPayloadBytes)
{
	return ullQueueSessionPayloadBytes >= ullProductivePayloadBytes;
}

/**
 * @brief Returns the payload floor for treating a drained no-request recycle as productive.
 */
inline std::uint64_t GetProductiveNoRequestCooldownPayloadBytes(
	std::uint32_t uTargetPerSlotBytesPerSec,
	std::uint64_t ullBasePayloadBytes = kProductiveNoRequestCooldownPayloadBytes)
{
	return uTargetPerSlotBytesPerSec > ullBasePayloadBytes
		? uTargetPerSlotBytesPerSec
		: ullBasePayloadBytes;
}

/**
 * @brief Returns the bounded cooldown used after a no-request upload recycle.
 */
inline std::uint32_t GetNoRequestUploadRetryCooldownSeconds(
	std::uint32_t uConfiguredCooldownSeconds,
	bool bRecentNoRequestRecycle,
	bool bProductiveNoRequestRecycle = false,
	std::uint32_t uMaxNoRequestCooldownSeconds = kNoRequestUploadCooldownMaxSeconds,
	std::uint32_t uMaxProductiveNoRequestCooldownSeconds = kProductiveNoRequestUploadCooldownMaxSeconds,
	std::uint32_t uMaxRepeatedNoRequestCooldownSeconds = kRepeatedNoRequestUploadCooldownMaxSeconds)
{
	if (bProductiveNoRequestRecycle)
		return uConfiguredCooldownSeconds < uMaxProductiveNoRequestCooldownSeconds
			? uConfiguredCooldownSeconds
			: uMaxProductiveNoRequestCooldownSeconds;
	if (bRecentNoRequestRecycle && !bProductiveNoRequestRecycle)
		return uConfiguredCooldownSeconds < uMaxRepeatedNoRequestCooldownSeconds
			? uConfiguredCooldownSeconds
			: uMaxRepeatedNoRequestCooldownSeconds;
	return uConfiguredCooldownSeconds < uMaxNoRequestCooldownSeconds
		? uConfiguredCooldownSeconds
		: uMaxNoRequestCooldownSeconds;
}

/**
 * @brief Returns how long a no-request recycle should remain visible for repeat detection.
 */
inline std::uint64_t GetNoRequestUploadRetryTrackSeconds(
	std::uint32_t uCooldownSeconds,
	std::uint32_t uConfiguredCooldownSeconds)
{
	return static_cast<std::uint64_t>(uCooldownSeconds) + uConfiguredCooldownSeconds;
}

inline bool ShouldClearUploadRetryCooldownOnQueuedRequest(
	bool bOnUploadQueue,
	bool bSlowUploadCooldownActive,
	bool bRequestFileKnown,
	bool bRequestRangeValid)
{
	return bOnUploadQueue
		&& bSlowUploadCooldownActive
		&& bRequestFileKnown
		&& bRequestRangeValid;
}

inline bool ShouldAttemptUploadRetryCooldownClearOnQueuedRequest(
	bool bOnUploadQueue,
	bool bRequestFileKnown,
	bool bRequestRangeValid)
{
	return bOnUploadQueue
		&& bRequestFileKnown
		&& bRequestRangeValid;
}

inline bool ShouldAllowNoRequestCooldownClear(
	bool bNoRequestCooldownTracked,
	bool bQueuedRequestClearAlreadyUsed)
{
	return !bNoRequestCooldownTracked
		|| !bQueuedRequestClearAlreadyUsed;
}

/**
 * @brief Reports whether one valid queued request may prove renewed demand from an active no-request cooldown.
 */
inline bool ShouldClearActiveNoRequestCooldownOnQueuedRequest(
	bool bHadNoRequestCooldown,
	bool bClearedProductiveNoRequestCooldown,
	bool bSustainedUnderfill,
	std::int64_t iUploadSlots,
	std::int64_t iSoftMaxUploadSlots)
{
	return bHadNoRequestCooldown
		&& !bClearedProductiveNoRequestCooldown
		&& bSustainedUnderfill
		&& iUploadSlots < iSoftMaxUploadSlots;
}

/**
 * @brief Reports whether an active no-request cooldown still blocks queued-request retry clears.
 */
inline bool ShouldBlockQueuedRequestRetryClearForActiveNoRequest(
	bool bHadNoRequestCooldown,
	bool bClearedProductiveNoRequestCooldown,
	bool bClearedUnderfilledNoRequestCooldown = false)
{
	return bHadNoRequestCooldown && !bClearedProductiveNoRequestCooldown && !bClearedUnderfilledNoRequestCooldown;
}

inline bool ShouldAllowUploadRetryCooldownClear(bool bRetryCooldownTracked, bool bQueuedRequestClearAlreadyUsed)
{
	return !bRetryCooldownTracked || !bQueuedRequestClearAlreadyUsed;
}

inline bool ShouldAdmitQueuedBlockRequestToUploadSlot(
	bool bQueuedRequestCooldownCleared,
	bool bOnUploadQueue,
	bool bAlreadyUploading,
	bool bCanOpenUploadSlot)
{
	return bQueuedRequestCooldownCleared
		&& bOnUploadQueue
		&& !bAlreadyUploading
		&& bCanOpenUploadSlot;
}

enum QueuedBlockRequestAdmissionResult
{
	queuedBlockRequestAdmitted,
	queuedBlockRequestCooldownNotCleared,
	queuedBlockRequestNotOnQueue,
	queuedBlockRequestAlreadyUploading,
	queuedBlockRequestCapFull,
	queuedBlockRequestAdmissionDeferred,
	queuedBlockRequestDirectAddFailed
};

inline QueuedBlockRequestAdmissionResult ClassifyQueuedBlockRequestAdmission(
	bool bQueuedRequestCooldownCleared,
	bool bOnUploadQueue,
	bool bAlreadyUploading,
	bool bCanOpenUploadSlot,
	bool bAdmissionGateOpen,
	bool bDirectAddSucceeded)
{
	if (!bQueuedRequestCooldownCleared)
		return queuedBlockRequestCooldownNotCleared;
	if (!bOnUploadQueue)
		return queuedBlockRequestNotOnQueue;
	if (bAlreadyUploading)
		return queuedBlockRequestAlreadyUploading;
	if (!bCanOpenUploadSlot)
		return queuedBlockRequestCapFull;
	if (!bAdmissionGateOpen)
		return queuedBlockRequestAdmissionDeferred;
	return bDirectAddSucceeded ? queuedBlockRequestAdmitted : queuedBlockRequestDirectAddFailed;
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
	bool bRemoteCancelledRemoval,
	bool bFriendSlot,
	std::uint64_t ullSessionAgeMs,
	std::uint64_t ullQueueSessionPayloadBytes,
	std::uint64_t ullMaxAgeMs = kShortFailedUploadCooldownMaxAgeMs,
	std::uint64_t ullRemoteCancelledMaxAgeMs = kRemoteCancelledUploadCooldownMaxAgeMs,
	std::uint64_t ullMaxPayloadBytes = kShortFailedUploadCooldownMaxPayloadBytes)
{
	return bDisconnectedRemoval
		&& !bFriendSlot
		&& (ullSessionAgeMs <= ullMaxAgeMs
			|| (bRemoteCancelledRemoval && ullSessionAgeMs <= ullRemoteCancelledMaxAgeMs))
		&& ullQueueSessionPayloadBytes <= ullMaxPayloadBytes;
}

inline bool ShouldCooldownNoSocketUploadSlot(
	bool bNoSocketRemoval,
	bool bFriendSlot,
	std::uint32_t uPeerIP,
	std::uint64_t ullSessionAgeMs,
	std::uint64_t ullQueueSessionPayloadBytes,
	std::uint64_t ullMaxAgeMs = kShortFailedUploadCooldownMaxAgeMs,
	std::uint64_t ullMaxPayloadBytes = kShortFailedUploadCooldownMaxPayloadBytes)
{
	return bNoSocketRemoval
		&& !bFriendSlot
		&& uPeerIP != 0
		&& ullSessionAgeMs <= ullMaxAgeMs
		&& ullQueueSessionPayloadBytes <= ullMaxPayloadBytes;
}

inline bool ShouldRotateBroadbandLimitedUploadSession(
	bool bNeedsReplacement,
	bool bBroadbandUnderfilled,
	std::uint32_t uUploadRateBytesPerSec,
	std::uint32_t uProductiveRateThresholdBytesPerSec)
{
	return bNeedsReplacement
		&& (!bBroadbandUnderfilled || uUploadRateBytesPerSec < uProductiveRateThresholdBytesPerSec);
}
