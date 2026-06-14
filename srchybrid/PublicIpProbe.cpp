//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.

#include "stdafx.h"
#include "PublicIpProbe.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <ws2tcpip.h>
#include "BindInterfaceSocketSeams.h"
#include "emule.h"
#include "IPv4AddressSeams.h"
#include "Log.h"
#include "Preferences.h"
#include "PublicIpProbeSeams.h"
#include "StunProbeSeams.h"
#include "otherfunctions.h"

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

	struct SAsyncProbeState
	{
		std::atomic<long> nRefs{1};
		std::atomic<long> nRemaining{0};
		std::atomic<bool> bCompleted{false};
		std::mutex mutex;
		HWND hNotifyWnd = NULL;
		UINT uNotifyMessage = 0;
		uint32 uGeneration = 0;
		CString strPurpose;
		SProbeContext context;
		CString strAttempts;
	};

	struct SProviderProbeContext
	{
		SAsyncProbeState* pState = NULL;
		PublicIpProbeSeams::SPublicIpv4ProbeProvider provider = {};
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

	bool FetchStunServer(
		const StunProbeSeams::SStunIpv4ProbeServer& server,
		const SProbeContext& context,
		uint32& ruPublicAddress,
		CString& rstrError)
	{
		ruPublicAddress = 0;
		rstrError.Empty();

		addrinfo hints = {};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_protocol = IPPROTO_UDP;
		CAddrInfoHandle addresses;
		const int nLookup = getaddrinfo(server.pszHost, server.pszPort, &hints, addresses.Out());
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

		// Use the first resolved address only; resilience comes from racing several
		// servers (StartBoundStunIpv4Probe), not from serial per-address retries --
		// matching the libtorrent and rust probes.
		addrinfo* const pAddress = addresses.Get();
		CSocketHandle socketHandle;
		socketHandle.Reset(socket(pAddress->ai_family, pAddress->ai_socktype, pAddress->ai_protocol));
		if (!socketHandle) {
			rstrError.Format(_T("socket failed (%d)"), WSAGetLastError());
			return false;
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
			rstrError.Format(_T("IP_UNICAST_IF failed (%d)"), nBindInterfaceError);
			return false;
		}
		if (bind(socketHandle.Get(), reinterpret_cast<const sockaddr*>(&bindAddress), sizeof bindAddress) == SOCKET_ERROR) {
			rstrError.Format(_T("bind failed (%d)"), WSAGetLastError());
			return false;
		}
		// WHY: connect() the UDP socket to the STUN server so the kernel drops
		// datagrams from any other source. For a security gate this stops an
		// off-path host from injecting a spoofed Binding response, and lets us
		// use send()/recv() without inspecting the sender.
		if (connect(socketHandle.Get(), pAddress->ai_addr, static_cast<int>(pAddress->ai_addrlen)) == SOCKET_ERROR) {
			rstrError.Format(_T("connect failed (%d)"), WSAGetLastError());
			return false;
		}

		// 96-bit random transaction id (house RNG); the response is matched
		// against it plus the magic cookie before we trust the mapped address.
		uint8 txid[StunProbeSeams::kStunTransactionIdLen];
		for (size_t i = 0; i < sizeof txid; i += sizeof(uint32)) {
			const uint32 uRandom = GetRandomUInt32();
			memcpy(txid + i, &uRandom, sizeof uRandom);
		}
		uint8 request[StunProbeSeams::kStunHeaderLen];
		StunProbeSeams::BuildStunBindingRequest(txid, request);

		// Single send, no retransmit: resilience comes from racing several servers
		// (StartBoundStunIpv4Probe), not from per-socket retries.
		const int nSent = send(socketHandle.Get(), reinterpret_cast<const char*>(request), static_cast<int>(sizeof request), 0);
		if (nSent != static_cast<int>(sizeof request)) {
			rstrError.Format(_T("send failed (%d)"), WSAGetLastError());
			return false;
		}

		uint8 response[1500];
		const int nReceived = recv(socketHandle.Get(), reinterpret_cast<char*>(response), static_cast<int>(sizeof response), 0);
		if (nReceived <= 0) {
			rstrError.Format(_T("recv failed (%d)"), WSAGetLastError());
			return false;
		}
		if (StunProbeSeams::TryParseStunIpv4Response(response, static_cast<size_t>(nReceived), txid, ruPublicAddress))
			return true;

		rstrError = _T("response was not a valid STUN binding success");
		return false;
	}

	bool BuildProbeContext(SProbeContext& rContext)
	{
		if (thePrefs.GetActiveBindInterface().IsEmpty()
			|| thePrefs.GetActiveBindAddressResolveResult() != BARR_Resolved
			|| thePrefs.GetActiveBindInterfaceIndex() == 0
			|| thePrefs.GetBindAddrA() == NULL
			|| *thePrefs.GetBindAddrA() == '\0')
			return false;

		rContext.strBindInterfaceName = thePrefs.GetActiveBindInterfaceName();
		rContext.strBindAddress = thePrefs.GetBindAddrA();
		rContext.dwBindInterfaceIndex = thePrefs.GetActiveBindInterfaceIndex();
		return true;
	}

	void AddRef(SAsyncProbeState* pState)
	{
		pState->nRefs.fetch_add(1, std::memory_order_relaxed);
	}

	void Release(SAsyncProbeState* pState)
	{
		if (pState->nRefs.fetch_sub(1, std::memory_order_acq_rel) == 1)
			delete pState;
	}

	std::unique_ptr<PublicIpProbe::SBoundPublicIpv4ProbeResult> CreateProbeResult(const SAsyncProbeState& state)
	{
		std::unique_ptr<PublicIpProbe::SBoundPublicIpv4ProbeResult> pResult(new PublicIpProbe::SBoundPublicIpv4ProbeResult);
		pResult->bAttempted = true;
		pResult->uGeneration = state.uGeneration;
		pResult->strPurpose = state.strPurpose;
		pResult->strBindInterfaceName = state.context.strBindInterfaceName;
		pResult->strBindAddress = state.context.strBindAddress;
		pResult->dwBindInterfaceIndex = state.context.dwBindInterfaceIndex;
		return pResult;
	}

	void PostProbeResult(SAsyncProbeState* pState, std::unique_ptr<PublicIpProbe::SBoundPublicIpv4ProbeResult> pResult)
	{
		if (pState->hNotifyWnd != NULL && ::IsWindow(pState->hNotifyWnd)
			&& ::PostMessage(pState->hNotifyWnd, pState->uNotifyMessage, pResult->uGeneration, reinterpret_cast<LPARAM>(pResult.get())) != FALSE) {
			(void)pResult.release();
		}
	}

	UINT AFX_CDECL ProviderProbeThreadProc(LPVOID pvContext)
	{
		std::unique_ptr<SProviderProbeContext> pContext(static_cast<SProviderProbeContext*>(pvContext));
		SAsyncProbeState* pState = pContext->pState;

		CStringA strPublicAddress;
		CString strError;
		bool bSuccess = false;
		uint32 uAddress = 0;
		if (FetchProvider(pContext->provider, pState->context, strPublicAddress, strError)) {
			if (!IPv4AddressSeams::TryParseIPv4Address(CString(CA2T(strPublicAddress)), uAddress)) {
				strError = _T("response did not contain a strict IPv4 literal");
			} else {
				bSuccess = true;
			}
		}

		if (bSuccess) {
			if (!pState->bCompleted.exchange(true, std::memory_order_acq_rel)) {
				std::unique_ptr<PublicIpProbe::SBoundPublicIpv4ProbeResult> pResult = CreateProbeResult(*pState);
				pResult->bSucceeded = true;
				pResult->strPublicAddress = strPublicAddress;
				pResult->uPublicAddress = uAddress;
				pResult->strProviderUrl = CString(pContext->provider.pszUrl);
				PostProbeResult(pState, std::move(pResult));
			}
			DebugLog(_T("VPN public IPv4 probe: provider=%s bindInterface=%s localBind=%S ifIndex=%lu publicIp=%S"),
				(LPCTSTR)CString(pContext->provider.pszUrl),
				(LPCTSTR)pState->context.strBindInterfaceName,
				pState->context.strBindAddress.GetString(),
				pState->context.dwBindInterfaceIndex,
				strPublicAddress.GetString());
			Release(pState);
			return 0;
		}

		CString strAttempt;
		strAttempt.Format(_T("%S: %s"), pContext->provider.pszUrl, (LPCTSTR)strError);
		{
			std::lock_guard<std::mutex> lock(pState->mutex);
			if (!pState->strAttempts.IsEmpty())
				pState->strAttempts.Append(_T("; "));
			pState->strAttempts.Append(strAttempt);
		}
		if (pState->nRemaining.fetch_sub(1, std::memory_order_acq_rel) == 1
			&& !pState->bCompleted.exchange(true, std::memory_order_acq_rel)) {
			std::unique_ptr<PublicIpProbe::SBoundPublicIpv4ProbeResult> pResult = CreateProbeResult(*pState);
			{
				std::lock_guard<std::mutex> lock(pState->mutex);
				pResult->strAttempts = pState->strAttempts;
			}
			pResult->strError = pResult->strAttempts.IsEmpty() ? CString(_T("all public IPv4 providers failed")) : pResult->strAttempts;
			DebugLogWarning(_T("VPN public IPv4 probe failed: bindInterface=%s localBind=%S attempts=%s"),
				(LPCTSTR)pState->context.strBindInterfaceName,
				pState->context.strBindAddress.GetString(),
				(LPCTSTR)pResult->strAttempts);
			PostProbeResult(pState, std::move(pResult));
		}
		Release(pState);
		return 1;
	}

	struct SStunServerProbeContext
	{
		SAsyncProbeState* pState = NULL;
		StunProbeSeams::SStunIpv4ProbeServer server = {};
	};

	UINT AFX_CDECL StunServerProbeThreadProc(LPVOID pvContext)
	{
		std::unique_ptr<SStunServerProbeContext> pContext(static_cast<SStunServerProbeContext*>(pvContext));
		SAsyncProbeState* pState = pContext->pState;

		CStringA strPublicAddress;
		CString strError;
		bool bSuccess = false;
		uint32 uAddress = 0;
		if (FetchStunServer(pContext->server, pState->context, uAddress, strError)) {
			bSuccess = true;
			// Format for display/logging from the scalar (same byte order as
			// IPv4AddressSeams::FormatIPv4Address); uPublicAddress is used as-is.
			strPublicAddress.Format("%u.%u.%u.%u",
				uAddress & 0xff, (uAddress >> 8) & 0xff, (uAddress >> 16) & 0xff, (uAddress >> 24) & 0xff);
		}

		if (bSuccess) {
			if (!pState->bCompleted.exchange(true, std::memory_order_acq_rel)) {
				std::unique_ptr<PublicIpProbe::SBoundPublicIpv4ProbeResult> pResult = CreateProbeResult(*pState);
				pResult->bSucceeded = true;
				pResult->strPublicAddress = strPublicAddress;
				pResult->uPublicAddress = uAddress;
				pResult->strProviderUrl = CString(pContext->server.pszLabel);
				PostProbeResult(pState, std::move(pResult));
			}
			DebugLog(_T("VPN STUN public IPv4 probe: server=%s bindInterface=%s localBind=%S ifIndex=%lu publicIp=%S"),
				(LPCTSTR)CString(pContext->server.pszLabel),
				(LPCTSTR)pState->context.strBindInterfaceName,
				pState->context.strBindAddress.GetString(),
				pState->context.dwBindInterfaceIndex,
				strPublicAddress.GetString());
			Release(pState);
			return 0;
		}

		CString strAttempt;
		strAttempt.Format(_T("%S: %s"), pContext->server.pszLabel, (LPCTSTR)strError);
		{
			std::lock_guard<std::mutex> lock(pState->mutex);
			if (!pState->strAttempts.IsEmpty())
				pState->strAttempts.Append(_T("; "));
			pState->strAttempts.Append(strAttempt);
		}
		if (pState->nRemaining.fetch_sub(1, std::memory_order_acq_rel) == 1
			&& !pState->bCompleted.exchange(true, std::memory_order_acq_rel)) {
			std::unique_ptr<PublicIpProbe::SBoundPublicIpv4ProbeResult> pResult = CreateProbeResult(*pState);
			{
				std::lock_guard<std::mutex> lock(pState->mutex);
				pResult->strAttempts = pState->strAttempts;
			}
			pResult->strError = pResult->strAttempts.IsEmpty() ? CString(_T("all STUN servers failed")) : pResult->strAttempts;
			DebugLogWarning(_T("VPN STUN public IPv4 probe failed: bindInterface=%s localBind=%S attempts=%s"),
				(LPCTSTR)pState->context.strBindInterfaceName,
				pState->context.strBindAddress.GetString(),
				(LPCTSTR)pResult->strAttempts);
			PostProbeResult(pState, std::move(pResult));
		}
		Release(pState);
		return 1;
	}
}

void PublicIpProbe::StartBoundPublicIpv4Probe()
{
	CString strError;
	if (!StartBoundPublicIpv4Probe(NULL, 0, 0, _T("log"), strError) && !strError.IsEmpty())
		DebugLogWarning(_T("VPN public IPv4 probe failed: %s"), (LPCTSTR)strError);
}

bool PublicIpProbe::StartBoundPublicIpv4Probe(HWND hNotifyWnd, UINT uNotifyMessage, uint32 uGeneration, const CString& strPurpose, CString& rstrError)
{
	SProbeContext context;
	if (!BuildProbeContext(context)) {
		rstrError = _T("active bind interface is not resolved");
		return false;
	}

	size_t nProviderCount = 0;
	const PublicIpProbeSeams::SPublicIpv4ProbeProvider* pProviders = PublicIpProbeSeams::GetPublicIpv4ProbeProviders(nProviderCount);
	if (nProviderCount == 0) {
		rstrError = _T("no public IPv4 providers are configured");
		return false;
	}

	SAsyncProbeState* pState = new SAsyncProbeState;
	pState->hNotifyWnd = hNotifyWnd;
	pState->uNotifyMessage = uNotifyMessage;
	pState->uGeneration = uGeneration;
	pState->strPurpose = strPurpose;
	pState->context = context;
	pState->nRemaining = static_cast<long>(nProviderCount);

	long nStarted = 0;
	for (size_t i = 0; i < nProviderCount; ++i) {
		std::unique_ptr<SProviderProbeContext> pProviderContext(new SProviderProbeContext);
		pProviderContext->pState = pState;
		pProviderContext->provider = pProviders[i];
		AddRef(pState);
		CWinThread* pThread = AfxBeginThread(ProviderProbeThreadProc, pProviderContext.get(), THREAD_PRIORITY_BELOW_NORMAL, 0, 0, NULL);
		if (pThread == NULL) {
			Release(pState);
			pState->nRemaining.fetch_sub(1, std::memory_order_acq_rel);
			{
				std::lock_guard<std::mutex> lock(pState->mutex);
				if (!pState->strAttempts.IsEmpty())
					pState->strAttempts.Append(_T("; "));
				pState->strAttempts.AppendFormat(_T("%S: could not start worker"), pProviders[i].pszUrl);
			}
			continue;
		}
		(void)pProviderContext.release();
		++nStarted;
	}

	if (nStarted == 0) {
		rstrError = pState->strAttempts.IsEmpty() ? CString(_T("could not start public IPv4 probe workers")) : pState->strAttempts;
		Release(pState);
		return false;
	}
	Release(pState);
	return true;
}

void PublicIpProbe::StartBoundStunIpv4Probe()
{
	CString strError;
	if (!StartBoundStunIpv4Probe(NULL, 0, 0, _T("log"), strError) && !strError.IsEmpty())
		DebugLogWarning(_T("VPN STUN public IPv4 probe failed: %s"), (LPCTSTR)strError);
}

bool PublicIpProbe::StartBoundStunIpv4Probe(HWND hNotifyWnd, UINT uNotifyMessage, uint32 uGeneration, const CString& strPurpose, CString& rstrError)
{
	SProbeContext context;
	if (!BuildProbeContext(context)) {
		rstrError = _T("active bind interface is not resolved");
		return false;
	}

	size_t nServerCount = 0;
	const StunProbeSeams::SStunIpv4ProbeServer* pServers = StunProbeSeams::GetStunIpv4ProbeServers(nServerCount);
	if (nServerCount == 0) {
		rstrError = _T("no STUN servers are configured");
		return false;
	}

	SAsyncProbeState* pState = new SAsyncProbeState;
	pState->hNotifyWnd = hNotifyWnd;
	pState->uNotifyMessage = uNotifyMessage;
	pState->uGeneration = uGeneration;
	pState->strPurpose = strPurpose;
	pState->context = context;
	pState->nRemaining = static_cast<long>(nServerCount);

	long nStarted = 0;
	for (size_t i = 0; i < nServerCount; ++i) {
		std::unique_ptr<SStunServerProbeContext> pServerContext(new SStunServerProbeContext);
		pServerContext->pState = pState;
		pServerContext->server = pServers[i];
		AddRef(pState);
		CWinThread* pThread = AfxBeginThread(StunServerProbeThreadProc, pServerContext.get(), THREAD_PRIORITY_BELOW_NORMAL, 0, 0, NULL);
		if (pThread == NULL) {
			Release(pState);
			pState->nRemaining.fetch_sub(1, std::memory_order_acq_rel);
			{
				std::lock_guard<std::mutex> lock(pState->mutex);
				if (!pState->strAttempts.IsEmpty())
					pState->strAttempts.Append(_T("; "));
				pState->strAttempts.AppendFormat(_T("%S: could not start worker"), pServers[i].pszLabel);
			}
			continue;
		}
		(void)pServerContext.release();
		++nStarted;
	}

	if (nStarted == 0) {
		rstrError = pState->strAttempts.IsEmpty() ? CString(_T("could not start STUN probe workers")) : pState->strAttempts;
		Release(pState);
		return false;
	}
	Release(pState);
	return true;
}
