#include "stdafx.h"

#include "BuildFeatures.h"
#if EMULEBB_HAS_PROTOCOL_TEST_CONTROL
#include "WebServerJsonSeams.h"
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

bool SelfTest()
{
#if EMULEBB_HAS_PROTOCOL_TEST_CONTROL
	using namespace WebServerJsonSeams;
	static const char *const kRoute = "/api/v1/diagnostics/protocol-fixtures/queued-lowid-sources";
	static const char *const kValidBody =
		"{\"fileHash\":\"00112233445566778899aabbccddeeff\","
		"\"sourceIp\":\"127.0.0.2\",\"sourceTcpPort\":4662,\"sourceUdpPort\":4672,"
		"\"sourceUserHash\":\"102132435465768798a9bacbdcedfe0f\",\"sourceUdpVersion\":4,"
		"\"buddyIp\":\"127.0.0.3\",\"buddyUdpPort\":4673,"
		"\"buddyId\":\"ffeeddccbbaa99887766554433221100\"}";

	SApiRoute route;
	std::string strErrorCode;
	std::string strErrorMessage;
	if (!TryBuildRoute("POST", kRoute, kValidBody, route, strErrorCode, strErrorMessage)
		|| route.strCommand != "diagnostics/protocol_fixture/queued_lowid_source"
		|| route.params.size() != 9
		|| GetLifecyclePolicyForCommand(route.strCommand) != ERestLifecyclePolicy::DiagnosticUnsafe)
	{
		return false;
	}

	const char *const kInvalidBodies[] = {
		"{\"fileHash\":\"00112233445566778899aabbccddeeff\"}",
		"{\"fileHash\":\"00112233445566778899AABBCCDDEEFF\",\"sourceIp\":\"127.0.0.2\",\"sourceTcpPort\":4662,\"sourceUdpPort\":4672,\"sourceUserHash\":\"102132435465768798a9bacbdcedfe0f\",\"sourceUdpVersion\":4,\"buddyIp\":\"127.0.0.3\",\"buddyUdpPort\":4673,\"buddyId\":\"ffeeddccbbaa99887766554433221100\"}",
		"{\"fileHash\":\"00112233445566778899aabbccddeeff\",\"sourceIp\":\"127.0.0.2\",\"sourceTcpPort\":4662,\"sourceUdpPort\":4672,\"sourceUserHash\":\"102132435465768798a9bacbdcedfe0f\",\"sourceUdpVersion\":5,\"buddyIp\":\"127.0.0.3\",\"buddyUdpPort\":4673,\"buddyId\":\"ffeeddccbbaa99887766554433221100\"}",
		"{\"fileHash\":\"00112233445566778899aabbccddeeff\",\"sourceIp\":\"127.000.0.2\",\"sourceTcpPort\":4662,\"sourceUdpPort\":4672,\"sourceUserHash\":\"102132435465768798a9bacbdcedfe0f\",\"sourceUdpVersion\":4,\"buddyIp\":\"127.0.0.3\",\"buddyUdpPort\":4673,\"buddyId\":\"ffeeddccbbaa99887766554433221100\"}",
		"{\"fileHash\":\"00112233445566778899aabbccddeeff\",\"sourceIp\":\"127.0.0.2\",\"sourceTcpPort\":4662,\"sourceUdpPort\":4672,\"sourceUserHash\":\"102132435465768798a9bacbdcedfe0f\",\"sourceUdpVersion\":4,\"buddyIp\":\"127.0.0.3\",\"buddyUdpPort\":4673,\"buddyId\":\"ffeeddccbbaa99887766554433221100\",\"extra\":true}"
	};
	for (const char *pszBody : kInvalidBodies) {
		SApiRoute invalidRoute;
		if (TryBuildRoute("POST", kRoute, pszBody, invalidRoute, strErrorCode, strErrorMessage)
			|| strErrorCode != "INVALID_ARGUMENT")
		{
			return false;
		}
	}

	SApiRoute buddyRelayRoute;
	if (!TryBuildRoute(
			"POST",
			"/api/v1/diagnostics/protocol-fixtures/buddy-relay-eligibility",
			"",
			buddyRelayRoute,
			strErrorCode,
			strErrorMessage)
		|| buddyRelayRoute.strCommand != "diagnostics/protocol_fixture/buddy_relay_eligibility"
		|| !buddyRelayRoute.params.empty()
		|| GetLifecyclePolicyForCommand(buddyRelayRoute.strCommand) != ERestLifecyclePolicy::DiagnosticUnsafe)
	{
		return false;
	}
#endif
	return true;
}
