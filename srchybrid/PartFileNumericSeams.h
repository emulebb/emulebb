#pragma once

#include <cstddef>
#include <limits>

#include "ProtocolGuards.h"
#include "types.h"

namespace PartFileNumericSeams
{
/**
 * @brief Maximum bounded completion percentage used by the chunk ranking logic.
 */
constexpr uint16 kChunkCompletionPercentMax = 100u;

/**
 * @brief Converts an unsigned 32-bit score into a bounded 16-bit chunk-ranking value.
 */
inline uint16 ClampUInt32ToUInt16(const uint32 nValue)
{
	return nValue > static_cast<uint32>((std::numeric_limits<uint16>::max)())
		? (std::numeric_limits<uint16>::max)()
		: static_cast<uint16>(nValue);
}

/**
 * @brief Converts an unsigned 64-bit score into a bounded 16-bit chunk-ranking value.
 */
inline uint16 ClampUInt64ToUInt16(const uint64 nValue)
{
	return nValue > static_cast<uint64>((std::numeric_limits<uint16>::max)())
		? (std::numeric_limits<uint16>::max)()
		: static_cast<uint16>(nValue);
}

/**
 * @brief Converts a signed list-count style value into a bounded non-negative 16-bit score.
 */
inline uint16 ClampCountToUInt16(const INT_PTR nValue)
{
	return nValue <= 0 ? 0u : ClampUInt64ToUInt16(static_cast<uint64>(nValue));
}

/**
 * @brief Derives the serialized AICH hashset span while rejecting overflowed multiply-add math.
 */
inline bool TryDeriveAICHHashSetSize(const size_t nHashSize, const uint32 nPartHashCount, uint32 *pnHashSetSize)
{
	if (pnHashSetSize == NULL)
		return false;

	size_t nHashCountWithMaster = 0;
	size_t nSerializedSize = 0;
	if (!TryAddSize(static_cast<size_t>(nPartHashCount), 1u, &nHashCountWithMaster)
		|| !TryMultiplyAddSize(nHashCountWithMaster, nHashSize, 2u, &nSerializedSize)
		|| nSerializedSize > static_cast<size_t>((std::numeric_limits<uint32>::max)()))
	{
		return false;
	}

	*pnHashSetSize = static_cast<uint32>(nSerializedSize);
	return true;
}

/**
 * @brief Calculates the bounded rare-part source bucket limit from the current source count.
 */
inline uint16 CalculateRareChunkSourceLimit(const size_t nSourceCount)
{
	size_t nRoundedSourceBucket = 0;
	if (!TryAddSize(nSourceCount, 9u, &nRoundedSourceBucket))
		nRoundedSourceBucket = (std::numeric_limits<size_t>::max)();
	else
		nRoundedSourceBucket /= 10u;

	if (nRoundedSourceBucket < 3u)
		nRoundedSourceBucket = 3u;

	return nRoundedSourceBucket > static_cast<size_t>((std::numeric_limits<uint16>::max)())
		? (std::numeric_limits<uint16>::max)()
		: static_cast<uint16>(nRoundedSourceBucket);
}

/**
 * @brief Calculates the chunk completion percentage with the legacy round-up behavior while staying in range.
 */
inline uint16 CalculateChunkCompletionPercent(const uint64 nCompletedBytes, const uint64 nFullPartSize)
{
	if (nFullPartSize == 0)
		return 0u;

	const uint64 nBoundedCompletedBytes = nCompletedBytes <= nFullPartSize ? nCompletedBytes : nFullPartSize;
	if (nBoundedCompletedBytes == 0)
		return 0u;
	if (nBoundedCompletedBytes == nFullPartSize)
		return kChunkCompletionPercentMax;

	const uint64 nRoundingBias = nFullPartSize - 1u;
	if (nBoundedCompletedBytes > (((std::numeric_limits<uint64>::max)() - nRoundingBias) / kChunkCompletionPercentMax))
		return kChunkCompletionPercentMax;

	return ClampUInt64ToUInt16(((nBoundedCompletedBytes * kChunkCompletionPercentMax) + nRoundingBias) / nFullPartSize);
}

/**
 * @brief Returns the legacy "flush at twice the configured buffer" threshold without overflow.
 */
inline uint64 GetBufferedDataFlushThreshold(const uint64 nEffectiveFileBufferSize)
{
	const uint64 nMax = (std::numeric_limits<uint64>::max)();
	return nEffectiveFileBufferSize > nMax / 2u ? nMax : nEffectiveFileBufferSize * 2u;
}

/**
 * @brief Reports whether already queued part-file data is beyond the flush budget.
 */
inline bool ShouldFlushBufferedData(const uint64 nCurrentBufferedBytes, const uint64 nEffectiveFileBufferSize)
{
	return nCurrentBufferedBytes > GetBufferedDataFlushThreshold(nEffectiveFileBufferSize);
}

/**
 * @brief Reports whether the current queue should be flushed before another buffer is allocated.
 *
 * The post-write flush keeps behavior compatible, but it still permits one more
 * download block allocation beyond the budget. This preflight check lets the
 * caller drain existing data first, reducing peak heap use without rejecting the
 * block being received from the peer.
 */
inline bool ShouldFlushBeforeBufferedWrite(
	const uint64 nCurrentBufferedBytes,
	const uint64 nIncomingBufferedBytes,
	const uint64 nEffectiveFileBufferSize)
{
	if (nCurrentBufferedBytes == 0)
		return false;

	const uint64 nThreshold = GetBufferedDataFlushThreshold(nEffectiveFileBufferSize);
	return nCurrentBufferedBytes > nThreshold || nIncomingBufferedBytes > nThreshold - nCurrentBufferedBytes;
}
}
