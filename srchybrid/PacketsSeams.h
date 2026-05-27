#pragma once

#include "Opcodes.h"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace PacketsSeams
{
/**
 * @brief Returns the allocation span for a TCP packet buffer.
 *
 * The packet class historically allocates "payload + 10" bytes even though the
 * serialized header is smaller. Keep that over-allocation for compatibility
 * with the legacy packet layout, but make the addition explicit so a future
 * caller cannot wrap a hostile or accidental UINT32_MAX-sized payload back into
 * a tiny heap allocation.
 */
inline bool TryGetTcpPacketAllocationSize(const uint32_t nPayloadSize, size_t *pnAllocationSize)
{
	if (pnAllocationSize == nullptr || nPayloadSize > (std::numeric_limits<uint32_t>::max)() - 10u)
		return false;

	*pnAllocationSize = static_cast<size_t>(nPayloadSize) + 10u;
	return true;
}

/**
 * @brief Narrows an arbitrary byte span to a TCP packet payload size.
 *
 * Some packet constructors are fed by MFC/string buffers whose length type is
 * wider than the 32-bit eD2K payload field. Check both the wire length
 * (payload + opcode) and the legacy payload + 10 allocation span before storing
 * the value in Packet::size; otherwise a future large in-memory producer could
 * truncate first and make the later allocation checks prove the wrong number.
 */
inline bool TryGetTcpPacketPayloadSizeFromSpan(const uint64_t nSpanBytes, uint32_t *pnPayloadSize)
{
	if (pnPayloadSize == nullptr || nSpanBytes > static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)()))
		return false;

	const uint32_t nPayloadSize = static_cast<uint32_t>(nSpanBytes);
	if (nPayloadSize > (std::numeric_limits<uint32_t>::max)() - 10u
		|| nPayloadSize == (std::numeric_limits<uint32_t>::max)()) {
		return false;
	}

	*pnPayloadSize = nPayloadSize;
	return true;
}

/**
 * @brief Returns the serialized packet length field without allowing wraparound.
 *
 * eD2K TCP headers store payload-plus-opcode in a 32-bit field. The payload is
 * also 32-bit, so UINT32_MAX is not representable once the opcode byte is added.
 */
inline bool TryGetTcpPacketLengthField(const uint32_t nPayloadSize, uint32_t *pnPacketLength)
{
	if (pnPacketLength == nullptr || nPayloadSize == (std::numeric_limits<uint32_t>::max)())
		return false;

	*pnPacketLength = nPayloadSize + 1u;
	return true;
}

/**
 * @brief Adds two packet payload spans while preserving the 32-bit wire limit.
 */
inline bool TryAddPacketPayloadSizes(const uint32_t nLeftPayloadSize, const size_t nRightPayloadSize, uint32_t *pnCombinedPayloadSize)
{
	if (pnCombinedPayloadSize == nullptr
		|| nRightPayloadSize > static_cast<size_t>((std::numeric_limits<uint32_t>::max)())
		|| nLeftPayloadSize > (std::numeric_limits<uint32_t>::max)() - static_cast<uint32_t>(nRightPayloadSize))
	{
		return false;
	}

	*pnCombinedPayloadSize = nLeftPayloadSize + static_cast<uint32_t>(nRightPayloadSize);
	return true;
}

/**
 * @brief Narrows an in-memory blob span to the protocol's 32-bit blob length.
 */
inline bool TryGetBlobPayloadSize(const size_t nBlobSize, uint32_t *pnBlobSize)
{
	if (pnBlobSize == nullptr || nBlobSize > static_cast<size_t>((std::numeric_limits<uint32_t>::max)()))
		return false;

	*pnBlobSize = static_cast<uint32_t>(nBlobSize);
	return true;
}

/**
 * @brief Narrows an arbitrary byte span to the raw packet payload field.
 */
inline bool TryGetRawPacketPayloadSizeFromSpan(const uint64_t nSpanBytes, uint32_t *pnPayloadSize)
{
	if (pnPayloadSize == nullptr || nSpanBytes > static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)()))
		return false;

	*pnPayloadSize = static_cast<uint32_t>(nSpanBytes);
	return true;
}

/**
 * @brief Returns zlib compression scratch size without integer wraparound.
 *
 * PackPacket historically asks zlib for payload + 300 bytes. Compression is an
 * optional size optimization, so callers can skip packing if this scratch span
 * is not representable instead of constructing a wrapped buffer and passing a
 * hostile length pair into zlib.
 */
inline bool TryGetPacketCompressionWorkSize(const uint32_t nPayloadSize, size_t *pnWorkSize)
{
	if (pnWorkSize == nullptr || nPayloadSize > (std::numeric_limits<uint32_t>::max)() - 300u)
		return false;

	*pnWorkSize = static_cast<size_t>(nPayloadSize) + 300u;
	return true;
}

/**
 * @brief Chooses the persistent integer tag type without allowing large values to truncate.
 */
inline uint8_t SelectIntegerTagType(const uint64_t nValue, const bool bForceInt64)
{
	return (bForceInt64 || nValue > UINT32_MAX) ? TAGTYPE_UINT64 : TAGTYPE_UINT32;
}
}
