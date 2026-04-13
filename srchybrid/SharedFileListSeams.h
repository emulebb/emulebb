#pragma once

#include "AtomicStateSeams.h"
#include "PathHelpers.h"

#define EMULE_TESTS_HAS_SHARED_FILE_LIST_PATH_SEAMS 1

namespace SharedFileListSeams
{
/**
 * @brief Stable snapshot of the state used to decide whether a shared-folder auto-reload should be queued.
 */
struct AutoReloadScheduleState
{
	bool bAutoRescanEnabled;
	bool bAutoReloadPending;
	bool bAutoReloadInProgress;
	bool bReloadTickElapsed;
	bool bFallbackPolling;
	bool bDirty;
};

/**
 * @brief Returns the bounded delay used to yield after queuing one full imported part for async write.
 */
constexpr DWORD kImportPartProgressYieldMs = 100;

/**
 * @brief Returns the scheduling decision for the next shared-folder auto-reload from one stable state snapshot.
 */
inline bool ShouldScheduleAutoReload(const AutoReloadScheduleState &rState)
{
	if (!rState.bAutoRescanEnabled
		|| rState.bAutoReloadPending
		|| rState.bAutoReloadInProgress
		|| !rState.bReloadTickElapsed)
	{
		return false;
	}

	return rState.bFallbackPolling || rState.bDirty;
}

/**
 * @brief Reports whether the import thread should yield after posting part-write progress.
 */
inline bool ShouldYieldAfterImportProgress(const bool bAppRunning, const bool bImportedFullPart, const bool bImportStillActive)
{
	return bAppRunning && bImportedFullPart && bImportStillActive;
}

/**
 * Returns whether a file may enter the shared-file list when it is either a part file,
 * shared by directory/category rules, or shared by an explicit single-file share.
 */
inline bool CanAddSharedFile(const bool bIsPartFile, const bool bSharedByDirectory, const bool bSharedByExactPath)
{
	return bIsPartFile || bSharedByDirectory || bSharedByExactPath;
}

/**
 * @brief Reports whether an explicitly shared file path still matches a candidate after canonical normalization.
 */
inline bool MatchesExplicitSharedFilePath(const CString &rstrSharedPath, const CString &rstrCandidatePath)
{
	return PathHelpers::ArePathsEquivalent(rstrSharedPath, rstrCandidatePath);
}

/**
 * @brief Reports whether a single-shared file still belongs to the provided directory after canonical normalization.
 */
inline bool ContainsSharedChildPath(const CString &rstrDirectoryPath, const CString &rstrCandidatePath)
{
	return PathHelpers::IsPathWithinDirectory(rstrDirectoryPath, rstrCandidatePath);
}

/**
 * @brief Marks the shared-file auto-rescan state as dirty for the next main-thread reload pass.
 */
inline void MarkAutoRescanDirtyFlag(std::atomic<LONG> &rDirtyFlag)
{
	SetAtomicLongFlag(rDirtyFlag);
}

/**
 * @brief Reports whether the shared-file auto-rescan state still has pending work.
 */
inline bool IsAutoRescanDirtyFlagSet(const std::atomic<LONG> &rDirtyFlag)
{
	return IsAtomicLongFlagSet(rDirtyFlag);
}

/**
 * @brief Clears the shared-file auto-rescan dirty marker after the reload pass drains it.
 */
inline void ClearAutoRescanDirtyFlag(std::atomic<LONG> &rDirtyFlag)
{
	ClearAtomicLongFlag(rDirtyFlag);
}
}
