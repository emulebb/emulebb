#pragma once

#include <cstddef>
#include <cstdint>

#define EMULEBB_TEST_HAVE_UPNP_WRAPPER_SEAMS 1

enum ENatMappingBackendMode : uint8_t
{
	NAT_MAPPING_BACKEND_MODE_AUTOMATIC = 0,
	NAT_MAPPING_BACKEND_MODE_UPNP_IGD_ONLY = 1,
	NAT_MAPPING_BACKEND_MODE_PCP_NATPMP_ONLY = 2
};

enum ENatMappingBackend : uint8_t
{
	NAT_MAPPING_BACKEND_UPNP_IGD,
	NAT_MAPPING_BACKEND_PCP_NATPMP
};

struct NatMappingBackendOrder
{
	ENatMappingBackend aeBackends[2];
	size_t uCount;
};

inline void AppendNatMappingBackend(NatMappingBackendOrder &rOrder, ENatMappingBackend eBackend) noexcept
{
	if (rOrder.uCount < (sizeof(rOrder.aeBackends) / sizeof(rOrder.aeBackends[0])))
		rOrder.aeBackends[rOrder.uCount++] = eBackend;
}

inline NatMappingBackendOrder BuildNatMappingBackendOrder(uint8_t uBackendMode) noexcept
{
	NatMappingBackendOrder order = {};
	switch (uBackendMode) {
		case NAT_MAPPING_BACKEND_MODE_UPNP_IGD_ONLY:
			AppendNatMappingBackend(order, NAT_MAPPING_BACKEND_UPNP_IGD);
			break;
		case NAT_MAPPING_BACKEND_MODE_PCP_NATPMP_ONLY:
			AppendNatMappingBackend(order, NAT_MAPPING_BACKEND_PCP_NATPMP);
			break;
		case NAT_MAPPING_BACKEND_MODE_AUTOMATIC:
		default:
			AppendNatMappingBackend(order, NAT_MAPPING_BACKEND_UPNP_IGD);
			AppendNatMappingBackend(order, NAT_MAPPING_BACKEND_PCP_NATPMP);
			break;
	}

	return order;
}
