#pragma once

#include <cstddef>
#include <windows.h>
#include <tchar.h>

#include "MenuCmds.h"
#include "resource.h"

namespace AppWebLinksSeams
{
struct SWebLink
{
	UINT uCommandID;
	UINT uLabelStringID;
	LPCTSTR pszUrl;
};

inline const SWebLink *GetDocumentationLinks(size_t &ruCount)
{
	static const SWebLink s_aLinks[] = {
		{ MP_HM_LINK_DOC_FAQ, IDS_HM_LINK_DOC_FAQ, _T("https://emulebb.github.io/faq/") },
		{ MP_HM_LINK_DOC_SETUP, IDS_HM_LINK_DOC_SETUP, _T("https://emulebb.github.io/emulebb-tooling/reference/GUIDE-SETUP/") },
		{ MP_HM_LINK_DOC_NETWORK, IDS_HM_LINK_DOC_NETWORK, _T("https://emulebb.github.io/emulebb-tooling/reference/GUIDE-NETWORK/") },
		{ MP_HM_LINK_DOC_SHARING, IDS_HM_LINK_DOC_SHARING, _T("https://emulebb.github.io/emulebb-tooling/reference/GUIDE-SHARING/") },
		{ MP_HM_LINK_DOC_DOWNLOADS_SEARCH, IDS_HM_LINK_DOC_DOWNLOADS_SEARCH, _T("https://emulebb.github.io/emulebb-tooling/reference/GUIDE-DOWNLOADS-SEARCH/") },
		{ MP_HM_LINK_DOC_TOOLS_MENU, IDS_HM_LINK_DOC_TOOLS_MENU, _T("https://emulebb.github.io/emulebb-tooling/reference/GUIDE-TOOLS-MENU/") },
		{ MP_HM_LINK_DOC_CONTROLLERS_REST, IDS_HM_LINK_DOC_CONTROLLERS_REST, _T("https://emulebb.github.io/emulebb-tooling/reference/GUIDE-CONTROLLERS-REST/") },
		{ MP_HM_LINK_DOC_TROUBLESHOOTING, IDS_HM_LINK_DOC_TROUBLESHOOTING, _T("https://emulebb.github.io/emulebb-tooling/reference/GUIDE-TROUBLESHOOTING/") }
	};
	ruCount = sizeof s_aLinks / sizeof s_aLinks[0];
	return s_aLinks;
}
}
