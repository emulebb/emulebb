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

inline EFailureAction GetFailureAction(bool bRuntimeProbe)
{
	return bRuntimeProbe ? EFailureAction::ExitApplication : EFailureAction::BlockStartup;
}
}
