//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.

#pragma once

enum class EDownloadHostnameResolveDispatch
{
	Drop,
	AddPackedSource,
	AddUrlSource
};

/**
 * @brief Classifies a resolved source hostname before touching download-queue state.
 */
inline EDownloadHostnameResolveDispatch GetDownloadHostnameResolveDispatch(const bool bFileStillExists, const bool bLookupSucceeded, const bool bHasIpv4Address, const bool bHasUrl)
{
	if (!bFileStillExists || !bLookupSucceeded || !bHasIpv4Address)
		return EDownloadHostnameResolveDispatch::Drop;
	return bHasUrl ? EDownloadHostnameResolveDispatch::AddUrlSource : EDownloadHostnameResolveDispatch::AddPackedSource;
}

namespace DownloadQueueHostnameResolverSeams
{
using ResolveDispatch = EDownloadHostnameResolveDispatch;

/**
 * @brief Classifies a resolved ed2k hostname source without enabling URL-source dispatch.
 */
inline ResolveDispatch GetResolveDispatch(const bool bFileStillExists, const bool bLookupSucceeded, const bool bHasIpv4Address)
{
	return GetDownloadHostnameResolveDispatch(bFileStillExists, bLookupSucceeded, bHasIpv4Address, false);
}
}
