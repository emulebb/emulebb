#pragma once

#include "Opcodes.h"

#include <cstdint>

namespace PacketsSeams
{
/**
 * @brief Chooses the persistent integer tag type without allowing large values to truncate.
 */
inline uint8_t SelectIntegerTagType(const uint64_t nValue, const bool bForceInt64)
{
	return (bForceInt64 || nValue > UINT32_MAX) ? TAGTYPE_UINT64 : TAGTYPE_UINT32;
}
}
