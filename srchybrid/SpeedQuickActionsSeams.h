#pragma once

#include "MenuCmds.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace SpeedQuickActionsSeams
{
/// Describes one fixed quick-speed percentage command.
struct CQuickSpeedPercentAction
{
	unsigned int uCommandId;
	unsigned int uPercent;
};

inline constexpr std::array<CQuickSpeedPercentAction, 9> kUploadPercentActions = {{
	{MP_QS_U10, 10u},
	{MP_QS_U20, 20u},
	{MP_QS_U30, 30u},
	{MP_QS_U40, 40u},
	{MP_QS_U50, 50u},
	{MP_QS_U60, 60u},
	{MP_QS_U70, 70u},
	{MP_QS_U80, 80u},
	{MP_QS_U90, 90u},
}};

inline constexpr std::array<CQuickSpeedPercentAction, 9> kDownloadPercentActions = {{
	{MP_QS_D10, 10u},
	{MP_QS_D20, 20u},
	{MP_QS_D30, 30u},
	{MP_QS_D40, 40u},
	{MP_QS_D50, 50u},
	{MP_QS_D60, 60u},
	{MP_QS_D70, 70u},
	{MP_QS_D80, 80u},
	{MP_QS_D90, 90u},
}};

/// Calculates a non-zero session cap from a configured bandwidth cap and percentage.
inline std::uint32_t CalculatePercentLimitKiB(std::uint32_t uConfiguredLimitKiB, unsigned int uPercent)
{
	return std::max<std::uint32_t>(uConfiguredLimitKiB * uPercent / 100u, 1u);
}

/// Returns the percentage attached to a quick-speed command, or 0 for non-percent commands.
inline unsigned int GetPercentForCommand(unsigned int uCommandId)
{
	for (const CQuickSpeedPercentAction &action : kUploadPercentActions) {
		if (action.uCommandId == uCommandId)
			return action.uPercent;
	}
	for (const CQuickSpeedPercentAction &action : kDownloadPercentActions) {
		if (action.uCommandId == uCommandId)
			return action.uPercent;
	}
	return 0u;
}
}
