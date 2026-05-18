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
	if (UPnPDiscoveryThreadSeams::IsAbortRequested(m_bAbortDiscovery))
		return false;
	// the only check we need to do is if we are already busy with some async/threaded function
	CSingleLock lockTest(&m_mutBusy);
	return lockTest.Lock(0);
}

void CUPnPImplMiniLib::StopAsyncFind()
{
	ReapDiscoveryThreadIfFinished();
	if (m_pDiscoveryThread != NULL) {
		DWORD dwLastError = ERROR_SUCCESS;
		const UPnPDiscoveryThreadSeams::EStopWaitAction eAction =
			UPnPDiscoveryThreadSeams::RequestDiscoveryThreadStop(m_pDiscoveryThread, m_bAbortDiscovery, dwLastError);
		if (eAction == UPnPDiscoveryThreadSeams::EStopWaitAction::WaitCooperatively) {
			DebugLogError(_T("Waiting for UPnP StartDiscoveryThread to quit timed out; preserving owner lifetime until cooperative exit"));
			if (UPnPDiscoveryThreadSeams::WaitForDiscoveryThreadOwnerLifetime(m_pDiscoveryThread, dwLastError) == UPnPDiscoveryThreadSeams::EOwnerLifetimeWaitAction::ReleaseAfterWaitFailure)
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
	} else {
		m_nOldUDPPort = 0;
		m_nOldTCPPort = 0;
		m_nOldTCPWebPort = 0;
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
			DeletePort(m_nOldTCPPort, sTCP);
			DeletePort(m_nOldUDPPort, sUDP);
			DeletePort(m_nOldTCPWebPort, sTCP);
		}
		m_nOldTCPPort = 0;
		m_nOldUDPPort = 0;
		m_nOldTCPWebPort = 0;
	} else
		DebugLogError(_T("Unable to remove port mappings - implementation still busy"));
}

void CUPnPImplMiniLib::StartDiscovery(uint16 nTCPPort, uint16 nUDPPort, uint16 nTCPWebPort)
{
	DebugLog(_T("Using MiniUPnPLib based implementation"));
	DebugLog(_T("miniupnpc (c) 2005-2026 Thomas Bernard - http://miniupnp.free.fr/"));
	StopAsyncFind();
	GetOldPorts();
	m_nUDPPort = nUDPPort;
	m_nTCPPort = nTCPPort;
	m_nTCPWebPort = nTCPWebPort;
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
		DebugLog(_T("Not refreshing UPnP ports because they don't seem to be forwarded in the first place"));
		return false;
	}
//>>> WiZaRd
	if (!IsReady()) {
		DebugLog(_T("Not refreshing UPnP ports because they are already in the process of being refreshed"));
		return false;
	}
//<<< WiZaRd

	DebugLog(_T("Checking and refreshing UPnP ports"));
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
		DebugLogWarning(_T("CUPnPImplMiniLib::CStartDiscoveryThread::Run, failed to acquire Lock, another Mapping try might be running already"));
		return 0;
	}

	if (UPnPDiscoveryThreadSeams::IsAbortRequested(m_pOwner->m_bAbortDiscovery))// requesting to abort ASAP?
		return 0;

	bool bSucceeded = false;
#if !(defined(_DEBUG) || defined(_DEVBUILD))
	try
#endif
	{
		if (!m_pOwner->m_bCheckAndRefresh) {
			int error = 0;
			UPNPDev *structDeviceList = upnpDiscover(2000, thePrefs.GetBindAddrA(), NULL, 0, 0, 2, &error);
			if (structDeviceList == NULL) {
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
#if !(defined(_DEBUG) || defined(_DEVBUILD))
	} catch (...) {
		DebugLogError(_T("Unknown Exception in CUPnPImplMiniLib::CStartDiscoveryThread::Run()"));
#endif
	}
	if (!UPnPDiscoveryThreadSeams::IsAbortRequested(m_pOwner->m_bAbortDiscovery)) {	// don't send the result on an abort request
		if (bSucceeded) {
			m_pOwner->m_bUPnPPortsForwarded = TRIS_TRUE;
			m_pOwner->m_bSucceededOnce = true;
		} else
			m_pOwner->m_bUPnPPortsForwarded = TRIS_FALSE;
		m_pOwner->SendResultMessage();
	}
	return 0;
}

bool CUPnPImplMiniLib::CStartDiscoveryThread::OpenPort(uint16 nPort, bool bTCP, char *pachLANIP, bool bCheckAndRefresh)
{
	if (UPnPDiscoveryThreadSeams::IsAbortRequested(m_pOwner->m_bAbortDiscovery))
		return false;

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

		if (nResult == UPNPCOMMAND_SUCCESS && achOutIP[0] != 0) {
			DebugLog(_T("Checking UPnP: Mapping for port %hu (%s) on local IP %S still exists"), nPort, (bTCP ? sTCP : sUDP), achOutIP);
			return true;
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

		DebugLogWarning(_T("Adding PortMapping failed for port %hu (%s), Error Code %u"), nPort, (bTCP ? sTCP : sUDP), nResult);
		return false;
	}

	if (UPnPDiscoveryThreadSeams::IsAbortRequested(m_pOwner->m_bAbortDiscovery))
		return false;

	// make sure it really worked
	achOutIP[0] = 0;
	nResult = UPNP_GetSpecificPortMappingEntry(m_pOwner->m_pURLs->controlURL
											 , m_pOwner->m_pIGDData->first.servicetype
											 , strPort
											 , (bTCP ? sTCPa : sUDPa)
											 , NULL
											 , achOutIP, achOutPort
											 , NULL, NULL, NULL);

	if (nResult == UPNPCOMMAND_SUCCESS && achOutIP[0] != 0) {
		DebugLog(_T("Successfully added mapping for port %hu (%s) on local IP %S"), nPort, (bTCP ? sTCP : sUDP), achOutIP);
		return true;
	}

	DebugLogWarning(_T("Failed to verify mapping for port %hu (%s) on local IP %S - considering as failed"), nPort, (bTCP ? sTCP : sUDP), achOutIP);
	// maybe counting this as error is a bit harsh as this may lead to false negatives, however if we would risk false positives
	// this would mean that the fallback implementations are not tried because eMule thinks it worked out fine
	return false;
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
