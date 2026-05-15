#pragma once

#include <atlstr.h>

/**
 * @brief Appends a right-aligned keyboard shortcut hint to a native menu label.
 */
inline CString AddMenuShortcutLabel(const CString &strLabel, LPCTSTR pszShortcut)
{
	CString strMenuLabel(strLabel);
	if (pszShortcut != NULL && *pszShortcut != _T('\0')) {
		strMenuLabel += _T("\t");
		strMenuLabel += pszShortcut;
	}
	return strMenuLabel;
}
