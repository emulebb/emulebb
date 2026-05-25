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
/* #ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif */
#include <atlconv.h>
#include <atlimage.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <shellapi.h>
#include <string>
#include <sys/stat.h>
#include <sockimpl.h> //for *m_pfnSockTerm()
#include <timeapi.h>
#include <uxtheme.h>
#include "AppCommandLineSeams.h"
#include "emule.h"
#include "resource.h"
#include "Version.h"
#include "opcodes.h"
#include "mdump.h"
#include "Scheduler.h"
#include "SearchList.h"
#include "kademlia/kademlia/Error.h"
#include "kademlia/kademlia/Kademlia.h"
#include "kademlia/kademlia/Prefs.h"
#include "kademlia/utils/UInt128.h"
#include "PerfLog.h"
#include "UploadBandwidthThrottler.h"
#include "ClientList.h"
#include "FriendList.h"
#include "ClientUDPSocket.h"
#include "UpDownClient.h"
#include "DownloadQueue.h"
#include "IPFilter.h"
#include "IPFilterUpdater.h"
#include "Statistics.h"
#include "WebServer.h"
#include "UploadQueue.h"
#include "SharedFileList.h"
#include "ServerList.h"
#include "ServerConnect.h"
#include "ListenSocket.h"
#include "ClientCredits.h"
#include "KnownFileList.h"
#include "Server.h"
#include "ED2KLink.h"
#include "ConfigStartupBackup.h"
#include "Preferences.h"
#include "StartupConfigOverride.h"
#include "SafeFile.h"
#include "ShellUiHelpers.h"
#include "StandbyPreventionSeams.h"
#include "LongPathSeams.h"
#include "PathHelpers.h"
#include "emuleDlg.h"
#include "SharedFilesWnd.h"
#include "enbitmap.h"
#include "StringConversion.h"
#include "LogArtifactNames.h"
#include "Log.h"
#include "Collection.h"
#include "GeoLocation.h"
#include "UPnPImplWrapper.h"
#include "UploadDiskIOThread.h"
#include "PartFileWriteThread.h"
#include "OtherFunctions.h"
#include "PartFilePersistenceSeams.h"
#include "SharedDirectoryOps.h"
#include "SharedDirectoryMonitorSeams.h"
#include "UserMsgs.h"
#include "BindStartupPolicy.h"
#include "WorkerUiMessageSeams.h"
#include "WebServerCertificate.h"
#include "MediaInfo.h"
#include "LifecycleProgressDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
LPCTSTR const MONITOREDSHAREDJOURNALSTATE = _T("shareddir.monitor-journal.dat");
LPCTSTR const STARTUPERRORLOG = _T("eMule-startup-errors.log");
constexpr DWORD kMonitoredSharedFileWatchMask = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;
constexpr DWORD kMonitoredSharedDirectoryWatchMask = FILE_NOTIFY_CHANGE_DIR_NAME;

std::vector<CString> TokenizeCurrentCommandLine()
{
	std::vector<CString> aTokens;
	int iArgCount = 0;
	LPWSTR *papszArguments = ::CommandLineToArgvW(::GetCommandLineW(), &iArgCount);
	if (papszArguments != NULL) {
		aTokens.reserve(iArgCount);
		for (int i = 0; i < iArgCount; ++i)
			aTokens.emplace_back(papszArguments[i]);
		::LocalFree(papszArguments);
		return aTokens;
	}

	aTokens.reserve(__argc);
	for (int i = 0; i < __argc; ++i)
		aTokens.emplace_back(__targv[i]);
	return aTokens;
}

void WriteCommandLineText(const DWORD dwStdHandle, const CString &rstrText)
{
	if (rstrText.IsEmpty())
		return;

	HANDLE hOutput = ::GetStdHandle(dwStdHandle);
	if (hOutput == NULL || hOutput == INVALID_HANDLE_VALUE)
		return;

	DWORD dwWritten = 0;
	if (::GetFileType(hOutput) == FILE_TYPE_CHAR) {
		(void)::WriteConsole(hOutput, rstrText.GetString(), static_cast<DWORD>(rstrText.GetLength()), &dwWritten, NULL);
		return;
	}

	const CStringA strUtf8(CW2A(rstrText, CP_UTF8));
	(void)::WriteFile(hOutput, strUtf8.GetString(), static_cast<DWORD>(strUtf8.GetLength()), &dwWritten, NULL);
}

void ReportCommandLineText(const DWORD dwStdHandle, const CString &rstrText)
{
	WriteCommandLineText(dwStdHandle, rstrText);
	::OutputDebugString(rstrText);
}

void ReportCommandLineError(const CString &rstrError, const CString &rstrUsage, const bool bIncludeUsage)
{
	CString strText;
	strText.Format(_T("eMuleBB command-line error: %s\r\n"), (LPCTSTR)rstrError);
	if (bIncludeUsage) {
		strText += _T("\r\n");
		strText += rstrUsage;
	}
	ReportCommandLineText(STD_ERROR_HANDLE, strText);
}

CString BuildStartupErrorLogPath()
{
	CString strLogDirectory(thePrefs.GetMuleDirectory(EMULE_LOGDIR, false));
	if (strLogDirectory.IsEmpty())
		strLogDirectory = thePrefs.GetMuleDirectory(EMULE_CONFIGDIR, false);
	if (strLogDirectory.IsEmpty())
		return CString();

	strLogDirectory = PathHelpers::EnsureTrailingSeparator(strLogDirectory);
	if (!LongPathSeams::CreateDirectoryPath(strLogDirectory))
		return CString();
	return strLogDirectory + STARTUPERRORLOG;
}

bool AppendStartupErrorLogLine(const CString &rstrPath, const CString &rstrMessage)
{
	if (rstrPath.IsEmpty() || rstrMessage.IsEmpty())
		return false;

	CString strLine;
	strLine.Format(_T("%s %s\r\n"), (LPCTSTR)CTime::GetCurrentTime().Format(_T("%Y-%m-%d %H:%M:%S")), (LPCTSTR)rstrMessage);
	const CStringA strUtf8(StrToUtf8(strLine));
	FILE *fp = LongPathSeams::OpenFileStreamDenyWriteLongPath(rstrPath, _T("ab"));
	if (fp == NULL)
		return false;

	const bool bWritten = fwrite(strUtf8.GetString(), 1, static_cast<size_t>(strUtf8.GetLength()), fp) == static_cast<size_t>(strUtf8.GetLength());
	const bool bClosed = fclose(fp) == 0;
	return bWritten && bClosed;
}

LPCTSTR GetStoragePlacementRiskLogName(const LongPathSeams::StoragePlacementRisk eRisk)
{
	switch (eRisk) {
	case LongPathSeams::StoragePlacementRisk::NetworkShare:
		return _T("network share");
	case LongPathSeams::StoragePlacementRisk::RemovableDrive:
		return _T("removable drive");
	case LongPathSeams::StoragePlacementRisk::None:
	default:
		return _T("");
	}
}

bool HasStartupStorageWarningKey(const std::vector<CString> &rKeys, const CString &rstrKey)
{
	for (const CString &rExistingKey : rKeys) {
		if (rExistingKey == rstrKey)
			return true;
	}
	return false;
}

int HandleHeadlessWebServerCertificateCommandLine(const AppCommandLineSeams::SParseResult &rCommandLine)
{
	WebServerCertificate::SGenerationRequest request = WebServerCertificate::BuildDefaultLocalRequest(
		rCommandLine.strKeyFile,
		rCommandLine.strCertFile);
	if (!rCommandLine.astrCertDnsNames.empty() || !rCommandLine.astrCertIpAddresses.empty()) {
		request.astrDnsNames.clear();
		request.astrIpAddresses.clear();
		request.astrDnsNames = rCommandLine.astrCertDnsNames;
		request.astrIpAddresses = rCommandLine.astrCertIpAddresses;
	}
	return WebServerCertificate::CreateSelfSignedCertificate(request) == 0 ? 0 : 1;
}

std::string CommandLineUtf8(const CString &rstrValue)
{
	return std::string(CW2A(rstrValue, CP_UTF8));
}

nlohmann::json BuildMetadataVariantJson(const MediaMetadataFallbackSeams::SVariantSummary &rVariant)
{
	const MediaMetadataFallbackSeams::SBasicMediaInfo &info = rVariant.info;
	return nlohmann::json{
		{"name", CommandLineUtf8(rVariant.strName)},
		{"succeeded", rVariant.bSucceeded},
		{"status", CommandLineUtf8(rVariant.strStatus)},
		{"facts", nlohmann::json{
			{"container", CommandLineUtf8(info.strContainer)},
			{"durationSeconds", info.fDurationSec > 0.0 ? nlohmann::json(info.fDurationSec) : nlohmann::json(nullptr)},
			{"videoStreams", info.iVideoStreams},
			{"audioStreams", info.iAudioStreams},
			{"videoCodec", CommandLineUtf8(info.strVideoCodec)},
			{"audioCodec", CommandLineUtf8(info.strAudioCodec)},
			{"width", info.uWidth != 0 ? nlohmann::json(info.uWidth) : nlohmann::json(nullptr)},
			{"height", info.uHeight != 0 ? nlohmann::json(info.uHeight) : nlohmann::json(nullptr)},
			{"videoBitrate", info.uVideoBitrate != 0 ? nlohmann::json(info.uVideoBitrate) : nlohmann::json(nullptr)},
			{"audioBitrate", info.uAudioBitrate != 0 ? nlohmann::json(info.uAudioBitrate) : nlohmann::json(nullptr)},
			{"audioChannels", info.uAudioChannels != 0 ? nlohmann::json(info.uAudioChannels) : nlohmann::json(nullptr)},
			{"audioSampleRate", info.uAudioSampleRate != 0 ? nlohmann::json(info.uAudioSampleRate) : nlohmann::json(nullptr)}
		}}
	};
}

int HandleHeadlessMediaMetadataCommandLine(const AppCommandLineSeams::SParseResult &rCommandLine)
{
	const CString strInputPath(rCommandLine.strMetadataInputFile);
	struct _stati64 fileStatus = {};
	if (_tstati64(strInputPath, &fileStatus) != 0 || (fileStatus.st_mode & _S_IFREG) == 0) {
		CString strError;
		strError.Format(_T("Metadata diagnostic input is not a readable file: %s\r\n"), (LPCTSTR)strInputPath);
		ReportCommandLineText(STD_ERROR_HANDLE, strError);
		return 2;
	}

	const EMFileSize ullInputSize(static_cast<uint64>(fileStatus.st_size));
	const std::vector<MediaMetadataFallbackSeams::SVariantSummary> variants = ProbeMediaMetadataVariants(strInputPath, ullInputSize);
	const MediaMetadataFallbackSeams::SVariantSummary *pReference = NULL;
	for (const MediaMetadataFallbackSeams::SVariantSummary &rVariant : variants) {
		if (rVariant.bSucceeded) {
			pReference = &rVariant;
			break;
		}
	}

	std::vector<CString> aFindings;
	if (pReference != NULL) {
		for (const MediaMetadataFallbackSeams::SVariantSummary &rVariant : variants)
			MediaMetadataFallbackSeams::AppendDivergenceFindings(*pReference, rVariant, aFindings);
	}

	nlohmann::json variantJson = nlohmann::json::array();
	for (const MediaMetadataFallbackSeams::SVariantSummary &rVariant : variants)
		variantJson.push_back(BuildMetadataVariantJson(rVariant));
	nlohmann::json findingsJson = nlohmann::json::array();
	for (const CString &rFinding : aFindings)
		findingsJson.push_back(CommandLineUtf8(rFinding));

	const nlohmann::json report{
		{"schema", "emulebb.mediaMetadataDiagnostic.v1"},
		{"input", nlohmann::json{
			{"path", CommandLineUtf8(strInputPath)},
			{"sizeBytes", static_cast<std::uint64_t>(fileStatus.st_size)}
		}},
		{"referenceVariant", pReference != NULL ? nlohmann::json(CommandLineUtf8(pReference->strName)) : nlohmann::json(nullptr)},
		{"variants", variantJson},
		{"divergenceFindings", findingsJson},
		{"hasDivergence", !aFindings.empty()},
		{"ok", pReference != NULL}
	};
	const std::string strReport = report.dump(2) + "\n";
	if (rCommandLine.strMetadataOutputFile.IsEmpty()) {
		ReportCommandLineText(STD_OUTPUT_HANDLE, CString(CA2W(strReport.c_str(), CP_UTF8)));
		return pReference != NULL ? 0 : 1;
	}

	CFile outputFile;
	if (!outputFile.Open(rCommandLine.strMetadataOutputFile, CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite)) {
		CString strError;
		strError.Format(_T("Could not write metadata diagnostic output: %s\r\n"), (LPCTSTR)rCommandLine.strMetadataOutputFile);
		ReportCommandLineText(STD_ERROR_HANDLE, strError);
		return 2;
	}
	outputFile.Write(strReport.data(), static_cast<UINT>(strReport.size()));
	return pReference != NULL ? 0 : 1;
}

BindStartupPolicy::CBindStartupPolicyText GetBindStartupPolicyText()
{
	BindStartupPolicy::CBindStartupPolicyText text;
	text.strAnyInterface = GetResString(IDS_BIND_ANY_INTERFACE);
	text.strInterfaceNotFoundFormat = GetResString(IDS_BIND_STARTUP_INTERFACE_NOT_FOUND_FMT);
	text.strInterfaceNameAmbiguousFormat = GetResString(IDS_BIND_STARTUP_INTERFACE_AMBIGUOUS_FMT);
	text.strInterfaceHasNoAddressFormat = GetResString(IDS_BIND_STARTUP_INTERFACE_NO_ADDRESS_FMT);
	text.strAddressNotFoundOnInterfaceFormat = GetResString(IDS_BIND_STARTUP_INTERFACE_ADDRESS_MISSING_FMT);
	text.strAddressNotFoundFormat = GetResString(IDS_BIND_STARTUP_ADDRESS_MISSING_FMT);
	return text;
}

struct SMonitoredRootWatcher
{
	CString strRootPath;
	HANDLE hFileWatch = INVALID_HANDLE_VALUE;
	HANDLE hDirectoryWatch = INVALID_HANDLE_VALUE;
};

CString MakeMonitoredDirectoryKey(const CString &rstrDirectory)
{
	CString strKey(PathHelpers::EnsureTrailingSeparator(PathHelpers::CanonicalizePath(PathHelpers::StripExtendedLengthPrefix(rstrDirectory))));
	strKey.MakeLower();
	return strKey;
}

bool IsMonitoredDirectoryKeyWithinKey(const CString &rstrRootKey, const CString &rstrCandidateKey)
{
	return rstrCandidateKey.GetLength() >= rstrRootKey.GetLength()
		&& _tcsncmp(rstrRootKey, rstrCandidateKey, rstrRootKey.GetLength()) == 0;
}

bool HasEquivalentRootPath(const CStringList &rRoots, const CString &rRootPath)
{
	const CString strRootKey(MakeMonitoredDirectoryKey(rRootPath));
	for (POSITION pos = rRoots.GetHeadPosition(); pos != NULL;) {
		if (MakeMonitoredDirectoryKey(rRoots.GetNext(pos)) == strRootKey)
			return true;
	}
	return false;
}

SMonitoredSharedRootJournalState* FindMonitoredSharedRootJournalState(std::vector<SMonitoredSharedRootJournalState> &rStates, const CString &rRootPath)
{
	const CString strRootKey(MakeMonitoredDirectoryKey(rRootPath));
	for (size_t i = 0; i < rStates.size(); ++i) {
		if (MakeMonitoredDirectoryKey(rStates[i].strRootPath) == strRootKey)
			return &rStates[i];
	}
	return NULL;
}

bool RemoveMonitoredSharedRootJournalState(std::vector<SMonitoredSharedRootJournalState> &rStates, const CString &rRootPath)
{
	const CString strRootKey(MakeMonitoredDirectoryKey(rRootPath));
	for (std::vector<SMonitoredSharedRootJournalState>::iterator it = rStates.begin(); it != rStates.end(); ++it) {
		if (MakeMonitoredDirectoryKey(it->strRootPath) == strRootKey) {
			rStates.erase(it);
			return true;
		}
	}
	return false;
}

bool IsCurrentProcessElevated()
{
	HANDLE hToken = NULL;
	if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &hToken))
		return false;

	TOKEN_ELEVATION elevation = {};
	DWORD dwLength = 0;
	const BOOL bResult = ::GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &dwLength);
	::CloseHandle(hToken);
	return bResult && elevation.TokenIsElevated != 0;
}

void PruneMonitoredSharedRootJournalStates(std::vector<SMonitoredSharedRootJournalState> &rStates, const CStringList &rConfiguredRoots)
{
	for (std::vector<SMonitoredSharedRootJournalState>::iterator it = rStates.begin(); it != rStates.end();) {
		if (!HasEquivalentRootPath(rConfiguredRoots, it->strRootPath))
			it = rStates.erase(it);
		else
			++it;
	}
}

bool TryCaptureMonitoredSharedRootJournalState(const CString &rRootPath, SMonitoredSharedRootJournalState &rState)
{
	LongPathSeams::NtfsJournalVolumeState volumeState = {};
	if (!LongPathSeams::TryGetLocalNtfsJournalVolumeState(PathHelpers::TrimTrailingSeparator(rRootPath), volumeState))
		return false;

	rState.strRootPath = PathHelpers::CanonicalizeDirectoryPath(rRootPath);
	rState.ullUsnJournalId = volumeState.ullUsnJournalId;
	rState.llCheckpointUsn = volumeState.llNextUsn;
	rState.bHasTrustedJournal = true;
	return true;
}

void CloseMonitoredRootWatcher(SMonitoredRootWatcher &rWatcher)
{
	if (rWatcher.hFileWatch != INVALID_HANDLE_VALUE) {
		VERIFY(::FindCloseChangeNotification(rWatcher.hFileWatch));
		rWatcher.hFileWatch = INVALID_HANDLE_VALUE;
	}
	if (rWatcher.hDirectoryWatch != INVALID_HANDLE_VALUE) {
		VERIFY(::FindCloseChangeNotification(rWatcher.hDirectoryWatch));
		rWatcher.hDirectoryWatch = INVALID_HANDLE_VALUE;
	}
}

void CloseMonitoredRootWatchers(std::vector<SMonitoredRootWatcher> &rWatchers)
{
	for (size_t i = 0; i < rWatchers.size(); ++i)
		CloseMonitoredRootWatcher(rWatchers[i]);
	rWatchers.clear();
}

bool TryOpenMonitoredRootWatcherHandle(const CString &rRootPath, const DWORD dwNotifyFilter, HANDLE &rhWatchHandle)
{
	rhWatchHandle = ::FindFirstChangeNotification(LongPathSeams::PreparePathForLongPath(PathHelpers::TrimTrailingSeparator(rRootPath)).c_str(), TRUE, dwNotifyFilter);
	return rhWatchHandle != INVALID_HANDLE_VALUE;
}

bool TryCreateMonitoredRootWatcher(const CString &rRootPath, SMonitoredRootWatcher &rWatcher)
{
	rWatcher = SMonitoredRootWatcher{};
	rWatcher.strRootPath = PathHelpers::CanonicalizeDirectoryPath(rRootPath);
	if (!TryOpenMonitoredRootWatcherHandle(rWatcher.strRootPath, kMonitoredSharedFileWatchMask, rWatcher.hFileWatch))
		return false;
	if (!TryOpenMonitoredRootWatcherHandle(rWatcher.strRootPath, kMonitoredSharedDirectoryWatchMask, rWatcher.hDirectoryWatch)) {
		CloseMonitoredRootWatcher(rWatcher);
		return false;
	}
	return true;
}

void LogStartupConfigBackupResult()
{
	const ConfigStartupBackup::StartupConfigBackupResult &rResult = ConfigStartupBackup::GetLastStartupConfigBackupResult();
	if (!rResult.bAttempted)
		return;

	if (rResult.bCreated) {
		Log(_T("Startup config backup created: %s (%u files, %u directories)."),
			(LPCTSTR)rResult.strBackupDirectory,
			rResult.uCopiedFiles,
			rResult.uCopiedDirectories);
	} else if (rResult.bSkippedExisting) {
		Log(_T("Startup config backup already exists for today: %s"), (LPCTSTR)rResult.strBackupDirectory);
	} else if (rResult.bCopyFailed) {
		Log(_T("Startup config backup failed for %s (error %lu)."),
			(LPCTSTR)rResult.strFailedPath,
			rResult.dwLastError);
	}

	if (rResult.uPrunedDirectories > 0)
		Log(_T("Startup config backup pruned %u old backup directories."), rResult.uPrunedDirectories);
	if (rResult.bPruneFailed)
		Log(_T("Startup config backup pruning failed for %s (error %lu)."),
			(LPCTSTR)rResult.strFailedPath,
			rResult.dwLastError);
}

void AddUniqueDirectoryPath(CStringList &rList, const CString &rDirectory)
{
	if (!SharedDirectoryOps::ListContainsEquivalentPath(rList, rDirectory))
		rList.AddTail(rDirectory);
}

bool ReconcileMonitoredSharedRoot(const CString &rRootPath, const CStringList &rSharedDirs, const CStringList &rOwnedDirs, const CStringList &rMonitoredRoots, SMonitoredSharedDirectoryUpdate &rUpdate)
{
	bool bChanged = false;
	CStringList currentSubtreeDirs;
	CStringList currentMonitorRoots;
	SharedDirectoryOps::AddMonitoredSharedRoot(currentMonitorRoots, currentSubtreeDirs, rRootPath, [](const CString &rstrDirectory) -> bool {
		return thePrefs.IsShareableDirectory(rstrDirectory);
	});

	for (POSITION pos = currentMonitorRoots.GetHeadPosition(); pos != NULL;) {
		const CString strCurrentRoot(currentMonitorRoots.GetNext(pos));
		if (SharedDirectoryOps::ListContainsEquivalentPath(rUpdate.liNewMonitoredRoots, strCurrentRoot)
			|| SharedDirectoryOps::ListContainsEquivalentPath(rMonitoredRoots, strCurrentRoot)
			|| !SharedDirectoryOps::ListContainsEquivalentPath(currentSubtreeDirs, strCurrentRoot))
		{
			continue;
		}

		rUpdate.liNewMonitoredRoots.AddTail(strCurrentRoot);
		bChanged = true;
	}

	for (POSITION pos = currentSubtreeDirs.GetHeadPosition(); pos != NULL;) {
		const CString strCurrent(currentSubtreeDirs.GetNext(pos));
		if (SharedDirectoryOps::ListContainsEquivalentPath(rOwnedDirs, strCurrent)
			&& SharedDirectoryOps::ListContainsEquivalentPath(rSharedDirs, strCurrent))
		{
			continue;
		}

		AddUniqueDirectoryPath(rUpdate.liNewDirectories, strCurrent);
		bChanged = true;
	}

	if (!thePrefs.GetKeepUnavailableFixedSharedDirs()) {
		const CString strRootKey(MakeMonitoredDirectoryKey(rRootPath));
		for (POSITION pos = rOwnedDirs.GetHeadPosition(); pos != NULL;) {
			const CString strOwned(rOwnedDirs.GetNext(pos));
			if (!IsMonitoredDirectoryKeyWithinKey(strRootKey, MakeMonitoredDirectoryKey(strOwned)))
				continue;
			if (SharedDirectoryOps::ListContainsEquivalentPath(currentSubtreeDirs, strOwned))
				continue;

			AddUniqueDirectoryPath(rUpdate.liRemovedDirectories, strOwned);
			bChanged = true;
		}
	}

	return bChanged;
}

bool LoadMonitoredSharedRootJournalStateFile(const CString &rstrFullPath, std::vector<SMonitoredSharedRootJournalState> &rOutStates)
{
	rOutStates.clear();
	const bool bIsUnicodeFile = IsUnicodeFile(rstrFullPath);
	CSafeBufferedFile file;
	if (!LongPathSeams::OpenFile(file, rstrFullPath, CFile::modeRead | CFile::shareDenyWrite | (bIsUnicodeFile ? CFile::typeBinary : 0)))
		return true;

	try {
		if (bIsUnicodeFile)
			file.Seek(sizeof(WORD), CFile::begin);

		CString strLine;
		while (file.CStdioFile::ReadString(strLine)) {
			strLine.Trim(_T(" \t\r\n"));
			if (strLine.IsEmpty())
				continue;

			const int iFirstTab = strLine.Find(_T('\t'));
			const int iSecondTab = iFirstTab >= 0 ? strLine.Find(_T('\t'), iFirstTab + 1) : -1;
			if (iFirstTab <= 0 || iSecondTab <= iFirstTab + 1)
				continue;

			SMonitoredSharedRootJournalState state = {};
			state.strRootPath = PathHelpers::CanonicalizeDirectoryPath(strLine.Left(iFirstTab));
			if (_stscanf_s(strLine.Mid(iFirstTab + 1, iSecondTab - iFirstTab - 1), _T("%I64u"), &state.ullUsnJournalId) != 1)
				continue;
			if (_stscanf_s(strLine.Mid(iSecondTab + 1), _T("%I64d"), &state.llCheckpointUsn) != 1)
				continue;
			if (state.strRootPath.IsEmpty() || state.ullUsnJournalId == 0 || state.llCheckpointUsn <= 0)
				continue;
			state.bHasTrustedJournal = true;

			SMonitoredSharedRootJournalState *pExistingState = FindMonitoredSharedRootJournalState(rOutStates, state.strRootPath);
			if (pExistingState != NULL)
				*pExistingState = state;
			else
				rOutStates.push_back(state);
		}
	} catch (CFileException *ex) {
		ASSERT(0);
		ex->Delete();
		return false;
	}
	file.Close();
	return true;
}

bool SaveMonitoredSharedRootJournalStateFile(const CString &rstrFullPath, const std::vector<SMonitoredSharedRootJournalState> &rStates)
{
	const CString strTempPath(rstrFullPath + _T(".tmp"));
	bool bTempFileCreated = false;
	bool bSaveSucceeded = false;
	CSafeBufferedFile file;
	if (!LongPathSeams::OpenFile(file, strTempPath, CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite | CFile::typeBinary))
		return false;
	bTempFileCreated = true;

	try {
		static const WORD wBOM = u'\xFEFF';
		file.Write(&wBOM, sizeof wBOM);
		for (size_t i = 0; i < rStates.size(); ++i) {
			if (!SharedDirectoryMonitorSeams::ShouldPersistJournalState(rStates[i].bHasTrustedJournal, rStates[i].ullUsnJournalId, rStates[i].llCheckpointUsn))
				continue;
			CString strLine;
			strLine.Format(_T("%s\t%I64u\t%I64d\r\n"), (LPCTSTR)rStates[i].strRootPath, rStates[i].ullUsnJournalId, rStates[i].llCheckpointUsn);
			file.CStdioFile::WriteString(strLine);
		}
		file.Close();
		bSaveSucceeded = LongPathSeams::MoveFileEx(strTempPath, rstrFullPath, SharedDirectoryMonitorSeams::GetJournalReplaceFlags()) != 0;
	} catch (CFileException *ex) {
		if (thePrefs.GetVerbose())
			AddDebugLogLine(true, _T("Failed to save %s%s"), (LPCTSTR)rstrFullPath, (LPCTSTR)CExceptionStrDash(*ex));
		ex->Delete();
		file.Abort();
	}
	if (SharedDirectoryMonitorSeams::ShouldDeleteJournalTempFile(bTempFileCreated, bSaveSucceeded))
		(void)LongPathSeams::DeleteFileIfExists(strTempPath);
	return bSaveSucceeded;
}
}

namespace
{
CString GetSkinProfileResourcePath(const CString &rstrSkinProfile, LPCTSTR pszSection, LPCTSTR pszResourceName)
{
	if (rstrSkinProfile.IsEmpty() || pszSection == NULL || pszResourceName == NULL)
		return CString();

	const CString strSkinResource(ShellUiHelpers::GetProfileString(pszSection, pszResourceName, NULL, rstrSkinProfile));
	if (strSkinResource.IsEmpty())
		return CString();

	return ShellUiHelpers::ResolveSkinResourcePath(rstrSkinProfile, strSkinResource);
}

#if EMULE_COMPILED_STARTUP_PROFILING
constexpr ULONGLONG kStartupTraceMicrosPerSecond = 1000000ui64;
constexpr LPCTSTR kStartupProfileTraceFileName = _T("startup-profile.trace.json");

struct SStartupTraceDescriptor
{
	CString strCategory;
	CString strStableId;
	bool bFinalizeTrace = false;
};

bool StartsWithNoCase(const CString &rstrText, LPCTSTR pszPrefix)
{
	if (pszPrefix == NULL)
		return false;

	const int iPrefixLength = static_cast<int>(_tcslen(pszPrefix));
	return rstrText.GetLength() >= iPrefixLength && rstrText.Left(iPrefixLength).CompareNoCase(pszPrefix) == 0;
}

CString NormalizeStartupTracePhaseName(LPCTSTR pszPhase)
{
	CString strNormalized(pszPhase != NULL ? pszPhase : _T(""));
	if (StartsWithNoCase(strNormalized, _T("BuildSharedDirectoryTree item "))) {
		const int iColonPos = strNormalized.Find(_T(':'));
		const int iEqualsPos = strNormalized.Find(_T('='));
		if (iColonPos >= 0)
			strNormalized = strNormalized.Left(iColonPos);
		else if (iEqualsPos >= 0)
			strNormalized = strNormalized.Left(iEqualsPos);
	}

	const int iParenPos = strNormalized.Find(_T(" ("));
	if (iParenPos >= 0 && strNormalized.Right(1) == _T(")"))
		strNormalized = strNormalized.Left(iParenPos);

	return strNormalized.Trim();
}

CString BuildStableTraceIdFromPhaseName(const CString &rstrPhaseName)
{
	CString strId(rstrPhaseName);
	strId.MakeLower();

	CString strSanitized;
	bool bPendingSeparator = false;
	for (int i = 0; i < strId.GetLength(); ++i) {
		const TCHAR ch = strId[i];
		if ((ch >= _T('a') && ch <= _T('z')) || (ch >= _T('0') && ch <= _T('9'))) {
			if (bPendingSeparator && !strSanitized.IsEmpty() && strSanitized.Right(1) != _T("."))
				strSanitized.AppendChar(_T('.'));
			strSanitized.AppendChar(ch);
			bPendingSeparator = false;
		} else {
			bPendingSeparator = !strSanitized.IsEmpty();
		}
	}

	while (!strSanitized.IsEmpty() && strSanitized.Right(1) == _T("."))
		strSanitized.Truncate(strSanitized.GetLength() - 1);
	if (strSanitized.IsEmpty())
		strSanitized = _T("startup.phase");
	return strSanitized;
}

SStartupTraceDescriptor DescribeStartupTracePhase(LPCTSTR pszPhase)
{
	const CString strNormalized(NormalizeStartupTracePhaseName(pszPhase));
	SStartupTraceDescriptor descriptor;
	descriptor.strStableId = BuildStableTraceIdFromPhaseName(strNormalized);
	descriptor.strCategory = _T("startup.phase");

	if (strNormalized == _T("InitInstance: logging and profile setup")) {
		descriptor.strStableId = _T("startup.logging_profile_setup");
		descriptor.strCategory = _T("startup.init");
	} else if (strNormalized == _T("StartupTimer complete")) {
		descriptor.strStableId = _T("startup.complete");
		descriptor.strCategory = _T("startup.lifecycle");
	} else if (strNormalized == _T("StartupTimer finalize: start deferred shared hashing")) {
		descriptor.strStableId = _T("shared.hashing.deferred_start");
		descriptor.strCategory = _T("startup.lifecycle");
	} else if (strNormalized == _T("Construct CSharedFileList")) {
		descriptor.strStableId = _T("shared.scan.construct");
		descriptor.strCategory = _T("shared.scan");
	} else if (strNormalized == _T("shared.scan.complete")) {
		descriptor.strStableId = _T("shared.scan.complete");
		descriptor.strCategory = _T("shared.scan");
	} else if (strNormalized == _T("shared.model.populated")) {
		descriptor.strStableId = _T("shared.model.populated");
		descriptor.strCategory = _T("shared.model");
	} else if (strNormalized == _T("shared.tree.populated")) {
		descriptor.strStableId = _T("shared.tree.populated");
		descriptor.strCategory = _T("shared.tree");
	} else if (strNormalized == _T("ui.shared_files_ready")) {
		descriptor.strStableId = _T("ui.shared_files_ready");
		descriptor.strCategory = _T("ui.readiness");
		descriptor.bFinalizeTrace = true;
	} else if (strNormalized == _T("ui.shared_files_hashing_done")) {
		descriptor.strStableId = _T("ui.shared_files_hashing_done");
		descriptor.strCategory = _T("ui.readiness");
		descriptor.bFinalizeTrace = true;
	} else if (strNormalized == _T("BuildSharedDirectoryTree done")) {
		descriptor.strStableId = _T("shared.tree.build");
		descriptor.strCategory = _T("shared.tree");
	} else if (strNormalized == _T("Construct CUploadQueue")) {
		descriptor.strStableId = _T("broadband.upload_queue.ready");
		descriptor.strCategory = _T("broadband.lifecycle");
	} else if (strNormalized == _T("Construct UploadBandwidthThrottler")) {
		descriptor.strStableId = _T("broadband.throttler.ready");
		descriptor.strCategory = _T("broadband.lifecycle");
	} else if (strNormalized == _T("Construct CUploadDiskIOThread")) {
		descriptor.strStableId = _T("broadband.upload_disk_io.ready");
		descriptor.strCategory = _T("broadband.lifecycle");
	} else if (strNormalized == _T("Construct CPartFileWriteThread")) {
		descriptor.strStableId = _T("broadband.partfile_write.ready");
		descriptor.strCategory = _T("broadband.lifecycle");
	} else if (strNormalized == _T("PerfLog startup")) {
		descriptor.strStableId = _T("startup.perflog.start");
		descriptor.strCategory = _T("startup.init");
	} else if (StartsWithNoCase(strNormalized, _T("shared.hash.file."))) {
		descriptor.strStableId = strNormalized;
		descriptor.strCategory = _T("shared.hash");
	} else if (StartsWithNoCase(strNormalized, _T("broadband."))) {
		descriptor.strStableId = strNormalized;
		descriptor.strCategory = _T("broadband.lifecycle");
	} else if (StartsWithNoCase(strNormalized, _T("Construct "))) {
		descriptor.strCategory = _T("startup.construct");
	} else if (StartsWithNoCase(strNormalized, _T("StartupTimer stage "))) {
		descriptor.strCategory = _T("startup.timer");
	} else if (StartsWithNoCase(strNormalized, _T("StartupTimer "))) {
		descriptor.strCategory = _T("startup.lifecycle");
	} else if (StartsWithNoCase(strNormalized, _T("CStatisticsDlg::OnInitDialog "))) {
		descriptor.strCategory = _T("ui.statistics");
	} else if (StartsWithNoCase(strNormalized, _T("CemuleDlg::OnInitDialog "))) {
		descriptor.strCategory = _T("ui.init");
	} else if (StartsWithNoCase(strNormalized, _T("CemuleDlg::ShowStartupProgress")) || StartsWithNoCase(strNormalized, _T("CemuleDlg::DestroyStartupProgress"))) {
		descriptor.strCategory = _T("ui.startup_progress");
	} else if (StartsWithNoCase(strNormalized, _T("CemuleDlg::OnKickIdle "))) {
		descriptor.strCategory = _T("startup.idle");
	} else if (StartsWithNoCase(strNormalized, _T("BuildSharedDirectoryTree "))) {
		descriptor.strCategory = _T("shared.tree");
	} else if (StartsWithNoCase(strNormalized, _T("CSharedDirsTreeCtrl::"))) {
		descriptor.strCategory = _T("shared.tree.control");
	} else if (StartsWithNoCase(strNormalized, _T("CSharedFilesWnd::"))) {
		descriptor.strCategory = _T("shared.window");
	} else if (StartsWithNoCase(strNormalized, _T("CSharedFilesCtrl::"))) {
		descriptor.strCategory = _T("shared.list");
	} else if (StartsWithNoCase(strNormalized, _T("CemuleDlg::DoModal lifetime"))) {
		descriptor.strCategory = _T("ui.lifecycle");
	}

	return descriptor;
}

bool ShouldFlushStartupProfileAfterCounter(LPCTSTR pszCounterName)
{
	return pszCounterName != NULL
		&& _tcscmp(pszCounterName, _T("shared.hash.currently_hashing")) == 0;
}

CString GetStartupCounterCategory(LPCTSTR pszCounterName)
{
	const CString strCounterName(pszCounterName != NULL ? pszCounterName : _T(""));
	if (StartsWithNoCase(strCounterName, _T("shared.hash.")))
		return _T("shared.hash.counter");
	if (StartsWithNoCase(strCounterName, _T("shared.")))
		return _T("shared.counter");
	if (StartsWithNoCase(strCounterName, _T("broadband.")))
		return _T("broadband.config");
	return _T("startup.counter");
}

ULONGLONG QueryPerformanceCounterValue()
{
	LARGE_INTEGER nCounter;
	return ::QueryPerformanceCounter(&nCounter) ? static_cast<ULONGLONG>(nCounter.QuadPart) : 0ui64;
}

ULONGLONG QueryPerformanceFrequencyValue()
{
	LARGE_INTEGER nFrequency;
	return ::QueryPerformanceFrequency(&nFrequency) ? static_cast<ULONGLONG>(nFrequency.QuadPart) : 0ui64;
}

ULONGLONG ConvertQpcTicksToMicroseconds(const ULONGLONG ullTicks, const ULONGLONG ullFrequency)
{
	if (ullFrequency == 0)
		return 0;
	return (ullTicks * kStartupTraceMicrosPerSecond) / ullFrequency;
}

std::string EscapeJsonUtf8(LPCTSTR pszText)
{
	CStringA strUtf8(CT2A(pszText != NULL ? pszText : _T(""), CP_UTF8));
	std::string strEscaped;
	strEscaped.reserve(strUtf8.GetLength() + 8);
	for (int i = 0; i < strUtf8.GetLength(); ++i) {
		const unsigned char ch = static_cast<unsigned char>(strUtf8[i]);
		switch (ch) {
		case '\\':
			strEscaped += "\\\\";
			break;
		case '"':
			strEscaped += "\\\"";
			break;
		case '\b':
			strEscaped += "\\b";
			break;
		case '\f':
			strEscaped += "\\f";
			break;
		case '\n':
			strEscaped += "\\n";
			break;
		case '\r':
			strEscaped += "\\r";
			break;
		case '\t':
			strEscaped += "\\t";
			break;
		default:
			if (ch < 0x20) {
				char szBuffer[7];
				sprintf_s(szBuffer, "\\u%04x", ch);
				strEscaped += szBuffer;
			} else {
				strEscaped.push_back(static_cast<char>(ch));
			}
		}
	}
	return strEscaped;
}

void AppendTraceField(std::string *pstrJson, const char *pszKey, LPCTSTR pszValue)
{
	if (pstrJson == NULL || pszKey == NULL)
		return;

	*pstrJson += "\"";
	*pstrJson += pszKey;
	*pstrJson += "\":\"";
	*pstrJson += EscapeJsonUtf8(pszValue);
	*pstrJson += "\"";
}

void AppendTraceMetadataEvent(std::string *pstrJson, const char *pszName, DWORD dwProcessId, DWORD dwThreadId, LPCTSTR pszValue)
{
	if (pstrJson == NULL || pszName == NULL)
		return;

	*pstrJson += "{\"ph\":\"M\",\"pid\":";
	*pstrJson += std::to_string(dwProcessId);
	*pstrJson += ",\"tid\":";
	*pstrJson += std::to_string(dwThreadId);
	*pstrJson += ",\"name\":\"";
	*pstrJson += pszName;
	*pstrJson += "\",\"args\":{\"name\":\"";
	*pstrJson += EscapeJsonUtf8(pszValue);
	*pstrJson += "\"}}";
}

std::string BuildStartupTraceEventJson(const SStartupProfileTraceEvent &rEvent, DWORD dwProcessId, DWORD dwThreadId)
{
	std::string strJson("{");
	strJson += "\"pid\":";
	strJson += std::to_string(dwProcessId);
	strJson += ",\"tid\":";
	strJson += std::to_string(dwThreadId);
	strJson += ",\"cat\":\"";
	strJson += EscapeJsonUtf8(rEvent.strCategory);
	strJson += "\",\"name\":\"";
	strJson += EscapeJsonUtf8(rEvent.strName);
	strJson += "\",\"ts\":";
	strJson += std::to_string(rEvent.ullTimestampUs);

	switch (rEvent.eType) {
	case SStartupProfileTraceEvent::EType::Complete:
		strJson += ",\"ph\":\"X\",\"dur\":";
		strJson += std::to_string(rEvent.ullDurationUs);
		strJson += ",\"args\":{";
		AppendTraceField(&strJson, "phase_id", rEvent.strStableId);
		strJson += "}}";
		break;
	case SStartupProfileTraceEvent::EType::Counter:
		strJson += ",\"ph\":\"C\",\"args\":{";
		AppendTraceField(&strJson, "counter_id", rEvent.strStableId);
		strJson += ",\"";
		strJson += EscapeJsonUtf8(rEvent.strCounterValueKey);
		strJson += "\":";
		strJson += std::to_string(rEvent.ullCounterValue);
		strJson += "}}";
		break;
	default:
		strJson += ",\"ph\":\"i\",\"s\":\"p\",\"args\":{";
		AppendTraceField(&strJson, "phase_id", rEvent.strStableId);
		strJson += "}}";
		break;
	}

	return strJson;
}
#endif

}


CLogFile theLog;
CLogFile theVerboseLog;
bool g_bLowColorDesktop = false;

//#define USE_16COLOR_ICONS

///////////////////////////////////////////////////////////////////////////////
// C-RTL Memory Debug Support
//
#ifdef _DEBUG
static CMemoryState oldMemState, newMemState, diffMemState;

using CrtAllocHookNoexcept = int (__cdecl *)(int, void*, size_t, int, long, const unsigned char*, int) noexcept;

CrtAllocHookNoexcept g_pfnPrevCrtAllocHook = NULL;
CMap<const unsigned char*, const unsigned char*, UINT, UINT> g_allocations;
int eMuleAllocHook(int mode, void *pUserData, size_t nSize, int nBlockUse, long lRequest, const unsigned char *pszFileName, int nLine) noexcept;

// Cannot use a CString for that memory - it will be unavailable on application termination!
static TCHAR s_szCrtDebugReportFilePath[MAX_PATH] = {};

static LPCTSTR GetCrtDebugReportFilePath()
{
	if (s_szCrtDebugReportFilePath[0] == _T('\0'))
		_tcsncpy_s(s_szCrtDebugReportFilePath, LogArtifactNames::CrtDebugLogFileName(), _TRUNCATE);
	return s_szCrtDebugReportFilePath;
}
#endif //_DEBUG

bool CemuleApp::CanWritePartMetFiles(LPCTSTR pszPath, const bool bForceRefresh, const bool bBypassDiskSpaceFloor)
{
	if (pszPath == NULL || pszPath[0] == _T('\0'))
		return true;

	if (bBypassDiskSpaceFloor)
		return true;

	CString strVolumeRoot;
	if (!TryResolvePartMetWriteGuardVolume(pszPath, bForceRefresh, strVolumeRoot)) {
		const uint64 nRequiredBytes = theApp.downloadqueue != NULL
			? theApp.downloadqueue->GetRequiredFreeDiskSpaceForPath(pszPath)
			: thePrefs.GetEffectiveMinFreeDiskSpaceForPath(pszPath);
		if (nRequiredBytes == 0)
			return true;

		QueueDebugLogLineEx(LOG_WARNING, _T("Part.met disk-space guard blocked metadata writes for \"%s\" because the target volume could not be resolved (need at least %I64u bytes free).")
			, pszPath
			, nRequiredBytes);
		return false;
	}

	const uint64 nFreeBytes = GetFreeDiskSpaceX(strVolumeRoot);
	const uint64 nRequiredBytes = theApp.downloadqueue != NULL
		? theApp.downloadqueue->GetRequiredFreeDiskSpaceForPath(pszPath)
		: thePrefs.GetEffectiveMinFreeDiskSpaceForPath(pszPath);
	const PartFilePersistenceSeams::PartMetWriteGuardDecision refreshedDecision = PartFilePersistenceSeams::ResolvePartMetWriteGuard(false, false, bForceRefresh, nFreeBytes, nRequiredBytes);
	const bool bCanWrite = refreshedDecision.CanWrite;

	if (!bCanWrite) {
		QueueDebugLogLineEx(LOG_WARNING, _T("Part.met disk-space guard blocked metadata writes on \"%s\" (%I64u bytes free, need at least %I64u).")
			, (LPCTSTR)strVolumeRoot
			, nFreeBytes
			, nRequiredBytes);
	}

	return bCanWrite;
}

bool CemuleApp::TryResolvePartMetWriteGuardVolume(LPCTSTR pszPath, const bool bForceRefresh, CString &rstrVolumeRoot)
{
	rstrVolumeRoot.Empty();
	if (pszPath == NULL || pszPath[0] == _T('\0'))
		return false;

	CString strPathKey(pszPath);
	strPathKey.MakeLower();

	{
		CSingleLock lock(&m_partMetWriteGuardLock, TRUE);
		if (bForceRefresh)
			m_aPartMetWriteGuardPathToVolume.RemoveKey(strPathKey);
		else if (m_aPartMetWriteGuardPathToVolume.Lookup(strPathKey, rstrVolumeRoot) && !rstrVolumeRoot.IsEmpty())
			return true;
	}

	CString strResolvedVolumeRoot;
	if (!TryGetVolumeIdentityPath(pszPath, strResolvedVolumeRoot))
		return false;

	{
		CSingleLock lock(&m_partMetWriteGuardLock, TRUE);
		m_aPartMetWriteGuardPathToVolume.SetAt(strPathKey, strResolvedVolumeRoot);
	}
	rstrVolumeRoot = strResolvedVolumeRoot;
	return true;
}

void CemuleApp::InvalidatePartMetWriteGuardCache(LPCTSTR pszPath)
{
	CSingleLock lock(&m_partMetWriteGuardLock, TRUE);
	if (pszPath == NULL || pszPath[0] == _T('\0')) {
		m_aPartMetWriteGuardPathToVolume.RemoveAll();
		return;
	}

	CString strVolumeRoot;
	CString strPathKey(pszPath);
	strPathKey.MakeLower();
	if (m_aPartMetWriteGuardPathToVolume.Lookup(strPathKey, strVolumeRoot)) {
		m_aPartMetWriteGuardPathToVolume.RemoveKey(strPathKey);
		return;
	}
}


///////////////////////////////////////////////////////////////////////////////
// Heap Corruption Detection
//
// Enables heap termination-on-corruption when the platform supports it.
//
#ifndef HeapEnableTerminationOnCorruption
#define HeapEnableTerminationOnCorruption (HEAP_INFORMATION_CLASS)1
#endif//!HeapEnableTerminationOnCorruption

static void InitHeapCorruptionDetection()
{
	::HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0);
}


struct SLogItem
{
	UINT uFlags;
	CTime timestamp;
	CString line;
};

constexpr size_t kMaxQueuedDebugLogEntries = 4096;
constexpr size_t kMaxQueuedLogEntries = 2048;

static void CALLBACK myErrHandler(const Kademlia::CKademliaError *error)
{
	CString msg;
	msg.Format(_T("\r\nError 0x%08X : %hs\r\n"), error->m_iErrorCode, error->m_szErrorDescription);
	if (!theApp.IsClosing())
		theApp.QueueDebugLogLine(false, _T("%s"), (LPCTSTR)msg);
}

static void CALLBACK myDebugAndLogHandler(LPCSTR lpMsg)
{
	if (!theApp.IsClosing())
		theApp.QueueDebugLogLine(false, _T("%hs"), lpMsg);
}

static void CALLBACK myLogHandler(LPCSTR lpMsg)
{
	if (!theApp.IsClosing())
		theApp.QueueLogLine(false, _T("%hs"), lpMsg);
}

static const UINT UWM_ARE_YOU_EMULE = RegisterWindowMessage(EMULE_GUID);

BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType) noexcept;

///////////////////////////////////////////////////////////////////////////////
// CemuleApp

BEGIN_MESSAGE_MAP(CemuleApp, CWinApp)
	ON_COMMAND(ID_HELP, OnHelp)
END_MESSAGE_MAP()

CemuleApp::CemuleApp(LPCTSTR lpszAppName)
	: CWinApp(lpszAppName)
	, emuledlg()
	, ipfilterUpdater()
	, geolocation()
	, m_ullComCtrlVer(MAKEDLLVERULL(4, 0, 0, 0))
	, m_iDfltImageListColorFlags(ILC_COLOR)
	, m_app_state(APP_STATE_STARTING)
	, m_hSystemImageList()
	, m_sizSmallSystemIcon(16, 16)
	, m_hBigSystemImageList()
	, m_sizBigSystemIcon(32, 32)
	, m_strDefaultFontFaceName(_T("MS Shell Dlg 2"))
	, m_uDroppedDebugLogEntries()
	, m_uDroppedLogEntries()
	, m_dwPublicIP()
	, m_strStartupBindBlockReason()
	, m_strStartupErrorLogPath()
	, m_bGuardClipboardPrompt()
	, m_bAutoStart()
	, m_bStartupBindBlocked()
	, m_bStartupComplete()
	, m_bStartupErrorLogOpened()
	, m_pEarlyStartupProgressDlg()
	, m_bStandbyOff()
#if EMULE_COMPILED_STARTUP_PROFILING
	, m_bStartupProfilingEnabled(::GetEnvironmentVariable(_T("EMULE_STARTUP_PROFILE"), NULL, 0) > 0)
	, m_bStartupProfileStartupComplete()
	, m_bStartupProfileCompleted()
	, m_ullStartupProfileBeginQpc()
	, m_ullStartupProfileFrequency()
#endif
	, m_pSharedDirectoryMonitorThread(NULL)
	, m_hSharedDirectoryMonitorStopEvent(NULL)
	, m_hSharedDirectoryMonitorWakeEvent(NULL)
{
	// Initialize Windows security features.
	InitHeapCorruptionDetection();

	// This does not seem to work well with multithreading, although there is no reason why it should not.
	//_set_sbh_threshold(768);

	srand((unsigned)time(NULL));

// MOD Note: Do not change this part - Merkur

	// this is the "base" version number <major>.<minor>.<update>.<build>
	m_dwProductVersionMS = MAKELONG(CemuleApp::m_nVersionMin, CemuleApp::m_nVersionMjr);
	m_dwProductVersionLS = MAKELONG(CemuleApp::m_nVersionBld, CemuleApp::m_nVersionUpd);

	// create a public mod release string while keeping the upstream base version for protocol checks
	ASSERT(CemuleApp::m_nVersionUpd + 'a' <= 'f');
	m_strCurVersionLongDbg.Format(_T("%s %s (base %u.%u%c.%u)"), MOD_RELEASE_PRODUCT_NAME, MOD_RELEASE_VERSION_TEXT, CemuleApp::m_nVersionMjr, CemuleApp::m_nVersionMin, _T('a') + CemuleApp::m_nVersionUpd, CemuleApp::m_nVersionBld);
#if defined( _DEBUG) || defined(_DEVBUILD)
	m_strCurVersionLong = m_strCurVersionLongDbg;
#else
	m_strCurVersionLong = MOD_RELEASE_VERSION_TEXT;
#endif
	m_strCurVersionLong += CemuleApp::m_sPlatform;

#if defined( _DEBUG)
	m_strCurVersionLong += _T(" DEBUG");
#endif
#ifdef _DEVBUILD
	m_strCurVersionLong += _T(" DEVBUILD");
#endif

	// create the protocol version number
	CString strTmp;
	strTmp.Format(_T("0x%lu"), m_dwProductVersionMS);
	VERIFY(_stscanf_s(strTmp, _T("0x%x"), &m_uCurVersionShort) == 1);
	ASSERT(m_uCurVersionShort < 0x99);
// MOD Note: end

}

void CemuleApp::RecordStartupError(LPCTSTR pszMessage)
{
	if (pszMessage == NULL || pszMessage[0] == _T('\0'))
		return;

	const CString strMessage(pszMessage);
	m_aStartupErrorLines.push_back(strMessage);

	CString strDebugLine;
	strDebugLine.Format(_T("eMule startup error: %s\r\n"), (LPCTSTR)strMessage);
	::OutputDebugString(strDebugLine);

	if (m_strStartupErrorLogPath.IsEmpty())
		m_strStartupErrorLogPath = BuildStartupErrorLogPath();
	if (!m_strStartupErrorLogPath.IsEmpty())
		(void)AppendStartupErrorLogLine(m_strStartupErrorLogPath, strMessage);
}

void CemuleApp::FlushStartupErrorsToLog()
{
	if (m_aStartupErrorLines.empty())
		return;

	LogError(LOG_STATUSBAR, _T("Recoverable startup errors were logged; eMule will continue with fallback paths where possible."));
	for (const CString &rstrLine : m_aStartupErrorLines)
		LogError(LOG_STATUSBAR, _T("Startup: %s"), (LPCTSTR)rstrLine);

	if (!m_bStartupErrorLogOpened && !m_strStartupErrorLogPath.IsEmpty() && LongPathSeams::PathExists(m_strStartupErrorLogPath)) {
		m_bStartupErrorLogOpened = true;
		ShellOpenFile(m_strStartupErrorLogPath);
	}

	m_aStartupErrorLines.clear();
}

void CemuleApp::WarnAboutStartupStoragePlacement()
{
	std::vector<CString> aWarningKeys;
	const auto warnIfRisky = [&](LPCTSTR pszRole, LPCTSTR pszPath) {
		if (pszRole == NULL || pszRole[0] == _T('\0') || pszPath == NULL || pszPath[0] == _T('\0'))
			return;

		const LongPathSeams::StoragePlacementProbeResult placement = LongPathSeams::ClassifyStoragePlacement(pszPath);
		if (placement.eRisk == LongPathSeams::StoragePlacementRisk::None)
			return;

		CString strKey;
		strKey.Format(_T("%s|%u|%s"), pszRole, static_cast<unsigned int>(placement.eRisk), placement.strInputPath.c_str());
		strKey.MakeLower();
		if (HasStartupStorageWarningKey(aWarningKeys, strKey))
			return;
		aWarningKeys.push_back(strKey);

		QueueLogLineEx(LOG_WARNING, _T("%s is on a %s (storage root %s): %s"),
			pszRole,
			GetStoragePlacementRiskLogName(placement.eRisk),
			placement.strVolumeRoot.c_str(),
			pszPath);
	};

	warnIfRisky(_T("eMule profile directory"), thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	for (INT_PTR i = 0; i < thePrefs.GetTempDirCount(); ++i)
		warnIfRisky(_T("Temporary Files directory"), thePrefs.GetTempDir(i));
	warnIfRisky(_T("Incoming Files directory"), thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
}

#if EMULE_COMPILED_STARTUP_PROFILING
void CemuleApp::ResetStartupProfile()
{
	if (!m_bStartupProfilingEnabled)
		return;

	CSingleLock lock(&m_startupProfileLock, TRUE);
	m_bStartupProfileStartupComplete = false;
	m_bStartupProfileCompleted = false;
	m_aStartupProfileTraceEvents.clear();
	m_ullStartupProfileFrequency = QueryPerformanceFrequencyValue();
	m_ullStartupProfileBeginQpc = QueryPerformanceCounterValue();
	m_strStartupProfilePath = thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + kStartupProfileTraceFileName;
	(void)LongPathSeams::DeleteFile(m_strStartupProfilePath);
}

ULONGLONG CemuleApp::GetStartupProfileTimestampUs() const
{
	if (!m_bStartupProfilingEnabled || m_ullStartupProfileFrequency == 0 || m_ullStartupProfileBeginQpc == 0)
		return 0;

	const ULONGLONG ullNowQpc = QueryPerformanceCounterValue();
	if (ullNowQpc <= m_ullStartupProfileBeginQpc)
		return 0;

	return ConvertQpcTicksToMicroseconds(ullNowQpc - m_ullStartupProfileBeginQpc, m_ullStartupProfileFrequency);
}

ULONGLONG CemuleApp::GetStartupProfileElapsedUs(const ULONGLONG ullStartTimestampUs) const
{
	const ULONGLONG ullNowUs = GetStartupProfileTimestampUs();
	return (ullNowUs >= ullStartTimestampUs) ? (ullNowUs - ullStartTimestampUs) : 0;
}

void CemuleApp::FinalizeStartupProfileTrace()
{
	if (!m_bStartupProfilingEnabled)
		return;

	CSingleLock lock(&m_startupProfileLock, TRUE);
	m_bStartupProfileCompleted = true;
	(void)WriteStartupProfileTrace();
}

void CemuleApp::FlushStartupProfileTrace()
{
	if (!m_bStartupProfilingEnabled)
		return;

	CSingleLock lock(&m_startupProfileLock, TRUE);
	(void)WriteStartupProfileTrace();
}

bool CemuleApp::WriteStartupProfileTrace() const
{
	if (m_strStartupProfilePath.IsEmpty())
		return false;

	FILE *fp = LongPathSeams::OpenFileStreamDenyWriteLongPath(m_strStartupProfilePath, _T("wb"));
	if (fp == NULL)
		return false;

	std::string strJson;
	strJson.reserve(32768);
	strJson += "{\n  \"displayTimeUnit\": \"ms\",\n  \"traceEvents\": [\n    ";
	AppendTraceMetadataEvent(&strJson, "process_name", ::GetCurrentProcessId(), ::GetCurrentThreadId(), _T("eMule startup"));
	strJson += ",\n    ";
	AppendTraceMetadataEvent(&strJson, "thread_name", ::GetCurrentProcessId(), ::GetCurrentThreadId(), _T("main"));
	for (size_t i = 0; i < m_aStartupProfileTraceEvents.size(); ++i) {
		strJson += ",\n    ";
		strJson += BuildStartupTraceEventJson(m_aStartupProfileTraceEvents[i], ::GetCurrentProcessId(), ::GetCurrentThreadId());
	}
	strJson += "\n  ]\n}\n";

	const size_t uWritten = fwrite(strJson.data(), 1, strJson.size(), fp);
	fclose(fp);
	return uWritten == strJson.size();
}

void CemuleApp::AppendStartupProfileLine(LPCTSTR pszPhase, const ULONGLONG ullDurationUs, ULONGLONG ullAbsoluteUs)
{
	if (!m_bStartupProfilingEnabled || pszPhase == NULL || pszPhase[0] == _T('\0'))
		return;

	const SStartupTraceDescriptor descriptor(DescribeStartupTracePhase(pszPhase));
	if (ullAbsoluteUs == static_cast<ULONGLONG>(-1))
		ullAbsoluteUs = GetStartupProfileTimestampUs();

	CSingleLock lock(&m_startupProfileLock, TRUE);
	SStartupProfileTraceEvent event;
	event.strName = pszPhase;
	event.strCategory = descriptor.strCategory;
	event.strStableId = descriptor.strStableId;
	event.eType = (ullDurationUs == 0) ? SStartupProfileTraceEvent::EType::Instant : SStartupProfileTraceEvent::EType::Complete;
	event.ullTimestampUs = ullAbsoluteUs;
	event.ullDurationUs = ullDurationUs;
	m_aStartupProfileTraceEvents.emplace_back(event);
	if (descriptor.strStableId == _T("startup.complete"))
		m_bStartupProfileStartupComplete = true;
	lock.Unlock();

	if (descriptor.bFinalizeTrace)
		FinalizeStartupProfileTrace();
}

void CemuleApp::AppendStartupProfileCounter(LPCTSTR pszCounterName, const ULONGLONG ullValue, LPCTSTR pszValueKey)
{
	if (!m_bStartupProfilingEnabled || pszCounterName == NULL || pszCounterName[0] == _T('\0'))
		return;

	const bool bFlushTrace = ShouldFlushStartupProfileAfterCounter(pszCounterName);
	CSingleLock lock(&m_startupProfileLock, TRUE);
	SStartupProfileTraceEvent event;
	event.strName = pszCounterName;
	event.strCategory = GetStartupCounterCategory(pszCounterName);
	event.strStableId = pszCounterName;
	event.strCounterValueKey = (pszValueKey != NULL && pszValueKey[0] != _T('\0')) ? pszValueKey : _T("value");
	event.eType = SStartupProfileTraceEvent::EType::Counter;
	event.ullTimestampUs = GetStartupProfileTimestampUs();
	event.ullCounterValue = ullValue;
	m_aStartupProfileTraceEvents.emplace_back(event);
	lock.Unlock();

	if (bFlushTrace)
		FlushStartupProfileTrace();
}
#endif


CemuleApp theApp(_T("eMule"));


// Workaround for bugged 'AfxSocketTerm' (needed at least for MFC 7.0 - 14.14)
static void __cdecl __AfxSocketTerm() noexcept
{
	_AFX_SOCK_STATE *pState = _afxSockState.GetData();
	if (pState->m_pfnSockTerm != NULL) {
		VERIFY(WSACleanup() == 0);
		pState->m_pfnSockTerm = NULL;
	}
}

static BOOL InitWinsock2(WSADATA *lpwsaData)
{
	_AFX_SOCK_STATE *pState = _afxSockState.GetData();
	if (pState->m_pfnSockTerm == NULL) {
		// initialize Winsock library
		WSADATA wsaData;
		if (lpwsaData == NULL)
			lpwsaData = &wsaData;
		static const WORD wVersionRequested = MAKEWORD(2, 2);
		int nResult = WSAStartup(wVersionRequested, lpwsaData);
		if (nResult != 0)
			return FALSE;
		if (lpwsaData->wVersion != wVersionRequested) {
			WSACleanup();
			return FALSE;
		}
		// setup for termination of sockets
		pState->m_pfnSockTerm = &AfxSocketTerm;
	}
#ifndef _AFXDLL
	//BLOCK: setup maps and lists specific to socket state
	{
		_AFX_SOCK_THREAD_STATE *pThreadState = _afxSockThreadState;
		if (pThreadState->m_pmapSocketHandle == NULL)
			pThreadState->m_pmapSocketHandle = new CMapPtrToPtr;
		if (pThreadState->m_pmapDeadSockets == NULL)
			pThreadState->m_pmapDeadSockets = new CMapPtrToPtr;
		if (pThreadState->m_plistSocketNotifications == NULL)
			pThreadState->m_plistSocketNotifications = new CPtrList;
	}
#endif
	return TRUE;
}

// CemuleApp Initialisierung

BOOL CemuleApp::InitInstance()
{
#ifdef _DEBUG
	// set Floating Point Processor to throw several exceptions, in particular the 'Floating point divide by zero'
	UINT uEmCtrlWord = _control87(0, 0) & _MCW_EM;
	_control87(uEmCtrlWord & ~(/*_EM_INEXACT |*/ _EM_UNDERFLOW | _EM_OVERFLOW | _EM_ZERODIVIDE | _EM_INVALID), _MCW_EM);

	// output all ASSERT messages to debug device
	_CrtSetReportMode(_CRT_ASSERT, _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_REPORT_MODE) | _CRTDBG_MODE_DEBUG);
#endif
	const AppCommandLineSeams::SParseResult commandLine = AppCommandLineSeams::ParseTokens(TokenizeCurrentCommandLine());
	if (commandLine.eMode == AppCommandLineSeams::EMode::Help) {
		ReportCommandLineText(STD_OUTPUT_HANDLE, commandLine.strUsage);
		::ExitProcess(0);
	}
	if (commandLine.eMode == AppCommandLineSeams::EMode::Invalid) {
		ReportCommandLineError(commandLine.strError, commandLine.strUsage, true);
		::ExitProcess(2);
	}

	CString strStartupConfigError;
	if (!InitializeStartupConfigBaseDirOverride(commandLine, strStartupConfigError)) {
		ReportCommandLineError(strStartupConfigError, commandLine.strUsage, false);
		::ExitProcess(2);
	}
	if (commandLine.eMode == AppCommandLineSeams::EMode::GenerateWebServerCertificate)
		::ExitProcess(static_cast<UINT>(HandleHeadlessWebServerCertificateCommandLine(commandLine)));
	free((void*)m_pszProfileName);
	const CString &sConfDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	m_pszProfileName = _tcsdup(sConfDir + _T("preferences.ini"));
	if (commandLine.eMode == AppCommandLineSeams::EMode::DiagnoseMediaMetadata)
		::ExitProcess(static_cast<UINT>(HandleHeadlessMediaMetadataCommandLine(commandLine)));

#ifdef _DEBUG
	oldMemState.Checkpoint();
	// Installing that memory debug code works fine in Debug builds when running within VS Debugger,
	// but some other test applications don't like that all....
	//g_pfnPrevCrtAllocHook = _CrtSetAllocHook(&eMuleAllocHook);
#endif
	//afxMemDF = allocMemDF | delayFreeMemDF;


	///////////////////////////////////////////////////////////////////////////
	// Install crash dump creation
	//
	theCrashDumper.uCreateCrashDump = GetProfileInt(_T("eMule"), _T("CreateCrashDump"), 0);
#if !defined(_DEVBUILD)
	if (theCrashDumper.uCreateCrashDump > 0)
#endif
		theCrashDumper.Enable(m_strCurVersionLongDbg, true, sConfDir);

	///////////////////////////////////////////////////////////////////////////
	// Locale initialization -- BE VERY CAREFUL HERE!!!
	//
	_tsetlocale(LC_ALL, _T(""));		// set all categories of locale to user-default ANSI code page obtained from the OS.
	_tsetlocale(LC_NUMERIC, _T("C"));	// set numeric category to 'C'
	//_tsetlocale(LC_CTYPE, _T("C"));		// set character types category to 'C' (VERY IMPORTANT, we need binary string compares!)

	AfxOleInit();
	DetectWin32LongPathsSupportAtStartup();

	if (ProcessCommandline(commandLine))
		return FALSE;

	///////////////////////////////////////////////////////////////////////////
	// Common Controls initialization
	//
	InitCommonControls();
	m_ullComCtrlVer = MAKEDLLVERULL(6, 16, 0, 0);

	m_sizSmallSystemIcon.cx = ::GetSystemMetrics(SM_CXSMICON);
	m_sizSmallSystemIcon.cy = ::GetSystemMetrics(SM_CYSMICON);
	UpdateLargeIconSize();
	UpdateDesktopColorDepth();

	CWinApp::InitInstance();
	AfxEnableControlContainer();
	ShowEarlyStartupProgress();
	UpdateEarlyStartupProgress(3, IDS_STARTUP_PROGRESS_STARTING, IDS_STARTUP_PROGRESS_PREPARING);
	const bool bDailyConfigBackup = GetProfileInt(
		_T("eMule"),
		CPreferences::GetDailyConfigBackupIniKey(),
		CPreferences::GetDefaultDailyConfigBackup() ? 1 : 0) != 0;
	if (bDailyConfigBackup)
		(void)ConfigStartupBackup::RunDailyStartupConfigBackup(sConfDir, ConfigStartupBackupSeams::kDefaultBackupRetentionCount);
	UpdateEarlyStartupProgress(4, IDS_STARTUP_PROGRESS_STARTING, IDS_STARTUP_PROGRESS_PREPARING);

	if (!InitWinsock2(&m_wsaData) && !AfxSocketInit(&m_wsaData)) {
		DestroyEarlyStartupProgress();
		LocMessageBox(IDS_SOCKETS_INIT_FAILED, MB_OK, 0);
		return FALSE;
	}

	atexit(__AfxSocketTerm);

	if (!AfxInitRichEdit5())
		AfxMessageBox(GetResString(IDS_FATAL_NO_RICHEDIT)); // should never happen.

	if (!Kademlia::CKademlia::InitUnicode(AfxGetInstanceHandle())) {
		DestroyEarlyStartupProgress();
		AfxMessageBox(GetResString(IDS_FATAL_KAD_UNICODE_TABLES)); // should never happen.
		return FALSE; // DO *NOT* START !!!
	}

	extern bool SelfTest();
	if (!SelfTest()) {
		DestroyEarlyStartupProgress();
		return FALSE; // DO *NOT* START !!!
	}

	// create & initialize all the important stuff
	UpdateEarlyStartupProgress(6, IDS_STARTUP_PROGRESS_STARTING, IDS_STARTUP_PROGRESS_PREPARING);
	thePrefs.Init();
	UpdateEarlyStartupProgress(10, IDS_STARTUP_PROGRESS_STARTING_SERVICES, IDS_STARTUP_PROGRESS_QUEUING_STAGES);
	RefreshStartupBindBlockState();
	theStats.Init();

	if (thePrefs.GetRTLWindowsLayout())
		EnableRTLWindowsLayout();

#ifdef _DEBUG
	_sntprintf_s(s_szCrtDebugReportFilePath, _countof(s_szCrtDebugReportFilePath), _TRUNCATE, _T("%s%s"), (LPCTSTR)thePrefs.GetMuleDirectory(EMULE_LOGDIR, false), LogArtifactNames::CrtDebugLogFileName());
#endif
	VERIFY(theLog.SetFilePath(thePrefs.GetMuleDirectory(EMULE_LOGDIR, thePrefs.GetLog2Disk()) + LogArtifactNames::MainLogFileName()));
	VERIFY(theVerboseLog.SetFilePath(thePrefs.GetMuleDirectory(EMULE_LOGDIR, false) + LogArtifactNames::VerboseLogFileName()));
	theLog.SetMaxFileSize(thePrefs.GetMaxLogFileSize());
	theLog.SetFileFormat(thePrefs.GetLogFileFormat());
	theVerboseLog.SetMaxFileSize(thePrefs.GetMaxLogFileSize());
	theVerboseLog.SetFileFormat(thePrefs.GetLogFileFormat());
	if (thePrefs.GetLog2Disk()) {
		theLog.Open();
		theLog.Log(_T("\r\n"));
	}
	if (thePrefs.GetDebug2Disk()) {
		theVerboseLog.Open();
		theVerboseLog.Log(_T("\r\n"));
	}
	Log(_T("Starting %s %s"), MOD_RELEASE_PRODUCT_NAME, (LPCTSTR)m_strCurVersionLong);
	LogStartupConfigBackupResult();
#if EMULE_COMPILED_STARTUP_PROFILING
	ResetStartupProfile();
	AppendStartupProfileLine(_T("InitInstance: logging and profile setup"), 0);
#endif
	if (!IsWin32LongPathsEnabled())
		QueueLogLineEx(LOG_WARNING, _T("Windows long-path support is disabled. Enable LongPathsEnabled to avoid failures with overlong paths."));
	if (IsCurrentProcessElevated())
		QueueLogLineEx(LOG_WARNING, _T("eMule is running with administrator privileges. This is not recommended for normal P2P use; restart without elevation unless required."));
	FlushStartupErrorsToLog();
	WarnAboutStartupStoragePlacement();

	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

#if EMULE_COMPILED_STARTUP_PROFILING
	ULONGLONG ullPhaseStart = GetStartupProfileTimestampUs();
#endif
	emuledlg = new CemuleDlg;
#if EMULE_COMPILED_STARTUP_PROFILING
	AppendStartupProfileLine(_T("Construct CemuleDlg"), GetStartupProfileElapsedUs(ullPhaseStart));
#endif
	m_pMainWnd = emuledlg;
	// Barry - Auto-take ed2k links
	if (thePrefs.AutoTakeED2KLinks())
		Ask4RegFix(false, true, false);

	SetAutoStart(thePrefs.GetAutoStart());

	// UPnP Port forwarding
#if EMULE_COMPILED_STARTUP_PROFILING
	ullPhaseStart = GetStartupProfileTimestampUs();
#endif
	m_pUPnPFinder = new CUPnPImplWrapper();
#if EMULE_COMPILED_STARTUP_PROFILING
	AppendStartupProfileLine(_T("Construct UPnP wrapper"), GetStartupProfileElapsedUs(ullPhaseStart));
#endif

	// Highres scheduling gives better resolution for Sleep(...) calls, and timeGetTime() calls
	m_wTimerRes = 0;
	if (thePrefs.GetHighresTimer()) {
		TIMECAPS tc;
		if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) == TIMERR_NOERROR) {
			m_wTimerRes = min(max(tc.wPeriodMin, 1), tc.wPeriodMax);
			if (m_wTimerRes > 0) {
				MMRESULT mmResult = timeBeginPeriod(m_wTimerRes);
				if (thePrefs.GetVerbose()) {
					if (mmResult == TIMERR_NOERROR)
						theApp.QueueDebugLogLine(false, _T("Succeeded to set timer/scheduler resolution to %i ms."), m_wTimerRes);
					else {
						theApp.QueueDebugLogLine(false, _T("Failed to set timer/scheduler resolution to %i ms."), m_wTimerRes);
						m_wTimerRes = 0;
					}
				}
			} else
				theApp.QueueDebugLogLine(false, _T("m_wTimerRes == 0. Not setting timer/scheduler resolution."));
		}
	}

#if EMULE_COMPILED_STARTUP_PROFILING
	ullPhaseStart = GetStartupProfileTimestampUs();
#endif
	clientlist = new CClientList();
#if EMULE_COMPILED_STARTUP_PROFILING
	AppendStartupProfileLine(_T("Construct CClientList"), GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = GetStartupProfileTimestampUs();
#endif
	friendlist = new CFriendList();
#if EMULE_COMPILED_STARTUP_PROFILING
	AppendStartupProfileLine(_T("Construct CFriendList"), GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = GetStartupProfileTimestampUs();
#endif
	searchlist = new CSearchList();
#if EMULE_COMPILED_STARTUP_PROFILING
	AppendStartupProfileLine(_T("Construct CSearchList"), GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = GetStartupProfileTimestampUs();
#endif
	knownfiles = new CKnownFileList();
#if EMULE_COMPILED_STARTUP_PROFILING
	AppendStartupProfileLine(_T("Construct CKnownFileList (known.met/cancelled.met)"), GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = GetStartupProfileTimestampUs();
#endif
	serverlist = new CServerList();
#if EMULE_COMPILED_STARTUP_PROFILING
	AppendStartupProfileLine(_T("Construct CServerList"), GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = GetStartupProfileTimestampUs();
#endif
	serverconnect = new CServerConnect();
#if EMULE_COMPILED_STARTUP_PROFILING
	AppendStartupProfileLine(_T("Construct CServerConnect"), GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = GetStartupProfileTimestampUs();
#endif
	sharedfiles = new CSharedFileList(serverconnect);
#if EMULE_COMPILED_STARTUP_PROFILING
	AppendStartupProfileLine(_T("Construct CSharedFileList (share cache/scan)"), GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = GetStartupProfileTimestampUs();
#endif
	listensocket = new CListenSocket();
#if EMULE_COMPILED_STARTUP_PROFILING
	AppendStartupProfileLine(_T("Construct CListenSocket"), GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = GetStartupProfileTimestampUs();
#endif
	clientudp = new CClientUDPSocket();
#if EMULE_COMPILED_STARTUP_PROFILING
	AppendStartupProfileLine(_T("Construct CClientUDPSocket"), GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = GetStartupProfileTimestampUs();
#endif
	clientcredits = new CClientCreditsList();
#if EMULE_COMPILED_STARTUP_PROFILING
	AppendStartupProfileLine(_T("Construct CClientCreditsList"), GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = GetStartupProfileTimestampUs();
#endif
	downloadqueue = new CDownloadQueue();	// bugfix - do this before creating the upload queue
#if EMULE_COMPILED_STARTUP_PROFILING
	AppendStartupProfileLine(_T("Construct CDownloadQueue"), GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = GetStartupProfileTimestampUs();
#endif
	uploadqueue = new CUploadQueue();
#if EMULE_COMPILED_STARTUP_PROFILING
	AppendStartupProfileLine(_T("Construct CUploadQueue"), GetStartupProfileElapsedUs(ullPhaseStart));
	AppendStartupProfileCounter(_T("broadband.configured_upload_budget_bytes_per_sec"), uploadqueue->GetConfiguredUploadBudgetBytesPerSec(), _T("bytes_per_sec"));
	AppendStartupProfileCounter(_T("broadband.slot_cap"), static_cast<ULONGLONG>(uploadqueue->GetBroadbandSlotCap()), _T("slots"));
	AppendStartupProfileCounter(_T("broadband.target_client_rate_bytes_per_sec"), uploadqueue->GetTargetClientDataRateBroadband(), _T("bytes_per_sec"));
	ullPhaseStart = GetStartupProfileTimestampUs();
#endif
	ipfilter = new CIPFilter();
#if EMULE_COMPILED_STARTUP_PROFILING
	AppendStartupProfileLine(_T("Construct CIPFilter"), GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = GetStartupProfileTimestampUs();
#endif
	ipfilterUpdater = new CIPFilterUpdater();
#if EMULE_COMPILED_STARTUP_PROFILING
	AppendStartupProfileLine(_T("Construct CIPFilterUpdater"), GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = GetStartupProfileTimestampUs();
#endif
	webserver = new CWebServer(); // Web Server [kuchin]
#if EMULE_COMPILED_STARTUP_PROFILING
	AppendStartupProfileLine(_T("Construct CWebServer"), GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = GetStartupProfileTimestampUs();
#endif
	scheduler = new CScheduler();
#if EMULE_COMPILED_STARTUP_PROFILING
	AppendStartupProfileLine(_T("Construct CScheduler"), GetStartupProfileElapsedUs(ullPhaseStart));

	ullPhaseStart = GetStartupProfileTimestampUs();
#endif
	geolocation = new CGeoLocation();
	geolocation->Load();
#if EMULE_COMPILED_STARTUP_PROFILING
	AppendStartupProfileLine(_T("Construct CGeoLocation"), GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = GetStartupProfileTimestampUs();
#endif
	uploadBandwidthThrottler = new UploadBandwidthThrottler();
#if EMULE_COMPILED_STARTUP_PROFILING
	AppendStartupProfileLine(_T("Construct UploadBandwidthThrottler"), GetStartupProfileElapsedUs(ullPhaseStart));

	ullPhaseStart = GetStartupProfileTimestampUs();
#endif
	m_pUploadDiskIOThread = new CUploadDiskIOThread();
#if EMULE_COMPILED_STARTUP_PROFILING
	AppendStartupProfileLine(_T("Construct CUploadDiskIOThread"), GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = GetStartupProfileTimestampUs();
#endif
	m_pPartFileWriteThread = new CPartFileWriteThread();
#if EMULE_COMPILED_STARTUP_PROFILING
	AppendStartupProfileLine(_T("Construct CPartFileWriteThread"), GetStartupProfileElapsedUs(ullPhaseStart));
#endif

	thePerfLog.Startup();
#if EMULE_COMPILED_STARTUP_PROFILING
	AppendStartupProfileLine(_T("PerfLog startup"), 0);
	ullPhaseStart = GetStartupProfileTimestampUs();
#endif
	emuledlg->DoModal();
#if EMULE_COMPILED_STARTUP_PROFILING
	AppendStartupProfileLine(_T("CemuleDlg::DoModal lifetime"), GetStartupProfileElapsedUs(ullPhaseStart));
#endif

	DisableRTLWindowsLayout();

	// Barry - Restore old registry if required
	if (thePrefs.AutoTakeED2KLinks())
		RevertReg();

	::CloseHandle(m_hMutexOneInstance);
#ifdef _DEBUG
	if (g_pfnPrevCrtAllocHook)
		_CrtSetAllocHook(g_pfnPrevCrtAllocHook);

	newMemState.Checkpoint();
	if (diffMemState.Difference(oldMemState, newMemState)) {
		TRACE("Memory usage:\n");
		diffMemState.DumpStatistics();
	}
	//_CrtDumpMemoryLeaks();
#endif //_DEBUG

	ClearDebugLogQueue(true);
	ClearLogQueue(true);

	AddDebugLogLine(DLP_VERYLOW, _T("%hs: returning: FALSE"), __FUNCTION__);
	delete emuledlg;
	emuledlg = NULL;
	return FALSE;
}

int CemuleApp::ExitInstance()
{
	AddDebugLogLine(DLP_VERYLOW, _T("%hs"), __FUNCTION__);
	DestroyEarlyStartupProgress();
	ReleaseStandbyPrevention();
	StopSharedDirectoryMonitor();

	if (m_wTimerRes != 0)
		timeEndPeriod(m_wTimerRes);

	return CWinApp::ExitInstance();
}

UINT AFX_CDECL CemuleApp::SharedDirectoryMonitorThreadProc(LPVOID pParam)
{
	CemuleApp *pApp = static_cast<CemuleApp*>(pParam);
	if (pApp != NULL)
		pApp->RunSharedDirectoryMonitorLoop();
	return 0;
}

bool CemuleApp::LoadSharedDirectoryMonitorJournalState()
{
	return LoadMonitoredSharedRootJournalStateFile(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + MONITOREDSHAREDJOURNALSTATE, m_aSharedDirectoryMonitorJournalStates);
}

bool CemuleApp::SaveSharedDirectoryMonitorJournalState() const
{
	return SaveMonitoredSharedRootJournalStateFile(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + MONITOREDSHAREDJOURNALSTATE, m_aSharedDirectoryMonitorJournalStates);
}

void CemuleApp::StartSharedDirectoryMonitor()
{
	if (m_pSharedDirectoryMonitorThread != NULL)
		return;

	m_aSharedDirectoryMonitorJournalStates.clear();
	(void)LoadSharedDirectoryMonitorJournalState();
	m_hSharedDirectoryMonitorStopEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
	m_hSharedDirectoryMonitorWakeEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!SharedDirectoryMonitorSeams::AreStartupEventsReady(m_hSharedDirectoryMonitorStopEvent, m_hSharedDirectoryMonitorWakeEvent)) {
		SharedDirectoryMonitorSeams::CloseMonitorEvent(m_hSharedDirectoryMonitorStopEvent);
		SharedDirectoryMonitorSeams::CloseMonitorEvent(m_hSharedDirectoryMonitorWakeEvent);
		return;
	}

	m_pSharedDirectoryMonitorThread = AfxBeginThread(&CemuleApp::SharedDirectoryMonitorThreadProc, this, THREAD_PRIORITY_BELOW_NORMAL, 0, CREATE_SUSPENDED);
	if (m_pSharedDirectoryMonitorThread == NULL) {
		SharedDirectoryMonitorSeams::CloseMonitorEvent(m_hSharedDirectoryMonitorStopEvent);
		SharedDirectoryMonitorSeams::CloseMonitorEvent(m_hSharedDirectoryMonitorWakeEvent);
		return;
	}

	// The app owns the CWinThread object so shutdown can wait and release it explicitly.
	m_pSharedDirectoryMonitorThread->m_bAutoDelete = FALSE;
	const DWORD dwResumeResult = m_pSharedDirectoryMonitorThread->ResumeThread();
	if (!SharedDirectoryMonitorSeams::DidResumeMonitorThread(dwResumeResult)) {
		const DWORD dwError = ::GetLastError();
		AddDebugLogLine(false, _T("Shared-directory monitor thread failed to resume (error %lu)."), dwError);
		delete m_pSharedDirectoryMonitorThread;
		m_pSharedDirectoryMonitorThread = NULL;
		SharedDirectoryMonitorSeams::CloseMonitorEvent(m_hSharedDirectoryMonitorStopEvent);
		SharedDirectoryMonitorSeams::CloseMonitorEvent(m_hSharedDirectoryMonitorWakeEvent);
	}
}

void CemuleApp::StopSharedDirectoryMonitor()
{
	if (m_hSharedDirectoryMonitorStopEvent != NULL)
		::SetEvent(m_hSharedDirectoryMonitorStopEvent);
	if (m_hSharedDirectoryMonitorWakeEvent != NULL)
		::SetEvent(m_hSharedDirectoryMonitorWakeEvent);

	SharedDirectoryMonitorSeams::StopWaitResult waitResult = {};
	waitResult.bHadThread = m_pSharedDirectoryMonitorThread != NULL;
	if (m_pSharedDirectoryMonitorThread != NULL) {
		waitResult.dwWaitResult = ::WaitForSingleObject(m_pSharedDirectoryMonitorThread->m_hThread, SharedDirectoryMonitorSeams::kMonitorShutdownWaitMs);
	}
	if (SharedDirectoryMonitorSeams::ShouldLogAbandonedMonitorResources(waitResult)) {
		const DWORD dwError = waitResult.dwWaitResult == WAIT_FAILED ? ::GetLastError() : ERROR_SUCCESS;
		AddDebugLogLine(false, _T("Shared-directory monitor did not exit cleanly during shutdown (wait result %lu, error %lu); leaving live monitor resources owned by the process."), waitResult.dwWaitResult, dwError);
	}
	if (!SharedDirectoryMonitorSeams::ShouldReleaseMonitorResources(waitResult))
		return;
	if (m_pSharedDirectoryMonitorThread != NULL) {
		delete m_pSharedDirectoryMonitorThread;
		m_pSharedDirectoryMonitorThread = NULL;
	}
	(void)SaveSharedDirectoryMonitorJournalState();
	m_aSharedDirectoryMonitorJournalStates.clear();
	SharedDirectoryMonitorSeams::CloseMonitorEvent(m_hSharedDirectoryMonitorStopEvent);
	SharedDirectoryMonitorSeams::CloseMonitorEvent(m_hSharedDirectoryMonitorWakeEvent);
}

void CemuleApp::WakeSharedDirectoryMonitor()
{
	if (m_hSharedDirectoryMonitorWakeEvent != NULL)
		::SetEvent(m_hSharedDirectoryMonitorWakeEvent);
}

void CemuleApp::RunSharedDirectoryMonitorLoop()
{
	std::vector<SMonitoredRootWatcher> aWatchers;
	CStringList liSuppressedRoots;

	const auto postUpdate = [&](SMonitoredSharedDirectoryUpdate &rUpdate) -> void {
		if (emuledlg == NULL || emuledlg->sharedfileswnd == NULL)
			return;
		if (!rUpdate.bReloadSharedFiles
			&& !rUpdate.bForceTreeReload
			&& rUpdate.liNewDirectories.IsEmpty()
			&& rUpdate.liRemovedDirectories.IsEmpty()
			&& rUpdate.liDowngradedRoots.IsEmpty()
			&& rUpdate.liNewMonitoredRoots.IsEmpty())
		{
			return;
		}

		std::unique_ptr<SMonitoredSharedDirectoryUpdate> pPostedUpdate(new SMonitoredSharedDirectoryUpdate);
		for (POSITION pos = rUpdate.liNewDirectories.GetHeadPosition(); pos != NULL;)
			pPostedUpdate->liNewDirectories.AddTail(rUpdate.liNewDirectories.GetNext(pos));
		for (POSITION pos = rUpdate.liRemovedDirectories.GetHeadPosition(); pos != NULL;)
			pPostedUpdate->liRemovedDirectories.AddTail(rUpdate.liRemovedDirectories.GetNext(pos));
		for (POSITION pos = rUpdate.liDowngradedRoots.GetHeadPosition(); pos != NULL;)
			pPostedUpdate->liDowngradedRoots.AddTail(rUpdate.liDowngradedRoots.GetNext(pos));
		for (POSITION pos = rUpdate.liNewMonitoredRoots.GetHeadPosition(); pos != NULL;)
			pPostedUpdate->liNewMonitoredRoots.AddTail(rUpdate.liNewMonitoredRoots.GetNext(pos));
		pPostedUpdate->bForceTreeReload = rUpdate.bForceTreeReload;
		pPostedUpdate->bReloadSharedFiles = rUpdate.bReloadSharedFiles;
		(void)TryPostWorkerUiPayloadMessage(
			emuledlg->sharedfileswnd->GetSafeHwnd(),
			emuledlg->sharedfileswnd->GetWorkerUiClosingFlag(),
			emuledlg->sharedfileswnd->GetWorkerUiPayloadOwnerKey(),
			UM_MONITORED_SHARED_DIR_UPDATE,
			std::move(pPostedUpdate));
	};

	const auto appendDowngradedRoot = [&](SMonitoredSharedDirectoryUpdate &rUpdate, const CString &rRootPath) -> void {
		if (SharedDirectoryOps::ListContainsEquivalentPath(rUpdate.liDowngradedRoots, rRootPath))
			return;
		rUpdate.liDowngradedRoots.AddTail(rRootPath);
		if (!SharedDirectoryOps::ListContainsEquivalentPath(liSuppressedRoots, rRootPath))
			liSuppressedRoots.AddTail(rRootPath);
		rUpdate.bForceTreeReload = true;
		(void)RemoveMonitoredSharedRootJournalState(m_aSharedDirectoryMonitorJournalStates, rRootPath);
	};

	const auto captureBestEffortRootState = [](const CString &rRootPath, SMonitoredSharedRootJournalState &rState) -> bool {
		if (TryCaptureMonitoredSharedRootJournalState(rRootPath, rState))
			return true;
		if (!DirAccsess(rRootPath))
			return false;

		rState = SMonitoredSharedRootJournalState{};
		rState.strRootPath = PathHelpers::CanonicalizeDirectoryPath(rRootPath);
		return true;
	};

	const auto refreshConfiguredRoots = [&](CStringList &rConfiguredRoots, CStringList &rSharedDirs, CStringList &rOwnedDirs) -> void {
		rConfiguredRoots.RemoveAll();
		rSharedDirs.RemoveAll();
		rOwnedDirs.RemoveAll();
		thePrefs.CopyMonitoredSharedRootList(rConfiguredRoots);
		thePrefs.CopySharedDirectoryList(rSharedDirs);
		thePrefs.CopyMonitorOwnedDirectoryList(rOwnedDirs);
	};

	const auto rebuildWatchers = [&](const bool bStartupCatchup, SMonitoredSharedDirectoryUpdate *pInitialUpdate) -> void {
		CStringList configuredRoots;
		CStringList sharedDirs;
		CStringList ownedDirs;
		refreshConfiguredRoots(configuredRoots, sharedDirs, ownedDirs);
		for (POSITION posSuppressed = liSuppressedRoots.GetHeadPosition(); posSuppressed != NULL;) {
			const POSITION posCurrent = posSuppressed;
			const CString strSuppressed(liSuppressedRoots.GetNext(posSuppressed));
			if (!HasEquivalentRootPath(configuredRoots, strSuppressed))
				liSuppressedRoots.RemoveAt(posCurrent);
		}

		const size_t uPreviousStateCount = m_aSharedDirectoryMonitorJournalStates.size();
		PruneMonitoredSharedRootJournalStates(m_aSharedDirectoryMonitorJournalStates, configuredRoots);

		CloseMonitoredRootWatchers(aWatchers);

		SMonitoredSharedDirectoryUpdate localUpdate;
		SMonitoredSharedDirectoryUpdate &rUpdate = pInitialUpdate != NULL ? *pInitialUpdate : localUpdate;
		bool bStateChanged = (m_aSharedDirectoryMonitorJournalStates.size() != uPreviousStateCount);
		const UINT uWatcherCapacity = static_cast<UINT>(SharedDirectoryMonitorSeams::GetMonitorWatcherCapacity());
		UINT uActiveWatchers = 0;

		for (POSITION posRoot = configuredRoots.GetHeadPosition(); posRoot != NULL;) {
			const CString strRoot(configuredRoots.GetNext(posRoot));
			if (SharedDirectoryOps::ListContainsEquivalentPath(liSuppressedRoots, strRoot))
				continue;

			if (!DirAccsess(strRoot)) {
				appendDowngradedRoot(rUpdate, strRoot);
				bStateChanged = true;
				continue;
			}

			SMonitoredSharedRootJournalState *pState = FindMonitoredSharedRootJournalState(m_aSharedDirectoryMonitorJournalStates, strRoot);
			const bool bHadRootState = pState != NULL;
			if (pState == NULL) {
				SMonitoredSharedRootJournalState state = {};
				if (!captureBestEffortRootState(strRoot, state)) {
					appendDowngradedRoot(rUpdate, strRoot);
					bStateChanged = true;
					continue;
				}
				m_aSharedDirectoryMonitorJournalStates.push_back(state);
				pState = &m_aSharedDirectoryMonitorJournalStates.back();
				bStateChanged = true;
			}

			const SharedDirectoryMonitorSeams::EMonitoredRootCatchupMode eCatchupMode =
				SharedDirectoryMonitorSeams::GetStartupCatchupMode(bStartupCatchup, bHadRootState, pState != NULL && pState->bHasTrustedJournal);
			if (eCatchupMode == SharedDirectoryMonitorSeams::EMonitoredRootCatchupMode::FullReconcile) {
				rUpdate.bReloadSharedFiles = true;
				if (ReconcileMonitoredSharedRoot(strRoot, sharedDirs, ownedDirs, configuredRoots, rUpdate))
					rUpdate.bForceTreeReload = true;
				SMonitoredSharedRootJournalState currentState = {};
				if (captureBestEffortRootState(strRoot, currentState)) {
					*pState = currentState;
					bStateChanged = true;
				}
			} else if (eCatchupMode == SharedDirectoryMonitorSeams::EMonitoredRootCatchupMode::JournalDelta) {
				LongPathSeams::NtfsDirectoryJournalState rootJournalState = {};
				LongPathSeams::NtfsJournalVolumeState currentVolumeState = {};
				if (!LongPathSeams::TryGetNtfsDirectoryJournalState(PathHelpers::TrimTrailingSeparator(strRoot), rootJournalState)
					|| !LongPathSeams::TryGetLocalNtfsJournalVolumeState(PathHelpers::TrimTrailingSeparator(strRoot), currentVolumeState))
				{
					rUpdate.bReloadSharedFiles = true;
					if (ReconcileMonitoredSharedRoot(strRoot, sharedDirs, ownedDirs, configuredRoots, rUpdate))
						rUpdate.bForceTreeReload = true;
					SMonitoredSharedRootJournalState currentState = {};
					if (captureBestEffortRootState(strRoot, currentState))
						*pState = currentState;
					bStateChanged = true;
				} else {
					std::unordered_set<LongPathSeams::UsnFileReference, LongPathSeams::UsnFileReferenceHasher> trackedDirectoryRefs;
					trackedDirectoryRefs.insert(rootJournalState.fileReference);
					const CString strRootKey(MakeMonitoredDirectoryKey(strRoot));
					for (POSITION posOwned = ownedDirs.GetHeadPosition(); posOwned != NULL;) {
						const CString strOwned(ownedDirs.GetNext(posOwned));
						if (!IsMonitoredDirectoryKeyWithinKey(strRootKey, MakeMonitoredDirectoryKey(strOwned)))
							continue;

						LongPathSeams::NtfsDirectoryJournalState ownedJournalState = {};
						if (LongPathSeams::TryGetNtfsDirectoryJournalState(PathHelpers::TrimTrailingSeparator(strOwned), ownedJournalState)) {
							trackedDirectoryRefs.insert(ownedJournalState.fileReference);
							continue;
						}

						if (!thePrefs.GetKeepUnavailableFixedSharedDirs())
							AddUniqueDirectoryPath(rUpdate.liRemovedDirectories, strOwned);
					}

					std::unordered_set<LongPathSeams::UsnFileReference, LongPathSeams::UsnFileReferenceHasher> changedDirectoryRefs;
					if (!LongPathSeams::TryCollectChangedDirectoryFileReferences(
							PathHelpers::TrimTrailingSeparator(strRoot),
							pState->ullUsnJournalId,
							pState->llCheckpointUsn,
							trackedDirectoryRefs,
							changedDirectoryRefs))
					{
						rUpdate.bReloadSharedFiles = true;
						if (ReconcileMonitoredSharedRoot(strRoot, sharedDirs, ownedDirs, configuredRoots, rUpdate))
							rUpdate.bForceTreeReload = true;
					} else if (!changedDirectoryRefs.empty()) {
						rUpdate.bReloadSharedFiles = true;
						if (ReconcileMonitoredSharedRoot(strRoot, sharedDirs, ownedDirs, configuredRoots, rUpdate))
							rUpdate.bForceTreeReload = true;
					}

					pState->ullUsnJournalId = currentVolumeState.ullUsnJournalId;
					pState->llCheckpointUsn = currentVolumeState.llNextUsn;
					pState->bHasTrustedJournal = true;
					bStateChanged = true;
					if (!rUpdate.liRemovedDirectories.IsEmpty())
						rUpdate.bForceTreeReload = true;
				}
			}

			if (uActiveWatchers >= uWatcherCapacity) {
				appendDowngradedRoot(rUpdate, strRoot);
				bStateChanged = true;
				continue;
			}

			SMonitoredRootWatcher watcher = {};
			if (!TryCreateMonitoredRootWatcher(strRoot, watcher)) {
				appendDowngradedRoot(rUpdate, strRoot);
				bStateChanged = true;
				continue;
			}

			aWatchers.push_back(watcher);
			++uActiveWatchers;
		}

		if (bStateChanged)
			(void)SaveSharedDirectoryMonitorJournalState();
		if (pInitialUpdate == NULL)
			postUpdate(rUpdate);
	};

	SMonitoredSharedDirectoryUpdate startupUpdate;
	rebuildWatchers(true, &startupUpdate);
	postUpdate(startupUpdate);

	while (true) {
		std::vector<HANDLE> aWaitHandles;
		aWaitHandles.reserve(2 + (aWatchers.size() * 2));
		aWaitHandles.push_back(m_hSharedDirectoryMonitorStopEvent);
		aWaitHandles.push_back(m_hSharedDirectoryMonitorWakeEvent);
		for (size_t i = 0; i < aWatchers.size(); ++i) {
			aWaitHandles.push_back(aWatchers[i].hFileWatch);
			aWaitHandles.push_back(aWatchers[i].hDirectoryWatch);
		}

		if (!SharedDirectoryMonitorSeams::CanWaitForMonitorHandles(aWaitHandles.size())) {
			AddDebugLogLine(false, _T("Shared-directory monitor has an invalid wait-handle count (%Iu); stopping monitor loop."), aWaitHandles.size());
			break;
		}

		const DWORD dwWait = ::WaitForMultipleObjects(static_cast<DWORD>(aWaitHandles.size()), &aWaitHandles[0], FALSE, INFINITE);
		const SharedDirectoryMonitorSeams::MonitorWaitResult waitResult = SharedDirectoryMonitorSeams::ClassifyMonitorWaitResult(dwWait, aWaitHandles.size());
		if (waitResult.eAction == SharedDirectoryMonitorSeams::EMonitorWaitAction::Stop || IsClosing())
			break;
		if (waitResult.eAction == SharedDirectoryMonitorSeams::EMonitorWaitAction::Wake) {
			if (m_hSharedDirectoryMonitorWakeEvent != NULL)
				VERIFY(::ResetEvent(m_hSharedDirectoryMonitorWakeEvent));
			rebuildWatchers(false, NULL);
			continue;
		}
		if (waitResult.eAction == SharedDirectoryMonitorSeams::EMonitorWaitAction::Failed) {
			const DWORD dwError = dwWait == WAIT_FAILED ? ::GetLastError() : ERROR_SUCCESS;
			AddDebugLogLine(false, _T("Shared-directory monitor wait failed (wait result %lu, handles %Iu, error %lu); stopping monitor loop."), dwWait, aWaitHandles.size(), dwError);
			break;
		}
		if (waitResult.eAction != SharedDirectoryMonitorSeams::EMonitorWaitAction::Watcher)
			continue;

		SMonitoredSharedDirectoryUpdate update;
		CStringList liFileEventRoots;
		CStringList liDirectoryEventRoots;
		const size_t uWatcherIndex = waitResult.uWatcherIndex;
		const bool bDirectoryEvent = waitResult.bDirectoryEvent;
		if (uWatcherIndex < aWatchers.size()) {
			SMonitoredRootWatcher &rWatcher = aWatchers[uWatcherIndex];
			HANDLE hWatchHandle = bDirectoryEvent ? rWatcher.hDirectoryWatch : rWatcher.hFileWatch;
			if (hWatchHandle == INVALID_HANDLE_VALUE || !::FindNextChangeNotification(hWatchHandle)) {
				appendDowngradedRoot(update, rWatcher.strRootPath);
				CloseMonitoredRootWatcher(rWatcher);
			} else if (bDirectoryEvent) {
				AddUniqueDirectoryPath(liDirectoryEventRoots, rWatcher.strRootPath);
			} else {
				AddUniqueDirectoryPath(liFileEventRoots, rWatcher.strRootPath);
			}
		}

		CStringList sharedDirs;
		CStringList ownedDirs;
		CStringList monitoredRoots;
		refreshConfiguredRoots(monitoredRoots, sharedDirs, ownedDirs);

		for (POSITION pos = liDirectoryEventRoots.GetHeadPosition(); pos != NULL;) {
			const CString strRoot(liDirectoryEventRoots.GetNext(pos));
			if (!thePrefs.IsMonitoredSharedRootListed(strRoot))
				continue;
			if (!DirAccsess(strRoot)) {
				appendDowngradedRoot(update, strRoot);
				continue;
			}

			(void)ReconcileMonitoredSharedRoot(strRoot, sharedDirs, ownedDirs, monitoredRoots, update);
			update.bForceTreeReload = true;
			update.bReloadSharedFiles = true;

			SMonitoredSharedRootJournalState *pState = FindMonitoredSharedRootJournalState(m_aSharedDirectoryMonitorJournalStates, strRoot);
			SMonitoredSharedRootJournalState currentState = {};
			if (!captureBestEffortRootState(strRoot, currentState)) {
				appendDowngradedRoot(update, strRoot);
				continue;
			}
			if (pState != NULL)
				*pState = currentState;
			else
				m_aSharedDirectoryMonitorJournalStates.push_back(currentState);
		}

		for (POSITION pos = liFileEventRoots.GetHeadPosition(); pos != NULL;) {
			const CString strRoot(liFileEventRoots.GetNext(pos));
			if (SharedDirectoryOps::ListContainsEquivalentPath(liDirectoryEventRoots, strRoot))
				continue;
			if (!thePrefs.IsMonitoredSharedRootListed(strRoot))
				continue;
			update.bReloadSharedFiles = true;

			SMonitoredSharedRootJournalState *pState = FindMonitoredSharedRootJournalState(m_aSharedDirectoryMonitorJournalStates, strRoot);
			SMonitoredSharedRootJournalState currentState = {};
			if (!captureBestEffortRootState(strRoot, currentState)) {
				appendDowngradedRoot(update, strRoot);
				continue;
			}
			if (pState != NULL)
				*pState = currentState;
			else
				m_aSharedDirectoryMonitorJournalStates.push_back(currentState);
		}

		if (!update.liDowngradedRoots.IsEmpty())
			(void)SaveSharedDirectoryMonitorJournalState();
		postUpdate(update);
		if (!update.liDowngradedRoots.IsEmpty())
			rebuildWatchers(false, NULL);
	}

	CloseMonitoredRootWatchers(aWatchers);
}

bool CemuleApp::InitializeStartupConfigBaseDirOverride(
	const AppCommandLineSeams::SParseResult &rCommandLine,
	CString &rstrError)
{
	rstrError.Empty();
	const CString strStartupConfigBaseDir = rCommandLine.strConfigBaseDir;

	if (rCommandLine.bHasConfigBaseDir) {
		const CString strConfigDir(StartupConfigOverride::GetConfigDirectoryFromBaseDir(strStartupConfigBaseDir));
		const CString strLogDir(StartupConfigOverride::GetLogDirectoryFromBaseDir(strStartupConfigBaseDir));
		if (!LongPathSeams::CreateDirectoryPath(strStartupConfigBaseDir, NULL)) {
			rstrError.Format(_T("The -c base directory could not be created: %s"), (LPCTSTR)strStartupConfigBaseDir);
			return false;
		}
		if (!LongPathSeams::CreateDirectoryPath(strConfigDir, NULL)) {
			rstrError.Format(_T("The -c config directory could not be created: %s"), (LPCTSTR)strConfigDir);
			return false;
		}
		if (!LongPathSeams::CreateDirectoryPath(strLogDir, NULL)) {
			rstrError.Format(_T("The -c log directory could not be created: %s"), (LPCTSTR)strLogDir);
			return false;
		}
	}

	m_strStartupConfigBaseDir = strStartupConfigBaseDir;
	if (rCommandLine.bHasConfigBaseDir)
		CPreferences::ResetDefaultDirectoryCache();
	return true;
}

void CemuleApp::RefreshStartupBindBlockState()
{
	m_bStartupBindBlocked = BindStartupPolicy::ShouldBlockSessionNetworking(thePrefs.IsActiveStartupBindBlockEnabled()
		, thePrefs.GetActiveBindInterface(), thePrefs.GetActiveConfiguredBindAddr(), thePrefs.GetActiveBindAddressResolveResult());
	m_strStartupBindBlockReason = BindStartupPolicy::FormatStartupBlockReason(thePrefs.GetActiveBindInterfaceName()
		, thePrefs.GetActiveBindInterface(), thePrefs.GetActiveConfiguredBindAddr(), thePrefs.GetActiveBindAddressResolveResult(), GetBindStartupPolicyText());
}

#ifdef _DEBUG
static int CrtDebugReportCB(int reportType, char *message, int *returnValue) noexcept
{
	FILE *fp = LongPathSeams::OpenFileStreamDenyWriteLongPath(GetCrtDebugReportFilePath(), _T("a"));
	if (fp) {
		time_t tNow = time(NULL);
		TCHAR szTime[40];
		tm tmNow = {};
		if (localtime_s(&tmNow, &tNow) == 0)
			_tcsftime(szTime, _countof(szTime), _T("%H:%M:%S"), &tmNow);
		else
			szTime[0] = _T('\0');
		_ftprintf(fp, _T("%ls  %i  %hs"), szTime, reportType, message);
		fclose(fp);
	}
	*returnValue = 0; // avoid invocation of 'AfxDebugBreak' in ASSERT macros
	return TRUE; // avoid further processing of this debug report message by the CRT
}

// allocation hook - for memory statistics gathering
int eMuleAllocHook(int mode, void *pUserData, size_t nSize, int nBlockUse, long lRequest, const unsigned char *pszFileName, int nLine) noexcept
{
	UINT count;
	if (!g_allocations.Lookup(pszFileName, count))
		count = 0;
	if (mode == _HOOK_ALLOC) {
		_CrtSetAllocHook(g_pfnPrevCrtAllocHook);
		g_allocations[pszFileName] = count + 1;
		_CrtSetAllocHook(&eMuleAllocHook);
	} else if (mode == _HOOK_FREE) {
		_CrtSetAllocHook(g_pfnPrevCrtAllocHook);
		g_allocations[pszFileName] = count - 1;
		_CrtSetAllocHook(&eMuleAllocHook);
	}
	return g_pfnPrevCrtAllocHook(mode, pUserData, nSize, nBlockUse, lRequest, pszFileName, nLine);
}
#endif

void CemuleApp::ShowEarlyStartupProgress()
{
	if (m_pEarlyStartupProgressDlg != NULL)
		return;

	const int iMode = CPreferences::NormalizeLifecycleProgressDialogMode(GetProfileInt(
		_T("eMule"),
		_T("StartupProgressDialog"),
		CPreferences::GetDefaultLifecycleProgressDialogMode()));
	if (iMode == LPDM_OFF)
		return;

	if (iMode == LPDM_WHEN_VISIBLE) {
		const bool bStartMinimized = GetProfileInt(_T("eMule"), _T("StartupMinimized"), 0) != 0;
		if (bStartMinimized || DidWeAutoStart())
			return;
	}

	CLifecycleProgressDlg *pDialog = NULL;
	try {
		pDialog = new CLifecycleProgressDlg(IDS_STARTING_EMULE, IDS_EMULE_IS_STARTING);
	} catch (...) {
		return;
	}
	if (pDialog->Create(IDD_SHUTDOWNPROGRESS, NULL)) {
		m_pEarlyStartupProgressDlg = pDialog;
		pDialog->CenterWindow();
		pDialog->ShowWindow(SW_SHOW);
		pDialog->SetPhase(1, GetResString(IDS_STARTUP_PROGRESS_STARTING), GetResString(IDS_STARTUP_PROGRESS_PREPARING), false);
		PumpLifecycleProgressMessages(pDialog);
#if EMULE_COMPILED_STARTUP_PROFILING
		AppendStartupProfileLine(_T("CemuleApp::ShowEarlyStartupProgress"), 0);
#endif
	} else
		delete pDialog;
}

void CemuleApp::UpdateEarlyStartupProgress(UINT uPercent, UINT uStepStringId, UINT uDetailStringId, bool bMarquee)
{
	if (m_pEarlyStartupProgressDlg == NULL)
		return;

	m_pEarlyStartupProgressDlg->SetPhase(uPercent, GetResString(uStepStringId), GetResString(uDetailStringId), bMarquee);
	PumpLifecycleProgressMessages(m_pEarlyStartupProgressDlg);
}

void CemuleApp::DestroyEarlyStartupProgress()
{
	if (m_pEarlyStartupProgressDlg != NULL) {
		if (m_pEarlyStartupProgressDlg->GetSafeHwnd() != NULL)
			m_pEarlyStartupProgressDlg->DestroyWindow();
		delete m_pEarlyStartupProgressDlg;
		m_pEarlyStartupProgressDlg = NULL;
#if EMULE_COMPILED_STARTUP_PROFILING
		AppendStartupProfileLine(_T("CemuleApp::DestroyEarlyStartupProgress"), 0);
#endif
	}
}

bool CemuleApp::ProcessCommandline(const AppCommandLineSeams::SParseResult &rCommandLine)
{
	bool bIgnoreRunningInstances = rCommandLine.bIgnoreInstances;
#ifdef _DEBUG
	if (rCommandLine.bAssertFile)
		_CrtSetReportHook(CrtDebugReportCB);
#endif
	m_bAutoStart |= rCommandLine.bAutoStart;

	// If we create our TCP listen socket with SO_REUSEADDR, we have to ensure that there are
	// no 2 eMules are running on the same port.
	// NOTE: This will not prevent from some other application using that port!
	UINT uTcpPort = GetProfileInt(_T("eMule"), _T("Port"), DEFAULT_TCP_PORT_OLD);
	CString strMutextName;
	strMutextName.Format(_T("%s:%u"), EMULE_GUID, uTcpPort);
	m_hMutexOneInstance = CreateMutex(NULL, FALSE, strMutextName);
	const DWORD dwMutexLastError = ::GetLastError();

	const CString &command(rCommandLine.strPositional);

	//this code part is to determine special cases when we do add a link to our eMule
	//because in this case it would be nonsense to start another instance!
	bool bAlreadyRunning = false;
	if (bIgnoreRunningInstances
		&& !command.IsEmpty()
		&& (command.Find(_T("://")) > 0 || CCollection::HasCollectionExtention(command)))
	{
		bIgnoreRunningInstances = false;
	}
	HWND maininst = NULL;
	if (!bIgnoreRunningInstances)
		switch (dwMutexLastError) {
		case ERROR_ALREADY_EXISTS:
		case ERROR_ACCESS_DENIED:
			bAlreadyRunning = true;
			EnumWindows(SearchEmuleWindow, (LPARAM)&maininst);
		}

	if (!command.IsEmpty()) {
		if (command.Find(_T("://")) > 0) {
			sendstruct.cbData = static_cast<DWORD>((command.GetLength() + 1) * sizeof(TCHAR));
			sendstruct.dwData = OP_ED2KLINK;
			sendstruct.lpData = const_cast<LPTSTR>((LPCTSTR)command);
			if (maininst) {
				SendMessage(maininst, WM_COPYDATA, (WPARAM)0, (LPARAM)(PCOPYDATASTRUCT)&sendstruct);
				return true;
			}

			m_strPendingLink = command;
		} else if (CCollection::HasCollectionExtention(command)) {
			sendstruct.cbData = static_cast<DWORD>((command.GetLength() + 1) * sizeof(TCHAR));
			sendstruct.dwData = OP_COLLECTION;
			sendstruct.lpData = const_cast<LPTSTR>((LPCTSTR)command);
			if (maininst) {
				SendMessage(maininst, WM_COPYDATA, (WPARAM)0, (LPARAM)(PCOPYDATASTRUCT)&sendstruct);
				return true;
			}

			m_strPendingLink = command;
		} else {
			sendstruct.cbData = static_cast<DWORD>((command.GetLength() + 1) * sizeof(TCHAR));
			sendstruct.dwData = OP_CLCOMMAND;
			sendstruct.lpData = const_cast<LPTSTR>((LPCTSTR)command);
			if (maininst) {
				SendMessage(maininst, WM_COPYDATA, (WPARAM)0, (LPARAM)(PCOPYDATASTRUCT)&sendstruct);
				return true;
			}
			// Don't start if we were invoked with 'exit' command.
			if (command.CompareNoCase(_T("exit")) == 0)
				return true;
		}
	}
	return maininst || bAlreadyRunning;
}

BOOL CALLBACK CemuleApp::SearchEmuleWindow(HWND hWnd, LPARAM lParam) noexcept
{
	DWORD_PTR dwMsgResult;
	LRESULT res = ::SendMessageTimeout(hWnd, UWM_ARE_YOU_EMULE, 0, 0, SMTO_BLOCK | SMTO_ABORTIFHUNG, SEC2MS(10), &dwMsgResult);
	if (res != 0 && dwMsgResult == UWM_ARE_YOU_EMULE) {
		*reinterpret_cast<HWND*>(lParam) = hWnd;
		return FALSE;
	}
	return TRUE;
}


void CemuleApp::UpdateReceivedBytes(uint32 bytesToAdd)
{
	SetTimeOnTransfer();
	theStats.sessionReceivedBytes += bytesToAdd;
}

void CemuleApp::UpdateSentBytes(uint32 bytesToAdd, bool sentToFriend)
{
	SetTimeOnTransfer();
	theStats.sessionSentBytes += bytesToAdd;

	if (sentToFriend)
		theStats.sessionSentBytesToFriend += bytesToAdd;
}

void CemuleApp::SetTimeOnTransfer()
{
	if (theStats.transferStarttime <= 0)
		theStats.transferStarttime = ::GetTickCount64();
}

CString CemuleApp::CreateKadSourceLink(const CAbstractFile *f)
{
	CString strLink;
	if (Kademlia::CKademlia::IsConnected() && theApp.clientlist->GetBuddy() && theApp.IsFirewalled()) {
		Kademlia::CUInt128 id(Kademlia::CKademlia::GetPrefs()->GetKadID());
		strLink.Format(_T("ed2k://|file|%s|%I64u|%s|/|kadsources,%s:%s|/")
			, (LPCTSTR)EncodeUrlUtf8(StripInvalidFilenameChars(f->GetFileName()))
			, (uint64)f->GetFileSize()
			, (LPCTSTR)EncodeBase16(f->GetFileHash(), 16)
			, (LPCTSTR)md4str(thePrefs.GetUserHash()), (LPCTSTR)id.Xor(Kademlia::CUInt128(true)).ToHexString());
	}
	return strLink;
}

//TODO: Move to emule-window
bool CemuleApp::CopyTextToClipboard(const CString &strText)
{
	if (strText.IsEmpty())
		return false;

	HGLOBAL hGlobalT = ::GlobalAlloc(GHND | GMEM_SHARE, (strText.GetLength() + 1) * sizeof(TCHAR));
	if (hGlobalT != NULL) {
		LPTSTR pGlobalT = static_cast<LPTSTR>(::GlobalLock(hGlobalT));
		if (pGlobalT != NULL) {
			_tcscpy_s(pGlobalT, strText.GetLength() + 1, strText);
			::GlobalUnlock(hGlobalT);
		} else {
			::GlobalFree(hGlobalT);
			hGlobalT = NULL;
		}
	}

	CStringA strTextA(strText);
	HGLOBAL hGlobalA = ::GlobalAlloc(GHND | GMEM_SHARE, (strTextA.GetLength() + 1) * sizeof(char));
	if (hGlobalA != NULL) {
		LPSTR pGlobalA = static_cast<LPSTR>(::GlobalLock(hGlobalA));
		if (pGlobalA != NULL) {
			memcpy(pGlobalA, (LPCSTR)strTextA, strTextA.GetLength() + 1);
			::GlobalUnlock(hGlobalA);
		} else {
			::GlobalFree(hGlobalA);
			hGlobalA = NULL;
		}
	}

	if (hGlobalT == NULL && hGlobalA == NULL)
		return false;

	int iCopied = 0;
	if (OpenClipboard(NULL)) {
		if (EmptyClipboard()) {
			if (hGlobalT) {
				if (SetClipboardData(CF_UNICODETEXT, hGlobalT) != NULL)
					++iCopied;
				else {
					::GlobalFree(hGlobalT);
					hGlobalT = NULL;
				}
			}
			if (hGlobalA) {
				if (SetClipboardData(CF_TEXT, hGlobalA) != NULL)
					++iCopied;
				else {
					::GlobalFree(hGlobalA);
					hGlobalA = NULL;
				}
			}
		}
		CloseClipboard();
	}

	if (iCopied == 0) {
		if (hGlobalT)
			::GlobalFree(hGlobalT);
		if (hGlobalA)
			::GlobalFree(hGlobalA);
		return false;
	}

	IgnoreClipboardLinks(strText); // this is so eMule won't think the clipboard has ed2k links for adding
	return true;
}

//TODO: Move to emule-window
CString CemuleApp::CopyTextFromClipboard()
{
	bool bResult = false;
	CString strClipboard;
	if (IsClipboardFormatAvailable(CF_UNICODETEXT) && OpenClipboard(NULL)) {
		HGLOBAL hMem = GetClipboardData(CF_UNICODETEXT);
		if (hMem) {
			LPCWSTR pwsz = (LPCWSTR)::GlobalLock(hMem);
			if (pwsz) {
				strClipboard = pwsz;
				::GlobalUnlock(hMem);
				bResult = true;
			}
		}
		CloseClipboard();
	}
	if (!bResult && IsClipboardFormatAvailable(CF_TEXT) && OpenClipboard(NULL)) {
		HGLOBAL hMem = GetClipboardData(CF_TEXT);
		if (hMem != NULL) {
			LPCSTR lptstr = (LPCSTR)::GlobalLock(hMem);
			if (lptstr != NULL) {
				strClipboard = lptstr;
				::GlobalUnlock(hMem);
			}
		}
		CloseClipboard();
	}
	return strClipboard;
}

void CemuleApp::OnlineSig() // Added By Bouc7
{
	if (!thePrefs.IsOnlineSignatureEnabled())
		return;

	static LPCTSTR const _szFileName = _T("onlinesig.dat");
	const CString &strSigPath(thePrefs.GetMuleDirectory(EMULE_CONFIGBASEDIR) + _szFileName);

	// The 'onlinesig.dat' is potentially read by other applications at more or less frequent intervals.
	//	 -	Set the file sharing mode to allow other processes to read the file while we are writing
	//		it (see also next point).
	//	 -	Try to write the hole file data at once, so other applications are always reading
	//		a consistent amount of file data. C-RTL uses a 4 KB buffer, this is large enough to write
	//		those 2 lines into the onlinesig.dat file with one IO operation.
	//	 -	Although this file is a text file, we set the file mode to 'binary' because of backward
	//		compatibility with older eMule versions.
	CSafeBufferedFile file;
	CFileException fex;
	if (!LongPathSeams::OpenFile(file, strSigPath, CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite | CFile::typeBinary, &fex)) {
		LogError(LOG_STATUSBAR, _T("%s %s%s"), (LPCTSTR)GetResString(IDS_ERROR_SAVEFILE), _szFileName, (LPCTSTR)CExceptionStrDash(fex));
		return;
	}

	try {
		char buffer[20];
		CStringA strBuff;
		if (IsConnected()) {
			file.Write("1|", 2);
			if (serverconnect->IsConnected())
				strBuff = serverconnect->GetCurrentServer()->GetListName();
			else
				strBuff = "Kademlia";
			file.Write(strBuff, strBuff.GetLength());

			file.Write("|", 1);
			if (serverconnect->IsConnected())
				strBuff = serverconnect->GetCurrentServer()->GetAddress();
			else
				strBuff = "0.0.0.0";
			file.Write(strBuff, strBuff.GetLength());

			file.Write("|", 1);
			if (serverconnect->IsConnected()) {
				_itoa_s(serverconnect->GetCurrentServer()->GetPort(), buffer, _countof(buffer), 10);
				file.Write(buffer, (UINT)strlen(buffer));
			} else
				file.Write("0", 1);
		} else
			file.Write("0", 1);
		file.Write("\n", 1);

		snprintf(buffer, _countof(buffer), "%.1f", (float)downloadqueue->GetDatarate() / 1024);
		file.Write(buffer, (UINT)strlen(buffer));
		file.Write("|", 1);

		snprintf(buffer, _countof(buffer), "%.1f", (float)uploadqueue->GetDatarate() / 1024);
		file.Write(buffer, (UINT)strlen(buffer));
		file.Write("|", 1);

		_itoa_s((int)uploadqueue->GetWaitingUserCount(), buffer, _countof(buffer), 10);
		file.Write(buffer, (UINT)strlen(buffer));

		file.Close();
	} catch (CFileException *ex) {
		LogError(LOG_STATUSBAR, _T("%s %s%s"), (LPCTSTR)GetResString(IDS_ERROR_SAVEFILE), _szFileName, (LPCTSTR)CExceptionStrDash(*ex));
		ex->Delete();
	}
} //End Added By Bouc7

void CemuleApp::OnHelp()
{
	if (m_dwPromptContext != 0) {
		// do not re-enter help when the error is failing to launch help
		if (m_dwPromptContext != HID_BASE_PROMPT + AFX_IDP_FAILED_TO_LAUNCH_HELP)
			ShowHelp(m_dwPromptContext);
		return;
	}
	ShowHelp(0, HELP_CONTENTS);
}

void CemuleApp::ShowHelp(UINT uTopic, UINT uCmd)
{
	UNREFERENCED_PARAMETER(uTopic);
	UNREFERENCED_PARAMETER(uCmd);

	const CString strOnlineHelpURL(thePrefs.GetOnlineHelpURL());
	const HINSTANCE hResult = BrowserOpen(strOnlineHelpURL, thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
	if (!OtherFunctionsSeams::DidShellExecuteLaunch(hResult)) {
		QueueDebugLogLineEx(LOG_ERROR, _T("Failed to open online help URL \"%s\" (ShellExecute result %Id)"), (LPCTSTR)strOnlineHelpURL, reinterpret_cast<INT_PTR>(hResult));
	}
}

int CemuleApp::GetFileTypeSystemImageIdx(LPCTSTR pszFilePath, int iLength /* = -1 */, bool bNormalsSize)
{
	DWORD dwFileAttributes;
	if (iLength == -1)
		iLength = (int)_tcslen(pszFilePath);
	const ShellUiHelpers::ShellIconDescriptor iconDescriptor = ShellUiHelpers::DescribeShellIcon(pszFilePath, iLength);
	const LPCTSTR pszCacheExt = iconDescriptor.strCacheKey;
	dwFileAttributes = iconDescriptor.dwFileAttributes;

	// Search extension in "ext->idx" cache.
	LPVOID vData;
	if (bNormalsSize) {
		if (!m_aBigExtToSysImgIdx.Lookup(pszCacheExt, vData)) {
			// Get index for the system's big icon image list
			SHFILEINFO sfi;
			HIMAGELIST hResult = (HIMAGELIST)::SHGetFileInfo(iconDescriptor.strQueryPath, dwFileAttributes, &sfi, sizeof(sfi), SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX);
			if (hResult == 0)
				return 0;
			ASSERT(m_hBigSystemImageList == NULL || m_hBigSystemImageList == hResult);
			m_hBigSystemImageList = hResult;

			// Store icon index in local cache
			m_aBigExtToSysImgIdx[pszCacheExt] = LongToPtr(sfi.iIcon);
			return sfi.iIcon;
		}
	} else if (!m_aExtToSysImgIdx.Lookup(pszCacheExt, vData)) {
		// Get index for the system's small icon image list
		SHFILEINFO sfi;
		HIMAGELIST hResult = (HIMAGELIST)::SHGetFileInfo(iconDescriptor.strQueryPath, dwFileAttributes, &sfi, sizeof(sfi)
			, SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
		if (hResult == 0)
			return 0;
		ASSERT(m_hSystemImageList == NULL || m_hSystemImageList == hResult);
		m_hSystemImageList = hResult;

		// Store icon index in local cache
		m_aExtToSysImgIdx[pszCacheExt] = LongToPtr(sfi.iIcon);
		return sfi.iIcon;
	}

	// Return already cached value
	return PtrToLong(vData);
}

bool CemuleApp::IsConnected(bool bIgnoreEd2k, bool bIgnoreKad)
{
	return (!bIgnoreEd2k && theApp.serverconnect->IsConnected()) || (!bIgnoreKad && Kademlia::CKademlia::IsConnected());
}

bool CemuleApp::IsPortchangeAllowed()
{
	return theApp.clientlist->GetClientCount() == 0 && !IsConnected();
}

uint32 CemuleApp::GetID()
{
	if (Kademlia::CKademlia::IsConnected() && !Kademlia::CKademlia::IsFirewalled())
		return ntohl(Kademlia::CKademlia::GetIPAddress());
	if (theApp.serverconnect->IsConnected())
		return theApp.serverconnect->GetClientID();
	return static_cast<uint32>(Kademlia::CKademlia::IsConnected() && Kademlia::CKademlia::IsFirewalled());
}

uint32 CemuleApp::GetPublicIP() const
{
	if (m_dwPublicIP == 0 && Kademlia::CKademlia::IsConnected()) {
		uint32 uIP = Kademlia::CKademlia::GetIPAddress();
		if (uIP)
			return ntohl(uIP);
	}
	return m_dwPublicIP;
}

void CemuleApp::SetPublicIP(const uint32 dwIP)
{
	if (dwIP != 0) {
		ASSERT(!::IsLowID(dwIP));

		if (GetPublicIP() == 0)
			AddDebugLogLine(DLP_VERYLOW, false, _T("My public IP Address is: %s"), (LPCTSTR)ipstr(dwIP));
		else if (Kademlia::CKademlia::IsConnected() && Kademlia::CKademlia::GetPrefs()->GetIPAddress())
			if (htonl(Kademlia::CKademlia::GetIPAddress()) != dwIP)
				AddDebugLogLine(DLP_DEFAULT, false, _T("Public IP Address reported by Kademlia (%s) differs from new-found (%s)"), (LPCTSTR)ipstr(htonl(Kademlia::CKademlia::GetIPAddress())), (LPCTSTR)ipstr(dwIP));
	} else
		AddDebugLogLine(DLP_VERYLOW, false, _T("Deleted public IP"));

	if (dwIP != 0 && dwIP != m_dwPublicIP && serverlist != NULL) {
		m_dwPublicIP = dwIP;
		serverlist->CheckForExpiredUDPKeys();
	} else
		m_dwPublicIP = dwIP;

	if (emuledlg != NULL)
		emuledlg->ShowNetworkAddressState();
}

bool CemuleApp::IsFirewalled()
{
	if (theApp.serverconnect->IsConnected() && !theApp.serverconnect->IsLowID())
		return false; // we have an eD2K HighID -> not firewalled

	if (Kademlia::CKademlia::IsConnected() && !Kademlia::CKademlia::IsFirewalled())
		return false; // we have a Kad HighID -> not firewalled

	return true; // firewalled
}

bool CemuleApp::CanDoCallback(CUpDownClient *client)
{
	bool ed2k = theApp.serverconnect->IsConnected();
	bool eLow = theApp.serverconnect->IsLowID();

	if (!Kademlia::CKademlia::IsConnected() || Kademlia::CKademlia::IsFirewalled())
		return ed2k && !eLow; //callback for high ID server connection

	//KAD is connected and Open
	//Special case of a low ID server connection
	//If the client connects to the same server, we prevent callback
	//as it breaks the protocol and will get us banned.
	if ((ed2k & eLow) != 0) {
		const CServer *srv = theApp.serverconnect->GetCurrentServer();
		return client->GetServerIP() != srv->GetIP() || client->GetServerPort() != srv->GetPort();
	}
	return true;
}

HICON CemuleApp::LoadIcon(UINT nIDResource) const
{
	// use string resource identifiers!!
	return CWinApp::LoadIcon(nIDResource);
}

HICON CemuleApp::LoadIcon(LPCTSTR lpszResourceName, int cx, int cy, UINT uFlags) const
{
	// Test using of 16 color icons. If 'LR_VGACOLOR' is specified _and_ the icon resource
	// contains a 16 color version, that 16 color version will be loaded. If there is no
	// 16 color version available, Windows will use the next (better) color version found.
#ifdef _DEBUG
	if (g_bLowColorDesktop)
		uFlags |= LR_VGACOLOR;
#endif

	HICON hIcon = NULL;
	const CString &sSkinProfile(thePrefs.GetSkinProfile());
	if (!sSkinProfile.IsEmpty()) {
		const CString strResolvedResourcePath(GetSkinProfileResourcePath(sSkinProfile, _T("Icons"), lpszResourceName));
		if (!strResolvedResourcePath.IsEmpty()) {
			// check for optional icon index or resource identifier within the icon resource file
			bool bExtractIcon = false;
			CString strFullResPath(strResolvedResourcePath);
			int iIconIndex = 0;
			int iComma = strFullResPath.ReverseFind(_T(','));
			if (iComma >= 0) {
				bExtractIcon |= (_stscanf_s(CPTR(strFullResPath, iComma + 1), _T("%d"), &iIconIndex) == 1);
				strFullResPath.Truncate(iComma);
			}

			if (bExtractIcon) {
				if (uFlags != 0 || !(cx == cy && (cx == 16 || cx == 32))) {
					UINT uIconId;
					::PrivateExtractIcons(strFullResPath, iIconIndex, cx, cy, &hIcon, &uIconId, 1, uFlags);
				}

				if (hIcon == NULL) {
					HICON aIconsLarge[1], aIconsSmall[1];
					int iExtractedIcons = ExtractIconEx(strFullResPath, iIconIndex, aIconsLarge, aIconsSmall, 1);
					if (iExtractedIcons > 0) { // 'iExtractedIcons' is 2(!) if we get a large and a small icon
						// alway try to return the icon size which was requested
						if (cx == 16 && aIconsSmall[0] != NULL) {
							hIcon = aIconsSmall[0];
							aIconsSmall[0] = NULL;
						} else if (cx == 32 && aIconsLarge[0] != NULL) {
							hIcon = aIconsLarge[0];
							aIconsLarge[0] = NULL;
						} else {
							if (aIconsSmall[0] != NULL) {
								hIcon = aIconsSmall[0];
								aIconsSmall[0] = NULL;
							} else if (aIconsLarge[0] != NULL) {
								hIcon = aIconsLarge[0];
								aIconsLarge[0] = NULL;
							}
						}

						DestroyIconsArr(aIconsLarge, _countof(aIconsLarge));
						DestroyIconsArr(aIconsSmall, _countof(aIconsSmall));
					}
				}
			} else {
				// WINBUG???: 'ExtractIcon' does not work well on ICO-files when using the color
				// scheme 'Windows-Standard (extragroß)' -> always try to use 'LoadImage'!
				//
				// If the ICO file contains a 16x16 icon, 'LoadImage' will though return a 32x32 icon,
				// if LR_DEFAULTSIZE is specified! -> always specify the requested size!
				hIcon = (HICON)::LoadImage(NULL, strFullResPath, IMAGE_ICON, cx, cy, uFlags | LR_LOADFROMFILE);
				if (hIcon == NULL && ::GetLastError() != ERROR_PATH_NOT_FOUND/* && g_bGdiPlusInstalled*/) {
					ULONG_PTR gdiplusToken = 0;
					Gdiplus::GdiplusStartupInput gdiplusStartupInput;
					if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL) == Gdiplus::Ok) {
						Gdiplus::Bitmap bmp(strFullResPath);
						bmp.GetHICON(&hIcon);
					}
					Gdiplus::GdiplusShutdown(gdiplusToken);
				}
			}
		}
	}

	if (hIcon == NULL) {
		if (cx != LR_DEFAULTSIZE || cy != LR_DEFAULTSIZE || uFlags != LR_DEFAULTCOLOR)
			hIcon = (HICON)::LoadImage(AfxGetResourceHandle(), lpszResourceName, IMAGE_ICON, cx, cy, uFlags);
		if (hIcon == NULL) {
			//TODO: Either do not use that function or copy the returned icon. All the calling code is designed
			// in a way that the icons returned by this function are to be freed with 'DestroyIcon'. But an
			// icon which was loaded with 'LoadIcon', is not be freed with 'DestroyIcon'.
			// Right now, we never come here...
			ASSERT(0);
			hIcon = CWinApp::LoadIcon(lpszResourceName);
		}
	}
	return hIcon;
}

HBITMAP CemuleApp::LoadImage(LPCTSTR lpszResourceName, LPCTSTR pszResourceType) const
{
	const CString &sSkinProfile(thePrefs.GetSkinProfile());
	if (!sSkinProfile.IsEmpty()) {
		const CString strFullResPath(GetSkinProfileResourcePath(sSkinProfile, _T("Bitmaps"), lpszResourceName));
		if (!strFullResPath.IsEmpty()) {
			CEnBitmap bmp;
			if (bmp.LoadImage(strFullResPath))
				return (HBITMAP)bmp.Detach();
		}
	}

	CEnBitmap bmp;
	return bmp.LoadImage(lpszResourceName, pszResourceType) ? (HBITMAP)bmp.Detach() : NULL;
}

CString CemuleApp::GetSkinFileItem(LPCTSTR lpszResourceName, LPCTSTR pszResourceType) const
{
	const CString &sSkinProfile(thePrefs.GetSkinProfile());
	return GetSkinProfileResourcePath(sSkinProfile, pszResourceType, lpszResourceName);
}

bool CemuleApp::LoadSkinColor(LPCTSTR pszKey, COLORREF &crColor) const
{
	const CString &sSkinProfile(thePrefs.GetSkinProfile());
	if (!sSkinProfile.IsEmpty()) {
		// Intentional shell-facing limitation: legacy skin color profiles stay on shell-safe paths;
		// exact-name or namespace-only skin profile paths are not a supported long-path target.
		const CString strColor(ShellUiHelpers::GetProfileString(_T("Colors"), pszKey, NULL, sSkinProfile));
		if (!strColor.IsEmpty()) {
			int red, grn, blu;
			if (_stscanf_s(strColor, _T("%i , %i , %i"), &red, &grn, &blu) == 3) {
				crColor = RGB(red, grn, blu);
				return true;
			}
		}
	}
	return false;
}

bool CemuleApp::LoadSkinColorAlt(LPCTSTR pszKey, LPCTSTR pszAlternateKey, COLORREF &crColor) const
{
	return LoadSkinColor(pszKey, crColor) || LoadSkinColor(pszAlternateKey, crColor);
}

void CemuleApp::ApplySkin(LPCTSTR pszSkinProfile)
{
	thePrefs.SetSkinProfile(pszSkinProfile);
	AfxGetMainWnd()->SendMessage(WM_SYSCOLORCHANGE);
}

CTempIconLoader::CTempIconLoader(LPCTSTR pszResourceID, int cx, int cy, UINT uFlags)
{
	m_hIcon = theApp.LoadIcon(pszResourceID, cx, cy, uFlags);
}

CTempIconLoader::CTempIconLoader(UINT uResourceID, int /*cx*/, int /*cy*/, UINT uFlags)
{
	UNREFERENCED_PARAMETER(uFlags);
	ASSERT(uFlags == 0);
	m_hIcon = theApp.LoadIcon(uResourceID);
}

CTempIconLoader::~CTempIconLoader()
{
	if (m_hIcon)
		VERIFY(::DestroyIcon(m_hIcon));
}

void CemuleApp::AddEd2kLinksToDownload(const CString &strLinks, int cat)
{
	for (int iPos = 0; iPos >= 0;) {
		const CString &sToken(strLinks.Tokenize(_T(" \t\r\n"), iPos)); //tokenize by whitespace
		if (sToken.IsEmpty())
			break;
		bool bSlash = (sToken[sToken.GetLength() - 1] == _T('/'));
		CED2KLink *pLink = NULL;
		try {
			pLink = CED2KLink::CreateLinkFromUrl(bSlash ? sToken : sToken + _T('/'));
			if (pLink) {
				if (pLink->GetKind() != CED2KLink::kFile)
					throwCStr(_T("bad link"));
				downloadqueue->AddFileLinkToDownload(*pLink->GetFileLink(), cat);
				delete pLink;
				pLink = NULL;
			}
		} catch (const CString &error) {
			delete pLink;
			CString sBuffer;
			sBuffer.Format(GetResString(IDS_ERR_INVALIDLINK), (LPCTSTR)error);
			LogError(LOG_STATUSBAR, GetResString(IDS_ERR_LINKERROR), (LPCTSTR)sBuffer);
			return;
		}
	}
}

void CemuleApp::SearchClipboard()
{
	if (m_bGuardClipboardPrompt)
		return;

	const CString strLinks(CopyTextFromClipboard());
	if (strLinks.IsEmpty())
		return;

	if (strLinks == m_strLastClipboardContents)
		return;

	// Do not alter (trim) 'strLinks' and then copy back to 'm_strLastClipboardContents'! The
	// next clipboard content compare would fail because of the modified string.
	LPCTSTR pszTrimmedLinks = strLinks;
	while (_istspace(*pszTrimmedLinks)) // Skip leading white space
		++pszTrimmedLinks;
	m_bGuardClipboardPrompt = !_tcsnicmp(pszTrimmedLinks, _T("ed2k://|file|"), 13);
	if (m_bGuardClipboardPrompt) {
		// Don't feed too long strings into the MessageBox function, it may freak out.
		CString strLinksDisplay(GetResString(IDS_ADDDOWNLOADSFROMCB));
		if (strLinks.GetLength() > 512)
			strLinksDisplay.AppendFormat(_T("\r\n%s..."), (LPCTSTR)strLinks.Left(509));
		else
			strLinksDisplay.AppendFormat(_T("\r\n%s"), (LPCTSTR)strLinks);
		if (AfxMessageBox(strLinksDisplay, MB_YESNO | MB_TOPMOST) == IDYES)
			AddEd2kLinksToDownload(pszTrimmedLinks, 0);
	}
	m_strLastClipboardContents = strLinks; // Save the unmodified(!) clipboard contents
	m_bGuardClipboardPrompt = false;
}

void CemuleApp::PasteClipboard(int cat)
{
	CString strLinks(CopyTextFromClipboard());
	if (!strLinks.Trim().IsEmpty())
		AddEd2kLinksToDownload(strLinks, cat);
}

bool CemuleApp::IsEd2kLinkInClipboard(LPCSTR pszLinkType, int iLinkTypeLen)
{
	bool bFoundLink = false;
	if (IsClipboardFormatAvailable(CF_TEXT)) {
		if (OpenClipboard(NULL)) {
			HGLOBAL	hText = GetClipboardData(CF_TEXT);
			if (hText != NULL) {
				// Use the ANSI string
				LPCSTR pszText = static_cast<LPCSTR>(::GlobalLock(hText));
				if (pszText != NULL) {
					while (isspace(*pszText))
						++pszText;
					bFoundLink = (_strnicmp(pszText, pszLinkType, iLinkTypeLen) == 0);
					::GlobalUnlock(hText);
				}
			}
			CloseClipboard();
		}
	}

	return bFoundLink;
}

bool CemuleApp::IsEd2kFileLinkInClipboard()
{
	static const char _szEd2kFileLink[] = "ed2k://|file|"; // Use the ANSI string
	return IsEd2kLinkInClipboard(_szEd2kFileLink, sizeof _szEd2kFileLink - 1);
}

bool CemuleApp::IsEd2kServerLinkInClipboard()
{
	static const char _szEd2kServerLink[] = "ed2k://|server|"; // Use the ANSI string
	return IsEd2kLinkInClipboard(_szEd2kServerLink, sizeof _szEd2kServerLink - 1);
}

// Elandal:ThreadSafeLogging -->
void CemuleApp::QueueDebugLogLine(bool bAddToStatusbar, LPCTSTR line, ...)
{
	if (!thePrefs.GetVerbose())
		return;

	CString bufferline;
	va_list argptr;
	va_start(argptr, line);
	bufferline.FormatV(line, argptr);
	va_end(argptr);
	if (!bufferline.IsEmpty()) {
		SLogItem *newItem = new SLogItem;
		newItem->uFlags = LOG_DEBUG | (bAddToStatusbar ? LOG_STATUSBAR : 0);
		newItem->timestamp = CTime::GetCurrentTime();
		newItem->line = bufferline;

		m_queueLock.Lock();
		if (m_QueueDebugLog.GetCount() >= kMaxQueuedDebugLogEntries) {
			delete m_QueueDebugLog.RemoveHead();
			++m_uDroppedDebugLogEntries;
		}
		m_QueueDebugLog.AddTail(newItem);
		m_queueLock.Unlock();
	}
}

void CemuleApp::QueueLogLine(bool bAddToStatusbar, LPCTSTR line, ...)
{
	CString bufferline;
	va_list argptr;
	va_start(argptr, line);
	bufferline.FormatV(line, argptr);
	va_end(argptr);
	if (!bufferline.IsEmpty()) {
		SLogItem *newItem = new SLogItem;
		newItem->uFlags = bAddToStatusbar ? LOG_STATUSBAR : 0;
		newItem->timestamp = CTime::GetCurrentTime();
		newItem->line = bufferline;

		m_queueLock.Lock();
		if (m_QueueLog.GetCount() >= kMaxQueuedLogEntries) {
			delete m_QueueLog.RemoveHead();
			++m_uDroppedLogEntries;
		}
		m_QueueLog.AddTail(newItem);
		m_queueLock.Unlock();
	}
}

void CemuleApp::QueueDebugLogLineEx(UINT uFlags, LPCTSTR line, ...)
{
	if (!thePrefs.GetVerbose())
		return;

	CString bufferline;
	va_list argptr;
	va_start(argptr, line);
	bufferline.FormatV(line, argptr);
	va_end(argptr);
	if (!bufferline.IsEmpty()) {
		SLogItem *newItem = new SLogItem;
		newItem->uFlags = uFlags | LOG_DEBUG;
		newItem->timestamp = CTime::GetCurrentTime();
		newItem->line = bufferline;

		m_queueLock.Lock();
		if (m_QueueDebugLog.GetCount() >= kMaxQueuedDebugLogEntries) {
			delete m_QueueDebugLog.RemoveHead();
			++m_uDroppedDebugLogEntries;
		}
		m_QueueDebugLog.AddTail(newItem);
		m_queueLock.Unlock();
	}
}

void CemuleApp::QueueLogLineEx(UINT uFlags, LPCTSTR line, ...)
{
	CString bufferline;
	va_list argptr;
	va_start(argptr, line);
	bufferline.FormatV(line, argptr);
	va_end(argptr);
	if (!bufferline.IsEmpty()) {
		SLogItem *newItem = new SLogItem;
		newItem->uFlags = uFlags;
		newItem->timestamp = CTime::GetCurrentTime();
		newItem->line = bufferline;

		m_queueLock.Lock();
		if (m_QueueLog.GetCount() >= kMaxQueuedLogEntries) {
			delete m_QueueLog.RemoveHead();
			++m_uDroppedLogEntries;
		}
		m_QueueLog.AddTail(newItem);
		m_queueLock.Unlock();
	}
}

void CemuleApp::HandleDebugLogQueue()
{
	CTypedPtrList<CPtrList, SLogItem*> aPendingItems;
	UINT uDroppedEntries = 0;
	m_queueLock.Lock();
	while (!m_QueueDebugLog.IsEmpty())
		aPendingItems.AddTail(m_QueueDebugLog.RemoveHead());
	uDroppedEntries = m_uDroppedDebugLogEntries;
	m_uDroppedDebugLogEntries = 0;
	m_queueLock.Unlock();

	const bool bBatchUiLogUpdates = emuledlg != NULL && !aPendingItems.IsEmpty();
	if (bBatchUiLogUpdates)
		emuledlg->BeginLogBatchUpdate();
	while (!aPendingItems.IsEmpty()) {
		const SLogItem *newItem = aPendingItems.RemoveHead();
		if (thePrefs.GetVerbose()) {
			if (emuledlg != NULL)
				emuledlg->AddLogText(newItem->uFlags, newItem->line, &newItem->timestamp);
			else
				Log(newItem->uFlags, _T("%s"), (LPCTSTR)newItem->line);
		}
		delete newItem;
	}
	if (bBatchUiLogUpdates)
		emuledlg->EndLogBatchUpdate();

	if (uDroppedEntries > 0)
		LogWarning(_T("Dropped %u queued verbose log messages due to queue overflow."), uDroppedEntries);
}

void CemuleApp::HandleLogQueue()
{
	CTypedPtrList<CPtrList, SLogItem*> aPendingItems;
	UINT uDroppedEntries = 0;
	m_queueLock.Lock();
	while (!m_QueueLog.IsEmpty())
		aPendingItems.AddTail(m_QueueLog.RemoveHead());
	uDroppedEntries = m_uDroppedLogEntries;
	m_uDroppedLogEntries = 0;
	m_queueLock.Unlock();

	const bool bBatchUiLogUpdates = emuledlg != NULL && !aPendingItems.IsEmpty();
	if (bBatchUiLogUpdates)
		emuledlg->BeginLogBatchUpdate();
	while (!aPendingItems.IsEmpty()) {
		const SLogItem *newItem = aPendingItems.RemoveHead();
		if (emuledlg != NULL)
			emuledlg->AddLogText(newItem->uFlags, newItem->line, &newItem->timestamp);
		else
			Log(newItem->uFlags, _T("%s"), (LPCTSTR)newItem->line);
		delete newItem;
	}
	if (bBatchUiLogUpdates)
		emuledlg->EndLogBatchUpdate();

	if (uDroppedEntries > 0)
		LogWarning(_T("Dropped %u queued log messages due to queue overflow."), uDroppedEntries);
}

void CemuleApp::ClearDebugLogQueue(bool bDebugPendingMsgs)
{
	m_queueLock.Lock();
	while (!m_QueueDebugLog.IsEmpty()) {
		if (bDebugPendingMsgs)
			TRACE(_T("Queued dbg log msg: %s\n"), (LPCTSTR)m_QueueDebugLog.GetHead()->line);
		delete m_QueueDebugLog.RemoveHead();
	}
	m_queueLock.Unlock();
}

void CemuleApp::ClearLogQueue(bool bDebugPendingMsgs)
{
	m_queueLock.Lock();
	while (!m_QueueLog.IsEmpty()) {
		if (bDebugPendingMsgs)
			TRACE(_T("Queued log msg: %s\n"), (LPCTSTR)m_QueueLog.GetHead()->line);
		delete m_QueueLog.RemoveHead();
	}
	m_queueLock.Unlock();
}
// Elandal:ThreadSafeLogging <--

void CemuleApp::CreateAllFonts()
{
	///////////////////////////////////////////////////////////////////////////
	// Symbol font
	//
	//VERIFY( CreatePointFont(m_fontSymbol, 10 * 10, _T("Marlett")) );
	// Creating that font with 'SYMBOL_CHARSET' should be safer (seen in ATL/MFC code). Though
	// it seems that it does not solve the problem with '6' and '9' characters which are
	// shown for some ppl.
	static LOGFONT lfSymbol = {0, 0, 0, 0, FW_NORMAL
					   , 0, 0, 0, SYMBOL_CHARSET, 0, 0, 0, 0
					   , _T("Marlett")};

	lfSymbol.lfHeight = ::GetSystemMetrics(SM_CYMENUCHECK);
	m_fontSymbol.CreateFontIndirect(&lfSymbol);

	///////////////////////////////////////////////////////////////////////////
	// Default GUI Font
	//
	// Fonts which are returned by 'GetStockObject'
	// --------------------------------------------
	// OEM_FIXED_FONT		Terminal
	// ANSI_FIXED_FONT		Courier
	// ANSI_VAR_FONT		MS Sans Serif
	// SYSTEM_FONT			System
	// DEVICE_DEFAULT_FONT	System
	// SYSTEM_FIXED_FONT	Fixedsys
	// DEFAULT_GUI_FONT		MS Shell Dlg (*1)
	//
	// (*1) Do not use 'GetStockObject(DEFAULT_GUI_FONT)' to get the 'Tahoma' font. It does
	// not work...
	//
	// The stock GUI font mapping is inconsistent across locales, so query the main
	// window font directly instead of relying on DEFAULT_GUI_FONT semantics.
	//
	// The reason why "MS Shell Dlg" is though mapped to "Tahoma" when used within dialog
	// resources is unclear.
	//
	// So, to get the same font which is used within dialogs which were created via dialog
	// resources which have the "MS Shell Dlg, 8" specified (again, in that special case
	// "MS Shell Dlg" gets mapped to "Tahoma" and not to "MS Sans Serif"), we just query
	// the main window (which is also a dialog) for the current font.
	//
	LOGFONT lfDefault;
	AfxGetMainWnd()->GetFont()->GetLogFont(&lfDefault);
	//
	// It would not be an error if that font name does not match our pre-determined
	// font name, I just want to know if that ever happens.
	ASSERT(m_strDefaultFontFaceName == lfDefault.lfFaceName);


	///////////////////////////////////////////////////////////////////////////
	// Bold Default GUI Font
	//
	LOGFONT lfDefaultBold = lfDefault;
	lfDefaultBold.lfWeight = FW_BOLD;
	VERIFY(m_fontDefaultBold.CreateFontIndirect(&lfDefaultBold));


	///////////////////////////////////////////////////////////////////////////
	// Server Log-, Message- and IRC-Window font
	//
	// Keep using the dialog-mapped shell font family here. Hard-coding legacy GUI
	// fonts causes poor results on localized systems.
	//
	LPLOGFONT plfHyperText = thePrefs.GetHyperTextLogFont();
	if (plfHyperText->lfFaceName[0] == _T('\0') || !m_fontHyperText.CreateFontIndirect(plfHyperText))
		CreatePointFont(m_fontHyperText, 10 * 10, lfDefault.lfFaceName);

	///////////////////////////////////////////////////////////////////////////
	// Server, log and verbose log font
	//
	// Use one fixed-width font for all diagnostic text panes so aligned log
	// output stays readable and consistent.
	//
	VERIFY(CreatePointFont(m_fontLog, 10 * 10, _T("Consolas")));

	///////////////////////////////////////////////////////////////////////////
	// Font used for Message and IRC edit control, default font, just a little
	// larger.
	//
	// Keep using the dialog-mapped shell font family here as well.
	//
	CreatePointFont(m_fontChatEdit, 11 * 10, lfDefault.lfFaceName);
}

const CString& CemuleApp::GetDefaultFontFaceName()
{
	return m_strDefaultFontFaceName;
}

void CemuleApp::CreateBackwardDiagonalBrush()
{
	static const WORD awBackwardDiagonalBrushPattern[8] = {0x0f, 0x1e, 0x3c, 0x78, 0xf0, 0xe1, 0xc3, 0x87};
	CBitmap bm;
	if (bm.CreateBitmap(8, 8, 1, 1, awBackwardDiagonalBrushPattern)) {
		LOGBRUSH logBrush{};
		logBrush.lbStyle = BS_PATTERN;
		logBrush.lbHatch = (ULONG_PTR)bm.GetSafeHandle();
		//logBrush.lbColor = RGB(0, 0, 0);
		VERIFY(m_brushBackwardDiagonal.CreateBrushIndirect(&logBrush));
	}
}

void CemuleApp::UpdateDesktopColorDepth()
{
	g_bLowColorDesktop = (GetDesktopColorDepth() <= 8);

	if (g_bLowColorDesktop) {
		// If we have 4- or 8-bit desktop color depth, Windows will (by design) load only
		// the 16 color versions of icons. Thus we force all image lists also to 4-bit format.
		m_iDfltImageListColorFlags = ILC_COLOR4;
	} else {
		// Get current desktop color depth and derive the image list format from it
		m_iDfltImageListColorFlags = GetAppImageListColorFlag();

		// Don't use 32-bit image lists if not supported by COMCTL32.DLL
		if (m_iDfltImageListColorFlags == ILC_COLOR32 && m_ullComCtrlVer < MAKEDLLVERULL(6, 0, 0, 0)) {
			// We fall back to 16-bit image lists because we do not provide 24-bit
			// versions of icons any longer.
			// could also fall back to 24-bit image lists here but the difference is minimal
			// and considered not to be worth the additional memory consumption.
			//
			// Though, do not fall back to 8-bit image lists because this would let Windows
			// reduce the color resolution to the standard 256 color window system palette.
			// We need a 16-bit or 24-bit image list to hold all our 256 color icons (which
			// are not pre-quantized to standard 256 color windows system palette) without
			// losing any colors.
			m_iDfltImageListColorFlags = ILC_COLOR16;
		}
	}

	// Doesn't help.
	//m_aExtToSysImgIdx.RemoveAll();
}

BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType) noexcept
{
	// *) This function is invoked by the system from within a *DIFFERENT* thread !!
	//
	// *) This function is invoked only, if eMule was started with "RUNAS"
	//		- when user explicitly/manually logs off from the system (CTRL_LOGOFF_EVENT).
	//		- when user explicitly/manually does a reboot or shutdown (also: CTRL_LOGOFF_EVENT).
	//		- when eMule issues an ExitWindowsEx(EWX_LOGOFF/EWX_REBOOT/EWX_SHUTDOWN)
	//
	// NOTE: Windows will in each case forcefully terminate the process after 20 seconds!
	// Every action which is started after receiving this notification will get forcefully
	// terminated by Windows after 20 seconds.

	if (thePrefs.GetDebug2Disk()) {
		static TCHAR szCtrlType[40];
		LPCTSTR pszCtrlType;
		switch (dwCtrlType) {
		case CTRL_C_EVENT:
			pszCtrlType = _T("CTRL_C_EVENT");
			break;
		case CTRL_BREAK_EVENT:
			pszCtrlType = _T("CTRL_BREAK_EVENT");
			break;
		case CTRL_CLOSE_EVENT:
			pszCtrlType = _T("CTRL_CLOSE_EVENT");
			break;
		case CTRL_LOGOFF_EVENT:
			pszCtrlType = _T("CTRL_LOGOFF_EVENT");
			break;
		case CTRL_SHUTDOWN_EVENT:
			pszCtrlType = _T("CTRL_SHUTDOWN_EVENT");
			break;
		default:
			_sntprintf_s(szCtrlType, _countof(szCtrlType), _TRUNCATE, _T("0x%08lx"), dwCtrlType);
			pszCtrlType = szCtrlType;
		}
		theVerboseLog.Logf(_T("%hs: CtrlType=%s"), __FUNCTION__, pszCtrlType);

		// Default ProcessShutdownParameters: Level=0x00000280, Flags=0x00000000
		// Setting 'SHUTDOWN_NORETRY' does not prevent from getting terminated after 20 sec.
		//DWORD dwLevel = 0, dwFlags = 0;
		//GetProcessShutdownParameters(&dwLevel, &dwFlags);
		//theVerboseLog.Logf(_T("%hs: ProcessShutdownParameters #0: Level=0x%08x, Flags=0x%08x"), __FUNCTION__, dwLevel, dwFlags);
		//SetProcessShutdownParameters(dwLevel, SHUTDOWN_NORETRY);
	}

	if (dwCtrlType == CTRL_CLOSE_EVENT || dwCtrlType == CTRL_LOGOFF_EVENT || dwCtrlType == CTRL_SHUTDOWN_EVENT) {
		if (theApp.emuledlg->m_hWnd) {
			if (thePrefs.GetDebug2Disk())
				theVerboseLog.Logf(_T("%hs: Sending TM_CONSOLETHREADEVENT to main window"), __FUNCTION__);

			// Use 'SendMessage' to send the message to the (different) main thread. This is
			// done by intention because it lets this thread wait as long as the main thread
			// has called 'ExitProcess' or returns from processing the message. This is
			// needed to not let Windows terminate the process before the 20 sec. timeout.
			if (!theApp.emuledlg->SendMessage(TM_CONSOLETHREADEVENT, dwCtrlType, (LPARAM)GetCurrentThreadId())) {
				theApp.m_app_state = APP_STATE_SHUTTINGDOWN; // as a last attempt
				if (thePrefs.GetDebug2Disk())
					theVerboseLog.Logf(_T("%hs: Error: Failed to send TM_CONSOLETHREADEVENT to main window - error %u"), __FUNCTION__, ::GetLastError());
			}
		}
	}

	// Returning FALSE does not cause Windows to immediately terminate the process. Though,
	// that only depends on the next registered console control handler. The default seems
	// to wait 20 sec. until the process has terminated. After that timeout Windows
	// nevertheless terminates the process.
	//
	// For whatever unknown reason, this is *not* always true!? It may happen that Windows
	// terminates the process *before* the 20 sec. timeout if (and only if) the console
	// control handler thread has already terminated. So, we have to take care that we do not
	// exit this thread before the main thread has called 'ExitProcess' (in a synchronous
	// way) -- see also the 'SendMessage' above.
	if (thePrefs.GetDebug2Disk())
		theVerboseLog.Logf(_T("%hs: returning"), __FUNCTION__);
	return FALSE; // FALSE: Let the system kill the process with the default handler.
}

void CemuleApp::UpdateLargeIconSize()
{
	// initialize with system values in case we don't find the Shell's registry key
	m_sizBigSystemIcon.cx = ::GetSystemMetrics(SM_CXICON);
	m_sizBigSystemIcon.cy = ::GetSystemMetrics(SM_CYICON);

	// get the Shell's registry key for the large icon size - the large icons which are
	// returned by the Shell are based on that size rather than on the system icon size
	CRegKey key;
	if (key.Open(HKEY_CURRENT_USER, _T("Control Panel\\desktop\\WindowMetrics"), KEY_READ) == ERROR_SUCCESS) {
		TCHAR szShellLargeIconSize[12];
		ULONG ulChars = _countof(szShellLargeIconSize);
		if (key.QueryStringValue(_T("Shell Icon Size"), szShellLargeIconSize, &ulChars) == ERROR_SUCCESS) {
			UINT uIconSize = 0;
			if (_stscanf_s(szShellLargeIconSize, _T("%u"), &uIconSize) == 1 && uIconSize > 0) {
				m_sizBigSystemIcon.cx = uIconSize;
				m_sizBigSystemIcon.cy = uIconSize;
			}
		}
	}
}

void CemuleApp::UpdateStandbyPrevention()
{
	const unsigned int uUploadQueueLength = uploadqueue != NULL ? static_cast<unsigned int>(uploadqueue->GetUploadQueueLength()) : 0;
	const unsigned int uDownloadDatarate = downloadqueue != NULL ? downloadqueue->GetDatarate() : 0;
	const bool bShouldPreventSystemSleep = StandbyPreventionSeams::ShouldPreventSystemSleep(thePrefs.GetPreventStandby(), IsConnected(), uUploadQueueLength, uDownloadDatarate);
	if (bShouldPreventSystemSleep) {
		if (!m_bStandbyOff && ::SetThreadExecutionState(StandbyPreventionSeams::GetPreventSystemSleepFlags()) != 0)
			m_bStandbyOff = true;
		return;
	}

	if (StandbyPreventionSeams::ShouldReleaseSystemSleepAssertion(m_bStandbyOff, bShouldPreventSystemSleep))
		ReleaseStandbyPrevention();
}

bool CemuleApp::ReleaseStandbyPrevention()
{
	if (!m_bStandbyOff)
		return true;

	if (::SetThreadExecutionState(StandbyPreventionSeams::GetReleaseSystemSleepFlags()) != 0) {
		m_bStandbyOff = false;
		return true;
	}

	return false;
}

bool CemuleApp::IsLegacyThemedControlsActive() const
{
	// TRUE when themed common controls are active in the pre-6.16 visual style bucket.
	return theApp.m_ullComCtrlVer < MAKEDLLVERULL(6, 16, 0, 0) && ::IsThemeActive() && ::IsAppThemed();
}

bool CemuleApp::IsModernThemedControlsActive() const
{
	// TRUE when themed common controls are active in the 6.16+ visual style bucket.
	return theApp.m_ullComCtrlVer >= MAKEDLLVERULL(6, 16, 0, 0) && ::IsThemeActive() && ::IsAppThemed();
}
