#pragma once

#include <ws2tcpip.h>

namespace BindInterfaceSocketSeams
{
	/**
	 * @brief Reports whether a resolved bind interface should enforce IPv4 unicast egress.
	 */
	inline bool ShouldApplyIpv4UnicastInterfaceOption(const bool bHasExplicitBindInterface
		, const bool bBindAddressResolved
		, const DWORD dwIpv4IfIndex)
	{
		return bHasExplicitBindInterface && bBindAddressResolved && dwIpv4IfIndex != 0;
	}

	/**
	 * @brief Applies Windows IPv4 outgoing-interface selection to a socket when bind policy requires it.
	 */
	inline bool ApplyIpv4UnicastInterfaceOption(const SOCKET hSocket
		, const ADDRESS_FAMILY nFamily
		, const bool bHasExplicitBindInterface
		, const bool bBindAddressResolved
		, const DWORD dwIpv4IfIndex
		, int *pnError)
	{
		if (pnError != NULL)
			*pnError = 0;
		if (nFamily != AF_INET || !ShouldApplyIpv4UnicastInterfaceOption(bHasExplicitBindInterface, bBindAddressResolved, dwIpv4IfIndex))
			return true;

		const DWORD dwNetworkOrderIfIndex = htonl(dwIpv4IfIndex);
		if (setsockopt(hSocket, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<const char*>(&dwNetworkOrderIfIndex), sizeof dwNetworkOrderIfIndex) == 0)
			return true;

		if (pnError != NULL)
			*pnError = WSAGetLastError();
		return false;
	}
}
