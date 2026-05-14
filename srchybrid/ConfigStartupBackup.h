#pragma once

#include <cstddef>
#include <vector>

#include <atlstr.h>
#include <windows.h>

#include "ConfigStartupBackupSeams.h"
#include "LongPathSeams.h"
#include "PathHelpers.h"

namespace ConfigStartupBackup
{
struct StartupConfigBackupResult
{
	bool bAttempted = false;
	bool bCreated = false;
	bool bSkippedExisting = false;
	bool bCopyFailed = false;
	bool bPruneFailed = false;
	DWORD dwLastError = ERROR_SUCCESS;
	UINT uCopiedFiles = 0;
	UINT uCopiedDirectories = 0;
	UINT uPrunedDirectories = 0;
	CString strBackupDirectory;
	CString strWorkingDirectory;
	CString strFailedPath;
};

namespace Detail
{
inline StartupConfigBackupResult g_lastStartupConfigBackupResult;

inline void SetFailure(StartupConfigBackupResult &rResult, const CString &rstrPath, const DWORD dwError)
{
	rResult.dwLastError = dwError;
	rResult.strFailedPath = rstrPath;
}

inline bool DeleteDirectoryTree(const CString &rstrDirectory, StartupConfigBackupResult &rResult)
{
	DWORD dwEnumerateError = ERROR_SUCCESS;
	if (!PathHelpers::ForEachDirectoryEntry(rstrDirectory, [&](const WIN32_FIND_DATA &findData) -> bool {
		const CString strChildPath(PathHelpers::AppendPathComponent(rstrDirectory, findData.cFileName));
		if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
			if (!DeleteDirectoryTree(strChildPath, rResult))
				return false;
		} else if (!LongPathSeams::DeleteFileIfExists(strChildPath)) {
			SetFailure(rResult, strChildPath, ::GetLastError());
			return false;
		}
		return true;
	}, &dwEnumerateError)) {
		if (dwEnumerateError != ERROR_FILE_NOT_FOUND && dwEnumerateError != ERROR_PATH_NOT_FOUND) {
			SetFailure(rResult, rstrDirectory, dwEnumerateError);
			return false;
		}
	}

	if (!LongPathSeams::RemoveDirectory(rstrDirectory)) {
		SetFailure(rResult, rstrDirectory, ::GetLastError());
		return false;
	}
	return true;
}

inline bool DeletePathIfExists(const CString &rstrPath, StartupConfigBackupResult &rResult)
{
	WIN32_FIND_DATA findData = {};
	DWORD dwFindError = ERROR_SUCCESS;
	if (!PathHelpers::TryGetPathEntryData(rstrPath, findData, &dwFindError))
		return dwFindError == ERROR_FILE_NOT_FOUND || dwFindError == ERROR_PATH_NOT_FOUND;

	if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		return DeleteDirectoryTree(rstrPath, rResult);

	if (!LongPathSeams::DeleteFileIfExists(rstrPath)) {
		SetFailure(rResult, rstrPath, ::GetLastError());
		return false;
	}
	return true;
}

inline bool CopyDirectoryTree(const CString &rstrSourceDirectory, const CString &rstrDestinationDirectory, StartupConfigBackupResult &rResult)
{
	DWORD dwEnumerateError = ERROR_SUCCESS;
	if (!PathHelpers::ForEachDirectoryEntry(rstrSourceDirectory, [&](const WIN32_FIND_DATA &findData) -> bool {
		const bool bIsDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		if (ConfigStartupBackupSeams::ShouldSkipConfigBackupEntry(findData.cFileName, bIsDirectory))
			return true;

		const CString strSourcePath(PathHelpers::AppendPathComponent(rstrSourceDirectory, findData.cFileName));
		const CString strDestinationPath(PathHelpers::AppendPathComponent(rstrDestinationDirectory, findData.cFileName));
		if (bIsDirectory) {
			if (!LongPathSeams::CreateDirectory(strDestinationPath)) {
				SetFailure(rResult, strDestinationPath, ::GetLastError());
				return false;
			}
			++rResult.uCopiedDirectories;
			return CopyDirectoryTree(strSourcePath, strDestinationPath, rResult);
		}

		if (!LongPathSeams::CopyFile(strSourcePath, strDestinationPath, TRUE)) {
			SetFailure(rResult, strSourcePath, ::GetLastError());
			return false;
		}
		++rResult.uCopiedFiles;
		return true;
	}, &dwEnumerateError)) {
		if (dwEnumerateError != ERROR_FILE_NOT_FOUND && dwEnumerateError != ERROR_PATH_NOT_FOUND)
			SetFailure(rResult, rstrSourceDirectory, dwEnumerateError);
		return dwEnumerateError == ERROR_FILE_NOT_FOUND || dwEnumerateError == ERROR_PATH_NOT_FOUND;
	}
	return true;
}

inline void PruneOldBackups(const CString &rstrConfigDirectory, const size_t uRetentionCount, StartupConfigBackupResult &rResult)
{
	std::vector<CString> backupNames;
	DWORD dwEnumerateError = ERROR_SUCCESS;
	if (!PathHelpers::ForEachDirectoryEntry(rstrConfigDirectory, [&](const WIN32_FIND_DATA &findData) -> bool {
		if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0
			&& ConfigStartupBackupSeams::IsConfigBackupDirectoryName(findData.cFileName))
		{
			backupNames.push_back(findData.cFileName);
		}
		return true;
	}, &dwEnumerateError)) {
		if (dwEnumerateError != ERROR_FILE_NOT_FOUND && dwEnumerateError != ERROR_PATH_NOT_FOUND) {
			rResult.bPruneFailed = true;
			SetFailure(rResult, rstrConfigDirectory, dwEnumerateError);
		}
		return;
	}

	const std::vector<CString> pruneNames(ConfigStartupBackupSeams::SelectConfigBackupDirectoriesToPrune(backupNames, uRetentionCount));
	for (size_t i = 0; i < pruneNames.size(); ++i) {
		const CString strPrunePath(PathHelpers::AppendPathComponent(rstrConfigDirectory, pruneNames[i]));
		if (!DeletePathIfExists(strPrunePath, rResult)) {
			rResult.bPruneFailed = true;
			return;
		}
		++rResult.uPrunedDirectories;
	}
}
}

inline const StartupConfigBackupResult& GetLastStartupConfigBackupResult()
{
	return Detail::g_lastStartupConfigBackupResult;
}

inline StartupConfigBackupResult RunDailyStartupConfigBackup(const CString &rstrConfigDirectory, const size_t uRetentionCount)
{
	StartupConfigBackupResult result;
	result.bAttempted = true;
	if (rstrConfigDirectory.IsEmpty()) {
		result.bCopyFailed = true;
		Detail::SetFailure(result, rstrConfigDirectory, ERROR_INVALID_NAME);
		Detail::g_lastStartupConfigBackupResult = result;
		return result;
	}

	SYSTEMTIME localTime = {};
	::GetLocalTime(&localTime);
	const CString strBackupName(ConfigStartupBackupSeams::BuildBackupDirectoryName(localTime.wYear, localTime.wMonth, localTime.wDay));
	const CString strWorkingName(ConfigStartupBackupSeams::BuildBackupWorkingDirectoryName(localTime.wYear, localTime.wMonth, localTime.wDay));
	const CString strConfigDirectory(PathHelpers::CanonicalizeDirectoryPath(rstrConfigDirectory));
	result.strBackupDirectory = PathHelpers::AppendPathComponent(strConfigDirectory, strBackupName);
	result.strWorkingDirectory = PathHelpers::AppendPathComponent(strConfigDirectory, strWorkingName);

	if (LongPathSeams::PathExists(result.strBackupDirectory)) {
		result.bSkippedExisting = true;
		Detail::PruneOldBackups(strConfigDirectory, uRetentionCount, result);
		Detail::g_lastStartupConfigBackupResult = result;
		return result;
	}

	if (!Detail::DeletePathIfExists(result.strWorkingDirectory, result)) {
		result.bCopyFailed = true;
		Detail::g_lastStartupConfigBackupResult = result;
		return result;
	}

	if (!LongPathSeams::CreateDirectory(result.strWorkingDirectory)) {
		result.bCopyFailed = true;
		Detail::SetFailure(result, result.strWorkingDirectory, ::GetLastError());
		Detail::g_lastStartupConfigBackupResult = result;
		return result;
	}

	if (!Detail::CopyDirectoryTree(strConfigDirectory, result.strWorkingDirectory, result)) {
		result.bCopyFailed = true;
		(void)Detail::DeletePathIfExists(result.strWorkingDirectory, result);
		Detail::g_lastStartupConfigBackupResult = result;
		return result;
	}

	if (!LongPathSeams::MoveFile(result.strWorkingDirectory, result.strBackupDirectory)) {
		const DWORD dwMoveError = ::GetLastError();
		if (dwMoveError == ERROR_ALREADY_EXISTS || dwMoveError == ERROR_FILE_EXISTS) {
			result.bSkippedExisting = true;
			(void)Detail::DeletePathIfExists(result.strWorkingDirectory, result);
		} else {
			result.bCopyFailed = true;
			Detail::SetFailure(result, result.strWorkingDirectory, dwMoveError);
			(void)Detail::DeletePathIfExists(result.strWorkingDirectory, result);
			Detail::g_lastStartupConfigBackupResult = result;
			return result;
		}
	} else {
		result.bCreated = true;
	}

	Detail::PruneOldBackups(strConfigDirectory, uRetentionCount, result);
	Detail::g_lastStartupConfigBackupResult = result;
	return result;
}
}
