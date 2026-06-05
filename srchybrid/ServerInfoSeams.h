#pragma once

#include <atlstr.h>

#include <vector>

namespace ServerInfoSeams
{
inline std::vector<CString> SplitServerInfoMessageLines(CString strText)
{
	std::vector<CString> lines;
	CString strLine;
	for (int i = 0; i <= strText.GetLength(); ++i) {
		const TCHAR ch = i < strText.GetLength() ? strText[i] : _T('\n');
		if (ch == _T('\r') || ch == _T('\n')) {
			strLine.Trim();
			if (!strLine.IsEmpty())
				lines.push_back(strLine);
			strLine.Empty();
			if (ch == _T('\r') && i + 1 < strText.GetLength() && strText[i + 1] == _T('\n'))
				++i;
		} else
			strLine.AppendChar(ch);
	}
	return lines;
}
}
