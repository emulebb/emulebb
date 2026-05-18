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
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#include "StdAfx.h"
#include "Preferences.h"
#include "UPnPImplPcpNatPmp.h"
#include "UPnPImplPcpNatPmpSeams.h"
#include "Log.h"
#include "OtherFunctions.h"

#include <pcpnatpmp.h>

#include <cstdlib>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	constexpr uint32 kPcpMappingLifetimeSeconds = 3600;
	constexpr int kPcpWaitTimeoutMs = 5000;

	bool IsSuccessfulPcpState(pcp_fstate_e state)
	{
		return state == pcp_state_succeeded || state == pcp_state_partial_result || state == pcp_state_short_lifetime_error;
	}

	CString GetPcpStateText(pcp_fstate_e state)
	{
		switch (state) {
			case pcp_state_processing:			return _T("processing");
			case pcp_state_succeeded:			return _T("succeeded");
			case pcp_state_partial_result:		return _T("partial-result");
			case pcp_state_short_lifetime_error:return _T("short-lifetime");
			case pcp_state_failed:				return _T("failed");
			default:							return _T("unknown");
		}
	}
}

CMutex CUPnPImplPcpNatPmp::m_mutBusy;

CUPnPImplPcpNatPmp::CUPnPImplPcpNatPmp()
	: m_pContext()
	, m_pTCPFlow()
	, m_pUDPFlow()
	, m_pTCPWebFlow()
	, m_pDiscoveryThread()
	, m_strSourceAddress()
	, m_sourceAddress()
	, m_nSourceAddressLen()
	, m_bSucceededOnce()
	, m_bAbortDiscovery()
{
	memset(&m_sourceAddress, 0, sizeof(m_sourceAddress));
}

CUPnPImplPcpNatPmp::~CUPnPImplPcpNatPmp()
{
	StopAsyncFind();
	DeletePorts();
	CleanupContext();
}

void CUPnPImplPcpNatPmp::StartDiscovery(uint16 nTCPPort, uint16 nUDPPort, uint16 nTCPWebPort)
{
	DebugLog(_T("Using PCP/NAT-PMP based implementation"));
	GetOldPorts();
	m_nUDPPort = nUDPPort;
	m_nTCPPort = nTCPPort;
	m_nTCPWebPort = nTCPWebPort;
	m_bUPnPPortsForwarded = TRIS_UNKNOWN;
	m_bCheckAndRefresh = false;
	if (!UPnPDiscoveryThreadSeams::IsAbortRequested(m_bAbortDiscovery))
		StartThread();
}

bool CUPnPImplPcpNatPmp::CheckAndRefresh()
{
	if (UPnPDiscoveryThreadSeams::IsAbortRequested(m_bAbortDiscovery) || !m_bSucceededOnce || m_pContext == NULL || m_pTCPFlow == NULL || m_nTCPPort == 0) {
		DebugLog(_T("Not refreshing PCP/NAT-PMP mappings because they don't seem to be active in the first place"));
		return false;
	}

	if (!IsReady()) {
		DebugLog(_T("Not refreshing PCP/NAT-PMP mappings because they are already in the process of being refreshed"));
		return false;
	}

	DebugLog(_T("Checking and refreshing PCP/NAT-PMP mappings"));
	m_bCheckAndRefresh = true;
	StartThread();
	return true;
}

bool CUPnPImplPcpNatPmp::IsReady()
{
	CSingleLock lockTest(&m_mutBusy);
	return lockTest.Lock(0);
}

void CUPnPImplPcpNatPmp::StopAsyncFind()
{
	ReapDiscoveryThreadIfFinished();
	if (m_pDiscoveryThread != NULL) {
		UPnPDiscoveryThreadSeams::RequestAbort(m_bAbortDiscovery); // cooperative shutdown; do not force-kill mapping code while libpcpnatpmp owns state
		const DWORD dwWait = ::WaitForSingleObject(m_pDiscoveryThread->m_hThread, UPnPDiscoveryThreadSeams::kCooperativeStopWaitMs);
		const UPnPDiscoveryThreadSeams::EStopWaitAction eAction = UPnPDiscoveryThreadSeams::ClassifyStopWait(dwWait);
		if (eAction == UPnPDiscoveryThreadSeams::EStopWaitAction::WaitCooperatively) {
			DebugLogError(_T("Waiting for PCP/NAT-PMP discovery thread to quit timed out; waiting for cooperative exit without forced termination"));
			(void)::WaitForSingleObject(m_pDiscoveryThread->m_hThread, INFINITE);
		} else if (eAction == UPnPDiscoveryThreadSeams::EStopWaitAction::ReleaseAfterWaitFailure)
			DebugLogError(_T("Waiting for PCP/NAT-PMP discovery thread failed (%u); releasing stale thread wrapper"), ::GetLastError());
		else
			DebugLog(_T("Aborted any possible PCP/NAT-PMP discovery thread"));
		delete m_pDiscoveryThread;
		m_pDiscoveryThread = NULL;
	}
	UPnPDiscoveryThreadSeams::ClearAbort(m_bAbortDiscovery);
}

void CUPnPImplPcpNatPmp::DeletePorts()
{
	GetOldPorts();
	m_nUDPPort = 0;
	m_nTCPPort = 0;
	m_nTCPWebPort = 0;
	m_bUPnPPortsForwarded = TRIS_FALSE;
	DeletePorts(false);
}

bool CUPnPImplPcpNatPmp::ResolveSourceAddress()
{
	memset(&m_sourceAddress, 0, sizeof(m_sourceAddress));
	m_nSourceAddressLen = 0;
	m_strSourceAddress.Empty();

	LPCSTR pszBindAddr = thePrefs.GetBindAddrA();
	if (pszBindAddr != NULL && *pszBindAddr != '\0') {
		sockaddr_in source4 = {};
		source4.sin_family = AF_INET;
		if (InetPtonA(AF_INET, pszBindAddr, &source4.sin_addr) == 1) {
			memcpy(&m_sourceAddress, &source4, sizeof(source4));
			m_nSourceAddressLen = sizeof(source4);
			m_strSourceAddress = pszBindAddr;
			return true;
		}

		sockaddr_in6 source6 = {};
		source6.sin6_family = AF_INET6;
		if (InetPtonA(AF_INET6, pszBindAddr, &source6.sin6_addr) == 1) {
			memcpy(&m_sourceAddress, &source6, sizeof(source6));
			m_nSourceAddressLen = sizeof(source6);
			m_strSourceAddress = pszBindAddr;
			return true;
		}

		DebugLogWarning(_T("PCP/NAT-PMP skipped because BindAddr '%S' is not a valid IPv4/IPv6 address"), pszBindAddr);
		return false;
	}

	SOCKET hSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (hSocket == INVALID_SOCKET) {
		DebugLogWarning(_T("PCP/NAT-PMP could not determine a source address because socket() failed: %u"), WSAGetLastError());
		return false;
	}

	sockaddr_in destination = {};
	destination.sin_family = AF_INET;
	destination.sin_port = htons(9);
	InetPtonA(AF_INET, "1.1.1.1", &destination.sin_addr);
	if (connect(hSocket, reinterpret_cast<const sockaddr*>(&destination), sizeof(destination)) != 0) {
		DebugLogWarning(_T("PCP/NAT-PMP could not determine a source address because UDP connect() failed: %u"), WSAGetLastError());
		closesocket(hSocket);
		return false;
	}

	sockaddr_in source = {};
	int nSourceLen = sizeof(source);
	if (getsockname(hSocket, reinterpret_cast<sockaddr*>(&source), &nSourceLen) != 0) {
		DebugLogWarning(_T("PCP/NAT-PMP could not determine a source address because getsockname() failed: %u"), WSAGetLastError());
		closesocket(hSocket);
		return false;
	}

	closesocket(hSocket);
	memcpy(&m_sourceAddress, &source, sizeof(source));
	m_nSourceAddressLen = nSourceLen;
	char achAddress[INET6_ADDRSTRLEN] = {};
	if (InetNtopA(AF_INET, &source.sin_addr, achAddress, _countof(achAddress)) != NULL)
		m_strSourceAddress = achAddress;
	return true;
}

void CUPnPImplPcpNatPmp::GetOldPorts()
{
	if (ArePortsForwarded() == TRIS_TRUE) {
		m_nOldUDPPort = m_nUDPPort;
		m_nOldTCPPort = m_nTCPPort;
		m_nOldTCPWebPort = m_nTCPWebPort;
	} else {
		m_nOldUDPPort = 0;
		m_nOldTCPPort = 0;
		m_nOldTCPWebPort = 0;
	}
}

void CUPnPImplPcpNatPmp::DeletePorts(bool bSkipLock)
{
	CSingleLock lockTest(&m_mutBusy);
	if (bSkipLock || lockTest.Lock(0)) {
		if (m_pTCPFlow != NULL || m_pUDPFlow != NULL || m_pTCPWebFlow != NULL) {
			CloseFlow(m_pTCPFlow, _T("TCP"));
			CloseFlow(m_pUDPFlow, _T("UDP"));
			CloseFlow(m_pTCPWebFlow, _T("TCP Web"));
		}
		m_nOldTCPPort = 0;
		m_nOldUDPPort = 0;
		m_nOldTCPWebPort = 0;
		if (m_nTCPPort == 0 && m_nUDPPort == 0 && m_nTCPWebPort == 0)
			CleanupContext();
	} else {
		DebugLogError(_T("Unable to remove PCP/NAT-PMP port mappings - implementation still busy"));
	}
}

void CUPnPImplPcpNatPmp::CloseFlow(pcp_flow_t *&pFlow, LPCTSTR pszLabel)
{
	if (pFlow == NULL)
		return;

	if (m_pContext != NULL) {
		pcp_close_flow(pFlow);
		pcp_fstate_e state = pcp_wait(pFlow, kPcpWaitTimeoutMs, 1);
		if (state == pcp_state_succeeded || state == pcp_state_partial_result) {
			DebugLog(_T("Successfully removed PCP/NAT-PMP mapping for %s"), pszLabel);
		} else {
			const CString strState(GetPcpStateText(state));
			DebugLogWarning(_T("PCP/NAT-PMP removal for %s finished with state '%s'"), pszLabel, (LPCTSTR)strState);
		}
	}

	pcp_delete_flow(pFlow);
	pFlow = NULL;
}

bool CUPnPImplPcpNatPmp::EnsureMappedPort(uint16 nPort, bool bTCP, pcp_flow_t *&pFlow, bool bOptional)
{
	if (nPort == 0) {
		CloseFlow(pFlow, bTCP ? _T("TCP") : _T("UDP"));
		return true;
	}

	if (m_pContext == NULL || m_nSourceAddressLen == 0)
		return false;

	sockaddr_storage source = m_sourceAddress;
	if (source.ss_family == AF_INET) {
		reinterpret_cast<sockaddr_in*>(&source)->sin_port = htons(nPort);
	} else if (source.ss_family == AF_INET6) {
		reinterpret_cast<sockaddr_in6*>(&source)->sin6_port = htons(nPort);
	} else {
		DebugLogWarning(_T("PCP/NAT-PMP cannot map %s port %hu because the source address family is unsupported"), bTCP ? _T("TCP") : _T("UDP"), nPort);
		return false;
	}

	if (pFlow == NULL) {
		const uint8_t protocol = static_cast<uint8_t>(bTCP ? IPPROTO_TCP : IPPROTO_UDP);
		pFlow = pcp_new_flow(m_pContext, reinterpret_cast<sockaddr*>(&source), NULL, NULL, protocol, kPcpMappingLifetimeSeconds, NULL);
		if (pFlow == NULL) {
			DebugLogWarning(_T("PCP/NAT-PMP failed to create a %s mapping flow for port %hu"), bTCP ? _T("TCP") : _T("UDP"), nPort);
			return false;
		}
	}

	pcp_fstate_e state = pcp_wait(pFlow, kPcpWaitTimeoutMs, 1);
	size_t uInfoCount = 0;
	pcp_flow_info_t *pInfo = pcp_flow_get_info(pFlow, &uInfoCount);
	bool bMapped = false;
	for (size_t i = 0; i < uInfoCount; ++i) {
		if (IsSuccessfulPcpState(pInfo[i].result) && ntohs(pInfo[i].ext_port) != 0) {
			bMapped = true;
			break;
		}
	}
	free(pInfo);

	if (bMapped) {
		if (state == pcp_state_short_lifetime_error) {
			DebugLogWarning(_T("PCP/NAT-PMP accepted %s port %hu with a shorter lifetime than requested"), bTCP ? _T("TCP") : _T("UDP"), nPort);
		} else {
			DebugLog(_T("PCP/NAT-PMP mapped %s port %hu for LAN address %S"), bTCP ? _T("TCP") : _T("UDP"), nPort, m_strSourceAddress.GetString());
		}
		return true;
	}

	const CString strState(GetPcpStateText(state));
	if (bOptional)
		DebugLogWarning(_T("PCP/NAT-PMP could not refresh optional %s port %hu (state: %s)"), bTCP ? _T("TCP") : _T("UDP"), nPort, (LPCTSTR)strState);
	else
		DebugLogWarning(_T("PCP/NAT-PMP failed to map %s port %hu (state: %s)"), bTCP ? _T("TCP") : _T("UDP"), nPort, (LPCTSTR)strState);
	pcp_delete_flow(pFlow);
	pFlow = NULL;
	return false;
}

void CUPnPImplPcpNatPmp::CleanupContext()
{
	if (m_pTCPFlow != NULL) {
		pcp_delete_flow(m_pTCPFlow);
		m_pTCPFlow = NULL;
	}
	if (m_pUDPFlow != NULL) {
		pcp_delete_flow(m_pUDPFlow);
		m_pUDPFlow = NULL;
	}
	if (m_pTCPWebFlow != NULL) {
		pcp_delete_flow(m_pTCPWebFlow);
		m_pTCPWebFlow = NULL;
	}
	if (m_pContext != NULL) {
		pcp_terminate(m_pContext, 0);
		m_pContext = NULL;
	}
}

void CUPnPImplPcpNatPmp::StartThread()
{
	ReapDiscoveryThreadIfFinished();
	if (m_pDiscoveryThread != NULL)
		StopAsyncFind();

	CStartDiscoveryThread *pStartDiscoveryThread = (CStartDiscoveryThread*)AfxBeginThread(RUNTIME_CLASS(CStartDiscoveryThread), THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
	if (pStartDiscoveryThread == NULL) {
		m_bUPnPPortsForwarded = TRIS_FALSE;
		DebugLogError(_T("Failed to create PCP/NAT-PMP discovery thread"));
		return;
	}
	pStartDiscoveryThread->m_bAutoDelete = FALSE;
	pStartDiscoveryThread->SetValues(this);
	m_pDiscoveryThread = pStartDiscoveryThread;
	if (pStartDiscoveryThread->ResumeThread() == static_cast<DWORD>(-1)) {
		DebugLogError(_T("Failed to resume PCP/NAT-PMP discovery thread (%u)"), ::GetLastError());
		delete m_pDiscoveryThread;
		m_pDiscoveryThread = NULL;
		m_bUPnPPortsForwarded = TRIS_FALSE;
	}
}

void CUPnPImplPcpNatPmp::ReapDiscoveryThreadIfFinished()
{
	if (m_pDiscoveryThread == NULL)
		return;

	const DWORD dwWait = ::WaitForSingleObject(m_pDiscoveryThread->m_hThread, 0);
	const UPnPDiscoveryThreadSeams::ENonblockingWaitAction eAction = UPnPDiscoveryThreadSeams::ClassifyNonblockingWait(dwWait);
	if (eAction == UPnPDiscoveryThreadSeams::ENonblockingWaitAction::ReleaseFinished) {
		delete m_pDiscoveryThread;
		m_pDiscoveryThread = NULL;
	} else if (eAction == UPnPDiscoveryThreadSeams::ENonblockingWaitAction::ReleaseAfterWaitFailure) {
		DebugLogError(_T("PCP/NAT-PMP discovery thread wait failed (%u); releasing stale thread wrapper"), ::GetLastError());
		delete m_pDiscoveryThread;
		m_pDiscoveryThread = NULL;
	}
}

typedef CUPnPImplPcpNatPmp::CStartDiscoveryThread CStartDiscoveryThread;
IMPLEMENT_DYNCREATE(CStartDiscoveryThread, CWinThread)

CUPnPImplPcpNatPmp::CStartDiscoveryThread::CStartDiscoveryThread()
	: m_pOwner()
{
}

BOOL CUPnPImplPcpNatPmp::CStartDiscoveryThread::InitInstance()
{
	InitThreadLocale();
	return TRUE;
}

int CUPnPImplPcpNatPmp::CStartDiscoveryThread::Run()
{
	DbgSetThreadName("CUPnPImplPcpNatPmp::CStartDiscoveryThread");
	if (!m_pOwner)
		return 0;

	CSingleLock sLock(&m_pOwner->m_mutBusy);
	if (!sLock.Lock(0)) {
		DebugLogWarning(_T("CUPnPImplPcpNatPmp::CStartDiscoveryThread::Run, failed to acquire Lock, another mapping try might be running already"));
		return 0;
	}

	if (UPnPDiscoveryThreadSeams::IsAbortRequested(m_pOwner->m_bAbortDiscovery))
		return 0;

	bool bSucceeded = false;
	try {
		pcp_log_level = PCP_LOGLVL_NONE;
		if (!m_pOwner->ResolveSourceAddress()) {
			m_pOwner->m_bUPnPPortsForwarded = TRIS_FALSE;
			m_pOwner->SendResultMessage();
			return 0;
		}

		if (m_pOwner->m_pContext == NULL) {
			m_pOwner->m_pContext = pcp_init(ENABLE_AUTODISCOVERY, NULL);
			if (m_pOwner->m_pContext == NULL) {
				DebugLogWarning(_T("PCP/NAT-PMP discovery failed to initialize libpcpnatpmp"));
				m_pOwner->m_bUPnPPortsForwarded = TRIS_FALSE;
				m_pOwner->SendResultMessage();
				return 0;
			}
		}

		if (!m_pOwner->m_bCheckAndRefresh) {
			DebugLog(_T("Trying to setup port forwarding with PCP/NAT-PMP..."));
			m_pOwner->DeletePorts(true);
		} else if (m_pOwner->m_nOldTCPWebPort != 0 && m_pOwner->m_nOldTCPWebPort != m_pOwner->m_nTCPWebPort) {
			m_pOwner->CloseFlow(m_pOwner->m_pTCPWebFlow, _T("TCP Web"));
			m_pOwner->m_nOldTCPWebPort = 0;
		}

		if (UPnPDiscoveryThreadSeams::IsAbortRequested(m_pOwner->m_bAbortDiscovery))
			return 0;

		bSucceeded = m_pOwner->EnsureMappedPort(m_pOwner->m_nTCPPort, true, m_pOwner->m_pTCPFlow, false);
		if (bSucceeded && m_pOwner->m_nUDPPort != 0)
			bSucceeded = m_pOwner->EnsureMappedPort(m_pOwner->m_nUDPPort, false, m_pOwner->m_pUDPFlow, false);
		if (bSucceeded && m_pOwner->m_nTCPWebPort != 0)
			m_pOwner->EnsureMappedPort(m_pOwner->m_nTCPWebPort, true, m_pOwner->m_pTCPWebFlow, true);
	} catch (...) {
		DebugLogError(_T("Unknown Exception in CUPnPImplPcpNatPmp::CStartDiscoveryThread::Run()"));
	}

	if (!UPnPDiscoveryThreadSeams::IsAbortRequested(m_pOwner->m_bAbortDiscovery)) {
		if (bSucceeded) {
			m_pOwner->m_bUPnPPortsForwarded = TRIS_TRUE;
			m_pOwner->m_bSucceededOnce = true;
		} else {
			m_pOwner->m_bUPnPPortsForwarded = TRIS_FALSE;
		}
		m_pOwner->SendResultMessage();
	}
	return 0;
}
