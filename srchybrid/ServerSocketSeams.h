#pragma once

#include <cstddef>
#include <cstdint>

#define EMULEBB_TEST_HAVE_SERVER_SOCKET_FAILURE_POLICY_SEAMS 1

/**
 * @brief Testable policy helpers for server TCP packet failure handling.
 */
namespace ServerSocketSeams
{
/** @brief Server TCP protocol and opcode values used by the failure policy seam. */
constexpr std::uint8_t kServerProtocolEdonkey = 0xE3u;
constexpr std::uint8_t kServerOpcodeReject = 0x05u;
constexpr std::uint8_t kServerOpcodeSearchResult = 0x33u;
constexpr std::uint8_t kServerOpcodeFoundSources = 0x42u;

/**
 * @brief Release-mode outcome after a server packet processing failure.
 */
enum class EServerPacketFailureAction
{
	KeepConnection,
	Disconnect
};

/**
 * @brief Keeps historically recoverable server packet failures from dropping the TCP connection.
 */
inline EServerPacketFailureAction GetProcessPacketFailureAction(const std::uint8_t opcode)
{
	switch (opcode) {
	case kServerOpcodeSearchResult:
	case kServerOpcodeFoundSources:
		return EServerPacketFailureAction::KeepConnection;
	default:
		return EServerPacketFailureAction::Disconnect;
	}
}

/**
 * @brief Reports whether a failed compressed-packet unpack should be treated as consumed input.
 */
inline bool ShouldConsumePackedPacketUnpackFailure()
{
	return true;
}

/**
 * @brief Safely reads a packet field for failure logs when the packet pointer may be null.
 */
template <typename TPacket, typename TValue>
inline TValue GetPacketFieldOrDefault(const TPacket *pPacket, TValue TPacket::*pMember, const TValue defaultValue)
{
	return pPacket != NULL ? pPacket->*pMember : defaultValue;
}
}
