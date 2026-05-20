#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <tchar.h>
#include <vector>

#include "PartFilePersistenceSeams.h"

namespace DownloadQueueDiskSpaceSeams
{
enum class FileDiskSpaceStatus : uint8_t
{
	Active = 0,
	Paused,
	Insufficient,
	Error,
	Completing,
	Complete
};

struct VolumeKey
{
	int DriveNumber;
	std::wstring ShareName;
};

struct FileDiskSpaceState
{
	FileDiskSpaceStatus Status;
	VolumeKey TempVolumeKey;
	bool IsNormalFile;
	uint64_t NeededBytes;
};

struct VolumeResumeBudget
{
	VolumeKey TempVolumeKey;
	uint64_t FreeBytes;
	uint64_t ResumeHeadroomBytes;
};

/**
 * Lightweight, testable view of one protected volume's free-space guard state.
 */
struct ProtectedVolumeSpaceState
{
	uint64_t FreeBytes;
	uint64_t RequiredBytes;
};

/**
 * Describes how the queue should react to the current protected-volume snapshot.
 */
struct ProtectedDiskSpaceBreachAction
{
	bool ShouldClearBlock;
	bool ShouldLogBreach;
	bool ShouldStopDownloads;
	bool ShouldRememberBlock;
};

using RequiredFreeSpacePathCacheKey = std::basic_string<TCHAR>;
using VolumeIdentity = std::basic_string<TCHAR>;

struct ProtectedVolumeAvailability
{
	VolumeIdentity VolumeId;
	int64_t AvailableBytes;
};

struct TempDirVolumeCandidate
{
	VolumeIdentity VolumeId;
	bool IsFatVolume;
};

struct TempDirPlacementDecision
{
	bool HasSelection;
	std::size_t CandidateIndex;
};

inline bool IsPathCacheSeparator(const TCHAR ch)
{
	return ch == _T('\\') || ch == _T('/');
}

inline bool IsDriveRootPathCacheKey(const RequiredFreeSpacePathCacheKey &rKey)
{
	return rKey.size() == 3u && rKey[1] == _T(':') && IsPathCacheSeparator(rKey[2]);
}

inline RequiredFreeSpacePathCacheKey NormalizeRequiredFreeSpacePathCacheKey(RequiredFreeSpacePathCacheKey key)
{
	for (TCHAR &rch : key) {
		if (rch == _T('/'))
			rch = _T('\\');
		rch = static_cast<TCHAR>(_totlower(rch));
	}

	while (!key.empty() && IsPathCacheSeparator(key.back()) && !IsDriveRootPathCacheKey(key))
		key.pop_back();
	return key;
}

inline bool IsSameVolumeKey(const VolumeKey &rLeft, const VolumeKey &rRight)
{
	return rLeft.DriveNumber == rRight.DriveNumber
		&& (rLeft.DriveNumber >= 0 || rLeft.ShareName == rRight.ShareName);
}

inline bool IsProtectedVolumeBreached(const ProtectedVolumeSpaceState &rState)
{
	return rState.FreeBytes < rState.RequiredBytes;
}

inline uint64_t GetUnresolvedProtectedVolumeFreeBytes()
{
	return 0u;
}

inline bool ShouldReserveProtectedVolumeSnapshotDemand(const bool bSnapshotValid, const bool bNotEnoughSpaceLeftSnapshot, const uint64_t nFileSize)
{
	return bSnapshotValid && !bNotEnoughSpaceLeftSnapshot && nFileSize > 0u;
}

inline bool ShouldInvalidateRequiredFreeSpacePathCacheAfterReservation(const bool bReservedDemand)
{
	return bReservedDemand;
}

inline bool WasProtectedVolumeSnapshotDemandFullyReserved(const bool bShouldReserveDemand, const bool bReservedTempDemand, const bool bNeedsIncomingDemand, const bool bReservedIncomingDemand)
{
	return !bShouldReserveDemand || (bReservedTempDemand && (!bNeedsIncomingDemand || bReservedIncomingDemand));
}

inline int64_t CalculateProtectedVolumeAvailableBytes(const uint64_t nFreeBytes, const uint64_t nRequiredBytes)
{
	if (nFreeBytes >= nRequiredBytes) {
		const uint64_t nAvailableBytes = nFreeBytes - nRequiredBytes;
		return nAvailableBytes > static_cast<uint64_t>((std::numeric_limits<int64_t>::max)())
			? (std::numeric_limits<int64_t>::max)()
			: static_cast<int64_t>(nAvailableBytes);
	}

	const uint64_t nDeficitBytes = nRequiredBytes - nFreeBytes;
	return nDeficitBytes > static_cast<uint64_t>((std::numeric_limits<int64_t>::max)())
		? (std::numeric_limits<int64_t>::min)()
		: -static_cast<int64_t>(nDeficitBytes);
}

inline int64_t FindProtectedVolumeAvailableBytes(const ProtectedVolumeAvailability *pVolumes, const std::size_t nVolumeCount, const VolumeIdentity &rVolumeId)
{
	if (pVolumes == NULL || rVolumeId.empty())
		return 0;

	for (std::size_t i = 0; i < nVolumeCount; ++i) {
		if (pVolumes[i].VolumeId == rVolumeId)
			return pVolumes[i].AvailableBytes;
	}
	return 0;
}

inline TempDirPlacementDecision SelectTempDirForProtectedVolumeSnapshot(
	const TempDirVolumeCandidate *pCandidates,
	const std::size_t nCandidateCount,
	const ProtectedVolumeAvailability *pVolumes,
	const std::size_t nVolumeCount,
	const VolumeIdentity &rIncomingVolumeId,
	const uint64_t nFileSize,
	const uint64_t nOldMaxFileSize)
{
	TempDirPlacementDecision noSelection = { false, 0u };
	if (pCandidates == NULL || nCandidateCount == 0)
		return noSelection;

	std::vector<bool> aDuplicateVolume(nCandidateCount, false);
	std::vector<int64_t> aAvailableBytes(nCandidateCount, 0);
	int64_t nHighestFreeBytes = 0;
	std::size_t nHighestFreeCandidate = nCandidateCount;
	for (std::size_t i = 0; i < nCandidateCount; ++i) {
		for (std::size_t j = 0; j < i; ++j) {
			if (pCandidates[i].VolumeId == pCandidates[j].VolumeId) {
				aDuplicateVolume[i] = true;
				break;
			}
		}
		if (aDuplicateVolume[i])
			continue;

		const int64_t nAvailableBytes = FindProtectedVolumeAvailableBytes(pVolumes, nVolumeCount, pCandidates[i].VolumeId);
		aAvailableBytes[i] = nAvailableBytes;
		if (nAvailableBytes > nHighestFreeBytes) {
			nHighestFreeBytes = nAvailableBytes;
			nHighestFreeCandidate = i;
		}
	}

	int64_t nHighestTotalBytes = 0;
	std::size_t nHighestTotalCandidate = nCandidateCount;
	std::size_t nHighestFreeEnoughCandidate = nCandidateCount;
	const int64_t nIncomingAvailableBytes = rIncomingVolumeId.empty()
		? 0
		: FindProtectedVolumeAvailableBytes(pVolumes, nVolumeCount, rIncomingVolumeId);
	const int64_t nTempDemandBytes = nFileSize > static_cast<uint64_t>((std::numeric_limits<int64_t>::max)())
		? (std::numeric_limits<int64_t>::max)()
		: static_cast<int64_t>(nFileSize);

	for (std::size_t i = 0; i < nCandidateCount; ++i) {
		if (aDuplicateVolume[i])
			continue;
		if (nFileSize > nOldMaxFileSize && pCandidates[i].IsFatVolume)
			continue;

		const int64_t nAvailableBytes = aAvailableBytes[i];
		const int64_t nIncomingDemandBytes = (!rIncomingVolumeId.empty() && rIncomingVolumeId != pCandidates[i].VolumeId)
			? nTempDemandBytes
			: 0;
		const bool bHasEnoughTempSpace = nAvailableBytes >= nTempDemandBytes;
		const bool bHasEnoughIncomingSpace = nIncomingDemandBytes == 0 || nIncomingAvailableBytes >= nIncomingDemandBytes;
		if (!bHasEnoughTempSpace || !bHasEnoughIncomingSpace)
			continue;

		if (!rIncomingVolumeId.empty() && rIncomingVolumeId == pCandidates[i].VolumeId) {
			TempDirPlacementDecision decision = { true, i };
			return decision;
		}

		if (nAvailableBytes > nHighestTotalBytes) {
			nHighestTotalBytes = nAvailableBytes;
			nHighestTotalCandidate = i;
		}
		if (i == nHighestFreeCandidate && nHighestFreeEnoughCandidate == nCandidateCount)
			nHighestFreeEnoughCandidate = i;
	}

	if (nHighestTotalCandidate < nCandidateCount) {
		TempDirPlacementDecision decision = { true, nHighestTotalCandidate };
		return decision;
	}
	if (nHighestFreeEnoughCandidate < nCandidateCount) {
		TempDirPlacementDecision decision = { true, nHighestFreeEnoughCandidate };
		return decision;
	}
	return noSelection;
}

inline bool HasProtectedVolumeBreach(const ProtectedVolumeSpaceState *pStates, const std::size_t nStateCount)
{
	if (pStates == NULL)
		return false;

	for (std::size_t i = 0; i < nStateCount; ++i) {
		if (IsProtectedVolumeBreached(pStates[i]))
			return true;
	}
	return false;
}

inline ProtectedDiskSpaceBreachAction ResolveProtectedDiskSpaceBreachAction(const bool bHasBreach, const bool bAlreadyBlocked, const bool bSameBreachSignature)
{
	if (!bHasBreach) {
		ProtectedDiskSpaceBreachAction action = { true, false, false, false };
		return action;
	}

	ProtectedDiskSpaceBreachAction action = { false, !(bAlreadyBlocked && bSameBreachSignature), true, true };
	return action;
}

inline bool IsPauseCandidate(const FileDiskSpaceStatus eStatus)
{
	return eStatus == FileDiskSpaceStatus::Active;
}

inline bool ShouldPauseForDiskSpace(const FileDiskSpaceState &rState, const uint64_t nFreeBytes, const uint64_t nMinimumFreeBytes)
{
	if (!IsPauseCandidate(rState.Status) || nFreeBytes >= nMinimumFreeBytes)
		return false;

	return !rState.IsNormalFile || rState.NeededBytes > 0;
}

inline bool IsResumeCandidate(const FileDiskSpaceStatus eStatus)
{
	return eStatus == FileDiskSpaceStatus::Insufficient;
}

inline void AccumulateResumeHeadroom(VolumeResumeBudget *pBudget, const FileDiskSpaceState &rState)
{
	if (pBudget == NULL || !IsResumeCandidate(rState.Status))
		return;

	pBudget->ResumeHeadroomBytes = PartFilePersistenceSeams::AddInsufficientResumeHeadroomBytes(
		pBudget->ResumeHeadroomBytes, rState.NeededBytes);
}

inline bool ShouldResumeForDiskSpace(const FileDiskSpaceState &rState, const VolumeResumeBudget &rBudget, const uint64_t nMinimumFreeBytes)
{
	return IsResumeCandidate(rState.Status)
		&& IsSameVolumeKey(rState.TempVolumeKey, rBudget.TempVolumeKey)
		&& PartFilePersistenceSeams::CanResumeInsufficientFileWithFreeSpace(
			rBudget.FreeBytes, nMinimumFreeBytes, rBudget.ResumeHeadroomBytes);
}
}
