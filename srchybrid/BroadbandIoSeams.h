#pragma once

#include <algorithm>
#include <cstdint>

namespace BroadbandIoSeams
{
inline constexpr std::uint64_t kDefaultGlobalDownloadBufferBudgetBytes = 512ull * 1024ull * 1024ull;

/**
 * @brief Computes the per-file download write-buffer threshold used before
 * flushing buffered part-file data to disk.
 */
inline std::uint64_t BuildEffectiveFileBufferSizeBytes(
	const bool bAutoBroadbandIoEnabled,
	const std::uint64_t ullConfiguredFileBufferBytes,
	const std::uint64_t ullGlobalBudgetBytes,
	const std::uint32_t uActiveBufferedFileCount)
{
	if (!bAutoBroadbandIoEnabled || ullGlobalBudgetBytes == 0 || uActiveBufferedFileCount <= 1)
		return ullConfiguredFileBufferBytes;

	const std::uint64_t ullPerFileBudget = ullGlobalBudgetBytes / uActiveBufferedFileCount;
	return std::min(ullConfiguredFileBufferBytes, std::max<std::uint64_t>(1u, ullPerFileBudget));
}
}
