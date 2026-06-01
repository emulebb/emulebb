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

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
constexpr int kMaxFileLogLineChars = 64 * 1024;
constexpr size_t kMaxRecentLogEntries = 200;
CCriticalSection g_recentLogLock;
std::deque<SRecentLogEntry> g_recentLogEntries;
#ifdef EMULEBB_ENABLE_PACKET_DIAGNOSTICS
CCriticalSection g_packetDiagnosticsLogLock;
volatile LONGLONG g_llPacketDiagnosticsEventSeq = 0;
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
	CSingleLock lock(&g_recentLogLock, TRUE);
	g_recentLogEntries.push_back(SRecentLogEntry{CTime::GetCurrentTime(), uFlags, pszText});
	while (g_recentLogEntries.size() > kMaxRecentLogEntries)
		g_recentLogEntries.pop_front();
}

#ifdef EMULEBB_ENABLE_PACKET_DIAGNOSTICS
CString BuildPacketDiagnosticsTimestampUtc()
{
	SYSTEMTIME st = {};
	::GetSystemTime(&st);
	CString strTimestamp;
	strTimestamp.Format(_T("%04u-%02u-%02uT%02u:%02u:%02u.%03uZ"), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	return strTimestamp;
}

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

CString EscapePacketDiagnosticsJson(const CString &strValue)
{
	CString strEscaped;
	for (int i = 0; i < strValue.GetLength(); ++i) {
		const TCHAR ch = strValue[i];
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

CString BuildPacketDiagnosticsJsonStringField(LPCTSTR pszValue)
{
	CString strField(_T("\""));
	strField += EscapePacketDiagnosticsJson(pszValue != NULL ? CString(pszValue) : CString());
	strField += _T("\"");
	return strField;
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
	const CString strProtocol = pszProtocolName != NULL ? BuildPacketDiagnosticsJsonStringField(pszProtocolName) : CString(_T("null"));
	const CString strOuterOpcodeName = DbgGetClientTCPOpcode(byProtocol, byOuterOpcode);
	const CString strPayloadHex = BuildPacketDiagnosticsHexString(pPayload, uPayloadLen);
	const CString strContextHex = BuildPacketDiagnosticsHexString(pPayload != NULL ? pPayload + uContextStart : NULL, uContextLen);
	const ULONGLONG ullEventSeq = static_cast<ULONGLONG>(::InterlockedIncrement64(&g_llPacketDiagnosticsEventSeq));

	CString strJson;
	strJson.Format(
		_T("{\"schema\":\"ed2k_invalid_sub_opcode_v1\",\"source\":\"emulebb\",\"ts_utc\":\"%s\",\"event_seq\":%I64u,\"packet_family\":\"%s\",\"remote_addr\":\"%s\",\"transport_mode\":\"%s\",\"protocol\":%s,\"protocol_marker\":%u,\"outer_opcode\":%u,\"outer_opcode_name\":\"%s\",\"invalid_sub_opcode\":%u,\"previous_sub_opcode\":%s,\"payload_len\":%u,\"invalid_offset\":%I64u,\"bytes_remaining\":%I64u,\"context_offset\":%u,\"context_len\":%u,\"context_hex\":\"%s\",\"payload_hex\":\"%s\"}\r\n"),
		(LPCTSTR)EscapePacketDiagnosticsJson(BuildPacketDiagnosticsTimestampUtc()),
		ullEventSeq,
		(LPCTSTR)EscapePacketDiagnosticsJson(pszPacketFamily != NULL ? CString(pszPacketFamily) : CString(_T("unknown"))),
		(LPCTSTR)EscapePacketDiagnosticsJson(pszPeerLabel != NULL ? CString(pszPeerLabel) : CString(_T("unknown"))),
		(LPCTSTR)EscapePacketDiagnosticsJson(pszTransportMode != NULL ? CString(pszTransportMode) : CString(_T("unknown"))),
		(LPCTSTR)strProtocol,
		static_cast<UINT>(byProtocol),
		static_cast<UINT>(byOuterOpcode),
		(LPCTSTR)EscapePacketDiagnosticsJson(strOuterOpcodeName),
		static_cast<UINT>(byInvalidSubOpcode),
		(LPCTSTR)strPreviousSubOpcode,
		uPayloadLen,
		ullInvalidOffset,
		ullBytesRemaining,
		uContextStart,
		uContextLen,
		(LPCTSTR)strContextHex,
		(LPCTSTR)strPayloadHex);

	CSingleLock lock(&g_packetDiagnosticsLogLock, TRUE);
	thePacketDiagnosticsLog.Log(strJson);
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
	else
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
