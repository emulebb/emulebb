#pragma once

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
}
