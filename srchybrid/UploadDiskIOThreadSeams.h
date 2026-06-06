#pragma once

#include <cstdint>

#include <windows.h>

namespace UploadDiskIOThreadSeams
{
constexpr LONG kMinPendingReadBlocksPerClient = 8;
constexpr LONG kMaxPendingReadBlocksPerClient = 256;
constexpr INT_PTR kMaxPendingReadBlocksPerThread = 4096;

/**
 * @brief Computes the per-client overlapped upload-read budget from the configured per-slot upload budget.
 */
inline LONG GetBroadbandPendingReadBlocksPerClient(
	const std::uint32_t uTargetPerSlotBytesPerSec,
	const std::uint32_t uBlockBytes = 184320u,
	const std::uint32_t uTargetBufferSeconds = 8u,
	const LONG nMinimumBlocks = kMinPendingReadBlocksPerClient,
	const LONG nMaximumBlocks = kMaxPendingReadBlocksPerClient)
{
	if (uTargetPerSlotBytesPerSec == 0 || uBlockBytes == 0 || uTargetBufferSeconds == 0)
		return nMinimumBlocks;

	const std::uint64_t ullTargetBytes = static_cast<std::uint64_t>(uTargetPerSlotBytesPerSec) * uTargetBufferSeconds;
	LONG nBlocks = static_cast<LONG>((ullTargetBytes + uBlockBytes - 1u) / uBlockBytes);
	if (nBlocks < nMinimumBlocks)
		return nMinimumBlocks;
	if (nBlocks > nMaximumBlocks)
		return nMaximumBlocks;
	return nBlocks;
}

/**
 * @brief Computes the shared upload disk-thread read budget from the per-client budget and slot cap.
 */
inline INT_PTR GetBroadbandPendingReadBlocksPerThread(
	const LONG nPerClientLimit,
	const INT_PTR nSlotCap,
	const INT_PTR nMaximumThreadBlocks = kMaxPendingReadBlocksPerThread)
{
	if (nPerClientLimit <= 0 || nSlotCap <= 0)
		return kMinPendingReadBlocksPerClient;

	const std::uint64_t ullThreadLimit = static_cast<std::uint64_t>(nPerClientLimit) * static_cast<std::uint64_t>(nSlotCap);
	if (ullThreadLimit > static_cast<std::uint64_t>(nMaximumThreadBlocks))
		return nMaximumThreadBlocks;
	return static_cast<INT_PTR>(ullThreadLimit);
}

/**
 * @brief Reports whether another overlapped upload read may be issued.
 *
 * Windows can reject excess overlapped reads with ERROR_NOT_ENOUGH_MEMORY or
 * ERROR_NOT_ENOUGH_QUOTA, but by then the process has already applied pressure
 * to the system I/O manager. Keep an explicit in-process budget so hostile or
 * simply very fast upload demand cannot turn the disk thread into an unbounded
 * pile of EMBLOCKSIZE buffers.
 */
inline bool CanIssuePendingUploadRead(
	const LONG nClientPendingReads,
	const INT_PTR nThreadPendingReads,
	const LONG nClientReadLimit = kMaxPendingReadBlocksPerClient,
	const INT_PTR nThreadReadLimit = kMaxPendingReadBlocksPerThread)
{
	return nClientPendingReads >= 0
		&& nClientPendingReads < nClientReadLimit
		&& nThreadPendingReads >= 0
		&& nThreadPendingReads < nThreadReadLimit;
}
}
