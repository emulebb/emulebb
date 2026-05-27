#pragma once

#include <cstddef>
#include <cstdint>

#define EMULE_TEST_HAVE_SERVER_UDP_SOCKET_FAILURE_SEAMS 1
#define EMULE_TEST_HAVE_SERVER_UDP_DNS_SEAMS 1

/**
 * @brief Testable policy helpers for server UDP packet failure diagnostics.
 */
namespace UDPSocketSeams
{
static const size_t kMaxOutgoingServerUdpControlQueuePackets = 1024u;

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
}
