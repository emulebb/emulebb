//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.

#include "stdafx.h"
#include "PublicIpProbe.h"

#include <memory>
#include <ws2tcpip.h>
#include "BindInterfaceSocketSeams.h"
#include "emule.h"
#include "IPv4AddressSeams.h"
#include "Log.h"
#include "Preferences.h"
#include "PublicIpProbeSeams.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	constexpr int kProbeTimeoutMs = 5000;
	constexpr size_t kMaxProbeResponseBytes = 8192;

	struct SProbeContext
	{
		CString strBindInterfaceName;
		CStringA strBindAddress;
		DWORD dwBindInterfaceIndex = 0;
	};

	class CSocketHandle
	{
	public:
		CSocketHandle() noexcept
			: m_hSocket(INVALID_SOCKET)
		{
		}

		CSocketHandle(const CSocketHandle&) = delete;
		CSocketHandle& operator=(const CSocketHandle&) = delete;

		~CSocketHandle()
		{
			Reset();
		}

		explicit operator bool() const noexcept
		{
			return m_hSocket != INVALID_SOCKET;
		}

		SOCKET Get() const noexcept
		{
			return m_hSocket;
		}

		void Reset(SOCKET hSocket = INVALID_SOCKET) noexcept
		{
			if (m_hSocket != INVALID_SOCKET && m_hSocket != hSocket)
				closesocket(m_hSocket);
			m_hSocket = hSocket;
		}

	private:
		SOCKET m_hSocket;
	};

	class CAddrInfoHandle
	{
	public:
		CAddrInfoHandle() noexcept
			: m_pAddressInfo(NULL)
		{
		}

		CAddrInfoHandle(const CAddrInfoHandle&) = delete;
		CAddrInfoHandle& operator=(const CAddrInfoHandle&) = delete;

		~CAddrInfoHandle()
		{
			Reset();
		}

		addrinfo* Get() const noexcept
		{
			return m_pAddressInfo;
		}

		addrinfo** Out() noexcept
		{
			Reset();
			return &m_pAddressInfo;
		}

		void Reset(addrinfo* pAddressInfo = NULL) noexcept
		{
			if (m_pAddressInfo != NULL && m_pAddressInfo != pAddressInfo)
				freeaddrinfo(m_pAddressInfo);
			m_pAddressInfo = pAddressInfo;
		}

	private:
		addrinfo* m_pAddressInfo;
	};

	bool SendAll(const SOCKET hSocket, const CStringA& strRequest, CString& rstrError)
	{
		const char* pszCursor = strRequest.GetString();
		int nRemaining = strRequest.GetLength();
		while (nRemaining > 0) {
			const int nSent = send(hSocket, pszCursor, nRemaining, 0);
			if (nSent <= 0) {
				rstrError.Format(_T("send failed (%d)"), WSAGetLastError());
				return false;
			}
			pszCursor += nSent;
			nRemaining -= nSent;
		}
		return true;
	}

	bool FetchProvider(
		const PublicIpProbeSeams::SPublicIpv4ProbeProvider& provider,
		const SProbeContext& context,
		CStringA& rstrPublicAddress,
		CString& rstrError)
	{
		rstrPublicAddress.Empty();
		rstrError.Empty();

		addrinfo hints = {};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		CAddrInfoHandle addresses;
		const int nLookup = getaddrinfo(provider.pszHost, "80", &hints, addresses.Out());
		if (nLookup != 0 || addresses.Get() == NULL) {
			rstrError.Format(_T("getaddrinfo failed (%d)"), nLookup);
			return false;
		}

		sockaddr_in bindAddress = {};
		bindAddress.sin_family = AF_INET;
		bindAddress.sin_port = 0;
		if (inet_pton(AF_INET, context.strBindAddress, &bindAddress.sin_addr) != 1) {
			rstrError.Format(_T("invalid bind address %S"), context.strBindAddress.GetString());
			return false;
		}

		CString strLastError;
		for (addrinfo* pAddress = addresses.Get(); pAddress != NULL; pAddress = pAddress->ai_next) {
			CSocketHandle socketHandle;
			socketHandle.Reset(socket(pAddress->ai_family, pAddress->ai_socktype, pAddress->ai_protocol));
			if (!socketHandle) {
				strLastError.Format(_T("socket failed (%d)"), WSAGetLastError());
				continue;
			}

			setsockopt(socketHandle.Get(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&kProbeTimeoutMs), sizeof kProbeTimeoutMs);
			setsockopt(socketHandle.Get(), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&kProbeTimeoutMs), sizeof kProbeTimeoutMs);

			int nBindInterfaceError = 0;
			if (!BindInterfaceSocketSeams::ApplyIpv4UnicastInterfaceOption(
				socketHandle.Get(),
				AF_INET,
				true,
				true,
				context.dwBindInterfaceIndex,
				&nBindInterfaceError)) {
				strLastError.Format(_T("IP_UNICAST_IF failed (%d)"), nBindInterfaceError);
				continue;
			}
			if (bind(socketHandle.Get(), reinterpret_cast<const sockaddr*>(&bindAddress), sizeof bindAddress) == SOCKET_ERROR) {
				strLastError.Format(_T("bind failed (%d)"), WSAGetLastError());
				continue;
			}
			if (connect(socketHandle.Get(), pAddress->ai_addr, static_cast<int>(pAddress->ai_addrlen)) == SOCKET_ERROR) {
				strLastError.Format(_T("connect failed (%d)"), WSAGetLastError());
				continue;
			}

			CStringA strRequest;
			strRequest.Format(
				"GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: eMuleBB-public-ip-probe\r\nAccept: text/plain\r\nConnection: close\r\n\r\n",
				provider.pszPath,
				provider.pszHost);
			if (!SendAll(socketHandle.Get(), strRequest, strLastError))
				continue;

			CStringA strResponse;
			char buffer[1024] = {};
			for (;;) {
				const int nReceived = recv(socketHandle.Get(), buffer, sizeof buffer, 0);
				if (nReceived == 0)
					break;
				if (nReceived < 0) {
					strLastError.Format(_T("recv failed (%d)"), WSAGetLastError());
					break;
				}
				if (static_cast<size_t>(strResponse.GetLength()) + static_cast<size_t>(nReceived) > kMaxProbeResponseBytes) {
					strLastError = _T("response too large");
					break;
				}
				strResponse.Append(buffer, nReceived);
			}
			if (PublicIpProbeSeams::TryParsePublicIpv4HttpResponse(strResponse, rstrPublicAddress))
				return true;
			if (strLastError.IsEmpty())
				strLastError = _T("response did not contain a strict IPv4 literal");
		}

		rstrError = strLastError.IsEmpty() ? CString(_T("all address attempts failed")) : strLastError;
		return false;
	}

	UINT PublicIpv4ProbeThreadProc(LPVOID pvContext)
	{
		std::unique_ptr<SProbeContext> pContext(static_cast<SProbeContext*>(pvContext));
		size_t nProviderCount = 0;
		const PublicIpProbeSeams::SPublicIpv4ProbeProvider* pProviders = PublicIpProbeSeams::GetPublicIpv4ProbeProviders(nProviderCount);
		CString strAttempts;
		for (size_t i = 0; i < nProviderCount; ++i) {
			CStringA strPublicAddress;
			CString strError;
			if (FetchProvider(pProviders[i], *pContext, strPublicAddress, strError)) {
				DebugLog(_T("VPN public IPv4 probe: provider=%S bindInterface=%s localBind=%S ifIndex=%lu publicIp=%S"),
					pProviders[i].pszUrl,
					(LPCTSTR)pContext->strBindInterfaceName,
					pContext->strBindAddress.GetString(),
					pContext->dwBindInterfaceIndex,
					strPublicAddress.GetString());
				return 0;
			}
			if (!strAttempts.IsEmpty())
				strAttempts.Append(_T("; "));
			strAttempts.AppendFormat(_T("%S: %s"), pProviders[i].pszUrl, (LPCTSTR)strError);
		}

		DebugLogWarning(_T("VPN public IPv4 probe failed: bindInterface=%s localBind=%S attempts=%s"),
			(LPCTSTR)pContext->strBindInterfaceName,
			pContext->strBindAddress.GetString(),
			(LPCTSTR)strAttempts);
		return 1;
	}
}

void PublicIpProbe::StartBoundPublicIpv4Probe()
{
	if (thePrefs.GetActiveBindInterface().IsEmpty()
		|| thePrefs.GetActiveBindAddressResolveResult() != BARR_Resolved
		|| thePrefs.GetActiveBindInterfaceIndex() == 0
		|| thePrefs.GetBindAddrA() == NULL
		|| *thePrefs.GetBindAddrA() == '\0')
		return;

	std::unique_ptr<SProbeContext> pContext(new SProbeContext);
	pContext->strBindInterfaceName = thePrefs.GetActiveBindInterfaceName();
	pContext->strBindAddress = thePrefs.GetBindAddrA();
	pContext->dwBindInterfaceIndex = thePrefs.GetActiveBindInterfaceIndex();

	CWinThread* pThread = AfxBeginThread(PublicIpv4ProbeThreadProc, pContext.get(), THREAD_PRIORITY_BELOW_NORMAL, 0, 0, NULL);
	if (pThread == NULL) {
		DebugLogWarning(_T("VPN public IPv4 probe failed: could not start worker for bindInterface=%s localBind=%S ifIndex=%lu"),
			(LPCTSTR)pContext->strBindInterfaceName,
			pContext->strBindAddress.GetString(),
			pContext->dwBindInterfaceIndex);
		return;
	}

	(void)pContext.release();
}
