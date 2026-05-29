#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#define EMULEBB_TEST_HAVE_SERVER_UDP_SOCKET_FAILURE_SEAMS 1
#define EMULEBB_TEST_HAVE_SERVER_UDP_DNS_SEAMS 1

/**
 * @brief Testable policy helpers for server UDP packet failure diagnostics.
 */
namespace UDPSocketSeams
{
static const size_t kMaxOutgoingServerUdpControlQueuePackets = 1024u;
static const size_t kMaxServerUdpDnsPacketsPerRequest = 128u;
static const uint64_t kMaxServerUdpDnsBytesPerRequest = 1024ui64 * 1024ui64;

/**
 * @brief Logging policy for server UDP packet processing failures.
 */
enum class EServerUdpPacketFailureLogPolicy
{
	VerboseOnly,
	Always
};

/**
 * @brief Completion states for server UDP hostname resolution.
 */
enum class EServerUdpDnsCompletion
{
	UnknownRequest,
	Failed,
	NoAddress,
	Resolved
};

/**
 * @brief Unexpected release exceptions should not be hidden behind verbose logging.
 */
inline EServerUdpPacketFailureLogPolicy GetPacketFailureLogPolicy(const bool bUnexpectedException)
{
	return bUnexpectedException ? EServerUdpPacketFailureLogPolicy::Always : EServerUdpPacketFailureLogPolicy::VerboseOnly;
}

/**
 * @brief Reports whether the current packet failure should be written to the log.
 */
inline bool ShouldLogPacketFailure(const bool bVerboseEnabled, const EServerUdpPacketFailureLogPolicy ePolicy)
{
	return bVerboseEnabled || ePolicy == EServerUdpPacketFailureLogPolicy::Always;
}

/**
 * @brief Classifies an asynchronous server UDP DNS completion before packet dispatch.
 */
inline EServerUdpDnsCompletion ClassifyDnsCompletion(const bool bKnownRequest, const bool bLookupSucceeded, const bool bHasIpv4Address, const uint32_t uNetworkOrderAddress)
{
	if (!bKnownRequest)
		return EServerUdpDnsCompletion::UnknownRequest;
	if (!bLookupSucceeded)
		return EServerUdpDnsCompletion::Failed;
	if (!bHasIpv4Address || uNetworkOrderAddress == 0xffffffffu)
		return EServerUdpDnsCompletion::NoAddress;
	return EServerUdpDnsCompletion::Resolved;
}

/**
 * @brief Reports whether another outgoing server UDP control packet may be
 *        queued without exceeding the memory budget.
 */
inline bool CanQueueOutgoingServerUdpControlPacket(const size_t uCurrentQueuedPackets)
{
	return uCurrentQueuedPackets < kMaxOutgoingServerUdpControlQueuePackets;
}

/**
 * @brief Computes one outgoing server UDP datagram span before allocation or int casts.
 *
 * Server UDP sends use the same two-byte protocol/opcode header as client UDP
 * and later store the queued send size in an int. Check the full plain packet,
 * optional obfuscation overhead, and socket-visible length together so a large
 * packet cannot wrap a heap allocation or truncate while queued for SendTo.
 */
inline bool TryGetOutgoingServerUdpPacketSize(
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

/**
 * @brief Reports whether a raw server UDP packet can be queued through SendTo.
 */
inline bool CanQueueRawServerUdpPacketSize(const uint32_t uRawPacketSize)
{
	return uRawPacketSize <= static_cast<uint32_t>((std::numeric_limits<int>::max)());
}

/**
 * @brief Reports whether an unresolved server DNS request may retain another raw packet.
 *
 * DNS-delayed packets do not pass through the normal outgoing control queue
 * until resolution completes. Budget both count and bytes here so repeated
 * sends to one unresolved hostname cannot accumulate raw heap buffers outside
 * the main throttled queue.
 */
inline bool CanQueueServerUdpDnsPacket(
	const size_t uCurrentPackets,
	const uint64_t uCurrentBytes,
	const uint32_t uPacketBytes)
{
	if (uCurrentPackets >= kMaxServerUdpDnsPacketsPerRequest)
		return false;
	if (uPacketBytes > kMaxServerUdpDnsBytesPerRequest)
		return false;
	return uCurrentBytes <= kMaxServerUdpDnsBytesPerRequest - uPacketBytes;
}
}
