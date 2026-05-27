#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace EncryptedStreamSocketSeams
{
static const uint64_t kMaxNegotiationSendBufferBytes = 16ui64 * 1024ui64 * 1024ui64;

/**
 * Returns whether flushing a delayed server negotiation buffer should complete the
 * delayed-send state because no buffered negotiation data remains.
 */
inline bool ShouldCompleteDelayedServerSendAfterFlush(const bool bIsDelayedServerSendState, const bool bHasPendingNegotiationBuffer)
{
	return bIsDelayedServerSendState && !bHasPendingNegotiationBuffer;
}

/**
 * @brief Validates the caller-owned span passed into SendNegotiatingData.
 *
 * Negotiation send buffers sometimes include the first real payload to preserve
 * legacy frame coalescing. The guard therefore cannot be handshake-sized, but it
 * still rejects negative int lengths, impossible crypt start offsets, and spans
 * beyond the bounded delayed-send memory budget before malloc/casts see them.
 */
inline bool IsNegotiationSendSpanValid(const int nBufLen, const int nStartCryptFromByte)
{
	return nBufLen >= 0
		&& nStartCryptFromByte >= 0
		&& nStartCryptFromByte <= nBufLen
		&& static_cast<uint64_t>(nBufLen) <= kMaxNegotiationSendBufferBytes;
}

/**
 * @brief Reports whether a pending negotiation buffer may append more bytes.
 */
inline bool CanAppendNegotiationSendBuffer(const uint64_t uCurrentBytes, const uint32_t uAppendBytes)
{
	if (uAppendBytes > kMaxNegotiationSendBufferBytes)
		return false;
	return uCurrentBytes <= kMaxNegotiationSendBufferBytes - uAppendBytes;
}

/**
 * @brief Narrows a buffered negotiation length for send APIs and RC4 helpers.
 */
inline bool TryGetNegotiationSendBufferLength(const uint64_t uBufferedBytes, uint32_t *puBufferBytes)
{
	if (puBufferBytes == nullptr
		|| uBufferedBytes > static_cast<uint64_t>((std::numeric_limits<int>::max)())
		|| uBufferedBytes > kMaxNegotiationSendBufferBytes)
	{
		return false;
	}

	*puBufferBytes = static_cast<uint32_t>(uBufferedBytes);
	return true;
}
}
