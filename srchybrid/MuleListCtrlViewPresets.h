#pragma once

#include <atlstr.h>

#include "MenuCmds.h"
#include "MuleListCtrlSeams.h"

namespace MuleListCtrlViewPresets
{
enum class ETableViewPreset
{
	Stock,
	Extended,
	Full,
};

enum class EColumnWidthMode
{
	Preserve,
	Reset,
};

struct SViewPresetCommand
{
	unsigned int uCommandId;
	ETableViewPreset ePreset;
	EColumnWidthMode eWidthMode;
};

struct SListControlViewPresetProfile
{
	LPCTSTR pszControlName;
	const int *piExtendedOrder;
	int iColumnCount;
	const int *piStockHiddenColumns;
	int iStockHiddenColumnCount;
	const int *piExtendedHiddenColumns;
	int iExtendedHiddenColumnCount;
};

inline constexpr SViewPresetCommand kViewPresetCommands[] = {
	{MP_HM_VIEW_PRESET_STOCK_KEEP_WIDTHS, ETableViewPreset::Stock, EColumnWidthMode::Preserve},
	{MP_HM_VIEW_PRESET_STOCK_RESET_WIDTHS, ETableViewPreset::Stock, EColumnWidthMode::Reset},
	{MP_HM_VIEW_PRESET_EXTENDED_KEEP_WIDTHS, ETableViewPreset::Extended, EColumnWidthMode::Preserve},
	{MP_HM_VIEW_PRESET_EXTENDED_RESET_WIDTHS, ETableViewPreset::Extended, EColumnWidthMode::Reset},
	{MP_HM_VIEW_PRESET_FULL_KEEP_WIDTHS, ETableViewPreset::Full, EColumnWidthMode::Preserve},
	{MP_HM_VIEW_PRESET_FULL_RESET_WIDTHS, ETableViewPreset::Full, EColumnWidthMode::Reset},
};

inline constexpr int kClientExtendedOrder[] = {0, 5, 1, 2, 3, 4, 6, 8, 7};
inline constexpr int kCommentExtendedOrder[] = {0, 1, 2, 3, 4};
inline constexpr int kDownloadClientsExtendedOrder[] = {0, 3, 4, 2, 1, 7, 5, 6, 8};
inline constexpr int kDownloadExtendedOrder[] = {0, 1, 3, 15, 5, 4, 9, 6, 8, 7, 2, 13, 10, 11, 12, 14, 16, 17};
inline constexpr int kQueueExtendedOrder[] = {0, 1, 13, 2, 4, 5, 3, 6, 8, 7, 12, 10, 11, 20, 21, 15, 14, 16, 17, 18, 19, 9};
inline constexpr int kSearchExtendedOrder[] = {0, 1, 2, 3, 4, 13, 14, 6, 7, 8, 9, 10, 11, 12, 5, 15};
inline constexpr int kServerExtendedOrder[] = {0, 1, 2, 3, 4, 5, 6, 13, 7, 9, 8, 10, 11, 12, 14, 15};
inline constexpr int kSharedFilesExtendedOrder[] = {0, 1, 2, 9, 8, 3, 10, 5, 6, 7, 18, 19, 11, 12, 13, 14, 15, 16, 17, 4};
inline constexpr int kUploadExtendedOrder[] = {0, 1, 2, 3, 18, 20, 6, 11, 4, 5, 10, 7, 8, 9, 19, 21, 13, 12, 14, 15, 16, 17};

inline constexpr int kDownloadStockHiddenColumns[] = {2, 10, 11, 12};
inline constexpr int kSearchStockHiddenColumns[] = {5, 12, 15};
inline constexpr int kSearchExtendedHiddenColumns[] = {5, 15};
inline constexpr int kServerStockHiddenColumns[] = {11, 12};
inline constexpr int kSharedFilesStockHiddenColumns[] = {4, 6, 9, 12, 13, 14, 15, 16, 17};
inline constexpr int kSharedFilesExtendedHiddenColumns[] = {4, 12, 13, 14, 15, 16, 17};

inline constexpr SListControlViewPresetProfile kProfiles[] = {
	{_T("ClientListCtrl"), kClientExtendedOrder, _countof(kClientExtendedOrder), nullptr, 0, nullptr, 0},
	{_T("CommentListCtrl"), kCommentExtendedOrder, _countof(kCommentExtendedOrder), nullptr, 0, nullptr, 0},
	{_T("DownloadClientsCtrl"), kDownloadClientsExtendedOrder, _countof(kDownloadClientsExtendedOrder), nullptr, 0, nullptr, 0},
	{_T("DownloadListCtrl"), kDownloadExtendedOrder, _countof(kDownloadExtendedOrder), kDownloadStockHiddenColumns, _countof(kDownloadStockHiddenColumns), nullptr, 0},
	{_T("QueueListCtrl"), kQueueExtendedOrder, _countof(kQueueExtendedOrder), nullptr, 0, nullptr, 0},
	{_T("SearchListCtrl"), kSearchExtendedOrder, _countof(kSearchExtendedOrder), kSearchStockHiddenColumns, _countof(kSearchStockHiddenColumns), kSearchExtendedHiddenColumns, _countof(kSearchExtendedHiddenColumns)},
	{_T("ServerListCtrl"), kServerExtendedOrder, _countof(kServerExtendedOrder), kServerStockHiddenColumns, _countof(kServerStockHiddenColumns), nullptr, 0},
	{_T("SharedFilesCtrl"), kSharedFilesExtendedOrder, _countof(kSharedFilesExtendedOrder), kSharedFilesStockHiddenColumns, _countof(kSharedFilesStockHiddenColumns), kSharedFilesExtendedHiddenColumns, _countof(kSharedFilesExtendedHiddenColumns)},
	{_T("UploadListCtrl"), kUploadExtendedOrder, _countof(kUploadExtendedOrder), nullptr, 0, nullptr, 0},
};

/**
 * @brief Resolves a Tools menu command into a table preset and width handling mode.
 */
inline bool TryGetViewPresetCommand(unsigned int uCommandId, ETableViewPreset &rePreset, EColumnWidthMode &reWidthMode)
{
	for (const SViewPresetCommand &command : kViewPresetCommands) {
		if (command.uCommandId == uCommandId) {
			rePreset = command.ePreset;
			reWidthMode = command.eWidthMode;
			return true;
		}
	}
	return false;
}

/**
 * @brief Returns the preset profile for a persisted list-control name.
 */
inline const SListControlViewPresetProfile *FindProfile(LPCTSTR pszControlName)
{
	if (pszControlName == NULL || *pszControlName == _T('\0'))
		return nullptr;
	const CString strControlName(pszControlName);
	for (const SListControlViewPresetProfile &profile : kProfiles) {
		if (strControlName.CompareNoCase(profile.pszControlName) == 0)
			return &profile;
	}
	return nullptr;
}

/**
 * @brief Returns whether a ListControlSetup key should be removed before applying a preset.
 */
inline bool ShouldResetPresetSuffix(LPCTSTR pszSuffix, EColumnWidthMode eWidthMode)
{
	if (pszSuffix == NULL || *pszSuffix == _T('\0'))
		return false;
	const CString strSuffix(pszSuffix);
	if (strSuffix.CompareNoCase(_T("ColumnOrders")) == 0 || strSuffix.CompareNoCase(_T("ColumnHidden")) == 0
		|| strSuffix.CompareNoCase(_T("TableSortItem")) == 0 || strSuffix.CompareNoCase(_T("TableSortAscending")) == 0
		|| strSuffix.CompareNoCase(_T("SortHistory")) == 0)
	{
		return true;
	}
	return eWidthMode == EColumnWidthMode::Reset && strSuffix.CompareNoCase(_T("ColumnWidths")) == 0;
}

/**
 * @brief Validates that hidden-column defaults are unique and in range for a preset profile.
 */
inline bool IsHiddenColumnSetValid(const int *piHiddenColumns, int iHiddenColumnCount, int iColumnCount)
{
	if (piHiddenColumns == nullptr)
		return iHiddenColumnCount == 0;
	if (iHiddenColumnCount < 0 || iColumnCount <= 0)
		return false;
	for (int i = 0; i < iHiddenColumnCount; ++i) {
		const int iHiddenColumn = piHiddenColumns[i];
		if (iHiddenColumn <= 0 || iHiddenColumn >= iColumnCount)
			return false;
		for (int j = i + 1; j < iHiddenColumnCount; ++j) {
			if (piHiddenColumns[j] == iHiddenColumn)
				return false;
		}
	}
	return true;
}

/**
 * @brief Validates all static preset data for one list-control profile.
 */
inline bool IsProfileValid(const SListControlViewPresetProfile &profile)
{
	return profile.pszControlName != NULL && *profile.pszControlName != _T('\0')
		&& MuleListCtrlSeams::IsCompleteColumnOrder(profile.piExtendedOrder, profile.iColumnCount)
		&& IsHiddenColumnSetValid(profile.piStockHiddenColumns, profile.iStockHiddenColumnCount, profile.iColumnCount)
		&& IsHiddenColumnSetValid(profile.piExtendedHiddenColumns, profile.iExtendedHiddenColumnCount, profile.iColumnCount);
}
}
