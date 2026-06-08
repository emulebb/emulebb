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


This work is based on the java implementation of the Kademlia protocol.
Kademlia: Peer-to-peer routing based on the XOR metric
Copyright (C) 2002  Petar Maymounkov [petar@post.harvard.edu]
http://kademlia.scs.cs.nyu.edu
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

#include "stdafx.h"
#include "Log.h"
#include "OtherFunctions.h"
#include "preferences.h"
#include "kademlia/kademlia/Defines.h"
#include "kademlia/routing/RoutingBin.h"
#include "kademlia/routing/Contact.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

using namespace Kademlia;

CMap<uint32, uint32, uint32, uint32> CRoutingBin::s_mapGlobalContactIPs;
CMap<uint32, uint32, uint32, uint32> CRoutingBin::s_mapGlobalContactSubnets;

#define MAX_CONTACTS_SUBNET			10
#define MAX_CONTACTS_IP				1
constexpr UINT KAD_LOCAL_QUALITY_REPLACEMENT_MARGIN = 120;

CRoutingBin::CRoutingBin()
	: m_bDontDeleteContacts()	// Init delete contact flag.
{
}

CRoutingBin::~CRoutingBin()
{
	try {
		// Delete all contacts
		for (ContactList::const_iterator itContact = m_listEntries.begin(); itContact != m_listEntries.end(); ++itContact) {
			AdjustGlobalTracking((*itContact)->GetIPAddress(), false);
			if (!m_bDontDeleteContacts)
				delete *itContact;
		}
		// Remove all contact entries.
		m_listEntries.clear();
	} catch (...) {
		AddDebugLogLine(false, _T("Exception in ~CRoutingBin"));
	}
}

bool CRoutingBin::AddContact(CContact *pContact)
{
	ASSERT(pContact != NULL);
	if (pContact == NULL)
		return false;

	if (!CanAcceptContactIPLimits(pContact, true))
		return false;

	// If not full, add to the end of list
	if (m_listEntries.size() < K) {
		m_listEntries.push_back(pContact);
		AdjustGlobalTracking(pContact->GetIPAddress(), true);
		return true;
	}
	return false;
}

void CRoutingBin::SetAlive(const CContact *pContact)
{
	ASSERT(pContact != NULL);
	if (pContact == NULL)
		return;

	// Check if we already have a contact with this ID in the list.
	CContact *pContactTest = GetContact(pContact->GetClientID());
	ASSERT(pContact == pContactTest);
	if (pContactTest) {
		// Mark contact as being alive.
		pContactTest->UpdateType();
		// Move to the end of the list
		PushToBottom(pContactTest);
	}
}

void CRoutingBin::SetTCPPort(uint32 uIP, uint16 uUDPPort, uint16 uTCPPort)
{
	// Find contact with IP/Port
	for (ContactList::const_iterator itContact = m_listEntries.begin(); itContact != m_listEntries.end(); ++itContact) {
		CContact *pContact = *itContact;
		if (uIP == pContact->GetIPAddress() && uUDPPort == pContact->GetUDPPort()) {
			// Set TCPPort and mark as alive.
			pContact->SetTCPPort(uTCPPort);
			pContact->UpdateType();
			// Move to the end of the list
			PushToBottom(pContact);
			break;
		}
	}
}

CContact* CRoutingBin::GetContact(uint32 uIP, uint16 nPort, bool bTCPPort)
{
	// Find contact with IP/Port
	for (ContactList::const_iterator itContact = m_listEntries.begin(); itContact != m_listEntries.end(); ++itContact) {
		CContact *pContact = *itContact;
		if ((uIP == pContact->GetIPAddress())
			&& ((!bTCPPort && nPort == pContact->GetUDPPort()) || (bTCPPort && nPort == pContact->GetTCPPort()) || nPort == 0))
		{
			return pContact;
		}
	}
	return NULL;
}

void CRoutingBin::RemoveContact(CContact *const pContact, bool bNoTrackingAdjust)
{
	ASSERT(pContact != NULL);
	if (pContact == NULL)
		return;

	if (!bNoTrackingAdjust)
		AdjustGlobalTracking(pContact->GetIPAddress(), false);
	m_listEntries.remove(pContact);
}

bool CRoutingBin::ContainsContactPointer(const CContact *pContact) const
{
	if (pContact == NULL)
		return false;

	for (ContactList::const_iterator itContact = m_listEntries.begin(); itContact != m_listEntries.end(); ++itContact)
		if (*itContact == pContact)
			return true;
	return false;
}

CContact* CRoutingBin::GetContact(const CUInt128 &uID)
{
	// Find contact by ID.
	for (ContactList::const_iterator itContact = m_listEntries.begin(); itContact != m_listEntries.end(); ++itContact)
		if (uID == (*itContact)->m_uClientID)
			return *itContact;

	return NULL;
}

UINT CRoutingBin::GetSize() const
{
	return (UINT)m_listEntries.size();
}

void CRoutingBin::GetNumContacts(uint32 &nInOutContacts, uint32 &nInOutFilteredContacts, uint8 byMinVersion) const
{
	// Count all Nodes which meet the search criteria and also report those who don't
	for (ContactList::const_iterator itContact = m_listEntries.begin(); itContact != m_listEntries.end(); ++itContact)
		if ((*itContact)->GetVersion() >= byMinVersion)
			++nInOutContacts;
		else
			++nInOutFilteredContacts;
}

UINT CRoutingBin::GetRemaining() const
{
	return (UINT)(K - m_listEntries.size());
}

void CRoutingBin::GetEntries(ContactArray &listResult, bool bEmptyFirst)
{
	if (bEmptyFirst) // Clear results if requested
		listResult.assign(m_listEntries.begin(), m_listEntries.end());
	else // Append all entries to the results
		listResult.insert(listResult.end(), m_listEntries.begin(), m_listEntries.end());
}

CContact* CRoutingBin::GetOldest()
{
	// All new/updated entries are appended to the back.
	return m_listEntries.empty() ? NULL : m_listEntries.front();
}

CContact* CRoutingBin::GetLowestQualityExpiredContact(time_t tNow)
{
	CContact *pLowestQualityContact = NULL;
	UINT uLowestQuality = UINT_MAX;
	for (ContactList::const_iterator itContact = m_listEntries.begin(); itContact != m_listEntries.end(); ++itContact) {
		CContact *pContact = *itContact;
		if (pContact == NULL || pContact->GetType() == 4 || pContact->m_tExpires == 0 || pContact->m_tExpires >= tNow)
			continue;
		const UINT uQuality = pContact->GetLocalQualityScore(tNow);
		if (pLowestQualityContact == NULL || uQuality < uLowestQuality) {
			pLowestQualityContact = pContact;
			uLowestQuality = uQuality;
		}
	}
	return pLowestQualityContact;
}

void CRoutingBin::GetClosestTo(uint32 uMaxType, const CUInt128 &uTarget, uint32 uMaxRequired, ContactMap &rmapResult, bool bEmptyFirst, bool bInUse)
{
	// Empty list if requested.
	if (bEmptyFirst)
		rmapResult.clear();

	// Return empty since we have no entries.
	if (m_listEntries.empty() || !uMaxRequired)
		return;

	// First put results in sorted by uTarget order so we can insert them correctly.
	// We don't care about max results at this time.
	for (ContactList::const_iterator itContact = m_listEntries.begin(); itContact != m_listEntries.end(); ++itContact)
		if ((*itContact)->GetType() <= uMaxType && (*itContact)->IsIpVerified()) {
			CUInt128 uTargetDistance((*itContact)->m_uClientID);
			uTargetDistance.Xor(uTarget);
			rmapResult[uTargetDistance] = *itContact;
			// This list will be used for an unknown time, Inc in use so it's not deleted.
			if (bInUse)
				(*itContact)->IncUse();
		}

	// Remove any extra results by least wanted first.
	while (rmapResult.size() > uMaxRequired) {
		ContactMap::const_iterator it = --rmapResult.end();
		// Dec in use count.
		if (bInUse)
			it->second->DecUse();
		// remove from results
		rmapResult.erase(it);
	}
	// Return result to the caller.
}

void CRoutingBin::AdjustGlobalTracking(uint32 uIP, bool bIncrease)
{
	// IP
	uint32 nSameIPCount;
	if (!s_mapGlobalContactIPs.Lookup(uIP, nSameIPCount))
		nSameIPCount = 0;
	if (bIncrease) {
		if (nSameIPCount >= MAX_CONTACTS_IP) {
			ASSERT(0);
			DebugLogError(_T("RoutingBin Global IP Tracking inconsistency on increase (%s)"), (LPCTSTR)ipstr(htonl(uIP)));
		}
		++nSameIPCount;
	} else if (nSameIPCount == 0) {
		ASSERT(0);
		DebugLogError(_T("RoutingBin Global IP Tracking inconsistency on decrease (%s)"), (LPCTSTR)ipstr(htonl(uIP)));
	} else
		--nSameIPCount;

	if (nSameIPCount != 0)
		s_mapGlobalContactIPs[uIP] = nSameIPCount;
	else
		s_mapGlobalContactIPs.RemoveKey(uIP);

	// Subnet
	uint32 nSameSubnetCount;
	if (!s_mapGlobalContactSubnets.Lookup(uIP & ~0xFF, nSameSubnetCount))
		nSameSubnetCount = 0;
	if (bIncrease) {
		if (nSameSubnetCount >= MAX_CONTACTS_SUBNET && !IsLANIP(ntohl(uIP))) {
			ASSERT(0);
			DebugLogError(_T("RoutingBin Global Subnet Tracking inconsistency on increase (%s)"), (LPCTSTR)ipstr(htonl(uIP)));
		}
		++nSameSubnetCount;
	} else if (nSameSubnetCount == 0) {
		ASSERT(0);
		DebugLogError(_T("RoutingBin Global IP Subnet inconsistency on decrease (%s)"), (LPCTSTR)ipstr(htonl(uIP)));
	} else
		--nSameSubnetCount;

	if (nSameSubnetCount != 0)
		s_mapGlobalContactSubnets[uIP & ~0xFF] = nSameSubnetCount;
	else
		s_mapGlobalContactSubnets.RemoveKey(uIP & ~0xFF);
}

bool CRoutingBin::ChangeContactIPAddress(CContact *pContact, uint32 uNewIP)
{
	ASSERT(pContact != NULL);
	if (pContact == NULL)
		return false;

	// Called if we want to update an indexed contact with a new IP. We have to check if we actually allow
	// such a change and if adjust our tracking. Rejecting a change will in the worst case lead a node contact
	// to become invalid and purged later, but it also protects against a flood of malicious update requests
	// from an IP which would be able to "reroute" all contacts to itself and by that making them useless
	if (pContact->GetIPAddress() == uNewIP)
		return true;

	ASSERT(GetContact(pContact->GetClientID()) == pContact);

	// no more than 1 KadID per IP
	uint32 nSameIPCount;
	if (!s_mapGlobalContactIPs.Lookup(uNewIP, nSameIPCount))
		nSameIPCount = 0;
	if (nSameIPCount >= MAX_CONTACTS_IP) {
		if (thePrefs.GetLogFilteredIPs())
			AddDebugLogLine(false, _T("Rejected kad contact ip change on update (old IP=%s, requested IP=%s) - too many contacts with the same IP (global)"), (LPCTSTR)ipstr(pContact->GetNetIP()), (LPCTSTR)ipstr(htonl(uNewIP)));
		return false;
	}

	if ((uNewIP ^ pContact->GetIPAddress()) & ~0xFF) {
		//  no more than 10 IPs from the same /24 block globally, except if it's a LAN IP
		// (if we don't accept LAN IPs, they already have been filtered before)
		uint32 nSameSubnetGlobalCount;
		if (!s_mapGlobalContactSubnets.Lookup(uNewIP & ~0xFF, nSameSubnetGlobalCount))
			nSameSubnetGlobalCount = 0;
		if (nSameSubnetGlobalCount >= MAX_CONTACTS_SUBNET && !IsLANIP(ntohl(uNewIP))) {
			if (thePrefs.GetLogFilteredIPs())
				AddDebugLogLine(false, _T("Rejected kad contact ip change on update (old IP=%s, requested IP=%s) - too many contacts with the same Subnet (global)"), (LPCTSTR)ipstr(pContact->GetNetIP()), (LPCTSTR)ipstr(htonl(uNewIP)));
			return false;
		}

		uint32 uSameSubnet = 0;
		// Check if we already have a contact with this ID in the list.
		for (ContactList::const_iterator itContact = m_listEntries.begin(); itContact != m_listEntries.end(); ++itContact)
			uSameSubnet += static_cast<uint32>(((uNewIP ^ (*itContact)->GetIPAddress()) & ~0xFF) == 0);

		// no more than 2 IPs from the same /24 block in one bin, except if it's a LAN IP
		// (if we don't accept LAN IPs, they already have been filtered before)
		if (uSameSubnet >= 2 && !IsLANIP(ntohl(uNewIP))) {
			if (thePrefs.GetLogFilteredIPs())
				AddDebugLogLine(false, _T("Rejected kad contact ip change on update (old IP=%s, requested IP=%s) - too many contacts with the same Subnet (local)"), (LPCTSTR)ipstr(pContact->GetNetIP()), (LPCTSTR)ipstr(htonl(uNewIP)));
			return false;
		}
	}

	// everything fine
	// LOGTODO REMOVE
	DEBUG_ONLY(DebugLog(_T("Index contact IP change allowed %s -> %s"), (LPCTSTR)ipstr(pContact->GetNetIP()), (LPCTSTR)ipstr(htonl(uNewIP))));
	AdjustGlobalTracking(pContact->GetIPAddress(), false);
	pContact->SetIPAddress(uNewIP);
	AdjustGlobalTracking(pContact->GetIPAddress(), true);
	return true;
}

void CRoutingBin::PushToBottom(CContact *pContact) // puts an existing contact from X to the end of the list
{
	ASSERT(pContact != NULL);
	if (pContact == NULL)
		return;

	ASSERT(GetContact(pContact->GetClientID()) == pContact);
	for (ContactList::iterator itContact = m_listEntries.begin(); itContact != m_listEntries.end(); ++itContact) {
		if (*itContact == pContact) {
			// WHY: this is only a recency reorder of an existing contact. Removing
			// and push_back-ing allocates a new std::list node; if that allocation
			// fails, the contact is no longer in the bin. splice moves the existing
			// node to the tail without allocation and preserves ownership/tracking.
			m_listEntries.splice(m_listEntries.end(), m_listEntries, itContact);
			return;
		}
	}
	ASSERT(0);
}

CContact* CRoutingBin::GetRandomContact(uint32 nMaxType, uint32 nMinKadVersion)
{
	if (m_listEntries.empty())
		return NULL;
	// Find contact with a suitable version
	CContact *pLastFit = NULL;
	int iRandomStartPos = rand() % m_listEntries.size();
	for (ContactList::const_iterator itContact = m_listEntries.begin(); itContact != m_listEntries.end(); ++itContact) {
		CContact *pContact = *itContact;
		if (pContact->GetType() <= nMaxType && pContact->GetVersion() >= nMinKadVersion) {
			if (iRandomStartPos <= 0)
				return pContact;
			pLastFit = pContact;
		}
		--iRandomStartPos;
	}
	return pLastFit;
}

bool CRoutingBin::ReplaceWeakContact(CContact *pContact, CContact *&rpRemovedContact, UINT &ruRemovedQuality, UINT &ruNewQuality)
{
	rpRemovedContact = NULL;
	ruRemovedQuality = 0;
	ruNewQuality = 0;

	ASSERT(pContact != NULL);
	if (pContact == NULL || m_listEntries.size() < K)
		return false;

	const time_t tNow = time(NULL);
	CContact *pWeakContact = GetWeakestReplaceableContact(tNow, ruRemovedQuality);
	if (pWeakContact == NULL)
		return false;
	if (!CanAcceptContactIPLimits(pContact, true, pWeakContact))
		return false;

	ruNewQuality = pContact->GetLocalQualityScore(tNow);
	if (ruNewQuality < ruRemovedQuality + KAD_LOCAL_QUALITY_REPLACEMENT_MARGIN)
		return false;

	// WHY: full unsplittable Kad bins otherwise keep weak stale contacts until
	// timer probing expires them. This swaps only a non-in-use weak local entry
	// for a materially healthier contact without changing Kad wire behavior.
	RemoveContact(pWeakContact);
	m_listEntries.push_back(pContact);
	AdjustGlobalTracking(pContact->GetIPAddress(), true);
	rpRemovedContact = pWeakContact;
	return true;
}

void CRoutingBin::SetAllContactsVerified()
{
	// Find contact by ID.
	for (ContactList::const_iterator itContact = m_listEntries.begin(); itContact != m_listEntries.end(); ++itContact)
		(*itContact)->SetIpVerified(true);
}

bool CRoutingBin::CheckGlobalIPLimits(uint32 uIP, uint16 uPort, bool bLog)
{
	// no more than 1 KadID per IP
	uint32 nSameIPCount;
	if (!s_mapGlobalContactIPs.Lookup(uIP, nSameIPCount))
		nSameIPCount = 0;
	if (nSameIPCount >= MAX_CONTACTS_IP) {
		if (bLog && thePrefs.GetLogFilteredIPs())
			AddDebugLogLine(false, _T("Ignored kad contact (IP=%s:%u) - too many contacts with the same IP (global)"), (LPCTSTR)ipstr(htonl(uIP)), uPort);
		return false;
	}
	//  no more than 10 IPs from the same /24 block globally, except if it's a LAN IP
	// (if we don't accept LAN IPs, they already have been filtered before)
	uint32 nSameSubnetGlobalCount;
	if (!s_mapGlobalContactSubnets.Lookup(uIP & ~0xFF, nSameSubnetGlobalCount))
		nSameSubnetGlobalCount = 0;
	if (nSameSubnetGlobalCount >= MAX_CONTACTS_SUBNET && !IsLANIP(ntohl(uIP))) {
		if (bLog && thePrefs.GetLogFilteredIPs())
			AddDebugLogLine(false, _T("Ignored kad contact (IP=%s:%u) - too many contacts with the same Subnet (global)"), (LPCTSTR)ipstr(htonl(uIP)), uPort);
		return false;
	}
	return true;
}

bool CRoutingBin::HasOnlyLANNodes() const
{
	for (ContactList::const_iterator itContact = m_listEntries.begin(); itContact != m_listEntries.end(); ++itContact)
		if (!IsLANIP((*itContact)->GetNetIP()))
			return false;

	return true;
}

bool CRoutingBin::CanAcceptContactIPLimits(const CContact *pContact, bool bLog, const CContact *pIgnoredContact) const
{
	ASSERT(pContact != NULL);
	if (pContact == NULL)
		return false;

	const uint32 uIP = pContact->GetIPAddress();
	uint32 uSameSubnets = 0;
	for (ContactList::const_iterator itContact = m_listEntries.begin(); itContact != m_listEntries.end(); ++itContact) {
		const CContact *pExistingContact = *itContact;
		if (pExistingContact == NULL || pExistingContact == pIgnoredContact)
			continue;
		if (pContact->GetClientID() == pExistingContact->m_uClientID)
			return false;
		uSameSubnets += static_cast<uint32>(((uIP ^ pExistingContact->GetIPAddress()) & ~0xFF) == 0);
	}

	// Several checks make sure that we don't store multiple contacts from the same IP or too many
	// contacts from the same subnet. Such IPs are not banned from Kad, they still can index, search, etc.
	if (!CheckGlobalIPLimits(uIP, pContact->GetUDPPort(), bLog))
		return false;

	// No more than 2 IPs from the same /24 block in one bin, except LAN IPs.
	if (uSameSubnets >= 2 && !IsLANIP(pContact->GetNetIP())) {
		if (bLog && thePrefs.GetLogFilteredIPs())
			AddDebugLogLine(false, _T("Ignored kad contact (IP=%s:%u) - too many contacts with the same subnet in RoutingBin"), (LPCTSTR)ipstr(pContact->GetNetIP()), pContact->GetUDPPort());
		return false;
	}
	return true;
}

CContact* CRoutingBin::GetWeakestReplaceableContact(time_t tNow, UINT &ruQuality) const
{
	CContact *pWeakestContact = NULL;
	ruQuality = UINT_MAX;
	for (ContactList::const_iterator itContact = m_listEntries.begin(); itContact != m_listEntries.end(); ++itContact) {
		CContact *pContact = *itContact;
		if (pContact == NULL || !pContact->IsWeakForReplacement(tNow))
			continue;
		const UINT uQuality = pContact->GetLocalQualityScore(tNow);
		if (pWeakestContact == NULL || uQuality < ruQuality) {
			pWeakestContact = pContact;
			ruQuality = uQuality;
		}
	}
	return pWeakestContact;
}
