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

#include <memory>
#include <Windows.h>

namespace VersionCheckLaunchSeams
{
	/**
	 * @brief Shared single-flight state for update checks that may outlive the
	 * dialog which started them.
	 */
	struct SQueuedState
	{
		SQueuedState()
			: lQueued(0)
		{
		}

		volatile LONG lQueued;
	};

	/**
	 * @brief Result of handing a worker completion message back to the UI thread.
	 */
	struct SCompletionPostResult
	{
		bool bDelivered = false;
		DWORD dwLastError = ERROR_SUCCESS;
	};

	/**
	 * @brief Atomically claims the single in-flight version-check slot.
	 */
	inline bool TryMarkQueued(volatile LONG &rlQueued)
	{
		return ::InterlockedCompareExchange(&rlQueued, 1, 0) == 0;
	}

	inline bool TryMarkQueued(SQueuedState& rState)
	{
		return TryMarkQueued(rState.lQueued);
	}

	/**
	 * @brief Releases the single in-flight version-check slot.
	 */
	inline void ClearQueued(volatile LONG &rlQueued)
	{
		(void)::InterlockedExchange(&rlQueued, 0);
	}

	inline void ClearQueued(SQueuedState& rState)
	{
		ClearQueued(rState.lQueued);
	}

	/**
	 * @brief Drops owner-visible queued state without invalidating workers which
	 * still own the shared state.
	 */
	inline void ClearQueuedOnOwnerTeardown(const std::shared_ptr<SQueuedState>& pState)
	{
		if (pState)
			ClearQueued(*pState);
	}

	/**
	 * @brief Reports whether a version check is already running.
	 */
	inline bool IsQueued(const volatile LONG &rlQueued)
	{
		return ::InterlockedCompareExchange(const_cast<volatile LONG*>(&rlQueued), 0, 0) != 0;
	}

	inline bool IsQueued(const SQueuedState& rState)
	{
		return IsQueued(rState.lQueued);
	}

	/**
	 * @brief Posts the worker completion message and clears shared queued state
	 * when the owner window can no longer receive it.
	 */
	inline SCompletionPostResult PostCompletion(HWND hNotifyWnd, UINT uMessage, LPARAM lParam, const std::shared_ptr<SQueuedState>& pState)
	{
		SCompletionPostResult result;
		::SetLastError(ERROR_SUCCESS);
		result.bDelivered = hNotifyWnd != NULL && ::PostMessage(hNotifyWnd, uMessage, 0, lParam) != FALSE;
		result.dwLastError = result.bDelivered ? ERROR_SUCCESS : ::GetLastError();
		if (!result.bDelivered)
			ClearQueuedOnOwnerTeardown(pState);
		return result;
	}
}
