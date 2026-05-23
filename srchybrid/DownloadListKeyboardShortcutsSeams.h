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

#include "FileListKeyboardShortcutsSeams.h"
#include "MenuCmds.h"

/** Testable keyboard shortcut policy for the Downloads file list. */
namespace DownloadListKeyboardShortcutsSeams
{
	/** Returns true when `wParam` is one of the plus keys used for priority-up. */
	inline bool IsPriorityUpKey(WPARAM wParam)
	{
		return wParam == VK_OEM_PLUS || wParam == VK_ADD;
	}

	/** Returns true when `wParam` is one of the minus keys used for priority-down. */
	inline bool IsPriorityDownKey(WPARAM wParam)
	{
		return wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT;
	}

	/**
	 * Classifies local Downloads list key messages into existing file commands.
	 *
	 * EMULE_KEYBOARD_SHORTCUT: keep this aligned with the Downloads list section
	 * in docs/reference/KEYBOARD-SHORTCUTS.md in emulebb-tooling.
	 */
	inline UINT ClassifyKeyMessage(UINT uMessage, WPARAM wParam, bool bCtrlDown, bool bAltDown, bool bShiftDown)
	{
		if (uMessage == WM_KEYDOWN && wParam == VK_DELETE && !bCtrlDown && !bAltDown)
			return bShiftDown ? MP_CANCEL_NO_CONFIRM : 0;

		if (uMessage == WM_KEYDOWN && bCtrlDown && !bAltDown) {
			if (bShiftDown) {
				if (wParam == _T('P'))
					return MP_PAUSE_CATEGORY;
				if (wParam == _T('S'))
					return MP_RESUME_CATEGORY;
				if (wParam == _T('T'))
					return MP_STOP_CATEGORY;
				if (IsPriorityUpKey(wParam))
					return MP_PRIOHIGH;
				if (IsPriorityDownKey(wParam))
					return MP_PRIOLOW;
			} else {
				if (IsPriorityUpKey(wParam))
					return MP_PRIOUP;
				if (IsPriorityDownKey(wParam))
					return MP_PRIODOWN;
			}
		}

		return FileListKeyboardShortcutsSeams::ClassifyKeyMessage(
			FileListKeyboardShortcutsSeams::EContext::Downloads,
			uMessage,
			wParam,
			bCtrlDown,
			bAltDown,
			bShiftDown);
	}
}
