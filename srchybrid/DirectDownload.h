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

#include <afxmt.h>
#include <cstddef>
#include <memory>
#include <wininet.h>

/**
 * @brief Shared helpers for noninteractive direct HTTP(S) downloads used by background data refreshers.
 */
namespace DirectDownload
{
	class CRegisteredInternetHandle;

	/**
	 * @brief Lets an owner abort an in-flight direct WinInet download by closing its active handles.
	 */
	class CDownloadCancellation
	{
	public:
		CDownloadCancellation() noexcept;
		CDownloadCancellation(const CDownloadCancellation&) = delete;
		CDownloadCancellation& operator=(const CDownloadCancellation&) = delete;

		void Cancel() noexcept;
		bool IsCancelled() const noexcept;

		enum class InternetHandleSlot : unsigned char
		{
			Session = 0,
			Connection,
			Request,
			Count
		};

	private:
		friend class CRegisteredInternetHandle;

		bool RegisterHandle(InternetHandleSlot eSlot, HINTERNET hInternet) noexcept;
		bool ReleaseHandle(InternetHandleSlot eSlot, HINTERNET hInternet) noexcept;

		mutable CCriticalSection m_lock;
		HINTERNET m_hInternet[static_cast<size_t>(InternetHandleSlot::Count)];
		bool m_bCancelled;
	};

	/**
	 * @brief Reserves one unique temporary path inside a directory.
	 */
	bool CreateTempPathInDirectory(const CString& strDirectory, LPCTSTR pszPrefix, CString& strTempPath, CString& strError);
	/**
	 * @brief Downloads a URL directly to a file with bounded background timeouts and no proxy UI.
	 */
	bool DownloadUrlToFile(const CString& strUrl, const CString& strTargetPath, CString& strError);
	/**
	 * @brief Downloads a URL directly to a file and lets the owner close active WinInet handles for hard cancellation.
	 */
	bool DownloadUrlToFile(const CString& strUrl, const CString& strTargetPath, CString& strError, const std::shared_ptr<CDownloadCancellation>& pCancellation);
}
