#pragma once

#include <atlstr.h>
#include <tchar.h>
#include "LongPathSeams.h"

namespace FilenameNormalizationPolicy
{
constexpr LPCTSTR kInvalidFilenameChars = _T("\"*<>?|\\/:");
constexpr LPCTSTR kDefaultDownloadFilename = _T("download");

inline bool IsNormalizedFilenameWhitespace(const TCHAR ch)
{
	return _istspace(ch) != 0
		|| ch == static_cast<TCHAR>(0x00A0)
		|| ch == static_cast<TCHAR>(0x2003);
}

inline void TrimWin32TrailingFilenameChars(CString &rstrFileName)
{
	while (!rstrFileName.IsEmpty()) {
		const TCHAR chLast = rstrFileName[rstrFileName.GetLength() - 1];
		if (chLast != _T(' ') && chLast != _T('.'))
			break;
		rstrFileName.Truncate(rstrFileName.GetLength() - 1);
	}
}

inline bool IsFilenameBasenameEdgePunctuation(const TCHAR ch)
{
	return ch == _T('.')
		|| ch == _T(',')
		|| ch == _T(';')
		|| ch == _T(':')
		|| ch == _T('!')
		|| ch == _T('?')
		|| ch == _T('-')
		|| ch == static_cast<TCHAR>(0x2013)
		|| ch == static_cast<TCHAR>(0x2014);
}

inline void TrimFilenameBasenameEdges(CString &rstrBaseName)
{
	while (!rstrBaseName.IsEmpty()) {
		const TCHAR chFirst = rstrBaseName[0];
		if (!IsNormalizedFilenameWhitespace(chFirst) && !IsFilenameBasenameEdgePunctuation(chFirst))
			break;
		rstrBaseName.Delete(0, 1);
	}
	while (!rstrBaseName.IsEmpty()) {
		const TCHAR chLast = rstrBaseName[rstrBaseName.GetLength() - 1];
		if (!IsNormalizedFilenameWhitespace(chLast) && !IsFilenameBasenameEdgePunctuation(chLast))
			break;
		rstrBaseName.Truncate(rstrBaseName.GetLength() - 1);
	}
}

inline int FindFilenameExtensionSeparator(const CString &rstrFileName)
{
	const int iDot = rstrFileName.ReverseFind(_T('.'));
	return iDot > 0 && iDot < rstrFileName.GetLength() - 1 ? iDot : -1;
}

inline CString CollapseFilenameWhitespace(const CString &rstrText, const bool bReplacePlaceholderSeparators)
{
	CString strCollapsed;
	bool bPreviousWasSpace = false;
	for (int i = 0; i < rstrText.GetLength(); ++i) {
		TCHAR ch = rstrText[i];
		if (bReplacePlaceholderSeparators && (ch == _T('_') || ch == _T('+') || ch == _T('=')))
			ch = _T(' ');
		if (IsNormalizedFilenameWhitespace(ch))
			ch = _T(' ');
		if (ch == _T(' ')) {
			if (strCollapsed.IsEmpty() || bPreviousWasSpace)
				continue;
			bPreviousWasSpace = true;
		} else
			bPreviousWasSpace = false;
		strCollapsed += ch;
	}
	return strCollapsed.Trim();
}

inline void ProtectReservedFilenameLeaf(CString &rstrFileName)
{
	if (rstrFileName.IsEmpty())
		return;

	bool bReservedName = LongPathSeams::IsReservedWin32DeviceName(LongPathSeams::PathString(static_cast<LPCTSTR>(rstrFileName)));
	if (!bReservedName) {
		CString strCandidate(rstrFileName);
		TrimWin32TrailingFilenameChars(strCandidate);
		const int iDot = FindFilenameExtensionSeparator(strCandidate);
		const CString strStem(iDot >= 0 ? strCandidate.Left(iDot) : strCandidate);
		bReservedName = strStem.CompareNoCase(_T("CLOCK$")) == 0;
	}

	if (!bReservedName)
		return;

	const int iDot = FindFilenameExtensionSeparator(rstrFileName);
	if (iDot >= 0)
		rstrFileName.Insert(iDot, _T('_'));
	else
		rstrFileName += _T('_');
}

inline CString StripInvalidFilenameChars(const CString &strText)
{
	CString strDest;

	for (LPCTSTR pszSource = strText; *pszSource; ++pszSource)
		if (*pszSource >= _T('\x20') && !_tcschr(kInvalidFilenameChars, *pszSource))
			strDest += *pszSource;

	TrimWin32TrailingFilenameChars(strDest);
	ProtectReservedFilenameLeaf(strDest);
	return strDest;
}

inline CString NormalizeDownloadFilename(const CString &strText)
{
	CString strNormalized(FilenameNormalizationPolicy::StripInvalidFilenameChars(strText));

	CString strBaseName(strNormalized);
	CString strExtension;
	const int iDot = FindFilenameExtensionSeparator(strNormalized);
	if (iDot >= 0) {
		strBaseName = strNormalized.Left(iDot);
		strExtension = strNormalized.Mid(iDot + 1);
	}

	strBaseName = CollapseFilenameWhitespace(strBaseName, true);
	TrimFilenameBasenameEdges(strBaseName);
	strExtension = CollapseFilenameWhitespace(strExtension, false);

	if (strBaseName.IsEmpty() && !strExtension.IsEmpty())
		strBaseName = kDefaultDownloadFilename;

	CString strResult;
	if (!strBaseName.IsEmpty())
		strResult = strBaseName;
	if (!strExtension.IsEmpty()) {
		if (!strResult.IsEmpty())
			strResult += _T('.');
		strResult += strExtension;
	}

	TrimWin32TrailingFilenameChars(strResult);
	if (strResult.IsEmpty())
		strResult = kDefaultDownloadFilename;

	ProtectReservedFilenameLeaf(strResult);
	return strResult;
}

/**
 * @brief Normalizes a source-provided filename only when it still has a usable basename.
 */
inline bool TryNormalizeDownloadFilenameCandidate(const CString &strText, CString &rstrNormalized)
{
	rstrNormalized.Empty();

	CString strTrimmed(strText);
	strTrimmed.Trim();
	if (strTrimmed.IsEmpty())
		return false;

	CString strNormalized(FilenameNormalizationPolicy::StripInvalidFilenameChars(strTrimmed));
	if (!strNormalized.IsEmpty() && strNormalized[0] == _T('.'))
		return false;

	CString strBaseName(strNormalized);
	CString strExtension;
	const int iDot = FindFilenameExtensionSeparator(strNormalized);
	if (iDot >= 0) {
		strBaseName = strNormalized.Left(iDot);
		strExtension = strNormalized.Mid(iDot + 1);
	}

	strBaseName = CollapseFilenameWhitespace(strBaseName, true);
	TrimFilenameBasenameEdges(strBaseName);
	if (strBaseName.IsEmpty())
		return false;

	strExtension = CollapseFilenameWhitespace(strExtension, false);
	rstrNormalized = strBaseName;
	if (!strExtension.IsEmpty()) {
		rstrNormalized += _T('.');
		rstrNormalized += strExtension;
	}

	TrimWin32TrailingFilenameChars(rstrNormalized);
	if (rstrNormalized.IsEmpty())
		return false;

	ProtectReservedFilenameLeaf(rstrNormalized);
	return true;
}
}
