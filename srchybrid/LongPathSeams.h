#pragma once

#include <array>
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <string>
#include <tchar.h>
#include <vector>
#include <windows.h>

#if defined(__AFX_H__)
class CFileException;
class CSafeBufferedFile;
class CSafeFile;
namespace Kademlia { class CBufferedFileIO; }
#endif

namespace LongPathSeams
{
using PathString = std::basic_string<TCHAR>;
constexpr size_t kCreateDirectoryLegacyHeadroom = 12u;

struct FileSystemObjectIdentity
{
	ULONGLONG ullVolumeSerialNumber = 0;
	std::array<BYTE, 16> fileId = {};
	bool bHasExtendedFileId = false;
};

inline bool operator==(const FileSystemObjectIdentity &rLeft, const FileSystemObjectIdentity &rRight)
{
	return rLeft.ullVolumeSerialNumber == rRight.ullVolumeSerialNumber
		&& rLeft.bHasExtendedFileId == rRight.bHasExtendedFileId
		&& rLeft.fileId == rRight.fileId;
}

/**
 * @brief Normalizes forward slashes to backslashes before building extended-length DOS/UNC paths.
 */
inline PathString NormalizeAbsolutePathSeparators(LPCTSTR pszPath)
{
	PathString normalized(pszPath != NULL ? pszPath : _T(""));
	for (size_t i = 0; i < normalized.size(); ++i) {
		if (normalized[i] == _T('/'))
			normalized[i] = _T('\\');
	}
	return normalized;
}

/**
 * @brief Returns true when the character is treated as a Win32 path separator.
 */
inline bool IsPathSeparator(const TCHAR ch)
{
	return ch == _T('\\') || ch == _T('/');
}

/**
 * @brief Detects whether the incoming Win32 path is already in extended-length form.
 */
inline bool HasLongPathPrefix(LPCTSTR pszPath)
{
	return pszPath != NULL && _tcsnicmp(pszPath, _T("\\\\?\\"), 4) == 0;
}

/**
 * @brief Removes an existing extended-length prefix so callers can inspect the logical DOS/UNC path text.
 */
inline PathString StripLongPathPrefix(LPCTSTR pszPath)
{
	const PathString normalized = NormalizeAbsolutePathSeparators(pszPath);
	if (normalized.rfind(_T("\\\\?\\UNC\\"), 0) == 0)
		return PathString(_T("\\\\")) + normalized.substr(8);
	if (normalized.rfind(_T("\\\\?\\"), 0) == 0)
		return normalized.substr(4);
	return normalized;
}

/**
 * @brief Returns true for fully qualified DOS drive paths such as `C:\dir\file`.
 */
inline bool IsDriveAbsolutePath(LPCTSTR pszPath)
{
	if (pszPath == NULL)
		return false;

	const TCHAR chDrive = pszPath[0];
	return ((chDrive >= _T('A') && chDrive <= _T('Z')) || (chDrive >= _T('a') && chDrive <= _T('z')))
		&& pszPath[1] == _T(':')
		&& (pszPath[2] == _T('\\') || pszPath[2] == _T('/'));
}

/**
 * @brief Returns true for UNC paths that can be converted to `\\?\UNC\...`.
 */
inline bool IsUncPath(LPCTSTR pszPath)
{
	return pszPath != NULL
		&& pszPath[0] == _T('\\')
		&& pszPath[1] == _T('\\')
		&& pszPath[2] != _T('\0')
		&& pszPath[2] != _T('?')
		&& pszPath[2] != _T('.');
}

/**
 * @brief Returns true when the path root denotes a DOS drive or UNC share and the logical tail begins after that root.
 */
inline size_t GetLogicalPathComponentStart(LPCTSTR pszPath)
{
	if (pszPath == NULL || pszPath[0] == _T('\0'))
		return 0u;

	const PathString logicalPath = StripLongPathPrefix(pszPath);
	if (logicalPath.size() >= 3u
		&& ((logicalPath[0] >= _T('A') && logicalPath[0] <= _T('Z')) || (logicalPath[0] >= _T('a') && logicalPath[0] <= _T('z')))
		&& logicalPath[1] == _T(':')
		&& IsPathSeparator(logicalPath[2]))
	{
		return 3u;
	}

	if (logicalPath.size() >= 2u && logicalPath[0] == _T('\\') && logicalPath[1] == _T('\\')) {
		size_t iIndex = 2u;
		while (iIndex < logicalPath.size() && !IsPathSeparator(logicalPath[iIndex]))
			++iIndex;
		while (iIndex < logicalPath.size() && IsPathSeparator(logicalPath[iIndex]))
			++iIndex;
		while (iIndex < logicalPath.size() && !IsPathSeparator(logicalPath[iIndex]))
			++iIndex;
		while (iIndex < logicalPath.size() && IsPathSeparator(logicalPath[iIndex]))
			++iIndex;
		return iIndex;
	}

	return 0u;
}

/**
 * @brief Returns true when a logical path component is a reserved Win32 device alias that requires namespace bypass.
 */
inline bool IsReservedWin32DeviceName(const PathString &rstrSegment)
{
	if (rstrSegment.empty())
		return false;

	PathString strCandidate(rstrSegment);
	while (!strCandidate.empty() && (strCandidate[strCandidate.size() - 1] == _T(' ') || strCandidate[strCandidate.size() - 1] == _T('.')))
		strCandidate.erase(strCandidate.size() - 1);
	if (strCandidate.empty())
		return false;

	const size_t iDot = strCandidate.find(_T('.'));
	if (iDot != PathString::npos)
		strCandidate.erase(iDot);

	PathString strUpper(strCandidate);
	for (size_t i = 0; i < strUpper.size(); ++i)
		strUpper[i] = static_cast<TCHAR>(_totupper(strUpper[i]));

	if (strUpper == _T("CON")
		|| strUpper == _T("PRN")
		|| strUpper == _T("AUX")
		|| strUpper == _T("NUL"))
	{
		return true;
	}

	const auto IsReservedPortName = [&](LPCTSTR pszPrefix) -> bool {
		const size_t nPrefixLength = _tcslen(pszPrefix);
		if (strUpper.compare(0u, nPrefixLength, pszPrefix) != 0 || strUpper.size() != nPrefixLength + 1u)
			return false;

		const TCHAR chDigit = strUpper[nPrefixLength];
		return (chDigit >= _T('1') && chDigit <= _T('9'))
			|| chDigit == static_cast<TCHAR>(0x00B9)
			|| chDigit == static_cast<TCHAR>(0x00B2)
			|| chDigit == static_cast<TCHAR>(0x00B3);
	};

	return IsReservedPortName(_T("COM")) || IsReservedPortName(_T("LPT"));
}

/**
 * @brief Returns true when any logical path component needs namespace semantics to preserve its exact Win32 meaning.
 */
inline bool RequiresExtendedLengthPathForExactName(LPCTSTR pszPath)
{
	if (pszPath == NULL || pszPath[0] == _T('\0'))
		return false;

	const PathString logicalPath = StripLongPathPrefix(pszPath);
	size_t iIndex = GetLogicalPathComponentStart(logicalPath.c_str());
	while (iIndex < logicalPath.size()) {
		while (iIndex < logicalPath.size() && IsPathSeparator(logicalPath[iIndex]))
			++iIndex;
		if (iIndex >= logicalPath.size())
			break;

		const size_t iStart = iIndex;
		while (iIndex < logicalPath.size() && !IsPathSeparator(logicalPath[iIndex]))
			++iIndex;

		const PathString segment = logicalPath.substr(iStart, iIndex - iStart);
		if (!segment.empty()
			&& segment != _T(".")
			&& segment != _T("..")
			&& (segment[0] == _T(' ')
				|| segment[segment.size() - 1] == _T(' ')
				|| segment[segment.size() - 1] == _T('.')
				|| IsReservedWin32DeviceName(segment)))
		{
			return true;
		}
	}

	return false;
}

/**
 * @brief Applies the extended-length prefix to fully qualified overlong or exact-name DOS/UNC paths.
 */
inline PathString PreparePathForLongPath(LPCTSTR pszPath)
{
	if (pszPath == NULL || pszPath[0] == _T('\0'))
		return PathString();

	if (HasLongPathPrefix(pszPath))
		return PathString(pszPath);

	const PathString normalizedPath = NormalizeAbsolutePathSeparators(pszPath);
	const size_t nLength = normalizedPath.size();
	const bool bRequiresExactNamePrefix = RequiresExtendedLengthPathForExactName(normalizedPath.c_str());
	if (nLength < MAX_PATH && !bRequiresExactNamePrefix)
		return PathString(pszPath);
	if (!IsDriveAbsolutePath(normalizedPath.c_str()) && !IsUncPath(normalizedPath.c_str()))
		return PathString(pszPath);

	if (IsUncPath(normalizedPath.c_str())) {
		PathString prepared(_T("\\\\?\\UNC\\"));
		prepared += normalizedPath.c_str() + 2;
		return prepared;
	}

	PathString prepared(_T("\\\\?\\"));
	prepared += normalizedPath;
	return prepared;
}

/**
 * @brief Prepares directory-create paths early enough to avoid the legacy `MAX_PATH - 12` limit or preserve exact namespace-only names.
 */
inline PathString PrepareDirectoryCreatePathForLongPath(LPCTSTR pszPath)
{
	if (pszPath == NULL || pszPath[0] == _T('\0'))
		return PathString();

	if (HasLongPathPrefix(pszPath))
		return PathString(pszPath);

	const PathString normalizedPath = NormalizeAbsolutePathSeparators(pszPath);
	const size_t nLength = normalizedPath.size();
	const bool bRequiresExactNamePrefix = RequiresExtendedLengthPathForExactName(normalizedPath.c_str());
	if (nLength + kCreateDirectoryLegacyHeadroom < MAX_PATH && !bRequiresExactNamePrefix)
		return PathString(pszPath);
	if (!IsDriveAbsolutePath(normalizedPath.c_str()) && !IsUncPath(normalizedPath.c_str()))
		return PathString(pszPath);

	if (IsUncPath(normalizedPath.c_str())) {
		PathString prepared(_T("\\\\?\\UNC\\"));
		prepared += normalizedPath.c_str() + 2;
		return prepared;
	}

	PathString prepared(_T("\\\\?\\"));
	prepared += normalizedPath;
	return prepared;
}

/**
 * @brief Returns compressed file size metadata through the long-path-aware wrapper.
 */
inline DWORD GetCompressedFileSize(LPCTSTR pszPath, LPDWORD pdwFileSizeHigh)
{
	return ::GetCompressedFileSize(PreparePathForLongPath(pszPath).c_str(), pdwFileSizeHigh);
}

/**
 * @brief Returns file attributes through the long-path-aware wrapper.
 */
inline DWORD GetFileAttributes(LPCTSTR pszPath)
{
	return ::GetFileAttributes(PreparePathForLongPath(pszPath).c_str());
}

/**
 * @brief Returns extended file attributes through the long-path-aware wrapper.
 */
inline BOOL GetFileAttributesEx(LPCTSTR pszPath, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation)
{
	return ::GetFileAttributesEx(PreparePathForLongPath(pszPath).c_str(), fInfoLevelId, lpFileInformation);
}

/**
 * @brief Reports whether the target path resolves to an existing filesystem entry.
 */
inline bool PathExists(LPCTSTR pszPath)
{
	return GetFileAttributes(pszPath) != INVALID_FILE_ATTRIBUTES;
}

/**
 * @brief Deletes a file using an extended-length path when needed.
 */
inline BOOL DeleteFile(LPCTSTR pszPath)
{
	return ::DeleteFile(PreparePathForLongPath(pszPath).c_str());
}

/**
 * @brief Creates a directory using the dedicated create-path preparation rule.
 */
inline BOOL CreateDirectory(LPCTSTR pszPath, LPSECURITY_ATTRIBUTES pSecurityAttributes = NULL)
{
	return ::CreateDirectory(PrepareDirectoryCreatePathForLongPath(pszPath).c_str(), pSecurityAttributes);
}

/**
 * @brief Removes a directory using an extended-length path when needed.
 */
inline BOOL RemoveDirectory(LPCTSTR pszPath)
{
	return ::RemoveDirectory(PreparePathForLongPath(pszPath).c_str());
}

/**
 * @brief Queries the resolved filesystem identity for an existing directory.
 */
inline bool TryGetResolvedDirectoryIdentity(LPCTSTR pszPath, FileSystemObjectIdentity &rIdentity, DWORD *pdwLastError = NULL)
{
	rIdentity = FileSystemObjectIdentity{};
	if (pdwLastError != NULL)
		*pdwLastError = ERROR_SUCCESS;
	if (pszPath == NULL || pszPath[0] == _T('\0')) {
		if (pdwLastError != NULL)
			*pdwLastError = ERROR_INVALID_PARAMETER;
		return false;
	}

	HANDLE hDirectory = ::CreateFile(
		PreparePathForLongPath(pszPath).c_str(),
		0,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS,
		NULL);
	if (hDirectory == INVALID_HANDLE_VALUE) {
		if (pdwLastError != NULL)
			*pdwLastError = ::GetLastError();
		return false;
	}

	bool bOk = false;
	DWORD dwError = ERROR_SUCCESS;

	FILE_ID_INFO fileIdInfo = {};
	if (::GetFileInformationByHandleEx(hDirectory, FileIdInfo, &fileIdInfo, sizeof(fileIdInfo)) != FALSE) {
		rIdentity.ullVolumeSerialNumber = fileIdInfo.VolumeSerialNumber;
		for (size_t i = 0; i < rIdentity.fileId.size(); ++i)
			rIdentity.fileId[i] = fileIdInfo.FileId.Identifier[i];
		rIdentity.bHasExtendedFileId = true;
		bOk = true;
	} else {
		dwError = ::GetLastError();
		BY_HANDLE_FILE_INFORMATION handleInfo = {};
		if (::GetFileInformationByHandle(hDirectory, &handleInfo) != FALSE) {
			rIdentity.ullVolumeSerialNumber = handleInfo.dwVolumeSerialNumber;
			rIdentity.fileId[0] = static_cast<BYTE>(handleInfo.nFileIndexLow & 0xFFu);
			rIdentity.fileId[1] = static_cast<BYTE>((handleInfo.nFileIndexLow >> 8) & 0xFFu);
			rIdentity.fileId[2] = static_cast<BYTE>((handleInfo.nFileIndexLow >> 16) & 0xFFu);
			rIdentity.fileId[3] = static_cast<BYTE>((handleInfo.nFileIndexLow >> 24) & 0xFFu);
			rIdentity.fileId[4] = static_cast<BYTE>(handleInfo.nFileIndexHigh & 0xFFu);
			rIdentity.fileId[5] = static_cast<BYTE>((handleInfo.nFileIndexHigh >> 8) & 0xFFu);
			rIdentity.fileId[6] = static_cast<BYTE>((handleInfo.nFileIndexHigh >> 16) & 0xFFu);
			rIdentity.fileId[7] = static_cast<BYTE>((handleInfo.nFileIndexHigh >> 24) & 0xFFu);
			rIdentity.bHasExtendedFileId = false;
			dwError = ERROR_SUCCESS;
			bOk = true;
		}
	}

	const DWORD dwCloseError = (::CloseHandle(hDirectory) != FALSE) ? ERROR_SUCCESS : ::GetLastError();
	if (!bOk) {
		if (pdwLastError != NULL)
			*pdwLastError = dwError;
		return false;
	}
	if (dwCloseError != ERROR_SUCCESS) {
		if (pdwLastError != NULL)
			*pdwLastError = dwCloseError;
		return false;
	}
	return true;
}

/**
 * @brief Opens a Win32 file handle through the shared long-path preparation helper.
 */
inline HANDLE CreateFile(LPCTSTR pszPath, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES pSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile = NULL)
{
	return ::CreateFile(PreparePathForLongPath(pszPath).c_str(), dwDesiredAccess, dwShareMode, pSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

/**
 * @brief Starts directory enumeration with long-path preparation applied to the search root.
 */
inline HANDLE FindFirstFile(LPCTSTR pszPath, LPWIN32_FIND_DATA pFindFileData)
{
	return ::FindFirstFile(PreparePathForLongPath(pszPath).c_str(), pFindFileData);
}

/**
 * @brief Renames or moves a file using long-path-aware source and destination paths.
 */
inline BOOL MoveFile(LPCTSTR pszExistingPath, LPCTSTR pszNewPath)
{
	return ::MoveFile(PreparePathForLongPath(pszExistingPath).c_str(), PreparePathForLongPath(pszNewPath).c_str());
}

/**
 * @brief Renames or moves a file with flags using long-path-aware source and destination paths.
 */
inline BOOL MoveFileEx(LPCTSTR pszExistingPath, LPCTSTR pszNewPath, DWORD dwFlags)
{
	return ::MoveFileEx(PreparePathForLongPath(pszExistingPath).c_str(), PreparePathForLongPath(pszNewPath).c_str(), dwFlags);
}

/**
 * @brief Copies a file using long-path-aware source and destination paths.
 */
inline BOOL CopyFile(LPCTSTR pszExistingPath, LPCTSTR pszNewPath, BOOL bFailIfExists)
{
	return ::CopyFile(PreparePathForLongPath(pszExistingPath).c_str(), PreparePathForLongPath(pszNewPath).c_str(), bFailIfExists);
}

/**
 * @brief Moves a file with progress callbacks using long-path-aware source and destination paths.
 */
inline BOOL MoveFileWithProgress(LPCTSTR pszExistingPath, LPCTSTR pszNewPath, LPPROGRESS_ROUTINE pProgressRoutine, LPVOID pData, DWORD dwFlags)
{
	return ::MoveFileWithProgress(PreparePathForLongPath(pszExistingPath).c_str(), PreparePathForLongPath(pszNewPath).c_str(), pProgressRoutine, pData, dwFlags);
}

/**
 * @brief Deletes a file and treats `file not found` as a successful no-op.
 */
inline bool DeleteFileIfExists(LPCTSTR pszPath)
{
	if (DeleteFile(pszPath) != FALSE)
		return true;
	const DWORD dwLastError = ::GetLastError();
	return dwLastError == ERROR_FILE_NOT_FOUND || dwLastError == ERROR_PATH_NOT_FOUND;
}

/**
 * @brief Reads a whole file into memory through the shared long-path-aware Win32 path wrapper.
 */
inline bool ReadAllBytes(LPCTSTR pszPath, std::vector<unsigned char> &rBytes)
{
	rBytes.clear();
	if (pszPath == NULL || *pszPath == _T('\0'))
		return false;

	HANDLE hFile = CreateFile(pszPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	LARGE_INTEGER fileSize = {};
	const BOOL bHasSize = ::GetFileSizeEx(hFile, &fileSize);
	if (!bHasSize || fileSize.QuadPart < 0) {
		::CloseHandle(hFile);
		return false;
	}
	if (fileSize.QuadPart > MAXDWORD) {
		::CloseHandle(hFile);
		::SetLastError(ERROR_FILE_TOO_LARGE);
		return false;
	}

	rBytes.resize(static_cast<size_t>(fileSize.QuadPart));
	DWORD dwBytesRead = 0;
	const BOOL bReadOk = (fileSize.QuadPart == 0)
		|| ::ReadFile(hFile, rBytes.data(), static_cast<DWORD>(fileSize.QuadPart), &dwBytesRead, NULL);
	const DWORD dwCloseError = (::CloseHandle(hFile) != FALSE) ? ERROR_SUCCESS : ::GetLastError();
	if (bReadOk == FALSE || dwBytesRead != static_cast<DWORD>(fileSize.QuadPart))
		return false;
	if (dwCloseError != ERROR_SUCCESS) {
		::SetLastError(dwCloseError);
		return false;
	}
	return true;
}

/**
 * @brief Writes a complete byte buffer to disk through the shared long-path-aware Win32 path wrapper.
 */
inline bool WriteAllBytes(LPCTSTR pszPath, const unsigned char *pBytes, const size_t nByteCount, const DWORD dwCreationDisposition = CREATE_ALWAYS, const DWORD dwShareMode = FILE_SHARE_READ)
{
	if (pszPath == NULL || *pszPath == _T('\0') || (pBytes == NULL && nByteCount != 0))
		return false;
	if (nByteCount > MAXDWORD) {
		::SetLastError(ERROR_FILE_TOO_LARGE);
		return false;
	}

	HANDLE hFile = CreateFile(pszPath, GENERIC_WRITE, dwShareMode, NULL, dwCreationDisposition, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	DWORD dwBytesWritten = 0;
	const BOOL bWriteOk = (nByteCount == 0)
		|| ::WriteFile(hFile, pBytes, static_cast<DWORD>(nByteCount), &dwBytesWritten, NULL);
	const DWORD dwCloseError = (::CloseHandle(hFile) != FALSE) ? ERROR_SUCCESS : ::GetLastError();
	if (bWriteOk == FALSE || dwBytesWritten != static_cast<DWORD>(nByteCount))
		return false;
	if (dwCloseError != ERROR_SUCCESS) {
		::SetLastError(dwCloseError);
		return false;
	}
	return true;
}

/**
 * @brief Writes a complete byte vector to disk through the shared long-path-aware Win32 path wrapper.
 */
inline bool WriteAllBytes(LPCTSTR pszPath, const std::vector<unsigned char> &rBytes, const DWORD dwCreationDisposition = CREATE_ALWAYS, const DWORD dwShareMode = FILE_SHARE_READ)
{
	return WriteAllBytes(pszPath, rBytes.empty() ? NULL : rBytes.data(), rBytes.size(), dwCreationDisposition, dwShareMode);
}

/**
 * @brief Opens a CRT file descriptor through a Win32 handle for long-path-safe readers and writers.
 */
inline int OpenCrtFileDescriptorLongPath(LPCTSTR pszPath, DWORD dwDesiredAccess, DWORD dwShareMode, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, int nOpenFlags)
{
	HANDLE hFile = CreateFile(pszPath, dwDesiredAccess, dwShareMode, NULL, dwCreationDisposition, dwFlagsAndAttributes, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return -1;

	const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(hFile), nOpenFlags);
	if (fd == -1)
		::CloseHandle(hFile);
	return fd;
}

/**
 * @brief Opens a `FILE*` stream via a Win32 handle so long paths bypass CRT path limits.
 */
inline FILE* OpenFileStreamLongPath(LPCTSTR pszPath, DWORD dwDesiredAccess, DWORD dwShareMode, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, int nOpenFlags, const char *pszStreamMode, const bool bSeekToEnd = false)
{
	const int fd = OpenCrtFileDescriptorLongPath(pszPath, dwDesiredAccess, dwShareMode, dwCreationDisposition, dwFlagsAndAttributes, nOpenFlags);
	if (fd == -1)
		return NULL;

	FILE *pStream = _fdopen(fd, pszStreamMode);
	if (pStream == NULL) {
		_close(fd);
		return NULL;
	}

	if (bSeekToEnd && fseek(pStream, 0, SEEK_END) != 0) {
		fclose(pStream);
		return NULL;
	}

	return pStream;
}

/**
 * @brief Recreates the common `_tfsopen(..., _SH_DENYWR)` patterns on top of long-path-aware Win32 opens.
 */
inline FILE* OpenFileStreamDenyWriteLongPath(LPCTSTR pszPath, LPCTSTR pszMode)
{
	if (pszPath == NULL || pszMode == NULL || pszMode[0] == _T('\0'))
		return NULL;

	const TCHAR chBaseMode = static_cast<TCHAR>(_totlower(pszMode[0]));
	const bool bBinaryMode = _tcschr(pszMode, _T('b')) != NULL;
	const bool bPlusMode = _tcschr(pszMode, _T('+')) != NULL;
	const int nTextFlag = bBinaryMode ? _O_BINARY : _O_TEXT;

	DWORD dwDesiredAccess = 0;
	DWORD dwCreationDisposition = OPEN_EXISTING;
	int nOpenFlags = nTextFlag;
	const char *pszStreamMode = NULL;
	bool bSeekToEnd = false;

	switch (chBaseMode) {
		case _T('r'):
			dwDesiredAccess = bPlusMode ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ;
			dwCreationDisposition = OPEN_EXISTING;
			nOpenFlags |= bPlusMode ? _O_RDWR : _O_RDONLY;
			pszStreamMode = bBinaryMode ? (bPlusMode ? "r+b" : "rb") : (bPlusMode ? "r+" : "r");
			break;
		case _T('w'):
			dwDesiredAccess = bPlusMode ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_WRITE;
			dwCreationDisposition = CREATE_ALWAYS;
			nOpenFlags |= bPlusMode ? _O_RDWR : _O_WRONLY;
			pszStreamMode = bBinaryMode ? (bPlusMode ? "w+b" : "wb") : (bPlusMode ? "w+" : "w");
			break;
		case _T('a'):
			dwDesiredAccess = bPlusMode ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_WRITE;
			dwCreationDisposition = OPEN_ALWAYS;
			nOpenFlags |= (bPlusMode ? _O_RDWR : _O_WRONLY) | _O_APPEND;
			pszStreamMode = bBinaryMode ? (bPlusMode ? "a+b" : "ab") : (bPlusMode ? "a+" : "a");
			bSeekToEnd = true;
			break;
		default:
			return NULL;
	}

	return OpenFileStreamLongPath(pszPath, dwDesiredAccess, FILE_SHARE_READ, dwCreationDisposition, FILE_ATTRIBUTE_NORMAL, nOpenFlags, pszStreamMode, bSeekToEnd);
}

/**
 * @brief Opens a shared-read `FILE*` stream for buffered long-path-safe reads.
 */
inline FILE* OpenFileStreamSharedReadLongPath(LPCTSTR pszPath, const bool bTextMode)
{
	const DWORD dwFlags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN;
	return OpenFileStreamLongPath(pszPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, dwFlags, _O_RDONLY | (bTextMode ? _O_TEXT : _O_BINARY), bTextMode ? "r" : "rb");
}

/**
 * @brief Opens a read-only CRT file descriptor through a Win32 handle for long-path-safe consumers.
 */
inline int OpenCrtReadOnlyLongPath(LPCTSTR pszPath)
{
	const DWORD dwFlags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN;
	return OpenCrtFileDescriptorLongPath(pszPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, dwFlags, _O_RDONLY | _O_BINARY);
}

/**
 * @brief Opens a write-only CRT file descriptor through a Win32 handle for long-path-safe output paths.
 */
inline int OpenCrtWriteOnlyLongPath(LPCTSTR pszPath, DWORD dwCreationDisposition = CREATE_ALWAYS, DWORD dwShareMode = FILE_SHARE_READ)
{
	return OpenCrtFileDescriptorLongPath(pszPath, GENERIC_WRITE, dwShareMode, dwCreationDisposition, FILE_ATTRIBUTE_NORMAL, _O_WRONLY | _O_BINARY);
}

#if defined(__AFX_H__)
/**
 * @brief Opens a `CSafeFile` through the shared long-path Win32 wrapper while preserving MFC-style flags.
 */
bool OpenFile(CSafeFile &file, LPCTSTR lpszFileName, UINT nOpenFlags, CFileException *pError = NULL);

/**
 * @brief Opens a `CSafeBufferedFile` through the shared long-path Win32+stdio wrapper while preserving MFC-style flags.
 */
bool OpenFile(CSafeBufferedFile &file, LPCTSTR lpszFileName, UINT nOpenFlags, CFileException *pError = NULL);

/**
 * @brief Opens a `Kademlia::CBufferedFileIO` through the shared long-path Win32+stdio wrapper while preserving MFC-style flags.
 */
bool OpenFile(Kademlia::CBufferedFileIO &file, LPCTSTR lpszFileName, UINT nOpenFlags, CFileException *pError = NULL);
#endif
}
