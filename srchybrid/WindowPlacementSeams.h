#pragma once

#include <Windows.h>

#include <algorithm>

namespace WindowPlacementSeams
{
inline constexpr unsigned int kDefaultNormalRectPercent = 80u;

/// Builds the centered normal-position rectangle used by the first-run main window placement.
inline RECT BuildCenteredPercentRect(const RECT &rcWorkArea, unsigned int uPercent)
{
	const LONG lWorkWidth = std::max<LONG>(rcWorkArea.right - rcWorkArea.left, 1);
	const LONG lWorkHeight = std::max<LONG>(rcWorkArea.bottom - rcWorkArea.top, 1);
	const LONG lWidth = std::max<LONG>(lWorkWidth * static_cast<LONG>(uPercent) / 100, 1);
	const LONG lHeight = std::max<LONG>(lWorkHeight * static_cast<LONG>(uPercent) / 100, 1);
	const LONG lLeft = rcWorkArea.left + (lWorkWidth - lWidth) / 2;
	const LONG lTop = rcWorkArea.top + (lWorkHeight - lHeight) / 2;

	RECT rcResult = {};
	rcResult.left = lLeft;
	rcResult.top = lTop;
	rcResult.right = lLeft + lWidth;
	rcResult.bottom = lTop + lHeight;
	return rcResult;
}

/// Builds the default main window placement: maximized, with an 80%-of-work-area normal rectangle.
inline WINDOWPLACEMENT BuildDefaultMainWindowPlacement(const RECT &rcWorkArea)
{
	WINDOWPLACEMENT placement = {};
	placement.length = sizeof(WINDOWPLACEMENT);
	placement.showCmd = SW_SHOWMAXIMIZED;
	placement.rcNormalPosition = BuildCenteredPercentRect(rcWorkArea, kDefaultNormalRectPercent);
	return placement;
}
}
