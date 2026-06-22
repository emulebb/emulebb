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

#if EMULEBB_HAS_DIAG_EVENT_V1
// Maps a master kad_event_v1 event name onto the coarse diag_event_v1 kad_event
// bucket the harness aligns on (bootstrap/lookup/publish/firewall/buddy). The
// full master event name is preserved as the comparable `milestone` field.
LPCTSTR ClassifyKadEvent(LPCTSTR pszEvent)
{
	const CString strEvent(pszEvent != NULL ? pszEvent : _T(""));
	if (strEvent.Find(_T("bootstrap")) >= 0)
		return _T("bootstrap");
	if (strEvent.Find(_T("search")) >= 0 || strEvent.Find(_T("lookup")) >= 0)
		return _T("lookup");
	if (strEvent.Find(_T("publish")) >= 0)
		return _T("publish");
	if (strEvent.Find(_T("firewall")) >= 0)
		return _T("firewall");
	if (strEvent.Find(_T("buddy")) >= 0)
		return _T("buddy");
	return _T("lookup");
}

void ReEmitKadEventDiagV1(LPCTSTR pszEvent, LPCTSTR pszSeverity, LPCTSTR pszAction, LPCTSTR pszReason, uint32 uNetworkIP)
{
	if (!theDiagEventV1Log.IsOpen())
		return;

	CString strKeys(_T("{}"));
	if (uNetworkIP != 0)
		strKeys.Format(_T("{\"peer\":%s}"), (LPCTSTR)BuildDiagnosticsJsonStringField(ipstr(uNetworkIP)));

	CString strBody;
	strBody.Format(
		_T("{\"milestone\":%s,\"action\":%s,\"reason\":%s}"),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszEvent),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszAction != NULL ? pszAction : _T("observe")),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszReason != NULL ? pszReason : _T("")));
	WriteDiagEventV1(_T("kad_event"), ClassifyKadEvent(pszEvent),
		pszSeverity != NULL ? pszSeverity : _T("info"), strKeys, strBody);
}
#endif
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

#if EMULEBB_HAS_DIAG_EVENT_V1
	if (theDiagEventV1Log.IsOpen()) {
		CString strBody;
		strBody.Format(
			_T("{\"milestone\":\"routing_summary\",\"action\":\"observe\",\"connected\":%s,\"bootstrapping\":%s,\"firewalled\":%s,\"lanMode\":%s,\"contactTotal\":%u,\"contactVerified\":%u,\"contactWithUdpKey\":%u}"),
			BoolJson(bConnected),
			BoolJson(bBootstrapping),
			BoolJson(bFirewalled),
			BoolJson(bLanMode),
			routingSummary.uTotal,
			routingSummary.uVerified,
			routingSummary.uWithUdpKey);
		WriteDiagEventV1(_T("kad_event"), _T("routing_summary"), _T("info"), CString(_T("{}")), strBody);
	}
#endif
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
#if EMULEBB_HAS_DIAG_EVENT_V1
	ReEmitKadEventDiagV1(pszEvent, pszSeverity, pszAction, pszReason, uNetworkIP);
#endif
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
#if EMULEBB_HAS_DIAG_EVENT_V1
	if (theDiagEventV1Log.IsOpen()) {
		CString strKeys;
		strKeys.Format(_T("{\"searchId\":%u}"), uSearchID);
		CString strBody;
		strBody.Format(
			_T("{\"milestone\":%s,\"action\":%s,\"reason\":%s,\"searchType\":%u,\"resultCount\":%u,\"expectedCount\":%u}"),
			(LPCTSTR)BuildDiagnosticsJsonStringField(pszEvent),
			(LPCTSTR)BuildDiagnosticsJsonStringField(pszAction != NULL ? pszAction : _T("observe")),
			(LPCTSTR)BuildDiagnosticsJsonStringField(pszReason != NULL ? pszReason : _T("")),
			uSearchType,
			uResultCount,
			uExpectedCount);
		WriteDiagEventV1(_T("kad_event"), _T("lookup"), pszSeverity != NULL ? pszSeverity : _T("info"), strKeys, strBody);
	}
#endif
}

void LogKadPublish(
	EKadPublishKind eKind,
	const uchar *pFileHash,
	UINT uFileCount)
{
#if EMULEBB_HAS_DIAG_EVENT_V1
	if (!theDiagEventV1Log.IsOpen())
		return;

	// Event/publishKind/milestone strings mirror the rust diag_kad_event
	// KadPublishKind byte-for-byte so the converged diff aligns on identical
	// field shapes.
	LPCTSTR pszEvent = _T("kad_keyword_publish");
	LPCTSTR pszPublishKind = _T("keyword");
	LPCTSTR pszMilestone = _T("keyword_published");
	switch (eKind) {
	case EKadPublishKind::Source:
		pszEvent = _T("kad_source_publish");
		pszPublishKind = _T("source");
		pszMilestone = _T("source_published");
		break;
	case EKadPublishKind::Notes:
		pszEvent = _T("kad_notes_publish");
		pszPublishKind = _T("notes");
		pszMilestone = _T("notes_published");
		break;
	case EKadPublishKind::Keyword:
	default:
		break;
	}

	// keys.fileHash is lower-case hex of the 16-byte eD2k MD4, matching the rust
	// keys.fileHash encoding.
	CString strKeys;
	strKeys.Format(
		_T("{\"fileHash\":%s}"),
		(LPCTSTR)BuildDiagnosticsJsonStringField(BuildDiagEventLowerHexString(pFileHash, 16)));

	// STRUCTURAL DIFFERENCE vs rust diag_kad_event::publish: the master fires an
	// async STORE* search here and does not know the per-contact reach/ack/timeout/
	// fail counts at the publish-initiation site (they arrive later via search
	// responses). Those fields (closestContactsConsidered/attemptedContacts/
	// ackedContacts/timedOutContacts/failedContacts) are intentionally omitted
	// rather than fabricated; body.fileCount is the master-only file fan-out of a
	// keyword STORE (1 for source/notes).
	CString strBody;
	strBody.Format(
		_T("{\"milestone\":%s,\"action\":\"publish\",\"publishKind\":%s,\"fileCount\":%u}"),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszMilestone),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszPublishKind),
		uFileCount);

	WriteDiagEventV1(_T("kad_event"), pszEvent, _T("info"), strKeys, strBody);
#else
	(void)eKind;
	(void)pFileHash;
	(void)uFileCount;
#endif
}

void LogKadPublishRound(
	UINT uItemCount,
	UINT uKeywordPublished,
	UINT uSourcePublished,
	UINT uNotesPublished)
{
#if EMULEBB_HAS_DIAG_EVENT_V1
	if (!theDiagEventV1Log.IsOpen())
		return;

	// STRUCTURAL DIFFERENCE vs rust diag_kad_event::publish_round: the master
	// publishes at most one keyword set / one source / one notes per Publish() tick
	// and does not aggregate per-contact ack counts, so keywordAckedContacts/
	// sourceAckedContacts/notesAckedContacts are intentionally omitted (the rust
	// rollup carries them). itemCount and the *Published counts match rust.
	CString strBody;
	strBody.Format(
		_T("{\"milestone\":\"publish_round\",\"action\":\"observe\",\"itemCount\":%u,\"keywordPublished\":%u,\"sourcePublished\":%u,\"notesPublished\":%u}"),
		uItemCount,
		uKeywordPublished,
		uSourcePublished,
		uNotesPublished);

	WriteDiagEventV1(_T("kad_event"), _T("kad_publish_round"), _T("info"), CString(_T("{}")), strBody);
#else
	(void)uItemCount;
	(void)uKeywordPublished;
	(void)uSourcePublished;
	(void)uNotesPublished;
#endif
}
}
#endif
