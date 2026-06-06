#pragma once

#include <tchar.h>

/**
 * Documents the non-stock preferences.ini sections and key names owned by the
 * broadband branch. Stock/community keys intentionally remain at their
 * established call sites.
 */
namespace PreferenceIniMap
{
namespace Sections
{
inline constexpr const TCHAR* FileCompletion = _T("FileCompletion");
inline constexpr const TCHAR* Proxy = _T("Proxy");
inline constexpr const TCHAR* Statistics = _T("Statistics");
inline constexpr const TCHAR* UploadPolicy = _T("UploadPolicy");
inline constexpr const TCHAR* UPnP = _T("UPnP");
inline constexpr const TCHAR* WebServer = _T("WebServer");
inline constexpr const TCHAR* eMule = _T("eMule");
}

namespace FileCompletionKeys
{
inline constexpr const TCHAR* RunCommandOnFileCompletion = _T("RunCommandOnFileCompletion");
inline constexpr const TCHAR* Program = _T("FileCompletionProgram");
inline constexpr const TCHAR* Arguments = _T("FileCompletionArguments");
}

namespace VideoThumbnailKeys
{
inline constexpr const TCHAR* FfmpegPath = _T("VideoThumbnailFfmpegPath");
inline constexpr const TCHAR* IntervalSeconds = _T("VideoThumbnailIntervalSeconds");
inline constexpr const TCHAR* AllowPeerPreview = _T("AllowPeerPreview");
}

namespace GeoLocationKeys
{
inline constexpr const TCHAR* LookupEnabled = _T("GeoLocationLookupEnabled");
inline constexpr const TCHAR* UpdatePeriodDays = _T("GeoLocationUpdatePeriodDays");
inline constexpr const TCHAR* LastUpdateTime = _T("GeoLocationLastUpdateTime");
inline constexpr const TCHAR* UpdateUrl = _T("GeoLocationUpdateUrl");
}

namespace IPFilterUpdateKeys
{
inline constexpr const TCHAR* Enabled = _T("IPFilterUpdateEnabled");
inline constexpr const TCHAR* PeriodDays = _T("IPFilterUpdatePeriodDays");
inline constexpr const TCHAR* LastUpdateTime = _T("IPFilterLastUpdateTime");
inline constexpr const TCHAR* Url = _T("IPFilterUpdateUrl");
}

namespace IPFilterKeys
{
inline constexpr const TCHAR* Enabled = _T("IPFilterEnabled");
}

namespace MigrationKeys
{
inline constexpr const TCHAR* PreferenceSchema = _T("BBPreferenceSchema");
}

namespace LifecycleKeys
{
inline constexpr const TCHAR* StartupProgressDialog = _T("StartupProgressDialog");
inline constexpr const TCHAR* ShutdownProgressDialog = _T("ShutdownProgressDialog");
}

namespace BackupKeys
{
inline constexpr const TCHAR* DailyConfigBackup = _T("DailyConfigBackup");
}

namespace DisplayKeys
{
inline constexpr const TCHAR* DesktopUiRefreshIntervalMs = _T("DesktopUiRefreshIntervalMs");
}

namespace DownloadIoKeys
{
inline constexpr const TCHAR* DownloadAutoBroadbandIO = _T("DownloadAutoBroadbandIO");
}

namespace UploadPolicyKeys
{
inline constexpr const TCHAR* MaxUploadClientsAllowed = _T("MaxUploadClientsAllowed");
inline constexpr const TCHAR* SlowUploadThresholdFactor = _T("SlowUploadThresholdFactor");
inline constexpr const TCHAR* SlowUploadGraceSeconds = _T("SlowUploadGraceSeconds");
inline constexpr const TCHAR* SlowUploadWarmupSeconds = _T("SlowUploadWarmupSeconds");
inline constexpr const TCHAR* ZeroUploadRateGraceSeconds = _T("ZeroUploadRateGraceSeconds");
inline constexpr const TCHAR* SlowUploadCooldownSeconds = _T("SlowUploadCooldownSeconds");
inline constexpr const TCHAR* LowRatioBoostEnabled = _T("LowRatioBoostEnabled");
inline constexpr const TCHAR* LowRatioThreshold = _T("LowRatioThreshold");
inline constexpr const TCHAR* LowRatioScoreBonus = _T("LowRatioScoreBonus");
inline constexpr const TCHAR* LowIDScoreDivisor = _T("LowIDScoreDivisor");
inline constexpr const TCHAR* SessionTransferLimitMode = _T("SessionTransferLimitMode");
inline constexpr const TCHAR* SessionTransferLimitValue = _T("SessionTransferLimitValue");
inline constexpr const TCHAR* SessionTimeLimitSeconds = _T("SessionTimeLimitSeconds");
}
}
