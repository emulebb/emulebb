#pragma once

#include <atlstr.h>
#include <Windows.h>

namespace ProcessLaunchSeams
{
constexpr DWORD kElevatedPowerShellActionTimeoutMs = 30u * 60u * 1000u;
constexpr DWORD kArchiveRecoveryPreviewTimeoutMs = 30u * 60u * 1000u;
constexpr DWORD kTimedOutProcessTerminateWaitMs = 2u * 1000u;

enum class EProcessWaitResult
{
	Completed,
	TimedOut,
	Failed,
	Other
};

struct DetachedLaunchResult
{
	bool Started = false;
	DWORD LastError = ERROR_SUCCESS;
};

struct BoundedProcessResult
{
	bool Started = false;
	EProcessWaitResult WaitResult = EProcessWaitResult::Other;
	DWORD LastError = ERROR_SUCCESS;
	DWORD ExitCode = ERROR_SUCCESS;
};

/**
 * @brief Classifies a Win32 process wait result for bounded external process launches.
 */
inline EProcessWaitResult ClassifyProcessWaitResult(DWORD dwWaitResult)
{
	switch (dwWaitResult) {
	case WAIT_OBJECT_0:
		return EProcessWaitResult::Completed;
	case WAIT_TIMEOUT:
		return EProcessWaitResult::TimedOut;
	case WAIT_FAILED:
		return EProcessWaitResult::Failed;
	default:
		return EProcessWaitResult::Other;
	}
}

/**
 * @brief Starts a detached child process and always closes the returned process handles.
 */
inline DetachedLaunchResult LaunchDetachedProcess(LPCTSTR pszApplicationName, CString strCommandLine, LPCTSTR pszWorkingDirectory, WORD wShowWindow, DWORD dwCreationFlags)
{
	STARTUPINFO startupInfo = {};
	startupInfo.cb = sizeof(startupInfo);
	startupInfo.dwFlags = STARTF_USESHOWWINDOW;
	startupInfo.wShowWindow = wShowWindow;

	PROCESS_INFORMATION processInfo = {};
	LPTSTR pszCommandLine = strCommandLine.GetBuffer();
	const BOOL bStarted = ::CreateProcess(
		pszApplicationName,
		pszCommandLine,
		NULL,
		NULL,
		FALSE,
		dwCreationFlags,
		NULL,
		pszWorkingDirectory,
		&startupInfo,
		&processInfo);
	const DWORD dwError = bStarted ? ERROR_SUCCESS : ::GetLastError();
	strCommandLine.ReleaseBuffer();

	if (bStarted) {
		(void)::CloseHandle(processInfo.hThread);
		(void)::CloseHandle(processInfo.hProcess);
	}

	DetachedLaunchResult result;
	result.Started = bStarted != FALSE;
	result.LastError = dwError;
	return result;
}

/**
 * @brief Starts a child process, waits for a bounded interval, and optionally terminates on timeout.
 */
inline BoundedProcessResult RunProcessWithTimeout(CString strCommandLine, LPCTSTR pszWorkingDirectory, WORD wShowWindow, DWORD dwCreationFlags, DWORD dwTimeoutMs, bool bTerminateOnTimeout, DWORD dwTerminateWaitMs)
{
	STARTUPINFO startupInfo = {};
	startupInfo.cb = sizeof(startupInfo);
	startupInfo.dwFlags = STARTF_USESHOWWINDOW;
	startupInfo.wShowWindow = wShowWindow;

	PROCESS_INFORMATION processInfo = {};
	LPTSTR pszCommandLine = strCommandLine.GetBuffer();
	const BOOL bStarted = ::CreateProcess(
		NULL,
		pszCommandLine,
		NULL,
		NULL,
		FALSE,
		dwCreationFlags,
		NULL,
		pszWorkingDirectory,
		&startupInfo,
		&processInfo);
	const DWORD dwError = bStarted ? ERROR_SUCCESS : ::GetLastError();
	strCommandLine.ReleaseBuffer();

	BoundedProcessResult result;
	result.Started = bStarted != FALSE;
	result.LastError = dwError;
	if (!result.Started)
		return result;

	const DWORD dwWaitResult = ::WaitForSingleObject(processInfo.hProcess, dwTimeoutMs);
	result.WaitResult = ClassifyProcessWaitResult(dwWaitResult);
	if (result.WaitResult == EProcessWaitResult::TimedOut) {
		result.ExitCode = WAIT_TIMEOUT;
		result.LastError = WAIT_TIMEOUT;
		if (bTerminateOnTimeout) {
			(void)::TerminateProcess(processInfo.hProcess, WAIT_TIMEOUT);
			(void)::WaitForSingleObject(processInfo.hProcess, dwTerminateWaitMs);
		}
	} else if (result.WaitResult == EProcessWaitResult::Completed) {
		if (!::GetExitCodeProcess(processInfo.hProcess, &result.ExitCode))
			result.LastError = ::GetLastError();
	} else
		result.LastError = result.WaitResult == EProcessWaitResult::Failed ? ::GetLastError() : dwWaitResult;

	(void)::CloseHandle(processInfo.hThread);
	(void)::CloseHandle(processInfo.hProcess);
	return result;
}
}
