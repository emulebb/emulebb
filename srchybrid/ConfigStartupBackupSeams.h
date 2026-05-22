#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include <atlstr.h>

namespace ConfigStartupBackupSeams
{
constexpr size_t kDefaultBackupRetentionCount = 7u;
constexpr LPCTSTR kBackupDirectoryPrefix = _T("config_bak_");
constexpr LPCTSTR kBackupWorkingDirectorySuffix = _T(".tmp");

inline CString BuildBackupDirectoryName(const int iYear, const int iMonth, const int iDay)
{
	CString strName;
	strName.Format(_T("%s%04d%02d%02d"), kBackupDirectoryPrefix, iYear, iMonth, iDay);
	return strName;
}

inline CString BuildBackupWorkingDirectoryName(const int iYear, const int iMonth, const int iDay)
{
	return BuildBackupDirectoryName(iYear, iMonth, iDay) + kBackupWorkingDirectorySuffix;
}

inline bool IsEightDigitDateText(const CString &rstrText)
{
	if (rstrText.GetLength() != 8)
		return false;
	for (int i = 0; i < rstrText.GetLength(); ++i) {
		if (rstrText[i] < _T('0') || rstrText[i] > _T('9'))
			return false;
	}
	return true;
}

inline bool IsConfigBackupDirectoryName(const CString &rstrName)
{
	const CString strPrefix(kBackupDirectoryPrefix);
	if (rstrName.GetLength() != strPrefix.GetLength() + 8)
		return false;
	if (rstrName.Left(strPrefix.GetLength()).CompareNoCase(strPrefix) != 0)
		return false;
	return IsEightDigitDateText(rstrName.Mid(strPrefix.GetLength()));
}

inline bool IsConfigBackupWorkingDirectoryName(const CString &rstrName)
{
	const CString strSuffix(kBackupWorkingDirectorySuffix);
	if (rstrName.GetLength() <= strSuffix.GetLength())
		return false;
	if (rstrName.Right(strSuffix.GetLength()).CompareNoCase(strSuffix) != 0)
		return false;
	return IsConfigBackupDirectoryName(rstrName.Left(rstrName.GetLength() - strSuffix.GetLength()));
}

/**
 * @brief Reports whether a config file is a generated cache that can be
 *        rebuilt and should not enlarge daily startup snapshots.
 */
inline bool IsGeneratedConfigBackupCacheFileName(const CString &rstrName)
{
	return rstrName.CompareNoCase(_T("sharedcache.dat")) == 0
		|| rstrName.CompareNoCase(_T("shareddups.dat")) == 0
		|| rstrName.CompareNoCase(_T("dbip-city-lite.mmdb")) == 0
		|| rstrName.CompareNoCase(_T("GeoIPCountryWhois.csv")) == 0;
}

inline bool ShouldSkipConfigBackupEntry(const CString &rstrName, const bool bIsDirectory)
{
	if (bIsDirectory)
		return IsConfigBackupDirectoryName(rstrName) || IsConfigBackupWorkingDirectoryName(rstrName);
	return IsGeneratedConfigBackupCacheFileName(rstrName);
}

inline std::vector<CString> SelectConfigBackupDirectoriesToPrune(const std::vector<CString> &rNames, const size_t uRetentionCount)
{
	std::vector<CString> validNames;
	for (size_t i = 0; i < rNames.size(); ++i) {
		if (IsConfigBackupDirectoryName(rNames[i]))
			validNames.push_back(rNames[i]);
	}

	std::sort(validNames.begin(), validNames.end(), [](const CString &rLeft, const CString &rRight) {
		return rLeft.CompareNoCase(rRight) < 0;
	});

	if (validNames.size() <= uRetentionCount)
		return std::vector<CString>();

	validNames.resize(validNames.size() - uRetentionCount);
	return validNames;
}
}
