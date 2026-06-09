#pragma once

#include <cstddef>

#define EMULEBB_TEST_HAVE_PART_FILE_WRITE_DISPATCH_CAP 1
#define EMULEBB_TEST_HAVE_PART_FILE_WRITE_CREATE_FLAGS 1

namespace PartFileWriteThreadSeams
{
inline constexpr size_t kMaxWriteDispatchesPerWake = 128u;

/**
 * @brief Returns the file creation flags for asynchronous part-file writes.
 */
inline unsigned long BuildPartFileWriteCreateFlags(const unsigned long nOverlappedFlag)
{
	return nOverlappedFlag;
}

/**
 * @brief Reports whether the part-file write helper may submit another overlapped write in this wake.
 */
inline bool CanDispatchWriteInCurrentWake(const size_t nPendingWrites, const size_t nDispatchLimit)
{
	return nDispatchLimit == 0u || nPendingWrites < nDispatchLimit;
}

/**
 * @brief Reports whether queued writes should be deferred to a later IOCP wake.
 */
inline bool ShouldDeferRemainingWrites(
	const bool bHasQueuedWrites,
	const bool bStopRequested,
	const size_t nPendingWrites,
	const size_t nDispatchLimit)
{
	return bHasQueuedWrites
		&& !bStopRequested
		&& nDispatchLimit != 0u
		&& nPendingWrites >= nDispatchLimit;
}

/**
 * @brief Reports whether private queued writes require a follow-up worker wake.
 */
inline bool ShouldWakeForUndispatchedWrites(const bool bHasQueuedWrites, const bool bStopRequested)
{
	return bHasQueuedWrites && !bStopRequested;
}
}
