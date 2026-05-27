#pragma once

#include <cstdint>

#include <windows.h>

namespace UploadDiskIOThreadSeams
{
constexpr LONG kMaxPendingReadBlocksPerClient = 4;
constexpr INT_PTR kMaxPendingReadBlocksPerThread = 64;

/**
 * @brief Reports whether another overlapped upload read may be issued.
 *
 * Windows can reject excess overlapped reads with ERROR_NOT_ENOUGH_MEMORY or
 * ERROR_NOT_ENOUGH_QUOTA, but by then the process has already applied pressure
 * to the system I/O manager. Keep an explicit in-process budget so hostile or
 * simply very fast upload demand cannot turn the disk thread into an unbounded
 * pile of EMBLOCKSIZE buffers.
 */
inline bool CanIssuePendingUploadRead(const LONG nClientPendingReads, const INT_PTR nThreadPendingReads)
{
	return nClientPendingReads >= 0
		&& nClientPendingReads < kMaxPendingReadBlocksPerClient
		&& nThreadPendingReads >= 0
		&& nThreadPendingReads < kMaxPendingReadBlocksPerThread;
}
}
