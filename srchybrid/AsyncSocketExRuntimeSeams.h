#pragma once

/**
 * @brief Testable release-mode policy for AsyncSocketEx runtime failures.
 */
namespace AsyncSocketExRuntimeSeams
{
/**
 * @brief Helper-window initialization failures prevent socket creation and should be release-visible.
 */
inline bool ShouldLogThreadDataInitFailure()
{
	return true;
}
}
