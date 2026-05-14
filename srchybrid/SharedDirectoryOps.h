#pragma once

#include <afxtempl.h>

#include "PathHelpers.h"
#include "SharedFileIntakePolicy.h"

bool EqualPaths(const CString &rstrDir1, const CString &rstrDir2);

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

inline bool ListContainsEquivalentPath(const CStringList &rList, const CString &rstrPath)
{
	for (POSITION pos = rList.GetHeadPosition(); pos != NULL;) {
		if (EqualPaths(rList.GetNext(pos), rstrPath))
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
 * @brief Returns true when a candidate resolves under an already canonicalized directory, excluding the directory itself.
 */
inline bool IsCanonicalParentOfPath(const CString &rstrCanonicalDirectory, const CString &rstrCandidatePath)
{
	const CString strDirectory(PathHelpers::EnsureTrailingSeparator(rstrCanonicalDirectory));
	const CString strCandidate(PathHelpers::CanonicalizePathForComparison(rstrCandidatePath));
	const CString strDirectoryWithoutSlash(PathHelpers::TrimTrailingSeparator(strDirectory));
	const CString strCandidateWithoutSlash(PathHelpers::TrimTrailingSeparator(strCandidate));
	return strCandidateWithoutSlash.CompareNoCase(strDirectoryWithoutSlash) != 0
		&& strCandidate.GetLength() >= strDirectory.GetLength()
		&& _tcsnicmp(strDirectory, strCandidate, strDirectory.GetLength()) == 0;
}

inline bool HasSharedSubdirectory(const CStringList &rList, const CString &rstrDirectory)
{
	const CString strCanonicalDirectory(PathHelpers::EnsureTrailingSeparator(PathHelpers::CanonicalizePathForComparison(rstrDirectory)));
	for (POSITION pos = rList.GetHeadPosition(); pos != NULL;) {
		if (IsCanonicalParentOfPath(strCanonicalDirectory, rList.GetNext(pos)))
			return true;
	}
	return false;
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

inline bool RemoveSharedDirectory(CStringList &rList, const CString &rstrDirectory, const bool bIncludeSubdirectories)
{
	const CString strCanonicalDirectory(PathHelpers::CanonicalizeDirectoryPath(rstrDirectory));
	bool bChanged = false;
	for (POSITION pos = rList.GetHeadPosition(); pos != NULL;) {
		const POSITION posCurrent = pos;
		const CString strCurrent(rList.GetNext(pos));
		const bool bMatches = bIncludeSubdirectories
			? (EqualPaths(strCurrent, strCanonicalDirectory) || PathHelpers::IsPathWithinDirectory(strCanonicalDirectory, strCurrent))
			: EqualPaths(strCurrent, strCanonicalDirectory);
		if (!bMatches)
			continue;

		rList.RemoveAt(posCurrent);
		bChanged = true;
		if (!bIncludeSubdirectories)
			break;
	}
	return bChanged;
}
}
