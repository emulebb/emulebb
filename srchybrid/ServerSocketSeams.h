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
constexpr std::uint8_t kServerProtocolPacked = 0xD4u;
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

struct ServerPacketFrameView
{
	std::uint8_t protocol;
	std::uint8_t opcode;
	const std::uint8_t *payload;
	std::uint32_t payloadSize;
};

inline bool TryReadEmbeddedTcpFrame(
	const std::uint8_t *buffer,
	const size_t bufferSize,
	size_t *offset,
	ServerPacketFrameView *frame)
{
	if (buffer == nullptr || offset == nullptr || frame == nullptr || *offset > bufferSize)
		return false;
	const size_t start = *offset;
	if (bufferSize - start < 6u)
		return false;
	const std::uint8_t protocol = buffer[start];
	if (protocol != kServerProtocolEdonkey && protocol != kServerProtocolPacked)
		return false;
	const std::uint32_t packetLength = static_cast<std::uint32_t>(buffer[start + 1u])
		| (static_cast<std::uint32_t>(buffer[start + 2u]) << 8u)
		| (static_cast<std::uint32_t>(buffer[start + 3u]) << 16u)
		| (static_cast<std::uint32_t>(buffer[start + 4u]) << 24u);
	if (packetLength < 1u || packetLength > bufferSize - start - 5u)
		return false;
	frame->protocol = protocol;
	frame->opcode = buffer[start + 5u];
	frame->payload = buffer + start + 6u;
	frame->payloadSize = packetLength - 1u;
	*offset = start + 5u + packetLength;
	return true;
}

inline bool IsCompleteEmbeddedTcpFrameBuffer(const std::uint8_t *buffer, const size_t bufferSize)
{
	size_t offset = 0;
	ServerPacketFrameView frame = {};
	bool foundFrame = false;
	while (offset < bufferSize) {
		if (!TryReadEmbeddedTcpFrame(buffer, bufferSize, &offset, &frame))
			return false;
		foundFrame = true;
	}
	return foundFrame && offset == bufferSize;
}

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
