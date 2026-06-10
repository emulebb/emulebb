// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN		// Exclude rarely-used stuff from Windows headers
#endif

#include "emule_site_config.h"
#include "BuildFeatures.h"

#ifndef WINVER
#define WINVER 0x0A00			// Windows 10 or later
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT WINVER
#endif

#ifndef _WIN32_IE
#define _WIN32_IE 0x0A00
#endif

#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS	// Makes certain CString constructors explicit, preventing any unintentional conversions
#define	_ATL_EX_CONVERSION_MACROS_ONLY		// Disable old ATL 3.0 string conversion macros
#ifdef _ATL_EX_CONVERSION_MACROS_ONLY
#define CharNextO CharNextW					// work around a bug in ATL headers
#endif//_ATL_EX_CONVERSION_MACROS_ONLY
#define _CONVERSION_DONT_USE_THREAD_LOCALE	// for consistency with C-RTL/MFC the ATL thread locale support has to get disabled
#define _ATL_NO_COM_SUPPORT
#define _ATL_NO_PERF_SUPPORT
#define	_ATL_NO_COMMODULE
#define _ATL_NO_CONNECTION_POINTS
#define _ATL_NO_DOCHOSTUIHANDLER
#define _ATL_NO_HOSTING

#define _ATL_ALL_WARNINGS
#define _AFX_ALL_WARNINGS
// Disable some warnings which get fired with /W4 or /Wall for Windows/MFC/ATL headers

#pragma warning(disable:4061) // enumerate in switch of enum is not explicitly handled by a case label
#pragma warning(disable:4062) // enumerate in switch of enum is not handled
#pragma warning(disable:4191) // 'type cast' : unsafe conversion from <this> to <that>
#pragma warning(disable:4263) // <func> member function does not override any base class virtual member function
#pragma warning(disable:4264) // <func>: no override available for virtual member function from base <class>; function is hidden
#pragma warning(disable:4265) // <class>: class has virtual functions, but destructor is not virtual
#pragma warning(disable:4266) // no override available for virtual member function from base <class>; function is hidden
#pragma warning(disable:4365) // conversion from 'int' to 'UINT', signed/unsigned mismatch
#pragma warning(disable:4435) // Object layout under /vd2 will change due to virtual base
#pragma warning(disable:4571) // Informational: catch(...) semantics changed since Visual C++ 7.1; structured exceptions (SEH) are no longer caught
#pragma warning(disable:4619) // #pragma warning : there is no warning number <n>
#pragma warning(disable:4625) // <class> : copy constructor could not be generated because a base class copy constructor is inaccessible
#pragma warning(disable:4626) // <class> : assignment operator could not be generated because a base class copy constructor is inaccessible
#pragma warning(disable:4640) // construction of local static object is not thread-safe
#pragma warning(disable:4668) // <name>  is not defined as a preprocessor macro, replacing with '0' for '#if/#elif'
#pragma warning(disable:4710) // function not inlined
#pragma warning(disable:4711) // function <func> selected for automatic inline expansion
#pragma warning(disable:4738) // storing 32-bit float result in memory, possible loss of performance
#ifdef _M_ARM64
#pragma warning(disable:4746) // volatile access of '<expression>' is subject to /volatile:[iso|ms] setting
#endif
#pragma warning(disable:4820) // <n> bytes padding added after member <member>
#pragma warning(disable:4917) // a GUID can only be associated with a class, interface or namespace
#pragma warning(disable:5026) // move constructor was implicitly defined as deleted
#pragma warning(disable:5027) // move assignment operator was implicitly defined as deleted
#pragma warning(disable:5045) // Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified
#pragma warning(disable:5220) // a non-static data member with a volatile qualified type no longer implies that compiler generated copy/move constructors and copy/move assignment operators are non trivial

// _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES - Overloads all standard string functions (e.g. strcpy) with "_s" functions
// if, and only if, the size of the output buffer is known at compile time (so, if it is a static array). If there is
// a buffer overflow during runtime, it will throw an exception.
//
#ifndef _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES
#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES 1
#endif

// _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_COUNT - This is a cool CRT feature but does not make sense for our code.
// With our existing code we could get exceptions which are not justifiable because we explicitly
// terminate all our string buffers. This define could be enabled for debug builds though.
//
//#ifndef _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_COUNT
//#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_COUNT 1
//#endif

#if !defined(_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES) || (_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES==0)
#ifndef _CRT_NON_CONFORMING_SWPRINTFS
#define _CRT_NON_CONFORMING_SWPRINTFS
#endif
#endif//!defined(_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES) || (_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES==0)

#ifdef _DEBUG
#define _ATL_DEBUG
#define _ATL_DEBUG_QI
#endif

#include <afxwin.h>			// MFC core and standard components
#undef _RICHEDIT_VER
#define _RICHEDIT_VER 0x0500

#include <afxext.h>			// MFC extensions
#include <afxdtctl.h>		// MFC support for 'CDateTimeCtrl' and 'CMonthCalCtrl'
#include <afxcmn.h>			// MFC support for Windows Common Controls
#include <afxole.h>			// MFC OLE support
#include <winsock2.h>
#define _WINSOCKAPI_
#include <afxsock.h>		// MFC support for Windows Sockets
#include <afxmt.h>			// MFC Multithreaded Extensions (Synchronization Objects)
#include <afxdlgs.h>		// MFC Standard dialogs
#include <atlcoll.h>
#include <afxcoll.h>
#include <afximpl.h>
#include <afxpriv.h>


#ifndef EWX_FORCEIFHUNG
#define EWX_FORCEIFHUNG			0x00000010
#endif

#ifndef WS_EX_LAYOUTRTL
#define WS_EX_LAYOUTRTL			0x00400000L // Right to left mirroring
#endif

#ifndef LAYOUT_RTL
#define LAYOUT_RTL				0x00000001 // Right to left
#endif

#ifndef COLOR_HOTLIGHT
#define COLOR_HOTLIGHT			26
#endif

#ifndef WS_EX_LAYERED
#define WS_EX_LAYERED			0x00080000
#endif

#ifndef LWA_COLORKEY
#define LWA_COLORKEY			0x00000001
#endif

#ifndef LWA_ALPHA
#define LWA_ALPHA				0x00000002
#endif

#ifndef HDF_SORTUP
#define HDF_SORTUP				0x0400
#endif

#ifndef HDF_SORTDOWN
#define HDF_SORTDOWN			0x0200
#endif

#ifndef COLOR_GRADIENTACTIVECAPTION
#define COLOR_GRADIENTACTIVECAPTION 27
#endif

#ifndef LVBKIF_TYPE_WATERMARK
#define LVBKIF_TYPE_WATERMARK   0x10000000
#endif

#ifndef LVBKIF_FLAG_ALPHABLEND
#define LVBKIF_FLAG_ALPHABLEND  0x20000000
#endif

#include "types.h"

#ifdef _DEBUG
#define malloc(s)		  _malloc_dbg(s, _NORMAL_BLOCK, __FILE__, __LINE__)
#define calloc(c, s)	  _calloc_dbg(c, s, _NORMAL_BLOCK, __FILE__, __LINE__)
#define realloc(p, s)	  _realloc_dbg(p, s, _NORMAL_BLOCK, __FILE__, __LINE__)
#define _expand(p, s)	  _expand_dbg(p, s, _NORMAL_BLOCK, __FILE__, __LINE__)
#define free(p)			  _free_dbg(p, _NORMAL_BLOCK)
#define _msize(p)		  _msize_dbg(p, _NORMAL_BLOCK)
#endif

typedef CArray<CStringA> CStringAArray;
typedef CStringArray CStringWArray;
//replaces str.Mid(idx) when a constant character pointer is required
#define CPTR(str, idx)	(&((LPCTSTR)(str))[(idx)])
#define CPTRA(str, idx)	(&((LPCSTR)(str))[(idx)])
#define CPTRW(str, idx)	(&((LPCWSTR)(str))[(idx)])


#ifdef UNICODE
#define _TWINAPI(fname)	fname "W"
#else
#define _TWINAPI(fname)	fname "A"
#endif

extern "C" int __cdecl __ascii_stricmp(const char * dst, const char * src);
