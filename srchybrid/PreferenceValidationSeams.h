#pragma once

#include <cstdint>
#include <climits>
#include <cmath>

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
constexpr std::uint32_t kDefaultMaxUploadSlots = 8u;
constexpr float kMinSlowUploadThresholdFactor = 0.10f;
constexpr float kDefaultSlowUploadThresholdFactor = 0.70f;
constexpr float kMaxSlowUploadThresholdFactor = 1.0f;
constexpr std::uint32_t kMinSlowUploadGraceSeconds = 5u;
constexpr std::uint32_t kDefaultSlowUploadGraceSeconds = 30u;
constexpr std::uint32_t kMaxSlowUploadGraceSeconds = 300u;
constexpr std::uint32_t kDefaultSlowUploadWarmupSeconds = 60u;
constexpr std::uint32_t kMaxSlowUploadWarmupSeconds = 3600u;
constexpr std::uint32_t kMinZeroUploadRateGraceSeconds = 3u;
constexpr std::uint32_t kDefaultZeroUploadRateGraceSeconds = 10u;
constexpr std::uint32_t kMaxZeroUploadRateGraceSeconds = 120u;
constexpr std::uint32_t kMinSlowUploadCooldownSeconds = 10u;
constexpr std::uint32_t kDefaultSlowUploadCooldownSeconds = 60u;
constexpr std::uint32_t kMaxSlowUploadCooldownSeconds = 3600u;
constexpr float kMinLowRatioThreshold = 0.0f;
constexpr float kDefaultLowRatioThreshold = 0.5f;
constexpr float kMaxLowRatioThreshold = 2.0f;
constexpr std::uint32_t kDefaultLowRatioBonus = 50u;
constexpr std::uint32_t kMaxLowRatioBonus = 500u;
constexpr std::uint32_t kMinLowIDDivisor = 1u;
constexpr std::uint32_t kDefaultLowIDDivisor = 2u;
constexpr std::uint32_t kMaxLowIDDivisor = 8u;
constexpr int kSessionTransferModeDisabled = 0;
constexpr int kSessionTransferModePercentOfFile = 1;
constexpr int kSessionTransferModeAbsoluteMiB = 2;
constexpr std::uint32_t kDefaultSessionTransferPercent = 90u;
constexpr std::uint32_t kMinSessionTransferPercent = 1u;
constexpr std::uint32_t kMaxSessionTransferPercent = 100u;
constexpr std::uint32_t kMinSessionTransferMiB = 1u;
constexpr std::uint32_t kMaxSessionTransferMiB = 4096u;
constexpr std::uint32_t kDefaultSessionTimeLimitSeconds = 7200u;
constexpr std::uint32_t kMaxSessionTimeLimitSeconds = 86400u;
constexpr std::uint32_t kVideoThumbnailDefaultIntervalSeconds = 0u;
constexpr std::uint32_t kVideoThumbnailMinIntervalSeconds = 30u;
constexpr std::uint32_t kVideoThumbnailRecommendedIntervalSeconds = 90u;
constexpr std::uint32_t kVideoThumbnailMaxIntervalSeconds = 900u;
constexpr std::uint16_t kRandomListenerPortMin = 49152u;
constexpr std::uint16_t kRandomListenerPortMax = 65535u;
constexpr std::uint32_t kRandomListenerPortRange = static_cast<std::uint32_t>(kRandomListenerPortMax) - kRandomListenerPortMin + 1u;

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
 * @brief Clamps a floating-point preference to a finite inclusive range.
 */
inline float NormalizeFloatRangeOrDefault(const float fValue, const float fDefault, const float fMin, const float fMax)
{
	if (!std::isfinite(fValue))
		return fDefault;
	if (fValue < fMin)
		return fMin;
	if (fValue > fMax)
		return fMax;
	return fValue;
}

/**
 * @brief Normalizes the broadband slow-upload threshold factor.
 */
inline float NormalizeSlowUploadThresholdFactor(const float fValue)
{
	return NormalizeFloatRangeOrDefault(fValue, kDefaultSlowUploadThresholdFactor, kMinSlowUploadThresholdFactor, kMaxSlowUploadThresholdFactor);
}

/**
 * @brief Normalizes the broadband slow-upload grace window.
 */
inline std::uint32_t NormalizeSlowUploadGraceSeconds(const std::uint32_t uValue)
{
	if (uValue < kMinSlowUploadGraceSeconds)
		return kMinSlowUploadGraceSeconds;
	if (uValue > kMaxSlowUploadGraceSeconds)
		return kMaxSlowUploadGraceSeconds;
	return uValue;
}

/**
 * @brief Normalizes the broadband slow-upload warm-up window.
 */
inline std::uint32_t NormalizeSlowUploadWarmupSeconds(const std::uint32_t uValue)
{
	return uValue > kMaxSlowUploadWarmupSeconds ? kMaxSlowUploadWarmupSeconds : uValue;
}

/**
 * @brief Normalizes the zero-rate grace window before an upload client can be recycled.
 */
inline std::uint32_t NormalizeZeroUploadRateGraceSeconds(const std::uint32_t uValue)
{
	if (uValue < kMinZeroUploadRateGraceSeconds)
		return kMinZeroUploadRateGraceSeconds;
	if (uValue > kMaxZeroUploadRateGraceSeconds)
		return kMaxZeroUploadRateGraceSeconds;
	return uValue;
}

/**
 * @brief Normalizes the slow-upload recycle cooldown.
 */
inline std::uint32_t NormalizeSlowUploadCooldownSeconds(const std::uint32_t uValue)
{
	if (uValue < kMinSlowUploadCooldownSeconds)
		return kMinSlowUploadCooldownSeconds;
	if (uValue > kMaxSlowUploadCooldownSeconds)
		return kMaxSlowUploadCooldownSeconds;
	return uValue;
}

/**
 * @brief Normalizes the low-ratio score boost threshold.
 */
inline float NormalizeLowRatioThreshold(const float fValue)
{
	return NormalizeFloatRangeOrDefault(fValue, kDefaultLowRatioThreshold, kMinLowRatioThreshold, kMaxLowRatioThreshold);
}

/**
 * @brief Normalizes the low-ratio score bonus.
 */
inline std::uint32_t NormalizeLowRatioBonus(const std::uint32_t uValue)
{
	return uValue > kMaxLowRatioBonus ? kMaxLowRatioBonus : uValue;
}

/**
 * @brief Normalizes the divisor applied to low-ID upload scores.
 */
inline std::uint32_t NormalizeLowIDDivisor(const std::uint32_t uValue)
{
	if (uValue < kMinLowIDDivisor)
		return kMinLowIDDivisor;
	if (uValue > kMaxLowIDDivisor)
		return kMaxLowIDDivisor;
	return uValue;
}

/**
 * @brief Normalizes the persisted upload-session transfer-limit mode.
 */
inline int NormalizeSessionTransferLimitMode(const int iValue)
{
	switch (iValue) {
	case kSessionTransferModeDisabled:
	case kSessionTransferModePercentOfFile:
	case kSessionTransferModeAbsoluteMiB:
		return iValue;
	default:
		return kSessionTransferModePercentOfFile;
	}
}

/**
 * @brief Normalizes the upload-session transfer-limit value for the selected mode.
 */
inline std::uint32_t NormalizeSessionTransferLimitValue(const int iMode, const std::uint32_t uValue)
{
	const int iNormalizedMode = NormalizeSessionTransferLimitMode(iMode);
	if (iNormalizedMode == kSessionTransferModePercentOfFile) {
		if (uValue < kMinSessionTransferPercent)
			return kMinSessionTransferPercent;
		if (uValue > kMaxSessionTransferPercent)
			return kMaxSessionTransferPercent;
		return uValue;
	}
	if (iNormalizedMode == kSessionTransferModeAbsoluteMiB) {
		if (uValue < kMinSessionTransferMiB)
			return kMinSessionTransferMiB;
		if (uValue > kMaxSessionTransferMiB)
			return kMaxSessionTransferMiB;
		return uValue;
	}
	return uValue > kMaxSessionTransferMiB ? kMaxSessionTransferMiB : uValue;
}

/**
 * @brief Normalizes the upload-session time limit, where zero disables the limit.
 */
inline std::uint32_t NormalizeSessionTimeLimitSeconds(const std::uint32_t uValue)
{
	return uValue > kMaxSessionTimeLimitSeconds ? kMaxSessionTimeLimitSeconds : uValue;
}

/**
 * @brief Derives a bounded upload slot count from a requested per-client data rate.
 */
inline std::uint32_t DeriveUploadSlotsForClientDataRate(const std::uint32_t uConfiguredUploadLimitKiB, const std::uint32_t uRequestedRateBytesPerSec)
{
	const std::uint64_t ullBudgetBytesPerSec = (static_cast<std::uint64_t>(uConfiguredUploadLimitKiB) * 1024u) < (3u * 1024u)
		? 3u * 1024u
		: static_cast<std::uint64_t>(uConfiguredUploadLimitKiB) * 1024u;
	const std::uint64_t ullRequestedRate = uRequestedRateBytesPerSec == 0u ? 1u : uRequestedRateBytesPerSec;
	const std::uint64_t ullDerivedSlots = ullBudgetBytesPerSec / ullRequestedRate;
	if (ullDerivedSlots < kMinUploadSlots)
		return static_cast<std::uint32_t>(kMinUploadSlots);
	if (ullDerivedSlots > kMaxUploadSlots)
		return static_cast<std::uint32_t>(kMaxUploadSlots);
	return static_cast<std::uint32_t>(ullDerivedSlots);
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
 * @brief Reports whether an automatically selected listener port is in the high dynamic-private range.
 */
inline bool IsRandomListenerPort(const std::uint16_t uPort)
{
	return uPort >= kRandomListenerPortMin && uPort <= kRandomListenerPortMax;
}

/**
 * @brief Chooses an adjacent in-range port when randomized TCP and UDP collide.
 */
inline std::uint16_t GetAdjacentRandomListenerPort(const std::uint16_t uPort)
{
	return uPort > kRandomListenerPortMin ? static_cast<std::uint16_t>(uPort - 1u) : static_cast<std::uint16_t>(uPort + 1u);
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
