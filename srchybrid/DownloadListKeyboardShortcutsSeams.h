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

/** Testable keyboard shortcut policy for the Downloads file list. */
namespace DownloadListKeyboardShortcutsSeams
{
	/**
	 * Classifies local Downloads list key messages into existing file commands.
	 *
	 * EMULE_KEYBOARD_SHORTCUT: keep this aligned with the Downloads list section
	 * in docs/reference/KEYBOARD-SHORTCUTS.md in eMule-tooling.
	 */
	inline UINT ClassifyKeyMessage(UINT uMessage, WPARAM wParam, bool bCtrlDown, bool bAltDown, bool bShiftDown)
	{
		if (uMessage != WM_KEYDOWN || !bCtrlDown || bAltDown || bShiftDown)
			return 0;

		switch (wParam) {
		case _T('P'):
			return MP_PAUSE;
		case _T('S'):
			return MP_RESUME;
		case _T('T'):
			return MP_STOP;
		default:
			return 0;
		}
	}
}
