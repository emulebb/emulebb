#pragma once

#include <algorithm>
#include <vector>

namespace MuleListCtrlSeams
{
/**
 * @brief Validates that a visual order contains every logical column exactly once.
 */
inline bool IsCompleteColumnOrder(const int *piColumnOrder, int iColumnCount)
{
	if (piColumnOrder == nullptr || iColumnCount <= 0)
		return false;

	std::vector<bool> seen(static_cast<size_t>(iColumnCount), false);
	for (int i = 0; i < iColumnCount; ++i) {
		const int iColumn = piColumnOrder[i];
		if (iColumn < 0 || iColumn >= iColumnCount || seen[static_cast<size_t>(iColumn)])
			return false;
		seen[static_cast<size_t>(iColumn)] = true;
	}
	return true;
}

/**
 * @brief Returns toggleable columns ordered by their intended visual location.
 */
inline std::vector<int> BuildColumnMenuOrder(const int *piColumnLocations, int iColumnCount)
{
	std::vector<int> menuOrder;
	if (piColumnLocations == nullptr || iColumnCount <= 1)
		return menuOrder;

	menuOrder.reserve(static_cast<size_t>(iColumnCount - 1));
	for (int i = 1; i < iColumnCount; ++i)
		menuOrder.push_back(i);

	std::sort(menuOrder.begin(), menuOrder.end(), [piColumnLocations, iColumnCount](int iLeft, int iRight) {
		const int iLeftLocation = piColumnLocations[iLeft];
		const int iRightLocation = piColumnLocations[iRight];
		const bool bLeftValid = iLeftLocation >= 0 && iLeftLocation < iColumnCount;
		const bool bRightValid = iRightLocation >= 0 && iRightLocation < iColumnCount;

		if (bLeftValid != bRightValid)
			return bLeftValid;
		if (bLeftValid && iLeftLocation != iRightLocation)
			return iLeftLocation < iRightLocation;
		return iLeft < iRight;
	});
	return menuOrder;
}
}
