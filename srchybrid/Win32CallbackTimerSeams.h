//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( devs@emule-project.net / https://www.emule-project.net )
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

#include "DisplayRefreshSeams.h"

namespace Win32CallbackTimerSeams
{
	using TNoThrowTimerProc = VOID (CALLBACK *)(HWND, UINT, UINT_PTR, DWORD) noexcept;

	enum class ETimerStopResult : unsigned char
	{
		NotRunning,
		Stopped,
		Failed
	};

	/**
	 * @brief Starts a Win32 callback timer owned by the current UI thread message queue.
	 */
	inline bool TryStartNullWindowCallbackTimer(UINT_PTR &ruTimerId, UINT uDelayMilliseconds, TNoThrowTimerProc pfnTimerProc)
	{
		ruTimerId = ::SetTimer(NULL, 0, uDelayMilliseconds, pfnTimerProc);
		return ruTimerId != 0;
	}

	/**
	 * @brief Stops and clears a Win32 callback timer idempotently.
	 */
	inline ETimerStopResult StopNullWindowCallbackTimer(UINT_PTR &ruTimerId)
	{
		if (ruTimerId == 0)
			return ETimerStopResult::NotRunning;

		const UINT_PTR uTimerId = ruTimerId;
		ruTimerId = 0;
		return (::KillTimer(NULL, uTimerId) != FALSE) ? ETimerStopResult::Stopped : ETimerStopResult::Failed;
	}

	/**
	 * @brief Reports whether queue-list refresh work should run from the timer callback.
	 */
	inline bool ShouldDispatchQueueListRefreshTimer(bool bUpdateQueueList, bool bTransferWindowActive, bool bQueueListVisible, bool bAppClosing)
	{
		return bUpdateQueueList && bTransferWindowActive && bQueueListVisible && !bAppClosing;
	}

	/**
	 * @brief Returns the legacy queue-list presentation cadence for compatibility tests.
	 */
	inline UINT GetQueueListRefreshTimerDelayMs(UINT uDesktopUiRefreshIntervalMs)
	{
		const UINT uNormalizedIntervalMs = NormalizeDesktopUiRefreshIntervalMs(uDesktopUiRefreshIntervalMs);
		return uNormalizedIntervalMs == DESKTOP_UI_REFRESH_PAUSED_MS ? DESKTOP_UI_REFRESH_BELOWNORMAL_MS : uNormalizedIntervalMs;
	}

	/**
	 * @brief Reports whether the upload queue timer should run its periodic processing.
	 */
	inline bool ShouldDispatchUploadQueueTimer(bool bAppClosing)
	{
		return !bAppClosing;
	}

	/**
	 * @brief Reports whether the server retry timer has a live controller to dispatch to.
	 */
	inline bool ShouldDispatchServerRetryTimer(bool bHasServerConnect)
	{
		return bHasServerConnect;
	}

	/**
	 * @brief Reports whether the UPnP timeout callback can safely notify the dialog.
	 */
	inline bool ShouldDispatchUPnPTimeoutTimer(bool bHasMainDialog, bool bAppClosing)
	{
		return bHasMainDialog && !bAppClosing;
	}
}
