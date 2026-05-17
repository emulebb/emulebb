#pragma once

#include "DialogInputParsingSeams.h"

#include <atlstr.h>
#include <vector>

#define EMULE_DIRECT_DOWNLOAD_SEAMS_HAS_CANCELLED_REGISTER_POLICY 1

namespace DirectDownloadSeams
{
/**
 * @brief Reports whether a newly created WinInet handle may enter the owner
 * cancellation registry for the current cancellation state.
 */
inline bool ShouldRegisterInternetHandleForCancellationState(bool bOwnerCancelled) noexcept
{
	return !bOwnerCancelled;
}

/**
 * @brief Normalizes the multiline direct-download edit text after focus loss.
 */
inline CString NormalizeDirectDownloadEditText(CString strLinks)
{
	if (!strLinks.IsEmpty()) {
		strLinks.Replace(_T("\n"), _T("\r\n"));
		strLinks.Replace(_T("\r\r"), _T("\r"));
	}
	return strLinks;
}

/**
 * @brief Splits direct-download text into the link tokens consumed by the dialog.
 */
inline std::vector<CString> TokenizeDirectDownloadLinks(const CString& strLinks)
{
	return DialogInputParsingSeams::TokenizeWhitespaceSeparatedText(strLinks);
}

/**
 * @brief Adds the parser-required trailing slash for eD2K file links when needed.
 */
inline CString NormalizeDirectDownloadLinkToken(CString strToken)
{
	strToken.Trim();
	if (!strToken.IsEmpty() && strToken[strToken.GetLength() - 1] != _T('/'))
		strToken += _T('/');
	return strToken;
}

/**
 * @brief Resolves the requested category to the dialog's safe default when tabs are unavailable.
 */
inline int NormalizeDirectDownloadCategorySelection(const int iSelectedCategory, const int iCategoryCount)
{
	if (iCategoryCount <= 0)
		return 0;
	return iSelectedCategory >= 0 && iSelectedCategory < iCategoryCount ? iSelectedCategory : 0;
}
}
