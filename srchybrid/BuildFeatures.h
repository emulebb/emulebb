#pragma once

#if defined(EMULEBB_DISABLE_SOCKET_STATES)
#error EMULEBB_DISABLE_SOCKET_STATES is retired; socket state tracking is mandatory.
#endif

#if defined(EMULEBB_DEV_BUILD)
#error EMULEBB_DEV_BUILD is retired; use targeted diagnostics flags instead.
#endif

#if defined(EMULEBB_ENABLE_DEBUG_DEVICE)
#error EMULEBB_ENABLE_DEBUG_DEVICE is retired; debug-only UI must stay behind _DEBUG.
#endif

#if defined(EMULEBB_DEBUG_EMSOCKET)
#error EMULEBB_DEBUG_EMSOCKET is retired; use packet diagnostics or verbose logging.
#endif

#if defined(EMULEBB_ENABLE_STARTUP_DIAGNOSTICS)
#define EMULEBB_HAS_STARTUP_DIAGNOSTICS 1
#else
#define EMULEBB_HAS_STARTUP_DIAGNOSTICS 0
#endif

#if defined(EMULEBB_ENABLE_BAD_PEER_DIAGNOSTICS)
#define EMULEBB_HAS_BAD_PEER_DIAGNOSTICS 1
#else
#define EMULEBB_HAS_BAD_PEER_DIAGNOSTICS 0
#endif

#if defined(EMULEBB_ENABLE_KAD_DIAGNOSTICS)
#define EMULEBB_HAS_KAD_DIAGNOSTICS 1
#else
#define EMULEBB_HAS_KAD_DIAGNOSTICS 0
#endif

// The converged diag_event_v1 writer and its shared log file exist only when at
// least one diagnostics family is compiled in. WHY: keep release builds (no
// diagnostics flags) byte-identical by adding no diag_event_v1 code or log
// object when every family gate is off.
#if defined(EMULEBB_ENABLE_PACKET_DIAGNOSTICS) || EMULEBB_HAS_KAD_DIAGNOSTICS \
	|| EMULEBB_HAS_BAD_PEER_DIAGNOSTICS || defined(EMULEBB_ENABLE_UPLOAD_SLOT_DIAGNOSTICS) \
	|| defined(EMULEBB_ENABLE_DOWNLOAD_SLOT_DIAGNOSTICS)
#define EMULEBB_HAS_DIAG_EVENT_V1 1
#else
#define EMULEBB_HAS_DIAG_EVENT_V1 0
#endif
