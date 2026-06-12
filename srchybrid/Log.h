#pragma once

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
#endif

#ifdef EMULEBB_ENABLE_UPLOAD_SLOT_DIAGNOSTICS
extern CLogFile theUploadSlotDiagnosticsLog;
void UploadSlotDiagnosticsLogLine(LPCTSTR pszFmt, ...);
#endif

#ifdef EMULEBB_ENABLE_DOWNLOAD_SLOT_DIAGNOSTICS
extern CLogFile theDownloadSlotDiagnosticsLog;
void DownloadSlotDiagnosticsLogLine(LPCTSTR pszFmt, ...);
#endif
