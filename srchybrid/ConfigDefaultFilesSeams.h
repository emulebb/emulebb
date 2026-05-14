#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <atlstr.h>

namespace ConfigDefaultFilesSeams
{
enum EDefaultFileAction
{
	SkipInternalState = 0,
	CommentedTemplate = 1,
	ActiveTemplate = 2,
	RuntimeGenerated = 3
};

struct DefaultFileSpec
{
	LPCTSTR pszFileName;
	EDefaultFileAction eAction;
	LPCTSTR pszTemplateText;
};

inline LPCTSTR GetFakeFileFilterDefaultText()
{
	return _T("# eMule BB fake-file bad-signal rules\r\n")
		_T("# UTF-8 text. One rule per line. Lines starting with # or ; are ignored.\r\n")
		_T("# Token rules match whole words or separator-delimited phrases.\r\n")
		_T("# Regex rules use ECMAScript syntax and are matched case-insensitively.\r\n\r\n")
		_T("[tokens]\r\n")
		_T("fake\r\n")
		_T("corrupt\r\n")
		_T("wrong file\r\n")
		_T("password\r\n")
		_T("virus\r\n")
		_T("trojan\r\n")
		_T("malware\r\n\r\n")
		_T("[regex]\r\n")
		_T("\\.mp4\\.exe$\r\n")
		_T("\\.avi\\.scr$\r\n");
}

inline LPCTSTR GetIPFilterTemplateText()
{
	return _T("# ipfilter.dat\r\n")
		_T("# Add IP ranges to block in this format:\r\n")
		_T("# 001.002.003.004 - 005.006.007.008 , 100 , optional description\r\n")
		_T("# PeerGuardian text format is also accepted.\r\n");
}

inline LPCTSTR GetWebServicesTemplateText()
{
	return _T("# webservices.dat\r\n")
		_T("# Adds browser actions to eMule web-service menus. Nothing runs until you click a menu item.\r\n")
		_T("# Format: Menu Label,URL template\r\n")
		_T("# File macros: #hashid #filesize #filename #name #cleanfilename #cleanname\r\n")
		_T("# Lines using file macros appear for one selected file in Transfers, Search, and Shared Files.\r\n")
		_T("# Lines without file macros appear in the main Tools/Links menu.\r\n")
		_T("Search Web for Clean Filename,https://duckduckgo.com/?q=#cleanfilename\r\n")
		_T("Search Web for Exact Filename,https://duckduckgo.com/?q=#filename\r\n")
		_T("Search Web for Base Name,https://duckduckgo.com/?q=#cleanname\r\n")
		_T("# Power-user examples. Uncomment and adapt for preferred indexers or archives:\r\n")
		_T("# Search Web for ED2K Hash,https://duckduckgo.com/?q=#hashid\r\n")
		_T("# Search Web for Name and Size,https://duckduckgo.com/?q=#cleanname+%23filesize+#filesize\r\n")
		_T("# Search Web for Exact Name and Size,https://duckduckgo.com/?q=#filename+%23filesize+#filesize\r\n");
}

inline LPCTSTR GetStaticServersTemplateText()
{
	return _T("# staticservers.dat\r\n")
		_T("# Add one static server per line:\r\n")
		_T("# host:port,priority,server name\r\n")
		_T("# Priority values are 0 high, 1 normal, 2 low.\r\n");
}

inline LPCTSTR GetShareIgnoreTemplateText()
{
	return _T("# shareignore.dat\r\n")
		_T("# Add file or directory names to ignore while scanning shares.\r\n")
		_T("# Supported forms: exact.name, prefix*, *suffix\r\n");
}

inline LPCTSTR GetSharedDirTemplateText()
{
	return _T("# shareddir.dat\r\n")
		_T("# Add one absolute shared directory path per line.\r\n")
		_T("# Lines starting with # or ; are ignored.\r\n");
}

inline LPCTSTR GetMonitoredSharedDirTemplateText()
{
	return _T("# shareddir.monitored.dat\r\n")
		_T("# Add one absolute monitored shared root per line.\r\n")
		_T("# Lines starting with # or ; are ignored.\r\n");
}

inline LPCTSTR GetMonitorOwnedSharedDirTemplateText()
{
	return _T("# shareddir.monitor-owned.dat\r\n")
		_T("# This file stores directories added by shared-root monitoring.\r\n")
		_T("# Lines starting with # or ; are ignored.\r\n");
}

inline LPCTSTR GetCategoryIniTemplateText()
{
	return _T("; Category.ini\r\n")
		_T("; Category settings are normally written by the category UI.\r\n");
}

inline LPCTSTR GetNotifierIniTemplateText()
{
	return _T("; Notifier.ini\r\n")
		_T("; Optional taskbar notifier skin settings.\r\n")
		_T("[Config]\r\n")
		_T("; BmpFileName=\r\n")
		_T("; FontSize=8\r\n");
}

inline LPCTSTR GetFileInfoIniTemplateText()
{
	return _T("; fileinfo.ini\r\n")
		_T("; Shared-file comments are normally written by the file details UI.\r\n");
}

inline LPCTSTR GetStatisticsIniTemplateText()
{
	return _T("; statistics.ini\r\n")
		_T("; Cumulative statistics are normally written by eMule.\r\n")
		_T("[Statistics]\r\n");
}

inline const DefaultFileSpec *GetKnownDefaultFileSpecs(size_t &ruCount)
{
	static const DefaultFileSpec s_specs[] = {
		{ _T("addresses.dat"), RuntimeGenerated, NULL },
		{ _T("FakeFileFilter.dat"), ActiveTemplate, GetFakeFileFilterDefaultText() },
		{ _T("ipfilter.dat"), CommentedTemplate, GetIPFilterTemplateText() },
		{ _T("webservices.dat"), CommentedTemplate, GetWebServicesTemplateText() },
		{ _T("staticservers.dat"), CommentedTemplate, GetStaticServersTemplateText() },
		{ _T("shareignore.dat"), CommentedTemplate, GetShareIgnoreTemplateText() },
		{ _T("shareddir.dat"), CommentedTemplate, GetSharedDirTemplateText() },
		{ _T("shareddir.monitored.dat"), CommentedTemplate, GetMonitoredSharedDirTemplateText() },
		{ _T("shareddir.monitor-owned.dat"), CommentedTemplate, GetMonitorOwnedSharedDirTemplateText() },
		{ _T("Category.ini"), CommentedTemplate, GetCategoryIniTemplateText() },
		{ _T("Notifier.ini"), CommentedTemplate, GetNotifierIniTemplateText() },
		{ _T("fileinfo.ini"), CommentedTemplate, GetFileInfoIniTemplateText() },
		{ _T("statistics.ini"), CommentedTemplate, GetStatisticsIniTemplateText() },
		{ _T("preferences.ini"), SkipInternalState, NULL },
		{ _T("preferences.dat"), SkipInternalState, NULL },
		{ _T("preferencesKad.dat"), SkipInternalState, NULL },
		{ _T("cryptkey.dat"), SkipInternalState, NULL },
		{ _T("collectioncryptkey.dat"), SkipInternalState, NULL },
		{ _T("nodes.dat"), SkipInternalState, NULL },
		{ _T("nodes.fastkad.dat"), SkipInternalState, NULL },
		{ _T("server.met"), SkipInternalState, NULL },
		{ _T("server_met.download"), SkipInternalState, NULL },
		{ _T("server_met.old"), SkipInternalState, NULL },
		{ _T("src_index.dat"), SkipInternalState, NULL },
		{ _T("key_index.dat"), SkipInternalState, NULL },
		{ _T("load_index.dat"), SkipInternalState, NULL },
		{ _T("sharedfiles.dat"), SkipInternalState, NULL },
		{ _T("sharedcache.dat"), SkipInternalState, NULL },
		{ _T("shareddups.dat"), SkipInternalState, NULL },
		{ _T("shareddir.monitor-journal.dat"), SkipInternalState, NULL },
		{ _T("FakeFile.met"), SkipInternalState, NULL },
		{ _T("statbkup.ini"), SkipInternalState, NULL },
		{ _T("statbkuptmp.ini"), SkipInternalState, NULL },
		{ _T("onlinesig.dat"), SkipInternalState, NULL },
		{ _T("AC_BootstrapIPs.dat"), SkipInternalState, NULL },
		{ _T("AC_IPFilterUpdateURLs.dat"), SkipInternalState, NULL },
		{ _T("AC_SearchStrings.dat"), SkipInternalState, NULL },
		{ _T("AC_ServerMetURLs.dat"), SkipInternalState, NULL },
		{ _T("AC_VF_RegExpr.dat"), SkipInternalState, NULL },
		{ _T("PreviewApps.dat"), SkipInternalState, NULL },
		{ _T("desktop.ini"), SkipInternalState, NULL }
	};
	ruCount = sizeof s_specs / sizeof s_specs[0];
	return s_specs;
}

inline const DefaultFileSpec *FindKnownDefaultFileSpec(LPCTSTR pszFileName)
{
	size_t uCount = 0;
	const DefaultFileSpec *pSpecs = GetKnownDefaultFileSpecs(uCount);
	for (size_t i = 0; i < uCount; ++i)
		if (CString(pSpecs[i].pszFileName).CompareNoCase(pszFileName) == 0)
			return &pSpecs[i];
	return NULL;
}

inline bool IsTemplateAction(EDefaultFileAction eAction)
{
	return eAction == CommentedTemplate || eAction == ActiveTemplate;
}

inline bool IsAsciiWhitespace(uint8_t uByte)
{
	return uByte == ' ' || uByte == '\t' || uByte == '\r' || uByte == '\n';
}

inline bool IsUtf16LeWhitespace(uint16_t uChar)
{
	return uChar == ' ' || uChar == '\t' || uChar == '\r' || uChar == '\n';
}

inline bool IsBlankTemplatePayload(const std::vector<unsigned char> &rBytes)
{
	if (rBytes.empty())
		return true;

	size_t uOffset = 0;
	if (rBytes.size() >= 3u && rBytes[0] == 0xEF && rBytes[1] == 0xBB && rBytes[2] == 0xBF)
		uOffset = 3u;
	else if (rBytes.size() >= 2u && rBytes[0] == 0xFF && rBytes[1] == 0xFE) {
		uOffset = 2u;
		for (size_t i = uOffset; i + 1u < rBytes.size(); i += 2u) {
			const uint16_t uChar = static_cast<uint16_t>(rBytes[i]) | (static_cast<uint16_t>(rBytes[i + 1u]) << 8u);
			if (!IsUtf16LeWhitespace(uChar))
				return false;
		}
		return ((rBytes.size() - uOffset) % 2u) == 0u;
	}

	for (size_t i = uOffset; i < rBytes.size(); ++i)
		if (!IsAsciiWhitespace(rBytes[i]))
			return false;
	return true;
}

inline bool ShouldCreateDefaultFile(const DefaultFileSpec *pSpec, bool bFileExists, const std::vector<unsigned char> &rExistingBytes)
{
	if (pSpec == NULL || !IsTemplateAction(pSpec->eAction) || pSpec->pszTemplateText == NULL)
		return false;
	if (!bFileExists)
		return true;
	return IsBlankTemplatePayload(rExistingBytes);
}
}
