//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( devs@emule-project.net / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
#pragma once

#include "FakeFileDetectorSeams.h"
#include "SearchTrustHintSeams.h"

#include <vector>

class CAbstractFile;
class CPartFile;
class CSearchFile;

/**
 * @brief App-facing fake-file report using MFC string containers for UI and
 * REST serialization.
 */
struct SFakeFileReport
{
	uint32 nScore = 0;
	FakeFileDetectorSeams::Severity eSeverity = FakeFileDetectorSeams::Severity::None;
	std::vector<CString> astrReasons;
	std::vector<CString> astrObservedNames;
	std::vector<CString> astrObservedExtensions;
	std::vector<CString> astrCanonicalNames;
	std::vector<CString> astrIgnoredNameTokens;
	std::vector<CString> astrNameDivergenceGroups;
	CString strClaimedType;
	EFileType eExtensionType = FILETYPE_UNKNOWN;
	EFileType eHeaderType = FILETYPE_UNKNOWN;
	bool bPendingHeaderCheck = false;
	bool bCached = false;
};

namespace FakeFileDetector
{
/**
 * @brief Returns the editable fake-file rule file path in the config directory.
 */
CString GetRuleFilePath();

/**
 * @brief Reloads the editable local bad-signal rules from FakeFileFilter.dat.
 */
bool ReloadRules();

/**
 * @brief Writes the local FakeFile.met analyzer cache when it has changed.
 */
void SaveCache();

/**
 * @brief Produces a local warning-only fake-file analysis for one search row.
 */
SFakeFileReport AnalyzeSearchFile(const CSearchFile &rSearchFile);

/**
 * @brief Produces a local warning-only fake-file analysis for one download.
 */
SFakeFileReport AnalyzePartFile(CPartFile &rPartFile);

/**
 * @brief Produces a render-safe search report without updating FakeFile.met.
 */
SFakeFileReport GetSearchFileReportSnapshot(const CSearchFile &rSearchFile);

/**
 * @brief Produces a render-safe download report without opening the part file
 * or updating FakeFile.met.
 */
SFakeFileReport GetPartFileReportSnapshot(CPartFile &rPartFile);

/**
 * @brief Probes newly available part-file header ranges outside render paths.
 */
bool RefreshPartFileHeaderIfAvailable(CPartFile &rPartFile);

/**
 * @brief Formats a shared trust hint for list columns and tooltip summaries.
 */
CString FormatTrustHint(const SearchTrustHintSeams::TrustHint &rHint);

/**
 * @brief Returns a compact localized score and reason summary.
 */
CString FormatReportSummary(const SFakeFileReport &rReport);

/**
 * @brief Returns localized score, reason, and evidence details for tooltips.
 */
CString FormatReportDetails(const SFakeFileReport &rReport);

/**
 * @brief Converts a severity bucket to the stable REST token.
 */
const char* SeverityToToken(FakeFileDetectorSeams::Severity eSeverity);
}
