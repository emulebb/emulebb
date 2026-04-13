#pragma once

namespace SharedFilesWndSeams
{
inline constexpr int kMinTreeWidth = 100;
inline constexpr int kMinRightPaneWidth = 160;
inline constexpr int kSplitterWidth = 4;

/**
 * @brief Returns the maximum splitter-left position for the Shared Files pane.
 */
inline int GetSplitterRangeMax(const int iClientWidth)
{
	const int iComputedMax = iClientWidth - kMinRightPaneWidth - kSplitterWidth;
	return iComputedMax < kMinTreeWidth ? kMinTreeWidth : iComputedMax;
}

/**
 * @brief Clamps a persisted or requested splitter-left position to the current client width.
 */
inline int ClampSplitterPosition(const int iSplitterLeft, const int iClientWidth)
{
	const int iRangeMax = GetSplitterRangeMax(iClientWidth);
	if (iSplitterLeft < kMinTreeWidth)
		return kMinTreeWidth;
	if (iSplitterLeft > iRangeMax)
		return iRangeMax;
	return iSplitterLeft;
}
}
