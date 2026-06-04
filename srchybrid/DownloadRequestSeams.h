#pragma once

#include <cstdint>

namespace DownloadRequestSeams
{
inline constexpr std::uint32_t kLegacySlowDownloadThresholdBytesPerSec = 9u * 1024u;
inline constexpr std::uint32_t kLegacyVerySlowDownloadThresholdBytesPerSec = 4u * 1024u;
inline constexpr std::uint32_t kFastDownloadThresholdBytesPerSec = 75u * 1024u;
inline constexpr std::uint32_t kVeryFastDownloadThresholdBytesPerSec = 150u * 1024u;
inline constexpr std::uint32_t kBroadbandDownloadThresholdBytesPerSec = 512u * 1024u;
inline constexpr std::uint32_t kVeryFastBroadbandDownloadThresholdBytesPerSec = 1024u * 1024u;
inline constexpr int kSlowDownloadBlockReserve = 1;
inline constexpr int kModerateSlowDownloadBlockReserve = 2;
inline constexpr int kNormalDownloadBlockReserve = 3;
inline constexpr int kFastDownloadBlockReserve = 6;
inline constexpr int kVeryFastDownloadBlockReserve = 9;
inline constexpr int kBroadbandDownloadBlockReserve = 12;
inline constexpr int kVeryFastBroadbandDownloadBlockReserve = 18;

/**
 * @brief Selects the local pending-block reserve target for a downloading peer.
 */
inline int SelectDownloadBlockRequestReserve(std::uint32_t uDownloadRateBytesPerSec, bool bLegacyEmuleClientWithoutCompression)
{
	if (bLegacyEmuleClientWithoutCompression && uDownloadRateBytesPerSec < kLegacySlowDownloadThresholdBytesPerSec)
		return uDownloadRateBytesPerSec < kLegacyVerySlowDownloadThresholdBytesPerSec
			? kSlowDownloadBlockReserve
			: kModerateSlowDownloadBlockReserve;
	if (uDownloadRateBytesPerSec >= kVeryFastBroadbandDownloadThresholdBytesPerSec)
		return kVeryFastBroadbandDownloadBlockReserve;
	if (uDownloadRateBytesPerSec >= kBroadbandDownloadThresholdBytesPerSec)
		return kBroadbandDownloadBlockReserve;
	if (uDownloadRateBytesPerSec > kVeryFastDownloadThresholdBytesPerSec)
		return kVeryFastDownloadBlockReserve;
	if (uDownloadRateBytesPerSec > kFastDownloadThresholdBytesPerSec)
		return kFastDownloadBlockReserve;
	return kNormalDownloadBlockReserve;
}

inline bool IsValidDownloadBlockRequestReserve(int iBlockReserve)
{
	return iBlockReserve > 0 && iBlockReserve <= kVeryFastBroadbandDownloadBlockReserve;
}
}
