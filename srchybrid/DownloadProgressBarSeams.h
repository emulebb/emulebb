#pragma once
#include "DisplayRefreshSeams.h"

namespace DownloadProgressBarSeams
{
inline bool HasDrawableExtent(const int iWidth, const int iHeight)
{
	return iWidth > 0 && iHeight > 0;
}

inline bool ShouldIsolateFlatBarDcState(const bool bUseFlatBar)
{
	return bUseFlatBar;
}

inline UINT GetStatusBitmapCacheDelayMs(UINT uDesktopUiRefreshIntervalMs)
{
	const UINT uNormalizedIntervalMs = NormalizeDesktopUiRefreshIntervalMs(uDesktopUiRefreshIntervalMs);
	return uNormalizedIntervalMs == DESKTOP_UI_REFRESH_PAUSED_MS ? DESKTOP_UI_REFRESH_BELOWNORMAL_MS : uNormalizedIntervalMs;
}
}
