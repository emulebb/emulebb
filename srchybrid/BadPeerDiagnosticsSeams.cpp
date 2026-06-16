//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
#include "stdafx.h"
#include "BadPeerDiagnosticsSeams.h"

#if EMULEBB_HAS_BAD_PEER_DIAGNOSTICS
#include "AbstractFile.h"
#include "Log.h"
#include "Opcodes.h"
#include "OtherFunctions.h"
#include "PartFile.h"
#include "SearchFile.h"
#include "UpdownClient.h"

#include <map>

namespace
{
CLogFile g_badPeerDiagnosticsLog;
CCriticalSection g_badPeerDiagnosticsLogLock;
CCriticalSection g_badPeerBehaviorLedgerLock;
volatile LONGLONG g_llBadPeerDiagnosticsEventSeq = 0;
ULONGLONG g_ullBadPeerBehaviorLedgerLastCleanup = 0;

constexpr ULONGLONG kBadPeerBehaviorLedgerWindowMs = MIN2MS(60);
constexpr ULONGLONG kBadPeerBehaviorLedgerCleanupMs = SEC2MS(60);

struct SBadPeerBehaviorLedgerState
{
	ULONGLONG ullFirstSeen;
	ULONGLONG ullLastSeen;
	UINT uCount;
};

struct CStringLess
{
	bool operator()(const CString &strLeft, const CString &strRight) const
	{
		return strLeft.Compare(strRight) < 0;
	}
};

std::map<CString, SBadPeerBehaviorLedgerState, CStringLess> g_badPeerBehaviorLedger;

CString JsonString(LPCTSTR pszValue)
{
	return BuildDiagnosticsJsonStringField(pszValue);
}

CString JsonHashOrNull(const uchar *pHash)
{
	if (pHash == NULL || isnulmd4(pHash))
		return CString(_T("null"));
	return JsonString(md4str(pHash));
}

CString HashOrEmpty(const uchar *pHash)
{
	if (pHash == NULL || isnulmd4(pHash))
		return CString();
	return md4str(pHash);
}

CString PeerBehaviorKey(const CUpDownClient *pClient)
{
	if (pClient == NULL)
		return CString();
	if (pClient->HasValidHash())
		return CString(_T("hash:")) + md4str(pClient->GetUserHash());
	const uint32 dwIP = pClient->GetIP() != 0 ? pClient->GetIP() : pClient->GetConnectIP();
	if (dwIP == 0)
		return CString();
	return CString(_T("ip:")) + ipstr(dwIP);
}

CString FileBehaviorHash(const CAbstractFile *pFile, const Requested_Block_Struct *pBlock)
{
	if (pFile != NULL)
		return HashOrEmpty(pFile->GetFileHash());
	if (pBlock != NULL)
		return HashOrEmpty(pBlock->FileID);
	return CString();
}

CString ClientJson(const CUpDownClient *pClient)
{
	if (pClient == NULL)
		return CString(_T("null"));

	CString strJson;
	strJson.Format(
		_T("{\"address\":%s,\"connect_ip\":%s,\"user_port\":%u,\"user_hash\":%s,\"user_name\":%s,\"client_software\":%s,\"client_mod\":%s,\"download_state\":%s,\"upload_state\":%s,\"session_down\":%I64u,\"session_payload_down\":%I64u,\"session_up\":%I64u,\"payload_in_buffer\":%I64u}"),
		(LPCTSTR)JsonString(ipstr(pClient->GetConnectIP())),
		(LPCTSTR)JsonString(ipstr(pClient->GetIP())),
		static_cast<UINT>(pClient->GetUserPort()),
		(LPCTSTR)JsonHashOrNull(pClient->HasValidHash() ? pClient->GetUserHash() : NULL),
		(LPCTSTR)JsonString(pClient->GetUserName()),
		(LPCTSTR)JsonString(pClient->GetClientSoftVer()),
		(LPCTSTR)JsonString(pClient->GetClientModVer()),
		(LPCTSTR)JsonString(pClient->DbgGetDownloadState()),
		(LPCTSTR)JsonString(pClient->DbgGetUploadState()),
		pClient->GetSessionDown(),
		pClient->GetSessionPayloadDown(),
		pClient->GetSessionUp(),
		pClient->GetPayloadInBuffer());
	return strJson;
}

CString FileJson(const CAbstractFile *pFile)
{
	if (pFile == NULL)
		return CString(_T("null"));

	CString strJson;
	strJson.Format(
		_T("{\"hash\":%s,\"name\":%s,\"size\":%I64u,\"type\":%s}"),
		(LPCTSTR)JsonHashOrNull(pFile->GetFileHash()),
		(LPCTSTR)JsonString(pFile->GetFileName()),
		static_cast<uint64>(pFile->GetFileSize()),
		(LPCTSTR)JsonString(pFile->GetFileType()));
	return strJson;
}

CString SearchJson(const CSearchFile *pSearchFile)
{
	if (pSearchFile == NULL)
		return CString(_T("null"));

	CString strJson;
	strJson.Format(
		_T("{\"hash\":%s,\"name\":%s,\"size\":%I64u,\"type\":%s,\"search_id\":%u,\"source_count\":%u,\"complete_source_count\":%u,\"client_ip\":%s,\"client_port\":%u,\"server_ip\":%s,\"server_port\":%u,\"spam_rating\":%u,\"considered_spam\":%s,\"kad\":%s,\"server_udp_answer\":%s}"),
		(LPCTSTR)JsonHashOrNull(pSearchFile->GetFileHash()),
		(LPCTSTR)JsonString(pSearchFile->GetFileName()),
		static_cast<uint64>(pSearchFile->GetFileSize()),
		(LPCTSTR)JsonString(pSearchFile->GetFileType()),
		pSearchFile->GetSearchID(),
		pSearchFile->GetSourceCount(),
		pSearchFile->GetCompleteSourceCount(),
		(LPCTSTR)JsonString(pSearchFile->GetClientID() != 0 ? ipstr(pSearchFile->GetClientID()) : CString()),
		static_cast<UINT>(pSearchFile->GetClientPort()),
		(LPCTSTR)JsonString(pSearchFile->GetClientServerIP() != 0 ? ipstr(pSearchFile->GetClientServerIP()) : CString()),
		static_cast<UINT>(pSearchFile->GetClientServerPort()),
		pSearchFile->GetSpamRating(),
		pSearchFile->IsConsideredSpam() ? _T("true") : _T("false"),
		pSearchFile->IsKademlia() ? _T("true") : _T("false"),
		pSearchFile->IsServerUDPAnswer() ? _T("true") : _T("false"));
	return strJson;
}

void WriteEvent(
	LPCTSTR pszEvent,
	LPCTSTR pszSeverity,
	const CString &strPeerJson,
	const CString &strFileJson,
	LPCTSTR pszAction,
	LPCTSTR pszReason,
	LPCTSTR pszEvidenceJson)
{
	WriteDiagnosticsJsonEvent(
		g_badPeerDiagnosticsLog,
		g_badPeerDiagnosticsLogLock,
		g_llBadPeerDiagnosticsEventSeq,
		_T("bad_peer_event_v1"),
		BadPeerDiagnosticsSeams::kBinaryMarker,
		pszEvent,
		pszSeverity,
		_T("peer"),
		strPeerJson,
		_T("file"),
		strFileJson,
		pszAction,
		pszReason,
		pszEvidenceJson);
}

#if EMULEBB_HAS_DIAG_EVENT_V1
CString DiagV1KeysJson(const CUpDownClient *pClient, const CString &strFileHash)
{
	CString strKeys(_T("{"));
	bool bHasField = false;
	if (pClient != NULL) {
		const uint32 dwIP = pClient->GetIP() != 0 ? pClient->GetIP() : pClient->GetConnectIP();
		if (dwIP != 0) {
			CString strPeer;
			strPeer.Format(_T("%s:%u"), (LPCTSTR)ipstr(dwIP), static_cast<UINT>(pClient->GetUserPort()));
			strKeys.AppendFormat(_T("\"peer\":%s"), (LPCTSTR)JsonString(strPeer));
			bHasField = true;
		}
		if (pClient->HasValidHash()) {
			if (bHasField)
				strKeys += _T(",");
			strKeys.AppendFormat(_T("\"peerHash\":%s"), (LPCTSTR)JsonString(md4str(pClient->GetUserHash())));
			bHasField = true;
		}
	}
	if (!strFileHash.IsEmpty()) {
		if (bHasField)
			strKeys += _T(",");
		strKeys.AppendFormat(_T("\"fileHash\":%s"), (LPCTSTR)JsonString(strFileHash));
	}
	strKeys += _T("}");
	return strKeys;
}

void ReEmitBadPeerDiagV1(
	LPCTSTR pszEvent,
	LPCTSTR pszSeverity,
	LPCTSTR pszBehavior,
	LPCTSTR pszAction,
	LPCTSTR pszReason,
	const CUpDownClient *pClient,
	const CString &strFileHash,
	const SBadPeerBehaviorLedgerState &rState,
	const Requested_Block_Struct *pBlock)
{
	if (!theDiagEventV1Log.IsOpen())
		return;

	CString strBody;
	strBody.Format(
		_T("{\"behavior\":%s,\"action\":%s,\"reason\":%s,\"repeatCount\":%u,\"windowSeconds\":%I64u"),
		(LPCTSTR)JsonString(pszBehavior),
		(LPCTSTR)JsonString(pszAction != NULL ? pszAction : _T("observe")),
		(LPCTSTR)JsonString(pszReason != NULL ? pszReason : _T("")),
		rState.uCount,
		kBadPeerBehaviorLedgerWindowMs / 1000);
	if (pBlock != NULL) {
		strBody.AppendFormat(
			_T(",\"startOffset\":%I64u,\"endOffset\":%I64u,\"partIndex\":%I64u"),
			pBlock->StartOffset,
			pBlock->EndOffset,
			pBlock->StartOffset / PARTSIZE);
	}
	strBody += _T("}");

	WriteDiagEventV1(_T("bad_peer"), pszEvent,
		pszSeverity != NULL ? pszSeverity : _T("info"),
		DiagV1KeysJson(pClient, strFileHash), strBody);
}
#endif

SBadPeerBehaviorLedgerState UpdateBehaviorLedger(const CString &strLedgerKey, ULONGLONG curTick)
{
	SBadPeerBehaviorLedgerState state = {};
	if (strLedgerKey.IsEmpty())
		return state;

	CSingleLock lock(&g_badPeerBehaviorLedgerLock, TRUE);
	if (curTick >= g_ullBadPeerBehaviorLedgerLastCleanup + kBadPeerBehaviorLedgerCleanupMs) {
		g_ullBadPeerBehaviorLedgerLastCleanup = curTick;
		for (std::map<CString, SBadPeerBehaviorLedgerState, CStringLess>::iterator itLedger = g_badPeerBehaviorLedger.begin(); itLedger != g_badPeerBehaviorLedger.end();) {
			if (curTick >= itLedger->second.ullLastSeen + kBadPeerBehaviorLedgerWindowMs)
				itLedger = g_badPeerBehaviorLedger.erase(itLedger);
			else
				++itLedger;
		}
	}

	std::map<CString, SBadPeerBehaviorLedgerState, CStringLess>::iterator itState = g_badPeerBehaviorLedger.find(strLedgerKey);
	if (itState == g_badPeerBehaviorLedger.end() || curTick >= itState->second.ullLastSeen + kBadPeerBehaviorLedgerWindowMs) {
		state.ullFirstSeen = curTick;
		state.ullLastSeen = curTick;
		state.uCount = 1;
		g_badPeerBehaviorLedger[strLedgerKey] = state;
		return state;
	}

	itState->second.ullLastSeen = curTick;
	if (itState->second.uCount < _UI32_MAX)
		++itState->second.uCount;
	return itState->second;
}

CString BlockRequestEvidenceJson(
	const CUpDownClient *pClient,
	const CAbstractFile *pFile,
	const Requested_Block_Struct *pBlock,
	INT_PTR iQueuedBlocks,
	INT_PTR iDoneBlocks,
	LONG nPendingIOBlocks,
	const SBadPeerBehaviorLedgerState &rState,
	ULONGLONG curTick)
{
	const uint64 uStartOffset = pBlock != NULL ? pBlock->StartOffset : 0;
	const uint64 uEndOffset = pBlock != NULL ? pBlock->EndOffset : 0;
	const uint64 uRangeBytes = uEndOffset >= uStartOffset ? uEndOffset - uStartOffset : 0;
	CString strPartIndex;
	if (pBlock != NULL)
		strPartIndex.Format(_T("%I64u"), uStartOffset / PARTSIZE);
	else
		strPartIndex = _T("null");

	CString strJson;
	strJson.Format(
		_T("{\"file_hash\":%s,\"start_offset\":%I64u,\"end_offset\":%I64u,\"part_index\":%s,\"range_bytes\":%I64u,\"queued_blocks\":%Id,\"done_blocks\":%Id,\"pending_io_blocks\":%ld,\"repeat_count\":%u,\"window_seconds\":%I64u,\"first_seen_age_ms\":%I64u,\"session_up\":%I64u,\"queue_session_payload\":%I64u,\"payload_in_buffer\":%I64u}"),
		(LPCTSTR)JsonString(FileBehaviorHash(pFile, pBlock)),
		uStartOffset,
		uEndOffset,
		(LPCTSTR)strPartIndex,
		uRangeBytes,
		iQueuedBlocks,
		iDoneBlocks,
		nPendingIOBlocks,
		rState.uCount,
		kBadPeerBehaviorLedgerWindowMs / 1000,
		rState.ullFirstSeen != 0 && curTick >= rState.ullFirstSeen ? curTick - rState.ullFirstSeen : 0,
		pClient != NULL ? pClient->GetSessionUp() : 0,
		pClient != NULL ? pClient->GetQueueSessionPayloadUp() : 0,
		pClient != NULL ? pClient->GetPayloadInBuffer() : 0);
	return strJson;
}

CString UploadFileBehaviorEvidenceJson(
	LPCTSTR pszBehavior,
	const CUpDownClient *pClient,
	const CAbstractFile *pFile,
	const SBadPeerBehaviorLedgerState &rState,
	ULONGLONG curTick)
{
	CString strJson;
	strJson.Format(
		_T("{\"behavior\":%s,\"file_hash\":%s,\"repeat_count\":%u,\"window_seconds\":%I64u,\"first_seen_age_ms\":%I64u,\"session_up\":%I64u,\"queue_session_payload\":%I64u,\"payload_in_buffer\":%I64u}"),
		(LPCTSTR)JsonString(pszBehavior),
		(LPCTSTR)JsonString(FileBehaviorHash(pFile, NULL)),
		rState.uCount,
		kBadPeerBehaviorLedgerWindowMs / 1000,
		rState.ullFirstSeen != 0 && curTick >= rState.ullFirstSeen ? curTick - rState.ullFirstSeen : 0,
		pClient != NULL ? pClient->GetSessionUp() : 0,
		pClient != NULL ? pClient->GetQueueSessionPayloadUp() : 0,
		pClient != NULL ? pClient->GetPayloadInBuffer() : 0);
	return strJson;
}
}

namespace BadPeerDiagnosticsSeams
{
void InitializeLog(LPCTSTR pszLogPath, UINT uMaxLogFileSize)
{
	if (pszLogPath == NULL || pszLogPath[0] == _T('\0'))
		return;

	InitializeDiagnosticsLog(g_badPeerDiagnosticsLog, pszLogPath, uMaxLogFileSize);
}

bool IsEnabled()
{
	return g_badPeerDiagnosticsLog.IsOpen();
}

void LogClientEvent(
	LPCTSTR pszEvent,
	LPCTSTR pszSeverity,
	const CUpDownClient *pClient,
	LPCTSTR pszAction,
	LPCTSTR pszReason,
	const CAbstractFile *pFile,
	LPCTSTR pszEvidenceJson)
{
	WriteEvent(pszEvent, pszSeverity, ClientJson(pClient), FileJson(pFile), pszAction, pszReason, pszEvidenceJson);
}

void LogIpEvent(
	LPCTSTR pszEvent,
	LPCTSTR pszSeverity,
	uint32 dwIP,
	uint16 uPort,
	LPCTSTR pszAction,
	LPCTSTR pszReason,
	LPCTSTR pszEvidenceJson)
{
	CString strPeerJson;
	strPeerJson.Format(_T("{\"address\":%s,\"user_port\":%u}"), (LPCTSTR)JsonString(dwIP != 0 ? ipstr(dwIP) : CString()), static_cast<UINT>(uPort));
	WriteEvent(pszEvent, pszSeverity, strPeerJson, CString(_T("null")), pszAction, pszReason, pszEvidenceJson);
}

void LogSearchEvent(
	LPCTSTR pszEvent,
	LPCTSTR pszSeverity,
	const CSearchFile *pSearchFile,
	LPCTSTR pszAction,
	LPCTSTR pszReason,
	LPCTSTR pszEvidenceJson)
{
	WriteEvent(pszEvent, pszSeverity, CString(_T("null")), SearchJson(pSearchFile), pszAction, pszReason, pszEvidenceJson);
}

void LogUploadBlockRequestBehavior(
	LPCTSTR pszEvent,
	LPCTSTR pszSeverity,
	const CUpDownClient *pClient,
	const CAbstractFile *pFile,
	const Requested_Block_Struct *pBlock,
	LPCTSTR pszAction,
	LPCTSTR pszReason,
	INT_PTR iQueuedBlocks,
	INT_PTR iDoneBlocks,
	LONG nPendingIOBlocks)
{
	if (pBlock == NULL)
		return;

	const ULONGLONG curTick = ::GetTickCount64();
	const CString strPeerKey(PeerBehaviorKey(pClient));
	const CString strFileHash(FileBehaviorHash(pFile, pBlock));
	if (strPeerKey.IsEmpty() || strFileHash.IsEmpty())
		return;

	CString strLedgerKey;
	strLedgerKey.Format(
		_T("%s|block|%s|%I64u|%I64u"),
		(LPCTSTR)strPeerKey,
		(LPCTSTR)strFileHash,
		pBlock->StartOffset,
		pBlock->EndOffset);
	const SBadPeerBehaviorLedgerState state = UpdateBehaviorLedger(strLedgerKey, curTick);
	const CString strEvidence(BlockRequestEvidenceJson(pClient, pFile, pBlock, iQueuedBlocks, iDoneBlocks, nPendingIOBlocks, state, curTick));
	WriteEvent(pszEvent, pszSeverity, ClientJson(pClient), FileJson(pFile), pszAction, pszReason, strEvidence);

	if (state.uCount > 1) {
		WriteEvent(
			_T("upload_repeat_block_request_observed"),
			_T("medium"),
			ClientJson(pClient),
			FileJson(pFile),
			_T("observe"),
			_T("Repeated same upload block request"),
			strEvidence);
#if EMULEBB_HAS_DIAG_EVENT_V1
		ReEmitBadPeerDiagV1(
			_T("repeat_block_request"),
			_T("medium"),
			_T("repeat_block_request"),
			_T("observe"),
			_T("Repeated same upload block request"),
			pClient,
			strFileHash,
			state,
			pBlock);
#endif
	}
}

void TrackUploadFileBehavior(
	LPCTSTR pszBehavior,
	const CUpDownClient *pClient,
	const CAbstractFile *pFile,
	LPCTSTR pszReason)
{
	const CString strFileHash(FileBehaviorHash(pFile, NULL));
	const CString strPeerKey(PeerBehaviorKey(pClient));
	if (strFileHash.IsEmpty() || strPeerKey.IsEmpty())
		return;

	const ULONGLONG curTick = ::GetTickCount64();
	CString strLedgerKey;
	strLedgerKey.Format(_T("%s|file|%s"), (LPCTSTR)strPeerKey, (LPCTSTR)strFileHash);
	const SBadPeerBehaviorLedgerState state = UpdateBehaviorLedger(strLedgerKey, curTick);
	if (state.uCount <= 1)
		return;

	const CString strEvidence(UploadFileBehaviorEvidenceJson(pszBehavior, pClient, pFile, state, curTick));
	const LPCTSTR pszObserveReason = pszReason != NULL && pszReason[0] != _T('\0') ? pszReason : _T("Repeated same-file upload churn");
	WriteEvent(
		_T("upload_repeat_file_request_observed"),
		_T("medium"),
		ClientJson(pClient),
		FileJson(pFile),
		_T("observe"),
		pszObserveReason,
		strEvidence);
#if EMULEBB_HAS_DIAG_EVENT_V1
	ReEmitBadPeerDiagV1(
		_T("repeat_file_request"),
		_T("medium"),
		pszBehavior,
		_T("observe"),
		pszObserveReason,
		pClient,
		strFileHash,
		state,
		NULL);
#endif
}

CString EvidenceJsonString(LPCTSTR pszValue)
{
	return JsonString(pszValue);
}
}
#endif
