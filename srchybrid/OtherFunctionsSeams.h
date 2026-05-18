#pragma once

#include <atlstr.h>
#include <tchar.h>
#include <windows.h>

namespace OtherFunctionsSeams
{
/**
 * @brief Returns the numeric ShellExecute result code without pointer comparison.
 */
inline INT_PTR GetShellExecuteResultCode(HINSTANCE hResult)
{
	return reinterpret_cast<INT_PTR>(hResult);
}

/**
 * @brief Reports whether ShellExecute accepted the launch request.
 */
inline bool DidShellExecuteLaunch(HINSTANCE hResult)
{
	return GetShellExecuteResultCode(hResult) > 32;
}

/**
 * @brief Converts a failed ShellExecute result to the legacy ShellExecute error code.
 */
inline DWORD GetShellExecuteErrorCode(HINSTANCE hResult)
{
	const INT_PTR nResult = GetShellExecuteResultCode(hResult);
	return nResult > 0 && nResult <= 32 ? static_cast<DWORD>(nResult) : ERROR_SUCCESS;
}

/**
 * @brief Builds the command stored in the current user's Windows Run key for unattended startup.
 */
inline CString BuildAutoStartRunCommand(const CString &strExeFilePath)
{
	CString strCommand;
	if (!strExeFilePath.IsEmpty())
		strCommand.Format(_T("\"%s\" -AutoStart"), (LPCTSTR)strExeFilePath);
	return strCommand;
}

/**
 * @brief Reports whether the current build should perform real Windows autorun registry writes.
 */
inline bool ShouldWriteAutoStartRegistry()
{
#ifdef _DEBUG
	return false;
#else
	return true;
#endif
}

/**
 * @brief Describes which shell-delete path should be taken for the current request.
 */
enum ShellDeleteRoute
{
	shellDeleteNoOp = 0,
	shellDeleteDirect,
	shellDeleteRecycleBin
};

/**
 * @brief Chooses the runtime delete route from the current existence and recycle-bin settings.
 */
inline ShellDeleteRoute ResolveShellDeleteRoute(const bool bPathExists, const bool bRemoveToBin)
{
	if (!bPathExists)
		return shellDeleteNoOp;

	return bRemoveToBin ? shellDeleteRecycleBin : shellDeleteDirect;
}

/**
 * @brief Runs the shell-delete flow through injected filesystem and recycle-bin callbacks.
 */
template <typename PathExistsFn, typename RecycleDeleteFn, typename DirectDeleteFn>
inline bool ExecuteShellDelete(
	LPCTSTR pszFilePath,
	const bool bRemoveToBin,
	HWND hOwnerWindow,
	PathExistsFn pathExistsFn,
	RecycleDeleteFn recycleDeleteFn,
	DirectDeleteFn directDeleteFn)
{
	const ShellDeleteRoute eRoute = ResolveShellDeleteRoute(pathExistsFn(pszFilePath), bRemoveToBin);
	switch (eRoute) {
		case shellDeleteNoOp:
			return true;
		case shellDeleteRecycleBin:
			return recycleDeleteFn(pszFilePath, hOwnerWindow);
		case shellDeleteDirect:
			return directDeleteFn(pszFilePath);
	}

	return false;
}
}

#define EMULE_TEST_HAVE_OTHER_FUNCTIONS_SEAMS 1
