#pragma once

#include "BuildFeatures.h"
#include "types.h"
#include "kademlia/routing/Maps.h"

#include <atlstr.h>
#include <windows.h>

namespace Kademlia
{
	class CContact;
	class CRoutingZone;
}

namespace KadDiagnosticsSeams
{
constexpr LPCTSTR kBinaryMarker = _T("KadDiagnostics:");

#if EMULEBB_HAS_KAD_DIAGNOSTICS
/**
 * Initializes the compile-gated Kad JSONL artifact.
 */
void InitializeLog(LPCTSTR pszLogPath, UINT uMaxLogFileSize);
bool IsEnabled();

/**
 * Writes a throttled routing-table health snapshot.
 */
void LogRoutingSummary(
	const Kademlia::CRoutingZone *pRoutingZone,
	const Kademlia::_ContactList &rBootstrapContacts,
	bool bConnected,
	bool bBootstrapping,
	bool bFirewalled,
	bool bLanMode);

/**
 * Writes a routing-contact admission/update/removal event.
 */
void LogContactEvent(
	LPCTSTR pszEvent,
	LPCTSTR pszSeverity,
	const Kademlia::CContact *pContact,
	LPCTSTR pszAction,
	LPCTSTR pszReason,
	LPCTSTR pszEvidenceJson = NULL);

/**
 * Writes a routing-contact event when a CContact was not materialized.
 */
void LogRawContactEvent(
	LPCTSTR pszEvent,
	LPCTSTR pszSeverity,
	uint32 uHostIP,
	uint16 uUDPPort,
	uint16 uTCPPort,
	uint8 uVersion,
	LPCTSTR pszAction,
	LPCTSTR pszReason,
	LPCTSTR pszEvidenceJson = NULL);

/**
 * Writes a Kad packet flood/protocol-health event.
 */
void LogPacketEvent(
	LPCTSTR pszEvent,
	LPCTSTR pszSeverity,
	uint32 uNetworkIP,
	uint8 byOpcode,
	uint8 byOriginalOpcode,
	LPCTSTR pszAction,
	LPCTSTR pszReason,
	LONGLONG llTokens);

/**
 * Writes Kad search response quality events.
 */
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
	LPCTSTR pszReason);
#else
inline void InitializeLog(LPCTSTR, UINT) {}
inline bool IsEnabled() { return false; }
inline void LogRoutingSummary(const Kademlia::CRoutingZone *, const Kademlia::_ContactList &, bool, bool, bool, bool) {}
inline void LogContactEvent(LPCTSTR, LPCTSTR, const Kademlia::CContact *, LPCTSTR, LPCTSTR, LPCTSTR = NULL) {}
inline void LogRawContactEvent(LPCTSTR, LPCTSTR, uint32, uint16, uint16, uint8, LPCTSTR, LPCTSTR, LPCTSTR = NULL) {}
inline void LogPacketEvent(LPCTSTR, LPCTSTR, uint32, uint8, uint8, LPCTSTR, LPCTSTR, LONGLONG) {}
inline void LogSearchResponseEvent(LPCTSTR, LPCTSTR, uint32, uint32, uint32, uint16, uint8, UINT, UINT, LPCTSTR, LPCTSTR) {}
#endif
}

#if EMULEBB_HAS_KAD_DIAGNOSTICS
#define EMULEBB_KAD_LOG_ROUTING_SUMMARY(...) KadDiagnosticsSeams::LogRoutingSummary(__VA_ARGS__)
#define EMULEBB_KAD_LOG_CONTACT_EVENT(...) KadDiagnosticsSeams::LogContactEvent(__VA_ARGS__)
#define EMULEBB_KAD_LOG_RAW_CONTACT_EVENT(...) KadDiagnosticsSeams::LogRawContactEvent(__VA_ARGS__)
#define EMULEBB_KAD_LOG_PACKET_EVENT(...) KadDiagnosticsSeams::LogPacketEvent(__VA_ARGS__)
#define EMULEBB_KAD_LOG_SEARCH_RESPONSE_EVENT(...) KadDiagnosticsSeams::LogSearchResponseEvent(__VA_ARGS__)
#else
#define EMULEBB_KAD_LOG_ROUTING_SUMMARY(...) ((void)0)
#define EMULEBB_KAD_LOG_CONTACT_EVENT(...) ((void)0)
#define EMULEBB_KAD_LOG_RAW_CONTACT_EVENT(...) ((void)0)
#define EMULEBB_KAD_LOG_PACKET_EVENT(...) ((void)0)
#define EMULEBB_KAD_LOG_SEARCH_RESPONSE_EVENT(...) ((void)0)
#endif
