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

#include <WinSock2.h>
#include <Ws2tcpip.h>
#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <memory>

#include <atlstr.h>

namespace AsyncDnsResolveSeams
{
	/**
	 * @brief Immutable work item for one asynchronous IPv4 hostname lookup.
	 */
	struct SHostnameResolveWork
	{
		HWND hTargetWnd = NULL;
		UINT uCompletionMessage = 0;
		WPARAM wParam = 0;
		UINT_PTR uRequestId = 0;
		CStringA strHostAddress;
		int nSocketType = SOCK_STREAM;
		USHORT nHostPort = 0;
	};

	/**
	 * @brief Result posted by a resolver worker back to the owning UI/helper window.
	 */
	struct SHostnameResolveResult
	{
		UINT_PTR uRequestId = 0;
		bool bLookupSucceeded = false;
		bool bHasIpv4Address = false;
		std::uint32_t nAddress = INADDR_NONE;
		int nLookupError = 0;
		SOCKADDR_IN sockAddr = {};
	};

	/**
	 * @brief Allocates a non-zero request id from a scalar UI-thread counter.
	 */
	template <typename TCounter>
	inline TCounter AllocateNonZeroRequestId(TCounter &rNextRequestId)
	{
		TCounter uRequestId = ++rNextRequestId;
		if (uRequestId == 0)
			uRequestId = ++rNextRequestId;
		return uRequestId;
	}

	/**
	 * @brief Allocates a non-zero request id from an atomic cross-thread counter.
	 */
	inline UINT_PTR AllocateNonZeroRequestId(std::atomic<UINT_PTR> &rNextRequestId)
	{
		UINT_PTR uRequestId = ++rNextRequestId;
		if (uRequestId == 0)
			uRequestId = ++rNextRequestId;
		return uRequestId;
	}

	/**
	 * @brief Builds the IPv4 socket address used by legacy connect completion callbacks.
	 */
	inline SOCKADDR_IN BuildIpv4SocketAddress(std::uint32_t nAddress, USHORT nHostPort)
	{
		SOCKADDR_IN sockAddr = {};
		sockAddr.sin_family = AF_INET;
		sockAddr.sin_addr.s_addr = nAddress;
		sockAddr.sin_port = htons(nHostPort);
		return sockAddr;
	}

	/**
	 * @brief Resolves one hostname to the first IPv4 address matching the requested socket type.
	 */
	inline SHostnameResolveResult ResolveHostnameIPv4(const CStringA &rstrHostAddress, int nSocketType, USHORT nHostPort)
	{
		SHostnameResolveResult result;

		addrinfo hints = {};
		hints.ai_family = AF_INET;
		hints.ai_socktype = nSocketType;

		addrinfo *pResult = NULL;
		result.nLookupError = getaddrinfo(rstrHostAddress.GetString(), NULL, &hints, &pResult);
		if (result.nLookupError != 0)
			return result;

		for (addrinfo *pCurrent = pResult; pCurrent != NULL; pCurrent = pCurrent->ai_next) {
			if (pCurrent->ai_family != AF_INET || pCurrent->ai_addr == NULL || pCurrent->ai_addrlen < sizeof(sockaddr_in))
				continue;

			result.nAddress = reinterpret_cast<sockaddr_in*>(pCurrent->ai_addr)->sin_addr.s_addr;
			result.sockAddr = BuildIpv4SocketAddress(result.nAddress, nHostPort);
			result.bLookupSucceeded = true;
			result.bHasIpv4Address = true;
			break;
		}

		freeaddrinfo(pResult);
		return result;
	}

	/**
	 * @brief Converts a resolver result to the legacy async-socket connect error.
	 */
	inline int GetLegacyHostResolveError(const SHostnameResolveResult &rResult)
	{
		return rResult.bHasIpv4Address ? 0 : WSAHOST_NOT_FOUND;
	}

	/**
	 * @brief Posts a heap-owned result and releases it only after successful delivery.
	 */
	inline bool PostOwnedResult(HWND hTargetWnd, UINT uMessage, WPARAM wParam, std::unique_ptr<SHostnameResolveResult> &rpResult)
	{
		if (hTargetWnd != NULL && ::PostMessage(hTargetWnd, uMessage, wParam, reinterpret_cast<LPARAM>(rpResult.get())) != FALSE) {
			(void)rpResult.release();
			return true;
		}
		rpResult.reset();
		return false;
	}

	/**
	 * @brief Worker entrypoint shared by legacy async-socket and server-UDP DNS callers.
	 */
	inline UINT __cdecl HostnameResolveThreadProc(LPVOID pParam)
	{
		std::unique_ptr<SHostnameResolveWork> pWork(static_cast<SHostnameResolveWork*>(pParam));
		if (pWork == NULL)
			return 0;

		std::unique_ptr<SHostnameResolveResult> pResult(new SHostnameResolveResult(ResolveHostnameIPv4(
			pWork->strHostAddress,
			pWork->nSocketType,
			pWork->nHostPort)));
		pResult->uRequestId = pWork->uRequestId;

		(void)PostOwnedResult(pWork->hTargetWnd, pWork->uCompletionMessage, pWork->wParam, pResult);
		return 0;
	}
}
