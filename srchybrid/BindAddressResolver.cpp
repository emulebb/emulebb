//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( devs@emule-project.net / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#include "stdafx.h"
#include <iphlpapi.h>
#include "BindAddressResolver.h"
#include "OtherFunctions.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	/**
	 * @brief Converts one sockaddr into an IPv4 string if possible.
	 */
	static CString GetIPv4String(const SOCKADDR *pSockAddr)
	{
		if (pSockAddr == NULL || pSockAddr->sa_family != AF_INET)
			return CString();

		const SOCKADDR_IN *pSockAddrIn = reinterpret_cast<const SOCKADDR_IN*>(pSockAddr);
		return ipstr(pSockAddrIn->sin_addr);
	}

	/**
	 * @brief Appends one IPv4 address only once while preserving discovery order.
	 */
	static void AddUniqueAddress(std::vector<CString> &addresses, const CString &strAddress)
	{
		if (strAddress.IsEmpty())
			return;

		for (std::vector<CString>::const_iterator it = addresses.begin(); it != addresses.end(); ++it) {
			if (!it->CompareNoCase(strAddress))
				return;
		}
		addresses.push_back(strAddress);
	}
}

std::vector<BindableNetworkInterface> CBindAddressResolver::GetBindableInterfaces()
{
	std::vector<BindableNetworkInterface> interfaces;

	ULONG ulOutBufLen = 16 * 1024;
	std::vector<BYTE> buffer(ulOutBufLen);
	ULONG ulFlags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
	DWORD dwResult = ERROR_BUFFER_OVERFLOW;

	for (int iRetry = 0; iRetry < 3 && dwResult == ERROR_BUFFER_OVERFLOW; ++iRetry) {
		dwResult = GetAdaptersAddresses(AF_INET, ulFlags, NULL
			, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &ulOutBufLen);
		if (dwResult == ERROR_BUFFER_OVERFLOW)
			buffer.resize(ulOutBufLen);
	}

	if (dwResult != NO_ERROR)
		return interfaces;

	for (const IP_ADAPTER_ADDRESSES *pAdapter = reinterpret_cast<const IP_ADAPTER_ADDRESSES*>(buffer.data())
		; pAdapter != NULL; pAdapter = pAdapter->Next) {
		if (pAdapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK || pAdapter->OperStatus != IfOperStatusUp)
			continue;

		BindableNetworkInterface iface;
		iface.strId = CString(CA2T(pAdapter->AdapterName));
		iface.strName = pAdapter->FriendlyName != NULL ? CString(pAdapter->FriendlyName) : CString();
		iface.strName.Trim();
		iface.dwIpv4IfIndex = pAdapter->IfIndex;

		for (const IP_ADAPTER_UNICAST_ADDRESS *pAddress = pAdapter->FirstUnicastAddress
			; pAddress != NULL; pAddress = pAddress->Next) {
			AddUniqueAddress(iface.addresses, GetIPv4String(pAddress->Address.lpSockaddr));
		}

		iface.strDisplayName = iface.strName.IsEmpty() ? iface.strId : iface.strName;
		if (!iface.addresses.empty())
			iface.strDisplayName.AppendFormat(_T(" (%s)"), (LPCTSTR)iface.addresses.front());

		interfaces.push_back(iface);
	}

	return interfaces;
}

EBindAddressResolveResult CBindAddressResolver::ResolveBindAddress(const CString &strInterfaceName
	, const CString &strConfiguredAddress
	, CString &strResolvedAddress
	, CString *pstrResolvedInterfaceName
	, DWORD *pdwResolvedIpv4IfIndex)
{
	strResolvedAddress.Empty();
	if (pstrResolvedInterfaceName != NULL)
		pstrResolvedInterfaceName->Empty();
	if (pdwResolvedIpv4IfIndex != NULL)
		*pdwResolvedIpv4IfIndex = 0;

	CString strWantedInterface(strInterfaceName);
	strWantedInterface.Trim();
	CString strWantedAddress(strConfiguredAddress);
	strWantedAddress.Trim();

	const std::vector<BindableNetworkInterface> interfaces = GetBindableInterfaces();
	if (strWantedInterface.IsEmpty()) {
		if (strWantedAddress.IsEmpty())
			return BARR_Default;

		for (std::vector<BindableNetworkInterface>::const_iterator it = interfaces.begin(); it != interfaces.end(); ++it) {
			for (std::vector<CString>::const_iterator itAddress = it->addresses.begin(); itAddress != it->addresses.end(); ++itAddress) {
				if (itAddress->CompareNoCase(strWantedAddress))
					continue;

				strResolvedAddress = *itAddress;
				if (pstrResolvedInterfaceName != NULL)
					*pstrResolvedInterfaceName = it->strName;
				if (pdwResolvedIpv4IfIndex != NULL)
					*pdwResolvedIpv4IfIndex = it->dwIpv4IfIndex;
				return BARR_Resolved;
			}
		}
		return BARR_AddressNotFound;
	}

	const BindableNetworkInterface *pMatchedInterface = NULL;
	for (std::vector<BindableNetworkInterface>::const_iterator it = interfaces.begin(); it != interfaces.end(); ++it) {
		CString strCandidateName(it->strName);
		strCandidateName.Trim();
		if (strCandidateName.CompareNoCase(strWantedInterface))
			continue;

		if (pMatchedInterface != NULL)
			return BARR_InterfaceNameAmbiguous;
		pMatchedInterface = &(*it);
	}

	if (pMatchedInterface == NULL)
		return BARR_InterfaceNotFound;

	if (pstrResolvedInterfaceName != NULL)
		*pstrResolvedInterfaceName = pMatchedInterface->strName;

	if (pMatchedInterface->addresses.empty())
		return BARR_InterfaceHasNoAddress;

	if (strWantedAddress.IsEmpty()) {
		strResolvedAddress = pMatchedInterface->addresses.front();
		if (pdwResolvedIpv4IfIndex != NULL)
			*pdwResolvedIpv4IfIndex = pMatchedInterface->dwIpv4IfIndex;
		return BARR_Resolved;
	}

	for (std::vector<CString>::const_iterator itAddress = pMatchedInterface->addresses.begin(); itAddress != pMatchedInterface->addresses.end(); ++itAddress) {
		if (!itAddress->CompareNoCase(strWantedAddress)) {
			strResolvedAddress = *itAddress;
			if (pdwResolvedIpv4IfIndex != NULL)
				*pdwResolvedIpv4IfIndex = pMatchedInterface->dwIpv4IfIndex;
			return BARR_Resolved;
		}
	}

	return BARR_AddressNotFoundOnInterface;
}
