#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#define EMULEBB_SOCKET_IO_SEAMS_HAS_BROADBAND_TCP_UPLOAD_BUFFER_HELPER 1

inline constexpr std::uint32_t kBroadbandUdpReceiveBufferBytes = 1024u * 1024u;
inline constexpr std::uint32_t kBroadbandTcpUploadSendBufferBytes = 32u * 1024u * 1024u;

/**
 * @brief Computes the TCP send-buffer target for an upload slot from the configured per-slot upload budget.
 */
inline std::uint32_t GetBroadbandTcpUploadSendBufferBytes(
	std::uint32_t uTargetPerSlotBytesPerSec,
	std::uint32_t uTargetBufferSeconds = 8u,
	std::uint32_t uMinimumBufferBytes = 1024u * 1024u,
	std::uint32_t uMaximumBufferBytes = kBroadbandTcpUploadSendBufferBytes)
{
	if (uTargetPerSlotBytesPerSec == 0 || uTargetBufferSeconds == 0)
		return uMinimumBufferBytes;

	const std::uint64_t ullTargetBytes = static_cast<std::uint64_t>(uTargetPerSlotBytesPerSec) * uTargetBufferSeconds;
	if (ullTargetBytes < uMinimumBufferBytes)
		return uMinimumBufferBytes;
	if (ullTargetBytes > uMaximumBufferBytes)
		return uMaximumBufferBytes;
	return static_cast<std::uint32_t>(ullTargetBytes);
}

/**
 * @brief Reports whether a socket receive result fits inside the requested read buffer span.
 */
inline bool HasValidSocketReceiveResult(int nReceiveResult, size_t nRequestedBytes)
{
	return nReceiveResult >= 0 && static_cast<size_t>(nReceiveResult) <= nRequestedBytes;
}

/**
 * @brief Clamps consumed receive-budget bytes so limited receive loops cannot underflow.
 */
inline std::uint32_t ClampSocketReceiveBudget(std::uint32_t nBudget, int nConsumedBytes)
{
	if (nConsumedBytes <= 0)
		return nBudget;

	const std::uint32_t nConsumed = static_cast<std::uint32_t>(nConsumedBytes);
	return nConsumed >= nBudget ? 0u : nBudget - nConsumed;
}

/**
 * @brief Validates a socket send result before the caller advances its buffer offsets.
 */
inline bool TryAccumulateSocketSendProgress(std::uint32_t nCurrentSent, std::uint32_t nRequestedSend, std::uint32_t nBufferLength, std::uint32_t nSendResult, std::uint32_t *pnNextSent)
{
	if (pnNextSent == nullptr || nRequestedSend == 0 || nSendResult == 0 || nSendResult > nRequestedSend)
		return false;
	if (nCurrentSent > nBufferLength || nSendResult > nBufferLength - nCurrentSent)
		return false;
	if (nCurrentSent > (std::numeric_limits<std::uint32_t>::max)() - nSendResult)
		return false;

	*pnNextSent = nCurrentSent + nSendResult;
	return true;
}
