/*
Copyright (C)2003 Barry Dunne (https://www.emule-project.net)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

// Note To Mods //
/*
Please do not change anything here and release it.
There is going to be a new forum created just for the Kademlia side of the client.
If you feel there is an error or a way to improve something, please
post it in the forum first and let us look at it. If it is a real improvement,
it will be added to the official client. Changing something without knowing
what all it does, can cause great harm to the network if released in mass form.
Any mod that changes anything within the Kademlia side will not be allowed to advertise
their client on the eMule forum.
*/

#pragma once
#include "BuildFeatures.h"
#include "kademlia/utils/UInt128.h"

namespace Kademlia
{
	class CPrefs
	{
	public:
		CPrefs();
		~CPrefs();

		void	GetKadID(CUInt128 &uID) const		{ uID.SetValue(m_uClientID); }
		void	GetKadID(CString &sID) const		{ m_uClientID.ToHexString(sID); }
		void	SetKadID(const CUInt128 &puID)		{ m_uClientID = puID; }
		const CUInt128& GetKadID() const			{ return m_uClientID; }
		void	GetClientHash(CUInt128 &uID) const	{ uID.SetValue(m_uClientHash); }
		void	GetClientHash(CString &sID) const	{ m_uClientHash.ToHexString(sID); }
		//void	SetClientHash(const CUInt128 &uID)	{ m_uClientHash = uID; }
		const CUInt128& GetClientHash() const		{ return m_uClientHash; }
		uint32	GetIPAddress() const				{ return m_uIP; }
		void	SetIPAddress(uint32 uVal);
		bool	GetRecheckIP() const;
		void	SetRecheckIP();
		void	IncRecheckIP()						{ ++m_uRecheckip; }
		bool	HasHadContact() const;
		void	SetLastContact()					{ m_tLastContact = time(NULL); }
		bool	HasLostConnection() const;
		time_t	GetLastContact() const				{ return m_tLastContact; }
		bool	GetFirewalled() const;
		void	SetFirewalled();
		void	IncFirewalled();
#if EMULEBB_HAS_PROTOCOL_TEST_CONTROL
		void	PrepareBuddyRelayEligibilityFixture();
#endif

		uint8	GetTotalFile() const				{ return m_uTotalFile; }
		void	SetTotalFile(uint8 uVal)			{ m_uTotalFile = uVal; }
		uint8	GetTotalStoreSrc() const			{ return m_uTotalStoreSrc; }
		void	SetTotalStoreSrc(uint8 uVal)		{ m_uTotalStoreSrc = uVal; }
		uint8	GetTotalStoreKey() const			{ return m_uTotalStoreKey; }
		void	SetTotalStoreKey(uint8 uVal)		{ m_uTotalStoreKey = uVal; }
		uint8	GetTotalSource() const				{ return m_uTotalSource; }
		void	SetTotalSource(uint8 uVal)			{ m_uTotalSource = uVal; }
		uint8	GetTotalNotes() const				{ return m_uTotalNotes; }
		void	SetTotalNotes(uint8 uVal)			{ m_uTotalNotes = uVal; }
		uint8	GetTotalStoreNotes() const			{ return m_uTotalStoreNotes; }
		void	SetTotalStoreNotes(uint8 uVal)		{ m_uTotalStoreNotes = uVal; }
		uint32	GetKademliaUsers() const			{ return m_uKademliaUsers; }
		void	SetKademliaUsers(uint32 uVal)		{ m_uKademliaUsers = uVal; }
		uint32	GetKademliaFiles() const			{ return m_uKademliaFiles; }
		void	SetKademliaFiles();
		bool	GetPublish() const					{ return m_bPublish; }
		void	SetPublish(bool bVal)				{ m_bPublish = bVal; }
		bool	GetFindBuddy();
		void	SetFindBuddy(bool bVal = true)		{ m_bFindBuddy = bVal; }
		bool	GetUseExternKadPort() const;
		void	SetUseExternKadPort(bool bVal)		{ m_bUseExternKadPort = bVal; }
		uint16	GetExternalKadPort() const			{ return m_nExternKadPort; }
		void	SetExternKadPort(uint16 uVal, uint32 uFromIP);
		bool	FindExternKadPort(bool bReset = false);
		static uint16 GetInternKadPort();
		uint8	GetMyConnectOptions(bool bEncryption = true, bool bCallback = true);
		void	StatsIncUDPFirewalledNodes(bool bFirewalled);
		void	StatsIncTCPFirewalledNodes(bool bFirewalled);
		float	StatsGetFirewalledRatio(bool bUDP) const;
		float	StatsGetKadV8Ratio();

		static uint32 GetUDPVerifyKey(uint32 dwTargetIP);
	private:
		void Init(LPCTSTR szFilename);
		//void Reset();
		//void SetDefaults();
		void ReadFile();
		void WriteFile();
		CString	m_sFilename;
		time_t	m_tLastContact;
		CUInt128 m_uClientID;
		CUInt128 m_uClientHash;
		uint32	m_uIP;
		uint32	m_uIPLast;
		uint32	m_uRecheckip;
		uint32	m_uFirewalled;
		uint32	m_uKademliaUsers;
		uint32	m_uKademliaFiles;
		uint8	m_uTotalFile;
		uint8	m_uTotalStoreSrc;
		uint8	m_uTotalStoreKey;
		uint8	m_uTotalSource;
		uint8	m_uTotalNotes;
		uint8	m_uTotalStoreNotes;
		bool	m_bPublish;
		bool	m_bFindBuddy;
		bool	m_bLastFirewallState;
		bool	m_bUseExternKadPort;
		uint16	m_nExternKadPort;
		CArray<uint32, uint32> m_anExternPortIPs;
		CArray<uint16, uint16> m_anExternPorts;
		uint32	m_nStatsUDPOpenNodes;
		uint32	m_nStatsUDPFirewalledNodes;
		uint32	m_nStatsTCPOpenNodes;
		uint32	m_nStatsTCPFirewalledNodes;
		time_t	m_nStatsKadV8LastChecked;
		float	m_fKadV8Ratio;
	};
}
