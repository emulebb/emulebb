#pragma once

#include <atlstr.h>
#include <cstdint>
#include "FileCompletionCommandSeams.h"

namespace PartFilePreviewSeams
{
enum EVideoThumbnailAttemptResult
{
	VTAR_NONE = 0,
	VTAR_COPY_FAILED,
	VTAR_VLC_BUSY,
	VTAR_VLC_TIMEOUT,
	VTAR_VLC_START_FAILED,
	VTAR_VLC_FAILED,
	VTAR_NO_THUMBNAIL,
	VTAR_DECODE_FAILED,
	VTAR_CACHE_WRITE_FAILED,
	VTAR_SUCCESS,
	VTAR_EXCEPTION
};

constexpr std::uint64_t kPartialVideoPreviewMinCompletedPermille = 5;
constexpr std::uint64_t kPartialVideoPreviewMinCompletedBytes = 1ull * 1024ull * 1024ull;
constexpr std::uint64_t kPartialVideoPreviewMaxCompletedBytes = 64ull * 1024ull * 1024ull;
constexpr std::uint64_t kVideoThumbnailScanIntervalMs = 90ull * 1000ull;
constexpr std::uint64_t kVideoThumbnailRetryIntervalMs = 90ull * 1000ull;
constexpr std::uint64_t kVideoThumbnailRefreshIntervalMs = kVideoThumbnailRetryIntervalMs;
constexpr std::uint64_t kVideoThumbnailRefreshDeltaPermille = 50;
constexpr std::uint64_t kVideoThumbnailRefreshMaxDeltaBytes = 128ull * 1024ull * 1024ull;
constexpr int kVideoThumbnailDisplayMaxWidth = 480;

/**
 * Extracts the executable basename used by preview-player dependent features.
 */
inline CString ExtractConfiguredVideoPlayerBaseName(const CString &rstrVideoPlayerPath)
{
	CString strVideoPlayerPath(rstrVideoPlayerPath);
	strVideoPlayerPath.Trim();
	strVideoPlayerPath.Trim(_T("\""));
	strVideoPlayerPath.Trim();

	int iSeparator = strVideoPlayerPath.ReverseFind(_T('\\'));
	const int iAltSeparator = strVideoPlayerPath.ReverseFind(_T('/'));
	if (iAltSeparator > iSeparator)
		iSeparator = iAltSeparator;

	const int iNameStart = iSeparator + 1;
	int iNameEnd = strVideoPlayerPath.GetLength();
	const int iDot = strVideoPlayerPath.ReverseFind(_T('.'));
	if (iDot > iSeparator)
		iNameEnd = iDot;

	return strVideoPlayerPath.Mid(iNameStart, iNameEnd - iNameStart);
}

/**
 * Returns whether the configured video preview player is VLC.
 */
inline bool IsConfiguredVlcPreviewPlayer(const CString &rstrVideoPlayerPath)
{
	return ExtractConfiguredVideoPlayerBaseName(rstrVideoPlayerPath).CompareNoCase(_T("vlc")) == 0;
}

/**
 * Returns the completed-data threshold used before partial-video preview attempts.
 */
inline std::uint64_t GetPartialVideoPreviewRequiredCompletedBytes(std::uint64_t ullFileSize)
{
	if (ullFileSize == 0)
		return kPartialVideoPreviewMinCompletedBytes;

	const std::uint64_t ullWhole = (ullFileSize / 1000ull) * kPartialVideoPreviewMinCompletedPermille;
	const std::uint64_t ullRemainder = ((ullFileSize % 1000ull) * kPartialVideoPreviewMinCompletedPermille + 999ull) / 1000ull;
	std::uint64_t ullRequired = ullWhole + ullRemainder;
	if (ullRequired < kPartialVideoPreviewMinCompletedBytes)
		ullRequired = kPartialVideoPreviewMinCompletedBytes;
	if (ullRequired > kPartialVideoPreviewMaxCompletedBytes)
		ullRequired = kPartialVideoPreviewMaxCompletedBytes;
	return ullRequired;
}

/**
 * Allows partial-video preview once a small percentage of the file has arrived.
 */
inline bool HasEnoughCompletedDataForPartialVideoPreview(std::uint64_t ullFileSize, std::uint64_t ullCompletedSize)
{
	return ullFileSize > 0 && ullCompletedSize >= GetPartialVideoPreviewRequiredCompletedBytes(ullFileSize);
}

/**
 * Returns the completed-data delta needed before a cached thumbnail is stale.
 */
inline std::uint64_t GetVideoThumbnailRefreshRequiredCompletedDelta(std::uint64_t ullFileSize)
{
	if (ullFileSize == 0)
		return kVideoThumbnailRefreshMaxDeltaBytes;

	const std::uint64_t ullWhole = (ullFileSize / 1000ull) * kVideoThumbnailRefreshDeltaPermille;
	const std::uint64_t ullRemainder = ((ullFileSize % 1000ull) * kVideoThumbnailRefreshDeltaPermille + 999ull) / 1000ull;
	std::uint64_t ullRequired = ullWhole + ullRemainder;
	if (ullRequired == 0)
		ullRequired = 1;
	if (ullRequired > kVideoThumbnailRefreshMaxDeltaBytes)
		ullRequired = kVideoThumbnailRefreshMaxDeltaBytes;
	return ullRequired;
}

/**
 * Returns whether a cached thumbnail can be refreshed with meaningfully newer completed data.
 */
inline bool ShouldRefreshVideoThumbnail(std::uint64_t ullCachedCompletedSize, std::uint64_t ullCurrentCompletedSize, std::uint64_t ullFileSize)
{
	if (ullCurrentCompletedSize <= ullCachedCompletedSize)
		return false;
	if (ullFileSize > 0 && ullCurrentCompletedSize >= ullFileSize)
		return true;
	return ullCurrentCompletedSize - ullCachedCompletedSize >= GetVideoThumbnailRefreshRequiredCompletedDelta(ullFileSize);
}

/**
 * Returns whether enough time has elapsed to attempt another VLC thumbnail render.
 */
inline bool IsVideoThumbnailAttemptDue(std::uint64_t ullCurrentTick, std::uint64_t ullLastAttemptTick, std::uint64_t ullIntervalMs = kVideoThumbnailRefreshIntervalMs)
{
	return ullLastAttemptTick == 0 || ullCurrentTick < ullLastAttemptTick || ullCurrentTick - ullLastAttemptTick >= ullIntervalMs;
}

/**
 * Returns whether a user-hovered item should bypass the background retry interval.
 */
inline bool ShouldForceVideoThumbnailAttempt(bool bHighPriority, bool bHasCachedBitmap)
{
	return bHighPriority && !bHasCachedBitmap;
}

/**
 * Returns whether the current progress has reached a percentage threshold without overflowing large sizes.
 */
inline bool HasReachedCompletedPermille(std::uint64_t ullFileSize, std::uint64_t ullCompletedSize, std::uint64_t ullRequiredPermille)
{
	if (ullFileSize == 0)
		return false;
	if (ullCompletedSize >= ullFileSize)
		return true;

	const std::uint64_t ullWhole = (ullFileSize / 1000ull) * ullRequiredPermille;
	const std::uint64_t ullRemainder = ((ullFileSize % 1000ull) * ullRequiredPermille + 999ull) / 1000ull;
	return ullCompletedSize >= ullWhole + ullRemainder;
}

/**
 * Selects a conservative scene timestamp that moves deeper as more of a video arrives.
 */
inline std::uint32_t GetVideoThumbnailCaptureStartSecond(std::uint64_t ullFileSize, std::uint64_t ullCompletedSize)
{
	if (HasReachedCompletedPermille(ullFileSize, ullCompletedSize, 950ull))
		return 180;
	if (HasReachedCompletedPermille(ullFileSize, ullCompletedSize, 500ull))
		return 120;
	if (HasReachedCompletedPermille(ullFileSize, ullCompletedSize, 250ull))
		return 90;
	if (HasReachedCompletedPermille(ullFileSize, ullCompletedSize, 100ull))
		return 60;
	if (HasReachedCompletedPermille(ullFileSize, ullCompletedSize, 50ull))
		return 30;
	return 15;
}

/**
 * Builds a VLC command line that captures one thumbnail into the scene output directory.
 */
inline CString BuildVlcThumbnailCommandLine(const CString &rstrVlcPath, const CString &rstrInputPath, const CString &rstrOutputDirectory, const CString &rstrOutputPrefix, std::uint32_t uStartSecond)
{
	CString strCommandLine(FileCompletionCommandSeams::QuoteCommandLineArgument(rstrVlcPath));
	strCommandLine += _T(" --intf dummy --dummy-quiet --vout=dummy --no-embedded-video --no-video-deco --no-qt-error-dialogs");
	strCommandLine += _T(" --no-audio --no-video-title-show --no-sub-autodetect-file");
	strCommandLine += _T(" --video-filter=scene --scene-format=png --scene-ratio=1");
	strCommandLine.AppendFormat(_T(" --start-time=%u --stop-time=%u --run-time=1"), uStartSecond, uStartSecond + 1);
	strCommandLine += _T(" ");
	strCommandLine += FileCompletionCommandSeams::QuoteCommandLineArgument(_T("--scene-prefix=") + rstrOutputPrefix);
	strCommandLine += _T(" ");
	strCommandLine += FileCompletionCommandSeams::QuoteCommandLineArgument(_T("--scene-path=") + rstrOutputDirectory);
	strCommandLine += _T(" ");
	strCommandLine += FileCompletionCommandSeams::QuoteCommandLineArgument(rstrInputPath);
	strCommandLine += _T(" vlc://quit");
	return strCommandLine;
}
}
