#pragma once

#define EMULE_TEST_HAVE_CLIENT_UDP_SOCKET_FAILURE_SEAMS 1

/**
 * @brief Testable policy helpers for client UDP packet failure diagnostics.
 */
namespace ClientUDPSocketSeams
{
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
}
