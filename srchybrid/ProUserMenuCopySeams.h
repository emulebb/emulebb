//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
#pragma once

#include <atlstr.h>
#include <tchar.h>
#include <vector>

/**
 * @brief Small formatting helpers for pro-user context-menu copy commands.
 */
namespace ProUserMenuCopySeams
{
	struct NamedField
	{
		CString Name;
		CString Value;
	};

	inline void AppendField(std::vector<NamedField>& fields, LPCTSTR name, const CString& value)
	{
		if (value.IsEmpty())
			return;
		fields.push_back(NamedField{ name, value });
	}

	inline CString FormatSummary(const std::vector<NamedField>& fields)
	{
		CString result;
		for (const NamedField& field : fields) {
			if (!result.IsEmpty())
				result += _T("; ");
			result += field.Name;
			result += _T("=\"");
			CString value(field.Value);
			value.Replace(_T("\""), _T("\\\""));
			result += value;
			result += _T("\"");
		}
		return result;
	}

	/** Formats a stable pro-user summary line for file-like context menus. */
	inline CString FormatFileSummary(
		const CString& name,
		const CString& hash,
		const CString& size,
		const CString& status,
		const CString& progress,
		const CString& path,
		const CString& link)
	{
		std::vector<NamedField> fields;
		AppendField(fields, _T("name"), name);
		AppendField(fields, _T("hash"), hash);
		AppendField(fields, _T("size"), size);
		AppendField(fields, _T("status"), status);
		AppendField(fields, _T("progress"), progress);
		AppendField(fields, _T("path"), path);
		AppendField(fields, _T("link"), link);
		return FormatSummary(fields);
	}

	inline CString JoinLines(const std::vector<CString>& values)
	{
		CString result;
		for (const CString& value : values) {
			if (value.IsEmpty())
				continue;
			if (!result.IsEmpty())
				result += _T("\r\n");
			result += value;
		}
		return result;
	}
}
