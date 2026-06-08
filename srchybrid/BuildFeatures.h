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

#if defined(EMULEBB_ENABLE_STARTUP_PROFILING)
#define EMULEBB_HAS_STARTUP_PROFILING 1
#else
#define EMULEBB_HAS_STARTUP_PROFILING 0
#endif

#if defined(EMULEBB_ENABLE_BAD_PEER_INSTRUMENTATION)
#define EMULEBB_HAS_BAD_PEER_INSTRUMENTATION 1
#else
#define EMULEBB_HAS_BAD_PEER_INSTRUMENTATION 0
#endif
