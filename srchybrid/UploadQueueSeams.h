#pragma once

#include <cstdint>

#include "DisplayRefreshSeams.h"

inline constexpr std::uint32_t kUploadTimerSlowLoopThresholdMs = 100u;

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

inline bool ShouldCountSlowUploadTimerLoop(std::uint32_t uDurationMs, std::uint32_t uSlowThresholdMs = kUploadTimerSlowLoopThresholdMs)
{
	return uSlowThresholdMs > 0 && uDurationMs > uSlowThresholdMs;
}
