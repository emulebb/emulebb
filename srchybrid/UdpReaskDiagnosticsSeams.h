#pragma once

#include <cstdint>

namespace UdpReaskDiagnosticsSeams
{
constexpr uint64_t kRepeatDecisionLogIntervalMs = 5ull * 60ull * 1000ull;
constexpr uint64_t kReaskScheduleWindowOpenMs = 2ull * 60ull * 1000ull;
constexpr uint64_t kReaskScheduleWindowCloseMs = 1000ull;
constexpr uint64_t kReaskScheduleConnectCooldownMs = 20ull * 60ull * 1000ull;

enum class EUdpReaskDecision : uint8_t
{
	None,
	DirectEligible,
	CallbackEligible,
	MissingRequestFile,
	RequestPending,
	FailureRatioExceeded,
	RemoteUdpPortMissing,
	RemoteUdpVersionMissing,
	LocalUdpDisabled,
	LocalFirewalled,
	TcpConnected,
	ProxyEnabled,
	BuddyIpMissing,
	BuddyPortMissing,
	BuddyIdInvalid,
	SourceExchangeDue,
	SwappedToAnotherFile,
	DirectReaskSent,
	CallbackReaskSent
};

enum class EUdpReaskScheduleDecision : uint8_t
{
	None,
	Eligible,
	AppDisconnected,
	ReaskWindowNotOpen,
	ReaskWindowClosed,
	ConnectCooldown
};

struct SUdpReaskEligibility
{
	bool bHasRequestFile;
	bool bRequestPending;
	uint32_t uTotalPackets;
	uint32_t uFailedPackets;
	uint16_t uRemoteUdpPort;
	uint8_t uRemoteUdpVersion;
	uint16_t uLocalUdpPort;
	bool bLocalFirewalled;
	bool bTcpConnected;
	bool bProxyEnabled;
	bool bLowId;
	bool bBuddyIpKnown;
	bool bBuddyPortKnown;
	bool bBuddyIdValid;
};

struct SUdpReaskScheduleEligibility
{
	bool bAppConnected;
	uint64_t ullTimeUntilReaskMs;
	uint64_t ullNowTick;
	uint64_t ullLastTriedTick;
};

inline uint64_t LastTriedAgeMs(const SUdpReaskScheduleEligibility &rEligibility)
{
	return rEligibility.ullNowTick >= rEligibility.ullLastTriedTick
		? rEligibility.ullNowTick - rEligibility.ullLastTriedTick
		: 0u;
}

inline uint64_t ConnectCooldownRemainingMs(const SUdpReaskScheduleEligibility &rEligibility)
{
	const uint64_t ullAgeMs = LastTriedAgeMs(rEligibility);
	return ullAgeMs < kReaskScheduleConnectCooldownMs
		? kReaskScheduleConnectCooldownMs - ullAgeMs
		: 0u;
}

inline EUdpReaskScheduleDecision EvaluateScheduleEligibility(
	const SUdpReaskScheduleEligibility &rEligibility)
{
	if (!rEligibility.bAppConnected)
		return EUdpReaskScheduleDecision::AppDisconnected;
	if (rEligibility.ullTimeUntilReaskMs >= kReaskScheduleWindowOpenMs)
		return EUdpReaskScheduleDecision::ReaskWindowNotOpen;
	if (rEligibility.ullTimeUntilReaskMs <= kReaskScheduleWindowCloseMs)
		return EUdpReaskScheduleDecision::ReaskWindowClosed;
	if (ConnectCooldownRemainingMs(rEligibility) != 0u)
		return EUdpReaskScheduleDecision::ConnectCooldown;
	return EUdpReaskScheduleDecision::Eligible;
}

inline bool IsScheduleEligible(const EUdpReaskScheduleDecision eDecision)
{
	return eDecision == EUdpReaskScheduleDecision::Eligible;
}

inline EUdpReaskDecision EvaluateEligibility(const SUdpReaskEligibility &rEligibility)
{
	if (!rEligibility.bHasRequestFile)
		return EUdpReaskDecision::MissingRequestFile;
	if (rEligibility.bRequestPending)
		return EUdpReaskDecision::RequestPending;
	if (rEligibility.uTotalPackets > 3u
		&& static_cast<uint64_t>(rEligibility.uFailedPackets) * 10u
			> static_cast<uint64_t>(rEligibility.uTotalPackets) * 3u)
	{
		return EUdpReaskDecision::FailureRatioExceeded;
	}
	if (rEligibility.uRemoteUdpPort == 0u)
		return EUdpReaskDecision::RemoteUdpPortMissing;
	if (rEligibility.uRemoteUdpVersion == 0u)
		return EUdpReaskDecision::RemoteUdpVersionMissing;
	if (rEligibility.uLocalUdpPort == 0u)
		return EUdpReaskDecision::LocalUdpDisabled;
	if (rEligibility.bLocalFirewalled)
		return EUdpReaskDecision::LocalFirewalled;
	if (rEligibility.bTcpConnected)
		return EUdpReaskDecision::TcpConnected;
	if (rEligibility.bProxyEnabled)
		return EUdpReaskDecision::ProxyEnabled;
	if (!rEligibility.bLowId)
		return EUdpReaskDecision::DirectEligible;
	if (!rEligibility.bBuddyIpKnown)
		return EUdpReaskDecision::BuddyIpMissing;
	if (!rEligibility.bBuddyPortKnown)
		return EUdpReaskDecision::BuddyPortMissing;
	if (!rEligibility.bBuddyIdValid)
		return EUdpReaskDecision::BuddyIdInvalid;
	return EUdpReaskDecision::CallbackEligible;
}

inline bool IsTransportEligibleDecision(const EUdpReaskDecision eDecision)
{
	return eDecision == EUdpReaskDecision::DirectEligible
		|| eDecision == EUdpReaskDecision::CallbackEligible
		|| eDecision == EUdpReaskDecision::SourceExchangeDue
		|| eDecision == EUdpReaskDecision::SwappedToAnotherFile
		|| eDecision == EUdpReaskDecision::DirectReaskSent
		|| eDecision == EUdpReaskDecision::CallbackReaskSent;
}

inline bool IsSentDecision(const EUdpReaskDecision eDecision)
{
	return eDecision == EUdpReaskDecision::DirectReaskSent
		|| eDecision == EUdpReaskDecision::CallbackReaskSent;
}

template <typename TDecision>
inline bool ShouldLogDecision(
	const TDecision ePreviousDecision,
	const uint64_t ullPreviousTick,
	const TDecision eCurrentDecision,
	const uint64_t ullCurrentTick,
	const bool bSameFile)
{
	return !bSameFile
		|| ePreviousDecision != eCurrentDecision
		|| ullPreviousTick == 0u
		|| ullCurrentTick < ullPreviousTick
		|| ullCurrentTick - ullPreviousTick >= kRepeatDecisionLogIntervalMs;
}
}
