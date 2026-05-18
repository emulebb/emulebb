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
#include <functional>
#include <memory>
#include <string>
#include <wininet.h>

/**
 * @brief Shared WinInet HTTP(S) GET transfer helper for app-owned HTTP requests and file downloads.
 *
 * This helper intentionally does not bind HTTP traffic to the selected P2P/VPN interface.
 * Requests use direct WinInet routing by default and may opt into Windows WinInet proxy
 * configuration. The legacy eMule internal proxy socket layer is separate and is not used here.
 */
namespace HttpTransfer
{
	class CRegisteredInternetHandle;

	class CTransferCancellation
	{
	public:
		CTransferCancellation() noexcept;
		CTransferCancellation(const CTransferCancellation&) = delete;
		CTransferCancellation& operator=(const CTransferCancellation&) = delete;

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

	struct SRequest
	{
		CString strUrl;
		CString strUserAgent;
		CString strHeaders;
		DWORD dwConnectTimeoutMs = 30000;
		DWORD dwSendTimeoutMs = 30000;
		DWORD dwReceiveTimeoutMs = 30000;
		ULONGLONG ullTotalTimeoutMs = 5ull * 60ull * 1000ull;
		size_t uMaxResponseBytes = 0;
		bool bUseWindowsSystemProxy = false;
	};

	typedef std::function<bool(DWORD dwBytesRead, DWORD dwContentLength)> ProgressCallback;

	bool CreateTempPathInDirectory(const CString& strDirectory, LPCTSTR pszPrefix, CString& strTempPath, CString& strError);
	bool DownloadToFile(const SRequest& request, const CString& strTargetPath, CString& strError, const std::shared_ptr<CTransferCancellation>& pCancellation = std::shared_ptr<CTransferCancellation>(), const ProgressCallback& progressCallback = ProgressCallback());
	bool FetchToMemory(const SRequest& request, std::string& strResponse, CString& strError, const std::shared_ptr<CTransferCancellation>& pCancellation = std::shared_ptr<CTransferCancellation>(), const ProgressCallback& progressCallback = ProgressCallback());
}
