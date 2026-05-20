#pragma once

#include <afxtempl.h>

#include "PathHelpers.h"
#include "SharedFileIntakePolicy.h"

namespace SharedDirectoryOps
{
inline bool ContainsDirectoryIdentity(const std::vector<LongPathSeams::FileSystemObjectIdentity> &rVisitedDirectories, const LongPathSeams::FileSystemObjectIdentity &rIdentity)
{
	for (size_t i = 0; i < rVisitedDirectories.size(); ++i) {
		if (rVisitedDirectories[i] == rIdentity)
			return true;
	}
	return false;
}

/**
 * @brief Builds a case-insensitive lexical key for shared-directory list checks.
 */
inline CString MakeSharedDirectoryLookupKey(const CString &rstrDirectory)
{
	CString strKey(PathHelpers::EnsureTrailingSeparator(PathHelpers::CanonicalizePath(PathHelpers::StripExtendedLengthPrefix(rstrDirectory))));
	strKey.MakeLower();
	return strKey;
}

inline bool ListContainsEquivalentPath(const CStringList &rList, const CString &rstrPath)
{
	const CString strPathKey(MakeSharedDirectoryLookupKey(rstrPath));
	for (POSITION pos = rList.GetHeadPosition(); pos != NULL;) {
		if (MakeSharedDirectoryLookupKey(rList.GetNext(pos)) == strPathKey)
			return true;
	}
	return false;
}

inline bool IsSharedDirectoryListed(const CStringList &rList, const CString &rstrPath)
{
	return ListContainsEquivalentPath(rList, rstrPath);
}

inline bool ListContainsEquivalentDirectoryObject(const CStringList &rList, const CString &rstrPath, const LongPathSeams::FileSystemObjectIdentity *pIdentity = NULL)
{
	if (ListContainsEquivalentPath(rList, rstrPath))
		return true;

	LongPathSeams::FileSystemObjectIdentity targetIdentity = {};
	if (pIdentity != NULL) {
		targetIdentity = *pIdentity;
	} else if (!LongPathSeams::TryGetResolvedDirectoryIdentity(rstrPath, targetIdentity)) {
		return false;
	}

	for (POSITION pos = rList.GetHeadPosition(); pos != NULL;) {
		const CString strCurrent(rList.GetNext(pos));
		LongPathSeams::FileSystemObjectIdentity currentIdentity = {};
		if (LongPathSeams::TryGetResolvedDirectoryIdentity(strCurrent, currentIdentity) && currentIdentity == targetIdentity)
			return true;
	}

	return false;
}

/**
 * @brief Returns true when a candidate key is below the directory key, excluding the directory itself.
 */
inline bool IsDirectoryKeyParentOfCandidate(const CString &rstrDirectoryKey, const CString &rstrCandidateKey)
{
	return rstrCandidateKey.GetLength() > rstrDirectoryKey.GetLength()
		&& _tcsncmp(rstrDirectoryKey, rstrCandidateKey, rstrDirectoryKey.GetLength()) == 0;
}

inline bool HasSharedSubdirectory(const CStringList &rList, const CString &rstrDirectory)
{
	const CString strDirectoryKey(MakeSharedDirectoryLookupKey(rstrDirectory));
	for (POSITION pos = rList.GetHeadPosition(); pos != NULL;) {
		if (IsDirectoryKeyParentOfCandidate(strDirectoryKey, MakeSharedDirectoryLookupKey(rList.GetNext(pos))))
			return true;
	}
	return false;
}

/**
 * @brief Resolves the stable local volume key for a directory, including mounted-folder volumes.
 */
inline bool TryResolveDirectoryVolumeKey(const CString &rstrDirectory, CString &rstrVolumeKey)
{
	rstrVolumeKey.Empty();
	LongPathSeams::ResolvedVolumeContext volumeContext = {};
	if (!LongPathSeams::TryResolveContainingVolumeContext(PathHelpers::TrimTrailingSeparator(rstrDirectory), volumeContext))
		return false;
	rstrVolumeKey = CString(volumeContext.strVolumeKey.c_str());
	return !rstrVolumeKey.IsEmpty();
}

inline bool EnumerateChildDirectories(const CString &rstrDirectory, CStringList &rChildNames)
{
	rChildNames.RemoveAll();
	DWORD dwError = ERROR_SUCCESS;
	const bool bEnumerated = PathHelpers::ForEachDirectoryEntry(rstrDirectory, [&](const WIN32_FIND_DATA &findData) -> bool {
		if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0
			&& (findData.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) == 0
			&& !SharedFileIntakePolicy::ShouldIgnoreDirectoryByName(findData.cFileName))
		{
			rChildNames.AddTail(findData.cFileName);
		}
		return true;
	}, &dwError);

	return bEnumerated || dwError == ERROR_FILE_NOT_FOUND || dwError == ERROR_PATH_NOT_FOUND;
}

template <typename IsShareableDirectoryFn>
inline bool AddSharedDirectoryImpl(CStringList &rList, const CString &rstrDirectory, const bool bIncludeSubdirectories, IsShareableDirectoryFn isShareableDirectoryFn, std::vector<LongPathSeams::FileSystemObjectIdentity> &rVisitedDirectories)
{
	const CString strCanonicalDirectory(PathHelpers::CanonicalizeDirectoryPath(rstrDirectory));
	LongPathSeams::FileSystemObjectIdentity directoryIdentity = {};
	const bool bHasDirectoryIdentity = LongPathSeams::TryGetResolvedDirectoryIdentity(strCanonicalDirectory, directoryIdentity);
	if (bHasDirectoryIdentity) {
		if (ContainsDirectoryIdentity(rVisitedDirectories, directoryIdentity))
			return false;
		rVisitedDirectories.push_back(directoryIdentity);
	}

	bool bChanged = false;

	if (isShareableDirectoryFn(strCanonicalDirectory)
		&& !ListContainsEquivalentDirectoryObject(rList, strCanonicalDirectory, bHasDirectoryIdentity ? &directoryIdentity : NULL))
	{
		rList.AddTail(strCanonicalDirectory);
		bChanged = true;
	}

	if (!bIncludeSubdirectories)
		return bChanged;

	CStringList childNames;
	if (!EnumerateChildDirectories(strCanonicalDirectory, childNames))
		return bChanged;

	for (POSITION pos = childNames.GetHeadPosition(); pos != NULL;) {
		const CString strChildPath(PathHelpers::AppendPathComponent(strCanonicalDirectory, childNames.GetNext(pos)));
		if (AddSharedDirectoryImpl(rList, strChildPath, true, isShareableDirectoryFn, rVisitedDirectories))
			bChanged = true;
	}
	return bChanged;
}

template <typename IsShareableDirectoryFn>
inline bool AddSharedDirectory(CStringList &rList, const CString &rstrDirectory, const bool bIncludeSubdirectories, IsShareableDirectoryFn isShareableDirectoryFn)
{
	std::vector<LongPathSeams::FileSystemObjectIdentity> visitedDirectories;
	return AddSharedDirectoryImpl(rList, rstrDirectory, bIncludeSubdirectories, isShareableDirectoryFn, visitedDirectories);
}

template <typename IsShareableDirectoryFn>
inline void CollectDirectorySubtreeImpl(CStringList &rList, const CString &rstrDirectory, const bool bIncludeRoot, IsShareableDirectoryFn isShareableDirectoryFn, std::vector<LongPathSeams::FileSystemObjectIdentity> &rVisitedDirectories)
{
	const CString strCanonicalDirectory(PathHelpers::CanonicalizeDirectoryPath(rstrDirectory));
	LongPathSeams::FileSystemObjectIdentity directoryIdentity = {};
	const bool bHasDirectoryIdentity = LongPathSeams::TryGetResolvedDirectoryIdentity(strCanonicalDirectory, directoryIdentity);
	if (bHasDirectoryIdentity) {
		if (ContainsDirectoryIdentity(rVisitedDirectories, directoryIdentity))
			return;
		rVisitedDirectories.push_back(directoryIdentity);
	}

	if (bIncludeRoot && isShareableDirectoryFn(strCanonicalDirectory) && !ListContainsEquivalentDirectoryObject(rList, strCanonicalDirectory, bHasDirectoryIdentity ? &directoryIdentity : NULL))
		rList.AddTail(strCanonicalDirectory);

	CStringList childNames;
	if (!EnumerateChildDirectories(strCanonicalDirectory, childNames))
		return;

	for (POSITION pos = childNames.GetHeadPosition(); pos != NULL;) {
		const CString strChildPath(PathHelpers::AppendPathComponent(strCanonicalDirectory, childNames.GetNext(pos)));
		CollectDirectorySubtreeImpl(rList, strChildPath, true, isShareableDirectoryFn, rVisitedDirectories);
	}
}

template <typename IsShareableDirectoryFn>
inline void CollectDirectorySubtree(CStringList &rList, const CString &rstrDirectory, const bool bIncludeRoot, IsShareableDirectoryFn isShareableDirectoryFn)
{
	std::vector<LongPathSeams::FileSystemObjectIdentity> visitedDirectories;
	CollectDirectorySubtreeImpl(rList, rstrDirectory, bIncludeRoot, isShareableDirectoryFn, visitedDirectories);
}

template <typename IsShareableDirectoryFn, typename ResolveVolumeKeyFn>
inline void CollectMonitoredSharedRootStateImpl(
	CStringList &rMonitoredRoots,
	CStringList &rMonitorOwnedDirs,
	const CString &rstrDirectory,
	const bool bIncludeRootInOwnedDirs,
	const CString &rstrRootVolumeKey,
	const bool bRootVolumeResolved,
	IsShareableDirectoryFn isShareableDirectoryFn,
	ResolveVolumeKeyFn resolveVolumeKeyFn,
	std::vector<LongPathSeams::FileSystemObjectIdentity> &rVisitedDirectories)
{
	const CString strCanonicalDirectory(PathHelpers::CanonicalizeDirectoryPath(rstrDirectory));
	LongPathSeams::FileSystemObjectIdentity directoryIdentity = {};
	const bool bHasDirectoryIdentity = LongPathSeams::TryGetResolvedDirectoryIdentity(strCanonicalDirectory, directoryIdentity);
	if (bHasDirectoryIdentity) {
		if (ContainsDirectoryIdentity(rVisitedDirectories, directoryIdentity))
			return;
		rVisitedDirectories.push_back(directoryIdentity);
	}

	CString strDirectoryVolumeKey;
	const bool bDirectoryVolumeResolved = resolveVolumeKeyFn(strCanonicalDirectory, strDirectoryVolumeKey);
	if (bIncludeRootInOwnedDirs
		&& bRootVolumeResolved
		&& bDirectoryVolumeResolved
		&& strDirectoryVolumeKey.CompareNoCase(rstrRootVolumeKey) != 0)
	{
		if (isShareableDirectoryFn(strCanonicalDirectory)
			&& !ListContainsEquivalentDirectoryObject(rMonitoredRoots, strCanonicalDirectory, bHasDirectoryIdentity ? &directoryIdentity : NULL))
		{
			rMonitoredRoots.AddTail(strCanonicalDirectory);
		}
		if (isShareableDirectoryFn(strCanonicalDirectory)
			&& !ListContainsEquivalentDirectoryObject(rMonitorOwnedDirs, strCanonicalDirectory, bHasDirectoryIdentity ? &directoryIdentity : NULL))
		{
			rMonitorOwnedDirs.AddTail(strCanonicalDirectory);
		}

		CStringList childNames;
		if (!EnumerateChildDirectories(strCanonicalDirectory, childNames))
			return;
		for (POSITION pos = childNames.GetHeadPosition(); pos != NULL;) {
			const CString strChildPath(PathHelpers::AppendPathComponent(strCanonicalDirectory, childNames.GetNext(pos)));
			CollectMonitoredSharedRootStateImpl(
				rMonitoredRoots,
				rMonitorOwnedDirs,
				strChildPath,
				true,
				strDirectoryVolumeKey,
				true,
				isShareableDirectoryFn,
				resolveVolumeKeyFn,
				rVisitedDirectories);
		}
		return;
	}

	if (bIncludeRootInOwnedDirs
		&& isShareableDirectoryFn(strCanonicalDirectory)
		&& !ListContainsEquivalentDirectoryObject(rMonitorOwnedDirs, strCanonicalDirectory, bHasDirectoryIdentity ? &directoryIdentity : NULL))
	{
		rMonitorOwnedDirs.AddTail(strCanonicalDirectory);
	}

	CStringList childNames;
	if (!EnumerateChildDirectories(strCanonicalDirectory, childNames))
		return;

	const CString strEffectiveRootVolumeKey(bDirectoryVolumeResolved ? strDirectoryVolumeKey : rstrRootVolumeKey);
	const bool bEffectiveRootVolumeResolved = bDirectoryVolumeResolved || bRootVolumeResolved;
	for (POSITION pos = childNames.GetHeadPosition(); pos != NULL;) {
		const CString strChildPath(PathHelpers::AppendPathComponent(strCanonicalDirectory, childNames.GetNext(pos)));
		CollectMonitoredSharedRootStateImpl(
			rMonitoredRoots,
			rMonitorOwnedDirs,
			strChildPath,
			true,
			strEffectiveRootVolumeKey,
			bEffectiveRootVolumeResolved,
			isShareableDirectoryFn,
			resolveVolumeKeyFn,
			rVisitedDirectories);
	}
}

/**
 * @brief Adds one recursive monitored root and promotes mounted-folder volume boundaries to separate monitored roots.
 */
template <typename IsShareableDirectoryFn, typename ResolveVolumeKeyFn>
inline void AddMonitoredSharedRoot(
	CStringList &rMonitoredRoots,
	CStringList &rMonitorOwnedDirs,
	const CString &rstrDirectory,
	IsShareableDirectoryFn isShareableDirectoryFn,
	ResolveVolumeKeyFn resolveVolumeKeyFn)
{
	const CString strCanonicalDirectory(PathHelpers::CanonicalizeDirectoryPath(rstrDirectory));
	CString strRootVolumeKey;
	const bool bRootVolumeResolved = resolveVolumeKeyFn(strCanonicalDirectory, strRootVolumeKey);
	if (!ListContainsEquivalentDirectoryObject(rMonitoredRoots, strCanonicalDirectory))
		rMonitoredRoots.AddTail(strCanonicalDirectory);

	std::vector<LongPathSeams::FileSystemObjectIdentity> visitedDirectories;
	CollectMonitoredSharedRootStateImpl(
		rMonitoredRoots,
		rMonitorOwnedDirs,
		strCanonicalDirectory,
		false,
		strRootVolumeKey,
		bRootVolumeResolved,
		isShareableDirectoryFn,
		resolveVolumeKeyFn,
		visitedDirectories);
}

/**
 * @brief Adds one recursive monitored root using the production Win32 volume resolver.
 */
template <typename IsShareableDirectoryFn>
inline void AddMonitoredSharedRoot(CStringList &rMonitoredRoots, CStringList &rMonitorOwnedDirs, const CString &rstrDirectory, IsShareableDirectoryFn isShareableDirectoryFn)
{
	AddMonitoredSharedRoot(rMonitoredRoots, rMonitorOwnedDirs, rstrDirectory, isShareableDirectoryFn, [](const CString &rstrCandidate, CString &rstrVolumeKey) -> bool {
		return TryResolveDirectoryVolumeKey(rstrCandidate, rstrVolumeKey);
	});
}

inline bool RemoveSharedDirectory(CStringList &rList, const CString &rstrDirectory, const bool bIncludeSubdirectories)
{
	const CString strCanonicalDirectory(PathHelpers::CanonicalizeDirectoryPath(rstrDirectory));
	const CString strDirectoryKey(MakeSharedDirectoryLookupKey(strCanonicalDirectory));
	bool bChanged = false;
	for (POSITION pos = rList.GetHeadPosition(); pos != NULL;) {
		const POSITION posCurrent = pos;
		const CString strCurrent(rList.GetNext(pos));
		const CString strCurrentKey(MakeSharedDirectoryLookupKey(strCurrent));
		const bool bSameDirectory = strCurrentKey == strDirectoryKey;
		const bool bMatches = bIncludeSubdirectories
			? (bSameDirectory || IsDirectoryKeyParentOfCandidate(strDirectoryKey, strCurrentKey))
			: bSameDirectory;
		if (!bMatches)
			continue;

		rList.RemoveAt(posCurrent);
		bChanged = true;
		if (!bIncludeSubdirectories)
			break;
	}
	return bChanged;
}

/**
 * @brief Reports whether a normalized directory key is equal to or below another normalized directory key.
 */
inline bool IsDirectoryKeySameOrDescendant(const CString &rstrRootKey, const CString &rstrCandidateKey)
{
	return rstrRootKey == rstrCandidateKey || IsDirectoryKeyParentOfCandidate(rstrRootKey, rstrCandidateKey);
}

/**
 * @brief Reports whether a retained monitored root is nested below a downgraded root.
 */
inline bool IsRetainedMonitoredRootProtectedByDowngradedRoot(const CString &rstrRetainedRootKey, const CStringList &rDowngradedRoots)
{
	for (POSITION pos = rDowngradedRoots.GetHeadPosition(); pos != NULL;) {
		if (IsDirectoryKeySameOrDescendant(MakeSharedDirectoryLookupKey(rDowngradedRoots.GetNext(pos)), rstrRetainedRootKey))
			return true;
	}
	return false;
}

/**
 * @brief Reports whether one monitor-owned directory still belongs to a retained promoted root.
 */
inline bool IsMonitorOwnedDirectoryProtectedByRetainedRoot(const CString &rstrOwnedKey, const CStringList &rRetainedMonitoredRoots, const CStringList &rDowngradedRoots)
{
	for (POSITION pos = rRetainedMonitoredRoots.GetHeadPosition(); pos != NULL;) {
		const CString strRetainedRootKey(MakeSharedDirectoryLookupKey(rRetainedMonitoredRoots.GetNext(pos)));
		if (IsDirectoryKeySameOrDescendant(strRetainedRootKey, rstrOwnedKey)
			&& IsRetainedMonitoredRootProtectedByDowngradedRoot(strRetainedRootKey, rDowngradedRoots))
		{
			return true;
		}
	}
	return false;
}

/**
 * @brief Reports whether one monitor-owned directory was covered by a downgraded root.
 */
inline bool IsMonitorOwnedDirectoryAffectedByDowngradedRoot(const CString &rstrOwnedKey, const CStringList &rDowngradedRoots)
{
	for (POSITION pos = rDowngradedRoots.GetHeadPosition(); pos != NULL;) {
		if (IsDirectoryKeySameOrDescendant(MakeSharedDirectoryLookupKey(rDowngradedRoots.GetNext(pos)), rstrOwnedKey))
			return true;
	}
	return false;
}

/**
 * @brief Removes monitor-owned directories lost by downgraded roots while preserving subtrees owned by retained promoted roots.
 */
inline bool RemoveMonitorOwnedDirectoriesForDowngradedRoots(CStringList &rMonitorOwnedDirs, const CStringList &rDowngradedRoots, const CStringList &rRetainedMonitoredRoots)
{
	bool bChanged = false;
	for (POSITION pos = rMonitorOwnedDirs.GetHeadPosition(); pos != NULL;) {
		const POSITION posCurrent = pos;
		const CString strOwnedKey(MakeSharedDirectoryLookupKey(rMonitorOwnedDirs.GetNext(pos)));
		if (!IsMonitorOwnedDirectoryAffectedByDowngradedRoot(strOwnedKey, rDowngradedRoots)
			|| IsMonitorOwnedDirectoryProtectedByRetainedRoot(strOwnedKey, rRetainedMonitoredRoots, rDowngradedRoots))
		{
			continue;
		}
		rMonitorOwnedDirs.RemoveAt(posCurrent);
		bChanged = true;
	}
	return bChanged;
}
}
