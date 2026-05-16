#pragma once

#include <cstdint>

#define EMULE_BB_SEARCH_NETWORK_DEFAULTS 1

namespace SearchParamsPolicy
{
constexpr uint8_t kDefaultSearchType = 1u;
constexpr uint8_t kAutomaticSearchType = 0u;
constexpr uint8_t kEd2kServerSearchType = 1u;
constexpr uint8_t kEd2kGlobalSearchType = 2u;
constexpr uint8_t kKadSearchType = 3u;
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

/**
 * @brief Resolves the live default search method from connected network state.
 *
 * eD2K connectivity prefers global server search. Kad is selected only when it
 * is the sole connected search network. With no connected network, Automatic is
 * kept unresolved so the caller can show the normal not-connected failure.
 */
inline uint8_t ResolveAutomaticSearchType(const bool bEd2kConnected, const bool bKadConnected)
{
	if (bEd2kConnected)
		return kEd2kGlobalSearchType;
	if (bKadConnected)
		return kKadSearchType;
	return kAutomaticSearchType;
}
}
