//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#pragma once

namespace ReleaseUpdateCheck
{
	/**
	 * @brief Runtime outcome of checking GitHub Releases for a newer eMuleBB package.
	 */
	enum class EUpdateCheckStatus
	{
		Failed,
		NoNewerVersion,
		NewerVersionAvailable
	};

	/**
	 * @brief Full update-check result consumed by the UI thread.
	 */
	struct SUpdateCheckResult
	{
		EUpdateCheckStatus eStatus = EUpdateCheckStatus::Failed;
		CString strLatestVersion;
		CString strReleaseUrl;
		CString strRequiredAssetName;
		CString strError;
	};

	/**
	 * @brief Fetches the GitHub latest-release JSON and evaluates it for this binary.
	 */
	SUpdateCheckResult CheckLatestRelease();
}
