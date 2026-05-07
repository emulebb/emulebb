#pragma once

#define EMULE_TEST_HAVE_SERVER_UDP_SOCKET_FAILURE_SEAMS 1

/**
 * @brief Testable policy helpers for server UDP packet failure diagnostics.
 */
namespace UDPSocketSeams
{
/**
 * @brief Logging policy for server UDP packet processing failures.
 */
enum class EServerUdpPacketFailureLogPolicy
{
	VerboseOnly,
	Always
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
}
