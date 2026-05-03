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
 * Parses the stable transfer-priority vocabulary used by the pipe API.
 */
inline ETransferPriority ParseTransferPriorityName(const char *pszPriority)
{
	if (pszPriority == nullptr || pszPriority[0] == '\0')
		return ETransferPriority::Invalid;
	if (strcmp(pszPriority, "auto") == 0)
		return ETransferPriority::Auto;
	if (strcmp(pszPriority, "veryLow") == 0)
		return ETransferPriority::VeryLow;
	if (strcmp(pszPriority, "low") == 0)
		return ETransferPriority::Low;
	if (strcmp(pszPriority, "normal") == 0)
		return ETransferPriority::Normal;
	if (strcmp(pszPriority, "high") == 0)
		return ETransferPriority::High;
	if (strcmp(pszPriority, "veryHigh") == 0)
		return ETransferPriority::VeryHigh;
	return ETransferPriority::Invalid;
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
