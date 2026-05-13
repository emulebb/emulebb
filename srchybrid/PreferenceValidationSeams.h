#pragma once

#include <cstdint>
#include <climits>

namespace PreferenceValidationSeams
{
constexpr std::uint32_t kDefaultConfiguredUploadLimitKiB = 6100u;
constexpr std::uint32_t kDefaultConfiguredDownloadLimitKiB = 12207u;
constexpr std::uint32_t kUnlimitedBandwidthSentinelKiB = UINT32_MAX;
constexpr std::uint32_t kMaxFiniteBandwidthLimitKiB = kUnlimitedBandwidthSentinelKiB - 1u;
constexpr std::uint64_t kMaxSignedIntPreference = static_cast<std::uint64_t>(INT_MAX);
constexpr std::uint64_t kMinQueueSize = 2000u;
constexpr std::uint64_t kDefaultQueueSize = 10000u;
constexpr std::uint64_t kMaxQueueSize = 10000u;
constexpr std::uint64_t kMinUploadSlots = 1u;
constexpr std::uint64_t kMaxUploadSlots = 32u;
constexpr std::uint32_t kVideoThumbnailDefaultIntervalSeconds = 0u;
constexpr std::uint32_t kVideoThumbnailMinIntervalSeconds = 30u;
constexpr std::uint32_t kVideoThumbnailRecommendedIntervalSeconds = 90u;
constexpr std::uint32_t kVideoThumbnailMaxIntervalSeconds = 900u;

/**
 * @brief Returns the fallback for negative persisted integers, otherwise the unsigned value.
 */
inline std::uint32_t NormalizeNonNegativeInt(const int value, const std::uint32_t uDefault)
{
	return value < 0 ? uDefault : static_cast<std::uint32_t>(value);
}

/**
 * @brief Clamps a persisted integer to a bounded unsigned range, using the default for negatives.
 */
inline std::uint32_t NormalizeBoundedInt(const int value, const std::uint32_t uDefault, const std::uint32_t uMin, const std::uint32_t uMax)
{
	if (value < 0)
		return uDefault;
	const std::uint32_t uValue = static_cast<std::uint32_t>(value);
	if (uValue < uMin)
		return uMin;
	if (uValue > uMax)
		return uMax;
	return uValue;
}

/**
 * @brief Returns the fallback for non-positive persisted integers, otherwise the unsigned value.
 */
inline std::uint32_t NormalizePositiveIntOrDefault(const int value, const std::uint32_t uDefault)
{
	return value <= 0 ? uDefault : static_cast<std::uint32_t>(value);
}

/**
 * @brief Returns the fallback for zero unsigned values, otherwise the value.
 */
inline std::uint32_t NormalizePositiveUIntOrDefault(const std::uint32_t uValue, const std::uint32_t uDefault)
{
	return uValue == 0 ? uDefault : uValue;
}

/**
 * @brief Returns whether an unsigned value is in an inclusive positive range.
 */
inline bool IsPositiveBounded(const std::uint64_t ullValue, const std::uint64_t ullMax)
{
	return ullValue >= 1u && ullValue <= ullMax;
}

/**
 * @brief Normalizes positive persisted integers with an upper bound, using the default outside the range.
 */
inline std::uint32_t NormalizePositiveBoundedIntOrDefault(const int value, const std::uint32_t uDefault, const std::uint32_t uMax)
{
	if (value <= 0)
		return uDefault;
	const std::uint32_t uValue = static_cast<std::uint32_t>(value);
	return uValue <= uMax ? uValue : uDefault;
}

/**
 * @brief Normalizes the configured upload limit, preserving one finite release budget.
 */
inline std::uint32_t NormalizeConfiguredUploadLimitKiB(const std::uint32_t uValue)
{
	return (uValue == 0u || uValue >= kUnlimitedBandwidthSentinelKiB) ? kDefaultConfiguredUploadLimitKiB : uValue;
}

/**
 * @brief Normalizes the configured download limit to a positive finite value.
 */
inline std::uint32_t NormalizeConfiguredDownloadLimitKiB(const std::uint32_t uValue)
{
	return uValue == 0u ? 1u : uValue;
}

/**
 * @brief Returns whether a REST bandwidth preference is an explicit finite KiB/s limit.
 */
inline bool IsFiniteBandwidthLimitKiB(const std::uint64_t ullValue)
{
	return ullValue >= 1u && ullValue <= kMaxFiniteBandwidthLimitKiB;
}

/**
 * @brief Returns whether a numeric preference can safely round-trip through signed INI storage.
 */
inline bool IsPositiveSignedIntValue(const std::uint64_t ullValue)
{
	return ullValue >= 1u && ullValue <= kMaxSignedIntPreference;
}

/**
 * @brief Reports whether a numeric preference can round-trip through an unsigned 32-bit value.
 */
inline bool IsPositiveUInt32Value(const std::uint64_t ullValue)
{
	return ullValue >= 1u && ullValue <= UINT32_MAX;
}

/**
 * @brief Clamps the upload slot count to the supported runtime range.
 */
inline std::uint32_t NormalizeUploadSlots(const std::uint32_t uValue)
{
	if (uValue < kMinUploadSlots)
		return static_cast<std::uint32_t>(kMinUploadSlots);
	if (uValue > kMaxUploadSlots)
		return static_cast<std::uint32_t>(kMaxUploadSlots);
	return uValue;
}

/**
 * @brief Reports whether the upload slot count is inside the externally accepted range.
 */
inline bool IsUploadSlotCount(const std::uint64_t ullValue)
{
	return ullValue >= kMinUploadSlots && ullValue <= kMaxUploadSlots;
}

/**
 * @brief Clamps the waiting-queue size to the supported runtime range.
 */
inline std::int64_t NormalizeQueueSize(const std::int64_t iValue)
{
	if (iValue < static_cast<std::int64_t>(kMinQueueSize))
		return static_cast<std::int64_t>(kMinQueueSize);
	if (iValue > static_cast<std::int64_t>(kMaxQueueSize))
		return static_cast<std::int64_t>(kMaxQueueSize);
	return iValue;
}

/**
 * @brief Reports whether the queue size is inside the externally accepted range.
 */
inline bool IsQueueSize(const std::uint64_t ullValue)
{
	return ullValue >= kMinQueueSize && ullValue <= kMaxQueueSize;
}

/**
 * @brief Normalizes TCP/UDP style port preferences with optional zero support.
 */
inline std::uint16_t NormalizePortValue(const int value, const std::uint16_t uDefault, const bool bAllowZero)
{
	if (value < 0 || value > UINT16_MAX)
		return uDefault;
	if (value == 0 && !bAllowZero)
		return uDefault;
	return static_cast<std::uint16_t>(value);
}

/**
 * @brief Bounds the thumbnail scan/retry interval; zero intentionally disables thumbnail generation.
 */
inline std::uint32_t NormalizeVideoThumbnailIntervalSeconds(const std::uint32_t uIntervalSeconds)
{
	if (uIntervalSeconds == 0u)
		return 0u;
	if (uIntervalSeconds < kVideoThumbnailMinIntervalSeconds)
		return kVideoThumbnailMinIntervalSeconds;
	if (uIntervalSeconds > kVideoThumbnailMaxIntervalSeconds)
		return kVideoThumbnailMaxIntervalSeconds;
	return uIntervalSeconds;
}
}
