#pragma once

namespace ServerConnectSeams
{
enum class EServerListExhaustionAction
{
	None,
	FallbackToPlain,
	ScheduleRetry
};

inline EServerListExhaustionAction SelectServerListExhaustionAction(bool bHasPendingConnectionAttempts, bool bTryObfuscated, bool bCryptLayerRequired)
{
	if (bHasPendingConnectionAttempts)
		return EServerListExhaustionAction::None;
	if (bTryObfuscated && !bCryptLayerRequired)
		return EServerListExhaustionAction::FallbackToPlain;
	return EServerListExhaustionAction::ScheduleRetry;
}

inline bool ShouldRetrySingleServerWithoutObfuscation(bool bSingleConnecting, bool bHasServer, bool bUsedServerCrypt, bool bCryptLayerRequired)
{
	return bSingleConnecting && bHasServer && bUsedServerCrypt && !bCryptLayerRequired;
}

inline bool ShouldSuppressDuplicateConnectRequest(bool bManual, bool bIsConnecting, bool bAlreadyAwaitingServer)
{
	return !bManual && bIsConnecting && bAlreadyAwaitingServer;
}
}
