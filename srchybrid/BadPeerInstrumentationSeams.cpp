//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
#include "stdafx.h"
#include "BadPeerInstrumentationSeams.h"

#if EMULEBB_HAS_BAD_PEER_DIAGNOSTICS
#include "AbstractFile.h"
#include "Log.h"
#include "OtherFunctions.h"
#include "SearchFile.h"
#include "UpdownClient.h"

namespace
{
CLogFile g_badPeerInstrumentationLog;
CCriticalSection g_badPeerInstrumentationLogLock;
volatile LONGLONG g_llBadPeerInstrumentationEventSeq = 0;

CString JsonString(LPCTSTR pszValue)
{
	return BuildDiagnosticsJsonStringField(pszValue);
}

CString NormalizeEvidenceJson(LPCTSTR pszEvidenceJson)
{
	CString strEvidence(pszEvidenceJson != NULL ? pszEvidenceJson : _T(""));
	strEvidence.Trim();
	if (strEvidence.IsEmpty())
		return CString(_T("{}"));

	const TCHAR chFirst = strEvidence[0];
	const TCHAR chLast = strEvidence[strEvidence.GetLength() - 1];
	if ((chFirst == _T('{') && chLast == _T('}')) || (chFirst == _T('[') && chLast == _T(']')))
		return strEvidence;

	CString strJson;
	strJson.Format(_T("{\"text\":%s}"), (LPCTSTR)JsonString(strEvidence));
	return strJson;
}

CString JsonHashOrNull(const uchar *pHash)
{
	if (pHash == NULL || isnulmd4(pHash))
		return CString(_T("null"));
	return JsonString(md4str(pHash));
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
	if (!g_badPeerInstrumentationLog.IsOpen())
		return;

	const ULONGLONG ullEventSeq = NextDiagnosticsEventSeq(g_llBadPeerInstrumentationEventSeq);
	const CString strEvidence(NormalizeEvidenceJson(pszEvidenceJson));
	CString strJson;
	strJson.Format(
		_T("{\"schema\":\"bad_peer_event_v1\",\"source\":\"emulebb\",\"marker\":\"%s\",\"ts_utc\":%s,\"event_seq\":%I64u,\"event\":%s,\"severity\":%s,\"peer\":%s,\"file\":%s,\"action\":%s,\"reason\":%s,\"evidence\":%s}\r\n"),
		BadPeerInstrumentationSeams::kBinaryMarker,
		(LPCTSTR)JsonString(BuildDiagnosticsTimestampUtc()),
		ullEventSeq,
		(LPCTSTR)JsonString(pszEvent),
		(LPCTSTR)JsonString(pszSeverity),
		(LPCTSTR)strPeerJson,
		(LPCTSTR)strFileJson,
		(LPCTSTR)JsonString(pszAction),
		(LPCTSTR)JsonString(pszReason),
		(LPCTSTR)strEvidence);

	WriteDiagnosticsLogLine(g_badPeerInstrumentationLog, g_badPeerInstrumentationLogLock, strJson);
}
}

namespace BadPeerInstrumentationSeams
{
void InitializeLog(LPCTSTR pszLogPath, UINT uMaxLogFileSize)
{
	if (pszLogPath == NULL || pszLogPath[0] == _T('\0'))
		return;

	InitializeDiagnosticsLog(g_badPeerInstrumentationLog, pszLogPath, uMaxLogFileSize);
}

bool IsEnabled()
{
	return g_badPeerInstrumentationLog.IsOpen();
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

CString EvidenceJsonString(LPCTSTR pszValue)
{
	return JsonString(pszValue);
}
}
#endif
