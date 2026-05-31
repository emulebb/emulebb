#pragma once

#include <atlstr.h>
#include <tchar.h>
#include <vector>
#include <windows.h>

#include "StartupConfigOverride.h"

namespace RestartAppSeams
{
/**
 * @brief Maximum time the hidden restart sidecar waits for the parent app to exit.
 */
constexpr DWORD kRestartSidecarShutdownWaitMs = 10u * 60u * 1000u;

/**
 * @brief Sidecar decision after observing the parent shutdown state.
 */
enum class ERestartSidecarAction
{
	LaunchRestart,
	ExitWithoutLaunch
};

/**
 * @brief Returns true for canonical drive-rooted Win32 paths without slash aliases.
 */
inline bool IsCanonicalAbsoluteWin32Path(const CString &strPath)
{
	if (strPath.IsEmpty() || strPath.Find(_T('/')) >= 0)
		return false;

	CString strFullPath;
	return StartupConfigOverride::IsDriveRootedPath(strPath)
		&& StartupConfigOverride::TryGetFullPathName(strPath, strFullPath)
		&& StartupConfigOverride::IsDriveRootedPath(strFullPath)
		&& strFullPath == strPath;
}

inline bool IsAbsoluteRequestFilePath(const CString &strPath)
{
	return IsCanonicalAbsoluteWin32Path(strPath);
}

/**
 * @brief Appends one character repeatedly to a CString without exposing loops at callers.
 */
inline void AppendRepeated(CString &rstrTarget, TCHAR chValue, int iCount)
{
	for (int i = 0; i < iCount; ++i)
		rstrTarget.AppendChar(chValue);
}

/**
 * @brief Quotes one argv token using the Windows CreateProcess command-line rules.
 */
inline CString QuoteCommandLineArgument(const CString &strArgument)
{
	const bool bNeedsQuotes = strArgument.IsEmpty()
		|| strArgument.FindOneOf(_T(" \t\r\n\v\"")) >= 0;
	if (!bNeedsQuotes)
		return strArgument;

	CString strQuoted(_T("\""));
	int iBackslashes = 0;
	for (int i = 0; i < strArgument.GetLength(); ++i) {
		const TCHAR ch = strArgument[i];
		if (ch == _T('\\')) {
			++iBackslashes;
			continue;
		}
		if (ch == _T('"')) {
			AppendRepeated(strQuoted, _T('\\'), iBackslashes * 2 + 1);
			strQuoted.AppendChar(ch);
			iBackslashes = 0;
			continue;
		}
		AppendRepeated(strQuoted, _T('\\'), iBackslashes);
		iBackslashes = 0;
		strQuoted.AppendChar(ch);
	}
	AppendRepeated(strQuoted, _T('\\'), iBackslashes * 2);
	strQuoted.AppendChar(_T('"'));
	return strQuoted;
}

/**
 * @brief Builds a CreateProcess-compatible command line from an executable and argv tokens.
 */
inline CString BuildCommandLine(const CString &strExecutablePath, const std::vector<CString> &raArguments)
{
	CString strCommandLine(QuoteCommandLineArgument(strExecutablePath));
	for (const CString &rArgument : raArguments) {
		strCommandLine.AppendChar(_T(' '));
		strCommandLine += QuoteCommandLineArgument(rArgument);
	}
	return strCommandLine;
}

/**
 * @brief Builds the user-visible restart argv, preserving only the active profile override.
 */
inline std::vector<CString> BuildProfileRestartArguments(const bool bHasConfigBaseDir, const CString &strConfigBaseDir)
{
	std::vector<CString> arguments;
	if (bHasConfigBaseDir && !strConfigBaseDir.IsEmpty()) {
		arguments.emplace_back(_T("-c"));
		arguments.emplace_back(strConfigBaseDir);
	}
	return arguments;
}

/**
 * @brief Maps parent wait results to a sidecar restart decision.
 */
inline ERestartSidecarAction GetRestartActionAfterParentWait(const DWORD dwWaitResult)
{
	return dwWaitResult == WAIT_OBJECT_0
		? ERestartSidecarAction::LaunchRestart
		: ERestartSidecarAction::ExitWithoutLaunch;
}

/**
 * @brief Maps OpenProcess failures to a conservative restart decision.
 */
inline ERestartSidecarAction GetRestartActionAfterOpenParentFailure(const DWORD dwError)
{
	return dwError == ERROR_INVALID_PARAMETER
		? ERestartSidecarAction::LaunchRestart
		: ERestartSidecarAction::ExitWithoutLaunch;
}
}
