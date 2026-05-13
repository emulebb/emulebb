#pragma once

#include <afxcoll.h>
#include <atlstr.h>
#include <climits>
#include <shlwapi.h>

namespace MediaInfoDllSeams
{
/**
 * @brief Classifies the optional MediaInfo DLL loader state.
 */
enum EMediaInfoDllStatus
{
	MediaInfoDll_NotInitialized,
	MediaInfoDll_Loaded,
	MediaInfoDll_Disabled,
	MediaInfoDll_Missing,
	MediaInfoDll_Incompatible,
	MediaInfoDll_BadExports,
	MediaInfoDll_LoadFailed
};

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
 * @brief Returns whether File Info should display an install/configuration hint.
 */
inline bool ShouldShowInstallHint(const EMediaInfoDllStatus eStatus)
{
	return eStatus == MediaInfoDll_Missing
		|| eStatus == MediaInfoDll_Incompatible
		|| eStatus == MediaInfoDll_BadExports
		|| eStatus == MediaInfoDll_LoadFailed;
}

/**
 * @brief Clamps suspicious MediaInfo stream counts before loop bounds use them.
 */
inline int NormalizeReportedCount(const int iValue, const int iMaxValue)
{
	if (iValue <= 0 || iMaxValue <= 0)
		return 0;
	return iValue > iMaxValue ? iMaxValue : iValue;
}

/**
 * @brief Clamps a MediaInfo chapter end offset to a bounded span after begin.
 */
inline int NormalizeChapterEnd(const int iBegin, const int iEnd, const int iMaxChapterCount)
{
	if (iEnd <= iBegin || iMaxChapterCount <= 0)
		return iBegin;
	const int iMaxEnd = iBegin > INT_MAX - iMaxChapterCount ? INT_MAX : iBegin + iMaxChapterCount;
	return iEnd > iMaxEnd ? iMaxEnd : iEnd;
}

/**
 * @brief Returns whether a MediaInfo codec string has enough bytes for FOURCC extraction.
 */
inline bool CanReadFourCc(const CStringA &rstrCodec)
{
	return rstrCodec.GetLength() >= 4;
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
