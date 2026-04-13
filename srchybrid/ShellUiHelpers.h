#pragma once

#include <atlstr.h>
#include <tchar.h>
#include <vector>
#include <windows.h>

#include "PathHelpers.h"

namespace ShellUiHelpers
{
struct ShellIconDescriptor
{
	CString strCacheKey;
	CString strQueryPath;
	DWORD dwFileAttributes;
};

struct DialogInitialSelection
{
	CString strInitialFolder;
	CString strFileName;
};

/**
 * @brief Loads an INI/profile string into a dynamically grown buffer instead of a fixed `MAX_PATH` array.
 */
template <typename GetProfileStringFn>
inline CString GetProfileString(const CString &rstrSection, const CString &rstrKey, LPCTSTR pszDefaultValue, const CString &rstrProfilePath, GetProfileStringFn getProfileStringFn)
{
	std::vector<TCHAR> buffer(MAX_PATH, _T('\0'));
	while (buffer.size() < PathHelpers::kMaxDynamicPathChars) {
		const DWORD dwCopied = getProfileStringFn(rstrSection, rstrKey, pszDefaultValue, buffer.data(), static_cast<DWORD>(buffer.size()), rstrProfilePath);
		if (dwCopied == 0)
			return CString();
		if (dwCopied < buffer.size() - 1u) {
			buffer[dwCopied] = _T('\0');
			return CString(buffer.data());
		}

		buffer.resize(buffer.size() * 2u, _T('\0'));
	}

	return CString();
}

/**
 * @brief Loads an INI/profile string through the real `GetPrivateProfileString` API.
 */
inline CString GetProfileString(const CString &rstrSection, const CString &rstrKey, LPCTSTR pszDefaultValue, const CString &rstrProfilePath)
{
	return GetProfileString(
		rstrSection,
		rstrKey,
		pszDefaultValue,
		rstrProfilePath,
		[](const CString &rstrSectionName, const CString &rstrKeyName, LPCTSTR pszDefault, LPTSTR pszBuffer, DWORD dwBufferChars, const CString &rstrProfileFile) -> DWORD {
			return ::GetPrivateProfileString(rstrSectionName, rstrKeyName, pszDefault, pszBuffer, dwBufferChars, rstrProfileFile);
		});
}

/**
 * @brief Limits shell display-name enrichment to shell-friendly non-prefixed paths.
 */
inline bool CanUseShellDisplayName(const CString &rstrPath)
{
	return PathHelpers::IsShellSafePath(rstrPath);
}

/**
 * @brief Ignores Windows shortcut files by extension without consulting shell metadata.
 */
inline bool ShouldIgnoreShortcutFileName(LPCTSTR pszFileName)
{
	if (pszFileName == NULL)
		return false;

	const CString strFileName(pszFileName);
	return strFileName.Right(4).CompareNoCase(_T(".lnk")) == 0;
}

/**
 * @brief Detects whether the incoming path already denotes a directory.
 */
inline bool IsDirectoryPathHint(LPCTSTR pszFilePath, int iLength = -1)
{
	if (pszFilePath == NULL)
		return false;
	if (iLength < 0)
		iLength = static_cast<int>(_tcslen(pszFilePath));
	return iLength > 0 && PathHelpers::IsPathSeparator(pszFilePath[iLength - 1]);
}

/**
 * @brief Describes shell icon lookup using stable cache-key and attribute-based query data.
 */
inline ShellIconDescriptor DescribeShellIcon(LPCTSTR pszFilePath, int iLength = -1)
{
	ShellIconDescriptor descriptor = { CString(), CString(_T("file")), FILE_ATTRIBUTE_NORMAL };
	if (pszFilePath == NULL)
		return descriptor;

	if (iLength < 0)
		iLength = static_cast<int>(_tcslen(pszFilePath));

	if (IsDirectoryPathHint(pszFilePath, iLength)) {
		descriptor.strCacheKey = _T("\\");
		descriptor.strQueryPath = _T("folder\\");
		descriptor.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
		return descriptor;
	}

	int iLastSlash = -1;
	int iLastDot = -1;
	for (int i = 0; i < iLength; ++i) {
		if (PathHelpers::IsPathSeparator(pszFilePath[i]))
			iLastSlash = i;
		else if (pszFilePath[i] == _T('.'))
			iLastDot = i;
	}

	if (iLastDot > iLastSlash && iLastDot + 1 < iLength) {
		descriptor.strCacheKey = CString(pszFilePath + iLastDot + 1);
		descriptor.strQueryPath = CString(_T("file")) + CString(pszFilePath + iLastDot);
	}
	return descriptor;
}

/**
 * @brief Splits an initial picker string into an initial folder and optional file name.
 */
inline DialogInitialSelection SplitDialogInitialSelection(const CString &rstrInitialPath)
{
	DialogInitialSelection selection;
	const CString strNormalized(PathHelpers::NormalizePathSeparators(rstrInitialPath));
	if (strNormalized.IsEmpty())
		return selection;

	if (PathHelpers::IsPathSeparator(strNormalized[strNormalized.GetLength() - 1])) {
		selection.strInitialFolder = strNormalized;
		return selection;
	}

	const int iLastSlash = strNormalized.ReverseFind(_T('\\'));
	if (iLastSlash < 0) {
		if (strNormalized.ReverseFind(_T('.')) >= 0)
			selection.strFileName = strNormalized;
		else
			selection.strInitialFolder = strNormalized;
		return selection;
	}

	const CString strLeaf(strNormalized.Mid(iLastSlash + 1));
	if (strLeaf.ReverseFind(_T('.')) >= 0) {
		selection.strInitialFolder = PathHelpers::GetDirectoryPath(strNormalized);
		selection.strFileName = strLeaf;
		return selection;
	}

	selection.strInitialFolder = strNormalized;
	return selection;
}

/**
 * @brief Normalizes a picked folder path and restores the trailing backslash expected by existing callers.
 */
inline CString FinalizeFolderSelection(const CString &rstrFolderPath)
{
	return PathHelpers::EnsureTrailingSeparator(rstrFolderPath);
}

/**
 * @brief Prepares a folder path for shell parsing without degrading drive or UNC share roots.
 */
inline CString PrepareFolderSelectionPathForShell(const CString &rstrFolderPath)
{
	const CString strShellPath(PathHelpers::StripExtendedLengthPrefix(PathHelpers::TrimTrailingSeparator(rstrFolderPath)));
	return PathHelpers::IsShellSafePath(strShellPath) ? strShellPath : CString();
}

/**
 * @brief Expands environment variables through an injected callback before resolving a skin resource path.
 */
template <typename ExpandEnvironmentFn>
inline CString ResolveSkinResourcePath(const CString &rstrSkinProfile, const CString &rstrSkinResource, ExpandEnvironmentFn expandEnvironmentFn)
{
	if (rstrSkinResource.IsEmpty())
		return CString();

	CString strExpanded(expandEnvironmentFn(rstrSkinResource));
	if (strExpanded.IsEmpty())
		strExpanded = rstrSkinResource;
	strExpanded = PathHelpers::NormalizePathSeparators(strExpanded);

	if (PathHelpers::ParsePathRoot(strExpanded).bAbsolute)
		return strExpanded;

	return PathHelpers::AppendPathComponent(PathHelpers::GetDirectoryPath(rstrSkinProfile), strExpanded);
}

/**
 * @brief Expands environment variables dynamically and resolves relative skin resources against the skin profile directory.
 */
inline CString ResolveSkinResourcePath(const CString &rstrSkinProfile, const CString &rstrSkinResource)
{
	return ResolveSkinResourcePath(
		rstrSkinProfile,
		rstrSkinResource,
		[](const CString &rstrInput) -> CString {
			std::vector<TCHAR> buffer(PathHelpers::kMaxDynamicPathChars, _T('\0'));
			const DWORD dwChars = ::ExpandEnvironmentStrings(rstrInput, buffer.data(), static_cast<DWORD>(buffer.size()));
			if (dwChars == 0 || dwChars >= buffer.size())
				return CString();
			return CString(buffer.data());
		});
}
}

#define EMULE_TEST_HAVE_SHELL_UI_HELPERS 1
