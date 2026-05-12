#pragma once

#include <atlstr.h>
#include <cstdint>
#include "FileCompletionCommandSeams.h"

namespace PartFilePreviewSeams
{
constexpr std::uint64_t kPartialVideoPreviewMinCompletedPermille = 5;
constexpr std::uint64_t kPartialVideoPreviewMinCompletedBytes = 1ull * 1024ull * 1024ull;
constexpr std::uint64_t kPartialVideoPreviewMaxCompletedBytes = 64ull * 1024ull * 1024ull;

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
 * Builds a VLC command line that captures one thumbnail into the scene output directory.
 */
inline CString BuildVlcThumbnailCommandLine(const CString &rstrVlcPath, const CString &rstrInputPath, const CString &rstrOutputDirectory, const CString &rstrOutputPrefix)
{
	CString strCommandLine(FileCompletionCommandSeams::QuoteCommandLineArgument(rstrVlcPath));
	strCommandLine += _T(" --intf dummy --dummy-quiet --no-audio --no-video-title-show --no-sub-autodetect-file");
	strCommandLine += _T(" --video-filter=scene --scene-format=png --scene-ratio=1 --start-time=15 --stop-time=16 --run-time=1");
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
