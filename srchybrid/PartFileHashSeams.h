#pragma once

#include <cstdint>

#define EMULE_TESTS_HAS_PART_FILE_HASH_LAYOUT_GENERATION 1

/**
 * @brief Reports whether a completed hash worker result still matches the part file's theoretical hashset shape.
 */
inline bool HasMatchingPartFileHashLayout(uint32_t nExpectedGeneration, uint32_t nActualGeneration, uint16_t nExpectedMd4Parts, uint16_t nActualMd4Parts, uint16_t nExpectedAichParts, uint16_t nActualAichParts)
{
	return nExpectedGeneration == nActualGeneration
		&& nExpectedMd4Parts == nActualMd4Parts
		&& nExpectedAichParts == nActualAichParts;
}
