#pragma once

#include <cstddef>
#include <tchar.h>

#include <atlstr.h>

namespace WebServicesSeams
{
constexpr size_t kMaxWebServiceMenuEntries = 100;

struct ParsedService
{
	CString strMenuLabel;
	CString strUrl;
	bool bFileMacros = false;
};

inline bool IsCommentOrBlankLine(const CString &rstrLine)
{
	CString strLine(rstrLine);
	strLine.Trim(_T(" \t\r\n"));
	return strLine.IsEmpty()
		|| strLine[0] == _T('#')
		|| strLine[0] == _T('/')
		|| strLine[0] == _T(';');
}

inline bool ContainsFileMacro(const CString &rstrUrlTemplate)
{
	static LPCTSTR const s_apszMacros[] = {
		_T("#hashid"),
		_T("#filesize"),
		_T("#filename"),
		_T("#name"),
		_T("#cleanfilename"),
		_T("#cleanname")
	};

	for (size_t i = 0; i < sizeof s_apszMacros / sizeof s_apszMacros[0]; ++i)
		if (rstrUrlTemplate.Find(s_apszMacros[i]) >= 0)
			return true;
	return false;
}

inline bool TryParseServiceLine(const CString &rstrLine, ParsedService &rParsedService)
{
	rParsedService = ParsedService{};

	CString strLine(rstrLine);
	strLine.Trim(_T(" \t\r\n"));
	if (IsCommentOrBlankLine(strLine))
		return false;

	const int iComma = strLine.Find(_T(','));
	if (iComma <= 0)
		return false;

	CString strMenuLabel(strLine.Left(iComma));
	CString strUrl(strLine.Mid(iComma + 1));
	strMenuLabel.Trim();
	strUrl.Trim();
	if (strMenuLabel.IsEmpty() || strUrl.IsEmpty())
		return false;

	rParsedService.strMenuLabel = strMenuLabel;
	rParsedService.strUrl = strUrl;
	rParsedService.bFileMacros = ContainsFileMacro(strUrl);
	return true;
}
}
