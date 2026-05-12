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

#include <tchar.h>
#include <Windows.h>

/** Testable keyboard shortcut policy for the native main eMule shell. */
namespace AppKeyboardShortcutsSeams
{
	/**
	 * Commands reserved by the main eMule shell keyboard policy.
	 *
	 * EMULE_KEYBOARD_SHORTCUT: keep this list aligned with
	 * docs/KEYBOARD-SHORTCUTS.md in eMule-tooling when shortcut ownership changes.
	 */
	enum class ECommand
	{
		None,
		ExitApp,
		ShowHotMenu,
		ShowToolsMenu
	};

	/**
	 * Commands owned by the Search parameter pane's local mnemonic policy.
	 *
	 * EMULE_KEYBOARD_SHORTCUT: these mnemonics intentionally avoid the
	 * main-shell toolbar and hotmenu letters documented in KEYBOARD-SHORTCUTS.md.
	 */
	enum class ESearchCommand
	{
		None,
		FocusName,
		FocusType,
		FocusMethod,
		StartSearch,
		SearchMore,
		ResetSearch,
		CancelSearch
	};

	/** Returns `ch` normalized for ASCII accelerator comparisons. */
	inline TCHAR NormalizeAsciiShortcutChar(TCHAR ch)
	{
		if (ch >= _T('A') && ch <= _T('Z'))
			return static_cast<TCHAR>(ch - _T('A') + _T('a'));
		return ch;
	}

	/**
	 * Classifies ordinary main-shell key messages into app-level shortcut commands.
	 *
	 * Modal contexts deliberately return `None` so modal dialogs keep their normal
	 * Enter/Escape/mnemonic handling.
	 */
	inline ECommand ClassifyMainKeyMessage(UINT uMessage, WPARAM wParam, bool bCtrlDown, bool bAltDown, bool bModalContext)
	{
		UNREFERENCED_PARAMETER(uMessage);
		UNREFERENCED_PARAMETER(wParam);
		UNREFERENCED_PARAMETER(bCtrlDown);
		UNREFERENCED_PARAMETER(bAltDown);
		UNREFERENCED_PARAMETER(bModalContext);
		return ECommand::None;
	}

	/**
	 * Classifies `SC_KEYMENU` events forwarded from modeless child panes.
	 *
	 * EMULE_KEYBOARD_SHORTCUT: Alt+X is reserved for app exit; Alt+U owns the
	 * floating hotmenu; Alt+T owns the Tools popup.
	 */
	inline ECommand ClassifySystemKeyMenu(UINT nID, LPARAM lParam, bool bModalContext)
	{
		if (bModalContext || (nID & 0xFFF0) != SC_KEYMENU)
			return ECommand::None;

		const TCHAR ch = NormalizeAsciiShortcutChar(static_cast<TCHAR>(lParam));
		if (ch == _T('x'))
			return ECommand::ExitApp;
		if (ch == _T('u'))
			return ECommand::ShowHotMenu;
		if (ch == _T('t'))
			return ECommand::ShowToolsMenu;
		return ECommand::None;
	}

	/**
	 * Classifies Search-pane local `SC_KEYMENU` mnemonics.
	 *
	 * App-level mnemonics such as Alt+X and Alt+U are deliberately not returned
	 * here; callers must give the main shell first refusal before using this map.
	 */
	inline ESearchCommand ClassifySearchKeyMenu(UINT nID, LPARAM lParam, bool bModalContext)
	{
		if (bModalContext || (nID & 0xFFF0) != SC_KEYMENU)
			return ESearchCommand::None;

		switch (NormalizeAsciiShortcutChar(static_cast<TCHAR>(lParam))) {
		case _T('n'):
			return ESearchCommand::FocusName;
		case _T('y'):
			return ESearchCommand::FocusType;
		case _T('d'):
			return ESearchCommand::FocusMethod;
		case _T('g'):
			return ESearchCommand::StartSearch;
		case _T('e'):
			return ESearchCommand::SearchMore;
		case _T('r'):
			return ESearchCommand::ResetSearch;
		case _T('l'):
			return ESearchCommand::CancelSearch;
		default:
			return ESearchCommand::None;
		}
	}
}
