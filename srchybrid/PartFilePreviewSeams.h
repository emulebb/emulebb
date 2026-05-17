#pragma once

#include <atlstr.h>
#include <cstdint>
#include "FileCompletionCommandSeams.h"
#include "LongPathSeams.h"
#include "PreferenceValidationSeams.h"

namespace PartFilePreviewSeams
{
enum EVideoThumbnailAttemptResult
{
	VTAR_NONE = 0,
	VTAR_COPY_FAILED,
	VTAR_FFMPEG_BUSY,
	VTAR_FFMPEG_TIMEOUT,
	VTAR_FFMPEG_START_FAILED,
	VTAR_FFMPEG_FAILED,
	VTAR_NO_THUMBNAIL,
	VTAR_DECODE_FAILED,
	VTAR_CACHE_WRITE_FAILED,
	VTAR_SUCCESS,
	VTAR_EXCEPTION
};

constexpr std::uint64_t kPartialVideoPreviewMinCompletedPermille = 5;
constexpr std::uint64_t kPartialVideoPreviewMinCompletedBytes = 1ull * 1024ull * 1024ull;
constexpr std::uint64_t kPartialVideoPreviewMaxCompletedBytes = 64ull * 1024ull * 1024ull;
constexpr UINT kVideoThumbnailDefaultIntervalSeconds = PreferenceValidationSeams::kVideoThumbnailDefaultIntervalSeconds;
constexpr UINT kVideoThumbnailMinIntervalSeconds = PreferenceValidationSeams::kVideoThumbnailMinIntervalSeconds;
constexpr UINT kVideoThumbnailRecommendedIntervalSeconds = PreferenceValidationSeams::kVideoThumbnailRecommendedIntervalSeconds;
constexpr UINT kVideoThumbnailMaxIntervalSeconds = PreferenceValidationSeams::kVideoThumbnailMaxIntervalSeconds;
constexpr std::uint64_t kVideoThumbnailRefreshDeltaPermille = 50;
constexpr std::uint64_t kVideoThumbnailRefreshMaxDeltaBytes = 128ull * 1024ull * 1024ull;
constexpr int kVideoThumbnailDisplayMaxWidth = 480;
constexpr int kVideoThumbnailWorkerThreadPriority = THREAD_PRIORITY_LOWEST;
constexpr DWORD kFfmpegThumbnailTimeoutMs = 30u * 1000u;
constexpr std::uint8_t kPeerPreviewFrameCount = 4;
constexpr std::uint32_t kPeerPreviewFirstFrameSecond = 15;
constexpr std::uint32_t kPeerPreviewFrameStepSeconds = 50;
constexpr int kPeerPreviewFrameMaxWidth = 450;

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
 * Returns whether the configured thumbnail helper is an existing FFmpeg executable.
 */
inline bool IsValidConfiguredFfmpegPath(const CString &rstrFfmpegPath)
{
	CString strFfmpegPath(rstrFfmpegPath);
	strFfmpegPath.Trim();
	const LPCTSTR pszExt = ::PathFindExtension(strFfmpegPath);
	return !strFfmpegPath.IsEmpty() && pszExt != NULL && _tcsicmp(pszExt, _T(".exe")) == 0 && LongPathSeams::PathExists(strFfmpegPath);
}

/**
 * Bounds the thumbnail scan/retry interval; zero intentionally disables thumbnail generation.
 */
inline UINT NormalizeVideoThumbnailIntervalSeconds(UINT uIntervalSeconds)
{
	return PreferenceValidationSeams::NormalizeVideoThumbnailIntervalSeconds(uIntervalSeconds);
}

/**
 * Returns whether the configured thumbnail interval enables generation.
 */
inline bool IsVideoThumbnailIntervalEnabled(UINT uIntervalSeconds)
{
	return NormalizeVideoThumbnailIntervalSeconds(uIntervalSeconds) > 0;
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
 * Returns whether enough time has elapsed to attempt another thumbnail render.
 */
inline bool IsVideoThumbnailAttemptDue(std::uint64_t ullCurrentTick, std::uint64_t ullLastAttemptTick, std::uint64_t ullIntervalMs)
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
 * Returns whether the download list should launch a thumbnail helper now.
 */
inline bool ShouldStartVideoThumbnailWorker(bool bWorkerActive, bool bWindowAvailable, bool bAppClosing)
{
	return !bWorkerActive && bWindowAvailable && !bAppClosing;
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
 * Builds an FFmpeg command line that captures one thumbnail into the exact output path.
 */
inline CString BuildFfmpegThumbnailCommandLine(const CString &rstrFfmpegPath, const CString &rstrInputPath, const CString &rstrOutputPath, std::uint32_t uStartSecond)
{
	CString strCommandLine(FileCompletionCommandSeams::QuoteCommandLineArgument(rstrFfmpegPath));
	strCommandLine += _T(" -hide_banner -loglevel error -y");
	strCommandLine.AppendFormat(_T(" -ss %u"), uStartSecond);
	strCommandLine += _T(" -fflags +genpts+discardcorrupt -err_detect ignore_err -analyzeduration 5M -probesize 5M -i ");
	strCommandLine += FileCompletionCommandSeams::QuoteCommandLineArgument(rstrInputPath);
	strCommandLine += _T(" -an -frames:v 1 -vf ");
	strCommandLine += FileCompletionCommandSeams::QuoteCommandLineArgument(_T("scale=480:-2:force_original_aspect_ratio=decrease"));
	strCommandLine += _T(" ");
	strCommandLine += FileCompletionCommandSeams::QuoteCommandLineArgument(rstrOutputPath);
	return strCommandLine;
}

/**
 * Selects the timestamp for a wire-compatible peer preview frame.
 */
inline std::uint32_t GetPeerPreviewFrameSecond(std::uint8_t uFrameIndex)
{
	return kPeerPreviewFirstFrameSecond + static_cast<std::uint32_t>(uFrameIndex) * kPeerPreviewFrameStepSeconds;
}

/**
 * Returns whether this client should advertise and serve peer preview frames.
 */
inline bool ShouldAllowPeerPreview(bool bAllowPeerPreview, bool bSharesVisible, bool bHasValidFfmpegPath)
{
	return bAllowPeerPreview && bSharesVisible && bHasValidFfmpegPath;
}

/**
 * Builds an FFmpeg command line that captures one peer-preview PNG frame.
 */
inline CString BuildFfmpegPeerPreviewFrameCommandLine(const CString &rstrFfmpegPath, const CString &rstrInputPath, const CString &rstrOutputPath, std::uint32_t uStartSecond)
{
	CString strCommandLine(FileCompletionCommandSeams::QuoteCommandLineArgument(rstrFfmpegPath));
	strCommandLine += _T(" -hide_banner -loglevel error -y");
	strCommandLine.AppendFormat(_T(" -ss %u"), uStartSecond);
	strCommandLine += _T(" -fflags +genpts+discardcorrupt -err_detect ignore_err -analyzeduration 5M -probesize 5M -i ");
	strCommandLine += FileCompletionCommandSeams::QuoteCommandLineArgument(rstrInputPath);
	strCommandLine += _T(" -an -frames:v 1 -vf ");
	CString strScaleFilter;
	strScaleFilter.Format(_T("scale=%d:-2:force_original_aspect_ratio=decrease"), kPeerPreviewFrameMaxWidth);
	strCommandLine += FileCompletionCommandSeams::QuoteCommandLineArgument(strScaleFilter);
	strCommandLine += _T(" ");
	strCommandLine += FileCompletionCommandSeams::QuoteCommandLineArgument(rstrOutputPath);
	return strCommandLine;
}
}
