#pragma once

#include <atlstr.h>

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
}
