#pragma once

#include <atlstr.h>
#include <tchar.h>
#include <vector>
#include <windows.h>
#include <ShlObj_core.h>
#include "LongPathSeams.h"

namespace PathHelpers
{
constexpr size_t kMaxDynamicPathChars = 32768u;

/**
 * @brief Returns true when the character is treated as a Win32 path separator.
 */
inline bool IsPathSeparator(const TCHAR ch)
{
	return ch == _T('\\') || ch == _T('/');
}

/**
 * @brief Normalizes forward slashes to backslashes before path composition or canonicalization.
 */
inline CString NormalizePathSeparators(const CString &rstrPath)
{
	CString strNormalized(rstrPath);
	strNormalized.Replace(_T('/'), _T('\\'));
	return strNormalized;
}

/**
 * @brief Returns true when the path already uses a Win32 extended-length prefix.
 */
inline bool HasExtendedLengthPrefix(const CString &rstrPath)
{
	const CString strNormalized(NormalizePathSeparators(rstrPath));
	return strNormalized.Left(8).CompareNoCase(_T("\\\\?\\UNC\\")) == 0
		|| strNormalized.Left(4).CompareNoCase(_T("\\\\?\\")) == 0;
}

/**
 * @brief Removes the Win32 extended-length prefix before passing a path to shell-facing APIs.
 */
inline CString StripExtendedLengthPrefix(const CString &rstrPath)
{
	const CString strNormalized(NormalizePathSeparators(rstrPath));
	if (strNormalized.Left(8).CompareNoCase(_T("\\\\?\\UNC\\")) == 0)
		return CString(_T("\\\\")) + strNormalized.Mid(8);
	if (strNormalized.Left(4).CompareNoCase(_T("\\\\?\\")) == 0)
		return strNormalized.Mid(4);
	return strNormalized;
}

/**
 * @brief Returns true when the logical path text needs extended-length namespace semantics to preserve its exact final component spelling.
 */
inline bool RequiresExtendedLengthPathForExactName(const CString &rstrPath)
{
	return LongPathSeams::RequiresExtendedLengthPathForExactName(NormalizePathSeparators(rstrPath));
}

/**
 * @brief Returns true when a path can be handed to shell/path-parsing APIs without losing its exact spelling.
 */
inline bool IsShellSafePath(const CString &rstrPath)
{
	return !rstrPath.IsEmpty()
		&& !HasExtendedLengthPrefix(rstrPath)
		&& rstrPath.GetLength() < MAX_PATH
		&& !RequiresExtendedLengthPathForExactName(rstrPath);
}

/**
 * @brief Grows the module-path buffer until `GetModuleFileName` returns the full path.
 */
template <typename GetModuleFileNameFn>
inline CString GetModuleFilePath(HMODULE hModule, GetModuleFileNameFn getModuleFileNameFn)
{
	std::vector<TCHAR> buffer(MAX_PATH, _T('\0'));
	while (buffer.size() < kMaxDynamicPathChars) {
		const DWORD dwCopied = getModuleFileNameFn(hModule, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (dwCopied == 0)
			return CString();
		if (dwCopied < buffer.size()) {
			buffer[dwCopied] = _T('\0');
			return CString(buffer.data());
		}

		buffer.resize(buffer.size() * 2u, _T('\0'));
	}

	::SetLastError(ERROR_BUFFER_OVERFLOW);
	return CString();
}

/**
 * @brief Returns the module path through the real Win32 `GetModuleFileName` API.
 */
inline CString GetModuleFilePath(HMODULE hModule)
{
	return GetModuleFilePath(hModule, ::GetModuleFileName);
}

/**
 * @brief Grows the current-directory buffer until `GetCurrentDirectory` returns the full path.
 */
inline CString GetCurrentDirectoryPath()
{
	DWORD dwCapacity = MAX_PATH;
	CString strPath;
	for (;;) {
		LPTSTR pszBuffer = strPath.GetBuffer(dwCapacity);
		const DWORD dwLength = ::GetCurrentDirectory(dwCapacity, pszBuffer);
		if (dwLength == 0) {
			strPath.ReleaseBuffer(0);
			return CString();
		}
		if (dwLength < dwCapacity) {
			strPath.ReleaseBuffer(dwLength);
			return strPath;
		}
		strPath.ReleaseBuffer(dwCapacity);
		dwCapacity = dwLength + 1;
	}
}

/**
 * @brief Queries a shell folder path into a dynamically sized buffer instead of a fixed `MAX_PATH` array.
 */
template <typename GetShellFolderPathFn>
inline CString GetShellFolderPath(int iCSIDL, GetShellFolderPathFn getShellFolderPathFn)
{
	std::vector<TCHAR> buffer(kMaxDynamicPathChars, _T('\0'));
	if (FAILED(getShellFolderPathFn(NULL, iCSIDL, NULL, SHGFP_TYPE_CURRENT, buffer.data())))
		return CString();
	buffer.back() = _T('\0');
	return CString(buffer.data());
}

/**
 * @brief Returns the shell folder path through the real `SHGetFolderPath` API.
 */
inline CString GetShellFolderPath(int iCSIDL)
{
	return GetShellFolderPath(
		iCSIDL,
		[](HWND hWnd, int iFolder, HANDLE hToken, DWORD dwFlags, LPTSTR pszPath) -> HRESULT {
			return ::SHGetFolderPath(hWnd, iFolder, hToken, dwFlags, pszPath);
		});
}

/**
 * @brief Appends a relative child path to a base path without relying on `PathCombine`.
 */
inline CString AppendPathComponent(const CString &rstrBasePath, LPCTSTR pszChildPath)
{
	if (pszChildPath == NULL || *pszChildPath == _T('\0'))
		return NormalizePathSeparators(rstrBasePath);
	if (rstrBasePath.IsEmpty())
		return NormalizePathSeparators(CString(pszChildPath));

	CString strCombined(NormalizePathSeparators(rstrBasePath));
	LPCTSTR pszSuffix = pszChildPath;
	while (IsPathSeparator(*pszSuffix))
		++pszSuffix;

	if (!strCombined.IsEmpty() && !IsPathSeparator(strCombined[strCombined.GetLength() - 1]))
		strCombined += _T('\\');
	strCombined += NormalizePathSeparators(CString(pszSuffix));
	return strCombined;
}

/**
 * @brief Returns the parent directory portion of a file path without using fixed buffers.
 */
inline CString GetDirectoryPath(const CString &rstrPath)
{
	const CString strNormalized(NormalizePathSeparators(rstrPath));
	const int iLastSlash = strNormalized.ReverseFind(_T('\\'));
	if (iLastSlash < 0)
		return CString();
	if (iLastSlash == 2 && strNormalized.GetLength() >= 3 && strNormalized[1] == _T(':'))
		return strNormalized.Left(iLastSlash + 1);
	return strNormalized.Left(iLastSlash);
}

struct ParsedPathRoot
{
	CString strPrefix;
	CString strRemainder;
	bool bAbsolute;
};

inline bool ReadNextPathSegment(const CString &rstrPath, int &iIndex, CString &rstrSegment)
{
	const int nLength = rstrPath.GetLength();
	while (iIndex < nLength && IsPathSeparator(rstrPath[iIndex]))
		++iIndex;
	if (iIndex >= nLength) {
		rstrSegment.Empty();
		return false;
	}

	const int iStart = iIndex;
	while (iIndex < nLength && !IsPathSeparator(rstrPath[iIndex]))
		++iIndex;
	rstrSegment = rstrPath.Mid(iStart, iIndex - iStart);
	return true;
}

inline bool TryParseUncRoot(const CString &rstrPath, const CString &rstrPrefix, ParsedPathRoot &rParsed)
{
	int iIndex = rstrPrefix.GetLength();
	CString strServer;
	CString strShare;
	if (!ReadNextPathSegment(rstrPath, iIndex, strServer) || !ReadNextPathSegment(rstrPath, iIndex, strShare))
		return false;

	rParsed.strPrefix.Format(_T("%s%s\\%s\\"), (LPCTSTR)rstrPrefix, (LPCTSTR)strServer, (LPCTSTR)strShare);
	rParsed.strRemainder = rstrPath.Mid(iIndex);
	rParsed.bAbsolute = true;
	return true;
}

inline ParsedPathRoot ParsePathRoot(const CString &rstrPath)
{
	const CString strNormalized(NormalizePathSeparators(rstrPath));
	ParsedPathRoot parsed = { CString(), strNormalized, false };

	if (strNormalized.Left(8).CompareNoCase(_T("\\\\?\\UNC\\")) == 0) {
		if (TryParseUncRoot(strNormalized, _T("\\\\?\\UNC\\"), parsed))
			return parsed;
		return parsed;
	}

	if (strNormalized.Left(4).CompareNoCase(_T("\\\\?\\")) == 0
		&& strNormalized.GetLength() >= 7
		&& strNormalized[5] == _T(':')
		&& IsPathSeparator(strNormalized[6]))
	{
		parsed.strPrefix = strNormalized.Left(7);
		parsed.strRemainder = strNormalized.Mid(7);
		parsed.bAbsolute = true;
		return parsed;
	}

	if (strNormalized.Left(2) == _T("\\\\")) {
		if (TryParseUncRoot(strNormalized, _T("\\\\"), parsed))
			return parsed;
		return parsed;
	}

	if (strNormalized.GetLength() >= 3
		&& ((strNormalized[0] >= _T('A') && strNormalized[0] <= _T('Z')) || (strNormalized[0] >= _T('a') && strNormalized[0] <= _T('z')))
		&& strNormalized[1] == _T(':')
		&& IsPathSeparator(strNormalized[2]))
	{
		parsed.strPrefix = strNormalized.Left(3);
		parsed.strRemainder = strNormalized.Mid(3);
		parsed.bAbsolute = true;
		return parsed;
	}

	if (strNormalized.GetLength() >= 2
		&& ((strNormalized[0] >= _T('A') && strNormalized[0] <= _T('Z')) || (strNormalized[0] >= _T('a') && strNormalized[0] <= _T('z')))
		&& strNormalized[1] == _T(':'))
	{
		parsed.strPrefix = strNormalized.Left(2);
		parsed.strRemainder = strNormalized.Mid(2);
		return parsed;
	}

	if (!strNormalized.IsEmpty() && IsPathSeparator(strNormalized[0])) {
		parsed.strPrefix = _T("\\");
		parsed.strRemainder = strNormalized.Mid(1);
		parsed.bAbsolute = true;
	}

	return parsed;
}

/**
 * @brief Ensures that a directory-valued path ends with a backslash.
 */
inline CString EnsureTrailingSeparator(const CString &rstrPath)
{
	CString strNormalized(NormalizePathSeparators(rstrPath));
	if (!strNormalized.IsEmpty() && !IsPathSeparator(strNormalized[strNormalized.GetLength() - 1]))
		strNormalized += _T('\\');
	return strNormalized;
}

/**
 * @brief Trims trailing separators without stripping the logical path root.
 */
inline CString TrimTrailingSeparator(const CString &rstrPath)
{
	CString strNormalized(NormalizePathSeparators(rstrPath));
	if (strNormalized.IsEmpty())
		return strNormalized;

	const ParsedPathRoot root(ParsePathRoot(strNormalized));
	const int nRootLength = root.strPrefix.GetLength();
	while (strNormalized.GetLength() > nRootLength && IsPathSeparator(strNormalized[strNormalized.GetLength() - 1]))
		strNormalized.Truncate(strNormalized.GetLength() - 1);
	return strNormalized;
}

/**
 * @brief Trims a trailing separator even for logical roots when callers need a leaf-style path view.
 */
inline CString TrimTrailingSeparatorForLeaf(const CString &rstrPath)
{
	CString strNormalized(TrimTrailingSeparator(rstrPath));
	if (strNormalized.IsEmpty() || !IsPathSeparator(strNormalized[strNormalized.GetLength() - 1]))
		return strNormalized;

	const ParsedPathRoot root(ParsePathRoot(strNormalized));
	if (!root.strPrefix.IsEmpty() && strNormalized.GetLength() == root.strPrefix.GetLength())
		strNormalized.Truncate(strNormalized.GetLength() - 1);
	return strNormalized;
}

/**
 * @brief Lexically removes `.` and `..` segments without depending on `PathCanonicalize`.
 */
inline CString CanonicalizePath(const CString &rstrPath)
{
	if (rstrPath.IsEmpty())
		return CString();

	const ParsedPathRoot root = ParsePathRoot(rstrPath);
	std::vector<CString> segments;
	int iIndex = 0;
	CString strSegment;
	while (ReadNextPathSegment(root.strRemainder, iIndex, strSegment)) {
		if (strSegment.IsEmpty() || strSegment == _T("."))
			continue;

		if (strSegment == _T("..")) {
			if (!segments.empty() && segments.back() != _T("..")) {
				segments.pop_back();
				continue;
			}
			if (!root.bAbsolute)
				segments.push_back(strSegment);
			continue;
		}

		segments.push_back(strSegment);
	}

	CString strCanonical(root.strPrefix);
	for (size_t i = 0; i < segments.size(); ++i) {
		if (!strCanonical.IsEmpty()
			&& !IsPathSeparator(strCanonical[strCanonical.GetLength() - 1])
			&& !(strCanonical.GetLength() == 2 && strCanonical[1] == _T(':')))
		{
			strCanonical += _T('\\');
		}
		strCanonical += segments[i];
	}

	if (!strCanonical.IsEmpty())
		return strCanonical;
	return root.strPrefix.IsEmpty() ? CString(_T(".")) : root.strPrefix;
}

/**
 * @brief Reports whether the normalized path text ends with a directory separator.
 */
inline bool HasTrailingSeparator(const CString &rstrPath)
{
	const CString strNormalized(NormalizePathSeparators(rstrPath));
	return !strNormalized.IsEmpty() && IsPathSeparator(strNormalized[strNormalized.GetLength() - 1]);
}

/**
 * @brief Returns true when the path can be resolved through the filesystem to a canonical long-name spelling.
 */
inline bool TryCanonicalizeExistingPath(const CString &rstrPath, CString &rstrCanonicalPath, DWORD *pdwLastError = NULL)
{
	rstrCanonicalPath.Empty();
	if (pdwLastError != NULL)
		*pdwLastError = ERROR_SUCCESS;

	if (rstrPath.IsEmpty()) {
		if (pdwLastError != NULL)
			*pdwLastError = ERROR_INVALID_NAME;
		return false;
	}

	const bool bHadTrailingSeparator = HasTrailingSeparator(rstrPath);
	const CString strLexicalPath(CanonicalizePath(StripExtendedLengthPrefix(rstrPath)));
	const ParsedPathRoot root(ParsePathRoot(strLexicalPath));
	if (!root.bAbsolute || root.strPrefix.IsEmpty()) {
		if (pdwLastError != NULL)
			*pdwLastError = ERROR_INVALID_NAME;
		return false;
	}

	CString strCurrent(root.strPrefix);
	if (root.strRemainder.IsEmpty()) {
		if (!LongPathSeams::PathExists(strCurrent)) {
			if (pdwLastError != NULL)
				*pdwLastError = ::GetLastError();
			return false;
		}
		rstrCanonicalPath = bHadTrailingSeparator ? EnsureTrailingSeparator(strCurrent) : strCurrent;
		return true;
	}

	int iIndex = 0;
	CString strSegment;
	while (ReadNextPathSegment(root.strRemainder, iIndex, strSegment)) {
		const CString strCandidate(AppendPathComponent(strCurrent, strSegment));
		WIN32_FIND_DATA findData = {};
		const HANDLE hFind = LongPathSeams::FindFirstFile(strCandidate, &findData);
		if (hFind == INVALID_HANDLE_VALUE) {
			if (pdwLastError != NULL)
				*pdwLastError = ::GetLastError();
			return false;
		}

		::FindClose(hFind);
		strCurrent = AppendPathComponent(strCurrent, findData.cFileName);
	}

	rstrCanonicalPath = bHadTrailingSeparator ? EnsureTrailingSeparator(strCurrent) : strCurrent;
	return true;
}

/**
 * @brief Canonicalizes a path lexically and expands existing entries to their long-name spelling when possible.
 */
inline CString CanonicalizePathForComparison(const CString &rstrPath, bool *pbResolvedExistingPath = NULL, DWORD *pdwLastError = NULL)
{
	if (pbResolvedExistingPath != NULL)
		*pbResolvedExistingPath = false;
	if (pdwLastError != NULL)
		*pdwLastError = ERROR_SUCCESS;

	if (rstrPath.IsEmpty())
		return CString();

	CString strCanonicalPath;
	DWORD dwResolveError = ERROR_SUCCESS;
	if (TryCanonicalizeExistingPath(rstrPath, strCanonicalPath, &dwResolveError)) {
		if (pbResolvedExistingPath != NULL)
			*pbResolvedExistingPath = true;
		return strCanonicalPath;
	}

	if (pdwLastError != NULL)
		*pdwLastError = dwResolveError;

	const CString strLexicalPath(CanonicalizePath(StripExtendedLengthPrefix(rstrPath)));
	return HasTrailingSeparator(rstrPath) ? EnsureTrailingSeparator(strLexicalPath) : strLexicalPath;
}

/**
 * @brief Canonicalizes a directory-valued path and preserves the trailing separator expected by callers.
 */
inline CString CanonicalizeDirectoryPath(const CString &rstrPath)
{
	if (rstrPath.IsEmpty())
		return rstrPath;
	return EnsureTrailingSeparator(CanonicalizePathForComparison(rstrPath));
}

/**
 * @brief Reports whether two filesystem paths refer to the same canonical location.
 */
inline bool ArePathsEquivalent(const CString &rstrLeft, const CString &rstrRight)
{
	CString strLeftCanonical(TrimTrailingSeparator(CanonicalizePathForComparison(rstrLeft)));
	CString strRightCanonical(TrimTrailingSeparator(CanonicalizePathForComparison(rstrRight)));
	return strLeftCanonical.GetLength() == strRightCanonical.GetLength()
		&& strLeftCanonical.CompareNoCase(strRightCanonical) == 0;
}

/**
 * @brief Reports whether a filesystem path lives inside the provided directory after semantic normalization.
 */
inline bool IsPathWithinDirectory(const CString &rstrDirectoryPath, const CString &rstrCandidatePath)
{
	const CString strDirectory(EnsureTrailingSeparator(CanonicalizePathForComparison(rstrDirectoryPath)));
	const CString strCandidate(CanonicalizePathForComparison(rstrCandidatePath));
	return strCandidate.GetLength() >= strDirectory.GetLength()
		&& _tcsnicmp(strDirectory, strCandidate, strDirectory.GetLength()) == 0;
}

/**
 * @brief Queries a single filesystem entry through the long-path-aware Win32 find APIs.
 */
inline bool TryGetPathEntryData(const CString &rstrPath, WIN32_FIND_DATA &rFindData, DWORD *pdwLastError = NULL)
{
	if (pdwLastError != NULL)
		*pdwLastError = ERROR_SUCCESS;

	const HANDLE hFind = LongPathSeams::FindFirstFile(CanonicalizePathForComparison(rstrPath), &rFindData);
	if (hFind == INVALID_HANDLE_VALUE) {
		if (pdwLastError != NULL)
			*pdwLastError = ::GetLastError();
		return false;
	}

	::FindClose(hFind);
	return true;
}

/**
 * @brief Enumerates wildcard matches through the long-path-aware Win32 find APIs.
 */
template <typename EntryCallback>
inline bool ForEachMatchingEntry(const CString &rstrSearchPattern, EntryCallback entryCallback, DWORD *pdwLastError = NULL)
{
	if (pdwLastError != NULL)
		*pdwLastError = ERROR_SUCCESS;

	const CString strNormalizedPattern(NormalizePathSeparators(rstrSearchPattern));
	const int iLastSlash = strNormalizedPattern.ReverseFind(_T('\\'));
	const CString strSearchPath = (iLastSlash >= 0)
		? AppendPathComponent(CanonicalizeDirectoryPath(strNormalizedPattern.Left(iLastSlash)), strNormalizedPattern.Mid(iLastSlash + 1))
		: strNormalizedPattern;

	WIN32_FIND_DATA findData = {};
	const HANDLE hFind = LongPathSeams::FindFirstFile(strSearchPath, &findData);
	if (hFind == INVALID_HANDLE_VALUE) {
		if (pdwLastError != NULL)
			*pdwLastError = ::GetLastError();
		return false;
	}

	bool bStoppedEarly = false;
	do {
		if (_tcscmp(findData.cFileName, _T(".")) == 0 || _tcscmp(findData.cFileName, _T("..")) == 0)
			continue;
		if (!entryCallback(findData)) {
			bStoppedEarly = true;
			break;
		}
	} while (::FindNextFile(hFind, &findData));

	const DWORD dwFindError = ::GetLastError();
	::FindClose(hFind);
	if (bStoppedEarly)
		return true;
	if (dwFindError != ERROR_NO_MORE_FILES) {
		if (pdwLastError != NULL)
			*pdwLastError = dwFindError;
		return false;
	}

	return true;
}

/**
 * @brief Enumerates immediate filesystem entries for a directory through the long-path-aware Win32 find APIs.
 */
template <typename EntryCallback>
inline bool ForEachDirectoryEntry(const CString &rstrDirectoryPath, EntryCallback entryCallback, DWORD *pdwLastError = NULL)
{
	if (pdwLastError != NULL)
		*pdwLastError = ERROR_SUCCESS;

	const CString strDirectory(EnsureTrailingSeparator(CanonicalizePathForComparison(rstrDirectoryPath)));
	WIN32_FIND_DATA findData = {};
	const HANDLE hFind = LongPathSeams::FindFirstFile(AppendPathComponent(strDirectory, _T("*")), &findData);
	if (hFind == INVALID_HANDLE_VALUE) {
		if (pdwLastError != NULL)
			*pdwLastError = ::GetLastError();
		return false;
	}

	bool bStoppedEarly = false;
	do {
		if (_tcscmp(findData.cFileName, _T(".")) == 0 || _tcscmp(findData.cFileName, _T("..")) == 0)
			continue;
		if (!entryCallback(findData)) {
			bStoppedEarly = true;
			break;
		}
	} while (::FindNextFile(hFind, &findData));

	const DWORD dwFindError = ::GetLastError();
	::FindClose(hFind);
	if (bStoppedEarly)
		return true;
	if (dwFindError != ERROR_NO_MORE_FILES) {
		if (pdwLastError != NULL)
			*pdwLastError = dwFindError;
		return false;
	}
	return true;
}

/**
 * @brief Formats a `res://` URL from the current module path without truncating overlong paths.
 */
template <typename GetModuleFileNameFn>
inline CString BuildModuleResourceBaseUrl(HMODULE hModule, GetModuleFileNameFn getModuleFileNameFn)
{
	const CString strModulePath(GetModuleFilePath(hModule, getModuleFileNameFn));
	if (strModulePath.IsEmpty())
		return CString();

	CString strResourceUrl;
	strResourceUrl.Format(_T("res://%s"), (LPCTSTR)strModulePath);
	return strResourceUrl;
}

/**
 * @brief Formats a `res://` URL from the real module path.
 */
inline CString BuildModuleResourceBaseUrl(HMODULE hModule)
{
	return BuildModuleResourceBaseUrl(hModule, ::GetModuleFileName);
}
}

#define EMULE_TEST_HAVE_PATH_HELPERS 1
