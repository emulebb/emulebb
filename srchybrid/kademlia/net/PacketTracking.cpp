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
#include "stdafx.h"
#include "clientlist.h"
#include "emule.h"
#include "KadDiagnosticsSeams.h"
#include "Log.h"
#include "opcodes.h"
#include "kademlia/kademlia/Kademlia.h"
#include "kademlia/net/PacketTracking.h"

#include <memory>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

using namespace Kademlia;

CPacketTracking::CPacketTracking()
	: dwLastTrackInCleanup()
{
}

CPacketTracking::~CPacketTracking()
{
	m_mapTrackPacketsIn.RemoveAll();
	while (!m_liTrackPacketsIn.IsEmpty())
		delete m_liTrackPacketsIn.RemoveHead();
}

void CPacketTracking::AddTrackedOutPacket(uint32 dwIP, uint8 byOpcode)
{
	// this tracklist monitors _outgoing_ request packets, to make sure incoming answer packets were requested
	// later we will check only tracked packets
	if (IsTrackedOutListRequestPacket(byOpcode)) {
		const ULONGLONG curTick = ::GetTickCount64();
		listTrackedRequests.AddHead(TrackPackets_Struct{curTick, dwIP, byOpcode});
		while (!listTrackedRequests.IsEmpty() && curTick >= listTrackedRequests.GetTail().dwInserted + SEC2MS(180))
			listTrackedRequests.RemoveTail();
	}
}

bool CPacketTracking::IsTrackedOutListRequestPacket(uint8 byOpcode)
{
	switch (byOpcode) {
	case KADEMLIA2_BOOTSTRAP_REQ:
	case KADEMLIA2_HELLO_REQ:
	case KADEMLIA2_HELLO_RES:
	case KADEMLIA2_REQ:
	case KADEMLIA_SEARCH_NOTES_REQ:
	case KADEMLIA2_SEARCH_NOTES_REQ:
	case KADEMLIA_PUBLISH_REQ:
	case KADEMLIA2_PUBLISH_KEY_REQ:
	case KADEMLIA2_PUBLISH_SOURCE_REQ:
	case KADEMLIA2_PUBLISH_NOTES_REQ:
	case KADEMLIA_FINDBUDDY_REQ:
	case KADEMLIA_CALLBACK_REQ:
	case KADEMLIA2_PING:
		return true;
	}
	return false;
}

bool CPacketTracking::IsOnOutTrackList(uint32 dwIP, uint8 byOpcode, bool bDontRemove)
{
#ifdef _DEBUG
	if (!IsTrackedOutListRequestPacket(byOpcode))
		ASSERT(0); // code error / bug
#endif
	const ULONGLONG curTick = ::GetTickCount64();
	for (POSITION pos = listTrackedRequests.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		const TrackPackets_Struct &req = listTrackedRequests.GetNext(pos);
		if (curTick >= req.dwInserted + SEC2MS(180))
			break;
		if (req.dwIP == dwIP && req.byOpcode == byOpcode) {
			if (!bDontRemove)
				listTrackedRequests.RemoveAt(pos2);
			return true;
		}
	}
	return false;
}

//return codes: 0 - no error; 1 - flood; 2 - massive flood
int CPacketTracking::InTrackListIsAllowedPacket(uint32 uIP, uint8 byOpcode, bool /*bValidSenderkey*/)
{
	// this tracklist monitors _incoming_ request packets and acts as a general flood protection
	// by dropping too frequent requests from a single IP, thus avoiding response floods, saving CPU time,
	// preventing DoS attacks and slowing down other possible attacks/behaviour
	// (scanning indexed files, fake publish floods, etc)

	// first figure out if this is a request packet to be tracked
	// time limits are chosen by estimating the max. frequency of such packets in normal operation (+ buffer)
	// (those limits are not meant be fine for normal usage, but only supposed to be a flood detection)
	//
	// Tokens are calculated as the number of milliseconds corresponding to the allowed quantity of packets per minute.
	int token;
	const byte byDbgOrgOpcode = byOpcode;
	switch (byOpcode) {
	case KADEMLIA2_BOOTSTRAP_REQ:
		token = MIN2MS(1) / 2;
		break;
	case KADEMLIA2_HELLO_REQ:
		token = MIN2MS(1) / 3;
		break;
	case KADEMLIA2_REQ:
		token = MIN2MS(1) / 10;
		break;
	case KADEMLIA2_SEARCH_NOTES_REQ:
	case KADEMLIA2_SEARCH_KEY_REQ:
	case KADEMLIA2_SEARCH_SOURCE_REQ:
		token = MIN2MS(1) / 3;
		break;
	case KADEMLIA2_PUBLISH_KEY_REQ:
		token = MIN2MS(1) / 4;
		break;
	case KADEMLIA2_PUBLISH_SOURCE_REQ:
		token = MIN2MS(1) / 3;
		break;
	case KADEMLIA2_PUBLISH_NOTES_REQ:
		token = MIN2MS(1) / 2;
		break;
	case KADEMLIA_FIREWALLED2_REQ:
		byOpcode = KADEMLIA_FIREWALLED_REQ;
		[[fallthrough]];
	case KADEMLIA_FIREWALLED_REQ:
	case KADEMLIA_FINDBUDDY_REQ:
		token = MIN2MS(1) / 2;
		break;
	case KADEMLIA_CALLBACK_REQ:
		token = MIN2MS(1) / 1;
		break;
	case KADEMLIA2_PING:
		token = MIN2MS(1) / 2;
		break;
	default:
		// not a request packets, but a response - no further checks at this point
		return 0;
	}
	const ULONGLONG curTick = ::GetTickCount64();
	// time for cleaning up?
	if (curTick >= dwLastTrackInCleanup + MIN2MS(12))
		InTrackListCleanup();

	// check for existing entries
	TrackPacketsIn_Struct *pTrackEntry;
	if (!m_mapTrackPacketsIn.Lookup(uIP, pTrackEntry)) {
		std::unique_ptr<TrackPacketsIn_Struct> pNewTrackEntry(new TrackPacketsIn_Struct());
		pNewTrackEntry->m_uIP = uIP;
		// WHY: the cleanup path owns TrackPacketsIn_Struct through
		// m_liTrackPacketsIn, while lookup uses m_mapTrackPacketsIn. AddHead can
		// allocate and CMap assignment can allocate; keep a local owner and roll
		// back the list link so failure cannot leave either container with the
		// only reference to an unreachable tracking object.
		POSITION posLinked = m_liTrackPacketsIn.AddHead(pNewTrackEntry.get());
		try {
			m_mapTrackPacketsIn[uIP] = pNewTrackEntry.get();
		} catch (...) {
			m_liTrackPacketsIn.RemoveAt(posLinked);
			throw;
		}
		pTrackEntry = pNewTrackEntry.release();
	}

	INT_PTR i = pTrackEntry->m_aTrackedRequests.GetCount();
	// search for the specific request track
	while (--i >= 0 && pTrackEntry->m_aTrackedRequests[i].m_byOpcode != byOpcode);

	if (i >= 0) {
		// already tracking requests with this opcode
		TrackPacketsIn_Struct::TrackedRequestIn_Struct &TrackedRequest = pTrackEntry->m_aTrackedRequests[i];
		TrackedRequest.m_tokens += static_cast<LONGLONG>(curTick - TrackedRequest.m_dwLatest);
		if (TrackedRequest.m_tokens > MIN2MS(1))
			TrackedRequest.m_tokens = MIN2MS(1);
		TrackedRequest.m_tokens -= token;
		TrackedRequest.m_dwLatest = curTick;
		// remember only for easier cleanup
		const ULONGLONG uTrackedRequestDeficit = TrackedRequest.m_tokens < 0 ? static_cast<ULONGLONG>(-TrackedRequest.m_tokens) : static_cast<ULONGLONG>(TrackedRequest.m_tokens);
		pTrackEntry->m_dwLastExpire = max(pTrackEntry->m_dwLastExpire, curTick + uTrackedRequestDeficit + static_cast<ULONGLONG>(token));

		if (CKademlia::IsRunningInLANMode() && IsLANIP(ntohl(uIP))) // no flood detection in LanMode
			return 0;

		// now the actual check if this request is allowed
		if (TrackedRequest.m_tokens < 0) {
			if (TrackedRequest.m_tokens < MIN2MS(-3)) {
				// this is so far above the limit that has to be an intentional flood / misuse
				// so we take higher level of punishment and ban the IP
				DebugLogWarning(_T("Kad: Massive request flood detected for opcode 0x%X (0x%X) from IP %s - Banning IP"), byOpcode, byDbgOrgOpcode, (LPCTSTR)ipstr(htonl(uIP)));
				EMULEBB_KAD_LOG_PACKET_EVENT(_T("kad_request_massive_flood"), _T("error"), htonl(uIP), byOpcode, byDbgOrgOpcode, _T("ban-ip"), _T("request-token-deficit"), TrackedRequest.m_tokens);
				theApp.clientlist->AddBannedClient(ntohl(uIP));
				return 2; // drop the packet, remove the contact from routing
			}
			// over the limit, drop the packet but do nothing else
			if (!TrackedRequest.m_bDbgLogged) {
				TrackedRequest.m_bDbgLogged = true;
				DebugLog(_T("Kad: Request flood detected for opcode 0x%X (0x%X) from IP %s - Dropping packets with this opcode"), byOpcode, byDbgOrgOpcode, (LPCTSTR)ipstr(htonl(uIP)));
				EMULEBB_KAD_LOG_PACKET_EVENT(_T("kad_request_flood"), _T("warning"), htonl(uIP), byOpcode, byDbgOrgOpcode, _T("drop-opcode"), _T("request-token-deficit"), TrackedRequest.m_tokens);
			}
			return 1; // drop the packet
		}
		TrackedRequest.m_bDbgLogged = false;
	} else {
		// add a new entry for this request
		TrackPacketsIn_Struct::TrackedRequestIn_Struct TrackedRequest =	{curTick, MIN2MS(1) - token, byOpcode, false};
		pTrackEntry->m_aTrackedRequests.Add(TrackedRequest);
	}
	return 0;
}

void CPacketTracking::InTrackListCleanup()
{
	const ULONGLONG curTick = ::GetTickCount64();
	const INT_PTR dbgOldSize = m_liTrackPacketsIn.GetCount();
	dwLastTrackInCleanup = curTick;
	for (POSITION pos = m_liTrackPacketsIn.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		const TrackPacketsIn_Struct *curEntry = m_liTrackPacketsIn.GetNext(pos);
		if (curTick >= curEntry->m_dwLastExpire) {
			VERIFY(m_mapTrackPacketsIn.RemoveKey(curEntry->m_uIP));
			m_liTrackPacketsIn.RemoveAt(pos2);
			delete curEntry;
		}
	}
	DebugLog(_T("Cleaned up Kad Incoming Requests Tracklist, entries before: %u, after %u"), (uint32)dbgOldSize, (uint32)m_liTrackPacketsIn.GetCount());
}

void CPacketTracking::AddLegacyChallenge(const CUInt128 &uContactID, const CUInt128 &uChallengeID, uint32 uIP, uint8 byOpcode)
{
	const ULONGLONG curTick = ::GetTickCount64();
	listChallengeRequests.AddHead(TrackChallenge_Struct{curTick, uIP, uContactID, uChallengeID, byOpcode});
	while (!listChallengeRequests.IsEmpty() && curTick >= listChallengeRequests.GetTail().dwInserted + SEC2MS(180)) {
		DEBUG_ONLY(DebugLog(_T("Challenge timed out, client not verified - %s"), (LPCTSTR)ipstr(htonl(listChallengeRequests.GetTail().uIP))));
		listChallengeRequests.RemoveTail();
	}
}

bool CPacketTracking::IsLegacyChallenge(const CUInt128 &uChallengeID, uint32 uIP, uint8 byOpcode, CUInt128 &ruContactID)
{
	bool bDbgWarning = false;
	const ULONGLONG curTick = ::GetTickCount64();
	for (POSITION pos = listChallengeRequests.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		const TrackChallenge_Struct &tc = listChallengeRequests.GetNext(pos);
		if (curTick >= tc.dwInserted + SEC2MS(180))
			break;
		if (tc.uIP == uIP && tc.byOpcode == byOpcode) {
			ASSERT(tc.uChallenge != 0 || byOpcode == KADEMLIA2_PING);
			if (tc.uChallenge == 0 || tc.uChallenge == uChallengeID) {
				ruContactID = tc.uContactID;
				listChallengeRequests.RemoveAt(pos2);
				return true;
			}
			bDbgWarning = true;
		}
	}
	if (bDbgWarning)
		DebugLogWarning(_T("Kad: IsLegacyChallenge: Wrong challenge answer received, client not verified (%s)"), (LPCTSTR)ipstr(htonl(uIP)));
	return false;
}

bool CPacketTracking::HasActiveLegacyChallenge(uint32 uIP) const
{
	const ULONGLONG curTick = ::GetTickCount64();
	for (POSITION pos = listChallengeRequests.GetHeadPosition(); pos != NULL;) {
		const TrackChallenge_Struct &tcstruct = listChallengeRequests.GetNext(pos);
		if (curTick >= tcstruct.dwInserted + SEC2MS(180))
			break;
		if (tcstruct.uIP == uIP)
			return true;
	}
	return false;
}
