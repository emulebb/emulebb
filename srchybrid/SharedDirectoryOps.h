#pragma once

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

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

inline std::wstring MakeSharedDirectoryLookupKeyW(const CString &rstrDirectory)
{
	const CStringW strKey(MakeSharedDirectoryLookupKey(rstrDirectory));
	return std::wstring((LPCWSTR)strKey);
}

inline bool AreSharedDirectoryLookupKeysEqual(const CString &rstrLeft, const CString &rstrRight)
{
	return MakeSharedDirectoryLookupKey(rstrLeft) == MakeSharedDirectoryLookupKey(rstrRight);
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

struct SharedDirectoryResponseState
{
	std::unordered_set<std::wstring> sharedDirectoryKeys;
	std::unordered_set<std::wstring> emittedDirectoryKeys;
	std::unordered_set<std::wstring> emittedPseudoNameKeys;
	UINT uDuplicateDirectoryCount = 0;
	UINT uPseudoNameCollisionCount = 0;
	UINT uUnlistedDirectoryCount = 0;
};

/**
 * @brief Builds the exact lexical key used by shared-directory response generation.
 */
inline std::wstring MakeSharedDirectoryResponseLookupKey(const CString &rstrDirectory)
{
	return std::wstring(MakeSharedDirectoryLookupKey(rstrDirectory));
}

/**
 * @brief Builds the uniqueness key for one pseudo directory name.
 */
inline std::wstring MakeSharedDirectoryResponsePseudoNameKey(CString strPseudoName)
{
	return std::wstring(strPseudoName);
}

/**
 * @brief Adds one directory that may be advertised in a shared-directory response.
 */
inline void AddSharedDirectoryResponseRoot(SharedDirectoryResponseState &rState, const CString &rstrDirectory)
{
	if (rstrDirectory.IsEmpty())
		return;
	rState.sharedDirectoryKeys.insert(MakeSharedDirectoryResponseLookupKey(rstrDirectory));
}

/**
 * @brief Reports whether a directory is part of the request-local shared-directory response snapshot.
 */
inline bool IsSharedDirectoryResponseRootListed(const SharedDirectoryResponseState &rState, const CString &rstrDirectory)
{
	return rState.sharedDirectoryKeys.find(MakeSharedDirectoryResponseLookupKey(rstrDirectory)) != rState.sharedDirectoryKeys.end();
}

/**
 * @brief Generates one peer-visible pseudo-directory name without filesystem identity probes.
 */
inline CString BuildSharedDirectoryResponsePseudoName(SharedDirectoryResponseState &rState, const CString &rstrDirectory)
{
	if (!IsSharedDirectoryResponseRootListed(rState, rstrDirectory)) {
		++rState.uUnlistedDirectoryCount;
		return CString();
	}

	const std::wstring strDirectoryKey(MakeSharedDirectoryResponseLookupKey(rstrDirectory));
	if (!rState.emittedDirectoryKeys.insert(strDirectoryKey).second) {
		++rState.uDuplicateDirectoryCount;
		return CString();
	}

	CString strDirectoryTmp(PathHelpers::TrimTrailingSeparatorForLeaf(rstrDirectory));
	CString strPseudoName;
	int iPos = 0;
	while ((iPos = strDirectoryTmp.ReverseFind(_T('\\'))) >= 0) {
		strPseudoName = strDirectoryTmp.Right(strDirectoryTmp.GetLength() - iPos) + strPseudoName;
		strDirectoryTmp.Truncate(iPos);
		if (!IsSharedDirectoryResponseRootListed(rState, strDirectoryTmp))
			break;
	}
	if (strPseudoName.IsEmpty()) {
		strPseudoName = strDirectoryTmp;
	} else {
		if (strPseudoName[0] == _T('\\'))
			strPseudoName.Delete(0, 1);
	}

	if (!rState.emittedPseudoNameKeys.insert(MakeSharedDirectoryResponsePseudoNameKey(strPseudoName)).second) {
		CString strUnique;
		for (iPos = 2; ; ++iPos) {
			strUnique.Format(_T("%s_%i"), (LPCTSTR)strPseudoName, iPos);
			if (rState.emittedPseudoNameKeys.insert(MakeSharedDirectoryResponsePseudoNameKey(strUnique)).second) {
				strPseudoName = strUnique;
				++rState.uPseudoNameCollisionCount;
				break;
			}
			if (iPos > 200) {
				rState.emittedDirectoryKeys.erase(strDirectoryKey);
				++rState.uPseudoNameCollisionCount;
				return CString();
			}
		}
	}

	return strPseudoName;
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

inline bool IsDirectoryKeyParentOfCandidate(const std::wstring &rstrDirectoryKey, const std::wstring &rstrCandidateKey)
{
	return rstrCandidateKey.length() > rstrDirectoryKey.length()
		&& rstrCandidateKey.compare(0, rstrDirectoryKey.length(), rstrDirectoryKey) == 0;
}

inline bool RemoveEquivalentPath(CStringList &rList, const CString &rstrPath)
{
	const std::wstring strPathKey(MakeSharedDirectoryLookupKeyW(rstrPath));
	for (POSITION pos = rList.GetHeadPosition(); pos != NULL;) {
		const POSITION posCurrent = pos;
		if (MakeSharedDirectoryLookupKeyW(rList.GetNext(pos)) != strPathKey)
			continue;
		rList.RemoveAt(posCurrent);
		return true;
	}
	return false;
}

inline bool RemovePathsWithinDirectory(CStringList &rList, const CString &rstrPath, const bool bIncludeRoot)
{
	bool bChanged = false;
	const std::wstring strPathKey(MakeSharedDirectoryLookupKeyW(rstrPath));
	for (POSITION pos = rList.GetHeadPosition(); pos != NULL;) {
		const POSITION posCurrent = pos;
		const std::wstring strCurrentKey(MakeSharedDirectoryLookupKeyW(rList.GetNext(pos)));
		const bool bSamePath = strCurrentKey == strPathKey;
		if ((!bIncludeRoot && bSamePath)
			|| (!bSamePath && !IsDirectoryKeyParentOfCandidate(strPathKey, strCurrentKey)))
		{
			continue;
		}
		rList.RemoveAt(posCurrent);
		bChanged = true;
	}
	return bChanged;
}

struct SharedDirectoryRuleEntry
{
	CString strDirectory;
	std::wstring strKey;
	bool bHasIdentity = false;
	LongPathSeams::FileSystemObjectIdentity identity = {};
};

struct SharedDirectoryRuleIndex
{
	std::vector<SharedDirectoryRuleEntry> entries;
	std::vector<std::wstring> sortedKeys;
	std::unordered_set<std::wstring> exactKeys;

	void Clear()
	{
		entries.clear();
		sortedKeys.clear();
		exactKeys.clear();
	}

	void Rebuild(const CStringList &rDirectories, const bool bResolveIdentities = false)
	{
		Clear();
		entries.reserve(static_cast<size_t>(rDirectories.GetCount()));
		sortedKeys.reserve(static_cast<size_t>(rDirectories.GetCount()));
		exactKeys.reserve(static_cast<size_t>(rDirectories.GetCount()));
		for (POSITION pos = rDirectories.GetHeadPosition(); pos != NULL;) {
			SharedDirectoryRuleEntry entry;
			entry.strDirectory = PathHelpers::CanonicalizeDirectoryPath(rDirectories.GetNext(pos));
			entry.strKey = MakeSharedDirectoryLookupKeyW(entry.strDirectory);
			if (entry.strKey.empty() || exactKeys.find(entry.strKey) != exactKeys.end())
				continue;
			if (bResolveIdentities) {
				entry.bHasIdentity = LongPathSeams::TryGetResolvedDirectoryIdentity(entry.strDirectory, entry.identity);
				if (entry.bHasIdentity) {
					bool bDuplicateIdentity = false;
					for (size_t i = 0; i < entries.size(); ++i) {
						if (entries[i].bHasIdentity && entries[i].identity == entry.identity) {
							bDuplicateIdentity = true;
							break;
						}
					}
					// WHY: mounted folders and equivalent Win32 spellings can name
					// the same directory object with different text. Identity-aware
					// rule indexes must keep stock preference semantics and collapse
					// those duplicates before any tree or descendant lookup uses them.
					if (bDuplicateIdentity)
						continue;
				}
			}
			exactKeys.insert(entry.strKey);
			sortedKeys.push_back(entry.strKey);
			entries.push_back(entry);
		}
		std::sort(sortedKeys.begin(), sortedKeys.end());
		sortedKeys.erase(std::unique(sortedKeys.begin(), sortedKeys.end()), sortedKeys.end());
	}

	bool ContainsExactPathKey(const CString &rstrDirectory) const
	{
		return exactKeys.find(MakeSharedDirectoryLookupKeyW(rstrDirectory)) != exactKeys.end();
	}

	bool ContainsEquivalentDirectoryObject(const CString &rstrDirectory) const
	{
		if (ContainsExactPathKey(rstrDirectory))
			return true;

		LongPathSeams::FileSystemObjectIdentity targetIdentity = {};
		if (!LongPathSeams::TryGetResolvedDirectoryIdentity(rstrDirectory, targetIdentity))
			return false;

		for (size_t i = 0; i < entries.size(); ++i) {
			if (entries[i].bHasIdentity && entries[i].identity == targetIdentity)
				return true;
		}
		return false;
	}

	bool HasDescendant(const CString &rstrDirectory) const
	{
		if (sortedKeys.empty())
			return false;
		const std::wstring strDirectoryKey(MakeSharedDirectoryLookupKeyW(rstrDirectory));
		const std::vector<std::wstring>::const_iterator it = std::upper_bound(sortedKeys.begin(), sortedKeys.end(), strDirectoryKey);
		return it != sortedKeys.end() && IsDirectoryKeyParentOfCandidate(strDirectoryKey, *it);
	}

	bool IsSameOrDescendantOfAny(const CString &rstrDirectory) const
	{
		const std::wstring strDirectoryKey(MakeSharedDirectoryLookupKeyW(rstrDirectory));
		for (size_t i = 0; i < sortedKeys.size(); ++i) {
			if (sortedKeys[i] == strDirectoryKey || IsDirectoryKeyParentOfCandidate(sortedKeys[i], strDirectoryKey))
				return true;
		}
		return false;
	}
};

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
 * @brief Builds normalized shared-directory lookup keys once for repeated containment checks.
 */
inline void BuildSharedDirectoryLookupKeyVector(const CStringList &rDirectories, std::vector<CString> &rDirectoryKeys)
{
	rDirectoryKeys.clear();
	rDirectoryKeys.reserve(static_cast<size_t>(rDirectories.GetCount()));
	for (POSITION pos = rDirectories.GetHeadPosition(); pos != NULL;)
		rDirectoryKeys.push_back(MakeSharedDirectoryLookupKey(rDirectories.GetNext(pos)));
}

/**
 * @brief Reports whether a normalized key exists in a prebuilt shared-directory key vector.
 */
inline bool ContainsSharedDirectoryLookupKey(const std::vector<CString> &rDirectoryKeys, const CString &rstrDirectoryKey)
{
	for (size_t i = 0; i < rDirectoryKeys.size(); ++i) {
		if (rDirectoryKeys[i] == rstrDirectoryKey)
			return true;
	}
	return false;
}

/**
 * @brief Reports whether one normalized candidate key is equal to or below any normalized root key.
 */
inline bool IsDirectoryKeySameOrDescendantOfAny(const std::vector<CString> &rRootKeys, const CString &rstrCandidateKey)
{
	for (size_t i = 0; i < rRootKeys.size(); ++i) {
		if (IsDirectoryKeySameOrDescendant(rRootKeys[i], rstrCandidateKey))
			return true;
	}
	return false;
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
