#pragma once

#include <afxcoll.h>
#include <atlstr.h>
#include <shlwapi.h>

namespace MediaInfoDllSeams
{
/**
 * @brief Returns the minimum MediaInfo DLL version accepted by the runtime loader.
 */
inline ULONGLONG GetMinimumCompatibleVersion()
{
	return MAKEDLLVERULL(26, 1, 0, 0);
}

/**
 * @brief Returns whether the persisted MediaInfo path disables runtime loading.
 */
inline bool IsLoadingDisabled(const CString &rstrConfiguredPath)
{
	return rstrConfiguredPath.CompareNoCase(_T("<noload>")) == 0;
}

/**
 * @brief Returns whether a MediaInfo DLL version is accepted by the loader.
 */
inline bool IsCompatibleVersion(const ULONGLONG ullVersion)
{
	return ullVersion >= GetMinimumCompatibleVersion();
}

/**
 * @brief Returns whether MediaInfo_Open reported a usable file handle.
 */
inline bool IsOpenSucceeded(const int iOpenResult)
{
	return iOpenResult != 0;
}

/**
 * @brief Appends a path component without fixed-size buffers.
 */
inline CString AppendCandidatePathComponent(const CString &rstrBasePath, LPCTSTR pszChildPath)
{
	CString strJoined(rstrBasePath);
	if (!strJoined.IsEmpty() && strJoined.Right(1) != _T("\\") && strJoined.Right(1) != _T("/"))
		strJoined += _T("\\");
	strJoined += pszChildPath != NULL ? pszChildPath : _T("");
	return strJoined;
}

/**
 * @brief Adds one absolute MediaInfo DLL candidate path, suppressing case-insensitive duplicates.
 */
inline void AddAbsoluteCandidatePath(CStringArray &raCandidatePaths, const CString &rstrCandidatePath)
{
	if (rstrCandidatePath.IsEmpty() || ::PathIsRelative(rstrCandidatePath))
		return;

	for (INT_PTR i = 0; i < raCandidatePaths.GetCount(); ++i) {
		if (raCandidatePaths[i].CompareNoCase(rstrCandidatePath) == 0)
			return;
	}
	raCandidatePaths.Add(rstrCandidatePath);
}

/**
 * @brief Adds an app-folder candidate when the configured MediaInfo DLL path is relative.
 */
inline void AddRelativeConfiguredCandidatePath(CStringArray &raCandidatePaths, const CString &rstrAppFolder, const CString &rstrConfiguredPath)
{
	if (rstrAppFolder.IsEmpty() || rstrConfiguredPath.IsEmpty() || !::PathIsRelative(rstrConfiguredPath))
		return;
	AddAbsoluteCandidatePath(raCandidatePaths, AppendCandidatePathComponent(rstrAppFolder, rstrConfiguredPath));
}
}
