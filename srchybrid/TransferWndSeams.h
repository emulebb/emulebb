#pragma once

#include <cwchar>
#include <cstdint>
#include <string>

#include "Resource.h"

#define EMULEBB_TEST_HAVE_TRANSFER_WND_SEAMS 1
#define EMULEBB_TEST_HAVE_CATEGORY_SHORTCUTS 1
#define EMULEBB_TEST_HAVE_TRANSFER_LIST_SHORTCUTS 1

/**
 * @brief Testable policy helpers for Transfer window view-state recovery.
 */
namespace TransferWndSeams
{
enum class ETransferListShortcutCommand
{
	None,
	Downloads,
	Uploading,
	OnQueue,
	KnownClients,
	DownloadingClients
};

constexpr int kSecondaryPaneDownloading = 0;
constexpr int kSecondaryPaneUploading = 1;
constexpr int kSecondaryPaneOnQueue = 2;
constexpr int kSecondaryPaneClients = 3;

constexpr std::uint32_t kPrimaryListSplit = IDC_DOWNLOADLIST + IDC_UPLOADLIST;
constexpr std::uint32_t kUploadUtilizationDisplayPercentMax = 999u;

/**
 * @brief Returns a rounded upload utilization percentage for compact queue status text.
 */
inline std::uint32_t CalculateUploadUtilizationPercent(
	const std::uint32_t uCurrentBytesPerSec,
	const std::uint32_t uMaxBytesPerSec,
	const std::uint32_t uDisplayCapPercent = kUploadUtilizationDisplayPercentMax)
{
	if (uMaxBytesPerSec == 0u)
		return 0u;
	const std::uint64_t ullPercent = (static_cast<std::uint64_t>(uCurrentBytesPerSec) * 100u + uMaxBytesPerSec / 2u) / uMaxBytesPerSec;
	return static_cast<std::uint32_t>(ullPercent > uDisplayCapPercent ? uDisplayCapPercent : ullPercent);
}

/**
 * @brief Formats a byte-per-second upload rate as a compact MB/s value.
 */
inline std::wstring FormatUploadRateMbValue(const std::uint32_t uBytesPerSec)
{
	wchar_t awchBuffer[32] = {};
	const double dMiBPerSec = static_cast<double>(uBytesPerSec) / (1024.0 * 1024.0);
	std::swprintf(awchBuffer, sizeof(awchBuffer) / sizeof(awchBuffer[0]), L"%.1f", dMiBPerSec);
	return awchBuffer;
}

/**
 * @brief Formats the Transfer-window queue footer with compact broadband upload state.
 */
inline std::wstring FormatQueueCountText(
	const std::uint64_t ullWaitingClients,
	const std::uint64_t ullBannedClients,
	const std::wstring &rstrBannedLabel,
	const std::int64_t iActiveUploadSlots,
	const std::int64_t iBaseUploadSlots,
	const std::int64_t iEffectiveUploadSlotCap,
	const std::uint32_t uElasticPercent,
	const std::uint32_t uCurrentUploadBytesPerSec,
	const std::uint32_t uMaxUploadBytesPerSec)
{
	const std::uint32_t uUtilizationPercent = CalculateUploadUtilizationPercent(uCurrentUploadBytesPerSec, uMaxUploadBytesPerSec);
	return std::to_wstring(ullWaitingClients)
		+ L" (" + std::to_wstring(ullBannedClients) + L" " + rstrBannedLabel + L")"
		+ L" | UL " + std::to_wstring(iActiveUploadSlots) + L"/" + std::to_wstring(iBaseUploadSlots) + L"-" + std::to_wstring(iEffectiveUploadSlotCap)
		+ L" +" + std::to_wstring(uElasticPercent) + L"%"
		+ L" | " + FormatUploadRateMbValue(uCurrentUploadBytesPerSec) + L"/" + FormatUploadRateMbValue(uMaxUploadBytesPerSec)
		+ L" MB/s " + std::to_wstring(uUtilizationPercent) + L"%";
}

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

inline ETransferListShortcutCommand ClassifyTransferListShortcut(const unsigned int uMessage, const unsigned int uKey, const bool bCtrlDown, const bool bAltDown, const bool bShiftDown)
{
	if (uMessage != WM_KEYDOWN || !bCtrlDown || bAltDown)
		return ETransferListShortcutCommand::None;
	if (uKey == 'D')
		return bShiftDown ? ETransferListShortcutCommand::DownloadingClients : ETransferListShortcutCommand::Downloads;
	if (bShiftDown)
		return ETransferListShortcutCommand::None;
	if (uKey == 'U')
		return ETransferListShortcutCommand::Uploading;
	if (uKey == 'Q')
		return ETransferListShortcutCommand::OnQueue;
	if (uKey == 'K')
		return ETransferListShortcutCommand::KnownClients;
	return ETransferListShortcutCommand::None;
}
}
