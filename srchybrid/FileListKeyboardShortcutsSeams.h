//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#pragma once

#include <Windows.h>
#include <tchar.h>

#include "MenuCmds.h"

/** Testable keyboard shortcut policy for file-like list controls. */
namespace FileListKeyboardShortcutsSeams
{
	enum class EContext
	{
		Downloads,
		SearchResults,
		SharedFiles,
		SharedDirs
	};

	/**
	 * Classifies local file-list key messages into existing menu commands.
	 *
	 * EMULE_KEYBOARD_SHORTCUT: keep this aligned with
	 * docs/reference/KEYBOARD-SHORTCUTS.md in eMule-tooling.
	 */
	inline UINT ClassifyKeyMessage(EContext eContext, UINT uMessage, WPARAM wParam, bool bCtrlDown, bool bAltDown, bool bShiftDown)
	{
		if (uMessage != WM_KEYDOWN || !bCtrlDown || bAltDown)
			return 0;

		if (!bShiftDown) {
			switch (wParam) {
			case _T('I'):
				if (eContext == EContext::SharedDirs)
					return 0;
				return eContext == EContext::Downloads ? MP_METINFO : MP_DETAIL;
			case _T('L'):
				if (eContext == EContext::SharedDirs)
					return 0;
				return MP_GETED2KLINK;
			case _T('O'):
				return eContext == EContext::Downloads || eContext == EContext::SharedFiles ? MP_OPEN : 0;
			case _T('D'):
				return eContext == EContext::SearchResults ? MP_RESUME : 0;
			case _T('P'):
				return eContext == EContext::Downloads ? MP_PAUSE : 0;
			case _T('S'):
				return eContext == EContext::Downloads ? MP_RESUME : 0;
			case _T('T'):
				return eContext == EContext::Downloads ? MP_STOP : 0;
			default:
				return 0;
			}
		}

		switch (wParam) {
		case _T('C'):
			if (eContext == EContext::Downloads || eContext == EContext::SharedFiles)
				return MP_COPY_FILE_SUMMARY;
			if (eContext == EContext::SearchResults)
				return MP_COPY_SEARCH_SUMMARY;
			return 0;
		case _T('D'):
			return eContext == EContext::SearchResults ? MP_RESUMEPAUSED : 0;
		case _T('O'):
			return eContext == EContext::Downloads || eContext == EContext::SharedFiles || eContext == EContext::SharedDirs ? MP_OPENFOLDER : 0;
		default:
			return 0;
		}
	}
}
