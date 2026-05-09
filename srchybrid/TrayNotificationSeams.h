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

/** Testable tray notification policy shared by the UI and release hardening tests. */
namespace TrayNotificationSeams
{
	/** Notification display modes that influence tray icon visibility. */
	enum class ENotifierDisplayMode
	{
		CustomPopup,
		WindowsToast,
		TrayBalloon
	};

	/** Inputs needed to decide whether the shell tray icon should be present. */
	struct CTrayVisibilityState
	{
		bool bAlwaysShowTrayIcon = false;
		ENotifierDisplayMode eNotifierDisplayMode = ENotifierDisplayMode::CustomPopup;
		bool bTrayBalloonFallbackForSession = false;
		bool bMainWindowVisible = true;
		bool bMinimizeToTray = false;
	};

	/** Returns true when the current UI and notification settings require a tray icon. */
	inline bool ShouldTrayIconBeVisible(const CTrayVisibilityState &state)
	{
		return state.bAlwaysShowTrayIcon
			|| state.eNotifierDisplayMode == ENotifierDisplayMode::TrayBalloon
			|| (state.eNotifierDisplayMode == ENotifierDisplayMode::WindowsToast && state.bTrayBalloonFallbackForSession)
			|| (!state.bMainWindowVisible && state.bMinimizeToTray);
	}
}
