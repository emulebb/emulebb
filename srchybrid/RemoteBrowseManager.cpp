//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#include "stdafx.h"
#include "emule.h"
#include "RemoteBrowseManager.h"

#include <algorithm>

#include "ClientList.h"
#include "ClientListCtrl.h"
#include "DownloadQueue.h"
#include "EmuleDlg.h"
#include "Log.h"
#include "LongPathSeams.h"
#include "OtherFunctions.h"
#include "SafeFile.h"
#include "SearchDlg.h"
#include "SearchFile.h"
#include "SearchList.h"
#include "SearchParams.h"
#include "SearchResultsWnd.h"
#include "TransferDlg.h"
#include "UpdownClient.h"
#include "UploadQueue.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	constexpr uint32 kCacheMagic = 0x52424348; // RBCH
	constexpr uint16 kCacheVersion = 1;
	constexpr ULONGLONG kSweepIntervalMs = MIN2MS(5);
	constexpr ULONGLONG kStartupSettleMs = MIN2MS(2);
	constexpr ULONGLONG kQueueJitterMaxMs = MIN2MS(2);
	constexpr ULONGLONG kSessionIdleTimeoutMs = MIN2MS(2);
	constexpr ULONGLONG kSessionAbsoluteTimeoutMs = MIN2MS(10);
	constexpr time_t kCooldownSeconds = DAY2S(7);
	constexpr UINT kMaxCachedFilesPerClient = 64 * 1024;
	constexpr int kMaxDirectoryDepth = 36;
	constexpr size_t kMaxConcurrentAutoBrowses = 5;

	CString GetCacheDirectory()
	{
		return thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + _T("RemoteBrowseCache\\");
	}

	CString MakeSafeFallbackName(LPCTSTR pszName)
	{
		CString strName(pszName != NULL ? pszName : _T("Unknown Client"));
		strName.Trim();
		if (strName.IsEmpty())
			strName = _T("Unknown Client");
		return strName;
	}

	CString MakeBrowseTitle(LPCTSTR pszClientName)
	{
		CString strTitle(MakeSafeFallbackName(pszClientName));
		return strTitle;
	}
}

CRemoteBrowseManager::CRemoteBrowseManager()
	: m_bCacheIndexLoaded(false)
	, m_ullStartupSettleUntil(::GetTickCount64() + kStartupSettleMs)
	, m_ullNextSweepTick(m_ullStartupSettleUntil)
{
}

CRemoteBrowseManager::~CRemoteBrowseManager() = default;

time_t CRemoteBrowseManager::GetCurrentTime()
{
	return time(NULL);
}

CString CRemoteBrowseManager::BuildHashCacheKey(const uchar *pHash)
{
	CString strKey(_T("hash-"));
	strKey.Append(md4str(pHash));
	strKey.MakeLower();
	return strKey;
}

CString CRemoteBrowseManager::BuildEndpointCacheKey(const CUpDownClient &client)
{
	CString strKey;
	strKey.Format(_T("endpoint-%08x-%04x"), client.GetConnectIP() ? client.GetConnectIP() : client.GetIP(), client.GetUserPort());
	strKey.MakeLower();
	return strKey;
}

CString CRemoteBrowseManager::BuildCacheKeyForClient(const CUpDownClient &client)
{
	return client.HasValidHash() ? BuildHashCacheKey(client.GetUserHash()) : BuildEndpointCacheKey(client);
}

CString CRemoteBrowseManager::BuildCacheFilePath(const CString &strKey)
{
	return GetCacheDirectory() + strKey + _T(".browsecache");
}

CString CRemoteBrowseManager::BuildIdentityLabel(const CUpDownClient &client)
{
	if (client.HasValidHash())
		return md4str(client.GetUserHash());

	CString strIdentity(ipstr(client.GetConnectIP() ? client.GetConnectIP() : client.GetIP()));
	strIdentity.AppendFormat(_T(":%u"), client.GetUserPort());
	return strIdentity;
}

CString CRemoteBrowseManager::BuildResultKey(const CSearchFile &file)
{
	CString strKey(md4str(file.GetFileHash()));
	strKey += _T("|");
	strKey += file.GetDirectory() ? file.GetDirectory() : _T("");
	strKey += _T("|");
	strKey += file.GetFileName();
	return strKey;
}

CString CRemoteBrowseManager::BuildOutcomeText(EBrowseOutcome eOutcome)
{
	switch (eOutcome) {
	case EBrowseOutcome::Queued:
		return _T("Queued");
	case EBrowseOutcome::InProgress:
		return _T("In Progress");
	case EBrowseOutcome::Success:
		return _T("Success");
	case EBrowseOutcome::Failed:
		return _T("Failed");
	case EBrowseOutcome::Denied:
		return _T("Denied");
	case EBrowseOutcome::Unsupported:
		return _T("Unsupported");
	case EBrowseOutcome::Corrupt:
		return _T("Corrupt");
	default:
		return _T("None");
	}
}

CString CRemoteBrowseManager::BuildStatusText(const Entry &entry)
{
	if (entry.bInProgress)
		return _T("Browsing");
	if (entry.bQueued)
		return _T("Queued");
	if (entry.bPermanentSkip)
		return _T("Skipped");
	if (entry.pLiveClient != NULL)
		return _T("Online");
	return _T("Offline");
}

CString CRemoteBrowseManager::FormatTimeForList(time_t tValue)
{
	if (tValue <= 0)
		return CString();

	CTime timeValue(tValue);
	return timeValue.Format(_T("%Y-%m-%d %H:%M"));
}

int CRemoteBrowseManager::GetDirectoryDepth(LPCTSTR pszDirectory)
{
	if (pszDirectory == NULL || *pszDirectory == _T('\0'))
		return 0;

	int nDepth = 0;
	for (LPCTSTR psz = pszDirectory; *psz != _T('\0'); ++psz) {
		if (*psz == _T('\\') || *psz == _T('/'))
			++nDepth;
	}
	return nDepth + 1;
}

CRemoteBrowseManager::Entry& CRemoteBrowseManager::EnsureEntryForKey(const CString &strKey)
{
	Entry &entry = m_entries[strKey];
	entry.strKey = strKey;
	if (entry.strCachePath.IsEmpty())
		entry.strCachePath = BuildCacheFilePath(strKey);
	return entry;
}

CRemoteBrowseManager::Entry& CRemoteBrowseManager::EnsureEntryForClient(const CUpDownClient &client)
{
	const CString strKey(BuildCacheKeyForClient(client));
	Entry &entry = EnsureEntryForKey(strKey);
	entry.strDisplayName = MakeSafeFallbackName(client.GetUserName());
	entry.strIdentity = BuildIdentityLabel(client);
	entry.strLastLiveUserName = entry.strDisplayName;
	entry.pLiveClient = const_cast<CUpDownClient*>(&client);
	if (client.HasValidHash()) {
		const CString strLegacyKey(BuildEndpointCacheKey(client));
		if (strLegacyKey != strKey) {
			auto itLegacy = m_entries.find(strLegacyKey);
			if (itLegacy != m_entries.end()) {
				if (!entry.bHasAnyCache && itLegacy->second.bHasAnyCache && LongPathSeams::PathExists(itLegacy->second.strCachePath)) {
					LongPathSeams::CreateDirectory(GetCacheDirectory(), NULL);
					(void)LongPathSeams::MoveFileEx(itLegacy->second.strCachePath, entry.strCachePath, MOVEFILE_REPLACE_EXISTING);
					entry.bHasAnyCache = true;
				}
				if (entry.tLastSuccess < itLegacy->second.tLastSuccess)
					entry.tLastSuccess = itLegacy->second.tLastSuccess;
				if (entry.tLastAttempt < itLegacy->second.tLastAttempt)
					entry.tLastAttempt = itLegacy->second.tLastAttempt;
				if (entry.tLastDenied < itLegacy->second.tLastDenied)
					entry.tLastDenied = itLegacy->second.tLastDenied;
				if (entry.eOutcome == EBrowseOutcome::None)
					entry.eOutcome = itLegacy->second.eOutcome;
				m_entries.erase(itLegacy);
			}
		}
	}
	return entry;
}

CRemoteBrowseManager::Entry* CRemoteBrowseManager::FindEntryByClient(const CUpDownClient &client)
{
	const CString strKey(BuildCacheKeyForClient(client));
	auto it = m_entries.find(strKey);
	if (it != m_entries.end())
		return &it->second;

	const CString strLegacyKey(BuildEndpointCacheKey(client));
	it = m_entries.find(strLegacyKey);
	return (it != m_entries.end()) ? &it->second : NULL;
}

const CRemoteBrowseManager::Entry* CRemoteBrowseManager::FindEntryByClient(const CUpDownClient &client) const
{
	const CString strKey(BuildCacheKeyForClient(client));
	auto it = m_entries.find(strKey);
	if (it != m_entries.end())
		return &it->second;

	const CString strLegacyKey(BuildEndpointCacheKey(client));
	it = m_entries.find(strLegacyKey);
	return (it != m_entries.end()) ? &it->second : NULL;
}

CRemoteBrowseManager::Entry* CRemoteBrowseManager::FindEntryByKey(const CString &strKey)
{
	auto it = m_entries.find(strKey);
	return (it != m_entries.end()) ? &it->second : NULL;
}

const CRemoteBrowseManager::Entry* CRemoteBrowseManager::FindEntryByKey(const CString &strKey) const
{
	auto it = m_entries.find(strKey);
	return (it != m_entries.end()) ? &it->second : NULL;
}

void CRemoteBrowseManager::RefreshLiveClientBindings()
{
	for (auto &pair : m_entries)
		pair.second.pLiveClient = NULL;

	if (theApp.clientlist == NULL)
		return;

	for (POSITION pos = theApp.clientlist->list.GetHeadPosition(); pos != NULL;) {
		CUpDownClient *pClient = theApp.clientlist->list.GetNext(pos);
		EnsureEntryForClient(*pClient);
	}
}

void CRemoteBrowseManager::EnsureCacheIndexLoaded()
{
	if (!m_bCacheIndexLoaded)
		ScanCacheDirectory();
}

void CRemoteBrowseManager::ScanCacheDirectory()
{
	m_bCacheIndexLoaded = true;
	LongPathSeams::CreateDirectory(GetCacheDirectory(), NULL);

	WIN32_FIND_DATA findData = {};
	const CString strPattern(GetCacheDirectory() + _T("*.browsecache"));
	const HANDLE hFind = LongPathSeams::FindFirstFile(strPattern, &findData);
	if (hFind == INVALID_HANDLE_VALUE)
		return;

	do {
		if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			continue;

		CString strPath(GetCacheDirectory() + findData.cFileName);
		CacheHeader header;
		uint64 ullBytes = 0;
		if (!LoadCacheHeader(strPath, header, ullBytes))
			continue;

		CString strKey(findData.cFileName);
		const int nSuffix = strKey.ReverseFind(_T('.'));
		if (nSuffix > 0)
			strKey = strKey.Left(nSuffix);
		Entry &entry = EnsureEntryForKey(strKey);
		entry.strCachePath = strPath;
		entry.strDisplayName = header.strDisplayName;
		entry.strIdentity = header.strIdentity;
		entry.tLastAttempt = header.tLastAttempt;
		entry.tLastSuccess = header.tLastSuccess;
		entry.tLastDenied = header.tLastDenied;
		entry.ullCachedBytes = ullBytes;
		entry.ullCachedTotalSize = header.ullCachedTotalSize;
		entry.uCachedFileCount = header.uCachedFileCount;
		entry.eOutcome = header.eOutcome;
		entry.bPermanentSkip = header.bPermanentSkip;
		entry.bHasAnyCache = header.uCachedFileCount > 0;
		entry.bCacheIndexed = true;
	}
	while (FindNextFile(hFind, &findData));
	FindClose(hFind);
}

bool CRemoteBrowseManager::LoadCacheHeader(const CString &strPath, CacheHeader &rHeader, uint64 &rFileBytes) const
{
	CSafeBufferedFile file;
	if (!LongPathSeams::OpenFile(file, strPath, CFile::modeRead | CFile::shareDenyWrite | CFile::typeBinary))
		return false;

	try {
		if (file.ReadUInt32() != kCacheMagic || file.ReadUInt16() != kCacheVersion)
			return false;
		rHeader.strDisplayName = static_cast<CFileDataIO&>(file).ReadString(true);
		rHeader.strIdentity = static_cast<CFileDataIO&>(file).ReadString(true);
		rHeader.tLastAttempt = static_cast<time_t>(file.ReadUInt64());
		rHeader.tLastSuccess = static_cast<time_t>(file.ReadUInt64());
		rHeader.tLastDenied = static_cast<time_t>(file.ReadUInt64());
		rHeader.ullCachedTotalSize = file.ReadUInt64();
		rHeader.uCachedFileCount = file.ReadUInt32();
		rHeader.eOutcome = static_cast<EBrowseOutcome>(file.ReadUInt8());
		rHeader.bPermanentSkip = file.ReadUInt8() != 0;
		rFileBytes = file.GetLength();
		file.Close();
		return true;
	}
	catch (CFileException *pEx) {
		pEx->Delete();
	}
	file.Close();
	return false;
}

bool CRemoteBrowseManager::LoadCacheFiles(const Entry &entry, uint32 dwSearchID, std::vector<std::unique_ptr<CSearchFile>> &rFiles) const
{
	CSafeBufferedFile file;
	if (!LongPathSeams::OpenFile(file, entry.strCachePath, CFile::modeRead | CFile::shareDenyWrite | CFile::typeBinary))
		return false;

	try {
		if (file.ReadUInt32() != kCacheMagic || file.ReadUInt16() != kCacheVersion)
			return false;
		(void)static_cast<CFileDataIO&>(file).ReadString(true);
		(void)static_cast<CFileDataIO&>(file).ReadString(true);
		(void)file.ReadUInt64();
		(void)file.ReadUInt64();
		(void)file.ReadUInt64();
		(void)file.ReadUInt64();
		const uint32 nFileCount = file.ReadUInt32();
		(void)file.ReadUInt8();
		(void)file.ReadUInt8();

		for (uint32 i = 0; i < nFileCount; ++i)
			rFiles.emplace_back(new CSearchFile(file, true, dwSearchID));
		file.Close();
		return true;
	}
	catch (CFileException *pEx) {
		pEx->Delete();
	}
	file.Close();
	return false;
}

bool CRemoteBrowseManager::PersistCache(Entry &entry, const std::map<CString, std::unique_ptr<CSearchFile>> &rFiles)
{
	LongPathSeams::CreateDirectory(GetCacheDirectory(), NULL);

	const CString strTempPath(entry.strCachePath + _T(".tmp"));
	CSafeBufferedFile file;
	if (!LongPathSeams::OpenFile(file, strTempPath, CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite | CFile::typeBinary))
		return false;

	try {
		file.WriteUInt32(kCacheMagic);
		file.WriteUInt16(kCacheVersion);
		static_cast<CFileDataIO&>(file).WriteString(entry.strDisplayName, UTF8strRaw);
		static_cast<CFileDataIO&>(file).WriteString(entry.strIdentity, UTF8strRaw);
		file.WriteUInt64(static_cast<uint64>(entry.tLastAttempt));
		file.WriteUInt64(static_cast<uint64>(entry.tLastSuccess));
		file.WriteUInt64(static_cast<uint64>(entry.tLastDenied));
		file.WriteUInt64(entry.ullCachedTotalSize);
		file.WriteUInt32(static_cast<uint32>(rFiles.size()));
		file.WriteUInt8(static_cast<uint8>(entry.eOutcome));
		file.WriteUInt8(static_cast<uint8>(entry.bPermanentSkip));
		for (const auto &pair : rFiles)
			pair.second->StoreToFile(file);
		file.Flush();
		entry.ullCachedBytes = file.GetLength();
		file.Close();
	}
	catch (CFileException *pEx) {
		pEx->Delete();
		file.Close();
		(void)LongPathSeams::DeleteFileIfExists(strTempPath);
		return false;
	}

	if (!LongPathSeams::MoveFileEx(strTempPath, entry.strCachePath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		(void)LongPathSeams::DeleteFileIfExists(strTempPath);
		return false;
	}

	entry.bHasAnyCache = !rFiles.empty();
	entry.bCacheIndexed = true;
	return true;
}

void CRemoteBrowseManager::MergeSessionIntoCache(Entry &entry, std::map<CString, std::unique_ptr<CSearchFile>> &rMergedFiles) const
{
	std::vector<std::unique_ptr<CSearchFile>> existingFiles;
	if (entry.bHasAnyCache)
		(void)LoadCacheFiles(entry, 0, existingFiles);

	for (auto &pFile : existingFiles) {
		const CString strKey(BuildResultKey(*pFile));
		if (rMergedFiles.find(strKey) == rMergedFiles.end())
			rMergedFiles[strKey] = std::move(pFile);
	}

	entry.ullCachedTotalSize = 0;
	for (const auto &pair : rMergedFiles)
		entry.ullCachedTotalSize += pair.second->GetFileSize();
	entry.uCachedFileCount = static_cast<UINT>(rMergedFiles.size());
}

void CRemoteBrowseManager::NotifyCachedClientsChanged() const
{
	if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL && theApp.emuledlg->transferwnd->GetSafeHwnd() != NULL)
		theApp.emuledlg->transferwnd->GetCachedClientsList()->Reload();
}

void CRemoteBrowseManager::UpdateTransferCount() const
{
	if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL)
		theApp.emuledlg->transferwnd->UpdateListCount(CTransferWnd::wnd2CachedClients, static_cast<int>(m_entries.size()));
}

void CRemoteBrowseManager::MarkCacheCorrupt(Entry &entry)
{
	entry.eOutcome = EBrowseOutcome::Corrupt;
	entry.bCacheCorrupt = true;
	entry.bHasAnyCache = false;
	entry.uCachedFileCount = 0;
	entry.ullCachedBytes = 0;
	entry.ullCachedTotalSize = 0;
	(void)LongPathSeams::DeleteFileIfExists(entry.strCachePath);
	AddLogLine(true, _T("Discarded corrupt remote browse cache for %s"), static_cast<LPCTSTR>(entry.strDisplayName));
}

void CRemoteBrowseManager::AbortSession(Entry &entry, EBrowseOutcome eOutcome, LPCTSTR pszReason, bool bKeepPermanentSkip)
{
	entry.bQueued = false;
	entry.bInProgress = false;
	entry.eOutcome = eOutcome;
	entry.tLastAttempt = GetCurrentTime();
	if (eOutcome == EBrowseOutcome::Denied)
		entry.tLastDenied = entry.tLastAttempt;
	entry.bPermanentSkip = bKeepPermanentSkip ? entry.bPermanentSkip : (eOutcome == EBrowseOutcome::Denied || eOutcome == EBrowseOutcome::Unsupported);
	entry.activeFiles.clear();
	entry.dwReplaceSearchID = 0;
	entry.ullQueueAfterTick = 0;
	entry.ullSessionStartTick = 0;
	entry.ullLastProgressTick = 0;
	entry.bDirectoryEnumerationSeen = false;
	if (pszReason != NULL)
		AddLogLine(true, _T("%s"), pszReason);
	NotifyCachedClientsChanged();
	UpdateTransferCount();
}

bool CRemoteBrowseManager::StartBrowseSession(Entry &entry, ESessionKind eKind, uint32 dwReplaceSearchID)
{
	if (entry.pLiveClient == NULL || theApp.IsClosing())
		return false;

	if (!entry.pLiveClient->GetViewSharedFilesSupport()) {
		AbortSession(entry, EBrowseOutcome::Unsupported, _T("Remote browse skipped: client explicitly disabled share browsing."));
		return false;
	}

	entry.bQueued = false;
	entry.bInProgress = true;
	entry.eSessionKind = eKind;
	entry.eOutcome = EBrowseOutcome::InProgress;
	entry.tLastAttempt = GetCurrentTime();
	entry.ullQueueAfterTick = 0;
	entry.ullSessionStartTick = ::GetTickCount64();
	entry.ullLastProgressTick = entry.ullSessionStartTick;
	entry.activeFiles.clear();
	entry.dwReplaceSearchID = dwReplaceSearchID;
	entry.bDirectoryEnumerationSeen = false;

	if (eKind == ESessionKind::Auto)
		AddLogLine(true, _T("Auto-browsing shared files from %s"), static_cast<LPCTSTR>(entry.strDisplayName));
	else
		AddLogLine(true, _T("Refreshing shared files from %s"), static_cast<LPCTSTR>(entry.strDisplayName));

	entry.pLiveClient->RequestSharedFileList();
	NotifyCachedClientsChanged();
	UpdateTransferCount();
	return true;
}

void CRemoteBrowseManager::CompleteSession(Entry &entry)
{
	if (!entry.bInProgress)
		return;

	if (entry.activeFiles.empty()) {
		AbortSession(entry, EBrowseOutcome::Failed, _T("Remote browse finished without any cacheable shared files."));
		return;
	}

	std::map<CString, std::unique_ptr<CSearchFile>> mergedFiles;
	for (auto &pair : entry.activeFiles)
		mergedFiles[pair.first] = std::move(pair.second);
	entry.activeFiles.clear();
	MergeSessionIntoCache(entry, mergedFiles);

	entry.eOutcome = EBrowseOutcome::Success;
	entry.tLastSuccess = GetCurrentTime();
	entry.tLastAttempt = entry.tLastSuccess;
	entry.bInProgress = false;
	entry.bQueued = false;
	entry.bPermanentSkip = false;
	entry.ullSessionStartTick = 0;
	entry.ullLastProgressTick = 0;
	entry.bDirectoryEnumerationSeen = false;

	if (!PersistCache(entry, mergedFiles)) {
		AbortSession(entry, EBrowseOutcome::Failed, _T("Failed to persist remote browse cache."));
		return;
	}

	AddLogLine(true, _T("Cached %u shared files from %s"), entry.uCachedFileCount, static_cast<LPCTSTR>(entry.strDisplayName));

	if (entry.eSessionKind == ESessionKind::Manual) {
		const uint32 dwReplaceSearchID = entry.dwReplaceSearchID;
		entry.dwReplaceSearchID = 0;
		(void)OpenCachedInternal(entry, dwReplaceSearchID);
	}
	NotifyCachedClientsChanged();
	UpdateTransferCount();
}

bool CRemoteBrowseManager::HandleSharedFileListPacket(CUpDownClient &client, const uchar *pPacket, uint32 nSize, LPCTSTR pszDirectory)
{
	Entry *pEntry = FindEntryByClient(client);
	if (pEntry == NULL || !pEntry->bInProgress)
		return false;

	CSafeMemFile packet(pPacket, nSize);
	try {
		const uint32 nResults = packet.ReadUInt32();
		for (uint32 i = 0; i < nResults; ++i) {
			std::unique_ptr<CSearchFile> pFile(new CSearchFile(packet, client.GetUnicodeSupport() != UTF8strNone, 0, client.GetServerIP(), client.GetServerPort(), pszDirectory));
			if (pFile->IsLargeFile() && !client.SupportsLargeFiles())
				continue;

			const CString strFileKey(BuildResultKey(*pFile));
			pEntry->activeFiles[strFileKey] = std::move(pFile);
			if (pEntry->activeFiles.size() > kMaxCachedFilesPerClient) {
				AbortSession(*pEntry, EBrowseOutcome::Failed, _T("Remote browse aborted: file count cap reached."));
				return true;
			}
		}
		pEntry->ullLastProgressTick = ::GetTickCount64();
		if (client.GetFileListRequested() == 0)
			CompleteSession(*pEntry);
		else
			NotifyCachedClientsChanged();
		return true;
	}
	catch (CFileException *pEx) {
		pEx->Delete();
		AbortSession(*pEntry, EBrowseOutcome::Failed, _T("Remote browse aborted while parsing shared file data."));
	}
	return true;
}

bool CRemoteBrowseManager::ShouldRequestDirectory(const CUpDownClient &client, const CString &strDirectory) const
{
	const Entry *pEntry = FindEntryByClient(client);
	if (pEntry == NULL || !pEntry->bInProgress)
		return true;

	return GetDirectoryDepth(strDirectory) <= kMaxDirectoryDepth;
}

void CRemoteBrowseManager::NotifyDirectoryEnumerationComplete(const CUpDownClient &client)
{
	Entry *pEntry = FindEntryByClient(client);
	if (pEntry == NULL || !pEntry->bInProgress)
		return;

	if (client.GetFileListRequested() == 0)
		CompleteSession(*pEntry);
}

void CRemoteBrowseManager::HandleBrowseDenied(const CUpDownClient *pClient)
{
	if (pClient == NULL)
		return;

	Entry &entry = EnsureEntryForClient(*pClient);
	AbortSession(entry, EBrowseOutcome::Denied, _T("Remote browse denied by peer."));
}

void CRemoteBrowseManager::HandleBrowseTransportFailure(const CUpDownClient *pClient)
{
	if (pClient == NULL)
		return;

	Entry *pEntry = FindEntryByClient(*pClient);
	if (pEntry != NULL && pEntry->bInProgress)
		AbortSession(*pEntry, EBrowseOutcome::Failed, _T("Remote browse failed before completion."));
}

void CRemoteBrowseManager::OnClientRemoved(const CUpDownClient *pClient)
{
	HandleBrowseTransportFailure(pClient);
}

void CRemoteBrowseManager::QueueEligibleAutoBrowses(ULONGLONG ullNowTick, time_t tNow)
{
	if (!thePrefs.GetAutoBrowseRemoteShares())
		return;

	for (auto &pair : m_entries) {
		Entry &entry = pair.second;
		if (entry.pLiveClient == NULL || entry.bQueued || entry.bInProgress || entry.bPermanentSkip)
			continue;

		if (!entry.pLiveClient->GetViewSharedFilesSupport()) {
			entry.bPermanentSkip = true;
			entry.eOutcome = EBrowseOutcome::Unsupported;
			continue;
		}

		const time_t tCooldownStart = max(max(entry.tLastSuccess, entry.tLastAttempt), entry.tLastDenied);
		if (tCooldownStart != 0 && tNow < tCooldownStart + kCooldownSeconds)
			continue;

		entry.bQueued = true;
		entry.eOutcome = EBrowseOutcome::Queued;
		entry.ullQueueAfterTick = ullNowTick + (rand() % (static_cast<int>(kQueueJitterMaxMs) + 1));
	}
}

void CRemoteBrowseManager::StartDueAutoBrowses(ULONGLONG ullNowTick)
{
	const bool bUnderPressure = (theApp.uploadqueue != NULL && theApp.uploadqueue->GetDatarate() > 0)
		|| (theApp.downloadqueue != NULL && theApp.downloadqueue->GetDatarate() > 0);
	size_t uActiveAutoSessions = 0;
	for (const auto &pair : m_entries)
		uActiveAutoSessions += static_cast<size_t>(pair.second.bInProgress && pair.second.eSessionKind == ESessionKind::Auto);

	size_t uLaunchBudget = (uActiveAutoSessions >= kMaxConcurrentAutoBrowses) ? 0 : (kMaxConcurrentAutoBrowses - uActiveAutoSessions);
	if (bUnderPressure)
		uLaunchBudget = std::min<size_t>(uLaunchBudget, 1);

	for (auto &pair : m_entries) {
		Entry &entry = pair.second;
		if (uLaunchBudget == 0)
			break;
		if (!entry.bQueued || entry.ullQueueAfterTick > ullNowTick)
			continue;
		if (StartBrowseSession(entry, ESessionKind::Auto))
			--uLaunchBudget;
	}
}

void CRemoteBrowseManager::Process()
{
	RefreshLiveClientBindings();

	const ULONGLONG ullNowTick = ::GetTickCount64();
	const time_t tNow = GetCurrentTime();
	for (auto &pair : m_entries) {
		Entry &entry = pair.second;
		if (!entry.bInProgress)
			continue;
		if (entry.pLiveClient == NULL) {
			AbortSession(entry, EBrowseOutcome::Failed, _T("Remote browse aborted because the client disappeared."));
			continue;
		}
		if (ullNowTick >= entry.ullLastProgressTick + kSessionIdleTimeoutMs
			|| ullNowTick >= entry.ullSessionStartTick + kSessionAbsoluteTimeoutMs)
		{
			AbortSession(entry, EBrowseOutcome::Failed, _T("Remote browse timed out."));
		}
	}

	if (ullNowTick < m_ullStartupSettleUntil)
		return;

	if (ullNowTick >= m_ullNextSweepTick) {
		QueueEligibleAutoBrowses(ullNowTick, tNow);
		m_ullNextSweepTick = ullNowTick + kSweepIntervalMs;
	}
	StartDueAutoBrowses(ullNowTick);
}

void CRemoteBrowseManager::StartManualBrowse(CUpDownClient *pClient)
{
	if (pClient == NULL)
		return;

	Entry &entry = EnsureEntryForClient(*pClient);
	if (entry.bInProgress && entry.eSessionKind == ESessionKind::Auto)
		AbortSession(entry, EBrowseOutcome::Failed, _T("Auto-browse superseded by manual browse."), true);

	uint32 dwReplaceSearchID = 0;
	if (entry.bHasAnyCache) {
		(void)OpenCachedInternal(entry, 0);
		dwReplaceSearchID = entry.dwVisibleSearchID;
	}
	StartBrowseSession(entry, ESessionKind::Manual, dwReplaceSearchID);
}

void CRemoteBrowseManager::OpenCachedOnlyByClient(const CUpDownClient *pClient)
{
	if (pClient == NULL)
		return;
	Entry &entry = EnsureEntryForClient(*pClient);
	(void)OpenCachedInternal(entry, 0);
}

bool CRemoteBrowseManager::OpenCachedInternal(Entry &entry, uint32 dwReplaceSearchID)
{
	if (!entry.bHasAnyCache)
		return false;

	const uint32 dwSearchID = theApp.emuledlg->searchwnd->m_pwndResults->GetNextSearchID();
	std::vector<std::unique_ptr<CSearchFile>> files;
	if (!LoadCacheFiles(entry, dwSearchID, files)) {
		MarkCacheCorrupt(entry);
		NotifyCachedClientsChanged();
		return false;
	}

	if (dwReplaceSearchID != 0)
		theApp.emuledlg->searchwnd->DeleteSearch(dwReplaceSearchID);
	else if (entry.dwVisibleSearchID != 0)
		theApp.emuledlg->searchwnd->DeleteSearch(entry.dwVisibleSearchID);

	SSearchParams *pParams = new SSearchParams;
	pParams->dwSearchID = dwSearchID;
	pParams->strExpression = MakeBrowseTitle(entry.strDisplayName);
	pParams->bClientSharedFiles = true;

	theApp.searchlist->NewSearch(&theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl, CString(), pParams);
	if (!theApp.emuledlg->searchwnd->CreateNewTab(pParams, false)) {
		delete pParams;
		return false;
	}

	for (auto &pFile : files)
		theApp.searchlist->AddToList(pFile.release(), true);
	theApp.searchlist->ShowResults(dwSearchID);
	entry.dwVisibleSearchID = dwSearchID;
	return true;
}

void CRemoteBrowseManager::OpenCachedOnlyByKey(const CString &strKey)
{
	EnsureCacheIndexLoaded();
	Entry *pEntry = FindEntryByKey(strKey);
	if (pEntry != NULL)
		(void)OpenCachedInternal(*pEntry, 0);
}

bool CRemoteBrowseManager::CanRefreshKey(const CString &strKey) const
{
	const Entry *pEntry = FindEntryByKey(strKey);
	return pEntry != NULL && pEntry->pLiveClient != NULL;
}

void CRemoteBrowseManager::RefreshKey(const CString &strKey)
{
	Entry *pEntry = FindEntryByKey(strKey);
	if (pEntry != NULL && pEntry->pLiveClient != NULL)
		StartManualBrowse(pEntry->pLiveClient);
}

void CRemoteBrowseManager::GetSnapshots(std::vector<Snapshot> &rSnapshots)
{
	rSnapshots.clear();
	rSnapshots.reserve(m_entries.size());

	for (const auto &pair : m_entries) {
		const Entry &entry = pair.second;
		Snapshot snapshot;
		snapshot.strKey = entry.strKey;
		snapshot.strDisplayName = entry.strDisplayName.IsEmpty() ? _T("Unknown Client") : entry.strDisplayName;
		snapshot.strIdentity = entry.strIdentity;
		snapshot.strStatus = BuildStatusText(entry);
		snapshot.strOutcome = BuildOutcomeText(entry.eOutcome);
		snapshot.tLastAttempt = entry.tLastAttempt;
		snapshot.tLastSuccess = entry.tLastSuccess;
		snapshot.ullCachedBytes = entry.ullCachedBytes;
		snapshot.ullCachedTotalSize = entry.ullCachedTotalSize;
		snapshot.uCachedFileCount = entry.uCachedFileCount;
		snapshot.bQueued = entry.bQueued;
		snapshot.bInProgress = entry.bInProgress;
		snapshot.bHasLiveClient = entry.pLiveClient != NULL;
		snapshot.bCanRefresh = entry.pLiveClient != NULL;
		rSnapshots.push_back(snapshot);
	}
}

UINT CRemoteBrowseManager::GetCachedClientCount()
{
	return static_cast<UINT>(m_entries.size());
}
