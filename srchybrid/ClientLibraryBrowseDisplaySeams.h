#pragma once

#include <atlstr.h>

namespace ClientLibraryBrowseDisplaySeams
{
/**
 * @brief Returns true when the row should advertise the View Shared Files action.
 */
inline bool ShouldShowLibraryBrowseMarker(bool bHasDisplayName, bool bIsEd2kClient, bool bViewSharedFilesSupport)
{
	return bHasDisplayName && bIsEd2kClient && bViewSharedFilesSupport;
}

/**
 * @brief Appends a compact library-browse marker to a client display name.
 */
inline void AppendLibraryBrowseMarker(CString &rstrText, bool bHasDisplayName, bool bIsEd2kClient, bool bViewSharedFilesSupport)
{
	if (!ShouldShowLibraryBrowseMarker(bHasDisplayName, bIsEd2kClient, bViewSharedFilesSupport))
		return;

#ifdef _UNICODE
	rstrText.Append(_T(" "));
	rstrText.AppendChar(static_cast<TCHAR>(0xD83D));
	rstrText.AppendChar(static_cast<TCHAR>(0xDCC1));
#else
	rstrText.Append(_T(" [files]"));
#endif
}
}
