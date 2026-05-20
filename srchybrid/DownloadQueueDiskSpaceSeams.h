#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

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
