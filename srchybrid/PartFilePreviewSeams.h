#pragma once

#include <atlstr.h>
#include "FileCompletionCommandSeams.h"

namespace PartFilePreviewSeams
{
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
