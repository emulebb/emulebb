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
#include <exception>

enum TRISTATE
{
	TRIS_FALSE,
	TRIS_UNKNOWN,
	TRIS_TRUE
};

enum UPNP_IMPLEMENTATION
{
	UPNP_IMPL_MINIUPNPLIB = 0,
	UPNP_IMPL_PCPNATPMP,
	UPNP_IMPL_NONE	/*last*/
};


class CUPnPImpl
{
	CWnd *m_wndResultMessage;
	UINT m_nResultMessageID;

public:
	struct SResultMessage
	{
		CUPnPImpl *pImplementation;
		int nResult;
		bool bWasRefresh;
	};

	CUPnPImpl();
	virtual	~CUPnPImpl() = default;
	struct UPnPError : public std::exception	{};
	enum
	{
		UPNP_OK,
		UPNP_FAILED,
		UPNP_TIMEOUT
	};
	enum ENatMappingResultReason
	{
		NAT_MAPPING_RESULT_NOT_RUN,
		NAT_MAPPING_RESULT_SUCCESS,
		NAT_MAPPING_RESULT_REFRESH_SUCCESS,
		NAT_MAPPING_RESULT_NO_GATEWAY,
		NAT_MAPPING_RESULT_NOT_READY,
		NAT_MAPPING_RESULT_BUSY,
		NAT_MAPPING_RESULT_TIMEOUT,
		NAT_MAPPING_RESULT_ADD_FAILED,
		NAT_MAPPING_RESULT_VERIFY_FAILED,
		NAT_MAPPING_RESULT_CONFLICT,
		NAT_MAPPING_RESULT_SOURCE_ADDRESS_FAILED,
		NAT_MAPPING_RESULT_BACKEND_INIT_FAILED,
		NAT_MAPPING_RESULT_BACKEND_UNAVAILABLE,
		NAT_MAPPING_RESULT_ABORTED,
		NAT_MAPPING_RESULT_EXCEPTION
	};

	virtual void StartDiscovery(uint16 nTCPPort, uint16 nUDPPort, uint16 nTCPWebPort) = 0;
	virtual bool CheckAndRefresh() = 0;
	virtual void StopAsyncFind() = 0;
	virtual void DeletePorts() = 0;
	virtual bool IsReady() = 0;
	virtual int GetImplementationID() = 0;
	virtual LPCTSTR GetImplementationName() const = 0;
	virtual bool MustAbandonDiscoveryOwner() const	{ return false; }

	void LateEnableWebServerPort(uint16 nPort);	// Add Web Server port to already existing port mapping

	void SetMessageOnResult(CWnd *cwnd, UINT nMessageID);
	void ClearResultMessage();
	TRISTATE ArePortsForwarded() const					{ return m_bUPnPPortsForwarded; }
	uint16 GetUsedTCPPort() const						{ return m_nTCPPort; }
	uint16 GetUsedUDPPort() const						{ return m_nUDPPort; }
	uint16 GetUsedTCPWebPort() const					{ return m_nTCPWebPort; }
	ENatMappingResultReason GetLastResultReason() const	{ return m_eLastResultReason; }
	CString GetLastResultDetail() const					{ return m_strLastResultDetail; }
	CString GetLastResultSummary() const;

// Implementation
protected:
	void SendResultMessage();
	void ClearLastResult();
	void SetLastResult(ENatMappingResultReason eReason, LPCTSTR pszFormat = NULL, ...);
	volatile TRISTATE m_bUPnPPortsForwarded;
	uint16 m_nOldTCPPort;
	uint16 m_nOldTCPWebPort;
	uint16 m_nOldUDPPort;
	uint16 m_nTCPPort;
	uint16 m_nTCPWebPort;
	uint16 m_nUDPPort;
	bool m_bCheckAndRefresh;
	ENatMappingResultReason m_eLastResultReason;
	CString m_strLastResultDetail;
};

// Dummy Implementation to be used when no other implementation is available
class CUPnPImplNone : public CUPnPImpl
{
public:
	virtual void StartDiscovery(uint16, uint16, uint16)	{ ASSERT(0); }
	virtual bool CheckAndRefresh()						{ return false; }
	virtual void StopAsyncFind()						{}
	virtual void DeletePorts()							{}
	virtual bool IsReady()								{ return false; }
	virtual int GetImplementationID()					{ return UPNP_IMPL_NONE; }
	virtual LPCTSTR GetImplementationName() const		{ return _T("None"); }
	virtual bool MustAbandonDiscoveryOwner() const		{ return false; }
};
