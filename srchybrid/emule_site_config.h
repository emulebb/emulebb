#pragma once

// Local compiler site configuration for the Windows 10/11 build baseline.
// The current branch assumes a modern Visual Studio toolchain and Windows SDK.

/**
 * Detect optional SDK integrations which are not vendored in the tree.
 * SAPI and WMSDK stay feature-gated by real header availability in the active
 * toolchain.
 */
#if defined(__has_include) && __has_include(<sapi.h>)
#define HAVE_SAPI_H
#endif

#if defined(__has_include) && __has_include(<wmsdk.h>)
#define HAVE_WMSDK_H
#endif

#define HAVE_WIN7_SDK_H

#if _MSC_VER < 1937 // before VS2022 17.7
#define NOEXCEPT noexcept
#else
#define NOEXCEPT
#endif

#include <sdkddkver.h>
