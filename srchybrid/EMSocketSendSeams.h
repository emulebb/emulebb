#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
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

/**
 * @brief Computes the receive buffer allocation for one incoming TCP packet.
 *
 * CEMSocket caps peer-controlled packet payloads before constructing Packet,
 * but the allocation still needs its own checked +1 byte for the legacy
 * trailing slack. Keeping this as a boolean seam lets the receive loop close
 * the socket on impossible sizes or allocation pressure instead of unwinding
 * through socket callbacks.
 */
inline bool TryGetIncomingPacketAllocationSize(
	const uint32 nPayloadBytes,
	const size_t nMaxPayloadBytes,
	size_t *pnAllocationBytes)
{
	if (pnAllocationBytes == NULL)
		return false;
	if (nPayloadBytes > nMaxPayloadBytes || static_cast<size_t>(nPayloadBytes) > (std::numeric_limits<size_t>::max)() - 1u)
		return false;

	*pnAllocationBytes = static_cast<size_t>(nPayloadBytes) + 1u;
	return true;
}

/**
 * @brief Reports whether an overlapped send may reference an existing
 *        sendbuffer slice directly instead of allocating a copy.
 */
inline bool CanBorrowOverlappedSendBufferSlice(const uint32 nSentOffset, const uint32 nSliceBytes, const uint32 nBufferBytes)
{
	return nSliceBytes > 0 && nSentOffset <= nBufferBytes && nSliceBytes <= nBufferBytes - nSentOffset;
}

/**
 * @brief Detects WSABUF slices that borrow storage from the socket sendbuffer.
 */
inline bool IsBorrowedOverlappedSendBufferSlice(const char *pCandidate, const char *pSendBuffer, const uint32 nSendBufferBytes)
{
	if (pCandidate == NULL || pSendBuffer == NULL || nSendBufferBytes == 0)
		return false;
	const uintptr_t uCandidate = reinterpret_cast<uintptr_t>(pCandidate);
	const uintptr_t uStart = reinterpret_cast<uintptr_t>(pSendBuffer);
	return uCandidate >= uStart && uCandidate < uStart + nSendBufferBytes;
}
