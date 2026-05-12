#pragma once

#include <cstddef>
#include <limits>

#include "types.h"

namespace PartFileEndgameSeams
{
/**
 * @brief First completion percentage where download scheduling starts favoring proven-fast peers.
 */
constexpr uint64 kLateDownloadPercentTimes1000 = 90000u;

/**
 * @brief True endgame percentage used to protect the final missing ranges from slow peers.
 */
constexpr uint64 kEndgamePercentTimes1000 = 99900u;

/**
 * @brief Time-to-completion threshold that also enables true endgame behavior.
 */
constexpr uint64 kEndgameSeconds = 30u;

/**
 * @brief Number of seconds a slow peer may reserve from its current rate in late/endgame mode.
 */
constexpr uint64 kSlowPeerReservationSeconds = 10u;

/**
 * @brief Minimum capped reservation for slow late/endgame peers.
 */
constexpr uint64 kMinCappedReservationBytes = 16u * 1024u;

/**
 * @brief Small tail threshold below which the current gap is kept whole.
 */
constexpr uint64 kMinTrailingFragmentBytes = 3u * 1024u;

/**
 * @brief Brief endgame grace period before slow peers may receive fallback work.
 */
constexpr uint64 kFastPeerWaitMs = 15u * 1000u;

/**
 * @brief Relative speed factor for treating another peer as meaningfully faster.
 */
constexpr uint32 kMeaningfullyFasterFactor = 5u;

/**
 * @brief Per-file cooldown after canceling a slow endgame owner for a faster peer.
 */
constexpr uint64 kEndgameStealCooldownMs = 60u * 1000u;

enum class ReservationAction : uint8
{
	AllowFull,
	AllowCapped,
	Withhold
};

struct ReservationDecision
{
	ReservationAction action;
	uint64 maxBytes;
};

/**
 * @brief Returns true when the completed byte ratio reaches a percent*1000 threshold.
 */
inline bool IsAtLeastPercent(const uint64 completedBytes, const uint64 fileSize, const uint64 percentTimes1000)
{
	if (fileSize == 0)
		return false;

	if (completedBytes >= fileSize)
		return true;

	const uint64 scale = 100000u;
	const uint64 scaledWhole = (fileSize / scale) * percentTimes1000;
	const uint64 scaledRemainder = ((fileSize % scale) * percentTimes1000 + scale - 1u) / scale;
	return completedBytes >= scaledWhole + scaledRemainder;
}

/**
 * @brief Returns true once the file has reached the speed-first late-download band.
 */
inline bool IsLateDownload(const uint64 completedBytes, const uint64 fileSize)
{
	return IsAtLeastPercent(completedBytes, fileSize, kLateDownloadPercentTimes1000);
}

/**
 * @brief Returns true when either percentage or remaining-time estimates show true endgame.
 */
inline bool IsEndgame(const uint64 completedBytes, const uint64 fileSize, const uint64 remainingBytes,
	const uint32 fileDatarate, const size_t requestedBlockCount)
{
	(void)requestedBlockCount;
	if (IsAtLeastPercent(completedBytes, fileSize, kEndgamePercentTimes1000))
		return true;
	if (fileDatarate == 0)
		return false;

	return remainingBytes <= static_cast<uint64>(fileDatarate) * kEndgameSeconds;
}

/**
 * @brief Checks whether a candidate peer is fast enough to take work from the current peer.
 */
inline bool IsMeaningfullyFasterPeer(const uint32 currentPeerRate, const uint32 candidatePeerRate)
{
	if (candidatePeerRate == 0)
		return false;
	if (currentPeerRate == 0)
		return true;

	return candidatePeerRate / kMeaningfullyFasterFactor >= currentPeerRate;
}

/**
 * @brief Returns true when the slow-owner steal cooldown has elapsed.
 */
inline bool IsEndgameStealCooldownExpired(const uint64 nowTick, const uint64 cooldownUntilTick)
{
	return cooldownUntilTick == 0 || nowTick >= cooldownUntilTick;
}

/**
 * @brief Returns true when a slow final-block owner should be canceled for a faster active peer.
 */
inline bool ShouldStealEndgameReservation(const bool endgame, const bool fasterPeerCanServePart,
	const uint32 slowPeerDatarate, const uint32 fastPeerDatarate, const uint64 nowTick,
	const uint64 cooldownUntilTick, const uint64 transferredBytes)
{
	(void)transferredBytes;
	return endgame
		&& fasterPeerCanServePart
		&& IsEndgameStealCooldownExpired(nowTick, cooldownUntilTick)
		&& IsMeaningfullyFasterPeer(slowPeerDatarate, fastPeerDatarate);
}

/**
 * @brief Calculates the byte cap for a slow late/endgame peer.
 */
inline uint64 CalculateCappedReservationBytes(const uint32 peerDatarate, const uint64 fullBlockBytes)
{
	if (fullBlockBytes <= kMinCappedReservationBytes)
		return fullBlockBytes;

	uint64 cappedBytes = static_cast<uint64>(peerDatarate) * kSlowPeerReservationSeconds;
	if (cappedBytes < kMinCappedReservationBytes)
		cappedBytes = kMinCappedReservationBytes;
	if (cappedBytes > fullBlockBytes)
		cappedBytes = fullBlockBytes;
	return cappedBytes;
}

/**
 * @brief Applies a byte cap to a candidate interval without creating a tiny trailing fragment.
 */
inline uint64 ClampReservationEnd(const uint64 startOffset, const uint64 candidateEndOffset, const uint64 maxBytes)
{
	if (candidateEndOffset < startOffset || maxBytes == 0)
		return candidateEndOffset;

	const uint64 spanBytes = candidateEndOffset - startOffset + 1u;
	if (maxBytes >= spanBytes || spanBytes <= maxBytes + kMinTrailingFragmentBytes)
		return candidateEndOffset;

	return startOffset + maxBytes - 1u;
}

/**
 * @brief Chooses whether a peer may reserve a block in late/endgame mode.
 */
inline ReservationDecision DecideReservation(const bool lateDownload, const bool endgame,
	const bool fasterPeerCanServePart, const bool fastPeerWaitExpired, const uint32 peerDatarate,
	const uint64 fullBlockBytes)
{
	if (!lateDownload || !fasterPeerCanServePart)
		return ReservationDecision{ReservationAction::AllowFull, fullBlockBytes};

	if (endgame && !fastPeerWaitExpired)
		return ReservationDecision{ReservationAction::Withhold, 0u};

	return ReservationDecision{
		ReservationAction::AllowCapped,
		CalculateCappedReservationBytes(peerDatarate, fullBlockBytes)
	};
}
}
