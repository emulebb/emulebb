#pragma once

#include <atlstr.h>
#include <tchar.h>

namespace CommentDialogSeams
{
constexpr int kMinFileRating = 0;
constexpr int kMaxFileRating = 5;

/**
 * @brief Normalizes the persisted comment spam-filter token list.
 */
inline CString NormalizeCommentFilterText(CString strCommentFilters)
{
	strCommentFilters.MakeLower();
	CString strNewCommentFilters;
	for (int iPos = 0; iPos >= 0;) {
		CString strFilter(strCommentFilters.Tokenize(_T("|"), iPos));
		strFilter.Trim();
		if (!strFilter.IsEmpty()) {
			if (!strNewCommentFilters.IsEmpty())
				strNewCommentFilters += _T('|');
			strNewCommentFilters += strFilter;
		}
	}
	return strNewCommentFilters;
}

/**
 * @brief Returns whether a rating combo selection can be written to a file.
 */
inline bool IsValidRatingSelection(const int iRating)
{
	return iRating >= kMinFileRating && iRating <= kMaxFileRating;
}

/**
 * @brief Returns whether a comment text edit should overwrite stored comments.
 */
inline bool ShouldWriteCommentText(const bool bMergedComment, const bool bCommentTextEmpty)
{
	return !bCommentTextEmpty || !bMergedComment;
}

/**
 * @brief Returns whether the selected-file set should enable comment editing.
 */
inline bool ShouldEnableCommentEditing(const bool bContainsSharedKnownFile)
{
	return bContainsSharedKnownFile;
}

/**
 * @brief Returns whether the Kad comment search button should be enabled.
 */
inline bool ShouldEnableKadCommentSearchButton(const bool bDialogEnabled, const bool bKadConnected, const bool bKadSearchable)
{
	return bDialogEnabled && bKadConnected && bKadSearchable;
}

/**
 * @brief Bounds the number of note searches that one button click may enqueue.
 */
inline int GetKadCommentSearchLimit(const int iFileCount, const int iKadTotalFileLimit)
{
	if (iFileCount <= 0 || iKadTotalFileLimit <= 0)
		return 0;
	return iFileCount < iKadTotalFileLimit ? iFileCount : iKadTotalFileLimit;
}

/**
 * @brief Returns whether a file can enter the editable comment dialog's Kad-note lookup.
 */
inline bool CanQueueEditableKadCommentSearch(const bool bDialogEnabled, const bool bKadConnected, const bool bIsKnownFile, const bool bIsSharedKnownFile, const bool bAlreadySearching, const int iQueuedCount, const int iQueueLimit)
{
	return bDialogEnabled
		&& bKadConnected
		&& bIsKnownFile
		&& bIsSharedKnownFile
		&& !bAlreadySearching
		&& iQueuedCount >= 0
		&& iQueuedCount < iQueueLimit;
}

/**
 * @brief Returns whether a file can enter the read-only comment-list Kad-note lookup.
 */
inline bool CanQueueListKadCommentSearch(const bool bKadConnected, const bool bHasFile, const bool bAlreadySearching, const int iQueuedCount, const int iQueueLimit)
{
	return bKadConnected
		&& bHasFile
		&& !bAlreadySearching
		&& iQueuedCount >= 0
		&& iQueuedCount < iQueueLimit;
}
}
