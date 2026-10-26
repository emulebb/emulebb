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
#include "DiagEventWriterSeams.h"
#include "Log.h"
#include "LogFileSeams.h"
#include "PathHelpers.h"
#ifdef EMULEBB_ENABLE_PACKET_DIAGNOSTICS
#include "Opcodes.h"
#endif
#include "OtherFunctions.h"
#include "Preferences.h"
#include "emuledlg.h"
#include "StringConversion.h"

#include <algorithm>
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

#if EMULEBB_HAS_DIAG_EVENT_V2
namespace
{
struct SDiagEventStreamCounter
{
	CString strFamily;
	ULONGLONG ullSequence;
};

struct SDiagEventSegmentFile
{
	CString strPath;
	ULONGLONG ullLastWriteTime;
	ULONGLONG ullBytes;
	bool bActive;
};

class CDiagEventV2Writer
{
public:
	CDiagEventV2Writer()
		: m_pFile(NULL)
		, m_lConfigured()
		, m_bInitialized(false)
		, m_dwProcessId()
		, m_ullSegmentId()
		, m_ullSequence()
		, m_ullActiveSegmentBytes()
		, m_ullTotalBytesWritten()
		, m_ullRecordsWritten()
		, m_ullDroppedEvents()
		, m_ullEncodeErrors()
		, m_ullWriteErrors()
		, m_ullRotations()
		, m_ullRetentionDeletes()
		, m_ullRetentionDeleteErrors()
		, m_ullRetentionScanErrors()
		, m_ullFreeSpaceProbeErrors()
		, m_ullRetentionPolicyErrors()
		, m_ullStoragePolicyDrops()
		, m_ullStorageFaultDrops()
		, m_ullRetentionLimitDrops()
		, m_ullAggregateLimitDrops()
		, m_ullMinimumFreeSpaceDrops()
		, m_ullWriterFaultDrops()
		, m_uRetainedSegmentCount()
		, m_ullRetainedBytes()
		, m_ullAvailableBytes()
		, m_bAvailableBytesKnown(false)
		, m_bStorageAuthorityFaulted(false)
		, m_bStoragePolicySatisfied(false)
		, m_ullMaxSegmentBytes(DiagEventWriterSeams::kDefaultSegmentBytes)
		, m_ullSegmentStartedTick()
		, m_ullLastDiskFlushTick()
		, m_ullLastHealthWriteTick()
	{
	}

	~CDiagEventV2Writer()
	{
		CSingleLock lock(&m_lock, TRUE);
		CloseActiveSegmentLocked();
		WriteHealthLocked(true);
	}

	bool Initialize(LPCTSTR pszLogDirectory, UINT uConfiguredSegmentBytes)
	{
		CSingleLock lock(&m_lock, TRUE);
		CloseActiveSegmentLocked();
		m_strLogDirectory = pszLogDirectory != NULL ? pszLogDirectory : _T("");
		m_strRunId = BuildRunId();
		m_dwProcessId = ::GetCurrentProcessId();
		m_ullSegmentId = 0;
		m_ullSequence = 0;
		m_ullActiveSegmentBytes = 0;
		m_ullTotalBytesWritten = 0;
		m_ullRecordsWritten = 0;
		m_ullDroppedEvents = 0;
		m_ullEncodeErrors = 0;
		m_ullWriteErrors = 0;
		m_ullRotations = 0;
		m_ullRetentionDeletes = 0;
		m_ullRetentionDeleteErrors = 0;
		m_ullRetentionScanErrors = 0;
		m_ullFreeSpaceProbeErrors = 0;
		m_ullRetentionPolicyErrors = 0;
		m_ullStoragePolicyDrops = 0;
		m_ullStorageFaultDrops = 0;
		m_ullRetentionLimitDrops = 0;
		m_ullAggregateLimitDrops = 0;
		m_ullMinimumFreeSpaceDrops = 0;
		m_ullWriterFaultDrops = 0;
		m_uRetainedSegmentCount = 0;
		m_ullRetainedBytes = 0;
		m_ullAvailableBytes = 0;
		m_bAvailableBytesKnown = false;
		m_bStorageAuthorityFaulted = false;
		m_bStoragePolicySatisfied = false;
		m_strStorageBlockReason.Empty();
		m_ullMaxSegmentBytes = DiagEventWriterSeams::NormalizeSegmentBytes(uConfiguredSegmentBytes);
		m_ullSegmentStartedTick = ::GetTickCount64();
		m_ullLastDiskFlushTick = 0;
		m_ullLastHealthWriteTick = 0;
		m_strInitializationFailure.Empty();
		m_strActiveFileName.Empty();
		m_strActiveFilePath.Empty();
		m_aStreamCounters.clear();
		m_strHealthFilePath = PathHelpers::AppendPathComponent(
			m_strLogDirectory,
			DiagEventWriterSeams::HealthFileName());
		::InterlockedExchange(&m_lConfigured, 1);

		m_bInitialized = OpenSegmentLocked(0, false);
		if (!m_bInitialized) {
			m_strInitializationFailure.Format(
				_T("unable to create first diagnostic segment (win32=%u)"),
				static_cast<UINT>(::GetLastError()));
		}
		WriteHealthLocked(true);
		return m_bInitialized;
	}

	bool IsConfigured() const
	{
		return ::InterlockedCompareExchange(
			const_cast<volatile LONG *>(&m_lConfigured),
			0,
			0) != 0;
	}

	CString BuildHealthJson()
	{
		CSingleLock lock(&m_lock, TRUE);
		RefreshStorageHealthLocked();
		return BuildHealthJsonLocked();
	}

	void WriteEvent(
		LPCTSTR pszFamily,
		LPCTSTR pszEvent,
		LPCTSTR pszSeverity,
		const CString &rstrKeysJson,
		const CString &rstrBodyJson)
	{
		CSingleLock lock(&m_lock, TRUE);
		if (!IsConfigured())
			return;

		if (m_pFile == NULL) {
			const bool bRecoveringInitializedWriter = m_bInitialized;
			const ULONGLONG ullRecoverySegmentId = bRecoveringInitializedWriter
				? m_ullSegmentId + 1
				: m_ullSegmentId;
			m_bInitialized = OpenSegmentLocked(ullRecoverySegmentId, bRecoveringInitializedWriter);
			if (m_bInitialized)
				m_strInitializationFailure.Empty();
			else {
				++m_ullDroppedEvents;
				WriteHealthLocked(true);
				return;
			}
		}

		const CString strFamily(pszFamily != NULL ? pszFamily : _T(""));
		const ULONGLONG ullSequence = m_ullSequence + 1;
		const ULONGLONG ullStreamSequence = NextStreamSequenceLocked(strFamily);
		CString strJson(BuildRecordLocked(
			m_ullSegmentId,
			ullSequence,
			ullStreamSequence,
			strFamily,
			pszEvent,
			pszSeverity,
			rstrKeysJson,
			rstrBodyJson));
		CStringA strUtf8(EncodeUtf8(strJson));
		if (static_cast<ULONGLONG>(strUtf8.GetLength()) > m_ullMaxSegmentBytes) {
			++m_ullDroppedEvents;
			++m_ullEncodeErrors;
			WriteHealthLocked(true);
			return;
		}

		const ULONGLONG ullNowTick = ::GetTickCount64();
		if (DiagEventWriterSeams::ShouldRotate(
			m_ullActiveSegmentBytes,
			static_cast<ULONGLONG>(strUtf8.GetLength()),
			ullNowTick - m_ullSegmentStartedTick,
			m_ullMaxSegmentBytes,
			DiagEventWriterSeams::kDefaultSegmentAgeMs)) {
			if (!OpenSegmentLocked(m_ullSegmentId + 1, true)) {
				++m_ullDroppedEvents;
				WriteHealthLocked(true);
				return;
			}
			strJson = BuildRecordLocked(
				m_ullSegmentId,
				ullSequence,
				ullStreamSequence,
				strFamily,
				pszEvent,
				pszSeverity,
				rstrKeysJson,
				rstrBodyJson);
			strUtf8 = EncodeUtf8(strJson);
			if (static_cast<ULONGLONG>(strUtf8.GetLength()) > m_ullMaxSegmentBytes) {
				++m_ullDroppedEvents;
				++m_ullEncodeErrors;
				WriteHealthLocked(true);
				return;
			}
		}

		const ULONGLONG ullOffset = m_ullActiveSegmentBytes;
		const size_t uExpectedBytes = static_cast<size_t>(strUtf8.GetLength());
		const size_t uWritten = fwrite((LPCSTR)strUtf8, 1, uExpectedBytes, m_pFile);
		const bool bComplete = uWritten == uExpectedBytes && !ferror(m_pFile) && fflush(m_pFile) == 0;
		if (!bComplete) {
			++m_ullDroppedEvents;
			++m_ullWriteErrors;
			RollbackPartialWriteLocked(ullOffset);
			WriteHealthLocked(true);
			return;
		}

		m_ullActiveSegmentBytes += uWritten;
		m_ullTotalBytesWritten += uWritten;
		++m_ullRecordsWritten;
		m_ullSequence = ullSequence;
		CommitStreamSequenceLocked(strFamily, ullStreamSequence);
		if (m_ullLastDiskFlushTick == 0
			|| ullNowTick >= m_ullLastDiskFlushTick + kDiagnosticsDiskFlushIntervalMs) {
			if (_commit(_fileno(m_pFile)) != 0)
				++m_ullWriteErrors;
			m_ullLastDiskFlushTick = ullNowTick;
		}
		WriteHealthLocked(false);
	}

private:
	static CString BuildRunId()
	{
		FILETIME fileTime = {};
		::GetSystemTimeAsFileTime(&fileTime);
		ULARGE_INTEGER timestamp = {};
		timestamp.LowPart = fileTime.dwLowDateTime;
		timestamp.HighPart = fileTime.dwHighDateTime;

		CString strRunId;
		strRunId.Format(
			_T("%016I64x-%08x-%016I64x"),
			timestamp.QuadPart,
			::GetCurrentProcessId(),
			::GetTickCount64());
		return strRunId;
	}

	static CStringA EncodeUtf8(const CString &rstrText)
	{
		TUnicodeToUTF8<2048> utf8(rstrText);
		return CStringA((LPCSTR)utf8, utf8.GetLength());
	}

	static ULONGLONG FileTimeValue(const FILETIME &rFileTime)
	{
		ULARGE_INTEGER value = {};
		value.LowPart = rFileTime.dwLowDateTime;
		value.HighPart = rFileTime.dwHighDateTime;
		return value.QuadPart;
	}

	static ULONGLONG FileSizeValue(const WIN32_FIND_DATA &rFindData)
	{
		ULARGE_INTEGER value = {};
		value.LowPart = rFindData.nFileSizeLow;
		value.HighPart = rFindData.nFileSizeHigh;
		return value.QuadPart;
	}

	CString BuildRecordLocked(
		ULONGLONG ullSegmentId,
		ULONGLONG ullSequence,
		ULONGLONG ullStreamSequence,
		const CString &rstrFamily,
		LPCTSTR pszEvent,
		LPCTSTR pszSeverity,
		const CString &rstrKeysJson,
		const CString &rstrBodyJson) const
	{
		const CString strKeys(rstrKeysJson.IsEmpty() ? CString(_T("{}")) : rstrKeysJson);
		const CString strBody(rstrBodyJson.IsEmpty() ? CString(_T("{}")) : rstrBodyJson);
		CString strJson;
		strJson.Format(
			_T("{\"schema\":\"diag_event_v2\",\"client\":\"mfc\",\"ts\":%s,\"runId\":%s,\"processId\":%u,\"segmentId\":%I64u,\"seq\":%I64u,\"streamSeq\":%I64u,\"family\":%s,\"event\":%s,\"severity\":%s,\"keys\":%s,\"body\":%s}\r\n"),
			(LPCTSTR)BuildDiagnosticsJsonStringField(BuildDiagnosticsTimestampUtc()),
			(LPCTSTR)BuildDiagnosticsJsonStringField(m_strRunId),
			static_cast<UINT>(m_dwProcessId),
			ullSegmentId,
			ullSequence,
			ullStreamSequence,
			(LPCTSTR)BuildDiagnosticsJsonStringField(rstrFamily),
			(LPCTSTR)BuildDiagnosticsJsonStringField(pszEvent),
			(LPCTSTR)BuildDiagnosticsJsonStringField(pszSeverity),
			(LPCTSTR)strKeys,
			(LPCTSTR)strBody);
		return strJson;
	}

	ULONGLONG NextStreamSequenceLocked(const CString &rstrFamily) const
	{
		for (const SDiagEventStreamCounter &rCounter : m_aStreamCounters) {
			if (rCounter.strFamily == rstrFamily)
				return rCounter.ullSequence + 1;
		}
		return 1;
	}

	void CommitStreamSequenceLocked(const CString &rstrFamily, ULONGLONG ullSequence)
	{
		for (SDiagEventStreamCounter &rCounter : m_aStreamCounters) {
			if (rCounter.strFamily == rstrFamily) {
				rCounter.ullSequence = ullSequence;
				return;
			}
		}
		m_aStreamCounters.push_back(SDiagEventStreamCounter{rstrFamily, ullSequence});
	}

	bool OpenSegmentLocked(ULONGLONG ullSegmentId, bool bRotation)
	{
		CString strFileName;
		CString strPath;
		FILE *pNewFile = NULL;
		ULONGLONG ullCandidateSegmentId = ullSegmentId;
		for (;;) {
			strFileName = DiagEventWriterSeams::BuildSegmentFileName(
				m_dwProcessId,
				m_strRunId,
				ullCandidateSegmentId);
			strPath = PathHelpers::AppendPathComponent(m_strLogDirectory, strFileName);
			pNewFile = LongPathSeams::OpenFileStreamLongPath(
				strPath,
				GENERIC_WRITE,
				FILE_SHARE_READ,
				CREATE_NEW,
				FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
				_O_WRONLY | _O_BINARY,
				"wb");
			if (pNewFile != NULL)
				break;

			const DWORD dwOpenError = ::GetLastError();
			const bool bExistingFile = dwOpenError == ERROR_FILE_EXISTS
				|| dwOpenError == ERROR_ALREADY_EXISTS
				|| LongPathSeams::GetFileAttributes(strPath) != INVALID_FILE_ATTRIBUTES;
			if (!bExistingFile || ullCandidateSegmentId == ~0ull) {
				if (bRotation)
					++m_ullWriteErrors;
				::SetLastError(dwOpenError);
				return false;
			}
			++ullCandidateSegmentId;
		}
		(void)setvbuf(pNewFile, NULL, _IOFBF, 64 * 1024);
		CloseActiveSegmentLocked();
		m_pFile = pNewFile;
		m_strActiveFileName = strFileName;
		m_strActiveFilePath = strPath;
		m_ullSegmentId = ullCandidateSegmentId;
		m_ullActiveSegmentBytes = 0;
		m_ullSegmentStartedTick = ::GetTickCount64();
		m_ullLastDiskFlushTick = 0;
		if (bRotation)
			++m_ullRotations;
		PruneSegmentsLocked();
		return true;
	}

	void CloseActiveSegmentLocked()
	{
		if (m_pFile == NULL)
			return;
		if (fflush(m_pFile) != 0 || _commit(_fileno(m_pFile)) != 0)
			++m_ullWriteErrors;
		if (fclose(m_pFile) != 0)
			++m_ullWriteErrors;
		m_pFile = NULL;
	}

	void RollbackPartialWriteLocked(ULONGLONG ullOffset)
	{
		if (m_pFile == NULL)
			return;
		clearerr(m_pFile);
		const int nFileDescriptor = _fileno(m_pFile);
		if (_chsize_s(nFileDescriptor, ullOffset) == 0
			&& _fseeki64(m_pFile, 0, SEEK_END) == 0) {
			clearerr(m_pFile);
			return;
		}
		CloseActiveSegmentLocked();
		m_bInitialized = OpenSegmentLocked(m_ullSegmentId + 1, true);
	}

	bool CollectSegmentsLocked(std::vector<SDiagEventSegmentFile> &rFiles)
	{
		rFiles.clear();
		const CString strPattern(PathHelpers::AppendPathComponent(
			m_strLogDirectory,
			DiagEventWriterSeams::SegmentFilePattern()));
		WIN32_FIND_DATA findData = {};
		const HANDLE hFind = LongPathSeams::FindFirstFile(strPattern, &findData);
		if (hFind == INVALID_HANDLE_VALUE) {
			if (::GetLastError() == ERROR_FILE_NOT_FOUND)
				return true;
			++m_ullRetentionScanErrors;
			m_bStorageAuthorityFaulted = true;
			m_strStorageBlockReason = _T("retentionScanFailure");
			return false;
		}
		do {
			if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
				continue;
			const CString strPath(PathHelpers::AppendPathComponent(m_strLogDirectory, findData.cFileName));
			rFiles.push_back(SDiagEventSegmentFile{
				strPath,
				FileTimeValue(findData.ftLastWriteTime),
				FileSizeValue(findData),
				strPath.CompareNoCase(m_strActiveFilePath) == 0});
		} while (::FindNextFile(hFind, &findData) != FALSE);
		const DWORD dwEnumerationError = ::GetLastError();
		::FindClose(hFind);
		if (dwEnumerationError != ERROR_NO_MORE_FILES) {
			++m_ullRetentionScanErrors;
			m_bStorageAuthorityFaulted = true;
			m_strStorageBlockReason = _T("retentionScanFailure");
			return false;
		}
		return true;
	}

	void PruneSegmentsLocked()
	{
		std::vector<SDiagEventSegmentFile> files;
		if (!CollectSegmentsLocked(files))
			return;

		std::sort(files.begin(), files.end(), [](const SDiagEventSegmentFile &rLeft, const SDiagEventSegmentFile &rRight) {
			if (rLeft.ullLastWriteTime != rRight.ullLastWriteTime)
				return rLeft.ullLastWriteTime < rRight.ullLastWriteTime;
			return rLeft.strPath.CompareNoCase(rRight.strPath) < 0;
		});
		FILETIME nowFileTime = {};
		::GetSystemTimeAsFileTime(&nowFileTime);
		const ULONGLONG ullNowFileTime = FileTimeValue(nowFileTime);
		const ULONGLONG ullMaximumAgeTicks =
			DiagEventWriterSeams::kDefaultMaxRetainedSeconds * 10000000ull;
		size_t uRemaining = files.size();
		for (const SDiagEventSegmentFile &rFile : files) {
			if (rFile.bActive)
				continue;
			const bool bExpired = ullNowFileTime > rFile.ullLastWriteTime
				&& ullNowFileTime - rFile.ullLastWriteTime > ullMaximumAgeTicks;
			if (!bExpired && uRemaining <= DiagEventWriterSeams::kDefaultRetainedSegments)
				continue;
			if (LongPathSeams::DeleteFile(rFile.strPath) != FALSE) {
				--uRemaining;
				++m_ullRetentionDeletes;
			} else {
				++m_ullRetentionDeleteErrors;
				m_bStorageAuthorityFaulted = true;
				m_strStorageBlockReason = _T("retentionDeleteFailure");
			}
		}
	}

	void RefreshStorageHealthLocked()
	{
		std::vector<SDiagEventSegmentFile> files;
		if (CollectSegmentsLocked(files)) {
			m_uRetainedSegmentCount = files.size();
			m_ullRetainedBytes = 0;
			for (const SDiagEventSegmentFile &rFile : files) {
				m_ullRetainedBytes += rFile.bActive ? m_ullActiveSegmentBytes : rFile.ullBytes;
			}
		}

		ULARGE_INTEGER availableBytes = {};
		if (!m_strLogDirectory.IsEmpty()
			&& ::GetDiskFreeSpaceEx(m_strLogDirectory, &availableBytes, NULL, NULL) != FALSE) {
			m_ullAvailableBytes = availableBytes.QuadPart;
			m_bAvailableBytesKnown = true;
		} else {
			++m_ullFreeSpaceProbeErrors;
			m_bAvailableBytesKnown = false;
			m_bStorageAuthorityFaulted = true;
			m_strStorageBlockReason = _T("freeSpaceProbeFailure");
		}

		const ULONGLONG ullMaximumTotalBytes = DiagEventWriterSeams::MaximumTotalBytes(m_ullMaxSegmentBytes);
		const bool bWithinRetentionLimit = m_uRetainedSegmentCount <= DiagEventWriterSeams::kDefaultRetainedSegments;
		const bool bWithinAggregateLimit = m_ullRetainedBytes <= ullMaximumTotalBytes;
		const bool bWithinFreeSpaceLimit = m_bAvailableBytesKnown
			&& m_ullAvailableBytes >= DiagEventWriterSeams::kDefaultMinimumFreeBytes;
		m_bStoragePolicySatisfied = !m_bStorageAuthorityFaulted
			&& bWithinRetentionLimit
			&& bWithinAggregateLimit
			&& bWithinFreeSpaceLimit;
		if (!m_bStorageAuthorityFaulted && !m_bStoragePolicySatisfied) {
			if (!bWithinRetentionLimit)
				m_strStorageBlockReason = _T("retainedSegmentLimit");
			else if (!bWithinAggregateLimit)
				m_strStorageBlockReason = _T("aggregateByteLimit");
			else
				m_strStorageBlockReason = _T("minimumFreeSpace");
		} else if (m_bStoragePolicySatisfied) {
			m_strStorageBlockReason.Empty();
		}
	}

	CString BuildHealthJsonLocked() const
	{
		const CString strRunIdJson(m_strRunId.IsEmpty()
			? CString(_T("null"))
			: BuildDiagnosticsJsonStringField(m_strRunId));
		CString strSegmentIdJson(_T("null"));
		if (!m_strActiveFileName.IsEmpty())
			strSegmentIdJson.Format(_T("%I64u"), m_ullSegmentId);
		const CString strActiveFileJson(m_strActiveFileName.IsEmpty()
			? CString(_T("null"))
			: BuildDiagnosticsJsonStringField(m_strActiveFileName));
		const CString strStorageBlockReasonJson(m_strStorageBlockReason.IsEmpty()
			? CString(_T("null"))
			: BuildDiagnosticsJsonStringField(m_strStorageBlockReason));
		const CString strInitializationFailureJson(m_strInitializationFailure.IsEmpty()
			? CString(_T("null"))
			: BuildDiagnosticsJsonStringField(m_strInitializationFailure));
		CString strAvailableBytesJson(_T("null"));
		CString strMaximumGrowthBytesJson(_T("null"));
		if (m_bAvailableBytesKnown) {
			strAvailableBytesJson.Format(_T("%I64u"), m_ullAvailableBytes);
			strMaximumGrowthBytesJson.Format(
				_T("%I64u"),
				DiagEventWriterSeams::MaximumGrowthBytes(
					m_ullRetainedBytes,
					m_ullAvailableBytes,
					DiagEventWriterSeams::MaximumTotalBytes(m_ullMaxSegmentBytes),
					DiagEventWriterSeams::kDefaultMinimumFreeBytes));
		}

		CString strJson;
		strJson.Format(
			_T("{\"schema\":\"diag_event_v2\",\"enabled\":true,\"configured\":%s,\"initialized\":%s,\"runId\":%s,\"processId\":%u,\"activeSegmentId\":%s,\"activeFileName\":%s"),
			IsConfigured() ? _T("true") : _T("false"),
			m_bInitialized ? _T("true") : _T("false"),
			(LPCTSTR)strRunIdJson,
			static_cast<UINT>(m_dwProcessId),
			(LPCTSTR)strSegmentIdJson,
			(LPCTSTR)strActiveFileJson);
		strJson.AppendFormat(
			_T(",\"activeSegmentBytes\":%I64u,\"recordsWritten\":%I64u,\"bytesWritten\":%I64u,\"droppedEvents\":%I64u,\"encodeErrors\":%I64u,\"writeErrors\":%I64u,\"rotations\":%I64u"),
			m_ullActiveSegmentBytes,
			m_ullRecordsWritten,
			m_ullTotalBytesWritten,
			m_ullDroppedEvents,
			m_ullEncodeErrors,
			m_ullWriteErrors,
			m_ullRotations);
		strJson.AppendFormat(
			_T(",\"retentionDeletes\":%I64u,\"retentionDeleteErrors\":%I64u,\"retentionScanErrors\":%I64u,\"freeSpaceProbeErrors\":%I64u,\"retentionPolicyErrors\":%I64u"),
			m_ullRetentionDeletes,
			m_ullRetentionDeleteErrors,
			m_ullRetentionScanErrors,
			m_ullFreeSpaceProbeErrors,
			m_ullRetentionPolicyErrors);
		strJson.AppendFormat(
			_T(",\"storagePolicyDrops\":%I64u,\"storageFaultDrops\":%I64u,\"retentionLimitDrops\":%I64u,\"aggregateLimitDrops\":%I64u,\"minimumFreeSpaceDrops\":%I64u,\"writerFaultDrops\":%I64u"),
			m_ullStoragePolicyDrops,
			m_ullStorageFaultDrops,
			m_ullRetentionLimitDrops,
			m_ullAggregateLimitDrops,
			m_ullMinimumFreeSpaceDrops,
			m_ullWriterFaultDrops);
		strJson.AppendFormat(
			_T(",\"retainedSegmentCount\":%Iu,\"retainedBytes\":%I64u,\"availableBytes\":%s,\"storageAuthorityFaulted\":%s,\"writerIoFaulted\":%s,\"storageBlockReason\":%s,\"storagePolicySatisfied\":%s,\"maximumGrowthBytes\":%s"),
			m_uRetainedSegmentCount,
			m_ullRetainedBytes,
			(LPCTSTR)strAvailableBytesJson,
			m_bStorageAuthorityFaulted ? _T("true") : _T("false"),
			m_ullWriteErrors != 0 ? _T("true") : _T("false"),
			(LPCTSTR)strStorageBlockReasonJson,
			m_bStoragePolicySatisfied ? _T("true") : _T("false"),
			(LPCTSTR)strMaximumGrowthBytesJson);
		strJson.AppendFormat(
			_T(",\"maxSegmentBytes\":%I64u,\"maxSegmentSeconds\":%I64u,\"retainedSegments\":%Iu,\"maxTotalBytes\":%I64u,\"maxRetainedSeconds\":%I64u,\"minimumFreeBytes\":%I64u,\"initializationFailure\":%s}"),
			m_ullMaxSegmentBytes,
			DiagEventWriterSeams::kDefaultSegmentAgeMs / 1000ull,
			DiagEventWriterSeams::kDefaultRetainedSegments,
			DiagEventWriterSeams::MaximumTotalBytes(m_ullMaxSegmentBytes),
			DiagEventWriterSeams::kDefaultMaxRetainedSeconds,
			DiagEventWriterSeams::kDefaultMinimumFreeBytes,
			(LPCTSTR)strInitializationFailureJson);
		return strJson;
	}

	void WriteHealthLocked(bool bForce)
	{
		if (m_strHealthFilePath.IsEmpty())
			return;
		const ULONGLONG ullNowTick = ::GetTickCount64();
		if (!bForce && m_ullLastHealthWriteTick != 0
			&& ullNowTick < m_ullLastHealthWriteTick + kDiagnosticsDiskFlushIntervalMs)
			return;

		RefreshStorageHealthLocked();
		const CString strJson(BuildHealthJsonLocked() + _T("\r\n"));

		CString strTemporaryPath;
		strTemporaryPath.Format(_T("%s.tmp-%u"), (LPCTSTR)m_strHealthFilePath, static_cast<UINT>(m_dwProcessId));
		FILE *pHealthFile = LongPathSeams::OpenFileStreamDenyWriteLongPath(strTemporaryPath, _T("wb"));
		if (pHealthFile == NULL)
			return;
		const CStringA strUtf8(EncodeUtf8(strJson));
		const size_t uExpectedBytes = static_cast<size_t>(strUtf8.GetLength());
		const bool bWritten = fwrite((LPCSTR)strUtf8, 1, uExpectedBytes, pHealthFile) == uExpectedBytes
			&& !ferror(pHealthFile)
			&& fflush(pHealthFile) == 0
			&& _commit(_fileno(pHealthFile)) == 0;
		const bool bClosed = fclose(pHealthFile) == 0;
		if (!bWritten || !bClosed
			|| LongPathSeams::MoveFileEx(
				strTemporaryPath,
				m_strHealthFilePath,
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
			(void)LongPathSeams::DeleteFile(strTemporaryPath);
			return;
		}
		m_ullLastHealthWriteTick = ullNowTick;
	}

	FILE *m_pFile;
	volatile LONG m_lConfigured;
	bool m_bInitialized;
	DWORD m_dwProcessId;
	ULONGLONG m_ullSegmentId;
	ULONGLONG m_ullSequence;
	ULONGLONG m_ullActiveSegmentBytes;
	ULONGLONG m_ullTotalBytesWritten;
	ULONGLONG m_ullRecordsWritten;
	ULONGLONG m_ullDroppedEvents;
	ULONGLONG m_ullEncodeErrors;
	ULONGLONG m_ullWriteErrors;
	ULONGLONG m_ullRotations;
	ULONGLONG m_ullRetentionDeletes;
	ULONGLONG m_ullRetentionDeleteErrors;
	ULONGLONG m_ullRetentionScanErrors;
	ULONGLONG m_ullFreeSpaceProbeErrors;
	ULONGLONG m_ullRetentionPolicyErrors;
	ULONGLONG m_ullStoragePolicyDrops;
	ULONGLONG m_ullStorageFaultDrops;
	ULONGLONG m_ullRetentionLimitDrops;
	ULONGLONG m_ullAggregateLimitDrops;
	ULONGLONG m_ullMinimumFreeSpaceDrops;
	ULONGLONG m_ullWriterFaultDrops;
	size_t m_uRetainedSegmentCount;
	ULONGLONG m_ullRetainedBytes;
	ULONGLONG m_ullAvailableBytes;
	bool m_bAvailableBytesKnown;
	bool m_bStorageAuthorityFaulted;
	bool m_bStoragePolicySatisfied;
	ULONGLONG m_ullMaxSegmentBytes;
	ULONGLONG m_ullSegmentStartedTick;
	ULONGLONG m_ullLastDiskFlushTick;
	ULONGLONG m_ullLastHealthWriteTick;
	CString m_strLogDirectory;
	CString m_strRunId;
	CString m_strActiveFileName;
	CString m_strActiveFilePath;
	CString m_strHealthFilePath;
	CString m_strInitializationFailure;
	CString m_strStorageBlockReason;
	std::vector<SDiagEventStreamCounter> m_aStreamCounters;
	CCriticalSection m_lock;
};

CDiagEventV2Writer g_diagEventV2Writer;
}

bool InitializeDiagEventV2Writer(LPCTSTR pszLogDirectory, UINT uConfiguredSegmentBytes)
{
	return g_diagEventV2Writer.Initialize(pszLogDirectory, uConfiguredSegmentBytes);
}

bool IsDiagEventV2WriterEnabled()
{
	return g_diagEventV2Writer.IsConfigured();
}

CString BuildDiagEventV2WriterHealthJson()
{
	return g_diagEventV2Writer.BuildHealthJson();
}

void WriteDiagEventV2(
	LPCTSTR pszFamily,
	LPCTSTR pszEvent,
	LPCTSTR pszSeverity,
	const CString &rstrKeysJson,
	const CString &rstrBodyJson)
{
	g_diagEventV2Writer.WriteEvent(
		pszFamily,
		pszEvent,
		pszSeverity,
		rstrKeysJson,
		rstrBodyJson);
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

#if EMULEBB_HAS_DIAG_EVENT_V2 && EMULEBB_HAS_KAD_DIAGNOSTICS
namespace
{
// Upper-case hex. WHY: the kad_udp wire-identity compare uses UPPER hex on both
// clients (rust udp_packet_v1 emits UPPER), so case must match.
CString BuildDiagEventUpperHexString(const BYTE *pPayload, UINT uPayloadLen)
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

// Resolve a Kad UDP opcode to the SAME name string the rust client emits in its
// udp_packet_v1 / kad_udp opcodeName field (emulebb-kad-net packet_info.rs
// opcode_name), so the converged kad_udp/packet diff correlates by opcodeName.
// NOTE: rust's names are not always the raw Opcodes.h macro name — e.g. 0x58 is
// KADEMLIA2_FIREWALLED_RES (rust), 0x53 KADEMLIA2_FIREWALLED2_REQ — so this table
// mirrors the rust strings verbatim, not the local #define identifiers. Unknown
// opcodes return "UNKNOWN" (rust's fallthrough), never raw hex.
LPCTSTR DiagEventKadOpcodeName(BYTE byOpcode)
{
	switch (byOpcode) {
	case KADEMLIA2_BOOTSTRAP_REQ:		return _T("KADEMLIA2_BOOTSTRAP_REQ");
	case KADEMLIA2_BOOTSTRAP_RES:		return _T("KADEMLIA2_BOOTSTRAP_RES");
	case KADEMLIA_HELLO_REQ_DEPRECATED:	return _T("KADEMLIA_HELLO_REQ_DEPRECATED");
	case KADEMLIA2_HELLO_REQ:		return _T("KADEMLIA2_HELLO_REQ");
	case KADEMLIA_HELLO_RES_DEPRECATED:	return _T("KADEMLIA_HELLO_RES_DEPRECATED");
	case KADEMLIA2_HELLO_RES:		return _T("KADEMLIA2_HELLO_RES");
	case KADEMLIA_REQ_DEPRECATED:		return _T("KADEMLIA_REQ_DEPRECATED");
	case KADEMLIA2_HELLO_RES_ACK:		return _T("KADEMLIA2_HELLO_RES_ACK");
	case KADEMLIA2_REQ:			return _T("KADEMLIA2_REQ");
	case KADEMLIA_RES_DEPRECATED:		return _T("KADEMLIA_RES_DEPRECATED");
	case KADEMLIA2_RES:			return _T("KADEMLIA2_RES");
	case KADEMLIA_SEARCH_REQ:		return _T("KADEMLIA_SEARCH_REQ");
	case KADEMLIA_SEARCH_NOTES_REQ:		return _T("KADEMLIA_SEARCH_NOTES_REQ");
	case KADEMLIA2_SEARCH_KEY_REQ:		return _T("KADEMLIA2_SEARCH_KEY_REQ");
	case KADEMLIA2_SEARCH_SOURCE_REQ:	return _T("KADEMLIA2_SEARCH_SOURCE_REQ");
	case KADEMLIA2_SEARCH_NOTES_REQ:	return _T("KADEMLIA2_SEARCH_NOTES_REQ");
	case KADEMLIA_SEARCH_RES:		return _T("KADEMLIA_SEARCH_RES");
	case KADEMLIA_SEARCH_NOTES_RES:		return _T("KADEMLIA_SEARCH_NOTES_RES");
	case KADEMLIA2_SEARCH_RES:		return _T("KADEMLIA2_SEARCH_RES");
	case KADEMLIA_PUBLISH_REQ:		return _T("KADEMLIA_PUBLISH_REQ");
	case KADEMLIA2_PUBLISH_KEY_REQ:		return _T("KADEMLIA2_PUBLISH_KEY_REQ");
	case KADEMLIA2_PUBLISH_SOURCE_REQ:	return _T("KADEMLIA2_PUBLISH_SOURCE_REQ");
	case KADEMLIA2_PUBLISH_NOTES_REQ:	return _T("KADEMLIA2_PUBLISH_NOTES_REQ");
	case KADEMLIA_PUBLISH_RES:		return _T("KADEMLIA_PUBLISH_RES");
	case KADEMLIA2_PUBLISH_RES:		return _T("KADEMLIA2_PUBLISH_RES");
	case KADEMLIA2_PUBLISH_RES_ACK:		return _T("KADEMLIA2_PUBLISH_RES_ACK");
	case KADEMLIA_FIREWALLED_REQ:		return _T("KADEMLIA_FIREWALLED_REQ");
	case KADEMLIA_FIREWALLED2_REQ:		return _T("KADEMLIA2_FIREWALLED2_REQ");
	case KADEMLIA_FIREWALLED_RES:		return _T("KADEMLIA2_FIREWALLED_RES");
	case KADEMLIA_FIREWALLED_ACK_RES:	return _T("KADEMLIA2_FIREWALLED_ACK_RES");
	case KADEMLIA2_FIREWALLUDP:		return _T("KADEMLIA2_FIREWALLUDP");
	case KADEMLIA_FINDBUDDY_REQ:		return _T("KADEMLIA_FINDBUDDY_REQ");
	case KADEMLIA_FINDBUDDY_RES:		return _T("KADEMLIA_FINDBUDDY_RES");
	case KADEMLIA_CALLBACK_REQ:		return _T("KADEMLIA_CALLBACK_REQ");
	case KADEMLIA2_PING:			return _T("KADEMLIA2_PING");
	case KADEMLIA2_PONG:			return _T("KADEMLIA2_PONG");
	default:				return _T("UNKNOWN");
	}
}
}

void DiagEventLogKadUdpPacket(
	LPCTSTR pszDirection,
	uint32 uHostIP,
	uint16 uUDPPort,
	const BYTE *pDecoded,
	UINT uDecodedLen)
{
	if (!IsDiagEventV2WriterEnabled() || pDecoded == NULL || uDecodedLen < 2)
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
		_T("{\"direction\":%s,\"protocolMarker\":%u,\"opcode\":%u,\"opcodeName\":%s,\"decodedLen\":%u,\"decodedHex\":\"%s\"}"),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszDirection != NULL ? pszDirection : _T("recv")),
		static_cast<UINT>(byProtocol),
		static_cast<UINT>(byOpcode),
		(LPCTSTR)BuildDiagnosticsJsonStringField(DiagEventKadOpcodeName(byOpcode)),
		uDecodedLen,
		(LPCTSTR)strDecodedHex);

	WriteDiagEventV2(_T("kad_udp"), _T("packet"), _T("info"), strKeys, strBody);
}

void DiagEventLogKadUdpPacketSend(
	uint32 uHostIP,
	uint16 uUDPPort,
	BYTE byProtocol,
	BYTE byOpcode,
	const BYTE *pPayload,
	UINT uPayloadLen)
{
	if (!IsDiagEventV2WriterEnabled())
		return;

	// Rebuild the decoded [marker][opcode][payload] buffer the recv emit consumes so
	// both directions share the same decodedHex/decodedLen identity.
	const UINT uTrueDecodedLen = 2 + uPayloadLen;
	std::vector<BYTE> aDecoded(uTrueDecodedLen);
	aDecoded[0] = byProtocol;
	aDecoded[1] = byOpcode;
	if (uPayloadLen > 0 && pPayload != NULL)
		memcpy(aDecoded.data() + 2, pPayload, uPayloadLen);

	const BYTE byProtocolMarker = aDecoded[0];
	const BYTE byOpcodeMarker = aDecoded[1];
	const CString strDecodedHex = BuildDiagEventUpperHexString(aDecoded.data(), uTrueDecodedLen);

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
		_T("{\"direction\":%s,\"protocolMarker\":%u,\"opcode\":%u,\"opcodeName\":%s,\"decodedLen\":%u,\"decodedHex\":\"%s\"}"),
		(LPCTSTR)BuildDiagnosticsJsonStringField(_T("send")),
		static_cast<UINT>(byProtocolMarker),
		static_cast<UINT>(byOpcodeMarker),
		(LPCTSTR)BuildDiagnosticsJsonStringField(DiagEventKadOpcodeName(byOpcodeMarker)),
		uTrueDecodedLen,
		(LPCTSTR)strDecodedHex);

	WriteDiagEventV2(_T("kad_udp"), _T("packet"), _T("info"), strKeys, strBody);
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

#if EMULEBB_HAS_DIAG_EVENT_V2 && defined(EMULEBB_ENABLE_UPLOAD_SLOT_DIAGNOSTICS)
void DiagEventLogUploadCapacitySnapshot(
	UINT uBaseSlots,
	UINT uElasticSlots,
	UINT uEffectiveSlotCap,
	UINT uActiveSlots,
	UINT uWaitingSessions)
{
	if (!IsDiagEventV2WriterEnabled())
		return;

	CString strBody;
	strBody.Format(
		_T("{\"baseSlots\":%u,\"elasticSlots\":%u,\"effectiveSlotCap\":%u,\"activeSlots\":%u,\"waitingSessions\":%u}"),
		uBaseSlots,
		uElasticSlots,
		uEffectiveSlotCap,
		uActiveSlots,
		uWaitingSessions);
	WriteDiagEventV2(_T("sched"), _T("capacity_snapshot"), _T("info"), CString(_T("{}")), strBody);
}
#endif

#if EMULEBB_HAS_DIAG_EVENT_V2 && defined(EMULEBB_ENABLE_DOWNLOAD_SLOT_DIAGNOSTICS)
void DiagEventLogDownloadSourceCount(
	UINT uSourceCount,
	UINT uValidSourceCount,
	UINT uNnpSourceCount,
	UINT uA4afFileCount,
	UINT uTransferringSourceCount)
{
	if (!IsDiagEventV2WriterEnabled())
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
	WriteDiagEventV2(_T("sched"), _T("source_count"), _T("info"), CString(_T("{}")), strBody);
}

void DiagEventLogUdpReaskSent(UINT uReaskCount)
{
	if (!IsDiagEventV2WriterEnabled() || uReaskCount == 0)
		return;

	// WHY: the master computes UDP file reasks as a periodic aggregate count, not
	// per-decision. Emit one reask_sent{transport:"udp"} carrying the count so the
	// harness sees the same transport+outcome shape rust emits per send.
	CString strBody;
	strBody.Format(
		_T("{\"outcome\":\"sent\",\"transport\":\"udp\",\"reaskCount\":%u}"),
		uReaskCount);
	WriteDiagEventV2(_T("sched"), _T("reask_sent"), _T("info"), CString(_T("{}")), strBody);
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

// Resolve a client<->server TCP/UDP opcode to the SAME name string the rust
// client emits in its ed2k-server dump opcode_name field (ed2k_server
// diagnostics.rs server_opcode_name). The c2c DbgGetClientTCPOpcode table does
// not carry the server opcodes (0x14/0x15/0x16/0x34/0x40/...), so the server flow
// fell through to a raw "0x%02x" while rust names them; this table closes that
// gap. Unknown opcodes return "UNKNOWN" (rust's fallthrough), matching rust
// exactly rather than raw hex.
static LPCTSTR DiagEventServerTCPOpcodeName(BYTE byOpcode)
{
	switch (byOpcode) {
	case OP_LOGINREQUEST:		return _T("OP_LOGINREQUEST");
	case OP_GETSERVERLIST:		return _T("OP_GETSERVERLIST");
	case OP_OFFERFILES:		return _T("OP_OFFERFILES");
	case OP_SEARCHREQUEST:		return _T("OP_SEARCHREQUEST");
	case OP_GETSOURCES:		return _T("OP_GETSOURCES");
	case OP_GETSOURCES_OBFU:	return _T("OP_GETSOURCES_OBFU");
	case OP_QUERY_MORE_RESULT:	return _T("OP_QUERY_MORE_RESULT");
	case OP_SERVERLIST:		return _T("OP_SERVERLIST");
	case OP_SEARCHRESULT:		return _T("OP_SEARCHRESULT");
	case OP_SERVERSTATUS:		return _T("OP_SERVERSTATUS");
	case OP_CALLBACKREQUEST:	return _T("OP_CALLBACKREQUEST");
	case OP_CALLBACKREQUESTED:	return _T("OP_CALLBACKREQUESTED");
	case OP_CALLBACK_FAIL:		return _T("OP_CALLBACK_FAIL");
	case OP_SERVERMESSAGE:		return _T("OP_SERVERMESSAGE");
	case OP_IDCHANGE:		return _T("OP_IDCHANGE");
	case OP_SERVERIDENT:		return _T("OP_SERVERIDENT");
	case OP_FOUNDSOURCES:		return _T("OP_FOUNDSOURCES");
	case OP_FOUNDSOURCES_OBFU:	return _T("OP_FOUNDSOURCES_OBFU");
	case OP_GLOBSEARCHREQ:		return _T("OP_GLOBSEARCHREQ");
	case OP_GLOBSEARCHREQ2:		return _T("OP_GLOBSEARCHREQ2");
	case OP_GLOBSEARCHREQ3:		return _T("OP_GLOBSEARCHREQ3");
	case OP_GLOBSEARCHRES:		return _T("OP_GLOBSEARCHRES");
	case OP_GLOBGETSOURCES:		return _T("OP_GLOBGETSOURCES");
	case OP_GLOBGETSOURCES2:	return _T("OP_GLOBGETSOURCES2");
	case OP_GLOBFOUNDSOURCES:	return _T("OP_GLOBFOUNDSOURCES");
	case OP_GLOBSERVSTATREQ:	return _T("OP_GLOBSERVSTATREQ");
	case OP_GLOBSERVSTATRES:	return _T("OP_GLOBSERVSTATRES");
	case OP_SERVER_DESC_REQ:		return _T("OP_SERVER_DESC_REQ");
	case OP_SERVER_DESC_RES:		return _T("OP_SERVER_DESC_RES");
	case OP_REJECT:			return _T("OP_REJECT");
	default:			return _T("UNKNOWN");
	}
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
	const CString strFlow(pszFlow != NULL ? pszFlow : _T("unknown"));
	// Server-flow packets carry server opcodes the c2c table doesn't name; resolve
	// them against the rust server_opcode_name set so opcode_name matches 1:1.
	const CString strOpcodeName = (strFlow == _T("server"))
		? CString(DiagEventServerTCPOpcodeName(byOpcode))
		: DbgGetClientTCPOpcode(byProtocol, byOpcode);
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

static void DiagEventLogEd2kServerPacket(
	bool bUdp,
	LPCTSTR pszPeerLabel,
	LPCTSTR pszTransportMode,
	LPCTSTR pszDirection,
	uint8 byProtocol,
	uint8 byOpcode,
	const BYTE *pPayload,
	UINT uPayloadLen)
{
	if (!IsDiagEventV2WriterEnabled())
		return;

	const UINT uHeaderLen = bUdp ? 2 : 6;
	const UINT uClearFrameLen = uHeaderLen + uPayloadLen;
	CString strClearFrameHex;
	if (bUdp) {
		const BYTE aHeader[2] = { byProtocol, byOpcode };
		strClearFrameHex = BuildDiagEventLowerHexString(aHeader, _countof(aHeader));
	} else {
		const uint32 uFrameLen = uPayloadLen + 1;
		const BYTE aHeader[6] = {
			byProtocol,
			static_cast<BYTE>(uFrameLen & 0xFF),
			static_cast<BYTE>((uFrameLen >> 8) & 0xFF),
			static_cast<BYTE>((uFrameLen >> 16) & 0xFF),
			static_cast<BYTE>((uFrameLen >> 24) & 0xFF),
			byOpcode
		};
		strClearFrameHex = BuildDiagEventLowerHexString(aHeader, _countof(aHeader));
	}
	strClearFrameHex += BuildDiagEventLowerHexString(pPayload, pPayload != NULL ? uPayloadLen : 0);

	const CString strPeer(pszPeerLabel != NULL ? pszPeerLabel : _T("unknown"));
	const CString strTransportMode(pszTransportMode != NULL ? pszTransportMode : _T("unknown"));
	const bool bObfuscated = _tcsicmp(strTransportMode, _T("obfuscated")) == 0;
	const CString strProtocolName = PacketDiagnosticsProtocolName(byProtocol) != NULL
		? BuildDiagnosticsJsonStringField(PacketDiagnosticsProtocolName(byProtocol))
		: CString(_T("null"));
	const CString strOpcodeName(DiagEventServerTCPOpcodeName(byOpcode));
	const CString strPayloadHex(BuildDiagEventLowerHexString(pPayload, pPayload != NULL ? uPayloadLen : 0));

	CString strKeys;
	strKeys.Format(
		_T("{\"peer\":%s,\"opcode\":%u,\"protocolMarker\":%u}"),
		(LPCTSTR)BuildDiagnosticsJsonStringField(strPeer),
		static_cast<UINT>(byOpcode),
		static_cast<UINT>(byProtocol));

	CString strTraceKey;
	strTraceKey.Format(_T("server:%s:%s"), bUdp ? _T("udp") : _T("tcp"), (LPCTSTR)strPeer);
	CString strBody;
	strBody.Format(
		_T("{\"direction\":%s,\"outcome\":%s,\"protocolMarker\":%u,\"protocolName\":%s,\"opcode\":%u,\"opcodeName\":%s,\"clearFrameLen\":%u,\"clearFrameHex\":\"%s\",\"payloadLen\":%u,\"payloadHex\":\"%s\",\"payloadHexTruncated\":false,\"obfuscated\":%s,\"transportMode\":%s,\"flow\":\"server\",\"phase\":%s,\"role\":%s,\"traceKey\":%s,\"stateId\":%s,\"stateLabel\":%s"),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszDirection != NULL ? pszDirection : _T("unknown")),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszDirection != NULL && _tcsicmp(pszDirection, _T("send")) == 0 ? _T("send_succeeded") : _T("accepted")),
		static_cast<UINT>(byProtocol),
		(LPCTSTR)strProtocolName,
		static_cast<UINT>(byOpcode),
		(LPCTSTR)BuildDiagnosticsJsonStringField(strOpcodeName),
		uClearFrameLen,
		(LPCTSTR)strClearFrameHex,
		uPayloadLen,
		(LPCTSTR)strPayloadHex,
		bObfuscated ? _T("true") : _T("false"),
		(LPCTSTR)BuildDiagnosticsJsonStringField(strTransportMode),
		bUdp ? _T("\"udp\"") : _T("\"tcp\""),
		bUdp ? _T("\"udp\"") : _T("\"server\""),
		(LPCTSTR)BuildDiagnosticsJsonStringField(strTraceKey),
		bUdp ? _T("\"server.udp.packet\"") : _T("\"server.tcp.packet\""),
		bUdp ? _T("\"udp\"") : _T("\"tcp\""));
	if (!bObfuscated)
		strBody.AppendFormat(_T(",\"rawLen\":%u,\"rawHex\":\"%s\""), uClearFrameLen, (LPCTSTR)strClearFrameHex);
	strBody += _T("}");

	WriteDiagEventV2(bUdp ? _T("ed2k_server_udp") : _T("ed2k_tcp"), _T("packet"), _T("info"), strKeys, strBody);
}

void PacketDiagnosticsLogServerTcpPacket(
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
	DiagEventLogEd2kServerPacket(false, pszPeerLabel, pszTransportMode, pszDirection,
		byProtocol, byOpcode, pPayload, uPayloadLen);
}

void PacketDiagnosticsLogServerUdpPacket(
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
	DiagEventLogEd2kServerPacket(true, pszPeerLabel, pszTransportMode, pszDirection,
		byProtocol, byOpcode, pPayload, uPayloadLen);
}

void PacketDiagnosticsLogClientPacket(
	LPCTSTR pszPeerLabel,
	LPCTSTR pszPeerHash,
	LPCTSTR pszTransportMode,
	LPCTSTR pszDirection,
	uint8 byProtocol,
	uint8 byOpcode,
	const BYTE *pPayload,
	UINT uPayloadLen)
{
	// Flow token parity: rust tags client-to-client traffic with an origin-based
	// flow — "listener" for the accepted (inbound) connection that carries served
	// uploads. Use the same token so the upload/publish path correlates in the
	// converged ed2k_packet_v1 / ed2k_tcp diff. (A finer "native_download" split
	// for sockets WE dialed out to download is a documented follow-up; it needs
	// the connection origin threaded to this call site.)
	PacketDiagnosticsLogEd2kPacket(_T("listener"), pszPeerLabel, pszTransportMode, pszDirection,
		byProtocol, byOpcode, pPayload, uPayloadLen);
	DiagEventLogEd2kTcpPacket(_T("listener"), pszPeerLabel, pszPeerHash, pszTransportMode, pszDirection,
		byProtocol, byOpcode, pPayload, uPayloadLen);
}

void PacketDiagnosticsLogClientUdpPacket(
	LPCTSTR pszPeerLabel,
	LPCTSTR pszTransportMode,
	LPCTSTR pszDirection,
	uint8 byProtocol,
	uint8 byOpcode,
	const BYTE *pPayload,
	UINT uPayloadLen)
{
	PacketDiagnosticsLogEd2kPacket(_T("client_udp"), pszPeerLabel, pszTransportMode, pszDirection,
		byProtocol, byOpcode, pPayload, uPayloadLen);
}

namespace
{
struct SEd2kClientUdpDiagnosticState
{
	LPCTSTR pszStateId;
	LPCTSTR pszStateLabel;
	LPCTSTR pszPhase;
	LPCTSTR pszSendRole;
};

SEd2kClientUdpDiagnosticState GetEd2kClientUdpDiagnosticState(uint8 byOpcode, bool bSend)
{
	switch (byOpcode) {
	case OP_REASKFILEPING:
		return { _T("client_udp.reask"), _T("reask"), _T("reask_request"), _T("reask_request") };
	case OP_REASKACK:
	case OP_FILENOTFOUND:
	case OP_QUEUEFULL:
		return { _T("client_udp.reask"), _T("reask"), bSend ? _T("reciprocity_reply") : _T("reask_reply"), _T("reciprocity_reply") };
	case OP_REASKCALLBACKUDP:
		return { _T("client_udp.buddy_relay"), _T("buddy_relay"), _T("buddy_relay_request"), _T("buddy_relay_request") };
	case OP_DIRECTCALLBACKREQ:
		return { _T("client_udp.direct_callback"), _T("direct_callback"), _T("direct_callback_request"), _T("direct_callback_request") };
	case OP_PORTTEST:
		return { _T("client_udp.port_test"), _T("port_test"), _T("port_test"), NULL };
	default:
		return { _T("client_udp.unknown"), _T("unknown"), _T("unknown"), NULL };
	}
}

LPCTSTR GetEd2kClientUdpOpcodeName(uint8 byOpcode)
{
	switch (byOpcode) {
	case OP_REASKFILEPING: return _T("OP_REASKFILEPING");
	case OP_REASKACK: return _T("OP_REASKACK");
	case OP_FILENOTFOUND: return _T("OP_FILENOTFOUND");
	case OP_QUEUEFULL: return _T("OP_QUEUEFULL");
	case OP_REASKCALLBACKUDP: return _T("OP_REASKCALLBACKUDP");
	case OP_DIRECTCALLBACKREQ: return _T("OP_DIRECTCALLBACKREQ");
	case OP_PORTTEST: return _T("OP_PORTTEST");
	default: return _T("UNKNOWN");
	}
}

LPCTSTR GetEd2kClientUdpSeverity(LPCTSTR pszOutcome)
{
	return pszOutcome != NULL
		&& (_tcscmp(pszOutcome, _T("dropped")) == 0
			|| _tcscmp(pszOutcome, _T("send_failed")) == 0
			|| _tcscmp(pszOutcome, _T("send_cancelled")) == 0)
		? _T("warning") : _T("info");
}
}

void DiagEventLogEd2kClientUdpPacket(
	LPCTSTR pszPeerLabel,
	LPCTSTR pszTransportMode,
	LPCTSTR pszDirection,
	LPCTSTR pszOutcome,
	uint8 byProtocol,
	uint8 byOpcode,
	const BYTE *pRaw,
	UINT uRawLen,
	const BYTE *pPayload,
	UINT uPayloadLen,
	ULONGLONG ullSendAttemptId,
	LPCTSTR pszNote)
{
	if (!IsDiagEventV2WriterEnabled())
		return;

	const bool bSend = pszDirection != NULL && _tcscmp(pszDirection, _T("send")) == 0;
	const CString strPeer(pszPeerLabel != NULL ? pszPeerLabel : _T("unknown"));
	CString strTraceKey(_T("client_udp:"));
	strTraceKey += strPeer;
	const CString strRawHex = BuildDiagEventLowerHexString(pRaw, pRaw != NULL ? uRawLen : 0);
	const CString strPayloadHex = BuildDiagEventLowerHexString(pPayload, pPayload != NULL ? uPayloadLen : 0);
	const SEd2kClientUdpDiagnosticState state = GetEd2kClientUdpDiagnosticState(byOpcode, bSend);

	CString strKeys;
	strKeys.Format(
		_T("{\"peer\":%s,\"opcode\":%u,\"protocolMarker\":%u"),
		(LPCTSTR)BuildDiagnosticsJsonStringField(strPeer),
		static_cast<UINT>(byOpcode),
		static_cast<UINT>(byProtocol));
	if (ullSendAttemptId != 0)
		strKeys.AppendFormat(_T(",\"sendAttemptId\":%I64u"), ullSendAttemptId);
	strKeys += _T("}");

	CString strBody;
	strBody.Format(
		_T("{\"direction\":%s,\"outcome\":%s"),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszDirection != NULL ? pszDirection : _T("unknown")),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszOutcome != NULL ? pszOutcome : _T("unknown")));
	if (ullSendAttemptId != 0)
		strBody.AppendFormat(_T(",\"sendAttemptId\":%I64u"), ullSendAttemptId);
	if (bSend && state.pszSendRole != NULL)
		strBody.AppendFormat(_T(",\"role\":%s"), (LPCTSTR)BuildDiagnosticsJsonStringField(state.pszSendRole));
	strBody.AppendFormat(
		_T(",\"stateId\":%s,\"stateLabel\":%s,\"traceKey\":%s,\"flow\":\"client_udp\",\"phase\":%s,\"transportMode\":%s"),
		(LPCTSTR)BuildDiagnosticsJsonStringField(state.pszStateId),
		(LPCTSTR)BuildDiagnosticsJsonStringField(state.pszStateLabel),
		(LPCTSTR)BuildDiagnosticsJsonStringField(strTraceKey),
		(LPCTSTR)BuildDiagnosticsJsonStringField(state.pszPhase),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszTransportMode != NULL ? pszTransportMode : _T("unknown")));
	if (pszTransportMode != NULL && _tcscmp(pszTransportMode, _T("obfuscated")) == 0)
		strBody += _T(",\"obfuscated\":true");
	else if (pszTransportMode != NULL && _tcscmp(pszTransportMode, _T("plaintext")) == 0)
		strBody += _T(",\"obfuscated\":false");
	strBody.AppendFormat(
		_T(",\"protocolMarker\":%u,\"protocolName\":%s,\"opcode\":%u,\"opcodeName\":%s,\"rawLen\":%u,\"rawHex\":\"%s\""),
		static_cast<UINT>(byProtocol),
		(LPCTSTR)BuildDiagnosticsJsonStringField(byProtocol == OP_EMULEPROT ? _T("emule") : _T("unknown")),
		static_cast<UINT>(byOpcode),
		(LPCTSTR)BuildDiagnosticsJsonStringField(GetEd2kClientUdpOpcodeName(byOpcode)),
		uRawLen,
		(LPCTSTR)strRawHex);
	if (bSend)
		strBody.AppendFormat(_T(",\"attemptedLen\":%u"), uRawLen);
	strBody.AppendFormat(
		_T(",\"payloadLen\":%u,\"payloadHex\":\"%s\",\"payloadHexTruncated\":false"),
		uPayloadLen,
		(LPCTSTR)strPayloadHex);
	if (pszNote != NULL && pszNote[0] != _T('\0'))
		strBody.AppendFormat(_T(",\"note\":%s"), (LPCTSTR)BuildDiagnosticsJsonStringField(pszNote));
	strBody += _T("}");

	WriteDiagEventV2(_T("ed2k_client_udp"), _T("packet"), GetEd2kClientUdpSeverity(pszOutcome), strKeys, strBody);
}

void DiagEventLogEd2kClientUdpSendOutcome(
	LPCTSTR pszPeerLabel,
	uint8 byProtocol,
	uint8 byOpcode,
	ULONGLONG ullSendAttemptId,
	LPCTSTR pszOutcome,
	bool bDeliveryKnown,
	UINT uAttemptedLen,
	LPCTSTR pszNote)
{
	if (!IsDiagEventV2WriterEnabled())
		return;

	const CString strPeer(pszPeerLabel != NULL ? pszPeerLabel : _T("unknown"));
	CString strTraceKey(_T("client_udp:"));
	strTraceKey += strPeer;
	const SEd2kClientUdpDiagnosticState state = GetEd2kClientUdpDiagnosticState(byOpcode, true);

	CString strKeys;
	strKeys.Format(
		_T("{\"peer\":%s,\"opcode\":%u,\"protocolMarker\":%u,\"sendAttemptId\":%I64u}"),
		(LPCTSTR)BuildDiagnosticsJsonStringField(strPeer),
		static_cast<UINT>(byOpcode),
		static_cast<UINT>(byProtocol),
		ullSendAttemptId);

	CString strBody;
	strBody.Format(
		_T("{\"direction\":\"send\",\"outcome\":%s,\"deliveryKnown\":%s,\"sendAttemptId\":%I64u"),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszOutcome != NULL ? pszOutcome : _T("unknown")),
		bDeliveryKnown ? _T("true") : _T("false"),
		ullSendAttemptId);
	if (state.pszSendRole != NULL)
		strBody.AppendFormat(_T(",\"role\":%s"), (LPCTSTR)BuildDiagnosticsJsonStringField(state.pszSendRole));
	strBody.AppendFormat(
		_T(",\"stateId\":%s,\"stateLabel\":%s,\"traceKey\":%s,\"flow\":\"client_udp\",\"phase\":%s,\"protocolMarker\":%u,\"protocolName\":%s,\"opcode\":%u,\"opcodeName\":%s,\"attemptedLen\":%u"),
		(LPCTSTR)BuildDiagnosticsJsonStringField(state.pszStateId),
		(LPCTSTR)BuildDiagnosticsJsonStringField(state.pszStateLabel),
		(LPCTSTR)BuildDiagnosticsJsonStringField(strTraceKey),
		(LPCTSTR)BuildDiagnosticsJsonStringField(state.pszPhase),
		static_cast<UINT>(byProtocol),
		(LPCTSTR)BuildDiagnosticsJsonStringField(byProtocol == OP_EMULEPROT ? _T("emule") : _T("unknown")),
		static_cast<UINT>(byOpcode),
		(LPCTSTR)BuildDiagnosticsJsonStringField(GetEd2kClientUdpOpcodeName(byOpcode)),
		uAttemptedLen);
	if (pszNote != NULL && pszNote[0] != _T('\0'))
		strBody.AppendFormat(_T(",\"note\":%s"), (LPCTSTR)BuildDiagnosticsJsonStringField(pszNote));
	strBody += _T("}");

	WriteDiagEventV2(_T("ed2k_client_udp"), _T("send_outcome"), GetEd2kClientUdpSeverity(pszOutcome), strKeys, strBody);
}

static int Ed2kTcpDiagnosticFileHashOffset(uint8 byProtocol, uint8 byOpcode)
{
	if (byProtocol == OP_EDONKEYPROT) {
		switch (byOpcode) {
		case OP_END_OF_DOWNLOAD:
		case OP_FILEREQANSNOFIL:
		case OP_FILESTATUS:
		case OP_HASHSETANSWER:
		case OP_HASHSETREQUEST:
		case OP_REQFILENAMEANSWER:
		case OP_REQUESTFILENAME:
		case OP_REQUESTPARTS:
		case OP_SENDINGPART:
		case OP_SETREQFILEID:
		case OP_STARTUPLOADREQ:
			return 0;
		default:
			return -1;
		}
	}
	if (byProtocol != OP_EMULEPROT)
		return -1;

	switch (byOpcode) {
	case OP_AICHANSWER:
	case OP_AICHFILEHASHANS:
	case OP_AICHFILEHASHREQ:
	case OP_AICHREQUEST:
	case OP_ANSWERSOURCES:
	case OP_COMPRESSEDPART:
	case OP_COMPRESSEDPART_I64:
	case OP_MULTIPACKET:
	case OP_MULTIPACKETANSWER:
	case OP_MULTIPACKET_EXT:
	case OP_REQUESTSOURCES:
	case OP_SENDINGPART_I64:
		return 0;
	case OP_ANSWERSOURCES2:
	case OP_HASHSETANSWER2:
	case OP_HASHSETREQUEST2:
	case OP_MULTIPACKETANSWER_EXT2:
	case OP_MULTIPACKET_EXT2:
		return 1;
	case OP_REQUESTSOURCES2:
		return 3;
	default:
		return -1;
	}
}

static CString Ed2kTcpDiagnosticFileHash(
	uint8 byProtocol,
	uint8 byOpcode,
	const BYTE *pPayload,
	UINT uPayloadLen)
{
	const int nOffset = Ed2kTcpDiagnosticFileHashOffset(byProtocol, byOpcode);
	if (nOffset < 0 || pPayload == NULL || uPayloadLen < static_cast<UINT>(nOffset) + 16u)
		return CString();
	return BuildDiagEventLowerHexString(pPayload + nOffset, 16u);
}

void DiagEventLogEd2kTcpPacket(
	LPCTSTR pszFlow,
	LPCTSTR pszPeerLabel,
	LPCTSTR pszPeerHash,
	LPCTSTR pszTransportMode,
	LPCTSTR pszDirection,
	uint8 byProtocol,
	uint8 byOpcode,
	const BYTE *pPayload,
	UINT uPayloadLen)
{
	if (!IsDiagEventV2WriterEnabled())
		return;

	const UINT uPayloadHexLen = pPayload != NULL ? uPayloadLen : 0;
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
	CString strClearFrameHex(BuildDiagEventLowerHexString(aHeader, 6));
	const UINT uClearFrameHexPayloadLen = pPayload != NULL ? uPayloadLen : 0;
	strClearFrameHex += BuildDiagEventLowerHexString(pPayload, uClearFrameHexPayloadLen);

	const LPCTSTR pszProtocolName = PacketDiagnosticsProtocolName(byProtocol);
	const CString strProtocolNameField = pszProtocolName != NULL ? BuildDiagnosticsJsonStringField(pszProtocolName) : CString(_T("null"));
	const CString strOpcodeName = DbgGetClientTCPOpcode(byProtocol, byOpcode);
	const CString strPeer(pszPeerLabel != NULL ? pszPeerLabel : _T("unknown"));
	const CString strFileHash(Ed2kTcpDiagnosticFileHash(byProtocol, byOpcode, pPayload, uPayloadLen));
	const CString strFlow(pszFlow != NULL ? pszFlow : _T("unknown"));
	const bool bObfuscated = pszTransportMode != NULL && _tcsicmp(pszTransportMode, _T("obfuscated")) == 0;

	CString strKeys;
	strKeys.Format(
		_T("{\"peer\":%s"),
		(LPCTSTR)BuildDiagnosticsJsonStringField(strPeer));
	if (pszPeerHash != NULL && pszPeerHash[0] != _T('\0'))
		strKeys.AppendFormat(_T(",\"peerHash\":%s"), (LPCTSTR)BuildDiagnosticsJsonStringField(pszPeerHash));
	if (!strFileHash.IsEmpty())
		strKeys.AppendFormat(_T(",\"fileHash\":%s"), (LPCTSTR)BuildDiagnosticsJsonStringField(strFileHash));
	strKeys.AppendFormat(
		_T(",\"opcode\":%u,\"protocolMarker\":%u}"),
		static_cast<UINT>(byOpcode),
		static_cast<UINT>(byProtocol));

	CString strTraceKey;
	strTraceKey.Format(_T("%s:%s"), (LPCTSTR)strFlow, (LPCTSTR)strPeer);
	CString strStateId;
	strStateId.Format(_T("%s.session"), (LPCTSTR)strFlow);

	CString strBody;
	strBody.Format(
		_T("{\"direction\":%s,\"outcome\":%s,\"protocolMarker\":%u,\"protocolName\":%s,\"opcode\":%u,\"opcodeName\":%s,\"clearFrameLen\":%u,\"clearFrameHex\":\"%s\",\"payloadLen\":%u,\"payloadHex\":\"%s\",\"payloadHexTruncated\":false,\"obfuscated\":%s,\"transportMode\":%s,\"flow\":%s,\"phase\":\"session\",\"traceKey\":%s,\"stateId\":%s,\"stateLabel\":\"session\""),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszDirection != NULL ? pszDirection : _T("unknown")),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszDirection != NULL && _tcsicmp(pszDirection, _T("send")) == 0 ? _T("send_succeeded") : _T("accepted")),
		static_cast<UINT>(byProtocol),
		(LPCTSTR)strProtocolNameField,
		static_cast<UINT>(byOpcode),
		(LPCTSTR)BuildDiagnosticsJsonStringField(strOpcodeName),
		uRawLen,
		(LPCTSTR)strClearFrameHex,
		uPayloadLen,
		(LPCTSTR)strPayloadHex,
		bObfuscated ? _T("true") : _T("false"),
		(LPCTSTR)BuildDiagnosticsJsonStringField(pszTransportMode != NULL ? pszTransportMode : _T("unknown")),
		(LPCTSTR)BuildDiagnosticsJsonStringField(strFlow),
		(LPCTSTR)BuildDiagnosticsJsonStringField(strTraceKey),
		(LPCTSTR)BuildDiagnosticsJsonStringField(strStateId));
	if (!bObfuscated)
		strBody.AppendFormat(_T(",\"rawLen\":%u,\"rawHex\":\"%s\""), uRawLen, (LPCTSTR)strClearFrameHex);
	strBody += _T("}");

	WriteDiagEventV2(_T("ed2k_tcp"), _T("packet"), _T("info"), strKeys, strBody);
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
