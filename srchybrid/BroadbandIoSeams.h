#pragma once

#include <cstddef>
#include <cstdint>

namespace BroadbandIoSeams
{
inline constexpr std::uint64_t kDefaultGlobalDownloadBufferBudgetBytes = 512ull * 1024ull * 1024ull;
inline constexpr std::uint64_t kMinAdaptiveGlobalDownloadBufferBudgetBytes = 512ull * 1024ull * 1024ull;
inline constexpr std::uint64_t kMaxAdaptiveGlobalDownloadBufferBudgetBytes = 4ull * 1024ull * 1024ull * 1024ull;
inline constexpr std::uint64_t kMinimumAdaptiveFileBufferShareBytes = 64ull * 1024ull * 1024ull;
inline constexpr std::uint32_t kAdaptiveDownloadBufferAvailableRamPercent = 25u;
inline constexpr std::size_t kLegacyStandardMetadataFileBufferBytes = 16u * 1024u;
inline constexpr std::size_t kLegacyLargeMetadataFileBufferBytes = 32u * 1024u;

inline std::uint64_t MaxUInt64(const std::uint64_t ullLeft, const std::uint64_t ullRight)
{
	return ullLeft > ullRight ? ullLeft : ullRight;
}

inline std::uint64_t MinUInt64(const std::uint64_t ullLeft, const std::uint64_t ullRight)
{
	return ullLeft < ullRight ? ullLeft : ullRight;
}

inline std::uint64_t SaturatingAddUInt64(const std::uint64_t ullLeft, const std::uint64_t ullRight)
{
	return ullLeft > UINT64_MAX - ullRight ? UINT64_MAX : ullLeft + ullRight;
}

/**
 * @brief Builds the Auto Broadband I/O global download buffer budget from currently available RAM.
 */
inline std::uint64_t BuildAdaptiveGlobalDownloadBufferBudgetBytes(const std::uint64_t ullAvailablePhysicalRamBytes)
{
	const std::uint64_t ullMaxSourceBytes = kMaxAdaptiveGlobalDownloadBufferBudgetBytes / kAdaptiveDownloadBufferAvailableRamPercent * 100u;
	const std::uint64_t ullTargetBytes = ullAvailablePhysicalRamBytes >= ullMaxSourceBytes
		? kMaxAdaptiveGlobalDownloadBufferBudgetBytes
		: ullAvailablePhysicalRamBytes * kAdaptiveDownloadBufferAvailableRamPercent / 100u;
	if (ullTargetBytes < kMinAdaptiveGlobalDownloadBufferBudgetBytes)
		return kMinAdaptiveGlobalDownloadBufferBudgetBytes;
	if (ullTargetBytes > kMaxAdaptiveGlobalDownloadBufferBudgetBytes)
		return kMaxAdaptiveGlobalDownloadBufferBudgetBytes;
	return ullTargetBytes;
}

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
	const std::uint64_t ullBoundedPerFileBudget = ullPerFileBudget == 0 ? 1u : ullPerFileBudget;
	return ullConfiguredFileBufferBytes < ullBoundedPerFileBudget ? ullConfiguredFileBufferBytes : ullBoundedPerFileBudget;
}

/**
 * @brief Computes the demand-based per-file download write-buffer threshold for Auto Broadband I/O.
 */
inline std::uint64_t BuildDemandBasedFileBufferSizeBytes(
	const bool bAutoBroadbandIoEnabled,
	const std::uint64_t ullConfiguredFileBufferBytes,
	const std::uint64_t ullGlobalBudgetBytes,
	const std::uint64_t ullTotalBufferedBytes,
	const std::uint32_t uActiveBufferedFileCount,
	const std::uint64_t ullCurrentFileBufferedBytes,
	const std::uint64_t ullMinimumShareBytes = kMinimumAdaptiveFileBufferShareBytes)
{
	if (!bAutoBroadbandIoEnabled || ullGlobalBudgetBytes == 0)
		return ullConfiguredFileBufferBytes;

	if (uActiveBufferedFileCount <= 1)
		return MaxUInt64(ullConfiguredFileBufferBytes, ullGlobalBudgetBytes);

	const std::uint64_t ullEqualShareBytes = ullGlobalBudgetBytes / uActiveBufferedFileCount;
	const std::uint64_t ullMinimumShare = MinUInt64(ullMinimumShareBytes, ullEqualShareBytes == 0 ? 1u : ullEqualShareBytes);
	if (ullCurrentFileBufferedBytes < ullMinimumShare)
		return MaxUInt64(ullConfiguredFileBufferBytes, ullMinimumShare);

	const std::uint64_t ullRemainingBudget = ullGlobalBudgetBytes > ullTotalBufferedBytes
		? ullGlobalBudgetBytes - ullTotalBufferedBytes
		: 0u;
	return MaxUInt64(ullConfiguredFileBufferBytes, SaturatingAddUInt64(ullCurrentFileBufferedBytes, ullRemainingBudget));
}

/**
 * @brief Reports whether a buffered file should flush because the global adaptive budget is exhausted.
 */
inline bool ShouldFlushLargestFileForAdaptiveGlobalBudget(
	const bool bAutoBroadbandIoEnabled,
	const std::uint64_t ullGlobalBudgetBytes,
	const std::uint64_t ullTotalBufferedBytes,
	const std::uint64_t ullCurrentFileBufferedBytes,
	const std::uint64_t ullLargestBufferedFileBytes)
{
	return bAutoBroadbandIoEnabled
		&& ullGlobalBudgetBytes > 0
		&& ullTotalBufferedBytes > ullGlobalBudgetBytes
		&& ullCurrentFileBufferedBytes > 0
		&& ullCurrentFileBufferedBytes >= ullLargestBufferedFileBytes;
}
}
