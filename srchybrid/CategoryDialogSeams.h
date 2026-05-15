#pragma once

#include "Resource.h"

#include <Windows.h>
#include <atlstr.h>
#include <tchar.h>

#include <vector>

namespace CategoryDialogSeams
{
constexpr UINT kCategoryPriorityLow = 0;
constexpr UINT kCategoryPriorityNormal = 1;
constexpr UINT kCategoryPriorityHigh = 2;

/**
 * @brief Normalizes a user-entered category title for validation and duplicate checks.
 */
inline CString NormalizeCategoryTitle(CString strTitle)
{
	strTitle.Trim();
	return strTitle;
}

/**
 * @brief Normalizes a free-form category text field before persistence.
 */
inline CString NormalizeCategoryText(CString strText)
{
	strText.Trim();
	return strText;
}

/**
 * @brief Compares category titles after the same normalization used by the UI.
 */
inline bool AreCategoryTitlesEquivalent(CString strLeft, CString strRight)
{
	return NormalizeCategoryTitle(strLeft).CompareNoCase(NormalizeCategoryTitle(strRight)) == 0;
}

/**
 * @brief Returns whether a category title already exists outside the edited index.
 */
inline bool CategoryTitleExists(const CString& strTitle, const std::vector<CString>& rCategoryTitles, INT_PTR iExcludeCategory)
{
	const CString strCandidate(NormalizeCategoryTitle(strTitle));
	if (strCandidate.IsEmpty())
		return false;

	for (INT_PTR i = 1; i < static_cast<INT_PTR>(rCategoryTitles.size()); ++i) {
		if (i == iExcludeCategory)
			continue;
		if (AreCategoryTitlesEquivalent(rCategoryTitles[static_cast<size_t>(i)], strCandidate))
			return true;
	}
	return false;
}

/**
 * @brief Bounds the category start priority to a supported eMule value.
 */
inline UINT NormalizeCategoryPriority(UINT uPriority)
{
	return uPriority <= kCategoryPriorityHigh ? uPriority : kCategoryPriorityNormal;
}

/**
 * @brief Returns whether the selected category is a user-editable category.
 */
inline bool IsCustomCategory(INT_PTR iCategory, INT_PTR iCategoryCount)
{
	return iCategory > 0 && iCategory < iCategoryCount;
}

/**
 * @brief Returns whether the selected category can be removed from the manager.
 */
inline bool CanRemoveCategory(INT_PTR iCategory, INT_PTR iCategoryCount, UINT uAssignedFiles)
{
	return IsCustomCategory(iCategory, iCategoryCount) && uAssignedFiles == 0;
}

/**
 * @brief Returns whether the selected category can be moved upward.
 */
inline bool CanMoveCategoryUp(INT_PTR iCategory, INT_PTR iCategoryCount)
{
	return IsCustomCategory(iCategory, iCategoryCount) && iCategory > 1;
}

/**
 * @brief Returns whether the selected category can be moved downward.
 */
inline bool CanMoveCategoryDown(INT_PTR iCategory, INT_PTR iCategoryCount)
{
	return IsCustomCategory(iCategory, iCategoryCount) && iCategory < iCategoryCount - 1;
}

/**
 * @brief Chooses the localized caption resource for add and edit category dialogs.
 */
inline UINT GetCategoryDialogCaptionResourceId(bool bAddDialog)
{
	return bAddDialog ? IDS_CAT_ADD : IDS_EDITCAT;
}
}
