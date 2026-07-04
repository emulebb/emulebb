#pragma once

#include "BuildFeatures.h"

#include <vector>

enum EDebugLogPriority : int
{
	DLP_VERYLOW = 0,
	DLP_LOW,
	DLP_DEFAULT,
	DLP_HIGH,
	DLP_VERYHIGH
};

// Log message type enumeration
#define	LOG_INFO		0
#define	LOG_WARNING		1
#define	LOG_ERROR		2
#define	LOG_SUCCESS		3
#define	LOGMSGTYPEMASK	0x03

// Log message targets flags
#define	LOG_DEFAULT		0x00
#define	LOG_DEBUG		0x10
#define	LOG_STATUSBAR	0x20
#define	LOG_DONTNOTIFY	0x40


void Log(LPCTSTR pszFmt, ...);
void LogError(LPCTSTR pszFmt, ...);
void LogWarning(LPCTSTR pszFmt, ...);

void Log(UINT uFlags, LPCTSTR pszFmt, ...);
void LogError(UINT uFlags, LPCTSTR pszFmt, ...);
void LogWarning(UINT uFlags, LPCTSTR pszFmt, ...);

void DebugLog(LPCTSTR pszFmt, ...);
void DebugLogError(LPCTSTR pszFmt, ...);
void DebugLogWarning(LPCTSTR pszFmt, ...);

void DebugLog(UINT uFlags, LPCTSTR pszFmt, ...);
void DebugLogError(UINT uFlags, LPCTSTR pszFmt, ...);
void DebugLogWarning(UINT uFlags, LPCTSTR pszFmt, ...);

void LogV(UINT uFlags, LPCTSTR pszFmt, va_list argp);

void AddLogLine(bool bAddToStatusBar, LPCTSTR pszLine, ...);
void AddDebugLogLine(bool bAddToStatusBar, LPCTSTR pszLine, ...);
void AddDebugLogLine(EDebugLogPriority Priority, bool bAddToStatusBar, LPCTSTR pszLine, ...);

void AddLogTextV(UINT uFlags, EDebugLogPriority dlpPriority, LPCTSTR pszLine, va_list argptr);

/**
 * @brief Captures one recent log entry for the REST API surface.
 */
struct SRecentLogEntry
{
	CTime time;
	UINT uFlags;
	CString strText;
};

std::vector<SRecentLogEntry> GetRecentLogEntries(size_t maxEntries);
void ClearRecentLogEntries();


///////////////////////////////////////////////////////////////////////////////
// CLogFile

enum ELogFileFormat : uint8
{
	Unicode = 0,
	Utf8
};

class CLogFile
{
public:
	CLogFile();
	~CLogFile();

	bool IsOpen() const								{ return m_fp != NULL; }
	const CString& GetFilePath() const				{ return m_strFilePath; }
	bool SetFilePath(LPCTSTR pszFilePath);
	void SetMaxFileSize(UINT uMaxFileSize);
	bool SetFileFormat(const ELogFileFormat eFileFormat);
	bool SetFlushOnWrite(bool bFlushOnWrite);

	bool Create(LPCTSTR pszFilePath, UINT uMaxFileSize = 1024 * 1024, const ELogFileFormat eFileFormat = Unicode);
	bool Open();
	bool Close();
	bool Log(LPCTSTR pszMsg, int iLen = -1);
	bool Logf(LPCTSTR pszFmt, ...);
	bool FlushToDisk();
	void StartNewLogFile();

protected:
	FILE	*m_fp;
	time_t	m_tStarted;
	CString	m_strFilePath;
	size_t	m_uBytesWritten;
	size_t	m_uMaxFileSize;
	bool	m_bInOpenCall;
	bool	m_bFlushOnWrite;
	ELogFileFormat m_eFileFormat;
};

extern CLogFile theLog;
extern CLogFile theVerboseLog;
bool InitializeDiagnosticsLog(CLogFile &rLog, LPCTSTR pszLogPath, UINT uMaxLogFileSize);
void WriteDiagnosticsLogLine(CLogFile &rLog, CCriticalSection &rLock, const CString &rstrLine);
void WriteDiagnosticsLogLineV(CLogFile &rLog, CCriticalSection &rLock, LPCTSTR pszFmt, va_list argp);
void WriteDiagnosticsLogLineF(CLogFile &rLog, CCriticalSection &rLock, LPCTSTR pszFmt, ...);
CString BuildDiagnosticsTimestampUtc();
CString EscapeDiagnosticsJson(const CString &rstrValue);
CString BuildDiagnosticsJsonStringField(LPCTSTR pszValue);
CString NormalizeDiagnosticsJsonPayload(LPCTSTR pszJsonOrText);
ULONGLONG NextDiagnosticsEventSeq(volatile LONGLONG &rllCounter);
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
	LPCTSTR pszEvidenceJson);

#if EMULEBB_HAS_DIAG_EVENT_V1
extern CLogFile theDiagEventV1Log;

/**
 * @brief Writes one converged diag_event_v1 record (camelCase envelope) to the
 *        shared diagnostics log so the eMuleBB and emulebb-rust traces diff 1:1.
 *
 * `rstrKeysJson` and `rstrBodyJson` are caller-built JSON objects (e.g. "{...}");
 * an empty value is emitted as "{}". The envelope owns schema/client/ts/seq.
 */
void WriteDiagEventV1(
	LPCTSTR pszFamily,
	LPCTSTR pszEvent,
	LPCTSTR pszSeverity,
	const CString &rstrKeysJson,
	const CString &rstrBodyJson);

/**
 * @brief Lower-case hex of a byte buffer, capped at the shared 4 KiB hex limit.
 *        Matches the emulebb-rust ed2k_tcp lower-case payloadHex/rawHex encoding.
 */
CString BuildDiagEventLowerHexString(const BYTE *pPayload, UINT uPayloadLen);
#endif

#if EMULEBB_HAS_DIAG_EVENT_V1 && defined(EMULEBB_ENABLE_PACKET_DIAGNOSTICS)
/**
 * @brief Emits one eD2k TCP packet as a diag_event_v1 family:"ed2k_tcp" record.
 *        Re-uses the same send/recv boundary as the ed2k_packet_v1 dump.
 */
void DiagEventLogEd2kTcpPacket(
	LPCTSTR pszFlow,
	LPCTSTR pszPeerLabel,
	LPCTSTR pszTransportMode,
	LPCTSTR pszDirection,
	uint8 byProtocol,
	uint8 byOpcode,
	const BYTE *pPayload,
	UINT uPayloadLen);
#endif

#if EMULEBB_HAS_DIAG_EVENT_V1 && EMULEBB_HAS_KAD_DIAGNOSTICS
/**
 * @brief Emits one decoded Kad UDP packet as a diag_event_v1 family:"kad_udp"
 *        record. `pDecoded` is the post-deobfuscation packet beginning at the
 *        protocol marker; decodedHex is the comparable wire-identity key.
 */
void DiagEventLogKadUdpPacket(
	LPCTSTR pszDirection,
	uint32 uHostIP,
	uint16 uUDPPort,
	const BYTE *pDecoded,
	UINT uDecodedLen);

/**
 * @brief Emits one outbound Kad UDP packet as a diag_event_v1 family:"kad_udp"
 *        record (direction "send"). The send hooks own the decoded protocol marker
 *        + opcode + payload separately (before the obfuscation/encrypt layer), so
 *        this rebuilds the [marker][opcode][payload] decoded buffer the recv emit
 *        consumes, keeping the decodedHex wire-identity key identical per direction.
 */
void DiagEventLogKadUdpPacketSend(
	uint32 uHostIP,
	uint16 uUDPPort,
	BYTE byProtocol,
	BYTE byOpcode,
	const BYTE *pPayload,
	UINT uPayloadLen);
#endif

#if EMULEBB_HAS_DIAG_EVENT_V1 && defined(EMULEBB_ENABLE_UPLOAD_SLOT_DIAGNOSTICS)
/**
 * @brief Emits the diag_event_v1 sched:"capacity_snapshot" gauge (upload slots).
 *        Mirrors the rust upload_queue capacity snapshot fields.
 */
void DiagEventLogUploadCapacitySnapshot(
	UINT uBaseSlots,
	UINT uElasticSlots,
	UINT uEffectiveSlotCap,
	UINT uActiveSlots,
	UINT uWaitingSessions);
#endif

#if EMULEBB_HAS_DIAG_EVENT_V1 && defined(EMULEBB_ENABLE_DOWNLOAD_SLOT_DIAGNOSTICS)
/**
 * @brief Emits the diag_event_v1 sched:"source_count" snapshot (download sources).
 *        Maps the master download-slot summary onto the converged source fields.
 */
void DiagEventLogDownloadSourceCount(
	UINT uSourceCount,
	UINT uValidSourceCount,
	UINT uNnpSourceCount,
	UINT uA4afFileCount,
	UINT uTransferringSourceCount);

/**
 * @brief Emits a diag_event_v1 sched:"reask_sent" event for a UDP source reask.
 */
void DiagEventLogUdpReaskSent(UINT uReaskCount);
#endif

/**
 * @brief Builds one diagnostics summary log line from formatted key/value segments.
 */
class CDiagnosticsKeyValueLineBuilder
{
public:
	explicit CDiagnosticsKeyValueLineBuilder(LPCTSTR pszPrefix);

	void AppendFormat(LPCTSTR pszKeyValueFmt, ...);
	const CString& GetLine() const						{ return m_strLine; }

private:
	CString m_strLine;
};

#ifdef EMULEBB_ENABLE_PACKET_DIAGNOSTICS
extern CLogFile thePacketDiagnosticsLog;

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
	int iPreviousSubOpcode);

// Logs one ED2K *server* connection packet (TCP) in either direction. Extends the
// packet diagnostics beyond peer/client packets so server-protocol parity work can
// be diffed against a reference server. Compile-gated; no-op unless the log is open.
void PacketDiagnosticsLogServerPacket(
	LPCTSTR pszPeerLabel,
	LPCTSTR pszTransportMode,
	LPCTSTR pszDirection,
	uint8 byProtocol,
	uint8 byOpcode,
	const BYTE *pPayload,
	UINT uPayloadLen);

// Logs one ED2K client-to-client (peer) packet (TCP) in either direction. Emits the
// shared ed2k_packet_v1 schema with flow="client" so the trace diffs 1:1 against the
// emulebb-rust client-to-client packet dump. Compile-gated; no-op unless the log is open.
void PacketDiagnosticsLogClientPacket(
	LPCTSTR pszPeerLabel,
	LPCTSTR pszTransportMode,
	LPCTSTR pszDirection,
	uint8 byProtocol,
	uint8 byOpcode,
	const BYTE *pPayload,
	UINT uPayloadLen);
#endif

#ifdef EMULEBB_ENABLE_UPLOAD_SLOT_DIAGNOSTICS
extern CLogFile theUploadSlotDiagnosticsLog;
void UploadSlotDiagnosticsLogLine(LPCTSTR pszFmt, ...);
#endif

#ifdef EMULEBB_ENABLE_DOWNLOAD_SLOT_DIAGNOSTICS
extern CLogFile theDownloadSlotDiagnosticsLog;
void DownloadSlotDiagnosticsLogLine(LPCTSTR pszFmt, ...);
#endif
