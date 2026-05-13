#pragma once

#include <atlstr.h>
#include <cstddef>
#include <shlwapi.h>

#include "PathHelpers.h"

namespace RARFileSeams
{
/**
 * @brief Returns the UnRAR DLL API version represented by the current RARLAB header.
 */
inline int GetMinimumDllApiVersion()
{
	return 9;
}

/**
 * @brief Returns true when the loaded UnRAR DLL reports a compatible API version.
 */
inline bool IsCompatibleDllApiVersion(const int iVersion)
{
	return iVersion >= GetMinimumDllApiVersion();
}

/**
 * @brief Returns the external UnRAR DLL filename expected for the current process architecture.
 */
inline LPCTSTR GetDllFileName()
{
#ifdef _WIN64
	return _T("UnRAR64.dll");
#else
	return _T("UnRAR.dll");
#endif
}

/**
 * @brief Returns the UnRAR installer-relative directory that contains the expected DLL.
 */
inline LPCTSTR GetInstalledDllRelativeDirectory()
{
#ifdef _WIN64
	return _T("UnrarDLL\\x64");
#else
	return _T("UnrarDLL");
#endif
}

/**
 * @brief Builds the absolute installed UnRAR DLL candidate path from the Program Files (x86) root.
 */
inline CString BuildInstalledDllPath(const CString &rstrProgramFilesX86)
{
	if (rstrProgramFilesX86.IsEmpty())
		return CString();
	return PathHelpers::AppendPathComponent(
		PathHelpers::AppendPathComponent(rstrProgramFilesX86, GetInstalledDllRelativeDirectory()),
		GetDllFileName());
}

/**
 * @brief Returns true when the path is a usable absolute DLL load candidate.
 */
inline bool IsAbsoluteLoadCandidate(const CString &rstrPath)
{
	return !rstrPath.IsEmpty() && !::PathIsRelative(rstrPath);
}

/**
 * @brief Returns the required UnRAR exports used by CRARFile.
 */
inline const char *const *GetRequiredExportNames(size_t &ruCount)
{
	static const char *const s_apRequiredExports[] = {
		"RAROpenArchiveEx",
		"RARCloseArchive",
		"RARReadHeaderEx",
		"RARProcessFileW"
	};
	ruCount = _countof(s_apRequiredExports);
	return s_apRequiredExports;
}

}
