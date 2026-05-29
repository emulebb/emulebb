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
#include "UPnPImplWrapper.h"
#include "UPnPImpl.h"
#include "UPnPImplMiniLib.h"
#include "UPnPImplPcpNatPmp.h"
#include "UPnPImplWrapperSeams.h"
#include "Preferences.h"
#include "Log.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

CUPnPImplWrapper::CUPnPImplWrapper()
	: m_pActiveImpl(NULL)
{
	static_assert(UPNP_BACKEND_AUTOMATIC == NAT_MAPPING_BACKEND_MODE_AUTOMATIC, "UPnP automatic preference value changed");
	static_assert(UPNP_BACKEND_IGD_ONLY == NAT_MAPPING_BACKEND_MODE_UPNP_IGD_ONLY, "UPnP IGD-only preference value changed");
	static_assert(UPNP_BACKEND_PCP_NATPMP_ONLY == NAT_MAPPING_BACKEND_MODE_PCP_NATPMP_ONLY, "UPnP PCP/NAT-PMP-only preference value changed");

	ConfigureImplementations();
}

CUPnPImplWrapper::~CUPnPImplWrapper()
{
	ClearImplementations();
}

void CUPnPImplWrapper::ClearImplementations()
{
	while (!m_liAvailable.IsEmpty()) {
		CUPnPImpl *pImpl = m_liAvailable.RemoveHead();
		if (pImpl != NULL) {
			pImpl->StopAsyncFind();
			if (pImpl->MustAbandonForShutdown()) {
				// WHY: a timed-out discovery worker still owns raw access to the
				// implementation. During process exit the safe choice is a bounded
				// leak, not deleting the owner and turning a slow router into UAF.
				DebugLogError(_T("Abandoning NAT mapping implementation '%s' because its discovery worker is still running"), pImpl->GetImplementationName());
			} else
				delete pImpl;
		}
	}
	while (!m_liUsed.IsEmpty()) {
		CUPnPImpl *pImpl = m_liUsed.RemoveHead();
		if (pImpl != NULL) {
			pImpl->StopAsyncFind();
			if (pImpl->MustAbandonForShutdown()) {
				// WHY: a timed-out discovery worker still owns raw access to the
				// implementation. During process exit the safe choice is a bounded
				// leak, not deleting the owner and turning a slow router into UAF.
				DebugLogError(_T("Abandoning NAT mapping implementation '%s' because its discovery worker is still running"), pImpl->GetImplementationName());
			} else
				delete pImpl;
		}
	}
	m_pActiveImpl = NULL;
}

void CUPnPImplWrapper::ConfigureImplementations()
{
	ClearImplementations();

	const NatMappingBackendOrder backendOrder = BuildNatMappingBackendOrder(thePrefs.GetUPnPBackendMode());
	for (size_t i = 0; i < backendOrder.uCount; ++i) {
		switch (backendOrder.aeBackends[i]) {
			case NAT_MAPPING_BACKEND_UPNP_IGD:
				m_liAvailable.AddTail(new CUPnPImplMiniLib());
				break;
			case NAT_MAPPING_BACKEND_PCP_NATPMP:
				m_liAvailable.AddTail(new CUPnPImplPcpNatPmp());
				break;
		}
	}
	if (m_liAvailable.IsEmpty())
		m_liAvailable.AddTail(new CUPnPImplNone());
	Init();
}

void CUPnPImplWrapper::Init()
{
	ASSERT(!m_liAvailable.IsEmpty());
	m_pActiveImpl = m_liAvailable.RemoveHead();
	m_liUsed.AddTail(m_pActiveImpl);
}

void CUPnPImplWrapper::Reset()
{
	ConfigureImplementations();
}

bool CUPnPImplWrapper::SwitchImplentation()
{
	if (m_liAvailable.IsEmpty())
		return false;

	m_pActiveImpl = m_liAvailable.RemoveHead();
	m_liUsed.AddTail(m_pActiveImpl);
	return true;
}
