#pragma once

/**
 * @brief Testable priority helpers for download-list keyboard commands.
 */
namespace DownloadPriorityShortcutsSeams
{
constexpr unsigned char kDownloadPriorityLow = 0;
constexpr unsigned char kDownloadPriorityNormal = 1;
constexpr unsigned char kDownloadPriorityHigh = 2;

/**
 * @brief Returns the next bounded manual download priority.
 */
inline unsigned char StepManualDownloadPriority(const unsigned char uCurrentPriority, const bool bIncrease)
{
	if (bIncrease) {
		if (uCurrentPriority <= kDownloadPriorityLow)
			return kDownloadPriorityNormal;
		return kDownloadPriorityHigh;
	}

	if (uCurrentPriority >= kDownloadPriorityHigh)
		return kDownloadPriorityNormal;
	return kDownloadPriorityLow;
}
}
