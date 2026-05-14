#pragma once

#include <atlstr.h>
#include <shlwapi.h>

/**
 * @brief Testable helpers for download queue automatic category matching.
 */
namespace DownloadQueueAutoCatSeams
{
/**
 * @brief Returns true when a non-regex automatic category rule matches a file name.
 */
inline bool MatchesNonRegexAutoCategory(CString strAutoCategoryPattern, CString strFileName)
{
	if (strAutoCategoryPattern.IsEmpty() || strFileName.IsEmpty())
		return false;

	strAutoCategoryPattern.MakeLower();
	strFileName.MakeLower();

	for (int iPos = 0; iPos >= 0;) {
		const CString strToken(strAutoCategoryPattern.Tokenize(_T("|"), iPos));
		if (strToken.IsEmpty())
			continue;

		if ((strToken.FindOneOf(_T("*?")) >= 0 && ::PathMatchSpec(strFileName, strToken))
			|| strFileName.Find(strToken) >= 0)
			return true;
	}

	return false;
}
}
