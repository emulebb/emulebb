#pragma once

#include <array>
#include <climits>
#include <cstring>
#include <cstdint>
#include "PreferenceValidationSeams.h"

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
	NetworkEd2K,
	DownloadAutoBroadbandIO
};

enum class EMutablePreferenceValueKind : uint8_t
{
	Unsigned,
	Boolean
};

struct SMutablePreferenceSpec
{
	const char *pszName;
	EMutablePreference ePreference;
	EMutablePreferenceValueKind eKind;
	const char *pszErrorMessage;
	bool (*pfnIsValidValue)(uint64_t);
};

inline constexpr uint64_t kMutablePreferenceMaxSignedInt = PreferenceValidationSeams::kMaxSignedIntPreference;
inline constexpr uint64_t kMutablePreferenceUnlimitedSentinel = PreferenceValidationSeams::kUnlimitedBandwidthSentinelKiB;
inline constexpr uint64_t kMutablePreferenceMaxFiniteKiBps = PreferenceValidationSeams::kMaxFiniteBandwidthLimitKiB;
inline constexpr uint64_t kMutablePreferenceMinQueueSize = PreferenceValidationSeams::kMinQueueSize;
inline constexpr uint64_t kMutablePreferenceMaxQueueSize = PreferenceValidationSeams::kMaxQueueSize;
inline constexpr uint64_t kMutablePreferenceMinUploadSlots = PreferenceValidationSeams::kMinUploadSlots;
inline constexpr uint64_t kMutablePreferenceMaxUploadSlots = PreferenceValidationSeams::kMaxUploadSlots;
inline constexpr uint64_t kTransferProgressRatioScale = 10000u;

/**
 * Reports whether one REST unsigned 32-bit preference is positive.
 */
inline bool IsPositiveUInt32PreferenceValue(const uint64_t ullValue)
{
	return PreferenceValidationSeams::IsPositiveUInt32Value(ullValue);
}

/**
 * Reports whether one REST bandwidth preference is a finite configured limit.
 */
inline bool IsFiniteKiBpsPreferenceValue(const uint64_t ullValue)
{
	return PreferenceValidationSeams::IsFiniteBandwidthLimitKiB(ullValue);
}

/**
 * Reports whether one REST integer preference can round-trip through the UI
 * and INI integer persistence without falling back to a default.
 */
inline bool IsPositiveSignedIntPreferenceValue(const uint64_t ullValue)
{
	return PreferenceValidationSeams::IsPositiveSignedIntValue(ullValue);
}

/**
 * Reports whether one REST queue-size preference matches the UI/storage range.
 */
inline bool IsQueueSizePreferenceValue(const uint64_t ullValue)
{
	return PreferenceValidationSeams::IsQueueSize(ullValue);
}

/**
 * Reports whether one REST upload-slot preference matches the broadband range.
 */
inline bool IsUploadSlotPreferenceValue(const uint64_t ullValue)
{
	return PreferenceValidationSeams::IsUploadSlotCount(ullValue);
}

inline constexpr const char *kMutablePreferenceFieldListCsv =
	"uploadLimitKiBps,downloadLimitKiBps,maxConnections,maxConnectionsPerFiveSeconds,maxSourcesPerFile,"
	"uploadClientDataRate,maxUploadSlots,queueSize,autoConnect,newAutoUp,newAutoDown,creditSystem,"
	"safeServerConnect,networkKademlia,networkEd2k,downloadAutoBroadbandIo";

/**
 * Returns the canonical metadata for the native REST mutable preferences.
 */
inline const std::array<SMutablePreferenceSpec, 16> &GetMutablePreferenceSpecs()
{
	static const std::array<SMutablePreferenceSpec, 16> specs = {{
		{"uploadLimitKiBps", EMutablePreference::MaxUploadKiB, EMutablePreferenceValueKind::Unsigned, "uploadLimitKiBps must be an unsigned number in the range 1..4294967294", IsFiniteKiBpsPreferenceValue},
		{"downloadLimitKiBps", EMutablePreference::MaxDownloadKiB, EMutablePreferenceValueKind::Unsigned, "downloadLimitKiBps must be an unsigned number in the range 1..4294967294", IsFiniteKiBpsPreferenceValue},
		{"maxConnections", EMutablePreference::MaxConnections, EMutablePreferenceValueKind::Unsigned, "maxConnections must be an unsigned number in the range 1..2147483647", IsPositiveSignedIntPreferenceValue},
		{"maxConnectionsPerFiveSeconds", EMutablePreference::MaxConPerFive, EMutablePreferenceValueKind::Unsigned, "maxConnectionsPerFiveSeconds must be an unsigned number in the range 1..2147483647", IsPositiveSignedIntPreferenceValue},
		{"maxSourcesPerFile", EMutablePreference::MaxSourcesPerFile, EMutablePreferenceValueKind::Unsigned, "maxSourcesPerFile must be an unsigned number in the range 1..2147483647", IsPositiveSignedIntPreferenceValue},
		{"uploadClientDataRate", EMutablePreference::UploadClientDataRate, EMutablePreferenceValueKind::Unsigned, "uploadClientDataRate must be an unsigned number in the range 1..4294967295", IsPositiveUInt32PreferenceValue},
		{"maxUploadSlots", EMutablePreference::MaxUploadSlots, EMutablePreferenceValueKind::Unsigned, "maxUploadSlots must be an unsigned number in the range 1..32", IsUploadSlotPreferenceValue},
		{"queueSize", EMutablePreference::QueueSize, EMutablePreferenceValueKind::Unsigned, "queueSize must be an unsigned number in the range 2000..10000", IsQueueSizePreferenceValue},
		{"autoConnect", EMutablePreference::AutoConnect, EMutablePreferenceValueKind::Boolean, "autoConnect must be a boolean", NULL},
		{"newAutoUp", EMutablePreference::NewAutoUp, EMutablePreferenceValueKind::Boolean, "newAutoUp must be a boolean", NULL},
		{"newAutoDown", EMutablePreference::NewAutoDown, EMutablePreferenceValueKind::Boolean, "newAutoDown must be a boolean", NULL},
		{"creditSystem", EMutablePreference::CreditSystem, EMutablePreferenceValueKind::Boolean, "creditSystem must be a boolean", NULL},
		{"safeServerConnect", EMutablePreference::SafeServerConnect, EMutablePreferenceValueKind::Boolean, "safeServerConnect must be a boolean", NULL},
		{"networkKademlia", EMutablePreference::NetworkKademlia, EMutablePreferenceValueKind::Boolean, "networkKademlia must be a boolean", NULL},
		{"networkEd2k", EMutablePreference::NetworkEd2K, EMutablePreferenceValueKind::Boolean, "networkEd2k must be a boolean", NULL},
		{"downloadAutoBroadbandIo", EMutablePreference::DownloadAutoBroadbandIO, EMutablePreferenceValueKind::Boolean, "downloadAutoBroadbandIo must be a boolean", NULL}
	}};
	return specs;
}

/**
 * Finds the canonical metadata for one mutable preference name.
 */
inline const SMutablePreferenceSpec *FindMutablePreferenceSpec(const char *pszPreferenceName)
{
	if (pszPreferenceName == nullptr || pszPreferenceName[0] == '\0')
		return NULL;
	for (const SMutablePreferenceSpec &spec : GetMutablePreferenceSpecs()) {
		if (strcmp(pszPreferenceName, spec.pszName) == 0)
			return &spec;
	}
	return NULL;
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
	const SMutablePreferenceSpec *const pSpec = FindMutablePreferenceSpec(pszPreferenceName);
	return pSpec != NULL ? pSpec->ePreference : EMutablePreference::Invalid;
}

/**
 * Reports whether a shared-file removal request is legal for the selected file.
 */
inline bool CanRemoveSharedFile(const bool bIsShared, const bool bMustRemainShared)
{
	return bIsShared && !bMustRemainShared;
}
}
