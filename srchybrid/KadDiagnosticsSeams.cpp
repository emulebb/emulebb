//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
#include "stdafx.h"
#include "KadDiagnosticsSeams.h"

#if EMULEBB_HAS_KAD_DIAGNOSTICS
#include "Log.h"
#include "Opcodes.h"
#include "OtherFunctions.h"
#include "kademlia/kademlia/Kademlia.h"
#include "kademlia/routing/Contact.h"
#include "kademlia/routing/RoutingZone.h"
#include "kademlia/utils/KadUDPKey.h"

namespace
{
CLogFile g_kadDiagnosticsLog;
CCriticalSection g_kadDiagnosticsLogLock;
volatile LONGLONG g_llKadDiagnosticsEventSeq = 0;
ULONGLONG g_ullLastKadSummaryTick = 0;

constexpr ULONGLONG kKadSummaryIntervalMs = SEC2MS(30);
constexpr UINT kKadKnownVersionSlots = 16;
constexpr UINT kKadKnownTypeSlots = 5;

CString JsonString(LPCTSTR pszValue)
{
	return BuildDiagnosticsJsonStringField(pszValue);
}

LPCTSTR BoolJson(bool bValue)
{
	return bValue ? _T("true") : _T("false");
}

UINT GetKadDistanceBucket(const Kademlia::CUInt128 &uDistance)
{
	for (UINT uBit = 0; uBit < 128; ++uBit)
		if (uDistance.GetBitNumber(uBit) != 0)
			return uBit;
	return 128;
}

CString JsonUIntArray(const UINT *puValues, UINT uCount)
{
	CString strJson(_T("["));
	for (UINT uIndex = 0; uIndex < uCount; ++uIndex) {
		CString strValue;
		strValue.Format(_T("%u"), puValues[uIndex]);
		if (uIndex != 0)
			strJson += _T(",");
		strJson += strValue;
	}
	strJson += _T("]");
	return strJson;
}

CString ContactJson(const Kademlia::CContact *pContact)
{
	if (pContact == NULL)
		return CString(_T("null"));

	const time_t tNow = time(NULL);
	const time_t tCreated = pContact->GetCreatedTime();
	const time_t tLastSeen = pContact->GetLastSeen();
	const time_t tExpires = pContact->GetExpireTime();
	const LONGLONG llAgeSeconds = tNow >= tCreated ? static_cast<LONGLONG>(tNow - tCreated) : 0;
	const LONGLONG llLastSeenAgeSeconds = tNow >= tLastSeen ? static_cast<LONGLONG>(tNow - tLastSeen) : 0;
	const LONGLONG llExpiresInSeconds = tExpires >= tNow ? static_cast<LONGLONG>(tExpires - tNow) : -static_cast<LONGLONG>(tNow - tExpires);
	const UINT uDistanceBucket = GetKadDistanceBucket(pContact->GetDistance());
	const UINT uLocalQualityScore = pContact->GetLocalQualityScore(tNow);
	const bool bHasUdpKey = !pContact->GetUDPKey().IsEmpty();

	CString strJson;
	strJson.Format(
		_T("{\"node_id\":%s,\"address\":%s,\"udp_port\":%u,\"tcp_port\":%u,\"version\":%u,\"type\":%u,\"ip_verified\":%s,\"received_hello\":%s,\"bootstrap\":%s,\"has_udp_key\":%s,\"distance_bucket\":%u,\"local_quality_score\":%u,\"age_seconds\":%I64d,\"last_seen_age_seconds\":%I64d,\"expires_in_seconds\":%I64d}"),
		(LPCTSTR)JsonString(pContact->GetClientID().ToHexString()),
		(LPCTSTR)JsonString(ipstr(pContact->GetNetIP())),
		static_cast<UINT>(pContact->GetUDPPort()),
		static_cast<UINT>(pContact->GetTCPPort()),
		static_cast<UINT>(pContact->GetVersion()),
		static_cast<UINT>(pContact->GetType()),
		BoolJson(pContact->IsIpVerified()),
		BoolJson(pContact->GetReceivedHelloPacket()),
		BoolJson(pContact->IsBootstrapContact()),
		BoolJson(bHasUdpKey),
		uDistanceBucket,
		uLocalQualityScore,
		llAgeSeconds,
		llLastSeenAgeSeconds,
		llExpiresInSeconds);
	return strJson;
}

CString RawContactJson(uint32 uHostIP, uint16 uUDPPort, uint16 uTCPPort, uint8 uVersion)
{
	CString strJson;
	strJson.Format(
		_T("{\"address\":%s,\"udp_port\":%u,\"tcp_port\":%u,\"version\":%u}"),
		(LPCTSTR)JsonString(uHostIP != 0 ? ipstr(htonl(uHostIP)) : CString()),
		static_cast<UINT>(uUDPPort),
		static_cast<UINT>(uTCPPort),
		static_cast<UINT>(uVersion));
	return strJson;
}

struct SKadContactSummary
{
	UINT uTotal = 0;
	UINT uVerified = 0;
	UINT uUnverified = 0;
	UINT uReceivedHello = 0;
	UINT uWithUdpKey = 0;
	UINT uBootstrap = 0;
	UINT uLegacyBeforeVersion6 = 0;
	UINT uModernVersion8OrNewer = 0;
	UINT uCurrentVersion = 0;
	UINT uExpiredType = 0;
	UINT uMaxDistanceBucket = 0;
	UINT auVersion[kKadKnownVersionSlots] = {};
	UINT uVersionOther = 0;
	UINT auType[kKadKnownTypeSlots] = {};
	UINT uTypeOther = 0;
};

void AccumulateContact(SKadContactSummary &rSummary, const Kademlia::CContact *pContact)
{
	if (pContact == NULL)
		return;

	++rSummary.uTotal;
	const UINT uVersion = pContact->GetVersion();
	if (uVersion < kKadKnownVersionSlots)
		++rSummary.auVersion[uVersion];
	else
		++rSummary.uVersionOther;
	if (uVersion >= KADEMLIA_VERSION8_49b)
		++rSummary.uModernVersion8OrNewer;
	if (uVersion >= KADEMLIA_VERSION2_47a && uVersion < KADEMLIA_VERSION6_49aBETA)
		++rSummary.uLegacyBeforeVersion6;
	if (uVersion == KADEMLIA_VERSION)
		++rSummary.uCurrentVersion;

	const UINT uType = pContact->GetType();
	if (uType < kKadKnownTypeSlots)
		++rSummary.auType[uType];
	else
		++rSummary.uTypeOther;
	if (uType == 4)
		++rSummary.uExpiredType;

	if (pContact->IsIpVerified())
		++rSummary.uVerified;
	else
		++rSummary.uUnverified;
	if (pContact->GetReceivedHelloPacket())
		++rSummary.uReceivedHello;
	if (!pContact->GetUDPKey().IsEmpty())
		++rSummary.uWithUdpKey;
	if (pContact->IsBootstrapContact())
		++rSummary.uBootstrap;
	rSummary.uMaxDistanceBucket = max(rSummary.uMaxDistanceBucket, GetKadDistanceBucket(pContact->GetDistance()));
}

CString SummaryJson(const SKadContactSummary &rSummary)
{
	CString strJson;
	strJson.Format(
		_T("{\"total\":%u,\"verified\":%u,\"unverified\":%u,\"received_hello\":%u,\"with_udp_key\":%u,\"bootstrap\":%u,\"legacy_v2_to_v5\":%u,\"modern_v8_or_newer\":%u,\"current_v10\":%u,\"expired_type\":%u,\"max_distance_bucket\":%u,\"version_histogram\":%s,\"version_other\":%u,\"type_histogram\":%s,\"type_other\":%u}"),
		rSummary.uTotal,
		rSummary.uVerified,
		rSummary.uUnverified,
		rSummary.uReceivedHello,
		rSummary.uWithUdpKey,
		rSummary.uBootstrap,
		rSummary.uLegacyBeforeVersion6,
		rSummary.uModernVersion8OrNewer,
		rSummary.uCurrentVersion,
		rSummary.uExpiredType,
		rSummary.uMaxDistanceBucket,
		(LPCTSTR)JsonUIntArray(rSummary.auVersion, kKadKnownVersionSlots),
		rSummary.uVersionOther,
		(LPCTSTR)JsonUIntArray(rSummary.auType, kKadKnownTypeSlots),
		rSummary.uTypeOther);
	return strJson;
}

void WriteEvent(
	LPCTSTR pszEvent,
	LPCTSTR pszSeverity,
	const CString &strContactJson,
	LPCTSTR pszAction,
	LPCTSTR pszReason,
	LPCTSTR pszEvidenceJson)
{
	WriteDiagnosticsJsonEvent(
		g_kadDiagnosticsLog,
		g_kadDiagnosticsLogLock,
		g_llKadDiagnosticsEventSeq,
		_T("kad_event_v1"),
		KadDiagnosticsSeams::kBinaryMarker,
		pszEvent,
		pszSeverity,
		_T("contact"),
		strContactJson,
		NULL,
		CString(),
		pszAction,
		pszReason,
		pszEvidenceJson);
}
}

namespace KadDiagnosticsSeams
{
void InitializeLog(LPCTSTR pszLogPath, UINT uMaxLogFileSize)
{
	if (pszLogPath == NULL || pszLogPath[0] == _T('\0'))
		return;

	InitializeDiagnosticsLog(g_kadDiagnosticsLog, pszLogPath, uMaxLogFileSize);
}

bool IsEnabled()
{
	return g_kadDiagnosticsLog.IsOpen();
}

void LogRoutingSummary(
	const Kademlia::CRoutingZone *pRoutingZone,
	const Kademlia::_ContactList &rBootstrapContacts,
	bool bConnected,
	bool bBootstrapping,
	bool bFirewalled,
	bool bLanMode)
{
	if (!g_kadDiagnosticsLog.IsOpen())
		return;

	const ULONGLONG curTick = ::GetTickCount64();
	if (g_ullLastKadSummaryTick != 0 && curTick < g_ullLastKadSummaryTick + kKadSummaryIntervalMs)
		return;
	g_ullLastKadSummaryTick = curTick;

	SKadContactSummary routingSummary;
	if (pRoutingZone != NULL) {
		Kademlia::ContactArray contacts;
		const_cast<Kademlia::CRoutingZone *>(pRoutingZone)->GetAllEntries(contacts);
		for (Kademlia::ContactArray::const_iterator itContact = contacts.begin(); itContact != contacts.end(); ++itContact)
			AccumulateContact(routingSummary, *itContact);
	}

	SKadContactSummary bootstrapSummary;
	for (POSITION pos = rBootstrapContacts.GetHeadPosition(); pos != NULL;)
		AccumulateContact(bootstrapSummary, rBootstrapContacts.GetNext(pos));

	const ULONGLONG ullEventSeq = NextDiagnosticsEventSeq(g_llKadDiagnosticsEventSeq);
	CString strJson;
	strJson.Format(
		_T("{\"schema\":\"kad_routing_summary_v1\",\"source\":\"emulebb\",\"marker\":\"%s\",\"ts_utc\":%s,\"event_seq\":%I64u,\"connected\":%s,\"bootstrapping\":%s,\"firewalled\":%s,\"lan_mode\":%s,\"routing\":%s,\"bootstrap_queue\":%s}\r\n"),
		kBinaryMarker,
		(LPCTSTR)JsonString(BuildDiagnosticsTimestampUtc()),
		ullEventSeq,
		BoolJson(bConnected),
		BoolJson(bBootstrapping),
		BoolJson(bFirewalled),
		BoolJson(bLanMode),
		(LPCTSTR)SummaryJson(routingSummary),
		(LPCTSTR)SummaryJson(bootstrapSummary));
	WriteDiagnosticsLogLine(g_kadDiagnosticsLog, g_kadDiagnosticsLogLock, strJson);
}

void LogContactEvent(
	LPCTSTR pszEvent,
	LPCTSTR pszSeverity,
	const Kademlia::CContact *pContact,
	LPCTSTR pszAction,
	LPCTSTR pszReason,
	LPCTSTR pszEvidenceJson)
{
	WriteEvent(pszEvent, pszSeverity, ContactJson(pContact), pszAction, pszReason, pszEvidenceJson);
}

void LogRawContactEvent(
	LPCTSTR pszEvent,
	LPCTSTR pszSeverity,
	uint32 uHostIP,
	uint16 uUDPPort,
	uint16 uTCPPort,
	uint8 uVersion,
	LPCTSTR pszAction,
	LPCTSTR pszReason,
	LPCTSTR pszEvidenceJson)
{
	WriteEvent(pszEvent, pszSeverity, RawContactJson(uHostIP, uUDPPort, uTCPPort, uVersion), pszAction, pszReason, pszEvidenceJson);
}

void LogPacketEvent(
	LPCTSTR pszEvent,
	LPCTSTR pszSeverity,
	uint32 uNetworkIP,
	uint8 byOpcode,
	uint8 byOriginalOpcode,
	LPCTSTR pszAction,
	LPCTSTR pszReason,
	LONGLONG llTokens)
{
	CString strContactJson;
	strContactJson.Format(_T("{\"address\":%s}"), (LPCTSTR)JsonString(uNetworkIP != 0 ? ipstr(uNetworkIP) : CString()));

	CString strEvidence;
	strEvidence.Format(
		_T("{\"opcode\":%u,\"original_opcode\":%u,\"tokens_ms\":%I64d}"),
		static_cast<UINT>(byOpcode),
		static_cast<UINT>(byOriginalOpcode),
		llTokens);
	WriteEvent(pszEvent, pszSeverity, strContactJson, pszAction, pszReason, strEvidence);
}

void LogSearchResponseEvent(
	LPCTSTR pszEvent,
	LPCTSTR pszSeverity,
	uint32 uSearchID,
	uint32 uSearchType,
	uint32 uHostIP,
	uint16 uUDPPort,
	uint8 uKadVersion,
	UINT uResultCount,
	UINT uExpectedCount,
	LPCTSTR pszAction,
	LPCTSTR pszReason)
{
	CString strContactJson;
	strContactJson.Format(
		_T("{\"address\":%s,\"udp_port\":%u,\"version\":%u}"),
		(LPCTSTR)JsonString(uHostIP != 0 ? ipstr(htonl(uHostIP)) : CString()),
		static_cast<UINT>(uUDPPort),
		static_cast<UINT>(uKadVersion));

	CString strEvidence;
	strEvidence.Format(
		_T("{\"search_id\":%u,\"search_type\":%u,\"result_count\":%u,\"expected_count\":%u}"),
		uSearchID,
		uSearchType,
		uResultCount,
		uExpectedCount);
	WriteEvent(pszEvent, pszSeverity, strContactJson, pszAction, pszReason, strEvidence);
}
}
#endif
