#pragma once

#include <atlstr.h>
#include <tchar.h>

namespace SharedFileIntakePolicy
{
inline bool ShouldIgnoreByName(const CString &rstrFileName)
{
	if (rstrFileName.IsEmpty())
		return false;

	static const LPCTSTR s_apszIgnoredExactNames[] = {
		_T("ehthumbs.db"),
		_T("desktop.ini"),
		_T(".ds_store"),
		_T(".localized"),
		_T("Icon\r")
	};

	if (rstrFileName.Right(4).CompareNoCase(_T(".lnk")) == 0)
		return true;

	for (size_t i = 0; i < _countof(s_apszIgnoredExactNames); ++i) {
		if (rstrFileName.CompareNoCase(s_apszIgnoredExactNames[i]) == 0)
			return true;
	}

	return false;
}

template <typename IsThumbsDbFn>
inline bool ShouldIgnoreCandidate(const CString &rstrFilePath, const CString &rstrFileName, IsThumbsDbFn isThumbsDbFn)
{
	return ShouldIgnoreByName(rstrFileName) || isThumbsDbFn(rstrFilePath, rstrFileName);
}
}

bool ShouldIgnoreSharedFileCandidate(const CString &sFilePath, const CString &sFileName);
