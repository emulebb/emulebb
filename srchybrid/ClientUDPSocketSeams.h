#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#define EMULEBB_TEST_HAVE_CLIENT_UDP_SOCKET_FAILURE_SEAMS 1

/**
 * @brief Testable policy helpers for client UDP packet failure diagnostics.
 */
namespace ClientUDPSocketSeams
{
static const uint8_t kCurrentClientUdpVersion = 4u;
static const size_t kMaxOutgoingClientUdpControlQueuePackets = 4096u;
static const uint32_t kClientUdpQueueFullMargin = 50u;
static const uint64_t kClientUdpQueuePressureFixtureTtlMs = 30000u;

struct SClientUdpQueuePressureFixture
{
	bool bArmed = false;
	uint64_t ullFixtureId = 0;
	unsigned char aucFileHash[16] = {};
	uint32_t uSourceIp = 0;
	uint16_t uSourceUdpPort = 0;
	uint32_t uActualWaitingUserCount = 0;
	uint32_t uEffectiveWaitingUserCount = 0;
	uint32_t uQueueSize = 0;
	uint64_t ullExpiresAtMs = 0;
};

inline uint32_t ClientUdpQueuePressureWaitingUserCount(const uint32_t uQueueSize)
{
	return (uQueueSize > kClientUdpQueueFullMargin
		? uQueueSize - kClientUdpQueueFullMargin
		: 0u) + 1u;
}

inline void ArmClientUdpQueuePressureFixture(
	SClientUdpQueuePressureFixture &rFixture,
	const uint64_t ullFixtureId,
	const unsigned char *pFileHash,
	const uint32_t uSourceIp,
	const uint16_t uSourceUdpPort,
	const uint32_t uActualWaitingUserCount,
	const uint32_t uQueueSize,
	const uint64_t ullNowMs)
{
	rFixture = SClientUdpQueuePressureFixture();
	rFixture.bArmed = true;
	rFixture.ullFixtureId = ullFixtureId;
	std::memcpy(rFixture.aucFileHash, pFileHash, sizeof rFixture.aucFileHash);
	rFixture.uSourceIp = uSourceIp;
	rFixture.uSourceUdpPort = uSourceUdpPort;
	rFixture.uActualWaitingUserCount = uActualWaitingUserCount;
	rFixture.uEffectiveWaitingUserCount = ClientUdpQueuePressureWaitingUserCount(uQueueSize);
	rFixture.uQueueSize = uQueueSize;
	rFixture.ullExpiresAtMs = ullNowMs + kClientUdpQueuePressureFixtureTtlMs;
}

inline bool TryConsumeClientUdpQueuePressureFixture(
	SClientUdpQueuePressureFixture &rFixture,
	const unsigned char *pFileHash,
	const uint32_t uSourceIp,
	const uint16_t uSourceUdpPort,
	const uint32_t uQueueSize,
	const uint64_t ullNowMs,
	SClientUdpQueuePressureFixture &rConsumed)
{
	if (!rFixture.bArmed)
		return false;
	if (ullNowMs >= rFixture.ullExpiresAtMs) {
		rFixture = SClientUdpQueuePressureFixture();
		return false;
	}
	if (rFixture.uSourceIp != uSourceIp
		|| rFixture.uSourceUdpPort != uSourceUdpPort
		|| rFixture.uQueueSize != uQueueSize
		|| std::memcmp(rFixture.aucFileHash, pFileHash, sizeof rFixture.aucFileHash) != 0)
	{
		return false;
	}
	rConsumed = rFixture;
	rFixture = SClientUdpQueuePressureFixture();
	return true;
}

enum class EClientUdpPacketDisposition
{
	Accepted,
	Ignored,
	Dropped
};

enum class EClientUdpSocketSendResult
{
	Succeeded,
	Failed,
	Retry
};

inline EClientUdpSocketSendResult ClassifyClientUdpSocketSendResult(
	const int nSocketResult,
	const int nAttemptedLen)
{
	if (nSocketResult < 0)
		return EClientUdpSocketSendResult::Retry;
	return nSocketResult == nAttemptedLen
		? EClientUdpSocketSendResult::Succeeded
		: EClientUdpSocketSendResult::Failed;
}

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
 * @brief Reports whether DecryptReceivedClient removed an obfuscation envelope.
 *
 * ED2K client-UDP obfuscation uses the peer user hash and therefore does not
 * populate the Kad receiver/sender verify keys. The authoritative receive-side
 * signal is the decoded frame becoming shorter than the exact wire datagram.
 */
inline bool IsIncomingClientUdpDatagramObfuscated(const int nWireLen, const int nDecodedLen)
{
	return nWireLen > 0 && nDecodedLen > 0 && nDecodedLen < nWireLen;
}

/**
 * @brief Classifies queued outgoing diagnostics by their wire protocol.
 *
 * Buddy-relayed OP_REASKCALLBACKUDP packets use the Kad-routed send path but
 * still carry OP_EMULEPROT on the wire, so routing flags must not classify the
 * diagnostic family.
 */
inline bool IsOutgoingEd2kClientUdpDiagnosticPacket(
	const bool bHasPacket,
	const unsigned char byProtocolMarker)
{
	return bHasPacket && byProtocolMarker == 0xC5u;
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
