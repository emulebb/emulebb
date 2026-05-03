#pragma once

#include <cstdint>

namespace SearchParamsPolicy
{
constexpr uint8_t kDefaultSearchType = 1u;
constexpr uint8_t kMaxSupportedSearchType = 3u;

/**
 * @brief Maps persisted search-method ids onto the currently supported search methods.
 *
 * Older profiles may still contain a removed legacy search-method id. Those
 * stale ids now fall back to the standard server search method instead of
 * reaching dead UI branches.
 */
inline uint8_t NormalizeStoredSearchType(const int iStoredSearchType)
{
	return iStoredSearchType >= 0 && iStoredSearchType <= kMaxSupportedSearchType
		? static_cast<uint8_t>(iStoredSearchType)
		: kDefaultSearchType;
}
}
