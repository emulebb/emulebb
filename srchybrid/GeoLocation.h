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

#include <map>
#include <memory>

class CDC;
class CImageList;
namespace BackgroundRefreshSeams { struct SRefreshState; }

/**
 * @brief One cached geolocation lookup result derived from the MMDB database.
 */
struct SGeoLocationRecord
{
	CString strCountryCode;
	CString strCountryName;
	CString strCityName;
	int iFlagImageIndex;
	bool bResolved;

	SGeoLocationRecord();
};

/**
 * @brief Resolves IPv4 addresses to country and city data from a local DB-IP MMDB.
 */
class CGeoLocation
{
public:
	CGeoLocation();
	~CGeoLocation();

	/**
	 * @brief Loads the configured local MMDB when geolocation is enabled.
	 */
	void Load();
	/**
	 * @brief Releases the loaded MMDB and all cached lookup state.
	 */
	void Unload();
	/**
	 * @brief Starts one post-startup background refresh when the local DB is missing or stale.
	 */
	void QueueBackgroundRefresh();
	/**
	 * @brief Starts one user-requested background refresh regardless of the automatic interval.
	 */
	void QueueManualRefresh();
	/**
	 * @brief Clears the queued refresh state and reloads after a completed background refresh.
	 */
	void HandleBackgroundRefreshResult(bool bUpdated);
	/**
	 * @brief Returns the cached geolocation record for one IPv4 address.
	 */
	const SGeoLocationRecord& Lookup(uint32 dwIP) const;
	/**
	 * @brief Returns the UI display string for one IPv4 address.
	 */
	CString GetDisplayText(uint32 dwIP) const;
	/**
	 * @brief Returns the lazy-loaded flag image list.
	 */
	CImageList* GetFlagImageList();
	/**
	 * @brief Returns the lazy-loaded flag index for one IPv4 address.
	 */
	int GetFlagImageIndex(uint32 dwIP);
	/**
	 * @brief Draws the lazy-loaded flag icon for one IPv4 address when available.
	 */
	bool DrawFlag(CDC& dc, uint32 dwIP, const POINT& point);
	/**
	 * @brief Invalidates the visible geo-aware windows after a DB reload.
	 */
	void RefreshVisibleWindows() const;
	/**
	 * @brief Returns whether a background refresh worker is already running.
	 */
	bool IsRefreshQueued() const;

private:
	struct CStringLess
	{
		bool operator()(const CString& left, const CString& right) const
		{
			return left.Compare(right) < 0;
		}
	};

	class CMmdbCityDatabase;

	struct SBackgroundRefreshContext
	{
		CString strDownloadUrl;
		CString strArchiveTempPath;
		CString strDatabaseTempPath;
		CString strInstallPath;
		HWND hNotifyWnd;
		std::shared_ptr<BackgroundRefreshSeams::SRefreshState> pRefreshState;
		bool bProxyEnabled;
	};

	bool IsEnabled() const;
	bool IsPrivateIPv4(uint32 dwIP) const;
	/**
	 * @brief Returns whether an automatic geolocation DB refresh is due under the configured day interval.
	 */
	bool IsAutomaticRefreshDue() const;
	CString ExpandConfiguredUpdateUrlTemplate() const;
	CString GetDatabaseFilePath() const;
	CString FormatDisplayText(const SGeoLocationRecord& record) const;
	int GetFlagImageIndexForCode(const CString& strCountryCode);
	void ClearCache();
	/**
	 * @brief Starts one background refresh when the current request type and due state allow it.
	 */
	bool QueueRefresh(bool bForce, bool bUserInitiated);

	static UINT AFX_CDECL BackgroundRefreshThread(LPVOID pParam);

	CMmdbCityDatabase* m_pDatabase;
	__time64_t m_tBuildEpoch;
	CImageList* m_pFlagImageList;
	mutable SGeoLocationRecord m_defaultRecord;
	mutable std::map<uint32, SGeoLocationRecord> m_cacheByIp;
	std::map<CString, int, CStringLess> m_flagIndexByCode;
	std::shared_ptr<BackgroundRefreshSeams::SRefreshState> m_pBackgroundRefreshState;
};
