//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.

#pragma once

#include <ctype.h>
#include <string.h>
#include <wctype.h>

namespace DropTargetSeams
{
	inline bool IsSupportedTextDrop(const wchar_t *pszText)
	{
		if (pszText == NULL)
			return false;
		while (iswspace(*pszText))
			++pszText;
		return _wcsnicmp(pszText, L"ed2k://|", 8) == 0;
	}

	inline bool IsSupportedTextDrop(const char *pszText)
	{
		if (pszText == NULL)
			return false;
		while (isspace(static_cast<unsigned char>(*pszText)))
			++pszText;
		return _strnicmp(pszText, "ed2k://|", 8) == 0;
	}
}
