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
#include "preferences.h"
#include "UPnPImplMiniLib.h"
#include "UPnPImplMiniLibSeams.h"
#include "FormatSafetySeams.h"
#include "Log.h"
#include "Otherfunctions.h"
#include "emule.h"
#include "miniupnpc\include\miniupnpc.h"
#include "miniupnpc\include\upnpcommands.h"
#include "opcodes.h"


#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

CMutex CUPnPImplMiniLib::m_mutBusy;

static LPCSTR const sTCPa = "TCP";
static LPCSTR const sUDPa = "UDP";
static LPCTSTR const sTCP = _T("TCP");
static LPCTSTR const sUDP = _T("UDP");

CUPnPImplMiniLib::CUPnPImplMiniLib()
	: m_pURLs()
	, m_pIGDData()
	, m_pDiscoveryThread()
	, m_bSucceededOnce()
	, m_bCreatedTCPPortMapping()
	, m_bCreatedUDPPortMapping()
	, m_bCreatedTCPWebPortMapping()
	, m_bCreatedOldTCPPortMapping()
	, m_bCreatedOldUDPPortMapping()
	, m_bCreatedOldTCPWebPortMapping()
	, m_bAbandonDiscoveryOwner()
	, m_bAbortDiscovery()
{
	m_nOldUDPPort = 0;
	m_nOldTCPPort = 0;
	m_nOldTCPWebPort = 0;
	m_achLanIP[0] = 0;
	m_achWanIP[0] = 0;
}

CUPnPImplMiniLib::~CUPnPImplMiniLib()
{
	StopAsyncFind();
	Cleanup();
}

bool CUPnPImplMiniLib::IsReady()
{
	if (UPnPDiscoveryThreadSeams::IsAbortRequested(m_bAbortDiscovery)) {
		SetLastResult(NAT_MAPPING_RESULT_ABORTED, _T("UPnP IGD backend has an abort request pending"));
		return false;
	}
	// the only check we need to do is if we are already busy with some async/threaded function
	CSingleLock lockTest(&m_mutBusy);
	const bool bReady = lockTest.Lock(0);
	if (!bReady)
		SetLastResult(NAT_MAPPING_RESULT_BUSY, _T("UPnP IGD backend is already handling another mapping request"));
	return bReady;
}

void CUPnPImplMiniLib::StopAsyncFind()
{
	if (m_bAbandonDiscoveryOwner)
		return;

	ReapDiscoveryThreadIfFinished();
	if (m_pDiscoveryThread != NULL) {
		DWORD dwLastError = ERROR_SUCCESS;
		const UPnPDiscoveryThreadSeams::EStopWaitAction eAction =
			UPnPDiscoveryThreadSeams::RequestDiscoveryThreadStop(m_pDiscoveryThread, m_bAbortDiscovery, dwLastError);
		if (eAction == UPnPDiscoveryThreadSeams::EStopWaitAction::WaitCooperatively) {
			SetLastResult(NAT_MAPPING_RESULT_TIMEOUT, _T("UPnP IGD worker did not stop within the timeout"));
			DebugLogError(_T("Waiting for UPnP StartDiscoveryThread to quit timed out; preserving owner lifetime until cooperative exit"));
			const UPnPDiscoveryThreadSeams::EOwnerLifetimeWaitAction eOwnerAction = UPnPDiscoveryThreadSeams::WaitForDiscoveryThreadOwnerLifetime(
				m_pDiscoveryThread,
				theApp.IsClosing() ? UPnPDiscoveryThreadSeams::kShutdownOwnerLifetimeWaitMs : UPnPDiscoveryThreadSeams::kRuntimeOwnerLifetimeWaitMs,
				dwLastError);
			if (eOwnerAction == UPnPDiscoveryThreadSeams::EOwnerLifetimeWaitAction::KeepOwnerAlive) {
				// WHY: discovery workers dereference this implementation while inside
				// third-party UPnP calls. A router/library hang must not block the
				// UI during timeout fallback or process exit, so keep both owner and
				// thread wrapper alive intentionally instead of deleting through a
				// raw worker owner; clearing the result target also suppresses stale
				// success/failure posts after another backend has already started.
				ClearResultMessage();
				DebugLogError(_T("Abandoning UPnP StartDiscoveryThread owner after timed-out owner-lifetime wait"));
				m_bAbandonDiscoveryOwner = true;
				m_pDiscoveryThread = NULL;
				return;
			}
			if (eOwnerAction == UPnPDiscoveryThreadSeams::EOwnerLifetimeWaitAction::ReleaseAfterWaitFailure)
				DebugLogError(_T("Final UPnP StartDiscoveryThread owner-lifetime wait failed (%u); releasing stale thread wrapper"), dwLastError);
		} else if (eAction == UPnPDiscoveryThreadSeams::EStopWaitAction::ReleaseAfterWaitFailure)
			DebugLogError(_T("Waiting for UPnP StartDiscoveryThread failed (%u); releasing stale thread wrapper"), dwLastError);
		else
			DebugLog(_T("Aborted any possible UPnP StartDiscoveryThread"));
		UPnPDiscoveryThreadSeams::ReleaseDiscoveryThread(m_pDiscoveryThread);
	}
	UPnPDiscoveryThreadSeams::ClearAbort(m_bAbortDiscovery);
}

void CUPnPImplMiniLib::DeletePorts()
{
	GetOldPorts();
	m_nUDPPort = 0;
	m_nTCPPort = 0;
	m_nTCPWebPort = 0;
	m_bCreatedTCPPortMapping = false;
	m_bCreatedUDPPortMapping = false;
	m_bCreatedTCPWebPortMapping = false;
	m_bUPnPPortsForwarded = TRIS_FALSE;
	DeletePorts(false);
}

void CUPnPImplMiniLib::DeletePort(uint16 port, LPCTSTR prot)
{
	if (port != 0) {
		const CStringA strPort(FormatSafetySeams::FormatDecimalPortValueA(port));
		int nResult = UPNP_DeletePortMapping(m_pURLs->controlURL, m_pIGDData->first.servicetype, strPort, CStringA(prot), NULL);
		if (nResult == UPNPCOMMAND_SUCCESS)
			DebugLog(_T("Successfully removed mapping for %s port %hu"), prot, port);
		else
			DebugLogWarning(_T("Failed to remove mapping for %s port %hu"), prot, port);
	}
}

void CUPnPImplMiniLib::GetOldPorts()
{
	if (ArePortsForwarded() == TRIS_TRUE) {
		m_nOldUDPPort = m_nUDPPort;
		m_nOldTCPPort = m_nTCPPort;
		m_nOldTCPWebPort = m_nTCPWebPort;
		m_bCreatedOldUDPPortMapping = m_bCreatedUDPPortMapping;
		m_bCreatedOldTCPPortMapping = m_bCreatedTCPPortMapping;
		m_bCreatedOldTCPWebPortMapping = m_bCreatedTCPWebPortMapping;
	} else {
		m_nOldUDPPort = 0;
		m_nOldTCPPort = 0;
		m_nOldTCPWebPort = 0;
		m_bCreatedOldUDPPortMapping = false;
		m_bCreatedOldTCPPortMapping = false;
		m_bCreatedOldTCPWebPortMapping = false;
	}
}

void CUPnPImplMiniLib::DeletePorts(bool bSkipLock)
{
	// this function can be blocking when called when eMule exits, and we need to wait for it to finish
	// before going on anyway. It might be called from the non-blocking StartDiscovery() function too however
	CSingleLock lockTest(&m_mutBusy);
	if (bSkipLock || lockTest.Lock(0)) {
		if (m_pURLs == NULL || m_pURLs->controlURL == NULL || m_pIGDData == NULL) {
			DebugLogWarning(_T("Skipping UPnP port removal because no valid IGD control endpoint is available"));
		} else {
			if (ShouldDeleteMiniUPnPPortMapping(m_nOldTCPPort, m_bCreatedOldTCPPortMapping))
				DeletePort(m_nOldTCPPort, sTCP);
			else if (m_nOldTCPPort != 0)
				DebugLog(_T("Skipping UPnP TCP port %hu removal because this process reused an existing mapping"), m_nOldTCPPort);
			if (ShouldDeleteMiniUPnPPortMapping(m_nOldUDPPort, m_bCreatedOldUDPPortMapping))
				DeletePort(m_nOldUDPPort, sUDP);
			else if (m_nOldUDPPort != 0)
				DebugLog(_T("Skipping UPnP UDP port %hu removal because this process reused an existing mapping"), m_nOldUDPPort);
			if (ShouldDeleteMiniUPnPPortMapping(m_nOldTCPWebPort, m_bCreatedOldTCPWebPortMapping))
				DeletePort(m_nOldTCPWebPort, sTCP);
			else if (m_nOldTCPWebPort != 0)
				DebugLog(_T("Skipping UPnP TCP web port %hu removal because this process reused an existing mapping"), m_nOldTCPWebPort);
		}
		m_nOldTCPPort = 0;
		m_nOldUDPPort = 0;
		m_nOldTCPWebPort = 0;
		m_bCreatedOldTCPPortMapping = false;
		m_bCreatedOldUDPPortMapping = false;
		m_bCreatedOldTCPWebPortMapping = false;
	} else
		DebugLogError(_T("Unable to remove port mappings - implementation still busy"));
}

void CUPnPImplMiniLib::StartDiscovery(uint16 nTCPPort, uint16 nUDPPort, uint16 nTCPWebPort)
{
	DebugLog(_T("Using MiniUPnPLib based implementation"));
	DebugLog(_T("miniupnpc (c) 2005-2026 Thomas Bernard - http://miniupnp.free.fr/"));
	ClearLastResult();
	StopAsyncFind();
	GetOldPorts();
	m_nUDPPort = nUDPPort;
	m_nTCPPort = nTCPPort;
	m_nTCPWebPort = nTCPWebPort;
	m_bCreatedTCPPortMapping = false;
	m_bCreatedUDPPortMapping = false;
	m_bCreatedTCPWebPortMapping = false;
	m_bUPnPPortsForwarded = TRIS_UNKNOWN;
	m_bCheckAndRefresh = false;

	Cleanup();
	if (!UPnPDiscoveryThreadSeams::IsAbortRequested(m_bAbortDiscovery))
		StartThread();
}

bool CUPnPImplMiniLib::CheckAndRefresh()
{
	// in CheckAndRefresh we don't do any new time consuming discovery tries, we expect to find the same router like the first time
	// and of course we also don't delete old ports (this was done in Discovery) but only check that our current mappings still exist
	// and refresh them if not
	if (UPnPDiscoveryThreadSeams::IsAbortRequested(m_bAbortDiscovery) || !m_bSucceededOnce || m_pURLs == NULL || m_pIGDData == NULL
	    || m_pURLs->controlURL == NULL || m_nTCPPort == 0)
	{
		SetLastResult(NAT_MAPPING_RESULT_NOT_READY, _T("UPnP IGD refresh skipped because no verified mapping is active"));
		DebugLog(_T("Not refreshing UPnP ports because they don't seem to be forwarded in the first place"));
		return false;
	}
//>>> WiZaRd
	if (!IsReady()) {
		SetLastResult(NAT_MAPPING_RESULT_BUSY, _T("UPnP IGD refresh skipped because another mapping request is running"));
		DebugLog(_T("Not refreshing UPnP ports because they are already in the process of being refreshed"));
		return false;
	}
//<<< WiZaRd

	DebugLog(_T("Checking and refreshing UPnP ports"));
	ClearLastResult();
	m_bCheckAndRefresh = true;
	StartThread();
	return true;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////
/// CUPnPImplMiniLib::CStartDiscoveryThread Implementation
typedef CUPnPImplMiniLib::CStartDiscoveryThread CStartDiscoveryThread;
IMPLEMENT_DYNCREATE(CStartDiscoveryThread, CWinThread)

CUPnPImplMiniLib::CStartDiscoveryThread::CStartDiscoveryThread()
	: m_pOwner()
{
}

BOOL CUPnPImplMiniLib::CStartDiscoveryThread::InitInstance()
{
	InitThreadLocale();
	return TRUE;
}

int CUPnPImplMiniLib::CStartDiscoveryThread::Run()
{
	DbgSetThreadName("CUPnPImplMiniLib::CStartDiscoveryThread");
	if (!m_pOwner)
		return 0;

	CSingleLock sLock(&m_pOwner->m_mutBusy);
	if (!sLock.Lock(0)) {
		m_pOwner->SetLastResult(NAT_MAPPING_RESULT_BUSY, _T("UPnP IGD backend is already handling another mapping request"));
		DebugLogWarning(_T("CUPnPImplMiniLib::CStartDiscoveryThread::Run, failed to acquire Lock, another Mapping try might be running already"));
		return 0;
	}

	if (UPnPDiscoveryThreadSeams::IsAbortRequested(m_pOwner->m_bAbortDiscovery)) { // requesting to abort ASAP?
		m_pOwner->SetLastResult(NAT_MAPPING_RESULT_ABORTED, _T("UPnP IGD mapping was aborted before discovery"));
		return 0;
	}

	bool bSucceeded = false;
#ifndef _DEBUG
	try
#endif
	{
		if (!m_pOwner->m_bCheckAndRefresh) {
			int error = 0;
			UPNPDev *structDeviceList = upnpDiscover(2000, thePrefs.GetBindAddrA(), NULL, 0, 0, 2, &error);
			if (structDeviceList == NULL) {
				m_pOwner->SetLastResult(NAT_MAPPING_RESULT_NO_GATEWAY, _T("UPnP IGD discovery found no gateway devices (error %d)"), error);
				DebugLog(_T("UPNP: No Internet Gateway Devices found, aborting: %d"), error);
				m_pOwner->m_bUPnPPortsForwarded = TRIS_FALSE;
				m_pOwner->SendResultMessage();
				return 0;
			}

			if (UPnPDiscoveryThreadSeams::IsAbortRequested(m_pOwner->m_bAbortDiscovery)) {	// requesting to abort ASAP?
				freeUPNPDevlist(structDeviceList);
				return 0;
			}

			DebugLog(_T("List of UPNP devices found on the network:"));
			for (UPNPDev *pDevice = structDeviceList; pDevice != NULL; pDevice = pDevice->pNext)
				DebugLog(_T("Desc: %S, st: %S"), pDevice->descURL, pDevice->st);

			m_pOwner->m_pURLs = new UPNPUrls();
			m_pOwner->m_pIGDData = new IGDdatas();
			*m_pOwner->m_achLanIP = 0;
			*m_pOwner->m_achWanIP = 0;
			int iResult = UPNP_GetValidIGD(structDeviceList, m_pOwner->m_pURLs, m_pOwner->m_pIGDData
							, m_pOwner->m_achLanIP, sizeof m_pOwner->m_achLanIP);
			freeUPNPDevlist(structDeviceList);
			if (m_pOwner->m_pURLs->controlURL != NULL)
				UPNP_GetExternalIPAddress(m_pOwner->m_pURLs->controlURL, m_pOwner->m_pIGDData->first.servicetype, m_pOwner->m_achWanIP);
			bool bNotFound = false;
			switch (iResult) {
			case 1:
				DebugLog(_T("Found valid IGD : %S"), m_pOwner->m_pURLs->controlURL);
				break;
			case 2:
				DebugLog(_T("Found an IGD with a reserved IP address (%S) : %S"), m_pOwner->m_achWanIP, m_pOwner->m_pURLs->controlURL);
				bNotFound = true;
				break;
			case 3:
				DebugLog(_T("Found a (not connected?) IGD : %S - Trying to continue anyway"), m_pOwner->m_pURLs->controlURL);
				break;
			case 4:
				DebugLog(_T("UPnP device found. Is it an IGD? : %S - Trying to continue anyway"), m_pOwner->m_pURLs->controlURL);
				break;
			default:
				DebugLog(_T("Found device (IGD?) : %S - Aborting"), m_pOwner->m_pURLs->controlURL != NULL ? m_pOwner->m_pURLs->controlURL : "(none)");
				bNotFound = true;
			}
			if (bNotFound || m_pOwner->m_pURLs->controlURL == NULL) {
				m_pOwner->SetLastResult(NAT_MAPPING_RESULT_NO_GATEWAY, _T("UPnP IGD discovery did not return a usable IGD control endpoint"));
				m_pOwner->m_bUPnPPortsForwarded = TRIS_FALSE;
				m_pOwner->SendResultMessage();
				return 0;
			}
			DebugLog(_T("Our LAN IP: %S"), m_pOwner->m_achLanIP);

			if (UPnPDiscoveryThreadSeams::IsAbortRequested(m_pOwner->m_bAbortDiscovery))// requesting to abort ASAP?
				return 0;

			// do we still have old mappings? Remove them first
			m_pOwner->DeletePorts(true);
		}

		bSucceeded = OpenPort(m_pOwner->m_nTCPPort, true, m_pOwner->m_achLanIP, m_pOwner->m_bCheckAndRefresh);
		if (bSucceeded && m_pOwner->m_nUDPPort != 0)
			bSucceeded = OpenPort(m_pOwner->m_nUDPPort, false, m_pOwner->m_achLanIP, m_pOwner->m_bCheckAndRefresh);
		if (bSucceeded) {
			if (m_pOwner->m_nOldTCPWebPort)
				m_pOwner->DeletePort(m_pOwner->m_nOldTCPWebPort, sTCP);	//unmap WebServer port (late binding)
			if (m_pOwner->m_nTCPWebPort)
				OpenPort(m_pOwner->m_nTCPWebPort, true, m_pOwner->m_achLanIP, m_pOwner->m_bCheckAndRefresh);	// don't fail if only the Web Interface port fails for some reason
		}
#ifndef _DEBUG
	} catch (...) {
		m_pOwner->SetLastResult(NAT_MAPPING_RESULT_EXCEPTION, _T("UPnP IGD worker raised an unknown exception"));
		DebugLogError(_T("Unknown Exception in CUPnPImplMiniLib::CStartDiscoveryThread::Run()"));
#endif
	}
	if (!UPnPDiscoveryThreadSeams::IsAbortRequested(m_pOwner->m_bAbortDiscovery)) {	// don't send the result on an abort request
		if (bSucceeded) {
			m_pOwner->m_bUPnPPortsForwarded = TRIS_TRUE;
			m_pOwner->m_bSucceededOnce = true;
			m_pOwner->SetLastResult(m_pOwner->m_bCheckAndRefresh ? NAT_MAPPING_RESULT_REFRESH_SUCCESS : NAT_MAPPING_RESULT_SUCCESS,
				_T("UPnP IGD verified TCP %hu and UDP %hu for LAN address %S"),
				m_pOwner->m_nTCPPort,
				m_pOwner->m_nUDPPort,
				m_pOwner->m_achLanIP);
		} else if (m_pOwner->GetLastResultReason() == NAT_MAPPING_RESULT_NOT_RUN) {
			m_pOwner->SetLastResult(NAT_MAPPING_RESULT_VERIFY_FAILED, _T("UPnP IGD could not verify the requested TCP/UDP mappings"));
			m_pOwner->m_bUPnPPortsForwarded = TRIS_FALSE;
		} else
			m_pOwner->m_bUPnPPortsForwarded = TRIS_FALSE;
		m_pOwner->SendResultMessage();
	}
	return 0;
}

bool CUPnPImplMiniLib::CStartDiscoveryThread::OpenPort(uint16 nPort, bool bTCP, char *pachLANIP, bool bCheckAndRefresh)
{
	if (UPnPDiscoveryThreadSeams::IsAbortRequested(m_pOwner->m_bAbortDiscovery))
	{
		m_pOwner->SetLastResult(NAT_MAPPING_RESULT_ABORTED, _T("UPnP IGD mapping was aborted before opening port %hu (%s)"), nPort, (bTCP ? sTCP : sUDP));
		return false;
	}

	static const char achDescTCP[] = "eMule_TCP";
	static const char achDescUDP[] = "eMule_UDP";
	const CStringA strPort(FormatSafetySeams::FormatDecimalPortValueA(nPort));

	int nResult;
	// if we are refreshing ports, check first if the mapping is still fine and only try to open if not
	char achOutIP[20] = {};
	char achOutPort[8] = {};
	if (bCheckAndRefresh) {
		nResult = UPNP_GetSpecificPortMappingEntry(m_pOwner->m_pURLs->controlURL, m_pOwner->m_pIGDData->first.servicetype
												 , strPort
												 , (bTCP ? sTCPa : sUDPa)
												 , NULL
												 , achOutIP, achOutPort
												 , NULL, NULL, NULL);

		if (ShouldAcceptMiniUPnPRefreshMapping(nResult == UPNPCOMMAND_SUCCESS, achOutIP, achOutPort, pachLANIP, nPort)) {
			DebugLog(_T("Checking UPnP: Mapping for port %hu (%s) still targets local IP %S:%S"), nPort, (bTCP ? sTCP : sUDP), achOutIP, achOutPort);
			return true;
		}
		if (nResult == UPNPCOMMAND_SUCCESS && achOutIP[0] != 0) {
			m_pOwner->SetLastResult(NAT_MAPPING_RESULT_CONFLICT,
				_T("UPnP IGD refresh found port %hu (%s) mapped to %S:%S instead of %S:%hu"),
				nPort,
				(bTCP ? sTCP : sUDP),
				achOutIP,
				achOutPort,
				pachLANIP,
				nPort);
			DebugLogWarning(_T("Checking UPnP: Mapping for port %hu (%s) targets local IP %S:%S instead of %S:%hu"), nPort, (bTCP ? sTCP : sUDP), achOutIP, achOutPort, pachLANIP, nPort);
			return false;
		}

		DebugLogWarning(_T("Checking UPnP: Mapping for port %hu (%s) on local IP %S is gone, trying to reopen port"), nPort, (bTCP ? sTCP : sUDP), achOutIP);
	}


	nResult = UPNP_AddPortMapping(m_pOwner->m_pURLs->controlURL
								, m_pOwner->m_pIGDData->first.servicetype
								, strPort, strPort, pachLANIP
								, (bTCP ? achDescTCP : achDescUDP)
								, (bTCP ? sTCPa : sUDPa)
								, NULL, NULL);

	if (nResult != UPNPCOMMAND_SUCCESS) {
		achOutIP[0] = 0;
		achOutPort[0] = 0;
		const int nMappingQueryResult = UPNP_GetSpecificPortMappingEntry(m_pOwner->m_pURLs->controlURL
											 , m_pOwner->m_pIGDData->first.servicetype
											 , strPort
											 , (bTCP ? sTCPa : sUDPa)
											 , NULL
											 , achOutIP, achOutPort
											 , NULL, NULL, NULL);
		if (ShouldAcceptMiniUPnPExistingMappingAfterAddFailure(nMappingQueryResult == UPNPCOMMAND_SUCCESS, achOutIP, achOutPort, pachLANIP, nPort)) {
			DebugLog(_T("PortMapping for port %hu (%s) already targets local IP %S:%S - considering as successful"), nPort, (bTCP ? sTCP : sUDP), achOutIP, achOutPort);
			return true;
		}
		if (nMappingQueryResult == UPNPCOMMAND_SUCCESS && achOutIP[0] != 0) {
			m_pOwner->SetLastResult(NAT_MAPPING_RESULT_CONFLICT,
				_T("UPnP IGD add failed because port %hu (%s) is already mapped to %S:%S"),
				nPort,
				(bTCP ? sTCP : sUDP),
				achOutIP,
				achOutPort);
			DebugLogWarning(_T("Adding PortMapping failed for port %hu (%s); router already maps it to local IP %S:%S"), nPort, (bTCP ? sTCP : sUDP), achOutIP, achOutPort);
			return false;
		}

		m_pOwner->SetLastResult(NAT_MAPPING_RESULT_ADD_FAILED, _T("UPnP IGD add failed for port %hu (%s), error code %u"), nPort, (bTCP ? sTCP : sUDP), nResult);
		DebugLogWarning(_T("Adding PortMapping failed for port %hu (%s), Error Code %u"), nPort, (bTCP ? sTCP : sUDP), nResult);
		return false;
	}

	if (UPnPDiscoveryThreadSeams::IsAbortRequested(m_pOwner->m_bAbortDiscovery)) {
		m_pOwner->SetLastResult(NAT_MAPPING_RESULT_ABORTED, _T("UPnP IGD mapping was aborted after adding port %hu (%s)"), nPort, (bTCP ? sTCP : sUDP));
		return false;
	}

	// make sure it really worked
	achOutIP[0] = 0;
	nResult = UPNP_GetSpecificPortMappingEntry(m_pOwner->m_pURLs->controlURL
											 , m_pOwner->m_pIGDData->first.servicetype
											 , strPort
											 , (bTCP ? sTCPa : sUDPa)
											 , NULL
											 , achOutIP, achOutPort
											 , NULL, NULL, NULL);

	if (DoesMiniUPnPMappingMatchRequest(achOutIP, achOutPort, pachLANIP, nPort)) {
		DebugLog(_T("Successfully added mapping for port %hu (%s) on local IP %S:%S"), nPort, (bTCP ? sTCP : sUDP), achOutIP, achOutPort);
		m_pOwner->MarkCreatedPortMapping(nPort, bTCP);
		return true;
	}
	if (nResult == UPNPCOMMAND_SUCCESS && achOutIP[0] != 0) {
		m_pOwner->SetLastResult(NAT_MAPPING_RESULT_CONFLICT,
			_T("UPnP IGD verification found port %hu (%s) mapped to %S:%S instead of %S:%hu"),
			nPort,
			(bTCP ? sTCP : sUDP),
			achOutIP,
			achOutPort,
			pachLANIP,
			nPort);
		DebugLogWarning(_T("Failed to verify mapping for port %hu (%s): router reports local IP %S:%S instead of %S:%hu"), nPort, (bTCP ? sTCP : sUDP), achOutIP, achOutPort, pachLANIP, nPort);
		return false;
	}

	m_pOwner->SetLastResult(NAT_MAPPING_RESULT_VERIFY_FAILED, _T("UPnP IGD failed to verify mapping for port %hu (%s)"), nPort, (bTCP ? sTCP : sUDP));
	DebugLogWarning(_T("Failed to verify mapping for port %hu (%s) on local IP %S - considering as failed"), nPort, (bTCP ? sTCP : sUDP), achOutIP);
	// maybe counting this as error is a bit harsh as this may lead to false negatives, however if we would risk false positives
	// this would mean that the fallback implementations are not tried because eMule thinks it worked out fine
	return false;
}

void CUPnPImplMiniLib::MarkCreatedPortMapping(uint16 nPort, bool bTCP)
{
	if (bTCP && nPort == m_nTCPWebPort && nPort != m_nTCPPort)
		m_bCreatedTCPWebPortMapping = true;
	else if (bTCP && nPort == m_nTCPPort)
		m_bCreatedTCPPortMapping = true;
	else if (!bTCP && nPort == m_nUDPPort)
		m_bCreatedUDPPortMapping = true;
}

void CUPnPImplMiniLib::Cleanup()
{
	FreeUPNPUrls(m_pURLs);
	delete m_pURLs;
	m_pURLs = NULL;

	delete m_pIGDData;
	m_pIGDData = NULL;
}

void CUPnPImplMiniLib::ReapDiscoveryThreadIfFinished()
{
	DWORD dwLastError = ERROR_SUCCESS;
	const UPnPDiscoveryThreadSeams::ENonblockingWaitAction eAction =
		UPnPDiscoveryThreadSeams::ReapDiscoveryThreadIfFinished(m_pDiscoveryThread, dwLastError);
	if (eAction == UPnPDiscoveryThreadSeams::ENonblockingWaitAction::ReleaseAfterWaitFailure)
		DebugLogError(_T("UPnP StartDiscoveryThread wait failed (%u); releasing stale thread wrapper"), dwLastError);
}

void CUPnPImplMiniLib::StartThread()
{
	ReapDiscoveryThreadIfFinished();
	if (m_pDiscoveryThread != NULL)
		StopAsyncFind();

	CStartDiscoveryThread *pStartDiscoveryThread = (CStartDiscoveryThread*)AfxBeginThread(RUNTIME_CLASS(CStartDiscoveryThread), THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
	if (pStartDiscoveryThread == NULL) {
		m_bUPnPPortsForwarded = TRIS_FALSE;
		DebugLogError(_T("Failed to create UPnP StartDiscoveryThread"));
		return;
	}
	DWORD dwLastError = ERROR_SUCCESS;
	if (!UPnPDiscoveryThreadSeams::OwnAndResumeDiscoveryThread(m_pDiscoveryThread, pStartDiscoveryThread, this, dwLastError)) {
		DebugLogError(_T("Failed to resume UPnP StartDiscoveryThread (%u)"), dwLastError);
		m_bUPnPPortsForwarded = TRIS_FALSE;
	}
}
