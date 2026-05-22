#pragma once

#include "PreferenceIniMap.h"

#include <atlstr.h>

namespace BBPreferenceMigrationSeams
{
inline constexpr int kCurrentPreferenceSchema = 1;
inline constexpr const TCHAR *kPreferenceSchemaKey = PreferenceIniMap::MigrationKeys::PreferenceSchema;
inline constexpr const TCHAR *kListControlSetupSection = _T("ListControlSetup");

inline constexpr const TCHAR *kMainGridListControlNames[] = {
	_T("ClientListCtrl"),
	_T("CommentListCtrl"),
	_T("DownloadClientsCtrl"),
	_T("DownloadListCtrl"),
	_T("QueueListCtrl"),
	_T("SearchListCtrl"),
	_T("ServerListCtrl"),
	_T("SharedFilesCtrl"),
	_T("UploadListCtrl"),
};

inline constexpr const TCHAR *kMainGridListControlResetSuffixes[] = {
	_T("ColumnOrders"),
	_T("ColumnHidden"),
	_T("ColumnWidths"),
	_T("TableSortItem"),
	_T("TableSortAscending"),
	_T("SortHistory"),
};

/**
 * @brief Determines whether eMuleBB preference migrations should run.
 */
inline bool ShouldRunPreferenceMigration(int iStoredSchema, int iCurrentSchema = kCurrentPreferenceSchema)
{
	return iStoredSchema < iCurrentSchema;
}

/**
 * @brief Tests whether a list-control preference key belongs to the main user-facing grids.
 */
inline bool IsMainGridListControlName(LPCTSTR pszName)
{
	if (pszName == NULL || *pszName == _T('\0'))
		return false;
	const CString strName(pszName);
	for (const TCHAR *pszKnownName : kMainGridListControlNames) {
		if (strName.CompareNoCase(pszKnownName) == 0)
			return true;
	}
	return false;
}

/**
 * @brief Tests whether a list-control preference suffix is reset by the BB schema migration.
 */
inline bool IsMainGridListControlResetSuffix(LPCTSTR pszSuffix)
{
	if (pszSuffix == NULL || *pszSuffix == _T('\0'))
		return false;
	const CString strSuffix(pszSuffix);
	for (const TCHAR *pszKnownSuffix : kMainGridListControlResetSuffixes) {
		if (strSuffix.CompareNoCase(pszKnownSuffix) == 0)
			return true;
	}
	return false;
}

/**
 * @brief Builds one ListControlSetup key from a control name and persisted-state suffix.
 */
inline CString BuildListControlSetupKey(LPCTSTR pszControlName, LPCTSTR pszSuffix)
{
	CString strKey(pszControlName != NULL ? pszControlName : _T(""));
	strKey += (pszSuffix != NULL ? pszSuffix : _T(""));
	return strKey;
}
}
