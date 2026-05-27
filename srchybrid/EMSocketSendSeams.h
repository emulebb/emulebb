#pragma once

#include <cstddef>
#include "types.h"

static const size_t kMaxEMSocketQueuedControlPackets = 1024u;
static const uint64 kMaxEMSocketQueuedControlBytes = 8ull * 1024ull * 1024ull;
static const size_t kMaxEMSocketQueuedStandardPackets = 2048u;
static const uint64 kMaxEMSocketQueuedStandardBytes = 16ull * 1024ull * 1024ull;

enum EMSocketQueueStateFlags
{
	emSocketQueueNone = 0,
	emSocketQueueHasSendBuffer = 1 << 0,
	emSocketQueueHasControlPackets = 1 << 1,
	emSocketQueueHasStandardPackets = 1 << 2
};

/**
 * @brief Classifies the visible EMSocket send queue state from a stable snapshot.
 */
inline unsigned ClassifyEMSocketQueueState(bool bHasSendBuffer, bool bHasControlPackets, bool bHasStandardPackets)
{
	unsigned nQueueState = emSocketQueueNone;
	if (bHasSendBuffer)
		nQueueState |= emSocketQueueHasSendBuffer;
	if (bHasControlPackets)
		nQueueState |= emSocketQueueHasControlPackets;
	if (bHasStandardPackets)
		nQueueState |= emSocketQueueHasStandardPackets;
	return nQueueState;
}

/**
 * @brief Reports whether the socket still has queued work matching the caller's filter.
 */
inline bool HasEMSocketQueuedPackets(unsigned nQueueState, bool bOnlyStandardPackets)
{
	const unsigned nRelevantFlags = emSocketQueueHasSendBuffer | emSocketQueueHasStandardPackets
		| (bOnlyStandardPackets ? 0u : emSocketQueueHasControlPackets);
	return (nQueueState & nRelevantFlags) != 0;
}

/**
 * @brief Consumes one queued payload contribution while checking whether more file data is still required.
 */
inline bool ConsumeQueuedFilePayload(uint32 nActualPayloadSize, uint32 *pnRemainingPayloadBytes)
{
	ASSERT(pnRemainingPayloadBytes != NULL);
	if (pnRemainingPayloadBytes == NULL)
		return false;
	if (nActualPayloadSize > *pnRemainingPayloadBytes)
		return false;
	*pnRemainingPayloadBytes -= nActualPayloadSize;
	return true;
}

/**
 * @brief Reports whether overlapped-send cancellation cleanup should retry another completion probe.
 */
inline bool ShouldRetryOverlappedCleanupProbe(int nLastError, int nRemainingRetries)
{
	return nLastError == ERROR_IO_INCOMPLETE && nRemainingRetries > 0;
}

/**
 * @brief Reports whether one more packet can be accepted into a TCP send queue
 *        without exceeding the per-peer memory budget.
 */
inline bool CanQueueEMSocketPacket(
	const size_t nCurrentPackets,
	const uint64 nCurrentBytes,
	const uint32 nPacketBytes,
	const size_t nMaxPackets,
	const uint64 nMaxBytes)
{
	if (nCurrentPackets >= nMaxPackets)
		return false;
	if (nPacketBytes > nMaxBytes)
		return false;
	return nCurrentBytes <= nMaxBytes - nPacketBytes;
}

inline bool CanQueueEMSocketControlPacket(const size_t nCurrentPackets, const uint64 nCurrentBytes, const uint32 nPacketBytes)
{
	return CanQueueEMSocketPacket(
		nCurrentPackets,
		nCurrentBytes,
		nPacketBytes,
		kMaxEMSocketQueuedControlPackets,
		kMaxEMSocketQueuedControlBytes);
}

inline bool CanQueueEMSocketStandardPacket(const size_t nCurrentPackets, const uint64 nCurrentBytes, const uint32 nPacketBytes)
{
	return CanQueueEMSocketPacket(
		nCurrentPackets,
		nCurrentBytes,
		nPacketBytes,
		kMaxEMSocketQueuedStandardPackets,
		kMaxEMSocketQueuedStandardBytes);
}
