#pragma once

#include <cstddef>
#include <cmath>
#include <cstdint>
#include <string>
#include <tchar.h>
#include <windows.h>

#include "LongPathSeams.h"

namespace PartFilePersistenceSeams
{
constexpr uint64_t kDiskSpaceFloorUnitBytes = 1024ull * 1024ull * 1024ull;
constexpr uint64_t kMinDiskSpaceFloorGiB = 1ull;
constexpr uint64_t kMinConfigDiskSpaceFloorGiB = 1ull;
constexpr uint64_t kMinTempDiskSpaceFloorGiB = 5ull;
constexpr uint64_t kMinIncomingDiskSpaceFloorGiB = 5ull;
constexpr uint64_t kMaxDiskSpaceFloorGiB = 5ull * 1024ull;
constexpr uint64_t kMinDownloadFreeBytes = kMinDiskSpaceFloorGiB * kDiskSpaceFloorUnitBytes;
constexpr uint64_t kMinConfigFreeBytes = kMinConfigDiskSpaceFloorGiB * kDiskSpaceFloorUnitBytes;
constexpr uint64_t kMinTempFreeBytes = kMinTempDiskSpaceFloorGiB * kDiskSpaceFloorUnitBytes;
constexpr uint64_t kMinIncomingFreeBytes = kMinIncomingDiskSpaceFloorGiB * kDiskSpaceFloorUnitBytes;
constexpr uint64_t kMaxDownloadFreeBytes = kMaxDiskSpaceFloorGiB * kDiskSpaceFloorUnitBytes;
constexpr uint64_t kMinPartMetWriteFreeBytes = kMinDownloadFreeBytes;
constexpr uint64_t kMaxInsufficientResumeHeadroomBytes = 1ull * kDiskSpaceFloorUnitBytes;

constexpr TCHAR kPartMetBackupSuffix[] = _T(".bak");
constexpr TCHAR kPartMetTemporarySuffix[] = _T(".backup");

using PartFilePathString = std::basic_string<TCHAR>;

/**
 * Names the file-system targets touched by CPartFile metadata cleanup and deletion.
 */
enum class PartFileDeletePathRole
{
	Metadata,
	Data,
	Backup,
	Temporary
};

/**
 * Mirrors the legacy RemoveFileExtension helper used by CPartFile deletion.
 */
inline PartFilePathString RemoveFinalExtensionForPartDataPath(const PartFilePathString &rPartMetPath)
{
	const PartFilePathString::size_type nDot = rPartMetPath.find_last_of(_T('.'));
	return nDot == PartFilePathString::npos ? rPartMetPath : rPartMetPath.substr(0u, nDot);
}

/**
 * Builds the companion path selected by the CPartFile delete/remove flows.
 */
inline PartFilePathString BuildPartFileDeletePath(const PartFilePathString &rPartMetPath, const PartFileDeletePathRole eRole)
{
	switch (eRole) {
		case PartFileDeletePathRole::Metadata:
			return rPartMetPath;
		case PartFileDeletePathRole::Data:
			return RemoveFinalExtensionForPartDataPath(rPartMetPath);
		case PartFileDeletePathRole::Backup:
			return rPartMetPath + kPartMetBackupSuffix;
		case PartFileDeletePathRole::Temporary:
			return rPartMetPath + kPartMetTemporarySuffix;
		default:
			return PartFilePathString();
	}
}

/**
 * Identifies which delete targets are active for metadata cleanup vs full removal.
 */
inline bool IsPartFileDeletePathActive(const PartFileDeletePathRole eRole, const bool bDeletePartDataFile)
{
	return eRole != PartFileDeletePathRole::Data || bDeletePartDataFile;
}

/**
 * Returns the number of paths the current CPartFile flow attempts to delete.
 */
inline std::size_t GetPartFileDeletePathCount(const bool bDeletePartDataFile)
{
	return bDeletePartDataFile ? 4u : 3u;
}

inline uint64_t ConvertDiskSpaceFloorGiBToBytes(const uint64_t nGiB)
{
	return nGiB * kDiskSpaceFloorUnitBytes;
}

inline uint64_t NormalizeDiskSpaceFloor(const uint64_t nConfiguredBytes, const uint64_t nMinimumBytes, const uint64_t nMaximumBytes = kMaxDownloadFreeBytes)
{
	const uint64_t nClampedToMin = nConfiguredBytes >= nMinimumBytes ? nConfiguredBytes : nMinimumBytes;
	return nClampedToMin <= nMaximumBytes ? nClampedToMin : nMaximumBytes;
}

inline uint64_t NormalizeDiskSpaceFloorGiB(const uint64_t nConfiguredGiB, const uint64_t nMinimumGiB, const uint64_t nMaximumGiB = kMaxDiskSpaceFloorGiB)
{
	const uint64_t nClampedToMin = nConfiguredGiB >= nMinimumGiB ? nConfiguredGiB : nMinimumGiB;
	return nClampedToMin <= nMaximumGiB ? nClampedToMin : nMaximumGiB;
}

inline uint64_t NormalizeDownloadFreeSpaceFloor(const uint64_t nConfiguredBytes, const uint64_t nMinimumBytes = kMinDownloadFreeBytes, const uint64_t nMaximumBytes = kMaxDownloadFreeBytes)
{
	return NormalizeDiskSpaceFloor(nConfiguredBytes, nMinimumBytes, nMaximumBytes);
}

inline uint64_t NormalizeDownloadFreeSpaceFloorGiB(const uint64_t nConfiguredGiB, const uint64_t nMinimumGiB = kMinDiskSpaceFloorGiB, const uint64_t nMaximumGiB = kMaxDiskSpaceFloorGiB)
{
	return NormalizeDiskSpaceFloorGiB(nConfiguredGiB, nMinimumGiB, nMaximumGiB);
}

inline uint64_t ConvertDiskSpaceFloorBytesToDisplayGiB(const uint64_t nConfiguredBytes, const uint64_t nMinimumBytes, const uint64_t nMinimumGiB, const uint64_t nMaximumGiB = kMaxDiskSpaceFloorGiB)
{
	const uint64_t nNormalizedBytes = NormalizeDiskSpaceFloor(nConfiguredBytes, nMinimumBytes, ConvertDiskSpaceFloorGiBToBytes(nMaximumGiB));
	return NormalizeDiskSpaceFloorGiB((nNormalizedBytes + (kDiskSpaceFloorUnitBytes - 1ull)) / kDiskSpaceFloorUnitBytes, nMinimumGiB, nMaximumGiB);
}

inline uint64_t ConvertDownloadFreeSpaceFloorBytesToDisplayGiB(const uint64_t nConfiguredBytes)
{
	return ConvertDiskSpaceFloorBytesToDisplayGiB(nConfiguredBytes, kMinDownloadFreeBytes, kMinDiskSpaceFloorGiB);
}

inline uint64_t GetInsufficientResumeHeadroomBytes(const uint64_t nNeededBytes, const uint64_t nMaximumHeadroomBytes = kMaxInsufficientResumeHeadroomBytes)
{
	return nNeededBytes <= nMaximumHeadroomBytes ? nNeededBytes : nMaximumHeadroomBytes;
}

inline uint64_t AddInsufficientResumeHeadroomBytes(const uint64_t nCurrentHeadroomBytes, const uint64_t nNeededBytes, const uint64_t nMaximumHeadroomBytes = kMaxInsufficientResumeHeadroomBytes)
{
	const uint64_t nAdditionalHeadroomBytes = GetInsufficientResumeHeadroomBytes(nNeededBytes, nMaximumHeadroomBytes);
	const uint64_t nRemainingCapacity = static_cast<uint64_t>(-1) - nCurrentHeadroomBytes;
	return nAdditionalHeadroomBytes <= nRemainingCapacity ? nCurrentHeadroomBytes + nAdditionalHeadroomBytes : static_cast<uint64_t>(-1);
}

inline uint64_t GetInsufficientResumeThresholdBytes(const uint64_t nMinimumFreeBytes, const uint64_t nHeadroomBytes)
{
	const uint64_t nRemainingCapacity = static_cast<uint64_t>(-1) - nMinimumFreeBytes;
	return nHeadroomBytes <= nRemainingCapacity ? nMinimumFreeBytes + nHeadroomBytes : static_cast<uint64_t>(-1);
}

inline bool CanResumeInsufficientFileWithFreeSpace(const uint64_t nFreeBytes, const uint64_t nMinimumFreeBytes, const uint64_t nHeadroomBytes)
{
	return nFreeBytes >= GetInsufficientResumeThresholdBytes(nMinimumFreeBytes, nHeadroomBytes);
}

struct PartMetWriteGuardDecision
{
	bool UseCachedResult;
	bool CanWrite;
};

struct PartMetWriteGuardState
{
	bool HasCachedResult;
	bool CanWrite;
};

using CopyFileFn = BOOL (WINAPI *)(LPCTSTR, LPCTSTR, BOOL);
using DeleteFileFn = BOOL (WINAPI *)(LPCTSTR);
using MoveFileExFn = BOOL (WINAPI *)(LPCTSTR, LPCTSTR, DWORD);
using GetFileAttributesFn = DWORD (WINAPI *)(LPCTSTR);
using GetLastErrorFn = DWORD (WINAPI *)(void);

struct FileSystemOps
{
	CopyFileFn CopyFile;
	DeleteFileFn DeleteFile;
	MoveFileExFn MoveFileEx;
	GetFileAttributesFn GetFileAttributes;
	GetLastErrorFn GetLastError;
};

inline BOOL WINAPI CopyFileLongPath(LPCTSTR pszExistingPath, LPCTSTR pszNewPath, BOOL bFailIfExists)
{
	return LongPathSeams::CopyFile(pszExistingPath, pszNewPath, bFailIfExists);
}

inline BOOL WINAPI DeleteFileLongPath(LPCTSTR pszPath)
{
	return LongPathSeams::DeleteFile(pszPath);
}

inline BOOL WINAPI MoveFileExLongPath(LPCTSTR pszExistingPath, LPCTSTR pszNewPath, DWORD dwFlags)
{
	return LongPathSeams::MoveFileEx(pszExistingPath, pszNewPath, dwFlags);
}

inline DWORD WINAPI GetFileAttributesLongPath(LPCTSTR pszPath)
{
	return LongPathSeams::GetFileAttributes(pszPath);
}

inline FileSystemOps GetDefaultFileSystemOps()
{
	FileSystemOps ops = { CopyFileLongPath, DeleteFileLongPath, MoveFileExLongPath, GetFileAttributesLongPath, ::GetLastError };
	return ops;
}

inline bool CanWritePartMetWithFreeSpace(const uint64_t nFreeBytes, const uint64_t nRequiredBytes = kMinPartMetWriteFreeBytes)
{
	return nFreeBytes >= nRequiredBytes;
}

/**
 * Returns the conservative free-space floor to apply when a protected path's volume cannot be identified.
 */
inline uint64_t GetRequiredFreeBytesForUnresolvedVolume(const uint64_t nHighestResolvedProtectedRequiredBytes, const uint64_t nConfiguredProtectedFallbackBytes)
{
	return nHighestResolvedProtectedRequiredBytes > nConfiguredProtectedFallbackBytes
		? nHighestResolvedProtectedRequiredBytes
		: nConfiguredProtectedFallbackBytes;
}

/**
 * Adds byte counts without wrapping, returning UINT64_MAX on overflow.
 */
inline uint64_t SaturatingAddBytes(const uint64_t nLeftBytes, const uint64_t nRightBytes)
{
	const uint64_t nRemainingCapacity = static_cast<uint64_t>(-1) - nLeftBytes;
	return nRightBytes <= nRemainingCapacity ? nLeftBytes + nRightBytes : static_cast<uint64_t>(-1);
}

inline bool TryMeasureGapBytes(const uint64_t nStartByte, const uint64_t nEndByte, uint64_t *pnGapBytes)
{
	if (pnGapBytes != NULL)
		*pnGapBytes = 0;
	if (nEndByte < nStartByte)
		return false;

	if (pnGapBytes != NULL)
		*pnGapBytes = SaturatingAddBytes(nEndByte - nStartByte, 1u);
	return true;
}

struct CompletedInfo
{
	uint64_t CompletedBytes;
	float PercentCompleted;
	bool ClampedInvalidTotalGaps;
};

inline CompletedInfo ResolveCompletedInfo(const uint64_t nFileSize, uint64_t nTotalGapBytes, const bool bHasGaps)
{
	CompletedInfo info = { nFileSize, 100.0F, false };
	if (!bHasGaps)
		return info;

	if (nTotalGapBytes > nFileSize) {
		nTotalGapBytes = nFileSize;
		info.ClampedInvalidTotalGaps = true;
	}

	info.CompletedBytes = nFileSize - nTotalGapBytes;
	if (nFileSize == 0u) {
		info.PercentCompleted = 0.0F;
		return info;
	}

	const double completedFraction = 1.0 - static_cast<double>(nTotalGapBytes) / static_cast<double>(nFileSize);
	info.PercentCompleted = static_cast<float>(std::floor(completedFraction * 1000.0) / 10.0);
	return info;
}

inline PartMetWriteGuardDecision ResolvePartMetWriteGuard(const bool bHasCachedResult, const bool bCachedCanWrite, const bool bForceRefresh, const uint64_t nFreeBytes, const uint64_t nRequiredBytes = kMinPartMetWriteFreeBytes)
{
	(void)bHasCachedResult;
	(void)bCachedCanWrite;
	(void)bForceRefresh;

	PartMetWriteGuardDecision decision = { false, CanWritePartMetWithFreeSpace(nFreeBytes, nRequiredBytes) };
	return decision;
}

inline bool ShouldReusePartMetWriteCache(const bool bHasCachedResult, const bool bForceRefresh)
{
	(void)bHasCachedResult;
	(void)bForceRefresh;
	return false;
}

inline void StorePartMetWriteGuardState(PartMetWriteGuardState *pState, const bool bCanWrite)
{
	if (pState == NULL)
		return;

	pState->HasCachedResult = true;
	pState->CanWrite = bCanWrite;
}

inline void InvalidatePartMetWriteGuardState(PartMetWriteGuardState *pState)
{
	if (pState == NULL)
		return;

	pState->HasCachedResult = false;
	pState->CanWrite = false;
}

inline bool ShouldFlushPartFileOnDestroy(const bool bIsClosing, const bool bHasWriteThread, const bool bIsWriteThreadRunning)
{
	return !bIsClosing || (bHasWriteThread && bIsWriteThreadRunning);
}

enum class PartFileDeleteAsyncWriteAction
{
	DeleteNow,
	WaitForWriteRelease,
	DeferUntilWriteRelease
};

inline bool HasPartFileAsyncWriteReferences(const int nOutstandingWrites, const bool bHasPendingBufferedWrite)
{
	return nOutstandingWrites > 0 || bHasPendingBufferedWrite;
}

inline PartFileDeleteAsyncWriteAction ClassifyPartFileDeleteAsyncWriteAction(const bool bHasAsyncWriteReferences, const bool bHasWriteThread, const bool bIsWriteThreadRunning)
{
	if (!bHasAsyncWriteReferences)
		return PartFileDeleteAsyncWriteAction::DeleteNow;
	if (bHasWriteThread && bIsWriteThreadRunning)
		return PartFileDeleteAsyncWriteAction::WaitForWriteRelease;
	return PartFileDeleteAsyncWriteAction::DeferUntilWriteRelease;
}

inline bool ShouldSavePartMetAfterShutdownFlush(const bool bFlushedBufferedData)
{
	return bFlushedBufferedData;
}

inline bool PathExists(const LPCTSTR pszPath, const FileSystemOps &rOps = GetDefaultFileSystemOps())
{
	if (pszPath == NULL || pszPath[0] == _T('\0'))
		return false;

	return rOps.GetFileAttributes(pszPath) != INVALID_FILE_ATTRIBUTES;
}

inline bool TryReplaceFileAtomicallyWithOps(const LPCTSTR pszSrc, const LPCTSTR pszDst, DWORD *pdwLastError, const FileSystemOps &rOps)
{
	if (pdwLastError != NULL)
		*pdwLastError = ERROR_SUCCESS;

	if (pszSrc == NULL || pszDst == NULL || pszSrc[0] == _T('\0') || pszDst[0] == _T('\0')) {
		if (pdwLastError != NULL)
			*pdwLastError = ERROR_INVALID_PARAMETER;
		return false;
	}

	if (rOps.MoveFileEx(pszSrc, pszDst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		return true;

	if (pdwLastError != NULL)
		*pdwLastError = rOps.GetLastError();
	return false;
}

inline bool TryReplaceFileAtomically(const LPCTSTR pszSrc, const LPCTSTR pszDst, DWORD *pdwLastError = NULL)
{
	return TryReplaceFileAtomicallyWithOps(pszSrc, pszDst, pdwLastError, GetDefaultFileSystemOps());
}

inline bool TryCopyFileToTempAndReplaceWithOps(const LPCTSTR pszSrc, const LPCTSTR pszDst, const LPCTSTR pszTmp, const bool bDontOverride, DWORD *pdwLastError, const FileSystemOps &rOps)
{
	if (pdwLastError != NULL)
		*pdwLastError = ERROR_SUCCESS;

	if (pszSrc == NULL || pszDst == NULL || pszTmp == NULL || pszSrc[0] == _T('\0') || pszDst[0] == _T('\0') || pszTmp[0] == _T('\0')) {
		if (pdwLastError != NULL)
			*pdwLastError = ERROR_INVALID_PARAMETER;
		return false;
	}

	(void)rOps.DeleteFile(pszTmp);
	if (!rOps.CopyFile(pszSrc, pszTmp, FALSE)) {
		if (pdwLastError != NULL)
			*pdwLastError = rOps.GetLastError();
		return false;
	}

	if (bDontOverride && PathExists(pszDst, rOps)) {
		(void)rOps.DeleteFile(pszTmp);
		if (pdwLastError != NULL)
			*pdwLastError = ERROR_FILE_EXISTS;
		return false;
	}

	DWORD dwLastError = ERROR_SUCCESS;
	if (!TryReplaceFileAtomicallyWithOps(pszTmp, pszDst, &dwLastError, rOps)) {
		(void)rOps.DeleteFile(pszTmp);
		if (pdwLastError != NULL)
			*pdwLastError = dwLastError;
		return false;
	}

	return true;
}

inline bool TryCopyFileToTempAndReplace(const LPCTSTR pszSrc, const LPCTSTR pszDst, const LPCTSTR pszTmp, const bool bDontOverride, DWORD *pdwLastError = NULL)
{
	return TryCopyFileToTempAndReplaceWithOps(pszSrc, pszDst, pszTmp, bDontOverride, pdwLastError, GetDefaultFileSystemOps());
}
}

#define EMULE_TEST_HAVE_PART_FILE_PERSISTENCE_SEAMS 1
#define EMULE_TEST_HAVE_PART_FILE_DISKSPACE_FLOOR_SEAMS 1
#define EMULE_TEST_HAVE_PART_FILE_DELETE_PLAN_SEAMS 1
