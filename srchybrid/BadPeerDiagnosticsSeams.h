#pragma once

#include "BuildFeatures.h"
#include "types.h"

#include <atlstr.h>
#include <windows.h>

class CAbstractFile;
class CSearchFile;
class CUpDownClient;
struct Requested_Block_Struct;

namespace BadPeerDiagnosticsSeams
{
constexpr LPCTSTR kBinaryMarker = _T("BadPeerDiagnostics:");

#if EMULEBB_HAS_BAD_PEER_DIAGNOSTICS
/**
 * Initializes the compile-gated bad-peer JSONL artifact.
 */
void InitializeLog(LPCTSTR pszLogPath, UINT uMaxLogFileSize);
bool IsEnabled();

/**
 * Writes a peer-scoped behavior event with optional file and evidence context.
 */
void LogClientEvent(
	LPCTSTR pszEvent,
	LPCTSTR pszSeverity,
	const CUpDownClient *pClient,
	LPCTSTR pszAction,
	LPCTSTR pszReason,
	const CAbstractFile *pFile = NULL,
	LPCTSTR pszEvidenceJson = NULL);

/**
 * Writes an IP-scoped behavior event when no client object is available.
 */
void LogIpEvent(
	LPCTSTR pszEvent,
	LPCTSTR pszSeverity,
	uint32 dwIP,
	uint16 uPort,
	LPCTSTR pszAction,
	LPCTSTR pszReason,
	LPCTSTR pszEvidenceJson = NULL);

/**
 * Writes a search-result scoped behavior event for spam/fake-file evidence.
 */
void LogSearchEvent(
	LPCTSTR pszEvent,
	LPCTSTR pszSeverity,
	const CSearchFile *pSearchFile,
	LPCTSTR pszAction,
	LPCTSTR pszReason,
	LPCTSTR pszEvidenceJson = NULL);

/**
 * Writes one upload block-request event and emits a repeat observation when the
 * same peer asks for the same file range again inside the diagnostics window.
 */
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
	LONG nPendingIOBlocks);

/**
 * Tracks repeated same-file upload-slot churn. This is diagnostics-only and
 * does not alter cooldowns, scores, or ban policy.
 */
void TrackUploadFileBehavior(
	LPCTSTR pszBehavior,
	const CUpDownClient *pClient,
	const CAbstractFile *pFile,
	LPCTSTR pszReason);

/**
 * Escapes a value for embedding into a caller-built evidence JSON object.
 */
CString EvidenceJsonString(LPCTSTR pszValue);
#else
inline void InitializeLog(LPCTSTR, UINT) {}
inline bool IsEnabled() { return false; }
inline void LogClientEvent(LPCTSTR, LPCTSTR, const CUpDownClient *, LPCTSTR, LPCTSTR, const CAbstractFile * = NULL, LPCTSTR = NULL) {}
inline void LogIpEvent(LPCTSTR, LPCTSTR, uint32, uint16, LPCTSTR, LPCTSTR, LPCTSTR = NULL) {}
inline void LogSearchEvent(LPCTSTR, LPCTSTR, const CSearchFile *, LPCTSTR, LPCTSTR, LPCTSTR = NULL) {}
inline void LogUploadBlockRequestBehavior(LPCTSTR, LPCTSTR, const CUpDownClient *, const CAbstractFile *, const Requested_Block_Struct *, LPCTSTR, LPCTSTR, INT_PTR, INT_PTR, LONG) {}
inline void TrackUploadFileBehavior(LPCTSTR, const CUpDownClient *, const CAbstractFile *, LPCTSTR) {}
#endif
}

#if EMULEBB_HAS_BAD_PEER_DIAGNOSTICS
#define EMULEBB_BAD_PEER_LOG_CLIENT_EVENT(...) BadPeerDiagnosticsSeams::LogClientEvent(__VA_ARGS__)
#define EMULEBB_BAD_PEER_LOG_IP_EVENT(...) BadPeerDiagnosticsSeams::LogIpEvent(__VA_ARGS__)
#define EMULEBB_BAD_PEER_LOG_SEARCH_EVENT(...) BadPeerDiagnosticsSeams::LogSearchEvent(__VA_ARGS__)
#else
#define EMULEBB_BAD_PEER_LOG_CLIENT_EVENT(...) ((void)0)
#define EMULEBB_BAD_PEER_LOG_IP_EVENT(...) ((void)0)
#define EMULEBB_BAD_PEER_LOG_SEARCH_EVENT(...) ((void)0)
#endif
