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
#include <io.h>
#include <share.h>
#include "emule.h"
#include "Log.h"
#include "LogFileSeams.h"
#ifdef EMULEBB_ENABLE_PACKET_DIAGNOSTICS
#include "Opcodes.h"
#endif
#include "OtherFunctions.h"
#include "Preferences.h"
#include "emuledlg.h"
#include "StringConversion.h"

#include <deque>
#include <vector>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
constexpr int kMaxFileLogLineChars = 64 * 1024;
constexpr int kMaxRecentLogEntryChars = 4 * 1024;
constexpr ULONGLONG kDiagnosticsDiskFlushIntervalMs = 1000;
constexpr size_t kMaxRecentLogEntries = 200;
CCriticalSection g_recentLogLock;
std::deque<SRecentLogEntry> g_recentLogEntries;
#ifdef EMULEBB_ENABLE_PACKET_DIAGNOSTICS
constexpr UINT kMaxPacketDiagnosticsPayloadHexBytes = 4 * 1024;
CCriticalSection g_packetDiagnosticsLogLock;
volatile LONGLONG g_llPacketDiagnosticsEventSeq = 0;
#endif
#ifdef EMULEBB_ENABLE_UPLOAD_SLOT_DIAGNOSTICS
CCriticalSection g_uploadSlotDiagnosticsLogLock;
#endif
#ifdef EMULEBB_ENABLE_DOWNLOAD_SLOT_DIAGNOSTICS
CCriticalSection g_downloadSlotDiagnosticsLogLock;
#endif
#if EMULEBB_HAS_DIAG_EVENT_V1
CCriticalSection g_diagEventV1LogLock;
volatile LONGLONG g_llDiagEventV1Seq = 0;
#endif

CString TruncateLogLine(const CString &rstrLine, const int iMaxChars)
{
	if (iMaxChars <= 0 || rstrLine.GetLength() <= iMaxChars)
		return rstrLine;

	CString strLine(rstrLine);
	CString strLineEnding;
	if (strLine.GetLength() >= 2 && strLine.Right(2) == _T("\r\n")) {
		strLineEnding = _T("\r\n");
		strLine.Truncate(strLine.GetLength() - 2);
	} else if (!strLine.IsEmpty() && (strLine.Right(1) == _T("\r") || strLine.Right(1) == _T("\n"))) {
		strLineEnding = strLine.Right(1);
		strLine.Truncate(strLine.GetLength() - 1);
	}

	const int iMaxPayloadChars = max(0, iMaxChars - strLineEnding.GetLength());
	if (strLine.GetLength() > iMaxPayloadChars)
		strLine.Truncate(iMaxPayloadChars);
	return strLine + strLineEnding;
}

void AddRecentLogEntry(UINT uFlags, LPCTSTR pszText)
{
	const CString strText(TruncateLogLine(CString(pszText != NULL ? pszText : _T("")), kMaxRecentLogEntryChars));
	CSingleLock lock(&g_recentLogLock, TRUE);
	g_recentLogEntries.push_back(SRecentLogEntry{CTime::GetCurrentTime(), uFlags, strText});
	while (g_recentLogEntries.size() > kMaxRecentLogEntries)
		g_recentLogEntries.pop_front();
}

void FlushDiagnosticsLogToDiskIfDue(CLogFile &rLog, ULONGLONG &rullLastFlushTick, const ULONGLONG ullNowTick)
{
	if (rullLastFlushTick != 0 && ullNowTick < rullLastFlushTick + kDiagnosticsDiskFlushIntervalMs)
		return;

	// WHY: diagnostics are often inspected while the process is live. The CRT
	// flushes every line, but forcing the OS file handle periodically prevents
	// stale zero-length or delayed tails during high-volume diagnosis.
	rLog.FlushToDisk();
	rullLastFlushTick = ullNowTick;
}

#ifdef EMULEBB_ENABLE_PACKET_DIAGNOSTICS
CString BuildPacketDiagnosticsHexString(const BYTE *pPayload, UINT uPayloadLen)
{
	if (pPayload == NULL || uPayloadLen == 0)
		return CString();

	CString strHex;
	LPTSTR pszHex = strHex.GetBuffer(static_cast<int>(uPayloadLen * 2));
	static const TCHAR s_szDigits[] = _T("0123456789ABCDEF");
	for (UINT uIndex = 0; uIndex < uPayloadLen; ++uIndex) {
		const BYTE byValue = pPayload[uIndex];
		pszHex[uIndex * 2] = s_szDigits[(byValue >> 4) & 0x0F];
		pszHex[uIndex * 2 + 1] = s_szDigits[byValue & 0x0F];
	}
	strHex.ReleaseBuffer(static_cast<int>(uPayloadLen * 2));
	return strHex;
}

LPCTSTR PacketDiagnosticsProtocolName(uint8 byProtocol)
{
	switch (byProtocol) {
	case OP_EDONKEYPROT:
		return _T("ed2k");
	case OP_EMULEPROT:
		return _T("emule");
	case OP_PACKEDPROT:
		return _T("packed");
	default:
		return NULL;
	}
}

#endif
}

std::vector<SRecentLogEntry> GetRecentLogEntries(size_t maxEntries)
{
	CSingleLock lock(&g_recentLogLock, TRUE);
	if (maxEntries == 0 || maxEntries > g_recentLogEntries.size())
		maxEntries = g_recentLogEntries.size();

	std::vector<SRecentLogEntry> entries;
	entries.reserve(maxEntries);
	for (size_t i = g_recentLogEntries.size() - maxEntries; i < g_recentLogEntries.size(); ++i)
		entries.push_back(g_recentLogEntries[i]);
	return entries;
}

void ClearRecentLogEntries()
{
	CSingleLock lock(&g_recentLogLock, TRUE);
	g_recentLogEntries.clear();
}

CString BuildDiagnosticsTimestampUtc()
{
	SYSTEMTIME st = {};
	::GetSystemTime(&st);
	CString strTimestamp;
	strTimestamp.Format(_T("%04u-%02u-%02uT%02u:%02u:%02u.%03uZ"), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	return strTimestamp;
}

CString EscapeDiagnosticsJson(const CString &rstrValue)
{
	CString strEscaped;
	for (int i = 0; i < rstrValue.GetLength(); ++i) {
		const TCHAR ch = rstrValue[i];
		switch (ch) {
		case _T('\\'):
			strEscaped += _T("\\\\");
			break;
		case _T('"'):
			strEscaped += _T("\\\"");
			break;
		case _T('\r'):
			strEscaped += _T("\\r");
			break;
		case _T('\n'):
			strEscaped += _T("\\n");
			break;
		case _T('\t'):
			strEscaped += _T("\\t");
			break;
		default:
			if (static_cast<unsigned int>(ch) < 0x20u) {
				CString strCodePoint;
				strCodePoint.Format(_T("\\u%04X"), static_cast<unsigned int>(ch));
				strEscaped += strCodePoint;
			} else {
				strEscaped.AppendChar(ch);
			}
		}
	}
	return strEscaped;
}

CString BuildDiagnosticsJsonStringField(LPCTSTR pszValue)
{
	CString strField(_T("\""));
	strField += EscapeDiagnosticsJson(pszValue != NULL ? CString(pszValue) : CString());
	strField += _T("\"");
	return strField;
}

CString NormalizeDiagnosticsJsonPayload(LPCTSTR pszJsonOrText)
{
	CString strPayload(pszJsonOrText != NULL ? pszJsonOrText : _T(""));
	strPayload.Trim();
	if (strPayload.IsEmpty())
		return CString(_T("{}"));

	const TCHAR chFirst = strPayload[0];
	const TCHAR chLast = strPayload[strPayload.GetLength() - 1];
	if ((chFirst == _T('{') && chLast == _T('}')) || (chFirst == _T('[') && chLast == _T(']')))
		return strPayload;

	CString strJson;
	strJson.Format(_T("{\"text\":%s}"), (LPCTSTR)BuildDiagnosticsJsonStringField(strPayload));
	return strJson;
}

ULONGLONG NextDiagnosticsEventSeq(volatile LONGLONG &rllCounter)
{
	return static_cast<ULONGLONG>(::InterlockedIncrement64(&rllCounter));
}

void WriteDiagnosticsJsonEvent(
	CLogFile &rLog,
	CCriticalSection &rLock,
	volatile LONGLONG &rllCounter,
	LPCTSTR pszSchema,
	LPCTSTR pszMarker,
	LPCTSTR pszEvent,
	LPCTSTR pszSeverity,
	LPCTSTR pszPrimaryObjectKey,
	const CString &rstrPrimaryObjectJson,
	LPCTSTR pszSecondaryObjectKey,
	const CString &rstrSecondaryObjectJson,
	LPCTSTR pszAction,
	LPCTSTR pszReason,
	LPCTSTR pszEvidenceJson)
{
	if (!rLog.IsOpen())
		return;

	CString strJson;
	strJson.Format(
		_T("{\"schema\":%s,\"source\":\"emulebb\",\"marker\":%s,\"ts_utc\":%s,\"event_seq\":%I64u,\"event\":%s,\"severity\":%s"),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszSchema),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszMarker),
		(LPCTSTR)BuildDiagnosticsJsonStringField(BuildDiagnosticsTimestampUtc()),
		NextDiagnosticsEventSeq(rllCounter),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszEvent),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszSeverity));

	if (pszPrimaryObjectKey != NULL && pszPrimaryObjectKey[0] != _T('\0')) {
		strJson += _T(",\"");
		strJson += EscapeDiagnosticsJson(pszPrimaryObjectKey);
		strJson += _T("\":");
		strJson += rstrPrimaryObjectJson.IsEmpty() ? CString(_T("null")) : rstrPrimaryObjectJson;
	}
	if (pszSecondaryObjectKey != NULL && pszSecondaryObjectKey[0] != _T('\0')) {
		strJson += _T(",\"");
		strJson += EscapeDiagnosticsJson(pszSecondaryObjectKey);
		strJson += _T("\":");
		strJson += rstrSecondaryObjectJson.IsEmpty() ? CString(_T("null")) : rstrSecondaryObjectJson;
	}

	strJson += _T(",\"action\":");
	strJson += BuildDiagnosticsJsonStringField(pszAction);
	strJson += _T(",\"reason\":");
	strJson += BuildDiagnosticsJsonStringField(pszReason);
	strJson += _T(",\"evidence\":");
	strJson += NormalizeDiagnosticsJsonPayload(pszEvidenceJson);
	strJson += _T("}\r\n");

	WriteDiagnosticsLogLine(rLog, rLock, strJson);
}

#if EMULEBB_HAS_DIAG_EVENT_V1
void WriteDiagEventV1(
	LPCTSTR pszFamily,
	LPCTSTR pszEvent,
	LPCTSTR pszSeverity,
	const CString &rstrKeysJson,
	const CString &rstrBodyJson)
{
	if (!theDiagEventV1Log.IsOpen())
		return;

	// WHY: an empty keys/body must still be valid JSON for the diff harness loader,
	// which assumes every record carries object-typed keys and body fields.
	const CString strKeys(rstrKeysJson.IsEmpty() ? CString(_T("{}")) : rstrKeysJson);
	const CString strBody(rstrBodyJson.IsEmpty() ? CString(_T("{}")) : rstrBodyJson);

	CString strJson;
	strJson.Format(
		_T("{\"schema\":\"diag_event_v1\",\"client\":\"mfc\",\"ts\":%s,\"seq\":%I64u,\"family\":%s,\"event\":%s,\"severity\":%s,\"keys\":%s,\"body\":%s}\r\n"),
		(LPCTSTR)BuildDiagnosticsJsonStringField(BuildDiagnosticsTimestampUtc()),
		NextDiagnosticsEventSeq(g_llDiagEventV1Seq),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszFamily),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszEvent),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszSeverity),
		(LPCTSTR)strKeys,
		(LPCTSTR)strBody);

	WriteDiagnosticsLogLine(theDiagEventV1Log, g_diagEventV1LogLock, strJson);
}

CString BuildDiagEventLowerHexString(const BYTE *pPayload, UINT uPayloadLen)
{
	if (pPayload == NULL || uPayloadLen == 0)
		return CString();

	CString strHex;
	LPTSTR pszHex = strHex.GetBuffer(static_cast<int>(uPayloadLen * 2));
	static const TCHAR s_szDigits[] = _T("0123456789abcdef");
	for (UINT uIndex = 0; uIndex < uPayloadLen; ++uIndex) {
		const BYTE byValue = pPayload[uIndex];
		pszHex[uIndex * 2] = s_szDigits[(byValue >> 4) & 0x0F];
		pszHex[uIndex * 2 + 1] = s_szDigits[byValue & 0x0F];
	}
	strHex.ReleaseBuffer(static_cast<int>(uPayloadLen * 2));
	return strHex;
}
#endif

#if EMULEBB_HAS_DIAG_EVENT_V1 && EMULEBB_HAS_KAD_DIAGNOSTICS
namespace
{
// Upper-case hex, capped at 4 KiB. WHY: the kad_udp wire-identity compare uses
// UPPER hex on both clients (rust udp_packet_v1 emits UPPER), so case must match.
CString BuildDiagEventUpperHexString(const BYTE *pPayload, UINT uPayloadLen)
{
	if (pPayload == NULL || uPayloadLen == 0)
		return CString();

	const UINT uCapped = min(uPayloadLen, 4u * 1024u);
	CString strHex;
	LPTSTR pszHex = strHex.GetBuffer(static_cast<int>(uCapped * 2));
	static const TCHAR s_szDigits[] = _T("0123456789ABCDEF");
	for (UINT uIndex = 0; uIndex < uCapped; ++uIndex) {
		const BYTE byValue = pPayload[uIndex];
		pszHex[uIndex * 2] = s_szDigits[(byValue >> 4) & 0x0F];
		pszHex[uIndex * 2 + 1] = s_szDigits[byValue & 0x0F];
	}
	strHex.ReleaseBuffer(static_cast<int>(uCapped * 2));
	return strHex;
}
}

void DiagEventLogKadUdpPacket(
	LPCTSTR pszDirection,
	uint32 uHostIP,
	uint16 uUDPPort,
	const BYTE *pDecoded,
	UINT uDecodedLen)
{
	if (!theDiagEventV1Log.IsOpen() || pDecoded == NULL || uDecodedLen < 2)
		return;

	const BYTE byProtocol = pDecoded[0];
	const BYTE byOpcode = pDecoded[1];
	const CString strDecodedHex = BuildDiagEventUpperHexString(pDecoded, uDecodedLen);

	CString strPeer;
	strPeer.Format(_T("%s:%u"), (LPCTSTR)ipstr(htonl(uHostIP)), static_cast<UINT>(uUDPPort));

	CString strKeys;
	strKeys.Format(
		_T("{\"peer\":%s,\"opcode\":%u,\"protocolMarker\":%u}"),
		(LPCTSTR)BuildDiagnosticsJsonStringField(strPeer),
		static_cast<UINT>(byOpcode),
		static_cast<UINT>(byProtocol));

	CString strBody;
	strBody.Format(
		_T("{\"direction\":%s,\"protocolMarker\":%u,\"opcode\":%u,\"decodedLen\":%u,\"decodedHex\":\"%s\"}"),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszDirection != NULL ? pszDirection : _T("recv")),
		static_cast<UINT>(byProtocol),
		static_cast<UINT>(byOpcode),
		uDecodedLen,
		(LPCTSTR)strDecodedHex);

	WriteDiagEventV1(_T("kad_udp"), _T("packet"), _T("info"), strKeys, strBody);
}

void DiagEventLogKadUdpPacketSend(
	uint32 uHostIP,
	uint16 uUDPPort,
	BYTE byProtocol,
	BYTE byOpcode,
	const BYTE *pPayload,
	UINT uPayloadLen)
{
	if (!theDiagEventV1Log.IsOpen())
		return;

	// Rebuild the decoded [marker][opcode][payload] buffer the recv emit consumes so
	// both directions share the same decodedHex/decodedLen identity. The hex is
	// capped at 4 KiB inside BuildDiagEventUpperHexString, so only cap+2 bytes need
	// to be materialised even though decodedLen reports the full decoded length.
	const UINT uTrueDecodedLen = 2 + uPayloadLen;
	const UINT uHexBytes = min(uTrueDecodedLen, 4u * 1024u);
	std::vector<BYTE> aDecoded(uHexBytes);
	aDecoded[0] = byProtocol;
	if (uHexBytes > 1)
		aDecoded[1] = byOpcode;
	if (uHexBytes > 2 && pPayload != NULL)
		memcpy(aDecoded.data() + 2, pPayload, uHexBytes - 2);

	const BYTE byProtocolMarker = aDecoded[0];
	const BYTE byOpcodeMarker = uHexBytes > 1 ? aDecoded[1] : static_cast<BYTE>(0);
	const CString strDecodedHex = BuildDiagEventUpperHexString(aDecoded.data(), uHexBytes);

	CString strPeer;
	strPeer.Format(_T("%s:%u"), (LPCTSTR)ipstr(htonl(uHostIP)), static_cast<UINT>(uUDPPort));

	CString strKeys;
	strKeys.Format(
		_T("{\"peer\":%s,\"opcode\":%u,\"protocolMarker\":%u}"),
		(LPCTSTR)BuildDiagnosticsJsonStringField(strPeer),
		static_cast<UINT>(byOpcodeMarker),
		static_cast<UINT>(byProtocolMarker));

	CString strBody;
	strBody.Format(
		_T("{\"direction\":%s,\"protocolMarker\":%u,\"opcode\":%u,\"decodedLen\":%u,\"decodedHex\":\"%s\"}"),
		(LPCTSTR)BuildDiagnosticsJsonStringField(_T("send")),
		static_cast<UINT>(byProtocolMarker),
		static_cast<UINT>(byOpcodeMarker),
		uTrueDecodedLen,
		(LPCTSTR)strDecodedHex);

	WriteDiagEventV1(_T("kad_udp"), _T("packet"), _T("info"), strKeys, strBody);
}
#endif

CDiagnosticsKeyValueLineBuilder::CDiagnosticsKeyValueLineBuilder(LPCTSTR pszPrefix)
	: m_strLine(pszPrefix != NULL ? pszPrefix : _T(""))
{
}

void CDiagnosticsKeyValueLineBuilder::AppendFormat(LPCTSTR pszKeyValueFmt, ...)
{
	if (pszKeyValueFmt == NULL || pszKeyValueFmt[0] == _T('\0'))
		return;

	CString strKeyValue;
	va_list argp;
	va_start(argp, pszKeyValueFmt);
	strKeyValue.FormatV(pszKeyValueFmt, argp);
	va_end(argp);

	if (!m_strLine.IsEmpty())
		m_strLine += _T(" ");
	m_strLine += strKeyValue;
}

bool InitializeDiagnosticsLog(CLogFile &rLog, LPCTSTR pszLogPath, UINT uMaxLogFileSize)
{
	if (pszLogPath == NULL || pszLogPath[0] == _T('\0'))
		return false;

	VERIFY(rLog.SetFilePath(pszLogPath));
	rLog.SetMaxFileSize(uMaxLogFileSize);
	rLog.SetFileFormat(Utf8);
	VERIFY(rLog.SetFlushOnWrite(true));
	if (!rLog.Open())
		return false;
	rLog.Log(_T("\r\n"));
	return true;
}

void WriteDiagnosticsLogLine(CLogFile &rLog, CCriticalSection &rLock, const CString &rstrLine)
{
	if (!rLog.IsOpen())
		return;
	static ULONGLONG s_ullLastDiagnosticsDiskFlushTick = 0;

	CString strLine(rstrLine);
	if (strLine.Right(2) != _T("\r\n"))
		strLine += _T("\r\n");

	CSingleLock lock(&rLock, TRUE);
	rLog.Log(strLine);
	FlushDiagnosticsLogToDiskIfDue(rLog, s_ullLastDiagnosticsDiskFlushTick, ::GetTickCount64());
}

void WriteDiagnosticsLogLineV(CLogFile &rLog, CCriticalSection &rLock, LPCTSTR pszFmt, va_list argp)
{
	CString strLine;
	strLine.FormatV(pszFmt, argp);
	WriteDiagnosticsLogLine(rLog, rLock, strLine);
}

void WriteDiagnosticsLogLineF(CLogFile &rLog, CCriticalSection &rLock, LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	WriteDiagnosticsLogLineV(rLog, rLock, pszFmt, argp);
	va_end(argp);
}

#ifdef EMULEBB_ENABLE_UPLOAD_SLOT_DIAGNOSTICS
void UploadSlotDiagnosticsLogLine(LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	WriteDiagnosticsLogLineV(theUploadSlotDiagnosticsLog, g_uploadSlotDiagnosticsLogLock, pszFmt, argp);
	va_end(argp);
}
#endif

#ifdef EMULEBB_ENABLE_DOWNLOAD_SLOT_DIAGNOSTICS
void DownloadSlotDiagnosticsLogLine(LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	WriteDiagnosticsLogLineV(theDownloadSlotDiagnosticsLog, g_downloadSlotDiagnosticsLogLock, pszFmt, argp);
	va_end(argp);
}
#endif

#if EMULEBB_HAS_DIAG_EVENT_V1 && defined(EMULEBB_ENABLE_UPLOAD_SLOT_DIAGNOSTICS)
void DiagEventLogUploadCapacitySnapshot(
	UINT uBaseSlots,
	UINT uElasticSlots,
	UINT uEffectiveSlotCap,
	UINT uActiveSlots,
	UINT uWaitingSessions)
{
	if (!theDiagEventV1Log.IsOpen())
		return;

	CString strBody;
	strBody.Format(
		_T("{\"baseSlots\":%u,\"elasticSlots\":%u,\"effectiveSlotCap\":%u,\"activeSlots\":%u,\"waitingSessions\":%u}"),
		uBaseSlots,
		uElasticSlots,
		uEffectiveSlotCap,
		uActiveSlots,
		uWaitingSessions);
	WriteDiagEventV1(_T("sched"), _T("capacity_snapshot"), _T("info"), CString(_T("{}")), strBody);
}
#endif

#if EMULEBB_HAS_DIAG_EVENT_V1 && defined(EMULEBB_ENABLE_DOWNLOAD_SLOT_DIAGNOSTICS)
void DiagEventLogDownloadSourceCount(
	UINT uSourceCount,
	UINT uValidSourceCount,
	UINT uNnpSourceCount,
	UINT uA4afFileCount,
	UINT uTransferringSourceCount)
{
	if (!theDiagEventV1Log.IsOpen())
		return;

	// transferringSourceCount = DS_DOWNLOADING sources (GetTransferringSrcCount),
	// the parity of rust active_download_peer_endpoints. Emitted so the converged
	// sched:source_count diff exposes leased-but-not-transferring (the download
	// stall signature) on both clients.
	CString strBody;
	strBody.Format(
		_T("{\"sourceCount\":%u,\"validSourceCount\":%u,\"nnpSourceCount\":%u,\"a4afFileCount\":%u,\"transferringSourceCount\":%u}"),
		uSourceCount,
		uValidSourceCount,
		uNnpSourceCount,
		uA4afFileCount,
		uTransferringSourceCount);
	WriteDiagEventV1(_T("sched"), _T("source_count"), _T("info"), CString(_T("{}")), strBody);
}

void DiagEventLogUdpReaskSent(UINT uReaskCount)
{
	if (!theDiagEventV1Log.IsOpen() || uReaskCount == 0)
		return;

	// WHY: the master computes UDP file reasks as a periodic aggregate count, not
	// per-decision. Emit one reask_sent{transport:"udp"} carrying the count so the
	// harness sees the same transport+outcome shape rust emits per send.
	CString strBody;
	strBody.Format(
		_T("{\"outcome\":\"sent\",\"transport\":\"udp\",\"reaskCount\":%u}"),
		uReaskCount);
	WriteDiagEventV1(_T("sched"), _T("reask_sent"), _T("info"), CString(_T("{}")), strBody);
}
#endif

#ifdef EMULEBB_ENABLE_PACKET_DIAGNOSTICS
void PacketDiagnosticsLogInvalidSubOpcode(
	LPCTSTR pszPacketFamily,
	LPCTSTR pszPeerLabel,
	LPCTSTR pszTransportMode,
	uint8 byProtocol,
	uint8 byOuterOpcode,
	uint8 byInvalidSubOpcode,
	const BYTE *pPayload,
	UINT uPayloadLen,
	ULONGLONG ullInvalidOffset,
	ULONGLONG ullBytesRemaining,
	int iPreviousSubOpcode)
{
	if (!thePacketDiagnosticsLog.IsOpen())
		return;

	const UINT uContextStart = (ullInvalidOffset > 32) ? static_cast<UINT>(ullInvalidOffset - 32) : 0;
	UINT uContextLen = 0;
	if (pPayload != NULL && ullInvalidOffset < uPayloadLen) {
		const ULONGLONG ullContextEnd = min(static_cast<ULONGLONG>(uPayloadLen), ullInvalidOffset + 33);
		uContextLen = static_cast<UINT>(ullContextEnd - uContextStart);
	}

	CString strPreviousSubOpcode(_T("null"));
	if (iPreviousSubOpcode >= 0)
		strPreviousSubOpcode.Format(_T("%u"), static_cast<UINT>(iPreviousSubOpcode));

	const LPCTSTR pszProtocolName = PacketDiagnosticsProtocolName(byProtocol);
	const CString strProtocol = pszProtocolName != NULL ? BuildDiagnosticsJsonStringField(pszProtocolName) : CString(_T("null"));
	const CString strOuterOpcodeName = DbgGetClientTCPOpcode(byProtocol, byOuterOpcode);
	const UINT uPayloadHexLen = (pPayload != NULL) ? min(uPayloadLen, kMaxPacketDiagnosticsPayloadHexBytes) : 0;
	const bool bPayloadHexTruncated = pPayload != NULL && uPayloadLen > uPayloadHexLen;
	const CString strPayloadHex = BuildPacketDiagnosticsHexString(pPayload, uPayloadHexLen);
	const CString strContextHex = BuildPacketDiagnosticsHexString(pPayload != NULL ? pPayload + uContextStart : NULL, uContextLen);
	const ULONGLONG ullEventSeq = NextDiagnosticsEventSeq(g_llPacketDiagnosticsEventSeq);

	CString strJson;
	strJson.Format(
		_T("{\"schema\":\"ed2k_invalid_sub_opcode_v1\",\"source\":\"emulebb\",\"ts_utc\":\"%s\",\"event_seq\":%I64u,\"packet_family\":\"%s\",\"remote_addr\":\"%s\",\"transport_mode\":\"%s\",\"protocol\":%s,\"protocol_marker\":%u,\"outer_opcode\":%u,\"outer_opcode_name\":\"%s\",\"invalid_sub_opcode\":%u,\"previous_sub_opcode\":%s,\"payload_len\":%u,\"invalid_offset\":%I64u,\"bytes_remaining\":%I64u,\"context_offset\":%u,\"context_len\":%u,\"context_hex\":\"%s\",\"payload_hex_truncated\":%s,\"payload_hex\":\"%s\"}\r\n"),
		(LPCTSTR)EscapeDiagnosticsJson(BuildDiagnosticsTimestampUtc()),
		ullEventSeq,
		(LPCTSTR)EscapeDiagnosticsJson(pszPacketFamily != NULL ? CString(pszPacketFamily) : CString(_T("unknown"))),
		(LPCTSTR)EscapeDiagnosticsJson(pszPeerLabel != NULL ? CString(pszPeerLabel) : CString(_T("unknown"))),
		(LPCTSTR)EscapeDiagnosticsJson(pszTransportMode != NULL ? CString(pszTransportMode) : CString(_T("unknown"))),
		(LPCTSTR)strProtocol,
		static_cast<UINT>(byProtocol),
		static_cast<UINT>(byOuterOpcode),
		(LPCTSTR)EscapeDiagnosticsJson(strOuterOpcodeName),
		static_cast<UINT>(byInvalidSubOpcode),
		(LPCTSTR)strPreviousSubOpcode,
		uPayloadLen,
		ullInvalidOffset,
		ullBytesRemaining,
		uContextStart,
		uContextLen,
		(LPCTSTR)strContextHex,
		bPayloadHexTruncated ? _T("true") : _T("false"),
		(LPCTSTR)strPayloadHex);

	WriteDiagnosticsLogLine(thePacketDiagnosticsLog, g_packetDiagnosticsLogLock, strJson);
}

// Shared emitter for one ED2K TCP packet in the converged ed2k_packet_v1 schema.
// `pszFlow` discriminates the connection family (e.g. "server", "client") so the
// emulebb-rust and eMuleBB dumps share one shape and diff 1:1.
static void PacketDiagnosticsLogEd2kPacket(
	LPCTSTR pszFlow,
	LPCTSTR pszPeerLabel,
	LPCTSTR pszTransportMode,
	LPCTSTR pszDirection,
	uint8 byProtocol,
	uint8 byOpcode,
	const BYTE *pPayload,
	UINT uPayloadLen)
{
	if (!thePacketDiagnosticsLog.IsOpen())
		return;

	const UINT uPayloadHexLen = (pPayload != NULL) ? min(uPayloadLen, kMaxPacketDiagnosticsPayloadHexBytes) : 0;
	const bool bPayloadHexTruncated = pPayload != NULL && uPayloadLen > uPayloadHexLen;
	const CString strPayloadHex = BuildPacketDiagnosticsHexString(pPayload, uPayloadHexLen);
	const LPCTSTR pszProtocolName = PacketDiagnosticsProtocolName(byProtocol);
	const CString strProtocol = pszProtocolName != NULL ? BuildDiagnosticsJsonStringField(pszProtocolName) : CString(_T("null"));
	const CString strOpcodeName = DbgGetClientTCPOpcode(byProtocol, byOpcode);
	const CString strFlow(pszFlow != NULL ? pszFlow : _T("unknown"));
	const CString strPeer(pszPeerLabel != NULL ? pszPeerLabel : _T("unknown"));
	const CString strTraceKey(strFlow + _T(":") + strPeer);
	const ULONGLONG ullEventSeq = NextDiagnosticsEventSeq(g_llPacketDiagnosticsEventSeq);

	CString strJson;
	strJson.Format(
		_T("{\"schema\":\"ed2k_packet_v1\",\"source\":\"emulebb\",\"ts_utc\":\"%s\",\"event_seq\":%I64u,\"flow\":\"%s\",\"trace_key\":\"%s\",\"direction\":\"%s\",\"remote_addr\":\"%s\",\"transport_mode\":\"%s\",\"protocol\":%s,\"protocol_marker\":%u,\"opcode\":%u,\"opcode_name\":\"%s\",\"payload_len\":%u,\"payload_hex_truncated\":%s,\"payload_hex\":\"%s\"}\r\n"),
		(LPCTSTR)EscapeDiagnosticsJson(BuildDiagnosticsTimestampUtc()),
		ullEventSeq,
		(LPCTSTR)EscapeDiagnosticsJson(strFlow),
		(LPCTSTR)EscapeDiagnosticsJson(strTraceKey),
		(LPCTSTR)EscapeDiagnosticsJson(pszDirection != NULL ? CString(pszDirection) : CString(_T("unknown"))),
		(LPCTSTR)EscapeDiagnosticsJson(strPeer),
		(LPCTSTR)EscapeDiagnosticsJson(pszTransportMode != NULL ? CString(pszTransportMode) : CString(_T("unknown"))),
		(LPCTSTR)strProtocol,
		static_cast<UINT>(byProtocol),
		static_cast<UINT>(byOpcode),
		(LPCTSTR)EscapeDiagnosticsJson(strOpcodeName),
		uPayloadLen,
		bPayloadHexTruncated ? _T("true") : _T("false"),
		(LPCTSTR)strPayloadHex);

	WriteDiagnosticsLogLine(thePacketDiagnosticsLog, g_packetDiagnosticsLogLock, strJson);
}

void PacketDiagnosticsLogServerPacket(
	LPCTSTR pszPeerLabel,
	LPCTSTR pszTransportMode,
	LPCTSTR pszDirection,
	uint8 byProtocol,
	uint8 byOpcode,
	const BYTE *pPayload,
	UINT uPayloadLen)
{
	PacketDiagnosticsLogEd2kPacket(_T("server"), pszPeerLabel, pszTransportMode, pszDirection,
		byProtocol, byOpcode, pPayload, uPayloadLen);
}

// B3 parity with the rust `should_skip_bulk_packet` sampler: the high-volume
// payload opcodes (SENDINGPART / COMPRESSEDPART and their I64 variants) are logged
// only for the first kBulkHead per (opcode, direction) then 1-in-kBulkEvery.
// Without this MFC logs EVERY payload packet, which drives the ~33 MB diagnostics
// rotations and inflates per-opcode counts far above rust's sampled dump.
static bool ShouldSkipBulkPacketDiagnostics(uint8 byProtocol, uint8 byOpcode, bool bFromRecv)
{
	static const ULONGLONG kBulkHead = 64;
	static const ULONGLONG kBulkEvery = 8192;
	static volatile LONG64 s_counters[4][2] = { { 0 } };
	int nSlot;
	if (byProtocol == OP_EDONKEYPROT && byOpcode == OP_SENDINGPART)
		nSlot = 0;
	else if (byProtocol == OP_EMULEPROT && byOpcode == OP_COMPRESSEDPART)
		nSlot = 1;
	else if (byProtocol == OP_EMULEPROT && byOpcode == OP_SENDINGPART_I64)
		nSlot = 2;
	else if (byProtocol == OP_EMULEPROT && byOpcode == OP_COMPRESSEDPART_I64)
		nSlot = 3;
	else
		return false;
	const ULONGLONG n = static_cast<ULONGLONG>(
		::InterlockedIncrement64(&s_counters[nSlot][bFromRecv ? 1 : 0])) - 1;
	return !(n < kBulkHead || (n % kBulkEvery) == 0);
}

void PacketDiagnosticsLogClientPacket(
	LPCTSTR pszPeerLabel,
	LPCTSTR pszTransportMode,
	LPCTSTR pszDirection,
	uint8 byProtocol,
	uint8 byOpcode,
	const BYTE *pPayload,
	UINT uPayloadLen)
{
	if (ShouldSkipBulkPacketDiagnostics(byProtocol, byOpcode, _tcsicmp(pszDirection, _T("recv")) == 0))
		return;
	PacketDiagnosticsLogEd2kPacket(_T("client"), pszPeerLabel, pszTransportMode, pszDirection,
		byProtocol, byOpcode, pPayload, uPayloadLen);
	DiagEventLogEd2kTcpPacket(_T("client"), pszPeerLabel, pszTransportMode, pszDirection,
		byProtocol, byOpcode, pPayload, uPayloadLen);
}

void DiagEventLogEd2kTcpPacket(
	LPCTSTR pszFlow,
	LPCTSTR pszPeerLabel,
	LPCTSTR pszTransportMode,
	LPCTSTR pszDirection,
	uint8 byProtocol,
	uint8 byOpcode,
	const BYTE *pPayload,
	UINT uPayloadLen)
{
	if (!theDiagEventV1Log.IsOpen())
		return;

	const UINT uPayloadHexLen = (pPayload != NULL) ? min(uPayloadLen, kMaxPacketDiagnosticsPayloadHexBytes) : 0;
	const bool bPayloadHexTruncated = pPayload != NULL && uPayloadLen > uPayloadHexLen;
	const CString strPayloadHex = BuildDiagEventLowerHexString(pPayload, uPayloadHexLen);

	// WHY: the eD2k frame is [protocol][len32 LE][opcode][payload]; rust emits the
	// full framed rawHex/rawLen. Reconstruct the header from the parts the packet
	// hook receives so both clients' rawHex compare keys line up byte-for-byte.
	const UINT uRawLen = 6 + uPayloadLen;
	BYTE aHeader[6];
	const uint32 uFrameLen = uPayloadLen + 1; // opcode byte + payload
	aHeader[0] = byProtocol;
	aHeader[1] = static_cast<BYTE>(uFrameLen & 0xFF);
	aHeader[2] = static_cast<BYTE>((uFrameLen >> 8) & 0xFF);
	aHeader[3] = static_cast<BYTE>((uFrameLen >> 16) & 0xFF);
	aHeader[4] = static_cast<BYTE>((uFrameLen >> 24) & 0xFF);
	aHeader[5] = byOpcode;
	CString strRawHex(BuildDiagEventLowerHexString(aHeader, 6));
	const UINT uRawHexPayloadLen = (pPayload != NULL) ? min(uPayloadLen, kMaxPacketDiagnosticsPayloadHexBytes) : 0;
	strRawHex += BuildDiagEventLowerHexString(pPayload, uRawHexPayloadLen);
	const bool bRawHexTruncated = pPayload != NULL && uPayloadLen > uRawHexPayloadLen;

	const LPCTSTR pszProtocolName = PacketDiagnosticsProtocolName(byProtocol);
	const CString strProtocolNameField = pszProtocolName != NULL ? BuildDiagnosticsJsonStringField(pszProtocolName) : CString(_T("null"));
	const CString strOpcodeName = DbgGetClientTCPOpcode(byProtocol, byOpcode);
	const CString strPeer(pszPeerLabel != NULL ? pszPeerLabel : _T("unknown"));

	CString strKeys;
	strKeys.Format(
		_T("{\"peer\":%s,\"opcode\":%u,\"protocolMarker\":%u}"),
		(LPCTSTR)BuildDiagnosticsJsonStringField(strPeer),
		static_cast<UINT>(byOpcode),
		static_cast<UINT>(byProtocol));

	CString strBody;
	strBody.Format(
		_T("{\"direction\":%s,\"protocolMarker\":%u,\"protocolName\":%s,\"opcode\":%u,\"opcodeName\":%s,\"rawLen\":%u,\"rawHex\":\"%s\",\"payloadLen\":%u,\"payloadHex\":\"%s\",\"payloadHexTruncated\":%s,\"obfuscated\":%s,\"transportMode\":%s,\"flow\":%s}"),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszDirection != NULL ? pszDirection : _T("unknown")),
		static_cast<UINT>(byProtocol),
		(LPCTSTR)strProtocolNameField,
		static_cast<UINT>(byOpcode),
		(LPCTSTR)BuildDiagnosticsJsonStringField(strOpcodeName),
		uRawLen,
		(LPCTSTR)strRawHex,
		uPayloadLen,
		(LPCTSTR)strPayloadHex,
		(bPayloadHexTruncated || bRawHexTruncated) ? _T("true") : _T("false"),
		// WHY: the master packet hook sits on the decoded packet buffer, after any
		// eD2k obfuscation layer has been stripped; the on-wire bytes are never
		// obfuscated at this boundary, matching rust's plaintext listener dump.
		_T("false"),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszTransportMode != NULL ? pszTransportMode : _T("unknown")),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszFlow != NULL ? pszFlow : _T("unknown")));

	WriteDiagEventV1(_T("ed2k_tcp"), _T("packet"), _T("info"), strKeys, strBody);
}
#endif


void LogV(UINT uFlags, LPCTSTR pszFmt, va_list argp)
{
	AddLogTextV(uFlags, DLP_DEFAULT, pszFmt, argp);
}

///////////////////////////////////////////////////////////////////////////////
//

void Log(LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	LogV(LOG_DEFAULT, pszFmt, argp);
	va_end(argp);
}

void LogError(LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	LogV(LOG_ERROR, pszFmt, argp);
	va_end(argp);
}

void LogWarning(LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	LogV(LOG_WARNING, pszFmt, argp);
	va_end(argp);
}

///////////////////////////////////////////////////////////////////////////////
//

void Log(UINT uFlags, LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	LogV(uFlags, pszFmt, argp);
	va_end(argp);
}

void LogError(UINT uFlags, LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	LogV(uFlags | LOG_ERROR, pszFmt, argp);
	va_end(argp);
}

void LogWarning(UINT uFlags, LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	LogV(uFlags | LOG_WARNING, pszFmt, argp);
	va_end(argp);
}

///////////////////////////////////////////////////////////////////////////////
//

void DebugLog(LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	LogV(LOG_DEBUG, pszFmt, argp);
	va_end(argp);
}

void DebugLogError(LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	LogV(LOG_DEBUG | LOG_ERROR, pszFmt, argp);
	va_end(argp);
}

void DebugLogWarning(LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	LogV(LOG_DEBUG | LOG_WARNING, pszFmt, argp);
	va_end(argp);
}

///////////////////////////////////////////////////////////////////////////////
//

void DebugLog(UINT uFlags, LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	LogV(uFlags | LOG_DEBUG, pszFmt, argp);
	va_end(argp);
}

void DebugLogError(UINT uFlags, LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	LogV(uFlags | LOG_DEBUG | LOG_ERROR, pszFmt, argp);
	va_end(argp);
}

void DebugLogWarning(UINT uFlags, LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	LogV(uFlags | LOG_DEBUG | LOG_WARNING, pszFmt, argp);
	va_end(argp);
}


///////////////////////////////////////////////////////////////////////////////
//

void AddLogLine(bool bAddToStatusBar, LPCTSTR pszLine, ...)
{
	ASSERT(pszLine != NULL);
	if (pszLine == NULL)
		return;

	va_list argptr;
	va_start(argptr, pszLine);
	AddLogTextV(LOG_DEFAULT | (bAddToStatusBar ? LOG_STATUSBAR : 0), DLP_DEFAULT, pszLine, argptr);
	va_end(argptr);
}

void AddDebugLogLine(bool bAddToStatusBar, LPCTSTR pszLine, ...)
{
	ASSERT(pszLine != NULL);
	if (pszLine == NULL)
		return;

	va_list argptr;
	va_start(argptr, pszLine);
	AddLogTextV(LOG_DEBUG | (bAddToStatusBar ? LOG_STATUSBAR : 0), DLP_DEFAULT, pszLine, argptr);
	va_end(argptr);
}

void AddDebugLogLine(EDebugLogPriority Priority, bool bAddToStatusBar, LPCTSTR pszLine, ...)
{
	// loglevel needs to be merged with LOG_WARNING and LOG_ERROR later
	// (which only means 3 instead of 5 levels which you can select in the preferences)
	// makes no sense to implement two different priority indicators
	//
	// until there is some time todo this, It will convert DLP_VERYHIGH to ERRORs
	// and DLP_HIGH to LOG_WARNING in order to be able using the Loglevel and color indicator
	ASSERT(pszLine != NULL);
	if (pszLine == NULL)
		return;

	va_list argptr;
	va_start(argptr, pszLine);
	uint32 nFlag;
	if (Priority == DLP_VERYHIGH)
		nFlag = LOG_ERROR;
	else if (Priority == DLP_HIGH)
		nFlag = LOG_WARNING;
	else
		nFlag = 0;

	AddLogTextV(LOG_DEBUG | nFlag | (bAddToStatusBar ? LOG_STATUSBAR : 0), Priority, pszLine, argptr);
	va_end(argptr);
}

void AddLogTextV(UINT uFlags, EDebugLogPriority dlpPriority, LPCTSTR pszLine, va_list argptr)
{
	ASSERT(pszLine != NULL);
	if (pszLine == NULL)
		return;

	if ((uFlags & LOG_DEBUG) && !(thePrefs.GetVerbose() && dlpPriority >= thePrefs.GetVerboseLogPriority()))
		return;

	CString strLogLine;
	strLogLine.FormatV(pszLine, argptr);
	AddRecentLogEntry(uFlags, strLogLine);

	if (theApp.emuledlg)
		theApp.emuledlg->AddLogText(uFlags, strLogLine);
	else {
		TRACE(_T("App Log: %s\n"), (LPCTSTR)strLogLine);

		CString strFullLogLine;
		strFullLogLine.Format(_T("%s: %s\r\n"), (LPCTSTR)CTime::GetCurrentTime().Format(thePrefs.GetDateTimeFormat4Log()), (LPCTSTR)strLogLine);
		if (!strFullLogLine.IsEmpty()) {
			if (!(uFlags & LOG_DEBUG) && thePrefs.GetLog2Disk())
				theLog.Log(strFullLogLine, strFullLogLine.GetLength());

			if (thePrefs.GetVerbose() && ((uFlags & LOG_DEBUG) || thePrefs.GetFullVerbose()))
				if (thePrefs.GetDebug2Disk())
					theVerboseLog.Log(strFullLogLine, strFullLogLine.GetLength());
		}
	}
}


///////////////////////////////////////////////////////////////////////////////
// CLogFile

CLogFile::CLogFile()
	: m_fp()
	, m_tStarted()
	, m_uBytesWritten()
	, m_uMaxFileSize(_UI32_MAX)
	, m_bInOpenCall()
	, m_bFlushOnWrite(true)
	, m_eFileFormat(Unicode)
{
	ASSERT(Unicode == 0);
}

CLogFile::~CLogFile()
{
	Close();
}

bool CLogFile::SetFilePath(LPCTSTR pszFilePath)
{
	if (IsOpen())
		return false;
	m_strFilePath = pszFilePath;
	return true;
}

void CLogFile::SetMaxFileSize(UINT uMaxFileSize)
{
	if (uMaxFileSize < 0x10000u)
		m_uMaxFileSize = (uMaxFileSize == 0) ? _UI32_MAX : 0x10000u;
	else
		m_uMaxFileSize = uMaxFileSize;
}

bool CLogFile::SetFileFormat(const ELogFileFormat eFileFormat)
{
	if (eFileFormat != Unicode && eFileFormat != Utf8) {
		ASSERT(0);
		return false;
	}
	if (m_fp != NULL)
		return false; // can't change file format on-the-fly
	m_eFileFormat = eFileFormat;
	return true;
}

bool CLogFile::SetFlushOnWrite(bool bFlushOnWrite)
{
	if (m_fp != NULL)
		return false;
	m_bFlushOnWrite = bFlushOnWrite;
	return true;
}

bool CLogFile::Create(LPCTSTR pszFilePath, UINT uMaxFileSize, const ELogFileFormat eFileFormat)
{
	Close();
	m_strFilePath = pszFilePath;
	m_uMaxFileSize = uMaxFileSize;
	m_eFileFormat = eFileFormat;
	return Open();
}

bool CLogFile::Open()
{
	if (m_fp != NULL)
		return true;

	m_fp = LongPathSeams::OpenFileStreamDenyWriteLongPath(m_strFilePath, _T("a+b"));
	if (m_fp != NULL) {
		if (!m_bFlushOnWrite)
			::setvbuf(m_fp, NULL, _IOFBF, 64 * 1024);
		m_tStarted = time(NULL);
		m_uBytesWritten = _filelength(_fileno(m_fp));
		if (m_uBytesWritten == 0) {
			if (m_eFileFormat == Unicode) {
				// write Unicode byte order mark 0xFEFF
				fputwc(u'\xFEFF', m_fp);
			} else {
				ASSERT(m_eFileFormat == Utf8);
				; // could write UTF-8 header.
			}
		} else if (m_uBytesWritten >= sizeof(WORD)) {
			// check for Unicode byte order mark 0xFEFF
			WORD wBOM;
			if (fread(&wBOM, sizeof wBOM, 1, m_fp) == 1) {
				if (wBOM == u'\xFEFF' && m_eFileFormat == Unicode) {
					// log file already in Unicode format
					(void)fseek(m_fp, 0, SEEK_END); // actually not needed because file is opened in 'Append' mode.
				} else if (wBOM != u'\xFEFF' && m_eFileFormat != Unicode) {
					// log file already in UTF-8 format
					(void)fseek(m_fp, 0, SEEK_END); // actually not needed because file is opened in 'Append' mode.
				} else {
					// log file does not have the required format, create a new one (with the req. format)
					ASSERT((m_eFileFormat == Unicode && wBOM != u'\xFEFF') || (m_eFileFormat == Utf8 && wBOM == 0xFEFF));

					ASSERT(!m_bInOpenCall);
					if (!m_bInOpenCall) { // just for safety
						m_bInOpenCall = true;
						StartNewLogFile();
						m_bInOpenCall = false;
					}
				}
			}
		}
	}
	return m_fp != NULL;
}

bool CLogFile::Close()
{
	if (m_fp == NULL)
		return true;
	bool bResult = (fclose(m_fp) == 0);
	m_fp = NULL;
	m_tStarted = 0;
	m_uBytesWritten = 0;
	return bResult;
}

bool CLogFile::Log(LPCTSTR pszMsg, int iLen)
{
	if (m_fp == NULL)
		return false;

	CString strOwnedLine;
	const TCHAR *pszWrite = pszMsg;
	size_t uToWriteChars = (iLen == -1) ? _tcslen(pszMsg) : static_cast<size_t>(iLen);
	if (uToWriteChars > static_cast<size_t>(kMaxFileLogLineChars)) {
		strOwnedLine = TruncateLogLine(CString(pszMsg, static_cast<int>(uToWriteChars)), kMaxFileLogLineChars);
		pszWrite = strOwnedLine;
		uToWriteChars = static_cast<size_t>(strOwnedLine.GetLength());
	}

	size_t uWritten;
	if (m_eFileFormat == Unicode) {
		// don't use 'fputs' + '_filelength' -- gives poor performance
		const size_t uToWriteBytes = uToWriteChars * sizeof(TCHAR);
		uWritten = fwrite(pszWrite, 1, uToWriteBytes, m_fp);
	} else {
		TUnicodeToUTF8<2048> utf8(pszWrite, static_cast<int>(uToWriteChars));
		uWritten = fwrite((LPCSTR)utf8, 1, utf8.GetLength(), m_fp);
	}
	bool bResult = !ferror(m_fp);
	m_uBytesWritten += uWritten;

	if (m_uBytesWritten >= m_uMaxFileSize)
		StartNewLogFile();
	else if (m_bFlushOnWrite)
		fflush(m_fp);

	return bResult;
}

bool CLogFile::Logf(LPCTSTR pszFmt, ...)
{
	if (m_fp == NULL)
		return false;

	va_list argp;
	va_start(argp, pszFmt);

	CString strMsg;
	strMsg.FormatV(pszFmt, argp);
	va_end(argp);

	CString strFullMsg;
	strFullMsg.Format(_T("%s: %s\r\n"), (LPCTSTR)CTime::GetCurrentTime().Format(thePrefs.GetDateTimeFormat4Log()), (LPCTSTR)strMsg);

	return !strFullMsg.IsEmpty() && Log(strFullMsg, strFullMsg.GetLength());
}

bool CLogFile::FlushToDisk()
{
	if (m_fp == NULL)
		return false;
	if (fflush(m_fp) != 0)
		return false;
	return _commit(_fileno(m_fp)) == 0;
}

void CLogFile::StartNewLogFile()
{
	time_t tStarted = m_tStarted;
	Close();

	TCHAR szDateLogStarted[40];
	tm tmStarted = {};
	if (localtime_s(&tmStarted, &tStarted) == 0)
		_tcsftime(szDateLogStarted, _countof(szDateLogStarted), _T("%Y%m%d-%H%M%S"), &tmStarted);
	else
		szDateLogStarted[0] = _T('\0');
	const CString strLogBakFilePath = LogFileSeams::BuildRotatedLogFilePath(m_strFilePath, CString(szDateLogStarted));

	if (!LongPathSeams::MoveFile(m_strFilePath, strLogBakFilePath))
		VERIFY(LongPathSeams::DeleteFile(m_strFilePath) != FALSE);

	Open();
}
