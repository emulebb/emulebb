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
 * @brief Chooses the persistent integer tag type without allowing large values to truncate.
 */
inline uint8_t SelectIntegerTagType(const uint64_t nValue, const bool bForceInt64)
{
	return (bForceInt64 || nValue > UINT32_MAX) ? TAGTYPE_UINT64 : TAGTYPE_UINT32;
}
}
