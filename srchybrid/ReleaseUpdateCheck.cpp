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
#include "stdafx.h"
#include <string>
#include "ReleaseUpdateCheck.h"
#include "ReleaseUpdateCheckSeams.h"
#include "HttpTransfer.h"
#include "Preferences.h"
#include "Version.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	CString CStringFromUtf8(const std::string &strValue)
	{
		if (strValue.empty())
			return CString();
		return CString(CA2T(strValue.c_str(), CP_UTF8));
	}

	CString GetReleaseCheckUserAgent()
	{
		CString strUserAgent;
	strUserAgent.Format(_T("emulebb/%s"), MOD_RELEASE_VERSION_TEXT);
		return strUserAgent;
	}

	bool FetchLatestReleaseJson(std::string &strJson, CString &strError)
	{
		HttpTransfer::SRequest request = HttpTransfer::MakeRequest(HttpTransferSeams::ERequestProfile::ReleaseUpdateJson, thePrefs.GetVersionCheckApiURL());
		request.strUserAgent = GetReleaseCheckUserAgent();
		request.strHeaders = _T("Accept: application/vnd.github+json\r\n")
			_T("Accept-Encoding: identity\r\n")
			_T("X-GitHub-Api-Version: 2022-11-28\r\n");
		if (!HttpTransfer::FetchToMemory(request, strJson, strError))
			return false;
		return true;
	}
}

ReleaseUpdateCheck::SUpdateCheckResult ReleaseUpdateCheck::CheckLatestRelease()
{
	SUpdateCheckResult result;

	std::string strJson;
	if (!FetchLatestReleaseJson(strJson, result.strError))
		return result;

	const ReleaseUpdateCheckSeams::SModReleaseVersion localVersion = {
		MOD_RELEASE_VERSION_MAJOR,
		MOD_RELEASE_VERSION_MINOR,
		MOD_RELEASE_VERSION_PATCH
	};
	ReleaseUpdateCheckSeams::SReleaseEvaluation evaluation = ReleaseUpdateCheckSeams::EvaluateLatestReleaseJson(
		strJson,
		localVersion,
		ReleaseUpdateCheckSeams::GetCurrentPlatformAssetToken());

	result.strLatestVersion = CStringFromUtf8(ReleaseUpdateCheckSeams::FormatReleaseVersion(evaluation.version));
	result.strReleaseUrl = CStringFromUtf8(evaluation.strReleaseUrl);
	result.strRequiredAssetName = CStringFromUtf8(evaluation.strRequiredAssetName);
	result.strError = CStringFromUtf8(evaluation.strError);

	switch (evaluation.eStatus) {
	case ReleaseUpdateCheckSeams::EReleaseEvaluationStatus::Newer:
		result.eStatus = EUpdateCheckStatus::NewerVersionAvailable;
		break;
	case ReleaseUpdateCheckSeams::EReleaseEvaluationStatus::NotNewer:
		result.eStatus = EUpdateCheckStatus::NoNewerVersion;
		break;
	case ReleaseUpdateCheckSeams::EReleaseEvaluationStatus::MissingAsset:
	case ReleaseUpdateCheckSeams::EReleaseEvaluationStatus::IgnoredRelease:
	case ReleaseUpdateCheckSeams::EReleaseEvaluationStatus::ParseFailed:
	default:
		result.eStatus = EUpdateCheckStatus::Failed;
		if (result.strError.IsEmpty())
			result.strError = _T("Latest release could not be evaluated.");
		break;
	}

	return result;
}
