#ifndef __VERSION_H__
#define __VERSION_H__

#pragma once

#ifndef _T
#define _T(x)	x
#endif

#define _chSTR(x)	_T(#x)
#define chSTR(x)	_chSTR(x)

// *) Specify the version of emule only here with the following defines.
// *) When changing any of those version nr. defines you also have to rebuild the language DLLs.
//
// General format:
//	<major>.<minor>.<update>.<build>
//
// Fields:
//	<major>		major number  (e.g. 0)
//	<minor>		minor number  (e.g. 30)
//	<update>	update number (e.g. 0='a'  1='b'  2='c'  3='d'  4='e'  5='f' ...)
//	<build>		build number  (1 or higher)
//
// Currently used:
//  <major>.<minor>.<update> is used for the displayed version (GUI) and the version check number
//	<major>.<minor>			 is used for the protocol(!) version
//  -------
//  <build>                  not used for any checks in the program
#define VERSION_MJR	0
#define VERSION_MIN	72
#define VERSION_UPDATE	0
#define VERSION_BUILD	1
// NOTE: Do not forget to update manifests in "res\eMule*.manifest"

// eMule BB is a mod release line with its own public release cadence.
// Keep the upstream eMule version above for protocol, manifests, and legacy
// compatibility checks; use these values for public branding and update checks.
#define MOD_RELEASE_PRODUCT_NAME		_T("eMule BB")
#define MOD_RELEASE_TAG_PREFIX			_T("emule-bb-v")
#define MOD_RELEASE_BASE_VERSION_TEXT	_T("0.72a")
#define MOD_RELEASE_VERSION_MAJOR		0
#define MOD_RELEASE_VERSION_MINOR		7
#define MOD_RELEASE_VERSION_PATCH		3
#define MOD_RELEASE_VERSION_TEXT		chSTR(MOD_RELEASE_VERSION_MAJOR) _T(".") chSTR(MOD_RELEASE_VERSION_MINOR) _T(".") chSTR(MOD_RELEASE_VERSION_PATCH)
#define MOD_RELEASE_DISPLAY_NAME		MOD_RELEASE_PRODUCT_NAME _T(" ") MOD_RELEASE_VERSION_TEXT
#define MOD_CLIENT_MOD_VERSION_TEXT	MOD_RELEASE_DISPLAY_NAME

#if defined _M_X64
#define VERSION_PLATFORM _T(" x64")
#elif defined _M_ARM64
#define VERSION_PLATFORM _T(" arm64")
#else //x86
#define VERSION_PLATFORM _T("")
#endif

// NOTE: This version string is also used by the language DLLs!
#define	SZ_VERSION_NAME		chSTR(VERSION_MJR) _T(".") chSTR(VERSION_MIN) _T(".") chSTR(VERSION_UPDATE)

#endif /* !__VERSION_H__ */
