#pragma once

#include <tchar.h>

namespace AppRegistryIdentitySeams
{
/**
 * @brief Registry key owned by eMule BB for per-user app settings.
 */
inline LPCTSTR GetAppSettingsKey()
{
	return _T("Software\\eMuleBB");
}

/**
 * @brief Value name owned by eMule BB in the current user's Windows Run key.
 */
inline LPCTSTR GetAutoStartRunValueName()
{
	return _T("eMuleBBAutoStart");
}

/**
 * @brief Shared Windows URL scheme used for ed2k links.
 */
inline LPCTSTR GetEd2kScheme()
{
	return _T("ed2k");
}

/**
 * @brief HKCU classes key used when eMule BB claims the shared ed2k URL scheme.
 */
inline LPCTSTR GetEd2kClassesKey()
{
	return _T("Software\\Classes\\ed2k");
}

/**
 * @brief ProgID owned by eMule BB for collection files.
 */
inline LPCTSTR GetCollectionProgId()
{
	return _T("eMuleBB.Collection");
}

/**
 * @brief HKCU classes key owned by eMule BB for collection files.
 */
inline LPCTSTR GetCollectionClassesKey()
{
	return _T("Software\\Classes\\eMuleBB.Collection");
}
}

#define EMULE_TEST_HAVE_APP_REGISTRY_IDENTITY_SEAMS 1
