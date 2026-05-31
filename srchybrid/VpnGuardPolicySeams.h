#pragma once

#include "VpnGuardSeams.h"

namespace VpnGuardPolicySeams
{
enum class EFailureAction
{
	BlockStartup,
	ExitApplication
};

inline bool ShouldRunStartupProbe(VpnGuardSeams::EMode eMode, bool bStartupBlocked, bool bStartupProbeApproved)
{
	return eMode == VpnGuardSeams::EMode::Block && !bStartupBlocked && !bStartupProbeApproved;
}

inline bool IsProbeResultAllowed(bool bProbeSucceeded, bool bPublicIpAllowed)
{
	return bProbeSucceeded && bPublicIpAllowed;
}

inline bool IsProbeResultAllowed(bool bPublicIpCheckRequired, bool bProbeSucceeded, bool bPublicIpAllowed)
{
	return !bPublicIpCheckRequired || IsProbeResultAllowed(bProbeSucceeded, bPublicIpAllowed);
}

inline bool IsRuntimeMonitorRequired(VpnGuardSeams::EMode eMode, bool bStartupBlocked)
{
	return eMode == VpnGuardSeams::EMode::Block && !bStartupBlocked;
}

inline bool CanUseP2PConnectionCommands(VpnGuardSeams::EMode eMode, bool bStartupBlocked, bool bRuntimeMonitorArmed)
{
	return !bStartupBlocked && (!IsRuntimeMonitorRequired(eMode, bStartupBlocked) || bRuntimeMonitorArmed);
}

inline bool CanUseStartupConnectionCommands(VpnGuardSeams::EMode eMode, bool bStartupBlocked, bool bRuntimeMonitorArmed)
{
	return CanUseP2PConnectionCommands(eMode, bStartupBlocked, bRuntimeMonitorArmed);
}

inline bool CanPostStartupAutoConnect(bool bAutoConnect, VpnGuardSeams::EMode eMode, bool bStartupBlocked, bool bRuntimeMonitorArmed)
{
	return bAutoConnect && CanUseP2PConnectionCommands(eMode, bStartupBlocked, bRuntimeMonitorArmed);
}

inline EFailureAction GetFailureAction(bool bRuntimeProbe)
{
	return bRuntimeProbe ? EFailureAction::ExitApplication : EFailureAction::BlockStartup;
}
}
