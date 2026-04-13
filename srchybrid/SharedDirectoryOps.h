#pragma once

#include <afxtempl.h>

#include "PathHelpers.h"

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

inline bool HasSharedSubdirectory(const CStringList &rList, const CString &rstrDirectory)
{
	for (POSITION pos = rList.GetHeadPosition(); pos != NULL;) {
		const CString strCurrent(rList.GetNext(pos));
		if (!EqualPaths(strCurrent, rstrDirectory) && PathHelpers::IsPathWithinDirectory(rstrDirectory, strCurrent))
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
			&& (findData.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) == 0)
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

inline bool RemoveSharedDirectory(CStringList &rList, const CString &rstrDirectory, const bool bIncludeSubdirectories)
{
	const CString strCanonicalDirectory(PathHelpers::CanonicalizeDirectoryPath(rstrDirectory));
	bool bChanged = false;
	for (POSITION pos = rList.GetHeadPosition(); pos != NULL;) {
		const POSITION posCurrent = pos;
		const CString strCurrent(rList.GetNext(pos));
		const bool bMatches = bIncludeSubdirectories
			? PathHelpers::IsPathWithinDirectory(strCanonicalDirectory, strCurrent)
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
