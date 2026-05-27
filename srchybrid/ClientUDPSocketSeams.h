#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#define EMULE_TEST_HAVE_CLIENT_UDP_SOCKET_FAILURE_SEAMS 1

/**
 * @brief Testable policy helpers for client UDP packet failure diagnostics.
 */
namespace ClientUDPSocketSeams
{
static const size_t kMaxOutgoingClientUdpControlQueuePackets = 4096u;

/**
 * @brief Logging policy for packet failures after UDP decryption succeeds.
 */
enum class EUdpPacketFailureLogPolicy
{
	VerboseOnly,
	Always
};

/**
 * @brief Unexpected release exceptions should not be hidden behind verbose logging.
 */
inline EUdpPacketFailureLogPolicy GetPacketFailureLogPolicy(const bool bUnexpectedException)
{
	return bUnexpectedException ? EUdpPacketFailureLogPolicy::Always : EUdpPacketFailureLogPolicy::VerboseOnly;
}

/**
 * @brief Reports whether the current packet failure should be written to the log.
 */
inline bool ShouldLogPacketFailure(const bool bVerboseEnabled, const EUdpPacketFailureLogPolicy ePolicy)
{
	return bVerboseEnabled || ePolicy == EUdpPacketFailureLogPolicy::Always;
}

/**
 * @brief Reads the UDP opcode for diagnostics only when the decrypted packet
 *        contains the protocol and opcode bytes.
 */
inline bool TryGetPacketOpcodeForLog(const unsigned char *pBuffer, const int nPacketLen, unsigned char &rOpcode)
{
	if (pBuffer == nullptr || nPacketLen < 2)
		return false;
	rOpcode = pBuffer[1];
	return true;
}

/**
 * @brief Release 1 outgoing client UDP encryption policy.
 *
 * Client UDP obfuscation follows the global crypt-layer preference for both
 * ED2K peer UDP and Kad UDP sends. Inbound encrypted datagrams remain accepted
 * by the encrypted datagram framing path, but disabling the global crypt layer
 * keeps new outgoing client UDP sends plain.
 */
inline bool ShouldQueueOutgoingClientUdpEncryption(
	const bool bCryptLayerEnabled,
	const bool bEncryptionRequested,
	const bool bHasTargetClientHash,
	const bool bKad,
	const unsigned int uReceiverVerifyKey)
{
	return bCryptLayerEnabled
		&& bEncryptionRequested
		&& (bHasTargetClientHash || (bKad && uReceiverVerifyKey != 0u));
}

/**
 * @brief Reports whether a queued UDP packet should reserve/apply encryption overhead.
 */
inline bool ShouldApplyOutgoingClientUdpEncryptionOverhead(
	const bool bQueuedForEncryption,
	const bool bCryptLayerEnabled,
	const bool bHasPublicIP,
	const bool bKad)
{
	return bQueuedForEncryption && bCryptLayerEnabled && (bHasPublicIP || bKad);
}

/**
 * @brief Reports whether another outgoing client/Kad UDP control packet may
 *        be queued without exceeding the memory budget.
 */
inline bool CanQueueOutgoingClientUdpControlPacket(const size_t uCurrentQueuedPackets)
{
	return uCurrentQueuedPackets < kMaxOutgoingClientUdpControlQueuePackets;
}

/**
 * @brief Computes one outgoing UDP datagram span before allocation or int casts.
 *
 * The UDP send path eventually hands an int length to Winsock and historically
 * formed that length with packet->size + 2. Keep the eMule UDP wire shape
 * unchanged, but reject spans that cannot represent the protocol/opcode header,
 * optional obfuscation overhead, and final socket length in the same number.
 */
inline bool TryGetOutgoingClientUdpPacketSize(
	const uint32_t uPayloadSize,
	const size_t uEncryptionOverhead,
	uint32_t *puPlainPacketSize,
	size_t *puAllocationSize,
	int *pnSocketSendSize)
{
	if (puPlainPacketSize == nullptr || puAllocationSize == nullptr || pnSocketSendSize == nullptr)
		return false;
	if (uPayloadSize > (std::numeric_limits<uint32_t>::max)() - 2u)
		return false;

	const uint32_t uPlainPacketSize = uPayloadSize + 2u;
	if (uEncryptionOverhead > static_cast<size_t>((std::numeric_limits<int>::max)())
		|| static_cast<size_t>(uPlainPacketSize) > static_cast<size_t>((std::numeric_limits<int>::max)()) - uEncryptionOverhead)
	{
		return false;
	}

	*puPlainPacketSize = uPlainPacketSize;
	*puAllocationSize = static_cast<size_t>(uPlainPacketSize) + uEncryptionOverhead;
	*pnSocketSendSize = static_cast<int>(uPlainPacketSize);
	return true;
}
}
