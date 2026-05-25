#pragma once

#include <atlstr.h>
#include <tchar.h>
#include <windows.h>

namespace StartupConfigOverride
{
	inline bool IsDriveRootedPath(const CString &strPath)
	{
		return strPath.GetLength() >= 3
			&& ((strPath[0] >= _T('A') && strPath[0] <= _T('Z')) || (strPath[0] >= _T('a') && strPath[0] <= _T('z')))
			&& strPath[1] == _T(':')
			&& strPath[2] == _T('\\');
	}

	inline CString TrimTrailingBaseDirSeparator(const CString &strPath)
	{
		CString strTrimmed(strPath);
		while (strTrimmed.GetLength() > 3 && strTrimmed.Right(1) == _T("\\"))
			strTrimmed.Truncate(strTrimmed.GetLength() - 1);
		return strTrimmed;
	}

	inline bool TryGetFullPathName(const CString &strPath, CString &rstrFullPath)
	{
		rstrFullPath.Empty();
		const DWORD dwRequired = ::GetFullPathName(strPath, 0, NULL, NULL);
		if (dwRequired == 0)
			return false;

		LPTSTR pszBuffer = rstrFullPath.GetBuffer(dwRequired);
		const DWORD dwLength = ::GetFullPathName(strPath, dwRequired, pszBuffer, NULL);
		if (dwLength == 0 || dwLength >= dwRequired) {
			rstrFullPath.ReleaseBuffer(0);
			return false;
		}

		rstrFullPath.ReleaseBuffer(dwLength);
		return true;
	}

	/**
	 * @brief Reports whether the supplied path is a canonical absolute Win32 drive path.
	 */
	inline bool IsAbsoluteBaseDirPath(const CString &strPath)
	{
		if (strPath.Find(_T('/')) >= 0)
			return false;

		if (!IsDriveRootedPath(strPath))
			return false;

		CString strFullPath;
		return TryGetFullPathName(strPath, strFullPath)
			&& IsDriveRootedPath(strFullPath)
			&& TrimTrailingBaseDirSeparator(strFullPath) == TrimTrailingBaseDirSeparator(strPath);
	}

	/**
	 * @brief Normalizes the override base directory into the canonical trailing-backslash form used by startup code.
	 */
	inline CString NormalizeBaseDir(const CString &strBaseDir)
	{
		CString strNormalized(strBaseDir);
		while (strNormalized.GetLength() > 3 && strNormalized.Right(1) == _T("\\"))
			strNormalized.Truncate(strNormalized.GetLength() - 1);
		if (!strNormalized.IsEmpty() && strNormalized.Right(1) != _T("\\"))
			strNormalized.AppendChar(_T('\\'));
		return strNormalized;
	}

	/**
	 * @brief Builds the effective config directory path below the selected base directory.
	 */
	inline CString GetConfigDirectoryFromBaseDir(const CString &strBaseDir)
	{
		return NormalizeBaseDir(strBaseDir) + _T("config\\");
	}

	/**
	 * @brief Builds the effective data base directory for the selected base directory.
	 */
	inline CString GetDataDirectoryFromBaseDir(const CString &strBaseDir)
	{
		return NormalizeBaseDir(strBaseDir);
	}

	/**
	 * @brief Builds the default temp directory path below the selected base directory.
	 */
	inline CString GetTempDirectoryFromBaseDir(const CString &strBaseDir)
	{
		return NormalizeBaseDir(strBaseDir) + _T("Temp\\");
	}

	/**
	 * @brief Builds the default incoming directory path below the selected base directory.
	 */
	inline CString GetIncomingDirectoryFromBaseDir(const CString &strBaseDir)
	{
		return NormalizeBaseDir(strBaseDir) + _T("Incoming\\");
	}

	/**
	 * @brief Reports whether a directory path is the implicit incoming directory below the selected base directory.
	 */
	inline bool IsDefaultIncomingDirectoryFromBaseDir(const CString &strBaseDir, const CString &strDirectory)
	{
		if (strBaseDir.IsEmpty())
			return false;

		CString strExpected(GetIncomingDirectoryFromBaseDir(strBaseDir));
		CString strActual(NormalizeBaseDir(strDirectory));
		strExpected.MakeLower();
		strActual.MakeLower();
		return !strActual.IsEmpty() && strActual == strExpected;
	}

	/**
	 * @brief Builds the effective log directory path below the selected base directory.
	 */
	inline CString GetLogDirectoryFromBaseDir(const CString &strBaseDir)
	{
		return NormalizeBaseDir(strBaseDir) + _T("logs\\");
	}

	/**
	 * @brief Builds the effective expansion directory for the selected base directory.
	 */
	inline CString GetExpansionDirectoryFromBaseDir(const CString &strBaseDir)
	{
		return NormalizeBaseDir(strBaseDir);
	}

	/**
	 * @brief Builds the effective `preferences.ini` path below the selected base directory.
	 */
	inline CString GetPreferencesIniPathFromBaseDir(const CString &strBaseDir)
	{
		return GetConfigDirectoryFromBaseDir(strBaseDir) + _T("preferences.ini");
	}
}
