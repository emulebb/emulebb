#pragma once

#include <Windows.h>

#include <cstddef>
#include <limits>

#include "ProtocolGuards.h"
#include "types.h"

#define EMULE_TEST_HAVE_AICH_HASH_INDEX_BUCKET_ESTIMATE 1

/**
 * @brief Captures the current foreground hashing state that can block background AICH sync work.
 */
struct AICHSyncForegroundHashState
{
	bool bIsClosing;
	INT_PTR nSharedFileHashingCount;
	bool bHasPendingPartFileHashing;
};

/**
 * @brief Describes whether the background AICH sync loop should exit, continue, or yield briefly.
 */
struct AICHSyncForegroundWaitAction
{
	bool bShouldExit;
	DWORD dwSleepMilliseconds;
};

/**
 * @brief Describes how duplicate stored AICH hash entries should be handled while compacting hash files.
 */
struct StoredAICHHashUpdate
{
	bool bShouldReplaceExisting;
	ULONGLONG nReplacedFilePos;
};

/**
 * @brief Describes how a malformed one-sided AICH tree node should be normalized.
 */
struct IncompleteAICHTreeNodeAction
{
	bool bHasIncompleteChildren;
	bool bShouldInvalidateNodeHash;
};

namespace AICHMaintenanceSeams
{
/**
 * @brief Bounded fallback delay used when the AICH sync thread yields to foreground hashing.
 */
constexpr DWORD kForegroundHashYieldDelayMs = 1u;
constexpr UINT kMinAICHHashIndexBucketCount = 257u;
constexpr UINT kMaxAICHHashIndexBucketCount = 1048583u;

/**
 * @brief Derives the raw byte span for a sequence of serialized AICH hashes while rejecting overflow.
 */
inline bool TryDeriveAICHHashPayloadSize(const size_t nHashSize, const uint32 nHashCount, uint32 *pnPayloadSize)
{
	if (pnPayloadSize == NULL)
		return false;

	size_t nPayloadSize = 0;
	if (!TryMultiplyAddSize(nHashSize, static_cast<size_t>(nHashCount), 0u, &nPayloadSize)
		|| nPayloadSize > static_cast<size_t>((std::numeric_limits<uint32>::max)()))
	{
		return false;
	}

	*pnPayloadSize = static_cast<uint32>(nPayloadSize);
	return true;
}

/**
 * @brief Estimates a bounded hash-table size for indexing serialized known2 hashsets.
 */
inline UINT EstimateAICHHashIndexBucketCount(ULONGLONG nSerializedFileSize, const size_t nHashSize)
{
	if (nHashSize == 0u || nHashSize > (std::numeric_limits<ULONGLONG>::max)() - sizeof(uint32))
		return kMinAICHHashIndexBucketCount;

	const ULONGLONG nMinSerializedHashsetSize = static_cast<ULONGLONG>(nHashSize) + sizeof(uint32);
	if (nSerializedFileSize <= 1u)
		return kMinAICHHashIndexBucketCount;

	const ULONGLONG nEstimatedHashsets = (nSerializedFileSize - 1u) / nMinSerializedHashsetSize;
	if (nEstimatedHashsets >= ((std::numeric_limits<ULONGLONG>::max)() - 1u) / 2u)
		return kMaxAICHHashIndexBucketCount;

	const ULONGLONG nRequestedBuckets = nEstimatedHashsets * 2u + 1u;
	if (nRequestedBuckets <= kMinAICHHashIndexBucketCount)
		return kMinAICHHashIndexBucketCount;
	if (nRequestedBuckets >= kMaxAICHHashIndexBucketCount)
		return kMaxAICHHashIndexBucketCount;
	return static_cast<UINT>(nRequestedBuckets | 1u);
}

/**
 * @brief Computes the bounded cooperative-wait action for background AICH sync work.
 */
inline AICHSyncForegroundWaitAction GetForegroundHashWaitAction(const AICHSyncForegroundHashState &state)
{
	if (state.bIsClosing)
		return {true, 0u};

	if (state.nSharedFileHashingCount > 0 || state.bHasPendingPartFileHashing)
		return {false, kForegroundHashYieldDelayMs};

	return {false, 0u};
}

/**
 * @brief Keeps the newest stored hash position while tolerating duplicate serialized AICH hashes.
 */
inline StoredAICHHashUpdate ResolveStoredAICHHashUpdate(ULONGLONG nExistingFilePos, ULONGLONG nNewFilePos)
{
	if (nNewFilePos <= nExistingFilePos)
		return {false, 0u};

	return {true, nExistingFilePos};
}

/**
 * @brief Detects and normalizes the unsupported state where only one child branch exists.
 */
inline IncompleteAICHTreeNodeAction GetIncompleteAICHTreeNodeAction(bool bHasLeftChild, bool bHasRightChild)
{
	if (bHasLeftChild != bHasRightChild)
		return {true, true};

	return {false, false};
}
}
