#pragma once

#include <climits>
#include <cstring>
#include <cstdint>

namespace WebApiSurfaceSeams
{
enum class ETransferPriority : uint8_t
{
	Invalid,
	Auto,
	VeryLow,
	Low,
	Normal,
	High,
	VeryHigh
};

enum class EMutablePreference : uint8_t
{
	Invalid,
	MaxUploadKiB,
	MaxDownloadKiB,
	MaxConnections,
	MaxConPerFive,
	MaxSourcesPerFile,
	UploadClientDataRate,
	MaxUploadSlots,
	QueueSize,
	AutoConnect,
	NewAutoUp,
	NewAutoDown,
	CreditSystem,
	SafeServerConnect,
	NetworkKademlia,
	NetworkEd2K
};

inline constexpr uint64_t kMutablePreferenceMaxSignedInt = static_cast<uint64_t>(INT_MAX);
inline constexpr uint64_t kMutablePreferenceUnlimitedSentinel = UINT32_MAX;
inline constexpr uint64_t kMutablePreferenceMaxFiniteKiBps = kMutablePreferenceUnlimitedSentinel - 1u;
inline constexpr uint64_t kMutablePreferenceMinQueueSize = 2000u;
inline constexpr uint64_t kMutablePreferenceMaxQueueSize = 10000u;
inline constexpr uint64_t kMutablePreferenceMinUploadSlots = 1u;
inline constexpr uint64_t kMutablePreferenceMaxUploadSlots = 32u;
inline constexpr uint64_t kTransferProgressRatioScale = 10000u;

/**
 * Reports whether one REST bandwidth preference is a finite configured limit.
 */
inline bool IsFiniteKiBpsPreferenceValue(const uint64_t ullValue)
{
	return ullValue >= 1u && ullValue <= kMutablePreferenceMaxFiniteKiBps;
}

/**
 * Reports whether one REST integer preference can round-trip through the UI
 * and INI integer persistence without falling back to a default.
 */
inline bool IsPositiveSignedIntPreferenceValue(const uint64_t ullValue)
{
	return ullValue >= 1u && ullValue <= kMutablePreferenceMaxSignedInt;
}

/**
 * Reports whether one REST queue-size preference matches the UI/storage range.
 */
inline bool IsQueueSizePreferenceValue(const uint64_t ullValue)
{
	return ullValue >= kMutablePreferenceMinQueueSize && ullValue <= kMutablePreferenceMaxQueueSize;
}

/**
 * Reports whether one REST upload-slot preference matches the broadband range.
 */
inline bool IsUploadSlotPreferenceValue(const uint64_t ullValue)
{
	return ullValue >= kMutablePreferenceMinUploadSlots && ullValue <= kMutablePreferenceMaxUploadSlots;
}

/**
 * Builds the stable REST transfer progress ratio shared by native and adapter APIs.
 */
inline double BuildTransferProgressRatio(const uint64_t ullCompletedBytes, const uint64_t ullTotalBytes)
{
	if (ullTotalBytes == 0u)
		return 0.0;
	if (ullCompletedBytes >= ullTotalBytes)
		return 1.0;

	const long double progress = static_cast<long double>(ullCompletedBytes) / static_cast<long double>(ullTotalBytes);
	const uint64_t ullScaled = static_cast<uint64_t>((progress * static_cast<long double>(kTransferProgressRatioScale)) + 0.5L);
	return static_cast<double>(ullScaled) / static_cast<double>(kTransferProgressRatioScale);
}

/**
 * Maps the persisted eD2K server priority to the public API string.
 */
inline const char* GetServerPriorityName(const unsigned uPreference)
{
	switch (uPreference) {
	case 2:
		return "low";
	case 1:
		return "high";
	case 0:
	default:
		return "normal";
	}
}

/**
 * Maps the internal upload state enum to the stable API state string.
 */
inline const char* GetUploadStateName(const uint8_t uUploadState)
{
	switch (uUploadState) {
	case 0:
		return "uploading";
	case 1:
		return "queued";
	case 2:
		return "connecting";
	case 3:
		return "banned";
	case 4:
	default:
		return "idle";
	}
}

/**
 * Maps the internal download source state enum to the stable REST token.
 */
inline const char* GetDownloadStateName(const uint8_t uDownloadState)
{
	switch (uDownloadState) {
	case 0:
		return "downloading";
	case 1:
		return "onqueue";
	case 2:
		return "connected";
	case 3:
		return "connecting";
	case 4:
		return "waitcallback";
	case 5:
		return "waitcallbackkad";
	case 6:
		return "reqhashset";
	case 7:
		return "noneededparts";
	case 8:
		return "toomanyconns";
	case 9:
		return "toomanyconnskad";
	case 10:
		return "lowtolowip";
	case 11:
		return "banned";
	case 12:
		return "error";
	case 13:
		return "none";
	case 14:
		return "remotequeuefull";
	default:
		return "unknown";
	}
}

/**
 * Parses the stable lowercase compact transfer-priority vocabulary used by
 * the REST and pipe APIs.
 */
inline ETransferPriority ParseTransferPriorityName(const char *pszPriority)
{
	if (pszPriority == nullptr || pszPriority[0] == '\0')
		return ETransferPriority::Invalid;
	if (strcmp(pszPriority, "auto") == 0)
		return ETransferPriority::Auto;
	if (strcmp(pszPriority, "verylow") == 0)
		return ETransferPriority::VeryLow;
	if (strcmp(pszPriority, "low") == 0)
		return ETransferPriority::Low;
	if (strcmp(pszPriority, "normal") == 0)
		return ETransferPriority::Normal;
	if (strcmp(pszPriority, "high") == 0)
		return ETransferPriority::High;
	if (strcmp(pszPriority, "veryhigh") == 0)
		return ETransferPriority::VeryHigh;
	return ETransferPriority::Invalid;
}

/**
 * Reports whether a token is in the REST transfer-priority vocabulary.
 */
inline bool IsTransferPriorityName(const char *pszPriority)
{
	return ParseTransferPriorityName(pszPriority) != ETransferPriority::Invalid;
}

/**
 * Reports whether a token is in the REST category-priority input vocabulary.
 */
inline bool IsCategoryPriorityName(const char *pszPriority)
{
	switch (ParseTransferPriorityName(pszPriority)) {
	case ETransferPriority::VeryLow:
	case ETransferPriority::Low:
	case ETransferPriority::Normal:
	case ETransferPriority::High:
	case ETransferPriority::VeryHigh:
		return true;
	case ETransferPriority::Auto:
	case ETransferPriority::Invalid:
	default:
		return false;
	}
}

/**
 * Reports whether a token is in the REST shared-file upload-priority vocabulary.
 */
inline bool IsSharedUploadPriorityName(const char *pszPriority)
{
	if (pszPriority == nullptr)
		return false;
	if (strcmp(pszPriority, "auto") == 0
		|| strcmp(pszPriority, "verylow") == 0
		|| strcmp(pszPriority, "low") == 0
		|| strcmp(pszPriority, "normal") == 0
		|| strcmp(pszPriority, "high") == 0
		|| strcmp(pszPriority, "release") == 0)
	{
		return true;
	}
	return false;
}

/**
 * Reports whether a token is in the REST server-priority vocabulary.
 */
inline bool IsServerPriorityName(const char *pszPriority)
{
	return pszPriority != nullptr
		&& (strcmp(pszPriority, "low") == 0
			|| strcmp(pszPriority, "normal") == 0
			|| strcmp(pszPriority, "high") == 0);
}

/**
 * Parses the stable mutable-preference vocabulary exposed over the pipe API.
 */
inline EMutablePreference ParseMutablePreferenceName(const char *pszPreferenceName)
{
	if (pszPreferenceName == nullptr || pszPreferenceName[0] == '\0')
		return EMutablePreference::Invalid;
	if (strcmp(pszPreferenceName, "uploadLimitKiBps") == 0)
		return EMutablePreference::MaxUploadKiB;
	if (strcmp(pszPreferenceName, "downloadLimitKiBps") == 0)
		return EMutablePreference::MaxDownloadKiB;
	if (strcmp(pszPreferenceName, "maxConnections") == 0)
		return EMutablePreference::MaxConnections;
	if (strcmp(pszPreferenceName, "maxConnectionsPerFiveSeconds") == 0)
		return EMutablePreference::MaxConPerFive;
	if (strcmp(pszPreferenceName, "maxSourcesPerFile") == 0)
		return EMutablePreference::MaxSourcesPerFile;
	if (strcmp(pszPreferenceName, "uploadClientDataRate") == 0)
		return EMutablePreference::UploadClientDataRate;
	if (strcmp(pszPreferenceName, "maxUploadSlots") == 0)
		return EMutablePreference::MaxUploadSlots;
	if (strcmp(pszPreferenceName, "queueSize") == 0)
		return EMutablePreference::QueueSize;
	if (strcmp(pszPreferenceName, "autoConnect") == 0)
		return EMutablePreference::AutoConnect;
	if (strcmp(pszPreferenceName, "newAutoUp") == 0)
		return EMutablePreference::NewAutoUp;
	if (strcmp(pszPreferenceName, "newAutoDown") == 0)
		return EMutablePreference::NewAutoDown;
	if (strcmp(pszPreferenceName, "creditSystem") == 0)
		return EMutablePreference::CreditSystem;
	if (strcmp(pszPreferenceName, "safeServerConnect") == 0)
		return EMutablePreference::SafeServerConnect;
	if (strcmp(pszPreferenceName, "networkKademlia") == 0)
		return EMutablePreference::NetworkKademlia;
	if (strcmp(pszPreferenceName, "networkEd2k") == 0)
		return EMutablePreference::NetworkEd2K;
	return EMutablePreference::Invalid;
}

/**
 * Reports whether a shared-file removal request is legal for the selected file.
 */
inline bool CanRemoveSharedFile(const bool bIsShared, const bool bMustRemainShared)
{
	return bIsShared && !bMustRemainShared;
}
}
