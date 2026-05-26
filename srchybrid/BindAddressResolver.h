#pragma once

#include <vector>

struct BindableNetworkInterface
{
	CString strId;
	CString strName;
	CString strDisplayName;
	DWORD dwIpv4IfIndex = 0;
	std::vector<CString> addresses;
};

enum EBindAddressResolveResult
{
	BARR_Default = 0,
	BARR_Resolved,
	BARR_InterfaceNotFound,
	BARR_InterfaceNameAmbiguous,
	BARR_InterfaceHasNoAddress,
	BARR_AddressNotFoundOnInterface,
	BARR_AddressNotFound
};

class CBindAddressResolver
{
public:
	// Enumerates the IPv4-capable interfaces which can be offered as bind targets in the UI.
	static std::vector<BindableNetworkInterface> GetBindableInterfaces();

	// Resolves the stored interface/address selection to the concrete IPv4 address used at runtime.
	static EBindAddressResolveResult ResolveBindAddress(const CString &strInterfaceName
		, const CString &strConfiguredAddress
		, CString &strResolvedAddress
		, CString *pstrResolvedInterfaceName = NULL
		, DWORD *pdwResolvedIpv4IfIndex = NULL);
};
