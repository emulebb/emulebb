#pragma once

#include <array>
#include <cstdint>
#include "PathHelpers.h"

/**
 * \brief Policy and wire-format helpers for the shared duplicate-path startup cache.
 *
 * The cache is intentionally local and disposable. It remembers duplicate
 * shared-file paths strongly enough to skip redundant startup rehashing
 * without changing stock data files or promoting duplicates to first-class
 * shared entries.
 */
namespace SharedDuplicatePathCachePolicy
{
constexpr std::uint32_t kMagic = 0x50554453u; // 'SDUP'
constexpr std::uint16_t kVersion = 1;

/**
 * \brief Returns the config-directory sidecar name for shared duplicate-path state.
 */
inline const wchar_t* GetFileName() noexcept
{
	return L"shareddups.dat";
}

/**
 * \brief Builds the case-insensitive in-memory lookup key for a duplicate shared-file path.
 *
 * Duplicate records keep their validated path spelling on disk. This lookup key
 * is intentionally lexical so startup cache map operations do not hit the
 * filesystem just to find an already-loaded sidecar record.
 */
inline CString MakePathLookupKey(const CString &rstrFilePath)
{
	CString strKey(PathHelpers::CanonicalizePath(PathHelpers::StripExtendedLengthPrefix(rstrFilePath)));
	strKey.MakeLower();
	return strKey;
}

/**
 * \brief Remembers one duplicate shared-file path bound to the canonical MD4 identity.
 */
struct PathRecord
{
	CString strFilePath;
	std::int64_t utcFileDate = -1;
	std::uint64_t ullFileSize = 0;
	std::array<std::uint8_t, 16> canonicalFileHash = {};
};

/**
 * \brief Checks one cached duplicate-path record for cheap structural sanity.
 */
inline bool IsStructurallyValid(const PathRecord &rRecord) noexcept
{
	return !rRecord.strFilePath.IsEmpty()
		&& rRecord.utcFileDate > 0
		&& rRecord.ullFileSize > 0;
}
}
