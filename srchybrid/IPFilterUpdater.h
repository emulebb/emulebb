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

/**
 * @brief Updates the local IP-filter file through shared safe download, promotion, and live reload paths.
 */
class CIPFilterUpdater
{
public:
	CIPFilterUpdater();
	~CIPFilterUpdater();

	/**
	 * @brief Runs the interactive manual update path for the supplied URL.
	 */
	bool UpdateFromUrlInteractive(const CString& strUrl);
	/**
	 * @brief Queues one automatic background update when the configured interval is due.
	 */
	bool QueueBackgroundRefresh();
	/**
	 * @brief Clears queued state and reloads the live filter after a background worker completes.
	 */
	void HandleBackgroundRefreshResult(bool bUpdated);
	/**
	 * @brief Returns whether an automatic IP-filter update worker is currently queued or running.
	 */
	bool IsRefreshQueued() const						{ return m_lBackgroundRefreshQueued != 0; }
	/**
	 * @brief Returns whether the automatic IP-filter update interval is due at the supplied time.
	 */
	static bool IsAutomaticRefreshDue(__time64_t tNow);

private:
	struct SBackgroundRefreshContext;

	static UINT AFX_CDECL BackgroundRefreshThread(LPVOID pParam);

	volatile LONG m_lBackgroundRefreshQueued;
};
