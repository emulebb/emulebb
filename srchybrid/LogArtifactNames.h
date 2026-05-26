#pragma once

#include <atlstr.h>
#include <windows.h>

namespace LogArtifactNames
{
/**
 * @brief Canonical runtime diagnostic artifact names for eMuleBB.
 */
inline LPCTSTR MainLogFileName()
{
	return _T("emulebb.log");
}

inline LPCTSTR VerboseLogFileName()
{
	return _T("emulebb-verbose.log");
}

inline LPCTSTR CrtDebugLogFileName()
{
	return _T("emulebb-crt-debug.log");
}

inline LPCTSTR StartupErrorLogFileName()
{
	return _T("emulebb-startup-errors.log");
}

inline LPCTSTR PerformanceCsvFileName()
{
	return _T("emulebb-performance.csv");
}

inline LPCTSTR PerformanceMrtgFileName()
{
	return _T("emulebb-performance.mrtg");
}

inline LPCTSTR PerformanceMrtgDataSuffix()
{
	return _T("-data.mrtg");
}

inline LPCTSTR PerformanceMrtgOverheadSuffix()
{
	return _T("-overhead.mrtg");
}

/**
 * @brief Formats local wall-clock time for sortable artifact filenames.
 */
inline CString FormatTimestamp(const SYSTEMTIME &rTime)
{
	CString strTimestamp;
	strTimestamp.Format(_T("%04u%02u%02u-%02u%02u%02u"),
		rTime.wYear,
		rTime.wMonth,
		rTime.wDay,
		rTime.wHour,
		rTime.wMinute,
		rTime.wSecond);
	return strTimestamp;
}

/**
 * @brief Finds the final path separator in a Windows or slash-separated path.
 */
inline int FindLastPathSeparator(const CString &rstrPath)
{
	int iSeparator = rstrPath.ReverseFind(_T('\\'));
	const int iAltSeparator = rstrPath.ReverseFind(_T('/'));
	if (iAltSeparator > iSeparator)
		iSeparator = iAltSeparator;
	return iSeparator;
}

/**
 * @brief Finds the end of the filename stem, before the final extension.
 */
inline int FindStemEnd(const CString &rstrPath, const int iSeparator)
{
	const int iDot = rstrPath.ReverseFind(_T('.'));
	return (iDot > iSeparator) ? iDot : rstrPath.GetLength();
}

/**
 * @brief Builds a sibling path by replacing the current extension with a suffix.
 */
inline CString BuildPathWithStemSuffix(const CString &rstrConfiguredPath, LPCTSTR pszSuffixWithExtension)
{
	const int iSeparator = FindLastPathSeparator(rstrConfiguredPath);
	const int iNameEnd = FindStemEnd(rstrConfiguredPath, iSeparator);

	CString strPath(rstrConfiguredPath.Left(iNameEnd));
	strPath += pszSuffixWithExtension;
	return strPath;
}

/**
 * @brief Builds a rotated log path by appending a timestamp before the extension.
 */
inline CString BuildPathWithTimestampSuffix(const CString &rstrFilePath, const CString &rstrTimestamp)
{
	const int iSeparator = FindLastPathSeparator(rstrFilePath);
	const int iNameStart = iSeparator + 1;
	const int iNameEnd = FindStemEnd(rstrFilePath, iSeparator);
	const int iDot = rstrFilePath.ReverseFind(_T('.'));

	CString strPath(rstrFilePath.Left(iNameStart));
	strPath += rstrFilePath.Mid(iNameStart, iNameEnd - iNameStart);
	strPath += _T("-");
	strPath += rstrTimestamp;
	if (iDot > iSeparator)
		strPath += rstrFilePath.Mid(iDot);
	return strPath;
}

/**
 * @brief Builds an operator-requested diagnostic dump filename.
 */
inline CString BuildManualDumpFileName(const SYSTEMTIME &rTime, DWORD dwProcessId, bool bFullMemoryDump, int iAttempt)
{
	CString strFileName;
	strFileName.Format(_T("emulebb-dump-%s-pid%lu-%s"),
		(LPCTSTR)FormatTimestamp(rTime),
		dwProcessId,
		bFullMemoryDump ? _T("full") : _T("mini"));
	if (iAttempt > 0) {
		CString strSuffix;
		strSuffix.Format(_T("-%02d"), iAttempt);
		strFileName += strSuffix;
	}
	strFileName += _T(".dmp");
	return strFileName;
}

/**
 * @brief Builds an unhandled-crash dump filename.
 */
inline CString BuildCrashDumpFileName(const SYSTEMTIME &rTime, DWORD dwProcessId)
{
	CString strFileName;
	strFileName.Format(_T("emulebb-crash-%s-pid%lu.dmp"), (LPCTSTR)FormatTimestamp(rTime), dwProcessId);
	return strFileName;
}
}
