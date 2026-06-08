#pragma once

#include <array>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <tchar.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <windows.h>
#include <winioctl.h>

#if defined(__AFX_H__)
class CFileException;
class CSafeBufferedFile;
class CSafeFile;
namespace Kademlia { class CBufferedFileIO; }
#endif

#define EMULEBB_LONG_PATH_SEAMS_HAS_NTFS_JOURNAL_HELPERS 1

namespace LongPathSeams
{
using PathString = std::basic_string<TCHAR>;
constexpr size_t kCreateDirectoryLegacyHeadroom = 12u;
constexpr size_t kPreparedPathMemoMaxEntries = 16384u;

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
 * @brief Stores one journal file reference in a version-agnostic 128-bit form.
 */
struct UsnFileReference
{
	std::array<BYTE, 16> identifier = {};
};

inline bool operator==(const UsnFileReference &rLeft, const UsnFileReference &rRight)
{
	return rLeft.identifier == rRight.identifier;
}

struct UsnFileReferenceHasher
{
	size_t operator()(const UsnFileReference &rReference) const noexcept
	{
		size_t uHash = 1469598103934665603ull;
		for (const BYTE value : rReference.identifier) {
			uHash ^= value;
			uHash *= 1099511628211ull;
		}
		return uHash;
	}
};

inline bool IsZeroUsnFileReference(const UsnFileReference &rReference)
{
	for (const BYTE value : rReference.identifier)
		if (value != 0)
			return false;
	return true;
}

inline UsnFileReference MakeUsnFileReferenceFromUInt64(const ULONGLONG ullValue)
{
	UsnFileReference reference = {};
	static_assert(sizeof(ullValue) <= sizeof(reference.identifier), "USN file reference storage must fit 64-bit identifiers");
	std::memcpy(reference.identifier.data(), &ullValue, sizeof(ullValue));
	return reference;
}

inline UsnFileReference MakeUsnFileReferenceFromFileId128(const FILE_ID_128 &rFileId)
{
	UsnFileReference reference = {};
	std::memcpy(reference.identifier.data(), rFileId.Identifier, sizeof(rFileId.Identifier));
	return reference;
}

/**
 * @brief Captures the local NTFS USN journal guard for one volume.
 */
struct NtfsJournalVolumeState
{
	PathString strVolumeKey;
	PathString strMountRoot;
	PathString strVolumeGuidPath;
	ULONGLONG ullVolumeSerialNumber = 0;
	ULONGLONG ullUsnJournalId = 0;
	LONGLONG llLowestValidUsn = 0;
	LONGLONG llNextUsn = 0;
};

/**
 * @brief Captures the directory reference number used for NTFS journal validation.
 */
struct NtfsDirectoryJournalState
{
	UsnFileReference fileReference = {};
	LONGLONG llUsn = 0;
};

inline bool TryParseUsnRecordIdentity(const USN_RECORD_COMMON_HEADER *pHeader, const DWORD dwBytesAvailable, UsnFileReference &rFileReference, UsnFileReference *pParentFileReference = NULL, LONGLONG *pllUsn = NULL, DWORD *pdwLastError = NULL)
{
	rFileReference = UsnFileReference{};
	if (pParentFileReference != NULL)
		*pParentFileReference = UsnFileReference{};
	if (pllUsn != NULL)
		*pllUsn = 0;
	if (pdwLastError != NULL)
		*pdwLastError = ERROR_SUCCESS;
	if (pHeader == NULL || dwBytesAvailable < sizeof(USN_RECORD_COMMON_HEADER)) {
		if (pdwLastError != NULL)
			*pdwLastError = ERROR_INVALID_PARAMETER;
		return false;
	}

	switch (pHeader->MajorVersion)
	{
		case 2:
		{
			if (dwBytesAvailable < sizeof(USN_RECORD_V2)) {
				if (pdwLastError != NULL)
					*pdwLastError = ERROR_INVALID_DATA;
				return false;
			}
			const USN_RECORD_V2 *pRecord = reinterpret_cast<const USN_RECORD_V2 *>(pHeader);
			rFileReference = MakeUsnFileReferenceFromUInt64(pRecord->FileReferenceNumber);
			if (pParentFileReference != NULL)
				*pParentFileReference = MakeUsnFileReferenceFromUInt64(pRecord->ParentFileReferenceNumber);
			if (pllUsn != NULL)
				*pllUsn = pRecord->Usn;
			return true;
		}
		case 3:
		{
			if (dwBytesAvailable < sizeof(USN_RECORD_V3)) {
				if (pdwLastError != NULL)
					*pdwLastError = ERROR_INVALID_DATA;
				return false;
			}
			const USN_RECORD_V3 *pRecord = reinterpret_cast<const USN_RECORD_V3 *>(pHeader);
			rFileReference = MakeUsnFileReferenceFromFileId128(pRecord->FileReferenceNumber);
			if (pParentFileReference != NULL)
				*pParentFileReference = MakeUsnFileReferenceFromFileId128(pRecord->ParentFileReferenceNumber);
			if (pllUsn != NULL)
				*pllUsn = pRecord->Usn;
			return true;
		}
		case 4:
		{
			if (dwBytesAvailable < sizeof(USN_RECORD_V4)) {
				if (pdwLastError != NULL)
					*pdwLastError = ERROR_INVALID_DATA;
				return false;
			}
			const USN_RECORD_V4 *pRecord = reinterpret_cast<const USN_RECORD_V4 *>(pHeader);
			rFileReference = MakeUsnFileReferenceFromFileId128(pRecord->FileReferenceNumber);
			if (pParentFileReference != NULL)
				*pParentFileReference = MakeUsnFileReferenceFromFileId128(pRecord->ParentFileReferenceNumber);
			if (pllUsn != NULL)
				*pllUsn = pRecord->Usn;
			return true;
		}
		default:
		{
			if (pdwLastError != NULL)
				*pdwLastError = ERROR_NOT_SUPPORTED;
			return false;
		}
	}
}

/**
 * @brief Describes the real local volume that owns one absolute path, including mounted-folder volumes.
 */
struct ResolvedVolumeContext
{
	PathString strMountRoot;
	PathString strVolumeGuidPath;
	PathString strVolumeKey;
	PathString strFileSystemName;
	ULONGLONG ullVolumeSerialNumber = 0;
	DWORD dwDriveType = DRIVE_UNKNOWN;
	DWORD dwFileSystemFlags = 0;
	DWORD dwMaximumComponentLength = 0;
	bool bIsLocal = false;
	bool bIsNtfs = false;
	bool bSupportsJournal = false;
};

/**
 * @brief Coarse startup-storage risks that should be visible in diagnostics.
 */
enum class StoragePlacementRisk
{
	None = 0,
	NetworkShare,
	RemovableDrive
};

/**
 * @brief Captures the resolved storage root and drive classification for one startup directory path.
 */
struct StoragePlacementProbeResult
{
	PathString strInputPath;
	PathString strVolumeRoot;
	DWORD dwDriveType = DRIVE_UNKNOWN;
	DWORD dwLastError = ERROR_SUCCESS;
	StoragePlacementRisk eRisk = StoragePlacementRisk::None;
	bool bResolved = false;
};

/**
 * @brief Normalizes forward slashes to backslashes before building extended-length DOS/UNC paths.
 */
inline PathString NormalizeAbsolutePathSeparators(LPCTSTR pszPath)
{
	PathString normalized(pszPath != NULL ? pszPath : _T(""));
	const size_t iForwardSlash = normalized.find(_T('/'));
	if (iForwardSlash == PathString::npos)
		return normalized;

	normalized[iForwardSlash] = _T('\\');
	for (size_t i = iForwardSlash + 1u; i < normalized.size(); ++i) {
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

inline PathString StripLongPathPrefixRaw(const PathString &rNormalizedPath)
{
	if (rNormalizedPath.rfind(_T("\\\\?\\UNC\\"), 0) == 0)
		return PathString(_T("\\\\")) + rNormalizedPath.substr(8);
	if (rNormalizedPath.rfind(_T("\\\\?\\"), 0) == 0)
		return rNormalizedPath.substr(4);
	return rNormalizedPath;
}

/**
 * @brief Removes an existing extended-length prefix so callers can inspect the logical DOS/UNC path text.
 */
inline PathString StripLongPathPrefix(LPCTSTR pszPath)
{
	return StripLongPathPrefixRaw(NormalizeAbsolutePathSeparators(pszPath));
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

inline bool IsReservedWin32DeviceName(const PathString &rstrSegment);

inline bool RequiresExtendedLengthPathForExactNameRaw(const PathString &rLogicalPath)
{
	size_t iIndex = 0u;
	if (rLogicalPath.size() >= 3u
		&& ((rLogicalPath[0] >= _T('A') && rLogicalPath[0] <= _T('Z')) || (rLogicalPath[0] >= _T('a') && rLogicalPath[0] <= _T('z')))
		&& rLogicalPath[1] == _T(':')
		&& IsPathSeparator(rLogicalPath[2]))
	{
		iIndex = 3u;
	} else if (rLogicalPath.size() >= 2u && rLogicalPath[0] == _T('\\') && rLogicalPath[1] == _T('\\')) {
		iIndex = 2u;
		while (iIndex < rLogicalPath.size() && !IsPathSeparator(rLogicalPath[iIndex]))
			++iIndex;
		while (iIndex < rLogicalPath.size() && IsPathSeparator(rLogicalPath[iIndex]))
			++iIndex;
		while (iIndex < rLogicalPath.size() && !IsPathSeparator(rLogicalPath[iIndex]))
			++iIndex;
		while (iIndex < rLogicalPath.size() && IsPathSeparator(rLogicalPath[iIndex]))
			++iIndex;
	}
	while (iIndex < rLogicalPath.size()) {
		while (iIndex < rLogicalPath.size() && IsPathSeparator(rLogicalPath[iIndex]))
			++iIndex;
		if (iIndex >= rLogicalPath.size())
			break;

		const size_t iStart = iIndex;
		while (iIndex < rLogicalPath.size() && !IsPathSeparator(rLogicalPath[iIndex]))
			++iIndex;

		const PathString segment = rLogicalPath.substr(iStart, iIndex - iStart);
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

inline PathString PreparePathForLongPathRaw(const PathString &rOriginalPath, const PathString &rNormalizedPath, const bool bRequiresExactNamePrefix)
{
	const size_t nLength = rNormalizedPath.size();
	if (nLength < MAX_PATH && !bRequiresExactNamePrefix)
		return rOriginalPath;
	if (!IsDriveAbsolutePath(rNormalizedPath.c_str()) && !IsUncPath(rNormalizedPath.c_str()))
		return rOriginalPath;

	if (IsUncPath(rNormalizedPath.c_str())) {
		PathString prepared(_T("\\\\?\\UNC\\"));
		prepared += rNormalizedPath.c_str() + 2;
		return prepared;
	}

	PathString prepared(_T("\\\\?\\"));
	prepared += rNormalizedPath;
	return prepared;
}

inline PathString PrepareDirectoryCreatePathForLongPathRaw(const PathString &rOriginalPath, const PathString &rNormalizedPath, const bool bRequiresExactNamePrefix)
{
	const size_t nLength = rNormalizedPath.size();
	if (nLength + kCreateDirectoryLegacyHeadroom < MAX_PATH && !bRequiresExactNamePrefix)
		return rOriginalPath;
	if (!IsDriveAbsolutePath(rNormalizedPath.c_str()) && !IsUncPath(rNormalizedPath.c_str()))
		return rOriginalPath;

	if (IsUncPath(rNormalizedPath.c_str())) {
		PathString prepared(_T("\\\\?\\UNC\\"));
		prepared += rNormalizedPath.c_str() + 2;
		return prepared;
	}

	PathString prepared(_T("\\\\?\\"));
	prepared += rNormalizedPath;
	return prepared;
}

/**
 * @brief Stores immutable path-preparation facts reused by long-path seam callers.
 */
struct PreparedPathMemoRecord
{
	PathString strOriginalPath;
	PathString strNormalizedPath;
	PathString strLogicalPath;
	PathString strPreparedPath;
	PathString strPreparedCreateDirectoryPath;
	bool bRequiresExactNamePrefix = false;
};

/**
 * @brief Reports current process-wide path-preparation memoization state.
 */
struct PreparedPathMemoStats
{
	size_t uEntryCount = 0;
	ULONGLONG ullCacheHits = 0;
	ULONGLONG ullCacheMisses = 0;
};

namespace detail
{
struct PreparedPathMemoCache
{
	std::shared_mutex mutex;
	std::unordered_map<PathString, PreparedPathMemoRecord> entries;
	std::atomic_ullong cacheHits{ 0 };
	std::atomic_ullong cacheMisses{ 0 };
};

inline PreparedPathMemoCache &GetPreparedPathMemoCache()
{
	static PreparedPathMemoCache cache;
	return cache;
}

inline PreparedPathMemoRecord BuildPreparedPathMemoRecord(LPCTSTR pszPath)
{
	PreparedPathMemoRecord record = {};
	record.strOriginalPath.assign(pszPath != NULL ? pszPath : _T(""));
	record.strNormalizedPath = NormalizeAbsolutePathSeparators(record.strOriginalPath.c_str());
	record.strLogicalPath = StripLongPathPrefixRaw(record.strNormalizedPath);
	record.bRequiresExactNamePrefix = RequiresExtendedLengthPathForExactNameRaw(record.strLogicalPath);
	if (HasLongPathPrefix(record.strNormalizedPath.c_str())) {
		record.strPreparedPath = record.strOriginalPath;
		record.strPreparedCreateDirectoryPath = record.strOriginalPath;
	} else {
		record.strPreparedPath = PreparePathForLongPathRaw(record.strOriginalPath, record.strNormalizedPath, record.bRequiresExactNamePrefix);
		record.strPreparedCreateDirectoryPath = PrepareDirectoryCreatePathForLongPathRaw(record.strOriginalPath, record.strNormalizedPath, record.bRequiresExactNamePrefix);
	}
	return record;
}

inline PreparedPathMemoRecord GetPreparedPathMemoRecord(LPCTSTR pszPath)
{
	if (pszPath == NULL || pszPath[0] == _T('\0'))
		return BuildPreparedPathMemoRecord(pszPath);

	PreparedPathMemoCache &cache = GetPreparedPathMemoCache();
	{
		std::shared_lock<std::shared_mutex> readLock(cache.mutex);
		const auto it = cache.entries.find(pszPath);
		if (it != cache.entries.end()) {
			++cache.cacheHits;
			return it->second;
		}
	}

	PreparedPathMemoRecord record = BuildPreparedPathMemoRecord(pszPath);
	{
		std::unique_lock<std::shared_mutex> writeLock(cache.mutex);
		const auto it = cache.entries.find(record.strOriginalPath);
		if (it != cache.entries.end()) {
			++cache.cacheHits;
			return it->second;
		}
		if (cache.entries.size() >= kPreparedPathMemoMaxEntries)
			cache.entries.clear();
		cache.entries.emplace(record.strOriginalPath, record);
	}
	++cache.cacheMisses;
	return record;
}
}

/**
 * @brief Clears the reusable process-wide path-preparation memoization cache.
 */
inline void ClearPreparedPathMemoizationCache()
{
	detail::PreparedPathMemoCache &cache = detail::GetPreparedPathMemoCache();
	std::unique_lock<std::shared_mutex> writeLock(cache.mutex);
	cache.entries.clear();
	cache.cacheHits = 0;
	cache.cacheMisses = 0;
}

/**
 * @brief Removes one exact path entry from the reusable path-preparation memo cache.
 */
inline void ForgetPreparedPathMemoizationEntry(LPCTSTR pszPath)
{
	if (pszPath == NULL || pszPath[0] == _T('\0'))
		return;

	detail::PreparedPathMemoCache &cache = detail::GetPreparedPathMemoCache();
	std::unique_lock<std::shared_mutex> writeLock(cache.mutex);
	cache.entries.erase(PathString(pszPath));
}

/**
 * @brief Returns current path-preparation memoization counters for tests and diagnostics.
 */
inline PreparedPathMemoStats GetPreparedPathMemoizationStats()
{
	PreparedPathMemoStats stats = {};
	detail::PreparedPathMemoCache &cache = detail::GetPreparedPathMemoCache();
	std::shared_lock<std::shared_mutex> readLock(cache.mutex);
	stats.uEntryCount = cache.entries.size();
	stats.ullCacheHits = cache.cacheHits.load();
	stats.ullCacheMisses = cache.cacheMisses.load();
	return stats;
}

/**
 * @brief Returns normalized path text from the shared path-preparation memo cache.
 */
inline PathString GetNormalizedPathText(LPCTSTR pszPath)
{
	return detail::GetPreparedPathMemoRecord(pszPath).strNormalizedPath;
}

/**
 * @brief Returns logical DOS/UNC path text from the shared path-preparation memo cache.
 */
inline PathString GetLogicalPathText(LPCTSTR pszPath)
{
	return detail::GetPreparedPathMemoRecord(pszPath).strLogicalPath;
}

/**
 * @brief Returns true when the path root denotes a DOS drive or UNC share and the logical tail begins after that root.
 */
inline size_t GetLogicalPathComponentStart(LPCTSTR pszPath)
{
	if (pszPath == NULL || pszPath[0] == _T('\0'))
		return 0u;

	const PathString logicalPath = GetLogicalPathText(pszPath);
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
 * @brief Ensures a path-like string ends with one directory separator.
 */
inline bool EnsureTrailingSeparator(PathString &rPath)
{
	if (!rPath.empty() && !IsPathSeparator(rPath[rPath.size() - 1]))
		rPath += _T('\\');
	return !rPath.empty();
}

/**
 * @brief Returns the drive or UNC root from normalized logical path text when it is syntactically available.
 */
inline PathString GetLogicalStorageRoot(LPCTSTR pszPath)
{
	if (pszPath == NULL || pszPath[0] == _T('\0'))
		return PathString();

	const PathString logicalPath = GetLogicalPathText(pszPath);
	const size_t uRootLength = GetLogicalPathComponentStart(logicalPath.c_str());
	if (uRootLength == 0u || uRootLength > logicalPath.size())
		return PathString();
	return logicalPath.substr(0u, uRootLength);
}

/**
 * @brief Maps Win32 drive types to startup-storage warning classes.
 */
inline StoragePlacementRisk GetStoragePlacementRiskFromDriveType(const DWORD dwDriveType)
{
	if (dwDriveType == DRIVE_REMOTE)
		return StoragePlacementRisk::NetworkShare;
	if (dwDriveType == DRIVE_REMOVABLE)
		return StoragePlacementRisk::RemovableDrive;
	return StoragePlacementRisk::None;
}

/**
 * @brief Classifies whether a path resolves to network or removable storage using injectable Win32 probes.
 */
template <typename GetVolumePathNameFn, typename GetDriveTypeFn>
inline StoragePlacementProbeResult ClassifyStoragePlacement(
	LPCTSTR pszPath,
	GetVolumePathNameFn getVolumePathNameFn,
	GetDriveTypeFn getDriveTypeFn)
{
	StoragePlacementProbeResult result;
	result.strInputPath = NormalizeAbsolutePathSeparators(pszPath);
	if (pszPath == NULL || pszPath[0] == _T('\0')) {
		result.dwLastError = ERROR_INVALID_PARAMETER;
		return result;
	}

	const PathString strLogicalPath(GetLogicalPathText(pszPath));
	if (GetLogicalPathComponentStart(strLogicalPath.c_str()) == 0u) {
		result.dwLastError = ERROR_NOT_SUPPORTED;
		return result;
	}

	std::vector<TCHAR> volumeRootBuffer(4096u, _T('\0'));
	if (getVolumePathNameFn(strLogicalPath.c_str(), volumeRootBuffer.data(), static_cast<DWORD>(volumeRootBuffer.size())) != FALSE) {
		result.strVolumeRoot.assign(volumeRootBuffer.data());
		result.strVolumeRoot = NormalizeAbsolutePathSeparators(result.strVolumeRoot.c_str());
	} else {
		result.dwLastError = ::GetLastError();
		result.strVolumeRoot = GetLogicalStorageRoot(strLogicalPath.c_str());
	}

	if (result.strVolumeRoot.empty())
		return result;

	EnsureTrailingSeparator(result.strVolumeRoot);
	result.dwDriveType = getDriveTypeFn(result.strVolumeRoot.c_str());
	result.eRisk = GetStoragePlacementRiskFromDriveType(result.dwDriveType);
	result.bResolved = result.dwDriveType != DRIVE_UNKNOWN && result.dwDriveType != DRIVE_NO_ROOT_DIR;
	if (!result.bResolved && result.dwLastError == ERROR_SUCCESS)
		result.dwLastError = ERROR_PATH_NOT_FOUND;
	return result;
}

/**
 * @brief Classifies whether a path resolves to network or removable storage using the real Win32 APIs.
 */
inline StoragePlacementProbeResult ClassifyStoragePlacement(LPCTSTR pszPath)
{
	return ClassifyStoragePlacement(
		pszPath,
		[](LPCTSTR pszFileName, LPTSTR pszVolumePathName, DWORD cchBufferLength) -> BOOL {
			return ::GetVolumePathName(pszFileName, pszVolumePathName, cchBufferLength);
		},
		[](LPCTSTR pszRootPathName) -> UINT {
			return ::GetDriveType(pszRootPathName);
		});
}

/**
 * @brief Normalizes one volume GUID path so callers can compare and hash mounted-volume identities consistently.
 */
inline PathString NormalizeVolumeGuidPathForKey(const PathString &rPath)
{
	PathString normalized(rPath);
	for (size_t i = 0; i < normalized.size(); ++i) {
		if (normalized[i] == _T('/'))
			normalized[i] = _T('\\');
		else
			normalized[i] = static_cast<TCHAR>(_totlower(normalized[i]));
	}
	EnsureTrailingSeparator(normalized);
	return normalized;
}

/**
 * @brief Resolves the real containing local volume for an absolute path, including mounted-folder volumes.
 */
inline bool TryResolveContainingVolumeContext(LPCTSTR pszPath, ResolvedVolumeContext &rContext, DWORD *pdwLastError = NULL)
{
	rContext = ResolvedVolumeContext{};
	if (pdwLastError != NULL)
		*pdwLastError = ERROR_SUCCESS;
	if (pszPath == NULL || pszPath[0] == _T('\0')) {
		if (pdwLastError != NULL)
			*pdwLastError = ERROR_INVALID_PARAMETER;
		return false;
	}

	const PathString logicalPath = GetLogicalPathText(pszPath);
	if (!IsDriveAbsolutePath(logicalPath.c_str())) {
		if (pdwLastError != NULL)
			*pdwLastError = ERROR_NOT_SUPPORTED;
		return false;
	}

	std::vector<TCHAR> mountRootBuffer(4096u, _T('\0'));
	if (!::GetVolumePathName(logicalPath.c_str(), mountRootBuffer.data(), static_cast<DWORD>(mountRootBuffer.size()))) {
		if (pdwLastError != NULL)
			*pdwLastError = ::GetLastError();
		return false;
	}

	rContext.strMountRoot.assign(mountRootBuffer.data());
	rContext.strMountRoot = NormalizeAbsolutePathSeparators(rContext.strMountRoot.c_str());
	EnsureTrailingSeparator(rContext.strMountRoot);
	rContext.dwDriveType = ::GetDriveType(rContext.strMountRoot.c_str());
	if (rContext.dwDriveType == DRIVE_REMOTE || rContext.dwDriveType == DRIVE_NO_ROOT_DIR || rContext.dwDriveType == DRIVE_UNKNOWN) {
		if (pdwLastError != NULL)
			*pdwLastError = ERROR_NOT_SUPPORTED;
		return false;
	}

	std::vector<TCHAR> volumeGuidBuffer(4096u, _T('\0'));
	if (!::GetVolumeNameForVolumeMountPoint(rContext.strMountRoot.c_str(), volumeGuidBuffer.data(), static_cast<DWORD>(volumeGuidBuffer.size()))) {
		if (pdwLastError != NULL)
			*pdwLastError = ::GetLastError();
		return false;
	}

	rContext.strVolumeGuidPath.assign(volumeGuidBuffer.data());
	rContext.strVolumeGuidPath = NormalizeAbsolutePathSeparators(rContext.strVolumeGuidPath.c_str());
	EnsureTrailingSeparator(rContext.strVolumeGuidPath);
	rContext.strVolumeKey = NormalizeVolumeGuidPathForKey(rContext.strVolumeGuidPath);

	DWORD dwVolumeSerialNumber = 0;
	TCHAR szFileSystemName[MAX_PATH] = {};
	if (!::GetVolumeInformation(
			rContext.strMountRoot.c_str(),
			NULL,
			0,
			&dwVolumeSerialNumber,
			&rContext.dwMaximumComponentLength,
			&rContext.dwFileSystemFlags,
			szFileSystemName,
			_countof(szFileSystemName)))
	{
		if (pdwLastError != NULL)
			*pdwLastError = ::GetLastError();
		return false;
	}

	rContext.ullVolumeSerialNumber = dwVolumeSerialNumber;
	rContext.strFileSystemName.assign(szFileSystemName);
	rContext.bIsLocal = true;
	rContext.bIsNtfs = (_tcsicmp(rContext.strFileSystemName.c_str(), _T("NTFS")) == 0);
	rContext.bSupportsJournal = rContext.bIsNtfs && !rContext.strVolumeGuidPath.empty();
	return true;
}

/**
 * @brief Opens the resolved local volume for NTFS journal queries through its stable volume GUID path.
 */
inline HANDLE OpenResolvedVolumeHandleForJournalQuery(const ResolvedVolumeContext &rContext, DWORD *pdwLastError = NULL)
{
	if (pdwLastError != NULL)
		*pdwLastError = ERROR_SUCCESS;
	if (rContext.strVolumeGuidPath.empty()) {
		if (pdwLastError != NULL)
			*pdwLastError = ERROR_INVALID_PARAMETER;
		return INVALID_HANDLE_VALUE;
	}

	PathString volumePath(rContext.strVolumeGuidPath);
	while (!volumePath.empty() && IsPathSeparator(volumePath[volumePath.size() - 1]))
		volumePath.erase(volumePath.size() - 1);
	HANDLE hVolume = ::CreateFile(volumePath.c_str(),
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL);
	if (hVolume == INVALID_HANDLE_VALUE && pdwLastError != NULL)
		*pdwLastError = ::GetLastError();
	return hVolume;
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
	return detail::GetPreparedPathMemoRecord(pszPath).bRequiresExactNamePrefix;
}

/**
 * @brief Applies the extended-length prefix to fully qualified overlong or exact-name DOS/UNC paths.
 */
inline PathString PreparePathForLongPath(LPCTSTR pszPath)
{
	return detail::GetPreparedPathMemoRecord(pszPath).strPreparedPath;
}

/**
 * @brief Prepares directory-create paths early enough to avoid the legacy `MAX_PATH - 12` limit or preserve exact namespace-only names.
 */
inline PathString PrepareDirectoryCreatePathForLongPath(LPCTSTR pszPath)
{
	return detail::GetPreparedPathMemoRecord(pszPath).strPreparedCreateDirectoryPath;
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
 * @brief Reports whether the target path exists and is a directory.
 */
inline bool DirectoryExists(LPCTSTR pszPath)
{
	const DWORD dwAttributes = GetFileAttributes(pszPath);
	return dwAttributes != INVALID_FILE_ATTRIBUTES && (dwAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

/**
 * @brief Returns the non-creatable root prefix length for absolute DOS, UNC, and extended-length directory paths.
 */
inline size_t GetAbsoluteDirectoryRootLength(const PathString &rPath)
{
	if (rPath.size() >= 8u && _tcsnicmp(rPath.c_str(), _T("\\\\?\\UNC\\"), 8) == 0) {
		size_t iIndex = 8u;
		while (iIndex < rPath.size() && !IsPathSeparator(rPath[iIndex]))
			++iIndex;
		while (iIndex < rPath.size() && IsPathSeparator(rPath[iIndex]))
			++iIndex;
		while (iIndex < rPath.size() && !IsPathSeparator(rPath[iIndex]))
			++iIndex;
		while (iIndex < rPath.size() && IsPathSeparator(rPath[iIndex]))
			++iIndex;
		return iIndex;
	}

	if (rPath.size() >= 7u
		&& _tcsnicmp(rPath.c_str(), _T("\\\\?\\"), 4) == 0
		&& rPath[5] == _T(':')
		&& IsPathSeparator(rPath[6]))
	{
		return 7u;
	}

	if (rPath.size() >= 3u
		&& ((rPath[0] >= _T('A') && rPath[0] <= _T('Z')) || (rPath[0] >= _T('a') && rPath[0] <= _T('z')))
		&& rPath[1] == _T(':')
		&& IsPathSeparator(rPath[2]))
	{
		return 3u;
	}

	if (rPath.size() >= 2u && rPath[0] == _T('\\') && rPath[1] == _T('\\')) {
		size_t iIndex = 2u;
		while (iIndex < rPath.size() && !IsPathSeparator(rPath[iIndex]))
			++iIndex;
		while (iIndex < rPath.size() && IsPathSeparator(rPath[iIndex]))
			++iIndex;
		while (iIndex < rPath.size() && !IsPathSeparator(rPath[iIndex]))
			++iIndex;
		while (iIndex < rPath.size() && IsPathSeparator(rPath[iIndex]))
			++iIndex;
		return iIndex;
	}

	if (!rPath.empty() && IsPathSeparator(rPath[0]))
		return 1u;

	return 0u;
}

/**
 * @brief Removes trailing directory separators without stripping the filesystem root.
 */
inline PathString TrimDirectoryTrailingSeparators(const PathString &rPath, const size_t uRootLength)
{
	PathString path(rPath);
	while (path.size() > uRootLength && IsPathSeparator(path[path.size() - 1u]))
		path.resize(path.size() - 1u);
	return path;
}

/**
 * @brief Creates every missing directory segment with the same long-path preparation rules as `CreateDirectory`.
 */
inline bool CreateDirectoryPath(LPCTSTR pszPath, LPSECURITY_ATTRIBUTES pSecurityAttributes = NULL)
{
	if (pszPath == NULL || pszPath[0] == _T('\0')) {
		::SetLastError(ERROR_PATH_NOT_FOUND);
		return false;
	}

	const PathString strNormalized(NormalizeAbsolutePathSeparators(pszPath));
	const size_t uRootLength = GetAbsoluteDirectoryRootLength(strNormalized);
	const PathString strDirectory(TrimDirectoryTrailingSeparators(strNormalized, uRootLength));
	if (strDirectory.empty()) {
		::SetLastError(ERROR_PATH_NOT_FOUND);
		return false;
	}

	if (DirectoryExists(strDirectory.c_str()))
		return true;

	if (uRootLength > 0u) {
		const size_t uClampedRootLength = uRootLength < strDirectory.size() ? uRootLength : strDirectory.size();
		const PathString strRoot(strDirectory.substr(0u, uClampedRootLength));
		if (!DirectoryExists(strRoot.c_str())) {
			::SetLastError(ERROR_PATH_NOT_FOUND);
			return false;
		}
	}

	for (size_t i = uRootLength; i < strDirectory.size(); ++i) {
		if (!IsPathSeparator(strDirectory[i]))
			continue;

		const PathString strCurrent(TrimDirectoryTrailingSeparators(strDirectory.substr(0u, i), uRootLength));
		if (strCurrent.size() <= uRootLength || DirectoryExists(strCurrent.c_str()))
			continue;

		if (::CreateDirectory(PrepareDirectoryCreatePathForLongPath(strCurrent.c_str()).c_str(), pSecurityAttributes) == FALSE) {
			const DWORD dwError = ::GetLastError();
			if (dwError != ERROR_ALREADY_EXISTS || !DirectoryExists(strCurrent.c_str())) {
				::SetLastError(dwError);
				return false;
			}
		}
	}

	if (::CreateDirectory(PrepareDirectoryCreatePathForLongPath(strDirectory.c_str()).c_str(), pSecurityAttributes) != FALSE)
		return true;

	const DWORD dwError = ::GetLastError();
	if (dwError == ERROR_ALREADY_EXISTS && DirectoryExists(strDirectory.c_str()))
		return true;
	::SetLastError(dwError);
	return false;
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
 * @brief Queries the local NTFS journal guard for the volume that owns the path.
 */
inline bool TryGetLocalNtfsJournalVolumeState(LPCTSTR pszPath, NtfsJournalVolumeState &rState, DWORD *pdwLastError = NULL)
{
	rState = NtfsJournalVolumeState{};
	if (pdwLastError != NULL)
		*pdwLastError = ERROR_SUCCESS;
	if (pszPath == NULL || pszPath[0] == _T('\0')) {
		if (pdwLastError != NULL)
			*pdwLastError = ERROR_INVALID_PARAMETER;
		return false;
	}

	ResolvedVolumeContext volumeContext = {};
	if (!TryResolveContainingVolumeContext(pszPath, volumeContext, pdwLastError))
		return false;
	if (!volumeContext.bIsLocal || !volumeContext.bIsNtfs || !volumeContext.bSupportsJournal) {
		if (pdwLastError != NULL)
			*pdwLastError = ERROR_NOT_SUPPORTED;
		return false;
	}

	HANDLE hVolume = OpenResolvedVolumeHandleForJournalQuery(volumeContext, pdwLastError);
	if (hVolume == INVALID_HANDLE_VALUE)
		return false;

	USN_JOURNAL_DATA_V0 journalData = {};
	DWORD dwBytesReturned = 0;
	const BOOL bOk = ::DeviceIoControl(
		hVolume,
		FSCTL_QUERY_USN_JOURNAL,
		NULL,
		0,
		&journalData,
		sizeof(journalData),
		&dwBytesReturned,
		NULL);
	const DWORD dwQueryError = bOk != FALSE ? ERROR_SUCCESS : ::GetLastError();
	const DWORD dwCloseError = (::CloseHandle(hVolume) != FALSE) ? ERROR_SUCCESS : ::GetLastError();
	if (bOk == FALSE) {
		if (pdwLastError != NULL)
			*pdwLastError = dwQueryError;
		return false;
	}
	if (dwCloseError != ERROR_SUCCESS) {
		if (pdwLastError != NULL)
			*pdwLastError = dwCloseError;
		return false;
	}

	rState.strVolumeKey = volumeContext.strVolumeKey;
	rState.strMountRoot = volumeContext.strMountRoot;
	rState.strVolumeGuidPath = volumeContext.strVolumeGuidPath;
	rState.ullVolumeSerialNumber = volumeContext.ullVolumeSerialNumber;
	rState.ullUsnJournalId = journalData.UsnJournalID;
	rState.llLowestValidUsn = journalData.LowestValidUsn;
	rState.llNextUsn = journalData.NextUsn;
	return true;
}

/**
 * @brief Queries the NTFS file reference number for an existing directory.
 */
inline bool TryGetNtfsDirectoryJournalState(LPCTSTR pszPath, NtfsDirectoryJournalState &rState, DWORD *pdwLastError = NULL)
{
	rState = NtfsDirectoryJournalState{};
	if (pdwLastError != NULL)
		*pdwLastError = ERROR_SUCCESS;
	if (pszPath == NULL || pszPath[0] == _T('\0')) {
		if (pdwLastError != NULL)
			*pdwLastError = ERROR_INVALID_PARAMETER;
		return false;
	}

	HANDLE hDirectory = ::CreateFile(
		PreparePathForLongPath(pszPath).c_str(),
		FILE_READ_ATTRIBUTES,
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

	READ_FILE_USN_DATA input = {};
	input.MinMajorVersion = 2;
	input.MaxMajorVersion = 4;
	std::array<BYTE, 4096u> buffer = {};
	DWORD dwBytesReturned = 0;
	const BOOL bOk = ::DeviceIoControl(
		hDirectory,
		FSCTL_READ_FILE_USN_DATA,
		&input,
		sizeof(input),
		buffer.data(),
		static_cast<DWORD>(buffer.size()),
		&dwBytesReturned,
		NULL);
	const DWORD dwReadError = bOk != FALSE ? ERROR_SUCCESS : ::GetLastError();
	const DWORD dwCloseError = (::CloseHandle(hDirectory) != FALSE) ? ERROR_SUCCESS : ::GetLastError();
	if (bOk == FALSE || dwBytesReturned < sizeof(USN_RECORD_COMMON_HEADER)) {
		if (pdwLastError != NULL)
			*pdwLastError = (bOk == FALSE) ? dwReadError : ERROR_INVALID_DATA;
		return false;
	}

	const USN_RECORD_COMMON_HEADER *pHeader = reinterpret_cast<const USN_RECORD_COMMON_HEADER *>(buffer.data());
	if (!TryParseUsnRecordIdentity(pHeader, dwBytesReturned, rState.fileReference, NULL, &rState.llUsn, pdwLastError))
		return false;
	if (dwCloseError != ERROR_SUCCESS) {
		if (pdwLastError != NULL)
			*pdwLastError = dwCloseError;
		return false;
	}
	return true;
}

/**
 * @brief Scans the local NTFS journal delta once and reports cached directories touched since the checkpoint.
 */
inline bool TryCollectChangedDirectoryFileReferences(LPCTSTR pszPath, const ULONGLONG ullExpectedJournalId, const LONGLONG llCheckpointUsn, const std::unordered_set<UsnFileReference, UsnFileReferenceHasher> &rTrackedDirectoryFileReferences, std::unordered_set<UsnFileReference, UsnFileReferenceHasher> &rChangedDirectoryFileReferences, DWORD *pdwLastError = NULL)
{
	rChangedDirectoryFileReferences.clear();
	if (pdwLastError != NULL)
		*pdwLastError = ERROR_SUCCESS;
	if (pszPath == NULL || pszPath[0] == _T('\0') || ullExpectedJournalId == 0 || llCheckpointUsn <= 0) {
		if (pdwLastError != NULL)
			*pdwLastError = ERROR_INVALID_PARAMETER;
		return false;
	}
	if (rTrackedDirectoryFileReferences.empty())
		return true;

	NtfsJournalVolumeState volumeState = {};
	if (!TryGetLocalNtfsJournalVolumeState(pszPath, volumeState, pdwLastError))
		return false;
	if (volumeState.ullUsnJournalId != ullExpectedJournalId || llCheckpointUsn < volumeState.llLowestValidUsn || llCheckpointUsn > volumeState.llNextUsn) {
		if (pdwLastError != NULL)
			*pdwLastError = ERROR_JOURNAL_DELETE_IN_PROGRESS;
		return false;
	}
	if (llCheckpointUsn == volumeState.llNextUsn)
		return true;

	ResolvedVolumeContext volumeContext = {};
	if (!TryResolveContainingVolumeContext(pszPath, volumeContext, pdwLastError))
		return false;
	if (NormalizeVolumeGuidPathForKey(volumeContext.strVolumeGuidPath) != NormalizeVolumeGuidPathForKey(volumeState.strVolumeGuidPath)) {
		if (pdwLastError != NULL)
			*pdwLastError = ERROR_NOT_SUPPORTED;
		return false;
	}

	HANDLE hVolume = OpenResolvedVolumeHandleForJournalQuery(volumeContext, pdwLastError);
	if (hVolume == INVALID_HANDLE_VALUE)
		return false;

	std::vector<BYTE> buffer(64u * 1024u, 0);
	READ_USN_JOURNAL_DATA_V1 input = {};
	input.StartUsn = llCheckpointUsn;
	input.ReasonMask = 0xFFFFFFFFu;
	input.ReturnOnlyOnClose = FALSE;
	input.Timeout = 0;
	input.BytesToWaitFor = 0;
	input.UsnJournalID = ullExpectedJournalId;
	input.MinMajorVersion = 2;
	input.MaxMajorVersion = 4;

	bool bOk = true;
	DWORD dwError = ERROR_SUCCESS;
	while (input.StartUsn < volumeState.llNextUsn && rChangedDirectoryFileReferences.size() < rTrackedDirectoryFileReferences.size()) {
		DWORD dwBytesReturned = 0;
		if (::DeviceIoControl(
			hVolume,
			FSCTL_READ_USN_JOURNAL,
			&input,
			sizeof(input),
			buffer.data(),
			static_cast<DWORD>(buffer.size()),
			&dwBytesReturned,
			NULL) == FALSE)
		{
			bOk = false;
			dwError = ::GetLastError();
			break;
		}
		if (dwBytesReturned < sizeof(USN))
			break;

		const BYTE *pCursor = buffer.data() + sizeof(USN);
		const BYTE *pEnd = buffer.data() + dwBytesReturned;
		while (pCursor + sizeof(USN_RECORD_COMMON_HEADER) <= pEnd) {
			const USN_RECORD_COMMON_HEADER *pHeader = reinterpret_cast<const USN_RECORD_COMMON_HEADER *>(pCursor);
			if (pHeader->RecordLength < sizeof(USN_RECORD_COMMON_HEADER) || pCursor + pHeader->RecordLength > pEnd) {
				bOk = false;
				dwError = ERROR_INVALID_DATA;
				break;
			}
			UsnFileReference fileReference = {};
			UsnFileReference parentFileReference = {};
			if (!TryParseUsnRecordIdentity(pHeader, pHeader->RecordLength, fileReference, &parentFileReference, NULL, &dwError)) {
				bOk = false;
				break;
			}

			const auto itFileReference = rTrackedDirectoryFileReferences.find(fileReference);
			if (itFileReference != rTrackedDirectoryFileReferences.end())
				rChangedDirectoryFileReferences.insert(*itFileReference);
			const auto itParentReference = rTrackedDirectoryFileReferences.find(parentFileReference);
			if (itParentReference != rTrackedDirectoryFileReferences.end())
				rChangedDirectoryFileReferences.insert(*itParentReference);

			pCursor += pHeader->RecordLength;
		}
		if (!bOk)
			break;

		input.StartUsn = *reinterpret_cast<const USN *>(buffer.data());
		if (input.StartUsn <= llCheckpointUsn) {
			bOk = false;
			dwError = ERROR_INVALID_DATA;
			break;
		}
	}

	const DWORD dwCloseError = (::CloseHandle(hVolume) != FALSE) ? ERROR_SUCCESS : ::GetLastError();
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
#pragma warning(push)
#pragma warning(disable:5039)
	return ::MoveFileWithProgress(PreparePathForLongPath(pszExistingPath).c_str(), PreparePathForLongPath(pszNewPath).c_str(), pProgressRoutine, pData, dwFlags);
#pragma warning(pop)
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
 * @brief Reserves a unique temp-file path inside a target directory through the shared long-path-aware Win32 wrapper.
 */
inline bool CreateUniqueTempFilePath(LPCTSTR pszDirectory, LPCTSTR pszPrefix, PathString &rTempPath)
{
	rTempPath.clear();
	if (pszDirectory == NULL || *pszDirectory == _T('\0'))
		return false;

	PathString strDirectory = GetNormalizedPathText(pszDirectory);
	if (strDirectory.empty())
		return false;
	if (!IsPathSeparator(strDirectory[strDirectory.size() - 1]))
		strDirectory.push_back(_T('\\'));

	PathString strPrefix = GetLogicalPathText((pszPrefix != NULL && *pszPrefix != _T('\0')) ? pszPrefix : _T("tmp"));
	static std::atomic<ULONGLONG> s_ullNextTempNonce(::GetTickCount64());
	for (UINT uAttempt = 0; uAttempt < 1024; ++uAttempt) {
		const ULONGLONG ullNonce = s_ullNextTempNonce.fetch_add(1, std::memory_order_relaxed);
		TCHAR szTempLeaf[64] = {};
		_sntprintf_s(szTempLeaf, _countof(szTempLeaf), _TRUNCATE, _T("%.16s%08llX.tmp"), strPrefix.c_str(), ullNonce);

		PathString strCandidate(strDirectory);
		strCandidate += szTempLeaf;
		HANDLE hTempFile = CreateFile(strCandidate.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, NULL);
		if (hTempFile != INVALID_HANDLE_VALUE) {
			const DWORD dwCloseError = (::CloseHandle(hTempFile) != FALSE) ? ERROR_SUCCESS : ::GetLastError();
			if (!DeleteFileIfExists(strCandidate.c_str())) {
				if (dwCloseError != ERROR_SUCCESS)
					::SetLastError(dwCloseError);
				return false;
			}
			if (dwCloseError != ERROR_SUCCESS) {
				::SetLastError(dwCloseError);
				return false;
			}
			rTempPath = strCandidate;
			return true;
		}

		const DWORD dwLastError = ::GetLastError();
		if (dwLastError != ERROR_FILE_EXISTS && dwLastError != ERROR_ALREADY_EXISTS)
			return false;
	}

	::SetLastError(ERROR_ALREADY_EXISTS);
	return false;
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
