#pragma once

#include <cstddef>

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
}
