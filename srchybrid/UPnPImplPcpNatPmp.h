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
#pragma once

#include "UPnPImpl.h"

struct pcp_ctx_s;
typedef struct pcp_ctx_s pcp_ctx_t;
struct pcp_flow_s;
typedef struct pcp_flow_s pcp_flow_t;

/**
 * Maintains port mappings through the libpcpnatpmp client library.
 */
class CUPnPImplPcpNatPmp : public CUPnPImpl
{
public:
	CUPnPImplPcpNatPmp();
	virtual	~CUPnPImplPcpNatPmp();

	virtual void StartDiscovery(uint16 nTCPPort, uint16 nUDPPort, uint16 nTCPWebPort);
	virtual bool CheckAndRefresh();
	virtual void StopAsyncFind();
	virtual void DeletePorts();
	virtual bool IsReady();
	virtual int GetImplementationID()				{ return UPNP_IMPL_PCPNATPMP; }
	virtual LPCTSTR GetImplementationName() const	{ return _T("PCP/NAT-PMP"); }
	virtual bool MustAbandonForShutdown() const		{ return m_bAbandonForShutdown; }

	/**
	 * Background worker which discovers the PCP/NAT-PMP gateway and maintains the active mappings.
	 */
	class CStartDiscoveryThread : public CWinThread
	{
		DECLARE_DYNCREATE(CStartDiscoveryThread)
	protected:
		CStartDiscoveryThread();

	public:
		virtual BOOL InitInstance();
		virtual int Run();
		void SetValues(CUPnPImplPcpNatPmp *pOwner)	{ m_pOwner = pOwner; }

	private:
		CUPnPImplPcpNatPmp *m_pOwner;
	};

private:
	/**
	 * Resolves the LAN endpoint which should be mapped on the gateway.
	 */
	bool ResolveSourceAddress();
	void StartThread();
	void ReapDiscoveryThreadIfFinished();
	void GetOldPorts();
	void DeletePorts(bool bSkipLock);
	void CloseFlow(pcp_flow_t *&pFlow, LPCTSTR pszLabel);
	bool EnsureMappedPort(uint16 nPort, bool bTCP, pcp_flow_t *&pFlow, bool bOptional);
	void CleanupContext();

	static CMutex m_mutBusy;

	pcp_ctx_t *m_pContext;
	pcp_flow_t *m_pTCPFlow;
	pcp_flow_t *m_pUDPFlow;
	pcp_flow_t *m_pTCPWebFlow;
	/**
	 * Owns the non-auto-deleting discovery worker while it is alive.
	 */
	CStartDiscoveryThread *m_pDiscoveryThread;
	CStringA m_strSourceAddress;
	sockaddr_storage m_sourceAddress;
	int m_nSourceAddressLen;

	bool m_bSucceededOnce;
	bool m_bAbandonForShutdown;
	/**
	 * Interlocked cooperative-cancellation flag read by the discovery worker.
	 */
	volatile LONG m_bAbortDiscovery;
};
