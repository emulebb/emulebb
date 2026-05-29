#pragma once

#include <cstdint>

#include "Resource.h"

#define EMULEBB_TEST_HAVE_TRANSFER_WND_SEAMS 1

/**
 * @brief Testable policy helpers for Transfer window view-state recovery.
 */
namespace TransferWndSeams
{
constexpr int kSecondaryPaneDownloading = 0;
constexpr int kSecondaryPaneUploading = 1;
constexpr int kSecondaryPaneOnQueue = 2;
constexpr int kSecondaryPaneClients = 3;

constexpr std::uint32_t kPrimaryListSplit = IDC_DOWNLOADLIST + IDC_UPLOADLIST;

/**
 * @brief Reports whether a persisted or runtime secondary pane id is valid.
 */
inline bool IsValidSecondaryPane(const int nPane)
{
	return nPane == kSecondaryPaneDownloading
		|| nPane == kSecondaryPaneUploading
		|| nPane == kSecondaryPaneOnQueue
		|| nPane == kSecondaryPaneClients;
}

/**
 * @brief Resolves invalid secondary pane state to the historical uploading pane fallback.
 */
inline int NormalizeSecondaryPane(const int nPane)
{
	return IsValidSecondaryPane(nPane) ? nPane : kSecondaryPaneUploading;
}

/**
 * @brief Reports whether a primary list id names a Transfer window list mode.
 */
inline bool IsValidPrimaryListId(const std::uint32_t nListId)
{
	return nListId == kPrimaryListSplit
		|| nListId == IDC_DOWNLOADLIST
		|| nListId == IDC_UPLOADLIST
		|| nListId == IDC_QUEUELIST
		|| nListId == IDC_CLIENTLIST
		|| nListId == IDC_DOWNLOADCLIENTS;
}

/**
 * @brief Resolves invalid primary list state to the historical split-view fallback.
 */
inline std::uint32_t NormalizePrimaryListId(const std::uint32_t nListId)
{
	return IsValidPrimaryListId(nListId) ? nListId : kPrimaryListSplit;
}

/**
 * @brief Reports whether a single-pane primary list can route middle-click user details.
 */
inline bool IsUserDetailPrimaryListId(const std::uint32_t nListId)
{
	return nListId == IDC_UPLOADLIST
		|| nListId == IDC_QUEUELIST
		|| nListId == IDC_CLIENTLIST
		|| nListId == IDC_DOWNLOADCLIENTS;
}

/**
 * @brief Reports whether a split-view secondary pane can route middle-click user details.
 */
inline bool IsUserDetailSecondaryPane(const int nPane)
{
	return IsValidSecondaryPane(nPane);
}

/**
 * @brief Reports whether invalid Transfer window view state should be logged.
 */
inline bool ShouldLogInvalidState(const bool bIsValid)
{
	return !bIsValid;
}

/**
 * @brief Returns true only after every MFC image-list drag-start step has succeeded.
 */
inline bool ShouldCommitCategoryDragStart(const bool bHasDragImage, const bool bBeginDragSucceeded, const bool bDragEnterSucceeded)
{
	return bHasDragImage && bBeginDragSucceeded && bDragEnterSucceeded;
}

/**
 * @brief Reports whether mouse movement indicates a captured category drag was cancelled.
 */
inline bool ShouldCancelCategoryDragOnMouseMove(const bool bIsDragging, const bool bLeftButtonDown)
{
	return bIsDragging && !bLeftButtonDown;
}

inline int GetCategoryShortcutIndex(const unsigned int uMessage, const unsigned int uKey, const bool bCtrlDown, const bool bAltDown, const bool bShiftDown)
{
	if (uMessage != WM_KEYDOWN || !bCtrlDown || bAltDown || bShiftDown)
		return -1;
	if (uKey == '0')
		return 0;
	if (uKey >= '1' && uKey <= '9')
		return static_cast<int>(uKey - '0');
	return -1;
}
}
