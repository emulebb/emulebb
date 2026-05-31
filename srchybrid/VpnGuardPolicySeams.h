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

inline bool IsRuntimeMonitorRequired(VpnGuardSeams::EMode eMode, bool bStartupBlocked)
{
	return eMode == VpnGuardSeams::EMode::Block && !bStartupBlocked;
}

inline bool CanUseStartupConnectionCommands(VpnGuardSeams::EMode eMode, bool bStartupBlocked, bool bRuntimeMonitorArmed)
{
	return !bStartupBlocked && (!IsRuntimeMonitorRequired(eMode, bStartupBlocked) || bRuntimeMonitorArmed);
}

inline bool CanPostStartupAutoConnect(bool bAutoConnect, VpnGuardSeams::EMode eMode, bool bStartupBlocked, bool bRuntimeMonitorArmed)
{
	return bAutoConnect && CanUseStartupConnectionCommands(eMode, bStartupBlocked, bRuntimeMonitorArmed);
}

inline EFailureAction GetFailureAction(bool bRuntimeProbe)
{
	return bRuntimeProbe ? EFailureAction::ExitApplication : EFailureAction::BlockStartup;
}
}
