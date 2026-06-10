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
#include "UPnPImpl.h"

#include <cstdarg>
#include <new>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

CUPnPImpl::CUPnPImpl()
	: m_wndResultMessage()
	, m_nResultMessageID()
	, m_bUPnPPortsForwarded(TRIS_FALSE)
	, m_nOldTCPPort()
	, m_nOldTCPWebPort()
	, m_nOldUDPPort()
	, m_nTCPPort()
	, m_nTCPWebPort()
	, m_nUDPPort()
	, m_bCheckAndRefresh()
	, m_eLastResultReason(NAT_MAPPING_RESULT_NOT_RUN)
{
}

void CUPnPImpl::SetMessageOnResult(CWnd *cwnd, UINT nMessageID)
{
	m_wndResultMessage = cwnd;
	m_nResultMessageID = nMessageID;
}

void CUPnPImpl::ClearResultMessage()
{
	m_nResultMessageID = 0;
	m_wndResultMessage = NULL;
}

void CUPnPImpl::SendResultMessage()
{
	if (m_wndResultMessage != NULL && m_nResultMessageID != 0) {
		SResultMessage *pResult = NULL;
		try {
			pResult = new SResultMessage{ this, (m_bUPnPPortsForwarded == TRIS_TRUE ? UPNP_OK : UPNP_FAILED), m_bCheckAndRefresh };
		} catch (CMemoryException *ex) {
			if (ex != NULL)
				ex->Delete();
		} catch (const std::bad_alloc&) {
		}
		if (pResult != NULL) {
			// WHY: backend discovery can finish after the wrapper has already
			// switched to a fallback implementation. The UI handler needs the
			// originating implementation pointer to reject stale results instead
			// of applying them to the current backend's state.
			if (!m_wndResultMessage->PostMessage(m_nResultMessageID, 0, reinterpret_cast<LPARAM>(pResult)))
				delete pResult;
		}
	}
	ClearResultMessage();
}

void CUPnPImpl::ClearLastResult()
{
	CSingleLock lock(&m_csLastResult, TRUE);
	m_eLastResultReason = NAT_MAPPING_RESULT_NOT_RUN;
	m_strLastResultDetail.Empty();
}

void CUPnPImpl::SetLastResult(ENatMappingResultReason eReason, LPCTSTR pszFormat, ...)
{
	CSingleLock lock(&m_csLastResult, TRUE);
	m_eLastResultReason = eReason;
	if (pszFormat == NULL) {
		m_strLastResultDetail.Empty();
		return;
	}

	va_list args;
	va_start(args, pszFormat);
	m_strLastResultDetail.FormatV(pszFormat, args);
	va_end(args);
}

CUPnPImpl::ENatMappingResultReason CUPnPImpl::GetLastResultReason() const
{
	CSingleLock lock(&m_csLastResult, TRUE);
	return m_eLastResultReason;
}

CString CUPnPImpl::GetLastResultDetail() const
{
	CSingleLock lock(&m_csLastResult, TRUE);
	return m_strLastResultDetail;
}

CString CUPnPImpl::GetLastResultSummary() const
{
	CSingleLock lock(&m_csLastResult, TRUE);
	if (!m_strLastResultDetail.IsEmpty())
		return m_strLastResultDetail;

	switch (m_eLastResultReason) {
		case NAT_MAPPING_RESULT_SUCCESS:
			return _T("mapping verified");
		case NAT_MAPPING_RESULT_REFRESH_SUCCESS:
			return _T("mapping refresh verified");
		case NAT_MAPPING_RESULT_NO_GATEWAY:
			return _T("no NAT gateway was found");
		case NAT_MAPPING_RESULT_NOT_READY:
			return _T("backend is not ready");
		case NAT_MAPPING_RESULT_BUSY:
			return _T("backend is already busy");
		case NAT_MAPPING_RESULT_TIMEOUT:
			return _T("mapping attempt timed out");
		case NAT_MAPPING_RESULT_ADD_FAILED:
			return _T("router rejected the mapping request");
		case NAT_MAPPING_RESULT_VERIFY_FAILED:
			return _T("mapping could not be verified");
		case NAT_MAPPING_RESULT_CONFLICT:
			return _T("router already maps the port to another LAN endpoint");
		case NAT_MAPPING_RESULT_SOURCE_ADDRESS_FAILED:
			return _T("local source address could not be determined");
		case NAT_MAPPING_RESULT_BACKEND_INIT_FAILED:
			return _T("backend initialization failed");
		case NAT_MAPPING_RESULT_BACKEND_UNAVAILABLE:
			return _T("backend is unavailable");
		case NAT_MAPPING_RESULT_ABORTED:
			return _T("mapping attempt was aborted");
		case NAT_MAPPING_RESULT_EXCEPTION:
			return _T("backend raised an exception");
		case NAT_MAPPING_RESULT_NOT_RUN:
		default:
			return _T("no mapping result is available");
	}
}

void CUPnPImpl::LateEnableWebServerPort(uint16 nPort)
{
	if (ArePortsForwarded() == TRIS_TRUE && IsReady()) {
		m_nOldTCPWebPort = (m_nTCPWebPort == nPort ? 0 : m_nTCPWebPort);
		m_nTCPWebPort = nPort;
		CheckAndRefresh();
	}
}
