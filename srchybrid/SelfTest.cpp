#include "stdafx.h"

#include "BuildFeatures.h"
#if EMULEBB_HAS_PROTOCOL_TEST_CONTROL
#include "AppCommandLineSeams.h"
#include "ClientUDPSocketSeams.h"
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
	const AppCommandLineSeams::SParseResult selectedUdpVersion =
		AppCommandLineSeams::ParseTokens({
			_T("emulebb-diagnostics.exe"),
			_T("--diagnostic-client-udp-version=3")
		});
	const AppCommandLineSeams::SParseResult invalidUdpVersion =
		AppCommandLineSeams::ParseTokens({
			_T("emulebb-diagnostics.exe"),
			_T("--diagnostic-client-udp-version=03")
		});
	if (selectedUdpVersion.eMode != AppCommandLineSeams::EMode::NormalStartup
		|| !selectedUdpVersion.bHasDiagnosticClientUdpVersion
		|| selectedUdpVersion.byDiagnosticClientUdpVersion != 3
		|| invalidUdpVersion.eMode != AppCommandLineSeams::EMode::Invalid)
	{
		return false;
	}
	if (ClientUDPSocketSeams::IsIncomingClientUdpDatagramObfuscated(23, 23)
		|| !ClientUDPSocketSeams::IsIncomingClientUdpDatagramObfuscated(31, 23)
		|| ClientUDPSocketSeams::IsIncomingClientUdpDatagramObfuscated(31, 0)
		|| ClientUDPSocketSeams::IsIncomingClientUdpDatagramObfuscated(8, 9))
	{
		return false;
	}
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

	static const char *const kReaskControlRoute =
		"/api/v1/diagnostics/protocol-controls/queued-source-reasks";
	static const char *const kValidReaskControlBody =
		"{\"fileHash\":\"00112233445566778899aabbccddeeff\","
		"\"sourceIp\":\"127.0.0.2\",\"sourceTcpPort\":4662,\"sourceUdpPort\":4672,"
		"\"sourceUserHash\":\"102132435465768798a9bacbdcedfe0f\","
		"\"sourceUdpVersion\":4}";
	SApiRoute reaskControlRoute;
	if (!TryBuildRoute(
			"POST",
			kReaskControlRoute,
			kValidReaskControlBody,
			reaskControlRoute,
			strErrorCode,
			strErrorMessage)
		|| reaskControlRoute.strCommand != "diagnostics/protocol_control/queued_source_reask"
		|| reaskControlRoute.params.size() != 6
		|| GetLifecyclePolicyForCommand(reaskControlRoute.strCommand) != ERestLifecyclePolicy::DiagnosticUnsafe)
	{
		return false;
	}

	const char *const kInvalidReaskControlBodies[] = {
		"{\"fileHash\":\"00112233445566778899aabbccddeeff\"}",
		"{\"fileHash\":\"00112233445566778899AABBCCDDEEFF\",\"sourceIp\":\"127.0.0.2\",\"sourceTcpPort\":4662,\"sourceUdpPort\":4672,\"sourceUserHash\":\"102132435465768798a9bacbdcedfe0f\",\"sourceUdpVersion\":4}",
		"{\"fileHash\":\"00112233445566778899aabbccddeeff\",\"sourceIp\":\"127.0.0.2\",\"sourceTcpPort\":4662,\"sourceUdpPort\":4672,\"sourceUserHash\":\"102132435465768798a9bacbdcedfe0f\",\"sourceUdpVersion\":5}",
		"{\"fileHash\":\"00112233445566778899aabbccddeeff\",\"sourceIp\":\"127.000.0.2\",\"sourceTcpPort\":4662,\"sourceUdpPort\":4672,\"sourceUserHash\":\"102132435465768798a9bacbdcedfe0f\",\"sourceUdpVersion\":4}",
		"{\"fileHash\":\"00112233445566778899aabbccddeeff\",\"sourceIp\":\"127.0.0.2\",\"sourceTcpPort\":4662,\"sourceUdpPort\":4672,\"sourceUserHash\":\"102132435465768798a9bacbdcedfe0f\",\"sourceUdpVersion\":4,\"extra\":true}"
	};
	for (const char *pszBody : kInvalidReaskControlBodies) {
		SApiRoute invalidRoute;
		if (TryBuildRoute(
				"POST",
				kReaskControlRoute,
				pszBody,
				invalidRoute,
				strErrorCode,
				strErrorMessage)
			|| strErrorCode != "INVALID_ARGUMENT")
		{
			return false;
		}
	}

	static const char *const kQueuePressureRoute =
		"/api/v1/diagnostics/protocol-fixtures/client-udp-queue-pressure";
	static const char *const kValidQueuePressureBody =
		"{\"fileHash\":\"00112233445566778899aabbccddeeff\","
		"\"sourceIp\":\"127.0.0.2\",\"sourceUdpPort\":4672}";
	SApiRoute queuePressureRoute;
	if (!TryBuildRoute(
			"POST",
			kQueuePressureRoute,
			kValidQueuePressureBody,
			queuePressureRoute,
			strErrorCode,
			strErrorMessage)
		|| queuePressureRoute.strCommand != "diagnostics/protocol_fixture/client_udp_queue_pressure"
		|| queuePressureRoute.params.size() != 3
		|| GetLifecyclePolicyForCommand(queuePressureRoute.strCommand) != ERestLifecyclePolicy::DiagnosticUnsafe)
	{
		return false;
	}

	const char *const kInvalidQueuePressureBodies[] = {
		"{\"fileHash\":\"00112233445566778899aabbccddeeff\"}",
		"{\"fileHash\":\"00112233445566778899AABBCCDDEEFF\",\"sourceIp\":\"127.0.0.2\",\"sourceUdpPort\":4672}",
		"{\"fileHash\":\"00112233445566778899aabbccddeeff\",\"sourceIp\":\"127.000.0.2\",\"sourceUdpPort\":4672}",
		"{\"fileHash\":\"00112233445566778899aabbccddeeff\",\"sourceIp\":\"127.0.0.2\",\"sourceUdpPort\":0}",
		"{\"fileHash\":\"00112233445566778899aabbccddeeff\",\"sourceIp\":\"127.0.0.2\",\"sourceUdpPort\":4672,\"extra\":true}"
	};
	for (const char *pszBody : kInvalidQueuePressureBodies) {
		SApiRoute invalidRoute;
		if (TryBuildRoute(
				"POST",
				kQueuePressureRoute,
				pszBody,
				invalidRoute,
				strErrorCode,
				strErrorMessage)
			|| strErrorCode != "INVALID_ARGUMENT")
		{
			return false;
		}
	}

	const unsigned char aucQueuePressureFileHash[16] = {
		0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
		0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
	};
	ClientUDPSocketSeams::SClientUdpQueuePressureFixture queuePressureFixture;
	ClientUDPSocketSeams::ArmClientUdpQueuePressureFixture(
		queuePressureFixture,
		7,
		aucQueuePressureFileHash,
		0x7F000002u,
		4672,
		3,
		2000,
		1000);
	if (!queuePressureFixture.bArmed
		|| queuePressureFixture.uEffectiveWaitingUserCount != 1951
		|| queuePressureFixture.uEffectiveWaitingUserCount + ClientUDPSocketSeams::kClientUdpQueueFullMargin
			!= queuePressureFixture.uQueueSize + 1)
	{
		return false;
	}
	ClientUDPSocketSeams::SClientUdpQueuePressureFixture consumedQueuePressure;
	if (ClientUDPSocketSeams::TryConsumeClientUdpQueuePressureFixture(
			queuePressureFixture,
			aucQueuePressureFileHash,
			0x7F000002u,
			4673,
			2000,
			1001,
			consumedQueuePressure)
		|| !queuePressureFixture.bArmed)
	{
		return false;
	}
	if (!ClientUDPSocketSeams::TryConsumeClientUdpQueuePressureFixture(
			queuePressureFixture,
			aucQueuePressureFileHash,
			0x7F000002u,
			4672,
			2000,
			1001,
			consumedQueuePressure)
		|| consumedQueuePressure.ullFixtureId != 7
		|| queuePressureFixture.bArmed
		|| ClientUDPSocketSeams::TryConsumeClientUdpQueuePressureFixture(
			queuePressureFixture,
			aucQueuePressureFileHash,
			0x7F000002u,
			4672,
			2000,
			1002,
			consumedQueuePressure))
	{
		return false;
	}
	ClientUDPSocketSeams::ArmClientUdpQueuePressureFixture(
		queuePressureFixture,
		8,
		aucQueuePressureFileHash,
		0x7F000002u,
		4672,
		3,
		2000,
		1000);
	if (ClientUDPSocketSeams::TryConsumeClientUdpQueuePressureFixture(
			queuePressureFixture,
			aucQueuePressureFileHash,
			0x7F000002u,
			4672,
			2000,
			1000 + ClientUDPSocketSeams::kClientUdpQueuePressureFixtureTtlMs,
			consumedQueuePressure)
		|| queuePressureFixture.bArmed)
	{
		return false;
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
