#pragma once

#include <atlstr.h>
#include <tchar.h>
#include <windows.h>

namespace StartupConfigOverride
{
	/**
	 * @brief Reports whether the raw command-line token is the supported config-base override option.
	 */
	inline bool IsConfigBaseDirOptionToken(LPCTSTR pszArgument)
	{
		return pszArgument != NULL && (_tcsicmp(pszArgument, _T("-c")) == 0 || _tcsicmp(pszArgument, _T("/c")) == 0);
	}

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
	 * @brief Builds the effective log directory path below the selected base directory.
	 */
	inline CString GetLogDirectoryFromBaseDir(const CString &strBaseDir)
	{
		return NormalizeBaseDir(strBaseDir) + _T("logs\\");
	}

	/**
	 * @brief Builds the effective `preferences.ini` path below the selected base directory.
	 */
	inline CString GetPreferencesIniPathFromBaseDir(const CString &strBaseDir)
	{
		return GetConfigDirectoryFromBaseDir(strBaseDir) + _T("preferences.ini");
	}

	/**
	 * @brief Parses the supported `-c <base-dir>` override from the raw command line.
	 *
	 * The option may be specified at most once and requires a canonical absolute drive path.
	 */
	inline bool TryParseConfigBaseDirOverride(int argc, TCHAR *argv[], CString &rstrBaseDir, CString &rstrError)
	{
		rstrBaseDir.Empty();
		rstrError.Empty();
		bool bSeenOverride = false;

		for (int i = 1; i < argc; ++i) {
			if (!IsConfigBaseDirOptionToken(argv[i]))
				continue;

			if (bSeenOverride) {
				rstrError = _T("The -c option may be specified only once.");
				return false;
			}
			if (++i >= argc) {
				rstrError = _T("The -c option requires a canonical absolute eMule base directory like C:\\path.");
				return false;
			}

			CString strCandidate(argv[i]);
			if (strCandidate.IsEmpty() || !IsAbsoluteBaseDirPath(strCandidate)) {
				rstrError = _T("The -c option requires a canonical absolute eMule base directory like C:\\path.");
				return false;
			}

			rstrBaseDir = NormalizeBaseDir(strCandidate);
			bSeenOverride = true;
		}
		return true;
	}
}
