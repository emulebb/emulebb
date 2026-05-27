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
#include "DeadSourceList.h"

class CClientReqSocket;
class CPartFile;
class CUpDownClient;
namespace Kademlia
{
	class CContact;
	class CUInt128;
};
typedef CTypedPtrList<CPtrList, CUpDownClient*> CUpDownClientPtrList;

#define	NUM_CLIENTLIST_STATS	19
#define BAN_CLEANUP_TIME		MIN2MS(20)

//------------CDeletedClient Class----------------------
// this class / list is a bit overkill, but currently needed to avoid any exploit possibility
// it will keep track of certain clients attributes for 2 hours, while the CUpDownClient object might be deleted already
// currently: IP, Port, UserHash
struct PORTANDHASH
{
	uint16 nPort;
	void *pHash;
};

struct IPANDTICS
{
	uint32 dwIP;
	ULONGLONG dwInserted;
};
struct CONNECTINGCLIENT
{
	CUpDownClient *pClient;
	ULONGLONG dwInserted;
};


class CDeletedClient
{
public:
	explicit CDeletedClient(const CUpDownClient *pClient);
	CArray<PORTANDHASH> m_ItemsList;
	ULONGLONG			m_dwInserted;
	uint32				m_cBadRequest;
};

struct TCPERRORFLOODERSTATE
{
	ULONGLONG m_dwIntervalStart = 0;
	ULONGLONG m_dwLastSeen = 0;
	uint32 m_uCount = 0;
};

enum buddyState : uint8
{
	Disconnected,
	Connecting,
	Connected
};

// ----------------------CClientList Class---------------
typedef CMap<uint32, uint32, uint32, uint32> CClientVersionMap;

class CClientList
{
	friend class CClientListCtrl;

public:
	CClientList();
	~CClientList();

	// Clients
	void	AddClient(CUpDownClient *toadd, bool bSkipDupTest = false);
	void	RemoveClient(CUpDownClient *toremove, LPCTSTR pszReason = NULL);
	void	GetStatistics(uint32 &ruTotalClients
						, int stats[NUM_CLIENTLIST_STATS]
						, CClientVersionMap &clientVersionEDonkey
						, CClientVersionMap &clientVersionEDonkeyHybrid
						, CClientVersionMap &clientVersionEMule
						, CClientVersionMap &clientVersionAMule);
	INT_PTR	GetClientCount()							{ return list.GetCount(); }
	void	DeleteAll();
	bool	AttachToAlreadyKnown(CUpDownClient **client, CClientReqSocket *sender);
	CUpDownClient* FindClientByConnIP(uint32 clientip, UINT port) const;
	CUpDownClient* FindClientByIP(uint32 clientip) const;
	CUpDownClient* FindClientByIP(uint32 clientip, UINT port) const;
	CUpDownClient* FindClientByIP_KadPort(uint32 ip, uint16 port) const;
	CUpDownClient* FindClientByIP_UDP(uint32 clientip, UINT nUDPport) const;
	CUpDownClient* FindClientBySearchID(uint32 dwSearchID) const;
	CUpDownClient* FindClientByServerID(uint32 uServerIP, uint32 uED2KUserID) const;
	CUpDownClient* FindClientByUserHash(const uchar *clienthash, uint32 dwIP = 0, uint16 nTCPPort = 0) const;
	CUpDownClient* FindClientByUserID_KadPort(uint32 clientID, uint16 kadPort) const;

	// Banned clients
	void	AddBannedClient(uint32 dwIP);
	bool	IsBannedClient(uint32 dwIP) const;
	void	RemoveBannedClient(uint32 dwIP);
	INT_PTR	GetBannedCount() const						{ return m_bannedList.GetCount(); }
	void	RemoveAllBannedClients();

	// Tracked clients
	void	AddTrackClient(const CUpDownClient *toadd);
	bool	ComparePriorUserhash(uint32 dwIP, uint16 nPort, const void *pNewHash);
	INT_PTR	GetClientsFromIP(uint32 dwIP) const;
	void	TrackBadRequest(const CUpDownClient *upcClient, int nIncreaseCounter);
	uint32	GetBadRequests(const CUpDownClient *upcClient) const;
	void	CheckTCPErrorFlooder(uint32 dwIP);
	INT_PTR	GetTrackedCount() const						{ return m_trackedClientsMap.GetCount(); }
	void	RemoveAllTrackedClients();

	// Kad client list, buddy handling
	bool	RequestTCP(const Kademlia::CContact *contact, uint8 byConnectOptions);
	void	RequestBuddy(const Kademlia::CContact *contact, uint8 byConnectOptions);
	bool	IncomingBuddy(const Kademlia::CContact *contact, const Kademlia::CUInt128 &buddyID);
	void	RemoveFromKadList(CUpDownClient *torem);
	void	AddToKadList(CUpDownClient *toadd);
	bool	DoRequestFirewallCheckUDP(const Kademlia::CContact &contact);
	//bool	DebugDoRequestFirewallCheckUDP(uint32 ip, uint16 port);
	uint8	GetBuddyStatus() const						{ return m_nBuddyStatus; }
	CUpDownClient* GetBuddy() const						{ return m_pBuddy; }

	void	AddKadFirewallRequest(uint32 dwIP);
	bool	IsKadFirewallCheckIP(uint32 dwIP) const;

	// Direct Callback List
	void	AddTrackCallbackRequests(uint32 dwIP);
	bool	AllowCalbackRequest(uint32 dwIP) const;

	// Connecting Clients
	void	AddConnectingClient(CUpDownClient *pToAdd);
	void	RemoveConnectingClient(const CUpDownClient *pToRemove);
	bool	IsConnectingClient(const CUpDownClient *pToCheck) const;

	void	Process();
	bool	ContainsClientPointer(const CUpDownClient *tocheck) const;
	void	ClearPartFileHistory(const CPartFile *file);
	bool	IsValidClient(CUpDownClient *tocheck) const;
	void	Debug_SocketDeleted(CClientReqSocket *deleted) const;
	void	ProcessA4AFClients() const; // ZZ:DownloadManager
	CDeadSourceList	m_globDeadSourceList;

protected:
	void	CleanUpClientList();
	void	ProcessConnectingClientsList();

private:
	CUpDownClientPtrList list;
	CUpDownClientPtrList m_KadList;
	CMap<uint32, uint32, ULONGLONG, ULONGLONG> m_bannedList;
	typedef CMap<uint32, uint32, CDeletedClient*, CDeletedClient*> CDeletedClientMap;
	CDeletedClientMap m_trackedClientsMap;
	typedef CMap<uint32, uint32, TCPERRORFLOODERSTATE, const TCPERRORFLOODERSTATE&> CTCPErrorFlooderMap;
	CTCPErrorFlooderMap m_tcpErrorFloodMap;
	CUpDownClient *m_pBuddy;
	CList<IPANDTICS> listFirewallCheckRequests;
	CList<IPANDTICS> listDirectCallbackRequests;
	CList<CONNECTINGCLIENT> m_liConnectingClients;
	ULONGLONG	m_dwLastBanCleanUp;
	ULONGLONG	m_dwLastTrackedCleanUp;
	ULONGLONG	m_dwLastClientCleanUp;
	uint8	m_nBuddyStatus;
};
