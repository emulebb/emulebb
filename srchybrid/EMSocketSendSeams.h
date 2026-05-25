#pragma once

#include "types.h"

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
