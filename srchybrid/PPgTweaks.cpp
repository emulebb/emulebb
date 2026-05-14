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
#include "opcodes.h"
#include "OtherFunctions.h"
#include "SearchDlg.h"
#include "FileBufferSlider.h"
#include "PPgTweaks.h"
#include "Scheduler.h"
#include "DownloadQueue.h"
#include "Preferences.h"
#include "TransferDlg.h"
#include "emuledlg.h"
#include "SharedFilesWnd.h"
#include "ServerWnd.h"
#include "GeoLocation.h"
#include "HelpIDs.h"
#include "Log.h"
#include "PerfLog.h"
#include "PreferenceUiSeams.h"
#include "UserMsgs.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	static const LPCTSTR kMediaInfoDllPathProfileKey = _T("MediaInfo_MediaInfoDllPath");
	static const LPCTSTR kMediaInfoDllPathDefault = _T("MEDIAINFO.DLL");

	static void FailTreeValidation(CDataExchange *pDX, UINT uMessageId, HTREEITEM hItem)
	{
		AfxMessageBox(uMessageId);
		pDX->PrepareEditCtrl(IDC_EXT_OPTS);
		pDX->Fail();
		UNREFERENCED_PARAMETER(hItem);
	}

	static CString GetTreeItemLabel(CTreeOptionsCtrlEx &ctrl, HTREEITEM hItem)
	{
		CString label(ctrl.GetItemText(hItem));
		const int nSeparator = label.Find(ctrl.GetTextSeparator());
		if (nSeparator >= 0)
			label = label.Left(nSeparator);
		label.Trim();
		return label;
	}

	static void RevealTreeItem(CTreeOptionsCtrlEx &ctrl, HTREEITEM hItem)
	{
		for (HTREEITEM hParent = ctrl.GetParentItem(hItem); hParent != NULL; hParent = ctrl.GetParentItem(hParent))
			ctrl.Expand(hParent, TVE_EXPAND);
		ctrl.SelectItem(hItem);
		ctrl.EnsureVisible(hItem);
	}

	static void ExpandTreeRecursively(CTreeOptionsCtrlEx &ctrl, HTREEITEM hItem)
	{
		if (hItem == NULL)
			return;

		ctrl.Expand(hItem, TVE_EXPAND);
		for (HTREEITEM hChild = ctrl.GetChildItem(hItem); hChild != NULL; hChild = ctrl.GetNextSiblingItem(hChild))
			ExpandTreeRecursively(ctrl, hChild);
	}

	static void ExpandAllTreeItems(CTreeOptionsCtrlEx &ctrl)
	{
		for (HTREEITEM hItem = ctrl.GetRootItem(); hItem != NULL; hItem = ctrl.GetNextSiblingItem(hItem))
			ExpandTreeRecursively(ctrl, hItem);
	}

	static void FailTreeValidation(CDataExchange *pDX, CTreeOptionsCtrlEx &ctrl, HTREEITEM hItem, const CString &detail)
	{
		RevealTreeItem(ctrl, hItem);
		const CString label = GetTreeItemLabel(ctrl, hItem);
		CString message;
		message.Format(GetResString(IDS_TWEAKS_VALIDATION_INVALID_VALUE_FMT), static_cast<LPCTSTR>(label), static_cast<LPCTSTR>(detail));
		AfxMessageBox(message);
		pDX->PrepareEditCtrl(IDC_EXT_OPTS);
		pDX->Fail();
	}

	static bool TryParseTreeInt(const CString &text, int &outValue)
	{
		CString trimmed(text);
		trimmed.Trim();
		if (trimmed.IsEmpty())
			return false;

		LPTSTR pEnd = NULL;
		errno = 0;
		const long value = _tcstol(trimmed, &pEnd, 10);
		if (pEnd == (LPCTSTR)trimmed || *pEnd != _T('\0') || errno == ERANGE || value < INT_MIN || value > INT_MAX)
			return false;
		outValue = static_cast<int>(value);
		return true;
	}

	static bool TryParseTreeUInt(const CString &text, UINT &outValue)
	{
		CString trimmed(text);
		trimmed.Trim();
		if (trimmed.IsEmpty() || trimmed[0] == _T('-'))
			return false;

		LPTSTR pEnd = NULL;
		errno = 0;
		const unsigned long value = _tcstoul(trimmed, &pEnd, 10);
		if (pEnd == (LPCTSTR)trimmed || *pEnd != _T('\0') || errno == ERANGE || value > UINT_MAX)
			return false;
		outValue = static_cast<UINT>(value);
		return true;
	}

	static void ExchangeTreeInt(CDataExchange *pDX, CTreeOptionsCtrlEx &ctrl, HTREEITEM hItem, int &value)
	{
		if (pDX->m_bSaveAndValidate) {
			if (!TryParseTreeInt(ctrl.GetEditText(hItem), value))
				FailTreeValidation(pDX, ctrl, hItem, GetResString(IDS_TWEAKS_VALIDATION_INTEGER));
		} else {
			CString text;
			text.Format(_T("%d"), value);
			ctrl.SetEditText(hItem, text);
		}
	}

	static void ExchangeTreeUInt(CDataExchange *pDX, CTreeOptionsCtrlEx &ctrl, HTREEITEM hItem, UINT &value)
	{
		if (pDX->m_bSaveAndValidate) {
			if (!TryParseTreeUInt(ctrl.GetEditText(hItem), value))
				FailTreeValidation(pDX, ctrl, hItem, GetResString(IDS_TWEAKS_VALIDATION_INTEGER));
		} else {
			CString text;
			text.Format(_T("%u"), value);
			ctrl.SetEditText(hItem, text);
		}
	}

	/**
	 * Parses a floating-point value from a tree-edit string and rejects trailing junk.
	 */
	static bool TryParseTreeFloat(const CString &text, float &outValue)
	{
		LPTSTR pEnd = NULL;
		outValue = static_cast<float>(_tcstod(text, &pEnd));
		if (pEnd == (LPCTSTR)text)
			return false;
		while (*pEnd != _T('\0')) {
			if (!_istspace(*pEnd))
				return false;
			++pEnd;
		}
		return true;
	}

	/**
	 * Builds the Extended-tree label for the geolocation refresh interval.
	 */
	static CString GetGeoLocationIntervalLabel()
	{
		CString label(GetResString(IDS_GEOLOCATION_CHECK_DAYS));
		label.Append(_T(" ("));
		label.Append(GetResString(IDS_GEOLOCATION_CHECK_DAYS_SUFFIX));
		label.Append(_T(")"));
		return label;
	}

	static CString GetFileBufferSizeLabel()
	{
		CString label;
		label.Format(GetResString(IDS_TWEAKS_FILE_BUFFER_SIZE_LABEL_FMT), (LPCTSTR)GetResString(IDS_FILEBUFFERSIZE));
		return label;
	}

	static CString GetCreateCrashDumpLabel()
	{
		return GetResString(IDS_TWEAKS_CRASH_DUMP_CREATION);
	}

	static CString GetCreateCrashDumpDisabledLabel()
	{
		return GetResString(IDS_DISABLED);
	}

	static CString GetCreateCrashDumpPromptLabel()
	{
		return GetResString(IDS_TWEAKS_CRASH_DUMP_PROMPT);
	}

	static CString GetCreateCrashDumpAlwaysLabel()
	{
		return GetResString(IDS_TWEAKS_CRASH_DUMP_ALWAYS);
	}

	static CString GetMaxLogFileSizeLabel()
	{
		return GetResString(IDS_TWEAKS_MAX_LOG_FILE_SIZE);
	}

	static CString GetMaxLogBufferLabel()
	{
		return GetResString(IDS_TWEAKS_MAX_LOG_BUFFER);
	}

	static CString GetLogFileFormatLabel()
	{
		return GetResString(IDS_TWEAKS_LOG_FILE_FORMAT);
	}

	static CString GetLogFileFormatUnicodeLabel()
	{
		return GetResString(IDS_TWEAKS_LOG_FILE_FORMAT_UTF16);
	}

	static CString GetLogFileFormatUtf8Label()
	{
		return GetResString(IDS_TWEAKS_LOG_FILE_FORMAT_UTF8);
	}

	static CString GetFullVerboseLabel()
	{
		return GetResString(IDS_TWEAKS_FULL_VERBOSE_LOGGING);
	}

	static CString GetPerfLogFileFormatLabel()
	{
		return GetResString(IDS_TWEAKS_PERF_LOG_FORMAT);
	}

	static CString GetPerfLogFileLabel()
	{
		return GetResString(IDS_TWEAKS_PERF_LOG_FILE);
	}

	static CString GetPerfLogIntervalLabel()
	{
		return GetResString(IDS_TWEAKS_PERF_LOG_INTERVAL);
	}

	static CString GetHighresTimerLabel()
	{
		return GetResString(IDS_TWEAKS_HIGHRES_TIMER);
	}

	static CString GetIchLabel()
	{
		return GetResString(IDS_TWEAKS_ICH);
	}

	static CString GetPreviewSmallBlocksLabel()
	{
		return GetResString(IDS_TWEAKS_PREVIEW_SMALL_BLOCKS);
	}

	static CString GetPreviewSmallBlocksAllowLabel()
	{
		return GetResString(IDS_TWEAKS_PREVIEW_SMALL_BLOCKS_ALLOW);
	}

	static CString GetPreviewSmallBlocksForceLabel()
	{
		return GetResString(IDS_TWEAKS_PREVIEW_SMALL_BLOCKS_FORCE);
	}

	static CString GetBeepOnErrorLabel()
	{
		return GetResString(IDS_TWEAKS_BEEP_ON_ERROR);
	}

	static CString GetShowCopyEd2kLinkCmdLabel()
	{
		return GetResString(IDS_TWEAKS_SHOW_COPY_ED2K_LINK);
	}

	static CString GetIconFlashOnNewMessageLabel()
	{
		return GetResString(IDS_TWEAKS_ICON_FLASH_NEW_MESSAGE);
	}

	static CString GetDateTimeFormatLabel()
	{
		return GetResString(IDS_TWEAKS_DATETIME_FORMAT);
	}

	static CString GetDateTimeFormat4LogLabel()
	{
		return GetResString(IDS_TWEAKS_DATETIME_LOG_FORMAT);
	}

	static CString GetTxtEditorLabel()
	{
		return GetResString(IDS_TWEAKS_TEXT_EDITOR_COMMAND);
	}

	static CString GetExtractMetaDataAutoLabel()
	{
		return _T("Auto media metadata");
	}

	static CString GetMediaInfoDllPathLabel()
	{
		return _T("MediaInfo.dll path");
	}

	static CString GetExtractMetaDataToolTip()
	{
		return _T("Controls whether eMule extracts metadata such as tags, codecs, bitrates, and media duration from shared audio/video files.\r\n\r\nThe enabled mode uses MediaInfo.dll when available, then built-in media parsers, then ID3 tags for MPEG audio.");
	}

	static CString GetExtractMetaDataAutoToolTip()
	{
		return _T("Extract metadata automatically.\r\n\r\nMediaInfo.dll is probed lazily only when metadata or Content Info is inspected. If MediaInfo.dll is unavailable, eMule falls back to built-in media parsers and ID3 tags for MPEG audio.");
	}

	static CString GetMediaInfoDllPathToolTip()
	{
		return _T("Optional MediaInfo.dll lookup path.\r\n\r\nDefault: MEDIAINFO.DLL. Relative paths are resolved from the eMule program folder. Use <noload> to disable MediaInfo.dll loading while keeping built-in metadata fallbacks. Compatible MediaInfo.dll versions must be 26.01 or newer. Restart eMule after changing this if MediaInfo.dll was already probed in this session.");
	}

	static CString GetMaxChatHistoryLinesLabel()
	{
		return GetResString(IDS_TWEAKS_MAX_CHAT_HISTORY_LINES);
	}

	static CString GetMaxMessageSessionsLabel()
	{
		return GetResString(IDS_TWEAKS_MAX_MESSAGE_SESSIONS);
	}

	static CString GetGeneralAdvancedLabel()
	{
		return GetResString(IDS_TWEAKS_GENERAL_ADVANCED);
	}

	static CString GetFileBehaviorLabel()
	{
		return GetResString(IDS_TWEAKS_FILE_BEHAVIOR);
	}

	static CString GetFollowMajorityFilenameDefaultLabel()
	{
		return GetResString(IDS_FOLLOW_MAJORITY_FILENAME_NEW_DOWNLOADS);
	}

	static CString GetFollowMajorityFilenameRequiredPercentLabel()
	{
		return GetResString(IDS_TWEAKS_FOLLOW_MAJORITY_REQUIRED_AGREEMENT);
	}

	static CString GetFollowMajorityFilenameMinimumVotesLabel()
	{
		return GetResString(IDS_TWEAKS_FOLLOW_MAJORITY_MINIMUM_VOTES);
	}

	static CString GetStoragePersistenceLabel()
	{
		return GetResString(IDS_TWEAKS_STORAGE_PERSISTENCE);
	}

	static CString GetStartupTweaksLabel()
	{
		return GetResString(IDS_TWEAKS_STARTUP);
	}

	static CString GetDisplayTweaksLabel()
	{
		return GetResString(IDS_TWEAKS_DISPLAY_INDICATORS);
	}

	static CString GetSecurityTweaksLabel()
	{
		return GetResString(IDS_TWEAKS_SECURITY_FILTERING);
	}

	static CString GetLoggingTweaksLabel()
	{
		return GetResString(IDS_TWEAKS_LOGGING_DIAGNOSTICS);
	}

	static CString GetConfigDiskSpaceLabel()
	{
		CString label;
		label.Format(GetResString(IDS_TWEAKS_DISK_SPACE_LABEL_FMT), (LPCTSTR)GetResString(IDS_TWEAKS_CONFIG_DRIVE), (LPCTSTR)GetResString(IDS_MINFREEDISKSPACE));
		return label;
	}

	static CString GetTempDiskSpaceLabel()
	{
		CString label;
		label.Format(GetResString(IDS_TWEAKS_DISK_SPACE_LABEL_FMT), (LPCTSTR)GetResString(IDS_TWEAKS_TEMP_DRIVES), (LPCTSTR)GetResString(IDS_MINFREEDISKSPACE));
		return label;
	}

	static CString GetIncomingDiskSpaceLabel()
	{
		CString label;
		label.Format(GetResString(IDS_TWEAKS_DISK_SPACE_LABEL_FMT), (LPCTSTR)GetResString(IDS_TWEAKS_INCOMING_DRIVES), (LPCTSTR)GetResString(IDS_MINFREEDISKSPACE));
		return label;
	}

	static CString GetCommitPolicyLabel()
	{
		return GetResString(IDS_TWEAKS_FLUSH_FILE_DATA);
	}

	static CString GetCommitNeverLabel()
	{
		return GetResString(IDS_TWEAKS_COMMIT_NEVER);
	}

	static CString GetCommitOnShutdownLabel()
	{
		return GetResString(IDS_TWEAKS_COMMIT_ON_SHUTDOWN);
	}

	static CString GetCommitAlwaysLabel()
	{
		return GetResString(IDS_TWEAKS_COMMIT_ALWAYS);
	}
}


///////////////////////////////////////////////////////////////////////////////
// CPPgTweaks dialog

IMPLEMENT_DYNAMIC(CPPgTweaks, CPropertyPage)

BEGIN_MESSAGE_MAP(CPPgTweaks, CPropertyPage)
	ON_WM_DESTROY()
	ON_MESSAGE(UM_TREEOPTSCTRL_NOTIFY, OnTreeOptsCtrlNotify)
	ON_NOTIFY(TVN_GETINFOTIP, IDC_EXT_OPTS, OnTvnGetInfoTip)
	ON_WM_HELPINFO()
END_MESSAGE_MAP()

CPPgTweaks::CPPgTweaks()
	: CPropertyPage(CPPgTweaks::IDD)
	, m_ctrlTreeOptions(theApp.m_iDfltImageListColorFlags)
	, m_htiA4AFSaveCpu()
	, m_htiAutoArch()
	, m_htiAutoTakeEd2kLinks()
	, m_htiBroadband()
	, m_htiMaxUploadClients()
	, m_htiSlowUploadThreshold()
	, m_htiSlowUploadGrace()
	, m_htiSlowUploadWarmup()
	, m_htiZeroUploadRateGrace()
	, m_htiSlowUploadCooldown()
	, m_htiLowRatioBoost()
	, m_htiLowRatioThreshold()
	, m_htiLowRatioBonus()
	, m_htiLowIDDivisor()
	, m_htiSessionTransferLimit()
	, m_htiSessionTransferLimitDisabled()
	, m_htiSessionTransferLimitPercent()
	, m_htiSessionTransferLimitMiB()
	, m_htiSessionTransferLimitPercentValue()
	, m_htiSessionTransferLimitMiBValue()
	, m_htiSessionTimeLimit()
	, m_htiCloseUPnPPorts()
	, m_htiCommit()
	, m_htiCommitAlways()
	, m_htiCommitNever()
	, m_htiCommitOnShutdown()
	, m_htiConditionalTCPAccept()
	, m_htiCreditSystem()
	, m_htiDebug2Disk()
	, m_htiDebugSourceExchange()
	, m_htiBeepOnError()
	, m_htiCreateCrashDump()
	, m_htiCreateCrashDumpDisabled()
	, m_htiCreateCrashDumpPrompt()
	, m_htiCreateCrashDumpAlways()
	, m_htiDateTimeFormat()
	, m_htiDateTimeFormat4Log()
	, m_htiFullVerbose()
	, m_htiHighresTimer()
	, m_htiHiddenDisplay()
	, m_htiHiddenFile()
	, m_htiHiddenSecurity()
	, m_htiHiddenStartup()
	, m_htiICH()
	, m_htiIconFlashOnNewMessage()
	, m_htiExtractMetaData()
	, m_htiExtractMetaDataID3Lib()
	, m_htiExtractMetaDataNever()
	, m_htiMediaInfoDllPath()
	, m_htiFilterLANIPs()
	, m_htiFullAlloc()
	, m_htiInspectAllFileTypes()
	, m_htiLog2Disk()
	, m_htiPerfLog()
	, m_htiPerfLogFileFormat()
	, m_htiPerfLogFileFormatCsv()
	, m_htiPerfLogFileFormatMrtg()
	, m_htiPerfLogFile()
	, m_htiPerfLogInterval()
	, m_htiLogFileFormat()
	, m_htiLogFileFormatUnicode()
	, m_htiLogFileFormatUtf8()
	, m_htiMaxLogFileSize()
	, m_htiMaxLogBuffer()
	, m_htiLogA4AF()
	, m_htiLogBannedClients()
	, m_htiLogFileSaving()
	, m_htiLogFilteredIPs()
	, m_htiLogLevel()
	, m_htiLogRatingDescReceived()
	, m_htiLogSecureIdent()
	, m_htiLogUlDlEvents()
	, m_htiConnectionTimeout()
	, m_htiGeneralAdvanced()
	, m_htiLoggingGroup()
	, m_htiMaxCon5Sec()
	, m_htiMaxHalfOpen()
	, m_htiDateTimeFormat4Lists()
	, m_htiMaxChatHistoryLines()
	, m_htiMaxMessageSessions()
	, m_htiSearchGroup()
	, m_htiSearchEd2kGroup()
	, m_htiSearchEd2kMaxResults()
	, m_htiSearchEd2kMaxMoreRequests()
	, m_htiSearchKadGroup()
	, m_htiSearchKadFileTotal()
	, m_htiSearchKadKeywordTotal()
	, m_htiSearchKadFileLifetime()
	, m_htiSearchKadKeywordLifetime()
	, m_htiPreviewCopiedArchives()
	, m_htiPreviewSmallBlocks()
	, m_htiPreviewSmallBlocksDisabled()
	, m_htiPreviewSmallBlocksAllow()
	, m_htiPreviewSmallBlocksForce()
	, m_htiPreviewOnIconDblClk()
	, m_htiShowCopyEd2kLinkCmd()
	, m_htiFollowMajorityFilenameForNewDownloads()
	, m_htiFollowMajorityFilenameRequiredPercent()
	, m_htiFollowMajorityFilenameMinimumVotes()
	, m_htiShowActiveDownloadsBold()
	, m_htiUseSystemFontForMainControls()
	, m_htiReBarToolbar()
	, m_htiShowUpDownIconInTaskbar()
	, m_htiShowVerticalHourMarkers()
	, m_htiForceSpeedsToKB()
	, m_htiGeoLocationEnabled()
	, m_htiGeoLocationCheckDays()
	, m_htiExtraPreviewWithMenu()
	, m_htiKeepUnavailableFixedSharedDirs()
	, m_htiPartiallyPurgeOldKnownFiles()
	, m_htiDetectTCPErrorFlooder()
	, m_htiTCPErrorFlooderIntervalMinutes()
	, m_htiTCPErrorFlooderThreshold()
	, m_htiRearrangeKadSearchKeywords()
	, m_htiMessageFromValidSourcesOnly()
	, m_htiAutoBroadbandIO()
	, m_htiFileBufferTimeLimit()
	, m_htiFileBufferSize()
	, m_htiQueueSize()
	, m_htiDownloadTimeout()
	, m_htiRestoreLastLogPane()
	, m_htiRestoreLastMainWndDlg()
	, m_htiStoragePersistence()
	, m_htiMinFreeDiskSpaceConfig()
	, m_htiMinFreeDiskSpaceTemp()
	, m_htiMinFreeDiskSpaceIncoming()
	, m_htiServerKeepAliveTimeout()
	, m_htiShareeMule()
	, m_htiShareeMuleMultiUser()
	, m_htiShareeMuleOldStyle()
	, m_htiShareeMulePublicUser()
	, m_htiSparsePartFiles()
	, m_htiTCPGroup()
	, m_htiUPnP()
	, m_htiUPnPBackendMode()
	, m_htiUPnPBackendModeAutomatic()
	, m_htiUPnPBackendModeIgdOnly()
	, m_htiUPnPBackendModePcpNatPmpOnly()
	, m_htiVerbose()
	, m_htiVerboseGroup()
	, m_htiTxtEditor()
	, m_htiYourHostname()
	, m_uMaxLogFileSizeKiB()
	, m_uMaxLogBufferKiB()
	, m_uMaxChatHistoryLines()
	, m_uMaxMessageSessions()
	, m_uFollowMajorityFilenameRequiredPercent()
	, m_uFollowMajorityFilenameMinimumVotes()
	, m_uPerfLogIntervalMinutes()
	, m_iMinFreeDiskSpaceConfigGB()
	, m_iMinFreeDiskSpaceTempGB()
	, m_iMinFreeDiskSpaceIncomingGB()
	, m_iQueueSize()
	, m_uFileBufferSizeKiB()
	, m_uConnectionTimeoutSeconds()
	, m_uDownloadTimeoutSeconds()
	, m_uEd2kSearchMaxResults()
	, m_uEd2kSearchMaxMoreRequests()
	, m_uKadFileSearchTotal()
	, m_uKadKeywordSearchTotal()
	, m_uKadFileSearchLifetimeSeconds()
	, m_uKadKeywordSearchLifetimeSeconds()
	, m_iTCPErrorFlooderIntervalMinutes()
	, m_iTCPErrorFlooderThreshold()
	, m_uServerKeepAliveTimeout()
	, m_iCommitFiles()
	, m_iExtractMetaData()
	, m_iCreateCrashDumpMode()
	, m_iLogFileFormat()
	, m_iPerfLogFileFormat()
	, m_iPreviewSmallBlocks()
	, m_bInspectAllFileTypes()
	, m_iLogLevel()
	, m_iMaxConnPerFive()
	, m_iMaxHalfOpen()
	, m_iShareeMule()
	, m_iSessionTransferLimitMode()
	, m_iUPnPBackendMode(UPNP_BACKEND_AUTOMATIC)
	, m_sDateTimeFormat4Lists()
	, m_sDateTimeFormat()
	, m_sDateTimeFormat4Log()
	, m_sMediaInfoDllPath()
	, m_sPerfLogFile()
	, m_sTxtEditor()
	, m_sSlowUploadThresholdFactor()
	, m_sLowRatioThreshold()
	, m_bA4AFSaveCpu()
	, m_bAutoArchDisable(true)
	, m_bAutoTakeEd2kLinks()
	, m_bBeepOnError()
	, m_bLowRatioBoost()
	, m_bCloseUPnPOnExit(true)
	, m_bConditionalTCPAccept()
	, m_bCreditSystem()
	, m_bDebug2Disk()
	, m_bDebugSourceExchange()
	, m_bExtraPreviewWithMenu()
	, m_bFilterLANIPs()
	, m_bFullAlloc()
	, m_bFullVerbose()
	, m_bGeoLocationEnabled()
	, m_bHighresTimer()
	, m_bICH()
	, m_bIconFlashOnNewMessage()
	, m_bInitializedTreeOpts()
	, m_bKeepUnavailableFixedSharedDirs()
	, m_bLog2Disk()
	, m_bPerfLogEnabled()
	, m_bLogA4AF()
	, m_bLogBannedClients()
	, m_bLogFileSaving()
	, m_bLogFilteredIPs()
	, m_bLogRatingDescReceived()
	, m_bLogSecureIdent()
	, m_bLogUlDlEvents()
	, m_bMessageFromValidSourcesOnly()
	, m_bPartiallyPurgeOldKnownFiles()
	, m_bPreviewCopiedArchives()
	, m_bPreviewOnIconDblClk()
	, m_bDetectTCPErrorFlooder()
	, m_bRearrangeKadSearchKeywords()
	, m_bReBarToolbar()
	, m_bRestoreLastLogPane()
	, m_bRestoreLastMainWndDlg()
	, m_bShowedWarning()
	, m_bShowActiveDownloadsBold()
	, m_bShowCopyEd2kLinkCmd()
	, m_bFollowMajorityFilenameForNewDownloads()
	, m_bShowUpDownIconInTaskbar()
	, m_bShowVerticalHourMarkers()
	, m_bSparsePartFiles()
	, m_bUseSystemFontForMainControls()
	, m_bVerbose()
	, m_bForceSpeedsToKB()
	, m_bAutoBroadbandIO()
	, m_uFileBufferTimeLimitSeconds()
	, m_uGeoLocationCheckDays()
	, m_iMaxUploadClients()
	, m_iSlowUploadGraceSeconds()
	, m_iSlowUploadWarmupSeconds()
	, m_iZeroUploadRateGraceSeconds()
	, m_iSlowUploadCooldownSeconds()
	, m_iLowRatioBonus()
	, m_iLowIDDivisor()
	, m_iSessionTransferLimitPercent()
	, m_iSessionTransferLimitMiB()
	, m_iSessionTimeLimitSeconds()
{
}

void CPPgTweaks::DoDataExchange(CDataExchange *pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_EXT_OPTS, m_ctrlTreeOptions);
	if (!m_bInitializedTreeOpts) {
		int iImgBackup = 8; // default icon
		int iImgLog = 8;
		int iImgDynyp = 8;
		int iImgConnection = 8;
		int iImgSearch = 8;
		//	int iImgA4AF = 8;
		int iImgMetaData = 8;
		int iImgUPnP = 8;
		int iImgShareeMule = 8;
		CImageList *piml = m_ctrlTreeOptions.GetImageList(TVSIL_NORMAL);
		if (piml) {
			iImgBackup = piml->Add(CTempIconLoader(_T("Harddisk")));
			iImgLog = piml->Add(CTempIconLoader(_T("Log")));
			iImgDynyp = piml->Add(CTempIconLoader(_T("upload")));
			iImgConnection = piml->Add(CTempIconLoader(_T("connection")));
			iImgSearch = piml->Add(CTempIconLoader(_T("SearchResults")));
			//	iImgA4AF = piml->Add(CTempIconLoader(_T("Download")));
			iImgMetaData = piml->Add(CTempIconLoader(_T("MediaInfo")));
			iImgUPnP = piml->Add(CTempIconLoader(_T("connectedhighhigh")));
			iImgShareeMule = piml->Add(CTempIconLoader(_T("viewfiles")));
		}

		/////////////////////////////////////////////////////////////////////////////
		// TCP/IP group
		//
		m_htiTCPGroup = m_ctrlTreeOptions.InsertGroup(GetResString(IDS_TCPIP_CONNS), iImgConnection, TVI_ROOT);
		m_htiMaxCon5Sec = m_ctrlTreeOptions.InsertItem(GetResString(IDS_MAXCON5SECLABEL), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiTCPGroup);
		m_ctrlTreeOptions.AddEditBox(m_htiMaxCon5Sec, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiMaxHalfOpen = m_ctrlTreeOptions.InsertItem(GetResString(IDS_MAXHALFOPENCONS), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiTCPGroup);
		m_ctrlTreeOptions.AddEditBox(m_htiMaxHalfOpen, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiConnectionTimeout = m_ctrlTreeOptions.InsertItem(GetResString(IDS_CONNECTIONTIMEOUT), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiTCPGroup);
		m_ctrlTreeOptions.AddEditBox(m_htiConnectionTimeout, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDownloadTimeout = m_ctrlTreeOptions.InsertItem(GetResString(IDS_DOWNLOADTIMEOUT), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiTCPGroup);
		m_ctrlTreeOptions.AddEditBox(m_htiDownloadTimeout, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiConditionalTCPAccept = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_CONDTCPACCEPT), m_htiTCPGroup, m_bConditionalTCPAccept);
		m_htiServerKeepAliveTimeout = m_ctrlTreeOptions.InsertItem(GetResString(IDS_SERVERKEEPALIVETIMEOUT), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiTCPGroup);
		m_ctrlTreeOptions.AddEditBox(m_htiServerKeepAliveTimeout, RUNTIME_CLASS(CNumTreeOptionsEdit));

		/////////////////////////////////////////////////////////////////////////////
		// Search group
		//
		m_htiSearchGroup = m_ctrlTreeOptions.InsertGroup(GetResString(IDS_SEARCHLIMITS), iImgSearch, TVI_ROOT);
		m_htiSearchEd2kGroup = m_ctrlTreeOptions.InsertGroup(GetResString(IDS_ED2K_SEARCH), iImgSearch, m_htiSearchGroup);
		m_htiSearchEd2kMaxResults = m_ctrlTreeOptions.InsertItem(GetResString(IDS_ED2K_SEARCH_MAX_RESULTS), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiSearchEd2kGroup);
		m_ctrlTreeOptions.AddEditBox(m_htiSearchEd2kMaxResults, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiSearchEd2kMaxMoreRequests = m_ctrlTreeOptions.InsertItem(GetResString(IDS_ED2K_SEARCH_MAX_MORE), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiSearchEd2kGroup);
		m_ctrlTreeOptions.AddEditBox(m_htiSearchEd2kMaxMoreRequests, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiSearchKadGroup = m_ctrlTreeOptions.InsertGroup(GetResString(IDS_SEARCHKAD), iImgSearch, m_htiSearchGroup);
		m_htiSearchKadFileTotal = m_ctrlTreeOptions.InsertItem(GetResString(IDS_KAD_SEARCH_FILE_TOTAL), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiSearchKadGroup);
		m_ctrlTreeOptions.AddEditBox(m_htiSearchKadFileTotal, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiSearchKadKeywordTotal = m_ctrlTreeOptions.InsertItem(GetResString(IDS_KAD_SEARCH_KEYWORD_TOTAL), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiSearchKadGroup);
		m_ctrlTreeOptions.AddEditBox(m_htiSearchKadKeywordTotal, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiSearchKadFileLifetime = m_ctrlTreeOptions.InsertItem(GetResString(IDS_KAD_SEARCH_FILE_LIFETIME), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiSearchKadGroup);
		m_ctrlTreeOptions.AddEditBox(m_htiSearchKadFileLifetime, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiSearchKadKeywordLifetime = m_ctrlTreeOptions.InsertItem(GetResString(IDS_KAD_SEARCH_KEYWORD_LIFETIME), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiSearchKadGroup);
		m_ctrlTreeOptions.AddEditBox(m_htiSearchKadKeywordLifetime, RUNTIME_CLASS(CNumTreeOptionsEdit));

		/////////////////////////////////////////////////////////////////////////////
		// Broadband group
		//
		m_htiBroadband = m_ctrlTreeOptions.InsertGroup(GetResString(IDS_BROADBAND), iImgDynyp, TVI_ROOT);
		m_htiMaxUploadClients = m_ctrlTreeOptions.InsertItem(GetResString(IDS_UPLOAD_POLICY_MAX_UPLOAD_CLIENTS), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiBroadband);
		m_ctrlTreeOptions.AddEditBox(m_htiMaxUploadClients, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiSlowUploadThreshold = m_ctrlTreeOptions.InsertItem(GetResString(IDS_UPLOAD_POLICY_SLOW_THRESHOLD_FACTOR), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiBroadband);
		m_ctrlTreeOptions.AddEditBox(m_htiSlowUploadThreshold, RUNTIME_CLASS(CTreeOptionsEditEx));
		m_htiSlowUploadGrace = m_ctrlTreeOptions.InsertItem(GetResString(IDS_UPLOAD_POLICY_SLOW_GRACE_SECONDS), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiBroadband);
		m_ctrlTreeOptions.AddEditBox(m_htiSlowUploadGrace, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiSlowUploadWarmup = m_ctrlTreeOptions.InsertItem(GetResString(IDS_UPLOAD_POLICY_SLOW_WARMUP_SECONDS), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiBroadband);
		m_ctrlTreeOptions.AddEditBox(m_htiSlowUploadWarmup, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiZeroUploadRateGrace = m_ctrlTreeOptions.InsertItem(GetResString(IDS_UPLOAD_POLICY_ZERO_RATE_GRACE_SECONDS), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiBroadband);
		m_ctrlTreeOptions.AddEditBox(m_htiZeroUploadRateGrace, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiSlowUploadCooldown = m_ctrlTreeOptions.InsertItem(GetResString(IDS_UPLOAD_POLICY_COOLDOWN_SECONDS), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiBroadband);
		m_ctrlTreeOptions.AddEditBox(m_htiSlowUploadCooldown, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiLowRatioBoost = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_UPLOAD_POLICY_LOW_RATIO_BOOST), m_htiBroadband, m_bLowRatioBoost);
		m_htiLowRatioThreshold = m_ctrlTreeOptions.InsertItem(GetResString(IDS_UPLOAD_POLICY_RATIO_THRESHOLD), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiLowRatioBoost);
		m_ctrlTreeOptions.AddEditBox(m_htiLowRatioThreshold, RUNTIME_CLASS(CTreeOptionsEditEx));
		m_htiLowRatioBonus = m_ctrlTreeOptions.InsertItem(GetResString(IDS_UPLOAD_POLICY_SCORE_BONUS), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiLowRatioBoost);
		m_ctrlTreeOptions.AddEditBox(m_htiLowRatioBonus, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiLowIDDivisor = m_ctrlTreeOptions.InsertItem(GetResString(IDS_UPLOAD_POLICY_LOWID_DIVISOR), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiBroadband);
		m_ctrlTreeOptions.AddEditBox(m_htiLowIDDivisor, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiSessionTransferLimit = m_ctrlTreeOptions.InsertGroup(GetResString(IDS_UPLOAD_POLICY_SESSION_TRANSFER_LIMIT), iImgDynyp, m_htiBroadband);
		m_htiSessionTransferLimitDisabled = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_DISABLED), m_htiSessionTransferLimit, m_iSessionTransferLimitMode == static_cast<int>(ESessionTransferLimitMode::Disabled));
		m_htiSessionTransferLimitPercent = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_UPLOAD_POLICY_PERCENT_OF_FILE_SIZE), m_htiSessionTransferLimit, m_iSessionTransferLimitMode == static_cast<int>(ESessionTransferLimitMode::PercentOfFile));
		m_htiSessionTransferLimitPercentValue = m_ctrlTreeOptions.InsertItem(GetResString(IDS_UPLOAD_POLICY_SESSION_TRANSFER_PERCENT_VALUE), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiSessionTransferLimitPercent);
		m_ctrlTreeOptions.AddEditBox(m_htiSessionTransferLimitPercentValue, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiSessionTransferLimitMiB = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_UPLOAD_POLICY_ABSOLUTE_MIB), m_htiSessionTransferLimit, m_iSessionTransferLimitMode == static_cast<int>(ESessionTransferLimitMode::AbsoluteMiB));
		m_htiSessionTransferLimitMiBValue = m_ctrlTreeOptions.InsertItem(GetResString(IDS_UPLOAD_POLICY_SESSION_TRANSFER_MIB_VALUE), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiSessionTransferLimitMiB);
		m_ctrlTreeOptions.AddEditBox(m_htiSessionTransferLimitMiBValue, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiSessionTimeLimit = m_ctrlTreeOptions.InsertItem(GetResString(IDS_UPLOAD_POLICY_SESSION_TIME_LIMIT), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiBroadband);
		m_ctrlTreeOptions.AddEditBox(m_htiSessionTimeLimit, RUNTIME_CLASS(CNumTreeOptionsEdit));

		/////////////////////////////////////////////////////////////////////////////
		// General advanced group
		//
		m_htiGeneralAdvanced = m_ctrlTreeOptions.InsertGroup(GetGeneralAdvancedLabel(), iImgConnection, TVI_ROOT);
		m_htiAutoTakeEd2kLinks = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_AUTOTAKEED2KLINKS), m_htiGeneralAdvanced, m_bAutoTakeEd2kLinks);
		m_htiCreditSystem = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_USECREDITSYSTEM), m_htiGeneralAdvanced, m_bCreditSystem);
		m_htiA4AFSaveCpu = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_A4AF_SAVE_CPU), m_htiGeneralAdvanced, m_bA4AFSaveCpu); // ZZ:DownloadManager
		m_htiAutoArch = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_DISABLE_AUTOARCHPREV), m_htiGeneralAdvanced, m_bAutoArchDisable);
		m_htiYourHostname = m_ctrlTreeOptions.InsertItem(GetResString(IDS_YOURHOSTNAME), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiGeneralAdvanced);
		m_ctrlTreeOptions.AddEditBox(m_htiYourHostname, RUNTIME_CLASS(CTreeOptionsEditEx));
		m_htiTxtEditor = m_ctrlTreeOptions.InsertItem(GetTxtEditorLabel(), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiGeneralAdvanced);
		m_ctrlTreeOptions.AddEditBox(m_htiTxtEditor, RUNTIME_CLASS(CTreeOptionsEditEx));
		m_htiHighresTimer = m_ctrlTreeOptions.InsertCheckBox(GetHighresTimerLabel(), m_htiGeneralAdvanced, m_bHighresTimer);

		/////////////////////////////////////////////////////////////////////////////
		// File behavior group
		//
		m_htiHiddenFile = m_ctrlTreeOptions.InsertGroup(GetFileBehaviorLabel(), iImgMetaData, TVI_ROOT);
		m_htiICH = m_ctrlTreeOptions.InsertCheckBox(GetIchLabel(), m_htiHiddenFile, m_bICH);
		m_htiPreviewSmallBlocks = m_ctrlTreeOptions.InsertGroup(GetPreviewSmallBlocksLabel(), iImgMetaData, m_htiHiddenFile);
		m_htiPreviewSmallBlocksDisabled = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_DISABLED), m_htiPreviewSmallBlocks, m_iPreviewSmallBlocks == 0);
		m_htiPreviewSmallBlocksAllow = m_ctrlTreeOptions.InsertRadioButton(GetPreviewSmallBlocksAllowLabel(), m_htiPreviewSmallBlocks, m_iPreviewSmallBlocks == 1);
		m_htiPreviewSmallBlocksForce = m_ctrlTreeOptions.InsertRadioButton(GetPreviewSmallBlocksForceLabel(), m_htiPreviewSmallBlocks, m_iPreviewSmallBlocks == 2);
		m_htiBeepOnError = m_ctrlTreeOptions.InsertCheckBox(GetBeepOnErrorLabel(), m_htiHiddenFile, m_bBeepOnError);
		m_htiShowCopyEd2kLinkCmd = m_ctrlTreeOptions.InsertCheckBox(GetShowCopyEd2kLinkCmdLabel(), m_htiHiddenFile, m_bShowCopyEd2kLinkCmd);
		m_htiFollowMajorityFilenameForNewDownloads = m_ctrlTreeOptions.InsertCheckBox(GetFollowMajorityFilenameDefaultLabel(), m_htiHiddenFile, m_bFollowMajorityFilenameForNewDownloads);
		m_htiFollowMajorityFilenameRequiredPercent = m_ctrlTreeOptions.InsertItem(GetFollowMajorityFilenameRequiredPercentLabel(), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiFollowMajorityFilenameForNewDownloads);
		m_ctrlTreeOptions.AddEditBox(m_htiFollowMajorityFilenameRequiredPercent, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiFollowMajorityFilenameMinimumVotes = m_ctrlTreeOptions.InsertItem(GetFollowMajorityFilenameMinimumVotesLabel(), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiFollowMajorityFilenameForNewDownloads);
		m_ctrlTreeOptions.AddEditBox(m_htiFollowMajorityFilenameMinimumVotes, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiSparsePartFiles = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_SPARSEPARTFILES), m_htiHiddenFile, m_bSparsePartFiles);
		m_htiFullAlloc = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_FULLALLOC), m_htiHiddenFile, m_bFullAlloc);
		m_htiExtractMetaData = m_ctrlTreeOptions.InsertGroup(GetResString(IDS_EXTRACT_META_DATA), iImgMetaData, m_htiHiddenFile);
		m_htiExtractMetaDataNever = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_NEVER), m_htiExtractMetaData, m_iExtractMetaData == 0);
		m_htiExtractMetaDataID3Lib = m_ctrlTreeOptions.InsertRadioButton(GetExtractMetaDataAutoLabel(), m_htiExtractMetaData, m_iExtractMetaData == 1);
		//m_htiExtractMetaDataMediaDet = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_META_DATA_MEDIADET), m_htiExtractMetaData, m_iExtractMetaData == 2);
		m_htiMediaInfoDllPath = m_ctrlTreeOptions.InsertItem(GetMediaInfoDllPathLabel(), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiExtractMetaData);
		m_ctrlTreeOptions.AddEditBox(m_htiMediaInfoDllPath, RUNTIME_CLASS(CTreeOptionsEditEx));
		m_htiDateTimeFormat4Lists = m_ctrlTreeOptions.InsertItem(GetResString(IDS_DATETIMEFORMAT4LISTS), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiHiddenFile);
		m_ctrlTreeOptions.AddEditBox(m_htiDateTimeFormat4Lists, RUNTIME_CLASS(CTreeOptionsEditEx));
		m_htiPreviewCopiedArchives = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_PREVIEWCOPIEDARCHIVES), m_htiHiddenFile, m_bPreviewCopiedArchives);
		m_htiInspectAllFileTypes = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_INSPECTALLFILETYPES), m_htiHiddenFile, m_bInspectAllFileTypes);
		m_htiPreviewOnIconDblClk = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_PREVIEWONICONDBLCLK), m_htiHiddenFile, m_bPreviewOnIconDblClk);
		m_htiExtraPreviewWithMenu = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_EXTRAPREVIEWWITHMENU), m_htiHiddenFile, m_bExtraPreviewWithMenu);
		m_htiKeepUnavailableFixedSharedDirs = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_KEEPUNAVAILABLEFIXEDSHAREDDIRS), m_htiHiddenFile, m_bKeepUnavailableFixedSharedDirs);
		m_htiPartiallyPurgeOldKnownFiles = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_PARTIALLYPURGEOLDKNOWNFILES), m_htiHiddenFile, m_bPartiallyPurgeOldKnownFiles);

		/////////////////////////////////////////////////////////////////////////////
		// Storage & persistence group
		//
		m_htiStoragePersistence = m_ctrlTreeOptions.InsertGroup(GetStoragePersistenceLabel(), iImgBackup, TVI_ROOT);
		m_htiMinFreeDiskSpaceConfig = m_ctrlTreeOptions.InsertItem(GetConfigDiskSpaceLabel(), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiStoragePersistence);
		m_ctrlTreeOptions.AddEditBox(m_htiMinFreeDiskSpaceConfig, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiMinFreeDiskSpaceTemp = m_ctrlTreeOptions.InsertItem(GetTempDiskSpaceLabel(), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiStoragePersistence);
		m_ctrlTreeOptions.AddEditBox(m_htiMinFreeDiskSpaceTemp, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiMinFreeDiskSpaceIncoming = m_ctrlTreeOptions.InsertItem(GetIncomingDiskSpaceLabel(), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiStoragePersistence);
		m_ctrlTreeOptions.AddEditBox(m_htiMinFreeDiskSpaceIncoming, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiAutoBroadbandIO = m_ctrlTreeOptions.InsertCheckBox(_T("Auto broadband IO buffers"), m_htiStoragePersistence, m_bAutoBroadbandIO);
		m_htiFileBufferTimeLimit = m_ctrlTreeOptions.InsertItem(GetResString(IDS_FILEBUFFERTIMELIMIT), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiStoragePersistence);
		m_ctrlTreeOptions.AddEditBox(m_htiFileBufferTimeLimit, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiFileBufferSize = m_ctrlTreeOptions.InsertItem(GetFileBufferSizeLabel(), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiStoragePersistence);
		m_ctrlTreeOptions.AddEditBox(m_htiFileBufferSize, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiQueueSize = m_ctrlTreeOptions.InsertItem(GetResString(IDS_QUEUESIZE), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiStoragePersistence);
		m_ctrlTreeOptions.AddEditBox(m_htiQueueSize, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiCommit = m_ctrlTreeOptions.InsertGroup(GetCommitPolicyLabel(), iImgBackup, m_htiStoragePersistence);
		m_htiCommitNever = m_ctrlTreeOptions.InsertRadioButton(GetCommitNeverLabel(), m_htiCommit, m_iCommitFiles == 0);
		m_htiCommitOnShutdown = m_ctrlTreeOptions.InsertRadioButton(GetCommitOnShutdownLabel(), m_htiCommit, m_iCommitFiles == 1);
		m_htiCommitAlways = m_ctrlTreeOptions.InsertRadioButton(GetCommitAlwaysLabel(), m_htiCommit, m_iCommitFiles == 2);

		/////////////////////////////////////////////////////////////////////////////
		// Startup group
		//
		m_htiHiddenStartup = m_ctrlTreeOptions.InsertGroup(GetStartupTweaksLabel(), iImgConnection, TVI_ROOT);
		m_htiRestoreLastMainWndDlg = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_RESTORELASTMAINWNDDLG), m_htiHiddenStartup, m_bRestoreLastMainWndDlg);
		m_htiRestoreLastLogPane = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_RESTORELASTLOGPANE), m_htiHiddenStartup, m_bRestoreLastLogPane);

		/////////////////////////////////////////////////////////////////////////////
		// Display group
		//
		m_htiHiddenDisplay = m_ctrlTreeOptions.InsertGroup(GetDisplayTweaksLabel(), iImgLog, TVI_ROOT);
		m_htiIconFlashOnNewMessage = m_ctrlTreeOptions.InsertCheckBox(GetIconFlashOnNewMessageLabel(), m_htiHiddenDisplay, m_bIconFlashOnNewMessage);
		m_htiDateTimeFormat = m_ctrlTreeOptions.InsertItem(GetDateTimeFormatLabel(), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiHiddenDisplay);
		m_ctrlTreeOptions.AddEditBox(m_htiDateTimeFormat, RUNTIME_CLASS(CTreeOptionsEditEx));
		m_htiDateTimeFormat4Log = m_ctrlTreeOptions.InsertItem(GetDateTimeFormat4LogLabel(), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiHiddenDisplay);
		m_ctrlTreeOptions.AddEditBox(m_htiDateTimeFormat4Log, RUNTIME_CLASS(CTreeOptionsEditEx));
		m_htiShowActiveDownloadsBold = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_SHOWACTIVEDOWNLOADSBOLD), m_htiHiddenDisplay, m_bShowActiveDownloadsBold);
		m_htiUseSystemFontForMainControls = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_USESYSTEMFONTFORMAINCONTROLS), m_htiHiddenDisplay, m_bUseSystemFontForMainControls);
		m_htiReBarToolbar = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_REBARTOOLBAR), m_htiHiddenDisplay, m_bReBarToolbar);
		m_htiShowUpDownIconInTaskbar = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_SHOWUPDOWNICONINTASKBAR), m_htiHiddenDisplay, m_bShowUpDownIconInTaskbar);
		m_htiShowVerticalHourMarkers = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_SHOWVERTICALHOURMARKERS), m_htiHiddenDisplay, m_bShowVerticalHourMarkers);
		m_htiForceSpeedsToKB = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_FORCESPEEDSTOKB), m_htiHiddenDisplay, m_bForceSpeedsToKB);
		m_htiGeoLocationEnabled = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_ENABLE_GEOLOCATION), m_htiHiddenDisplay, m_bGeoLocationEnabled);
		m_htiGeoLocationCheckDays = m_ctrlTreeOptions.InsertItem(GetGeoLocationIntervalLabel(), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiGeoLocationEnabled);
		m_ctrlTreeOptions.AddEditBox(m_htiGeoLocationCheckDays, RUNTIME_CLASS(CNumTreeOptionsEdit));

		/////////////////////////////////////////////////////////////////////////////
		// Security group
		//
		m_htiHiddenSecurity = m_ctrlTreeOptions.InsertGroup(GetSecurityTweaksLabel(), iImgConnection, TVI_ROOT);
		m_htiFilterLANIPs = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_PW_FILTER), m_htiHiddenSecurity, m_bFilterLANIPs);
		m_htiDetectTCPErrorFlooder = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_DETECT_TCP_ERROR_FLOODER), m_htiHiddenSecurity, m_bDetectTCPErrorFlooder);
		m_htiTCPErrorFlooderIntervalMinutes = m_ctrlTreeOptions.InsertItem(GetResString(IDS_TCP_ERROR_FLOODER_INTERVAL_MINUTES), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDetectTCPErrorFlooder);
		m_ctrlTreeOptions.AddEditBox(m_htiTCPErrorFlooderIntervalMinutes, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiTCPErrorFlooderThreshold = m_ctrlTreeOptions.InsertItem(GetResString(IDS_TCP_ERROR_FLOODER_THRESHOLD), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDetectTCPErrorFlooder);
		m_ctrlTreeOptions.AddEditBox(m_htiTCPErrorFlooderThreshold, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiRearrangeKadSearchKeywords = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_REARRANGEKADSEARCHKEYWORDS), m_htiHiddenSecurity, m_bRearrangeKadSearchKeywords);
		m_htiMessageFromValidSourcesOnly = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_MESSAGEFROMVALIDSOURCESONLY), m_htiHiddenSecurity, m_bMessageFromValidSourcesOnly);
		m_htiMaxChatHistoryLines = m_ctrlTreeOptions.InsertItem(GetMaxChatHistoryLinesLabel(), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiHiddenSecurity);
		m_ctrlTreeOptions.AddEditBox(m_htiMaxChatHistoryLines, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiMaxMessageSessions = m_ctrlTreeOptions.InsertItem(GetMaxMessageSessionsLabel(), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiHiddenSecurity);
		m_ctrlTreeOptions.AddEditBox(m_htiMaxMessageSessions, RUNTIME_CLASS(CNumTreeOptionsEdit));

		/////////////////////////////////////////////////////////////////////////////
		// Logging group
		//
		m_htiLoggingGroup = m_ctrlTreeOptions.InsertGroup(GetLoggingTweaksLabel(), iImgLog, TVI_ROOT);
		m_htiCreateCrashDump = m_ctrlTreeOptions.InsertGroup(GetCreateCrashDumpLabel(), iImgLog, m_htiLoggingGroup);
		m_htiCreateCrashDumpDisabled = m_ctrlTreeOptions.InsertRadioButton(GetCreateCrashDumpDisabledLabel(), m_htiCreateCrashDump, m_iCreateCrashDumpMode == 0);
		m_htiCreateCrashDumpPrompt = m_ctrlTreeOptions.InsertRadioButton(GetCreateCrashDumpPromptLabel(), m_htiCreateCrashDump, m_iCreateCrashDumpMode == 1);
		m_htiCreateCrashDumpAlways = m_ctrlTreeOptions.InsertRadioButton(GetCreateCrashDumpAlwaysLabel(), m_htiCreateCrashDump, m_iCreateCrashDumpMode == 2);
		m_htiMaxLogFileSize = m_ctrlTreeOptions.InsertItem(GetMaxLogFileSizeLabel(), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiLoggingGroup);
		m_ctrlTreeOptions.AddEditBox(m_htiMaxLogFileSize, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiMaxLogBuffer = m_ctrlTreeOptions.InsertItem(GetMaxLogBufferLabel(), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiLoggingGroup);
		m_ctrlTreeOptions.AddEditBox(m_htiMaxLogBuffer, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiLogFileFormat = m_ctrlTreeOptions.InsertGroup(GetLogFileFormatLabel(), iImgLog, m_htiLoggingGroup);
		m_htiLogFileFormatUnicode = m_ctrlTreeOptions.InsertRadioButton(GetLogFileFormatUnicodeLabel(), m_htiLogFileFormat, m_iLogFileFormat == Unicode);
		m_htiLogFileFormatUtf8 = m_ctrlTreeOptions.InsertRadioButton(GetLogFileFormatUtf8Label(), m_htiLogFileFormat, m_iLogFileFormat == Utf8);
		m_htiLog2Disk = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_LOG2DISK), m_htiLoggingGroup, m_bLog2Disk);
		m_htiPerfLog = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_ENABLE_PERFLOG), m_htiLoggingGroup, m_bPerfLogEnabled);
		m_htiPerfLogFileFormat = m_ctrlTreeOptions.InsertGroup(GetPerfLogFileFormatLabel(), iImgLog, m_htiPerfLog);
		m_htiPerfLogFileFormatCsv = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_TWEAKS_PERF_LOG_FORMAT_CSV), m_htiPerfLogFileFormat, m_iPerfLogFileFormat == CPerfLog::CSV);
		m_htiPerfLogFileFormatMrtg = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_TWEAKS_PERF_LOG_FORMAT_MRTG), m_htiPerfLogFileFormat, m_iPerfLogFileFormat == CPerfLog::MRTG);
		m_htiPerfLogFile = m_ctrlTreeOptions.InsertItem(GetPerfLogFileLabel(), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiPerfLog);
		m_ctrlTreeOptions.AddEditBox(m_htiPerfLogFile, RUNTIME_CLASS(CTreeOptionsEditEx));
		m_htiPerfLogInterval = m_ctrlTreeOptions.InsertItem(GetPerfLogIntervalLabel(), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiPerfLog);
		m_ctrlTreeOptions.AddEditBox(m_htiPerfLogInterval, RUNTIME_CLASS(CNumTreeOptionsEdit));
		if (thePrefs.GetEnableVerboseOptions()) {
			m_htiVerboseGroup = m_ctrlTreeOptions.InsertGroup(GetResString(IDS_VERBOSE), iImgLog, m_htiLoggingGroup);
			m_htiVerbose = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_ENABLED), m_htiVerboseGroup, m_bVerbose);
			m_htiFullVerbose = m_ctrlTreeOptions.InsertCheckBox(GetFullVerboseLabel(), m_htiVerboseGroup, m_bFullVerbose);
			m_htiLogLevel = m_ctrlTreeOptions.InsertItem(GetResString(IDS_LOG_LEVEL), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiVerboseGroup);
			m_ctrlTreeOptions.AddEditBox(m_htiLogLevel, RUNTIME_CLASS(CNumTreeOptionsEdit));
			m_htiDebug2Disk = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_LOG2DISK), m_htiVerboseGroup, m_bDebug2Disk);
			m_htiDebugSourceExchange = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_DEBUG_SOURCE_EXCHANGE), m_htiVerboseGroup, m_bDebugSourceExchange);
			m_htiLogBannedClients = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_LOG_BANNED_CLIENTS), m_htiVerboseGroup, m_bLogBannedClients);
			m_htiLogRatingDescReceived = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_LOG_RATING_RECV), m_htiVerboseGroup, m_bLogRatingDescReceived);
			m_htiLogSecureIdent = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_LOG_SECURE_IDENT), m_htiVerboseGroup, m_bLogSecureIdent);
			m_htiLogFilteredIPs = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_LOG_FILTERED_IPS), m_htiVerboseGroup, m_bLogFilteredIPs);
			m_htiLogFileSaving = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_LOG_FILE_SAVING), m_htiVerboseGroup, m_bLogFileSaving);
			m_htiLogA4AF = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_LOG_A4AF), m_htiVerboseGroup, m_bLogA4AF); // ZZ:DownloadManager
			m_htiLogUlDlEvents = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_LOG_ULDL_EVENTS), m_htiVerboseGroup, m_bLogUlDlEvents);
		}

		/////////////////////////////////////////////////////////////////////////////
		// UPnP group
		//
		m_htiUPnP = m_ctrlTreeOptions.InsertGroup(GetResString(IDS_UPNP), iImgUPnP, TVI_ROOT);
		m_htiCloseUPnPPorts = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_UPNPCLOSEONEXIT), m_htiUPnP, m_bCloseUPnPOnExit);
		m_htiUPnPBackendMode = m_ctrlTreeOptions.InsertGroup(GetResString(IDS_UPNPBACKENDMODE), iImgUPnP, m_htiUPnP);
		m_htiUPnPBackendModeAutomatic = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_AUTOMATIC), m_htiUPnPBackendMode, m_iUPnPBackendMode == UPNP_BACKEND_AUTOMATIC);
		m_htiUPnPBackendModeIgdOnly = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_UPNPBACKENDMODE_IGDONLY), m_htiUPnPBackendMode, m_iUPnPBackendMode == UPNP_BACKEND_IGD_ONLY);
		m_htiUPnPBackendModePcpNatPmpOnly = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_UPNPBACKENDMODE_PCPONLY), m_htiUPnPBackendMode, m_iUPnPBackendMode == UPNP_BACKEND_PCP_NATPMP_ONLY);

		/////////////////////////////////////////////////////////////////////////////
		// eMule Shared Use
		//
		m_htiShareeMule = m_ctrlTreeOptions.InsertGroup(GetResString(IDS_SHAREEMULELABEL), iImgShareeMule, TVI_ROOT);
		m_htiShareeMuleMultiUser = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_SHAREEMULEMULTI), m_htiShareeMule, m_iShareeMule == 0);
		m_htiShareeMulePublicUser = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_SHAREEMULEPUBLIC), m_htiShareeMule, m_iShareeMule == 1);
		m_htiShareeMuleOldStyle = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_SHAREEMULEOLD), m_htiShareeMule, m_iShareeMule == 2);

		SetTreeToolTip(m_htiConditionalTCPAccept, IDS_TWEAKS_TT_CONDITIONAL_TCP_ACCEPT);
		SetTreeToolTip(m_htiMaxCon5Sec, IDS_TWEAKS_TT_MAX_CON5_SEC);
		SetTreeToolTip(m_htiMaxHalfOpen, IDS_TWEAKS_TT_MAX_HALF_OPEN);
		SetTreeToolTip(m_htiConnectionTimeout, IDS_TWEAKS_TT_CONNECTION_TIMEOUT);
		SetTreeToolTip(m_htiDownloadTimeout, IDS_TWEAKS_TT_DOWNLOAD_TIMEOUT);
		SetTreeToolTip(m_htiServerKeepAliveTimeout, IDS_TWEAKS_TT_SERVER_KEEP_ALIVE_TIMEOUT);
		SetTreeToolTip(m_htiSearchEd2kMaxResults, IDS_TWEAKS_TT_SEARCH_ED2K_MAX_RESULTS);
		SetTreeToolTip(m_htiSearchEd2kMaxMoreRequests, IDS_TWEAKS_TT_SEARCH_ED2K_MAX_MORE_REQUESTS);
		SetTreeToolTip(m_htiSearchKadFileTotal, IDS_TWEAKS_TT_SEARCH_KAD_FILE_TOTAL);
		SetTreeToolTip(m_htiSearchKadKeywordTotal, IDS_TWEAKS_TT_SEARCH_KAD_KEYWORD_TOTAL);
		SetTreeToolTip(m_htiSearchKadFileLifetime, IDS_TWEAKS_TT_SEARCH_KAD_FILE_LIFETIME);
		SetTreeToolTip(m_htiSearchKadKeywordLifetime, IDS_TWEAKS_TT_SEARCH_KAD_KEYWORD_LIFETIME);
		SetTreeToolTip(m_htiGeneralAdvanced, IDS_TWEAKS_TT_GENERAL_ADVANCED);
		SetTreeToolTip(m_htiHiddenFile, IDS_TWEAKS_TT_HIDDEN_FILE);
		SetTreeToolTip(m_htiStoragePersistence, IDS_TWEAKS_TT_STORAGE_PERSISTENCE);
		SetTreeToolTip(m_htiHiddenStartup, IDS_TWEAKS_TT_HIDDEN_STARTUP);
		SetTreeToolTip(m_htiHiddenDisplay, IDS_TWEAKS_TT_HIDDEN_DISPLAY);
		SetTreeToolTip(m_htiHiddenSecurity, IDS_TWEAKS_TT_HIDDEN_SECURITY);
		SetTreeToolTip(m_htiLoggingGroup, IDS_TWEAKS_TT_LOGGING_GROUP);
		SetTreeToolTip(m_htiAutoTakeEd2kLinks, IDS_TWEAKS_TT_AUTO_TAKE_ED2K_LINKS);
		SetTreeToolTip(m_htiCreditSystem, IDS_TWEAKS_TT_CREDIT_SYSTEM);
		SetTreeToolTip(m_htiYourHostname, IDS_TWEAKS_TT_YOUR_HOSTNAME);
		SetTreeToolTip(m_htiTxtEditor, IDS_TWEAKS_TT_TXT_EDITOR);
		SetTreeToolTip(m_htiHighresTimer, IDS_TWEAKS_TT_HIGHRES_TIMER);
		SetTreeToolTip(m_htiICH, IDS_TWEAKS_TT_ICH);
		SetTreeToolTip(m_htiPreviewSmallBlocks, IDS_TWEAKS_TT_PREVIEW_SMALL_BLOCKS);
		SetTreeToolTip(m_htiPreviewSmallBlocksDisabled, IDS_TWEAKS_TT_PREVIEW_SMALL_BLOCKS_DISABLED);
		SetTreeToolTip(m_htiPreviewSmallBlocksAllow, IDS_TWEAKS_TT_PREVIEW_SMALL_BLOCKS_ALLOW);
		SetTreeToolTip(m_htiPreviewSmallBlocksForce, IDS_TWEAKS_TT_PREVIEW_SMALL_BLOCKS_FORCE);
		SetTreeToolTip(m_htiBeepOnError, IDS_TWEAKS_TT_BEEP_ON_ERROR);
		SetTreeToolTip(m_htiShowCopyEd2kLinkCmd, IDS_TWEAKS_TT_SHOW_COPY_ED2K_LINK_CMD);
		SetTreeToolTip(m_htiSparsePartFiles, IDS_TWEAKS_TT_SPARSE_PART_FILES);
		SetTreeToolTip(m_htiFullAlloc, IDS_TWEAKS_TT_FULL_ALLOC);
		SetTreeToolTip(m_htiMinFreeDiskSpaceConfig, IDS_TWEAKS_TT_MIN_FREE_DISK_SPACE_CONFIG);
		SetTreeToolTip(m_htiMinFreeDiskSpaceTemp, IDS_TWEAKS_TT_MIN_FREE_DISK_SPACE_TEMP);
		SetTreeToolTip(m_htiMinFreeDiskSpaceIncoming, IDS_TWEAKS_TT_MIN_FREE_DISK_SPACE_INCOMING);
		SetTreeToolTip(m_htiPerfLog, IDS_TWEAKS_TT_PERF_LOG);
		SetTreeToolTip(m_htiCommit, IDS_TWEAKS_TT_COMMIT);
		SetTreeToolTip(m_htiCommitNever, IDS_TWEAKS_TT_COMMIT_NEVER);
		SetTreeToolTip(m_htiCommitOnShutdown, IDS_TWEAKS_TT_COMMIT_ON_SHUTDOWN);
		SetTreeToolTip(m_htiCommitAlways, IDS_TWEAKS_TT_COMMIT_ALWAYS);
		SetTreeToolTip(m_htiExtractMetaData, GetExtractMetaDataToolTip());
		SetTreeToolTip(m_htiGeoLocationEnabled, IDS_TWEAKS_TT_GEO_LOCATION_ENABLED);
		SetTreeToolTip(m_htiCloseUPnPPorts, IDS_TWEAKS_TT_CLOSE_U_PN_P_PORTS);
		SetTreeToolTip(m_htiUPnPBackendMode, IDS_TWEAKS_TT_U_PN_P_BACKEND_MODE);
		SetTreeToolTip(m_htiUPnPBackendModeAutomatic, IDS_TWEAKS_TT_U_PN_P_BACKEND_MODE_AUTOMATIC);
		SetTreeToolTip(m_htiUPnPBackendModeIgdOnly, IDS_TWEAKS_TT_U_PN_P_BACKEND_MODE_IGD_ONLY);
		SetTreeToolTip(m_htiUPnPBackendModePcpNatPmpOnly, IDS_TWEAKS_TT_U_PN_P_BACKEND_MODE_PCP_NAT_PMP_ONLY);
		SetTreeToolTip(m_htiPreviewCopiedArchives, IDS_TWEAKS_TT_PREVIEW_COPIED_ARCHIVES);
		SetTreeToolTip(m_htiFileBufferTimeLimit, IDS_TWEAKS_TT_FILE_BUFFER_TIME_LIMIT);
		SetTreeToolTip(m_htiFileBufferSize, IDS_TWEAKS_TT_FILE_BUFFER_SIZE);
		SetTreeToolTip(m_htiQueueSize, IDS_TWEAKS_TT_QUEUE_SIZE);
		SetTreeToolTip(m_htiDateTimeFormat4Lists, IDS_TWEAKS_TT_DATE_TIME_FORMAT4_LISTS);
		SetTreeToolTip(m_htiDateTimeFormat, IDS_TWEAKS_TT_DATE_TIME_FORMAT);
		SetTreeToolTip(m_htiDateTimeFormat4Log, IDS_TWEAKS_TT_DATE_TIME_FORMAT4_LOG);
		SetTreeToolTip(m_htiIconFlashOnNewMessage, IDS_TWEAKS_TT_ICON_FLASH_ON_NEW_MESSAGE);
		SetTreeToolTip(m_htiInspectAllFileTypes, IDS_TWEAKS_TT_INSPECT_ALL_FILE_TYPES);
		SetTreeToolTip(m_htiReBarToolbar, IDS_TWEAKS_TT_RE_BAR_TOOLBAR);
		SetTreeToolTip(m_htiShowUpDownIconInTaskbar, IDS_TWEAKS_TT_SHOW_UP_DOWN_ICON_IN_TASKBAR);
		SetTreeToolTip(m_htiShowVerticalHourMarkers, IDS_TWEAKS_TT_SHOW_VERTICAL_HOUR_MARKERS);
		SetTreeToolTip(m_htiForceSpeedsToKB, IDS_TWEAKS_TT_FORCE_SPEEDS_TO_KB);
		SetTreeToolTip(m_htiRearrangeKadSearchKeywords, IDS_TWEAKS_TT_REARRANGE_KAD_SEARCH_KEYWORDS);
		SetTreeToolTip(m_htiMessageFromValidSourcesOnly, IDS_TWEAKS_TT_MESSAGE_FROM_VALID_SOURCES_ONLY);
		SetTreeToolTip(m_htiMaxChatHistoryLines, IDS_TWEAKS_TT_MAX_CHAT_HISTORY_LINES);
		SetTreeToolTip(m_htiMaxMessageSessions, IDS_TWEAKS_TT_MAX_MESSAGE_SESSIONS);
		SetTreeToolTip(m_htiCreateCrashDump, IDS_TWEAKS_TT_CREATE_CRASH_DUMP);
		SetTreeToolTip(m_htiCreateCrashDumpDisabled, IDS_TWEAKS_TT_CREATE_CRASH_DUMP_DISABLED);
		SetTreeToolTip(m_htiCreateCrashDumpPrompt, IDS_TWEAKS_TT_CREATE_CRASH_DUMP_PROMPT);
		SetTreeToolTip(m_htiCreateCrashDumpAlways, IDS_TWEAKS_TT_CREATE_CRASH_DUMP_ALWAYS);
		SetTreeToolTip(m_htiMaxLogFileSize, IDS_TWEAKS_TT_MAX_LOG_FILE_SIZE);
		SetTreeToolTip(m_htiMaxLogBuffer, IDS_TWEAKS_TT_MAX_LOG_BUFFER);
		SetTreeToolTip(m_htiLogFileFormat, IDS_TWEAKS_TT_LOG_FILE_FORMAT);
		SetTreeToolTip(m_htiLogFileFormatUnicode, IDS_TWEAKS_TT_LOG_FILE_FORMAT_UNICODE);
		SetTreeToolTip(m_htiLogFileFormatUtf8, IDS_TWEAKS_TT_LOG_FILE_FORMAT_UTF8);
		SetTreeToolTip(m_htiLog2Disk, IDS_TWEAKS_TT_LOG2_DISK);
		SetTreeToolTip(m_htiPerfLogFileFormat, IDS_TWEAKS_TT_PERF_LOG_FILE_FORMAT);
		SetTreeToolTip(m_htiPerfLogFileFormatCsv, IDS_TWEAKS_TT_PERF_LOG_FILE_FORMAT_CSV);
		SetTreeToolTip(m_htiPerfLogFileFormatMrtg, IDS_TWEAKS_TT_PERF_LOG_FILE_FORMAT_MRTG);
		SetTreeToolTip(m_htiPerfLogFile, IDS_TWEAKS_TT_PERF_LOG_FILE);
		SetTreeToolTip(m_htiPerfLogInterval, IDS_TWEAKS_TT_PERF_LOG_INTERVAL);
		SetTreeToolTip(m_htiVerboseGroup, IDS_TWEAKS_TT_VERBOSE_GROUP);
		SetTreeToolTip(m_htiRestoreLastMainWndDlg, IDS_TWEAKS_TT_RESTORE_LAST_MAIN_WND_DLG);
		SetTreeToolTip(m_htiRestoreLastLogPane, IDS_TWEAKS_TT_RESTORE_LAST_LOG_PANE);
		SetTreeToolTip(m_htiKeepUnavailableFixedSharedDirs, IDS_TWEAKS_TT_KEEP_UNAVAILABLE_FIXED_SHARED_DIRS);
		SetTreeToolTip(m_htiPartiallyPurgeOldKnownFiles, IDS_TWEAKS_TT_PARTIALLY_PURGE_OLD_KNOWN_FILES);
		SetTreeToolTip(m_htiDetectTCPErrorFlooder, IDS_TWEAKS_TT_DETECT_TCP_ERROR_FLOODER);
		SetTreeToolTip(m_htiTCPErrorFlooderIntervalMinutes, IDS_TWEAKS_TT_TCP_ERROR_FLOODER_INTERVAL_MINUTES);
		SetTreeToolTip(m_htiTCPErrorFlooderThreshold, IDS_TWEAKS_TT_TCP_ERROR_FLOODER_THRESHOLD);
		SetTreeToolTip(m_htiShareeMule, IDS_TWEAKS_TT_SHAREE_MULE);
		SetTreeToolTip(m_htiTCPGroup, IDS_TWEAKS_TT_TCP_GROUP);
		SetTreeToolTip(m_htiSearchGroup, IDS_TWEAKS_TT_SEARCH_GROUP);
		SetTreeToolTip(m_htiSearchEd2kGroup, IDS_TWEAKS_TT_SEARCH_ED2K_GROUP);
		SetTreeToolTip(m_htiSearchKadGroup, IDS_TWEAKS_TT_SEARCH_KAD_GROUP);
		SetTreeToolTip(m_htiBroadband, IDS_TWEAKS_TT_BROADBAND);
		SetTreeToolTip(m_htiMaxUploadClients, IDS_TWEAKS_TT_UPLOAD_POLICY_MAX_UPLOAD_CLIENTS);
		SetTreeToolTip(m_htiSlowUploadThreshold, IDS_TWEAKS_TT_UPLOAD_POLICY_SLOW_THRESHOLD);
		SetTreeToolTip(m_htiSlowUploadGrace, IDS_TWEAKS_TT_UPLOAD_POLICY_SLOW_GRACE);
		SetTreeToolTip(m_htiSlowUploadWarmup, IDS_TWEAKS_TT_UPLOAD_POLICY_SLOW_WARMUP);
		SetTreeToolTip(m_htiZeroUploadRateGrace, IDS_TWEAKS_TT_UPLOAD_POLICY_ZERO_RATE_GRACE);
		SetTreeToolTip(m_htiSlowUploadCooldown, IDS_TWEAKS_TT_UPLOAD_POLICY_COOLDOWN);
		SetTreeToolTip(m_htiLowRatioBoost, IDS_TWEAKS_TT_UPLOAD_POLICY_LOW_RATIO_BOOST);
		SetTreeToolTip(m_htiLowRatioThreshold, IDS_TWEAKS_TT_UPLOAD_POLICY_LOW_RATIO_THRESHOLD);
		SetTreeToolTip(m_htiLowRatioBonus, IDS_TWEAKS_TT_UPLOAD_POLICY_LOW_RATIO_BONUS);
		SetTreeToolTip(m_htiLowIDDivisor, IDS_TWEAKS_TT_UPLOAD_POLICY_LOW_ID_DIVISOR);
		SetTreeToolTip(m_htiSessionTransferLimit, IDS_TWEAKS_TT_UPLOAD_POLICY_SESSION_TRANSFER);
		SetTreeToolTip(m_htiSessionTransferLimitDisabled, IDS_TWEAKS_TT_UPLOAD_POLICY_SESSION_TRANSFER_DISABLED);
		SetTreeToolTip(m_htiSessionTransferLimitPercent, IDS_TWEAKS_TT_UPLOAD_POLICY_SESSION_TRANSFER_PERCENT);
		SetTreeToolTip(m_htiSessionTransferLimitPercentValue, IDS_TWEAKS_TT_UPLOAD_POLICY_SESSION_TRANSFER_PERCENT_VALUE);
		SetTreeToolTip(m_htiSessionTransferLimitMiB, IDS_TWEAKS_TT_UPLOAD_POLICY_SESSION_TRANSFER_MI_B);
		SetTreeToolTip(m_htiSessionTransferLimitMiBValue, IDS_TWEAKS_TT_UPLOAD_POLICY_SESSION_TRANSFER_MI_B_VALUE);
		SetTreeToolTip(m_htiSessionTimeLimit, IDS_TWEAKS_TT_UPLOAD_POLICY_SESSION_TIME_LIMIT);
		SetTreeToolTip(m_htiA4AFSaveCpu, IDS_TWEAKS_TT_A4_AF_SAVE_CPU);
		SetTreeToolTip(m_htiAutoArch, IDS_TWEAKS_TT_AUTO_ARCH);
		SetTreeToolTip(m_htiExtractMetaDataNever, IDS_TWEAKS_TT_EXTRACT_META_DATA_NEVER);
		SetTreeToolTip(m_htiExtractMetaDataID3Lib, GetExtractMetaDataAutoToolTip());
		SetTreeToolTip(m_htiMediaInfoDllPath, GetMediaInfoDllPathToolTip());
		SetTreeToolTip(m_htiPreviewOnIconDblClk, IDS_TWEAKS_TT_PREVIEW_ON_ICON_DBL_CLK);
		SetTreeToolTip(m_htiExtraPreviewWithMenu, IDS_TWEAKS_TT_EXTRA_PREVIEW_WITH_MENU);
		SetTreeToolTip(m_htiShowActiveDownloadsBold, IDS_TWEAKS_TT_SHOW_ACTIVE_DOWNLOADS_BOLD);
		SetTreeToolTip(m_htiUseSystemFontForMainControls, IDS_TWEAKS_TT_USE_SYSTEM_FONT_FOR_MAIN_CONTROLS);
		SetTreeToolTip(m_htiFilterLANIPs, IDS_TWEAKS_TT_FILTER_LANI_PS);
		SetTreeToolTip(m_htiGeoLocationCheckDays, IDS_TWEAKS_TT_GEO_LOCATION_CHECK_DAYS);
		SetTreeToolTip(m_htiVerbose, IDS_TWEAKS_TT_VERBOSE);
		SetTreeToolTip(m_htiFullVerbose, IDS_TWEAKS_TT_FULL_VERBOSE);
		SetTreeToolTip(m_htiLogLevel, IDS_TWEAKS_TT_LOG_LEVEL);
		SetTreeToolTip(m_htiDebug2Disk, IDS_TWEAKS_TT_DEBUG2_DISK);
		SetTreeToolTip(m_htiDebugSourceExchange, IDS_TWEAKS_TT_DEBUG_SOURCE_EXCHANGE);
		SetTreeToolTip(m_htiLogBannedClients, IDS_TWEAKS_TT_LOG_BANNED_CLIENTS);
		SetTreeToolTip(m_htiLogRatingDescReceived, IDS_TWEAKS_TT_LOG_RATING_DESC_RECEIVED);
		SetTreeToolTip(m_htiLogSecureIdent, IDS_TWEAKS_TT_LOG_SECURE_IDENT);
		SetTreeToolTip(m_htiLogFilteredIPs, IDS_TWEAKS_TT_LOG_FILTERED_I_PS);
		SetTreeToolTip(m_htiLogFileSaving, IDS_TWEAKS_TT_LOG_FILE_SAVING);
		SetTreeToolTip(m_htiLogA4AF, IDS_TWEAKS_TT_LOG_A4_AF);
		SetTreeToolTip(m_htiLogUlDlEvents, IDS_TWEAKS_TT_LOG_UL_DL_EVENTS);
		SetTreeToolTip(m_htiUPnP, IDS_TWEAKS_TT_U_PN_P);
		SetTreeToolTip(m_htiShareeMuleMultiUser, IDS_TWEAKS_TT_SHAREE_MULE_MULTI_USER);
		SetTreeToolTip(m_htiShareeMulePublicUser, IDS_TWEAKS_TT_SHAREE_MULE_PUBLIC_USER);
		SetTreeToolTip(m_htiShareeMuleOldStyle, IDS_TWEAKS_TT_SHAREE_MULE_OLD_STYLE);

		ExpandAllTreeItems(m_ctrlTreeOptions);
		m_ctrlTreeOptions.SendMessage(WM_VSCROLL, SB_TOP);
		m_bInitializedTreeOpts = true;
	}

	/////////////////////////////////////////////////////////////////////////////
	// TCP/IP group
	//
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiMaxCon5Sec, m_iMaxConnPerFive);
	DDV_MinMaxInt(pDX, m_iMaxConnPerFive, 1, INT_MAX);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiMaxHalfOpen, m_iMaxHalfOpen);
	DDV_MinMaxInt(pDX, m_iMaxHalfOpen, 1, INT_MAX);
	DDX_Text(pDX, IDC_EXT_OPTS, m_htiConnectionTimeout, m_uConnectionTimeoutSeconds);
	DDX_Text(pDX, IDC_EXT_OPTS, m_htiDownloadTimeout, m_uDownloadTimeoutSeconds);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiConditionalTCPAccept, m_bConditionalTCPAccept);
	DDX_Text(pDX, IDC_EXT_OPTS, m_htiServerKeepAliveTimeout, m_uServerKeepAliveTimeout);
	if (pDX->m_bSaveAndValidate) {
		if (m_uConnectionTimeoutSeconds < thePrefs.GetMinTimeoutSeconds())
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiConnectionTimeout);
		if (m_uDownloadTimeoutSeconds < thePrefs.GetMinTimeoutSeconds())
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiDownloadTimeout);
	}
	DDX_Text(pDX, IDC_EXT_OPTS, m_htiSearchEd2kMaxResults, m_uEd2kSearchMaxResults);
	DDX_Text(pDX, IDC_EXT_OPTS, m_htiSearchEd2kMaxMoreRequests, m_uEd2kSearchMaxMoreRequests);
	DDX_Text(pDX, IDC_EXT_OPTS, m_htiSearchKadFileTotal, m_uKadFileSearchTotal);
	DDX_Text(pDX, IDC_EXT_OPTS, m_htiSearchKadKeywordTotal, m_uKadKeywordSearchTotal);
	DDX_Text(pDX, IDC_EXT_OPTS, m_htiSearchKadFileLifetime, m_uKadFileSearchLifetimeSeconds);
	DDX_Text(pDX, IDC_EXT_OPTS, m_htiSearchKadKeywordLifetime, m_uKadKeywordSearchLifetimeSeconds);
	if (pDX->m_bSaveAndValidate) {
		if (m_uKadFileSearchTotal < thePrefs.GetMinKadSearchTotal() || m_uKadFileSearchTotal > thePrefs.GetMaxKadSearchTotal())
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiSearchKadFileTotal);
		if (m_uKadKeywordSearchTotal < thePrefs.GetMinKadSearchTotal() || m_uKadKeywordSearchTotal > thePrefs.GetMaxKadSearchTotal())
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiSearchKadKeywordTotal);
		if (m_uKadFileSearchLifetimeSeconds < thePrefs.GetMinKadSearchLifetimeSeconds() || m_uKadFileSearchLifetimeSeconds > thePrefs.GetMaxKadSearchLifetimeSeconds())
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiSearchKadFileLifetime);
		if (m_uKadKeywordSearchLifetimeSeconds < thePrefs.GetMinKadSearchLifetimeSeconds() || m_uKadKeywordSearchLifetimeSeconds > thePrefs.GetMaxKadSearchLifetimeSeconds())
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiSearchKadKeywordLifetime);
	}
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiMaxUploadClients, m_iMaxUploadClients);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiSlowUploadThreshold, m_sSlowUploadThresholdFactor);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiSlowUploadGrace, m_iSlowUploadGraceSeconds);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiSlowUploadWarmup, m_iSlowUploadWarmupSeconds);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiZeroUploadRateGrace, m_iZeroUploadRateGraceSeconds);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiSlowUploadCooldown, m_iSlowUploadCooldownSeconds);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiLowRatioBoost, m_bLowRatioBoost);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiLowRatioThreshold, m_sLowRatioThreshold);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiLowRatioBonus, m_iLowRatioBonus);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiLowIDDivisor, m_iLowIDDivisor);
	DDX_TreeRadio(pDX, IDC_EXT_OPTS, m_htiSessionTransferLimit, m_iSessionTransferLimitMode);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiSessionTransferLimitPercentValue, m_iSessionTransferLimitPercent);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiSessionTransferLimitMiBValue, m_iSessionTransferLimitMiB);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiSessionTimeLimit, m_iSessionTimeLimitSeconds);
	if (pDX->m_bSaveAndValidate) {
		float fParsedValue = 0.0f;
		if (m_iMaxUploadClients < 1 || m_iMaxUploadClients > 32)
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiMaxUploadClients);
		if (!TryParseTreeFloat(m_sSlowUploadThresholdFactor, fParsedValue) || fParsedValue < 0.10f || fParsedValue > 1.0f)
			FailTreeValidation(pDX, AFX_IDP_PARSE_REAL, m_htiSlowUploadThreshold);
		if (m_iSlowUploadGraceSeconds < 5 || m_iSlowUploadGraceSeconds > 300)
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiSlowUploadGrace);
		if (m_iSlowUploadWarmupSeconds < 0 || m_iSlowUploadWarmupSeconds > 3600)
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiSlowUploadWarmup);
		if (m_iZeroUploadRateGraceSeconds < 3 || m_iZeroUploadRateGraceSeconds > 120)
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiZeroUploadRateGrace);
		if (m_iSlowUploadCooldownSeconds < 10 || m_iSlowUploadCooldownSeconds > 3600)
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiSlowUploadCooldown);
		if (!TryParseTreeFloat(m_sLowRatioThreshold, fParsedValue)
			|| fParsedValue < PreferenceUiSeams::kMinLowRatioThreshold
			|| fParsedValue > PreferenceUiSeams::kMaxLowRatioThreshold)
			FailTreeValidation(pDX, AFX_IDP_PARSE_REAL, m_htiLowRatioThreshold);
		if (m_iLowRatioBonus < 0 || m_iLowRatioBonus > static_cast<int>(PreferenceUiSeams::kMaxLowRatioBonus))
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiLowRatioBonus);
		if (m_iLowIDDivisor < 1 || m_iLowIDDivisor > 8)
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiLowIDDivisor);
		if (m_iSessionTransferLimitMode == static_cast<int>(ESessionTransferLimitMode::PercentOfFile)
			&& (m_iSessionTransferLimitPercent < static_cast<int>(PreferenceUiSeams::kMinSessionTransferPercent)
				|| m_iSessionTransferLimitPercent > static_cast<int>(PreferenceUiSeams::kMaxSessionTransferPercent)))
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiSessionTransferLimitPercentValue);
		if (m_iSessionTransferLimitMode == static_cast<int>(ESessionTransferLimitMode::AbsoluteMiB)
			&& (m_iSessionTransferLimitMiB < static_cast<int>(PreferenceUiSeams::kMinSessionTransferMiB)
				|| m_iSessionTransferLimitMiB > static_cast<int>(PreferenceUiSeams::kMaxSessionTransferMiB)))
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiSessionTransferLimitMiBValue);
		if (m_iSessionTimeLimitSeconds < 0 || m_iSessionTimeLimitSeconds > 86400)
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiSessionTimeLimit);
	}

	/////////////////////////////////////////////////////////////////////////////
	// Miscellaneous group
	//
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiAutoTakeEd2kLinks, m_bAutoTakeEd2kLinks);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiCreditSystem, m_bCreditSystem);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiFilterLANIPs, m_bFilterLANIPs);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiA4AFSaveCpu, m_bA4AFSaveCpu);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiYourHostname, m_sYourHostname);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiTxtEditor, m_sTxtEditor);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiHighresTimer, m_bHighresTimer);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiAutoArch, m_bAutoArchDisable);
	if (pDX->m_bSaveAndValidate && m_sTxtEditor.Trim().IsEmpty())
		FailTreeValidation(pDX, m_ctrlTreeOptions, m_htiTxtEditor, GetResString(IDS_TWEAKS_VALIDATION_EDITOR_COMMAND));

	/////////////////////////////////////////////////////////////////////////////
	// File related group
	//
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiICH, m_bICH);
	DDX_TreeRadio(pDX, IDC_EXT_OPTS, m_htiPreviewSmallBlocks, m_iPreviewSmallBlocks);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiBeepOnError, m_bBeepOnError);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiShowCopyEd2kLinkCmd, m_bShowCopyEd2kLinkCmd);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiFollowMajorityFilenameForNewDownloads, m_bFollowMajorityFilenameForNewDownloads);
	ExchangeTreeUInt(pDX, m_ctrlTreeOptions, m_htiFollowMajorityFilenameRequiredPercent, m_uFollowMajorityFilenameRequiredPercent);
	ExchangeTreeUInt(pDX, m_ctrlTreeOptions, m_htiFollowMajorityFilenameMinimumVotes, m_uFollowMajorityFilenameMinimumVotes);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiSparsePartFiles, m_bSparsePartFiles);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiFullAlloc, m_bFullAlloc);
	DDX_Text(pDX, IDC_EXT_OPTS, m_htiMinFreeDiskSpaceConfig, m_iMinFreeDiskSpaceConfigGB);
	DDV_MinMaxInt(pDX, m_iMinFreeDiskSpaceConfigGB, static_cast<int>(PartFilePersistenceSeams::kMinConfigDiskSpaceFloorGiB), static_cast<int>(PartFilePersistenceSeams::kMaxDiskSpaceFloorGiB));
	DDX_Text(pDX, IDC_EXT_OPTS, m_htiMinFreeDiskSpaceTemp, m_iMinFreeDiskSpaceTempGB);
	DDV_MinMaxInt(pDX, m_iMinFreeDiskSpaceTempGB, static_cast<int>(PartFilePersistenceSeams::kMinTempDiskSpaceFloorGiB), static_cast<int>(PartFilePersistenceSeams::kMaxDiskSpaceFloorGiB));
	DDX_Text(pDX, IDC_EXT_OPTS, m_htiMinFreeDiskSpaceIncoming, m_iMinFreeDiskSpaceIncomingGB);
	DDV_MinMaxInt(pDX, m_iMinFreeDiskSpaceIncomingGB, static_cast<int>(PartFilePersistenceSeams::kMinIncomingDiskSpaceFloorGiB), static_cast<int>(PartFilePersistenceSeams::kMaxDiskSpaceFloorGiB));
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiAutoBroadbandIO, m_bAutoBroadbandIO);
	DDX_TreeRadio(pDX, IDC_EXT_OPTS, m_htiCommit, m_iCommitFiles);
	DDX_TreeRadio(pDX, IDC_EXT_OPTS, m_htiExtractMetaData, m_iExtractMetaData);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiMediaInfoDllPath, m_sMediaInfoDllPath);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiRestoreLastMainWndDlg, m_bRestoreLastMainWndDlg);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiRestoreLastLogPane, m_bRestoreLastLogPane);
	DDX_Text(pDX, IDC_EXT_OPTS, m_htiFileBufferTimeLimit, m_uFileBufferTimeLimitSeconds);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiDateTimeFormat4Lists, m_sDateTimeFormat4Lists);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiPreviewCopiedArchives, m_bPreviewCopiedArchives);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiInspectAllFileTypes, m_bInspectAllFileTypes);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiPreviewOnIconDblClk, m_bPreviewOnIconDblClk);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiExtraPreviewWithMenu, m_bExtraPreviewWithMenu);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiKeepUnavailableFixedSharedDirs, m_bKeepUnavailableFixedSharedDirs);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiPartiallyPurgeOldKnownFiles, m_bPartiallyPurgeOldKnownFiles);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiIconFlashOnNewMessage, m_bIconFlashOnNewMessage);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiDateTimeFormat, m_sDateTimeFormat);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiDateTimeFormat4Log, m_sDateTimeFormat4Log);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiShowActiveDownloadsBold, m_bShowActiveDownloadsBold);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiUseSystemFontForMainControls, m_bUseSystemFontForMainControls);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiReBarToolbar, m_bReBarToolbar);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiShowUpDownIconInTaskbar, m_bShowUpDownIconInTaskbar);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiShowVerticalHourMarkers, m_bShowVerticalHourMarkers);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiForceSpeedsToKB, m_bForceSpeedsToKB);
	if (pDX->m_bSaveAndValidate) {
		if (m_uFollowMajorityFilenameRequiredPercent < thePrefs.GetMinFollowMajorityFilenameRequiredPercent() || m_uFollowMajorityFilenameRequiredPercent > thePrefs.GetMaxFollowMajorityFilenameRequiredPercent()) {
			CString detail;
			detail.Format(GetResString(IDS_TWEAKS_VALIDATION_FOLLOW_MAJORITY_PERCENT_RANGE_FMT), thePrefs.GetMinFollowMajorityFilenameRequiredPercent(), thePrefs.GetMaxFollowMajorityFilenameRequiredPercent());
			FailTreeValidation(pDX, m_ctrlTreeOptions, m_htiFollowMajorityFilenameRequiredPercent, detail);
		}
		if (m_uFollowMajorityFilenameMinimumVotes > thePrefs.GetMaxFollowMajorityFilenameMinimumVotes()) {
			CString detail;
			detail.Format(GetResString(IDS_TWEAKS_VALIDATION_FOLLOW_MAJORITY_VOTES_RANGE_FMT), thePrefs.GetMaxFollowMajorityFilenameMinimumVotes());
			FailTreeValidation(pDX, m_ctrlTreeOptions, m_htiFollowMajorityFilenameMinimumVotes, detail);
		}
		if (m_sDateTimeFormat.Trim().IsEmpty())
			FailTreeValidation(pDX, m_ctrlTreeOptions, m_htiDateTimeFormat, GetResString(IDS_TWEAKS_VALIDATION_CTIME_FORMAT));
		if (m_sDateTimeFormat4Log.Trim().IsEmpty())
			FailTreeValidation(pDX, m_ctrlTreeOptions, m_htiDateTimeFormat4Log, GetResString(IDS_TWEAKS_VALIDATION_CTIME_FORMAT));
	}
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiGeoLocationEnabled, m_bGeoLocationEnabled);
	ExchangeTreeUInt(pDX, m_ctrlTreeOptions, m_htiGeoLocationCheckDays, m_uGeoLocationCheckDays);
	if (pDX->m_bSaveAndValidate) {
		if (m_uGeoLocationCheckDays != 0
			&& (m_uGeoLocationCheckDays < thePrefs.GetMinGeoLocationCheckDays() || m_uGeoLocationCheckDays > thePrefs.GetMaxGeoLocationCheckDays()))
		{
			CString detail;
			detail.Format(GetResString(IDS_TWEAKS_VALIDATION_ZERO_OR_RANGE_FMT), thePrefs.GetMinGeoLocationCheckDays(), thePrefs.GetMaxGeoLocationCheckDays());
			FailTreeValidation(pDX, m_ctrlTreeOptions, m_htiGeoLocationCheckDays, detail);
		}
	}
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiDetectTCPErrorFlooder, m_bDetectTCPErrorFlooder);
	if (m_htiTCPErrorFlooderIntervalMinutes) {
		DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiTCPErrorFlooderIntervalMinutes, m_iTCPErrorFlooderIntervalMinutes);
		DDV_MinMaxInt(pDX, m_iTCPErrorFlooderIntervalMinutes, static_cast<int>(thePrefs.GetMinTCPErrorFlooderIntervalMinutes()), static_cast<int>(thePrefs.GetMaxTCPErrorFlooderIntervalMinutes()));
	}
	if (m_htiTCPErrorFlooderThreshold) {
		DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiTCPErrorFlooderThreshold, m_iTCPErrorFlooderThreshold);
		DDV_MinMaxInt(pDX, m_iTCPErrorFlooderThreshold, static_cast<int>(thePrefs.GetMinTCPErrorFlooderThreshold()), static_cast<int>(thePrefs.GetMaxTCPErrorFlooderThreshold()));
	}
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiRearrangeKadSearchKeywords, m_bRearrangeKadSearchKeywords);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiMessageFromValidSourcesOnly, m_bMessageFromValidSourcesOnly);
	ExchangeTreeUInt(pDX, m_ctrlTreeOptions, m_htiMaxChatHistoryLines, m_uMaxChatHistoryLines);
	ExchangeTreeUInt(pDX, m_ctrlTreeOptions, m_htiMaxMessageSessions, m_uMaxMessageSessions);
	if (pDX->m_bSaveAndValidate) {
		if (!PreferenceUiSeams::IsPositiveBounded(m_uMaxChatHistoryLines, PreferenceUiSeams::kMaxChatHistoryLines)) {
			CString detail;
			detail.Format(GetResString(IDS_TWEAKS_VALIDATION_POSITIVE_UINT_RANGE_FMT), PreferenceUiSeams::kMaxChatHistoryLines);
			FailTreeValidation(pDX, m_ctrlTreeOptions, m_htiMaxChatHistoryLines, detail);
		}
		if (!PreferenceUiSeams::IsPositiveBounded(m_uMaxMessageSessions, PreferenceUiSeams::kMaxMessageSessions)) {
			CString detail;
			detail.Format(GetResString(IDS_TWEAKS_VALIDATION_POSITIVE_UINT_RANGE_FMT), PreferenceUiSeams::kMaxMessageSessions);
			FailTreeValidation(pDX, m_ctrlTreeOptions, m_htiMaxMessageSessions, detail);
		}
	}
	ExchangeTreeUInt(pDX, m_ctrlTreeOptions, m_htiFileBufferSize, m_uFileBufferSizeKiB);
	if (pDX->m_bSaveAndValidate) {
		const UINT uMinFileBufferSizeKiB = thePrefs.GetMinFileBufferSizeBytes() / 1024u;
		const UINT uMaxFileBufferSizeKiB = thePrefs.GetMaxFileBufferSizeBytes() / 1024u;
		if (m_uFileBufferSizeKiB < uMinFileBufferSizeKiB || m_uFileBufferSizeKiB > uMaxFileBufferSizeKiB) {
			CString detail;
			detail.Format(GetResString(IDS_TWEAKS_VALIDATION_UINT_KIB_RANGE_FMT), uMinFileBufferSizeKiB, uMaxFileBufferSizeKiB);
			FailTreeValidation(pDX, m_ctrlTreeOptions, m_htiFileBufferSize, detail);
		}
	}
	ExchangeTreeInt(pDX, m_ctrlTreeOptions, m_htiQueueSize, m_iQueueSize);
	if (pDX->m_bSaveAndValidate) {
		if (m_iQueueSize < static_cast<int>(thePrefs.GetMinQueueSize()) || m_iQueueSize > static_cast<int>(thePrefs.GetMaxQueueSize())) {
			CString detail;
			detail.Format(GetResString(IDS_TWEAKS_VALIDATION_INT_RANGE_FMT), static_cast<int>(thePrefs.GetMinQueueSize()), static_cast<int>(thePrefs.GetMaxQueueSize()));
			FailTreeValidation(pDX, m_ctrlTreeOptions, m_htiQueueSize, detail);
		}
	}

	/////////////////////////////////////////////////////////////////////////////
	// Logging group
	//
	DDX_TreeRadio(pDX, IDC_EXT_OPTS, m_htiCreateCrashDump, m_iCreateCrashDumpMode);
	ExchangeTreeUInt(pDX, m_ctrlTreeOptions, m_htiMaxLogFileSize, m_uMaxLogFileSizeKiB);
	ExchangeTreeUInt(pDX, m_ctrlTreeOptions, m_htiMaxLogBuffer, m_uMaxLogBufferKiB);
	DDX_TreeRadio(pDX, IDC_EXT_OPTS, m_htiLogFileFormat, m_iLogFileFormat);
	if (pDX->m_bSaveAndValidate) {
		if (!PreferenceUiSeams::IsLogFileSizeKiBAllowed(m_uMaxLogFileSizeKiB)) {
			CString detail;
			detail.Format(GetResString(IDS_TWEAKS_VALIDATION_LOG_FILE_SIZE_RANGE_FMT), PreferenceUiSeams::kMaxLogFileSizeKiB);
			FailTreeValidation(pDX, m_ctrlTreeOptions, m_htiMaxLogFileSize, detail);
		}
		if (!PreferenceUiSeams::IsLogBufferKiBAllowed(m_uMaxLogBufferKiB)) {
			CString detail;
			detail.Format(GetResString(IDS_TWEAKS_VALIDATION_UINT_KIB_RANGE_FMT), PreferenceUiSeams::kMinLogBufferKiB, PreferenceUiSeams::kMaxLogBufferKiB);
			FailTreeValidation(pDX, m_ctrlTreeOptions, m_htiMaxLogBuffer, detail);
		}
	}
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiLog2Disk, m_bLog2Disk);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiPerfLog, m_bPerfLogEnabled);
	DDX_TreeRadio(pDX, IDC_EXT_OPTS, m_htiPerfLogFileFormat, m_iPerfLogFileFormat);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiPerfLogFile, m_sPerfLogFile);
	ExchangeTreeUInt(pDX, m_ctrlTreeOptions, m_htiPerfLogInterval, m_uPerfLogIntervalMinutes);
	if (pDX->m_bSaveAndValidate
		&& !PreferenceUiSeams::IsPositiveBounded(m_uPerfLogIntervalMinutes, PreferenceUiSeams::kMaxPerfLogIntervalMinutes))
	{
		CString detail;
		detail.Format(GetResString(IDS_TWEAKS_VALIDATION_MINUTES_RANGE_FMT), PreferenceUiSeams::kMaxPerfLogIntervalMinutes);
		FailTreeValidation(pDX, m_ctrlTreeOptions, m_htiPerfLogInterval, detail);
	}
	if (m_htiLogLevel) {
		DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiLogLevel, m_iLogLevel);
		DDV_MinMaxInt(pDX, m_iLogLevel, 1, 5);
	}
	if (m_htiVerbose)
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiVerbose, m_bVerbose);
	if (m_htiFullVerbose) {
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiFullVerbose, m_bFullVerbose);
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiFullVerbose, m_bVerbose);
	}
	if (m_htiDebug2Disk) {
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiDebug2Disk, m_bDebug2Disk);
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiDebug2Disk, m_bVerbose);
	}
	if (m_htiDebugSourceExchange) {
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiDebugSourceExchange, m_bDebugSourceExchange);
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiDebugSourceExchange, m_bVerbose);
	}
	if (m_htiLogBannedClients) {
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiLogBannedClients, m_bLogBannedClients);
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogBannedClients, m_bVerbose);
	}
	if (m_htiLogRatingDescReceived) {
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiLogRatingDescReceived, m_bLogRatingDescReceived);
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogRatingDescReceived, m_bVerbose);
	}
	if (m_htiLogSecureIdent) {
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiLogSecureIdent, m_bLogSecureIdent);
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogSecureIdent, m_bVerbose);
	}
	if (m_htiLogFilteredIPs) {
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiLogFilteredIPs, m_bLogFilteredIPs);
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogFilteredIPs, m_bVerbose);
	}
	if (m_htiLogFileSaving) {
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiLogFileSaving, m_bLogFileSaving);
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogFileSaving, m_bVerbose);
	}
	if (m_htiLogA4AF) {
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiLogA4AF, m_bLogA4AF);
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogA4AF, m_bVerbose);
	}
	if (m_htiLogUlDlEvents) {
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiLogUlDlEvents, m_bLogUlDlEvents);
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogUlDlEvents, m_bVerbose);
	}

	/////////////////////////////////////////////////////////////////////////////
	// UPnP group
	//
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiCloseUPnPPorts, m_bCloseUPnPOnExit);
	DDX_TreeRadio(pDX, IDC_EXT_OPTS, m_htiUPnPBackendMode, m_iUPnPBackendMode);

	/////////////////////////////////////////////////////////////////////////////
	// eMule Shared User
	//
	DDX_TreeRadio(pDX, IDC_EXT_OPTS, m_htiShareeMule, m_iShareeMule);
}

BOOL CPPgTweaks::OnInitDialog()
{
	m_iMaxConnPerFive = thePrefs.GetMaxConperFive();
	m_iMaxHalfOpen = thePrefs.GetMaxHalfConnections();
	m_bConditionalTCPAccept = thePrefs.GetConditionalTCPAccept();
	m_bAutoTakeEd2kLinks = thePrefs.AutoTakeED2KLinks();
	if (thePrefs.GetEnableVerboseOptions()) {
		m_bVerbose = thePrefs.m_bVerbose;
		m_bDebug2Disk = thePrefs.debug2disk;							// do *not* use the corresponding 'Get...' function here!
		m_bDebugSourceExchange = thePrefs.m_bDebugSourceExchange;		// do *not* use the corresponding 'Get...' function here!
		m_bLogBannedClients = thePrefs.m_bLogBannedClients;				// do *not* use the corresponding 'Get...' function here!
		m_bLogRatingDescReceived = thePrefs.m_bLogRatingDescReceived;	// do *not* use the corresponding 'Get...' function here!
		m_bLogSecureIdent = thePrefs.m_bLogSecureIdent;					// do *not* use the corresponding 'Get...' function here!
		m_bLogFilteredIPs = thePrefs.m_bLogFilteredIPs;					// do *not* use the corresponding 'Get...' function here!
		m_bLogFileSaving = thePrefs.m_bLogFileSaving;					// do *not* use the corresponding 'Get...' function here!
		m_bLogA4AF = thePrefs.m_bLogA4AF;							    // do *not* use the corresponding 'Get...' function here! // ZZ:DownloadManager
		m_bLogUlDlEvents = thePrefs.m_bLogUlDlEvents;
		m_bFullVerbose = thePrefs.m_bFullVerbose;
		m_iLogLevel = 5 - thePrefs.m_byLogLevel;
	}
	m_iCreateCrashDumpMode = thePrefs.GetCreateCrashDumpMode();
	m_uMaxLogFileSizeKiB = PreferenceUiSeams::LogFileSizeBytesToKiB(thePrefs.GetMaxLogFileSize());
	m_uMaxLogBufferKiB = static_cast<UINT>(max(0, thePrefs.GetMaxLogBuff() / 1024));
	m_iLogFileFormat = thePrefs.GetLogFileFormat();
	m_bLog2Disk = thePrefs.log2disk;
	m_bPerfLogEnabled = thePerfLog.IsEnabled();
	m_iPerfLogFileFormat = thePerfLog.GetConfiguredFileFormat();
	m_sPerfLogFile = thePerfLog.GetConfiguredFilePath();
	m_uPerfLogIntervalMinutes = thePerfLog.GetConfiguredIntervalMinutes();
	m_bCreditSystem = thePrefs.m_bCreditSystem;
	m_iCommitFiles = thePrefs.m_iCommitFiles;
	m_iExtractMetaData = thePrefs.m_iExtractMetaData;
	m_bFilterLANIPs = thePrefs.filterLANIPs;
	m_uServerKeepAliveTimeout = thePrefs.GetServerKeepAliveTimeout() == 0
		? 0
		: max(1u, static_cast<UINT>((thePrefs.GetServerKeepAliveTimeout() + MIN2MS(1) - 1) / MIN2MS(1)));
	m_bSparsePartFiles = thePrefs.GetSparsePartFiles();
	m_bFullAlloc = thePrefs.m_bAllocFull;
	m_iMinFreeDiskSpaceConfigGB = static_cast<int>(PartFilePersistenceSeams::ConvertDiskSpaceFloorBytesToDisplayGiB(
		thePrefs.GetMinFreeDiskSpaceConfig(), thePrefs.GetMinFreeDiskSpaceConfigFloor(), PartFilePersistenceSeams::kMinConfigDiskSpaceFloorGiB));
	m_iMinFreeDiskSpaceTempGB = static_cast<int>(PartFilePersistenceSeams::ConvertDiskSpaceFloorBytesToDisplayGiB(
		thePrefs.GetMinFreeDiskSpaceTemp(), thePrefs.GetMinFreeDiskSpaceTempFloor(), PartFilePersistenceSeams::kMinTempDiskSpaceFloorGiB));
	m_iMinFreeDiskSpaceIncomingGB = static_cast<int>(PartFilePersistenceSeams::ConvertDiskSpaceFloorBytesToDisplayGiB(
		thePrefs.GetMinFreeDiskSpaceIncoming(), thePrefs.GetMinFreeDiskSpaceIncomingFloor(), PartFilePersistenceSeams::kMinIncomingDiskSpaceFloorGiB));
	m_sYourHostname = thePrefs.GetYourHostname();
	m_bAutoArchDisable = !thePrefs.m_bAutomaticArcPreviewStart;

	m_bCloseUPnPOnExit = thePrefs.CloseUPnPOnExit();
	m_iUPnPBackendMode = thePrefs.GetUPnPBackendMode();

	m_iShareeMule = thePrefs.m_nCurrentUserDirMode;

	m_bA4AFSaveCpu = thePrefs.GetA4AFSaveCpu();
	m_bHighresTimer = thePrefs.GetHighresTimer();
	m_bRestoreLastMainWndDlg = thePrefs.GetRestoreLastMainWndDlg();
	m_bRestoreLastLogPane = thePrefs.GetRestoreLastLogPane();
	m_uConnectionTimeoutSeconds = max(thePrefs.GetMinTimeoutSeconds(), thePrefs.TimeoutMsToSeconds(thePrefs.GetConnectionTimeout()));
	m_uDownloadTimeoutSeconds = max(thePrefs.GetMinTimeoutSeconds(), thePrefs.TimeoutMsToSeconds(thePrefs.GetDownloadTimeout()));
	m_uEd2kSearchMaxResults = thePrefs.GetEd2kSearchMaxResults();
	m_uEd2kSearchMaxMoreRequests = thePrefs.GetEd2kSearchMaxMoreRequests();
	m_uKadFileSearchTotal = thePrefs.GetKadFileSearchTotal();
	m_uKadKeywordSearchTotal = thePrefs.GetKadKeywordSearchTotal();
	m_uKadFileSearchLifetimeSeconds = thePrefs.GetKadFileSearchLifetimeSeconds();
	m_uKadKeywordSearchLifetimeSeconds = thePrefs.GetKadKeywordSearchLifetimeSeconds();
	m_bDetectTCPErrorFlooder = thePrefs.IsDetectTCPErrorFlooder();
	m_iTCPErrorFlooderIntervalMinutes = static_cast<int>(thePrefs.GetTCPErrorFlooderIntervalMinutes());
	m_iTCPErrorFlooderThreshold = static_cast<int>(thePrefs.GetTCPErrorFlooderThreshold());
	m_iMaxUploadClients = static_cast<int>(thePrefs.GetMaxUploadClientsAllowed());
	m_sSlowUploadThresholdFactor.Format(_T("%.2f"), thePrefs.GetSlowUploadThresholdFactor());
	m_iSlowUploadGraceSeconds = static_cast<int>(thePrefs.GetSlowUploadGraceSeconds());
	m_iSlowUploadWarmupSeconds = static_cast<int>(thePrefs.GetSlowUploadWarmupSeconds());
	m_iZeroUploadRateGraceSeconds = static_cast<int>(thePrefs.GetZeroUploadRateGraceSeconds());
	m_iSlowUploadCooldownSeconds = static_cast<int>(thePrefs.GetSlowUploadCooldownSeconds());
	m_bLowRatioBoost = thePrefs.IsLowRatioBoostEnabled();
	m_sLowRatioThreshold.Format(_T("%.2f"), thePrefs.GetLowRatioThreshold());
	m_iLowRatioBonus = static_cast<int>(thePrefs.GetLowRatioBonus());
	m_iLowIDDivisor = static_cast<int>(thePrefs.GetLowIDDivisor());
	m_iSessionTransferLimitMode = static_cast<int>(thePrefs.GetSessionTransferLimitMode());
	m_iSessionTransferLimitPercent = static_cast<int>(thePrefs.GetSessionTransferLimitMode() == ESessionTransferLimitMode::PercentOfFile ? thePrefs.GetSessionTransferLimitValue() : PreferenceUiSeams::kDefaultSessionTransferPercent);
	m_iSessionTransferLimitMiB = static_cast<int>(thePrefs.GetSessionTransferLimitMode() == ESessionTransferLimitMode::AbsoluteMiB ? thePrefs.GetSessionTransferLimitValue() : 0);
	m_iSessionTimeLimitSeconds = static_cast<int>(thePrefs.GetSessionTimeLimitSeconds());
	m_uFileBufferTimeLimitSeconds = max(1u, thePrefs.GetFileBufferTimeLimit() / SEC2MS(1));
	m_bICH = thePrefs.IsICHEnabled();
	m_iPreviewSmallBlocks = thePrefs.GetPreviewSmallBlocks();
	m_bBeepOnError = thePrefs.IsErrorBeepEnabled();
	m_bShowCopyEd2kLinkCmd = thePrefs.GetShowCopyEd2kLinkCmd();
	m_bFollowMajorityFilenameForNewDownloads = thePrefs.GetFollowMajorityFilenameForNewDownloads();
	m_uFollowMajorityFilenameRequiredPercent = thePrefs.GetFollowMajorityFilenameRequiredPercent();
	m_uFollowMajorityFilenameMinimumVotes = thePrefs.GetFollowMajorityFilenameMinimumVotes();
	m_sDateTimeFormat4Lists = thePrefs.GetDateTimeFormat4Lists();
	m_sDateTimeFormat = thePrefs.GetDateTimeFormat();
	m_sDateTimeFormat4Log = thePrefs.GetDateTimeFormat4Log();
	m_sMediaInfoDllPath = theApp.GetProfileString(_T("eMule"), kMediaInfoDllPathProfileKey, kMediaInfoDllPathDefault);
	m_bPreviewCopiedArchives = thePrefs.GetPreviewCopiedArchives();
	m_bInspectAllFileTypes = thePrefs.GetInspectAllFileTypes();
	m_bPreviewOnIconDblClk = thePrefs.GetPreviewOnIconDblClk();
	m_bShowActiveDownloadsBold = thePrefs.GetShowActiveDownloadsBold();
	m_bUseSystemFontForMainControls = thePrefs.GetUseSystemFontForMainControls();
	m_bReBarToolbar = thePrefs.GetReBarToolbar();
	m_bShowUpDownIconInTaskbar = thePrefs.IsShowUpDownIconInTaskbar();
	m_bShowVerticalHourMarkers = thePrefs.m_bShowVerticalHourMarkers;
	m_bForceSpeedsToKB = thePrefs.GetForceSpeedsToKB();
	m_bIconFlashOnNewMessage = thePrefs.DoFlashOnNewMessage();
	m_bGeoLocationEnabled = thePrefs.IsGeoLocationEnabled();
	m_uGeoLocationCheckDays = thePrefs.NormalizeGeoLocationCheckDays(thePrefs.GetGeoLocationCheckDays());
	m_bExtraPreviewWithMenu = thePrefs.GetExtraPreviewWithMenu();
	m_bKeepUnavailableFixedSharedDirs = thePrefs.m_bKeepUnavailableFixedSharedDirs;
	m_bPartiallyPurgeOldKnownFiles = thePrefs.DoPartiallyPurgeOldKnownFiles();
	m_bRearrangeKadSearchKeywords = thePrefs.GetRearrangeKadSearchKeywords();
	m_bMessageFromValidSourcesOnly = thePrefs.MsgOnlySecure();
	m_bAutoBroadbandIO = thePrefs.IsAutoBroadbandIOEnabled();
	m_iQueueSize = static_cast<int>(thePrefs.GetQueueSize());
	m_uFileBufferSizeKiB = thePrefs.GetFileBufferSize() / 1024u;
	m_uMaxChatHistoryLines = static_cast<UINT>(thePrefs.GetMaxChatHistoryLines());
	m_uMaxMessageSessions = thePrefs.GetMsgSessionsMax();
	m_sTxtEditor = thePrefs.GetTxtEditor();

	m_ctrlTreeOptions.SetImageListColorFlags(theApp.m_iDfltImageListColorFlags);
	CPropertyPage::OnInitDialog();
	InitWindowStyles(this);
	m_ctrlTreeOptions.SetItemHeight(m_ctrlTreeOptions.GetItemHeight() + 2);

	Localize();

	return TRUE;  // return TRUE unless you set the focus to the control
				  // EXCEPTION: OCX Property Pages should return FALSE
}

BOOL CPPgTweaks::OnKillActive()
{
	// if prop page is closed by pressing ENTER we have to explicitly commit any possibly pending
	// data from an open edit control
	m_ctrlTreeOptions.HandleChildControlLosingFocus();
	return CPropertyPage::OnKillActive();
}

BOOL CPPgTweaks::OnApply()
{
	// if prop page is closed by pressing ENTER we have to explicitly commit any possibly pending
	// data from an open edit control
	m_ctrlTreeOptions.HandleChildControlLosingFocus();

	if (!UpdateData())
		return FALSE;

	thePrefs.SetMaxConsPerFive(m_iMaxConnPerFive ? m_iMaxConnPerFive : thePrefs.GetDefaultMaxConperFive());
	theApp.scheduler->original_cons5s = thePrefs.GetMaxConperFive();
	thePrefs.SetMaxHalfConnections(m_iMaxHalfOpen ? m_iMaxHalfOpen : thePrefs.GetDefaultMaxHalfConnections());
	thePrefs.SetConnectionTimeout(thePrefs.NormalizeTimeoutSeconds(m_uConnectionTimeoutSeconds, thePrefs.GetDefaultConnectionTimeoutSeconds()));
	thePrefs.SetDownloadTimeout(thePrefs.NormalizeTimeoutSeconds(m_uDownloadTimeoutSeconds, thePrefs.GetDefaultDownloadTimeoutSeconds()));
	thePrefs.SetEd2kSearchMaxResults(m_uEd2kSearchMaxResults);
	thePrefs.SetEd2kSearchMaxMoreRequests(m_uEd2kSearchMaxMoreRequests);
	thePrefs.SetKadFileSearchTotal(m_uKadFileSearchTotal);
	thePrefs.SetKadKeywordSearchTotal(m_uKadKeywordSearchTotal);
	thePrefs.SetKadFileSearchLifetimeSeconds(m_uKadFileSearchLifetimeSeconds);
	thePrefs.SetKadKeywordSearchLifetimeSeconds(m_uKadKeywordSearchLifetimeSeconds);
	thePrefs.m_bDetectTCPErrorFlooder = m_bDetectTCPErrorFlooder;
	thePrefs.m_uTCPErrorFlooderIntervalMinutes = static_cast<UINT>(m_iTCPErrorFlooderIntervalMinutes);
	thePrefs.m_uTCPErrorFlooderThreshold = static_cast<UINT>(m_iTCPErrorFlooderThreshold);
	thePrefs.m_bConditionalTCPAccept = m_bConditionalTCPAccept;
	thePrefs.SetMaxUploadClientsAllowed(static_cast<UINT>(max(1, m_iMaxUploadClients)));
	thePrefs.SetSlowUploadThresholdFactor(static_cast<float>(_tstof(m_sSlowUploadThresholdFactor)));
	thePrefs.SetSlowUploadGraceSeconds(static_cast<UINT>(max(1, m_iSlowUploadGraceSeconds)));
	thePrefs.SetSlowUploadWarmupSeconds(static_cast<UINT>(max(0, m_iSlowUploadWarmupSeconds)));
	thePrefs.SetZeroUploadRateGraceSeconds(static_cast<UINT>(max(1, m_iZeroUploadRateGraceSeconds)));
	thePrefs.SetSlowUploadCooldownSeconds(static_cast<UINT>(max(1, m_iSlowUploadCooldownSeconds)));
	thePrefs.SetLowRatioBoostEnabled(m_bLowRatioBoost);
	thePrefs.SetLowRatioThreshold(static_cast<float>(_tstof(m_sLowRatioThreshold)));
	thePrefs.SetLowRatioBonus(static_cast<UINT>(max(0, m_iLowRatioBonus)));
	thePrefs.SetLowIDDivisor(static_cast<UINT>(max(1, m_iLowIDDivisor)));
	thePrefs.SetSessionTransferLimitMode(static_cast<ESessionTransferLimitMode>(m_iSessionTransferLimitMode));
	if (m_iSessionTransferLimitMode == static_cast<int>(ESessionTransferLimitMode::AbsoluteMiB))
		thePrefs.SetSessionTransferLimitValue(static_cast<UINT>(max(1, m_iSessionTransferLimitMiB)));
	else
		thePrefs.SetSessionTransferLimitValue(static_cast<UINT>(max(1, m_iSessionTransferLimitPercent)));
	thePrefs.SetSessionTimeLimitSeconds(static_cast<UINT>(max(0, m_iSessionTimeLimitSeconds)));

	const bool bGeoLocationEnabledOld = thePrefs.IsGeoLocationEnabled();
	const UINT uGeoLocationCheckDaysOld = thePrefs.GetGeoLocationCheckDays();
	thePrefs.m_bGeoLocationEnabled = m_bGeoLocationEnabled;
	m_uGeoLocationCheckDays = thePrefs.NormalizeGeoLocationCheckDays(m_uGeoLocationCheckDays);
	thePrefs.SetGeoLocationCheckDays(m_uGeoLocationCheckDays);

	if (thePrefs.AutoTakeED2KLinks() != m_bAutoTakeEd2kLinks) {
		thePrefs.autotakeed2klinks = m_bAutoTakeEd2kLinks;
		if (thePrefs.AutoTakeED2KLinks())
			Ask4RegFix(false, true, false);
		else
			RevertReg();
	}

	thePrefs.SetCreateCrashDumpMode(m_iCreateCrashDumpMode);
	thePrefs.uMaxLogFileSize = PreferenceUiSeams::LogFileSizeKiBToBytes(m_uMaxLogFileSizeKiB);
	thePrefs.iMaxLogBuff = static_cast<int>(m_uMaxLogBufferKiB * 1024u);
	thePrefs.m_iLogFileFormat = static_cast<ELogFileFormat>(PreferenceUiSeams::NormalizeLogFileFormat(m_iLogFileFormat));
	theLog.SetMaxFileSize(thePrefs.GetMaxLogFileSize());
	theVerboseLog.SetMaxFileSize(thePrefs.GetMaxLogFileSize());
	(void)theLog.SetFileFormat(thePrefs.GetLogFileFormat());
	(void)theVerboseLog.SetFileFormat(thePrefs.GetLogFileFormat());

	if (!thePrefs.log2disk && m_bLog2Disk)
		theLog.Open();
	else if (thePrefs.log2disk && !m_bLog2Disk)
		theLog.Close();
	thePrefs.log2disk = m_bLog2Disk;

	thePerfLog.SetSettings(m_bPerfLogEnabled, m_iPerfLogFileFormat, m_sPerfLogFile, m_uPerfLogIntervalMinutes);

	if (thePrefs.GetEnableVerboseOptions()) {
		if (!thePrefs.GetDebug2Disk() && m_bVerbose && m_bDebug2Disk)
			theVerboseLog.Open();
		else if (thePrefs.GetDebug2Disk() && (!m_bVerbose || !m_bDebug2Disk))
			theVerboseLog.Close();
		thePrefs.debug2disk = m_bDebug2Disk;

		thePrefs.m_bDebugSourceExchange = m_bDebugSourceExchange;
		thePrefs.m_bLogBannedClients = m_bLogBannedClients;
		thePrefs.m_bLogRatingDescReceived = m_bLogRatingDescReceived;
		thePrefs.m_bLogSecureIdent = m_bLogSecureIdent;
		thePrefs.m_bLogFilteredIPs = m_bLogFilteredIPs;
		thePrefs.m_bLogFileSaving = m_bLogFileSaving;
		thePrefs.m_bLogA4AF = m_bLogA4AF;
		thePrefs.m_bLogUlDlEvents = m_bLogUlDlEvents;
		thePrefs.m_bFullVerbose = m_bFullVerbose;
		thePrefs.m_byLogLevel = 5 - m_iLogLevel;

		thePrefs.m_bVerbose = m_bVerbose; // store after related options were stored!
	}

	thePrefs.m_bCreditSystem = m_bCreditSystem;
	thePrefs.m_iCommitFiles = m_iCommitFiles;
	thePrefs.m_iExtractMetaData = m_iExtractMetaData;
	thePrefs.filterLANIPs = m_bFilterLANIPs;
	const bool bShowCopyEd2kLinkCmdChanged = thePrefs.GetShowCopyEd2kLinkCmd() != m_bShowCopyEd2kLinkCmd;
	thePrefs.ICH = m_bICH;
	thePrefs.m_iPreviewSmallBlocks = PreferenceUiSeams::NormalizePreviewSmallBlocks(m_iPreviewSmallBlocks);
	thePrefs.beepOnError = m_bBeepOnError;
	thePrefs.m_bShowCopyEd2kLinkCmd = m_bShowCopyEd2kLinkCmd;
	thePrefs.SetFollowMajorityFilenameForNewDownloads(m_bFollowMajorityFilenameForNewDownloads);
	thePrefs.SetFollowMajorityFilenameRequiredPercent(m_uFollowMajorityFilenameRequiredPercent);
	thePrefs.SetFollowMajorityFilenameMinimumVotes(m_uFollowMajorityFilenameMinimumVotes);
	thePrefs.m_bHighresTimer = m_bHighresTimer;
	thePrefs.m_strTxtEditor = m_sTxtEditor;
	thePrefs.SetAutoBroadbandIOEnabled(m_bAutoBroadbandIO);
	thePrefs.SetFileBufferSize(m_uFileBufferSizeKiB * 1024u);
	thePrefs.SetQueueSize(m_iQueueSize);
	m_uFileBufferSizeKiB = thePrefs.GetFileBufferSize() / 1024u;
	m_iQueueSize = static_cast<int>(thePrefs.GetQueueSize());

	if (bShowCopyEd2kLinkCmdChanged)
		theApp.emuledlg->transferwnd->GetDownloadList()->CreateMenus();

	if (thePrefs.GetServerKeepAliveTimeout() != MIN2MS(CPreferences::NormalizeServerKeepAliveTimeoutMinutes(m_uServerKeepAliveTimeout))) {
		thePrefs.SetServerKeepAliveTimeoutMinutes(m_uServerKeepAliveTimeout);
	}
	thePrefs.m_bSparsePartFiles = m_bSparsePartFiles;
	thePrefs.m_bAllocFull = m_bFullAlloc;
	thePrefs.SetMinFreeDiskSpaceConfigGiB(static_cast<UINT>(m_iMinFreeDiskSpaceConfigGB));
	thePrefs.SetMinFreeDiskSpaceTempGiB(static_cast<UINT>(m_iMinFreeDiskSpaceTempGB));
	thePrefs.SetMinFreeDiskSpaceIncomingGiB(static_cast<UINT>(m_iMinFreeDiskSpaceIncomingGB));
	if (thePrefs.GetYourHostname() != m_sYourHostname) {
		thePrefs.SetYourHostname(m_sYourHostname);
		theApp.emuledlg->serverwnd->UpdateMyInfo();
	}
	thePrefs.m_bAutomaticArcPreviewStart = !m_bAutoArchDisable;

	thePrefs.m_bCloseUPnPOnExit = m_bCloseUPnPOnExit;
	thePrefs.SetUPnPBackendMode(static_cast<uint8>(m_iUPnPBackendMode));

	thePrefs.ChangeUserDirMode(m_iShareeMule);

	thePrefs.m_bA4AFSaveCpu = m_bA4AFSaveCpu;
	thePrefs.m_bRestoreLastMainWndDlg = m_bRestoreLastMainWndDlg;
	thePrefs.m_bRestoreLastLogPane = m_bRestoreLastLogPane;
	thePrefs.SetFileBufferTimeLimitSeconds(m_uFileBufferTimeLimitSeconds);
	thePrefs.m_strDateTimeFormat = m_sDateTimeFormat;
	thePrefs.m_strDateTimeFormat4Log = m_sDateTimeFormat4Log;
	thePrefs.m_strDateTimeFormat4Lists = m_sDateTimeFormat4Lists;
	theApp.WriteProfileString(_T("eMule"), kMediaInfoDllPathProfileKey, m_sMediaInfoDllPath);
	thePrefs.m_iMaxChatHistory = static_cast<INT_PTR>(m_uMaxChatHistoryLines);
	thePrefs.SetMsgSessionsMax(m_uMaxMessageSessions);
	thePrefs.m_bPreviewCopiedArchives = m_bPreviewCopiedArchives;
	thePrefs.m_bInspectAllFileTypes = m_bInspectAllFileTypes;
	thePrefs.m_bPreviewOnIconDblClk = m_bPreviewOnIconDblClk;
	thePrefs.m_bShowActiveDownloadsBold = m_bShowActiveDownloadsBold;
	thePrefs.m_bUseSystemFontForMainControls = m_bUseSystemFontForMainControls;
	thePrefs.m_bReBarToolbar = m_bReBarToolbar;
	thePrefs.m_bShowUpDownIconInTaskbar = m_bShowUpDownIconInTaskbar;
	thePrefs.m_bShowVerticalHourMarkers = m_bShowVerticalHourMarkers;
	thePrefs.m_bForceSpeedsToKB = m_bForceSpeedsToKB;
	thePrefs.m_bIconflashOnNewMessage = m_bIconFlashOnNewMessage;
	thePrefs.m_bExtraPreviewWithMenu = m_bExtraPreviewWithMenu;
	thePrefs.m_bKeepUnavailableFixedSharedDirs = m_bKeepUnavailableFixedSharedDirs;
	thePrefs.m_bPartiallyPurgeOldKnownFiles = m_bPartiallyPurgeOldKnownFiles;
	thePrefs.m_bRearrangeKadSearchKeywords = m_bRearrangeKadSearchKeywords;
	thePrefs.msgsecure = m_bMessageFromValidSourcesOnly;

	if (theApp.geolocation != NULL && bGeoLocationEnabledOld != thePrefs.IsGeoLocationEnabled()) {
		if (thePrefs.IsGeoLocationEnabled()) {
			theApp.geolocation->Load();
			theApp.geolocation->QueueBackgroundRefresh();
		} else
			theApp.geolocation->Unload();
		theApp.geolocation->RefreshVisibleWindows();
	} else if (theApp.geolocation != NULL
		&& thePrefs.IsGeoLocationEnabled()
		&& uGeoLocationCheckDaysOld != thePrefs.GetGeoLocationCheckDays())
	{
		theApp.geolocation->QueueBackgroundRefresh();
	}

	if (thePrefs.GetEnableVerboseOptions()) {
		theApp.emuledlg->serverwnd->ToggleDebugWindow();
		theApp.emuledlg->serverwnd->UpdateLogTabSelection();
	}
	theApp.downloadqueue->CheckDiskspace();

	SetModified(FALSE);
	return CPropertyPage::OnApply();
}

void CPPgTweaks::LocalizeItemText(HTREEITEM item, UINT strid)
{
	if (item)
		m_ctrlTreeOptions.SetItemText(item, GetResString(strid));
}

void CPPgTweaks::LocalizeEditLabel(HTREEITEM item, UINT strid)
{
	if (item)
		m_ctrlTreeOptions.SetEditLabel(item, GetResString(strid));
}

void CPPgTweaks::SetTreeToolTip(HTREEITEM item, const CString &text)
{
	if (item != NULL)
		m_treeToolTips[item] = text;
}

void CPPgTweaks::SetTreeToolTip(HTREEITEM item, UINT strid)
{
	SetTreeToolTip(item, GetResString(strid));
}

void CPPgTweaks::Localize()
{
	if (m_hWnd) {
		SetWindowText(GetResString(IDS_PW_TWEAK));
		SetDlgItemText(IDC_WARNING, GetResString(IDS_TWEAKS_WARNING));

		LocalizeEditLabel(m_htiLogLevel, IDS_LOG_LEVEL);
		LocalizeEditLabel(m_htiConnectionTimeout, IDS_CONNECTIONTIMEOUT);
		LocalizeEditLabel(m_htiDownloadTimeout, IDS_DOWNLOADTIMEOUT);
		LocalizeEditLabel(m_htiMaxCon5Sec, IDS_MAXCON5SECLABEL);
		LocalizeEditLabel(m_htiMaxHalfOpen, IDS_MAXHALFOPENCONS);
		m_ctrlTreeOptions.SetEditLabel(m_htiMinFreeDiskSpaceConfig, GetConfigDiskSpaceLabel());
		m_ctrlTreeOptions.SetEditLabel(m_htiMinFreeDiskSpaceTemp, GetTempDiskSpaceLabel());
		m_ctrlTreeOptions.SetEditLabel(m_htiMinFreeDiskSpaceIncoming, GetIncomingDiskSpaceLabel());
		LocalizeEditLabel(m_htiServerKeepAliveTimeout, IDS_SERVERKEEPALIVETIMEOUT);
		LocalizeItemText(m_htiSearchGroup, IDS_SEARCHLIMITS);
		LocalizeItemText(m_htiSearchEd2kGroup, IDS_ED2K_SEARCH);
		LocalizeEditLabel(m_htiSearchEd2kMaxResults, IDS_ED2K_SEARCH_MAX_RESULTS);
		LocalizeEditLabel(m_htiSearchEd2kMaxMoreRequests, IDS_ED2K_SEARCH_MAX_MORE);
		LocalizeItemText(m_htiSearchKadGroup, IDS_SEARCHKAD);
		LocalizeEditLabel(m_htiSearchKadFileTotal, IDS_KAD_SEARCH_FILE_TOTAL);
		LocalizeEditLabel(m_htiSearchKadKeywordTotal, IDS_KAD_SEARCH_KEYWORD_TOTAL);
		LocalizeEditLabel(m_htiSearchKadFileLifetime, IDS_KAD_SEARCH_FILE_LIFETIME);
		LocalizeEditLabel(m_htiSearchKadKeywordLifetime, IDS_KAD_SEARCH_KEYWORD_LIFETIME);
		LocalizeItemText(m_htiBroadband, IDS_BROADBAND);
		LocalizeEditLabel(m_htiMaxUploadClients, IDS_UPLOAD_POLICY_MAX_UPLOAD_CLIENTS);
		LocalizeEditLabel(m_htiSlowUploadThreshold, IDS_UPLOAD_POLICY_SLOW_THRESHOLD_FACTOR);
		LocalizeEditLabel(m_htiSlowUploadGrace, IDS_UPLOAD_POLICY_SLOW_GRACE_SECONDS);
		LocalizeEditLabel(m_htiSlowUploadWarmup, IDS_UPLOAD_POLICY_SLOW_WARMUP_SECONDS);
		LocalizeEditLabel(m_htiZeroUploadRateGrace, IDS_UPLOAD_POLICY_ZERO_RATE_GRACE_SECONDS);
		LocalizeEditLabel(m_htiSlowUploadCooldown, IDS_UPLOAD_POLICY_COOLDOWN_SECONDS);
		LocalizeItemText(m_htiLowRatioBoost, IDS_UPLOAD_POLICY_LOW_RATIO_BOOST);
		LocalizeEditLabel(m_htiLowRatioThreshold, IDS_UPLOAD_POLICY_RATIO_THRESHOLD);
		LocalizeEditLabel(m_htiLowRatioBonus, IDS_UPLOAD_POLICY_SCORE_BONUS);
		LocalizeEditLabel(m_htiLowIDDivisor, IDS_UPLOAD_POLICY_LOWID_DIVISOR);
		LocalizeItemText(m_htiSessionTransferLimit, IDS_UPLOAD_POLICY_SESSION_TRANSFER_LIMIT);
		LocalizeItemText(m_htiSessionTransferLimitDisabled, IDS_DISABLED);
		LocalizeItemText(m_htiSessionTransferLimitPercent, IDS_UPLOAD_POLICY_PERCENT_OF_FILE_SIZE);
		LocalizeItemText(m_htiSessionTransferLimitMiB, IDS_UPLOAD_POLICY_ABSOLUTE_MIB);
		LocalizeEditLabel(m_htiSessionTransferLimitPercentValue, IDS_UPLOAD_POLICY_SESSION_TRANSFER_PERCENT_VALUE);
		LocalizeEditLabel(m_htiSessionTransferLimitMiBValue, IDS_UPLOAD_POLICY_SESSION_TRANSFER_MIB_VALUE);
		LocalizeEditLabel(m_htiSessionTimeLimit, IDS_UPLOAD_POLICY_SESSION_TIME_LIMIT);
		m_ctrlTreeOptions.SetItemText(m_htiGeneralAdvanced, GetGeneralAdvancedLabel());
		LocalizeEditLabel(m_htiYourHostname, IDS_YOURHOSTNAME);	// itsonlyme: hostnameSource
		m_ctrlTreeOptions.SetEditLabel(m_htiTxtEditor, GetTxtEditorLabel());
		m_ctrlTreeOptions.SetItemText(m_htiHighresTimer, GetHighresTimerLabel());
		LocalizeItemText(m_htiA4AFSaveCpu, IDS_A4AF_SAVE_CPU);
		LocalizeItemText(m_htiAutoArch, IDS_DISABLE_AUTOARCHPREV);
		LocalizeItemText(m_htiAutoTakeEd2kLinks, IDS_AUTOTAKEED2KLINKS);
		LocalizeItemText(m_htiCloseUPnPPorts, IDS_UPNPCLOSEONEXIT);
		m_ctrlTreeOptions.SetItemText(m_htiCommit, GetCommitPolicyLabel());
		m_ctrlTreeOptions.SetItemText(m_htiCommitAlways, GetCommitAlwaysLabel());
		m_ctrlTreeOptions.SetItemText(m_htiCommitNever, GetCommitNeverLabel());
		m_ctrlTreeOptions.SetItemText(m_htiCommitOnShutdown, GetCommitOnShutdownLabel());
		LocalizeItemText(m_htiConditionalTCPAccept, IDS_CONDTCPACCEPT);
		LocalizeItemText(m_htiCreditSystem, IDS_USECREDITSYSTEM);
		LocalizeItemText(m_htiDebug2Disk, IDS_LOG2DISK);
		LocalizeItemText(m_htiDebugSourceExchange, IDS_DEBUG_SOURCE_EXCHANGE);
		LocalizeItemText(m_htiExtraPreviewWithMenu, IDS_EXTRAPREVIEWWITHMENU);
		LocalizeItemText(m_htiExtractMetaData, IDS_EXTRACT_META_DATA);
		m_ctrlTreeOptions.SetItemText(m_htiExtractMetaDataID3Lib, GetExtractMetaDataAutoLabel());
		//LocalizeItemText(m_htiExtractMetaDataMediaDet, IDS_META_DATA_MEDIADET);
		LocalizeItemText(m_htiExtractMetaDataNever, IDS_NEVER);
		m_ctrlTreeOptions.SetEditLabel(m_htiMediaInfoDllPath, GetMediaInfoDllPathLabel());
		LocalizeItemText(m_htiFilterLANIPs, IDS_PW_FILTER);
		LocalizeItemText(m_htiForceSpeedsToKB, IDS_FORCESPEEDSTOKB);
		LocalizeItemText(m_htiFullAlloc, IDS_FULLALLOC);
		m_ctrlTreeOptions.SetItemText(m_htiHiddenDisplay, GetDisplayTweaksLabel());
		m_ctrlTreeOptions.SetItemText(m_htiHiddenFile, GetFileBehaviorLabel());
		m_ctrlTreeOptions.SetItemText(m_htiHiddenSecurity, GetSecurityTweaksLabel());
		m_ctrlTreeOptions.SetItemText(m_htiICH, GetIchLabel());
		m_ctrlTreeOptions.SetItemText(m_htiPreviewSmallBlocks, GetPreviewSmallBlocksLabel());
		LocalizeItemText(m_htiPreviewSmallBlocksDisabled, IDS_DISABLED);
		m_ctrlTreeOptions.SetItemText(m_htiPreviewSmallBlocksAllow, GetPreviewSmallBlocksAllowLabel());
		m_ctrlTreeOptions.SetItemText(m_htiPreviewSmallBlocksForce, GetPreviewSmallBlocksForceLabel());
		m_ctrlTreeOptions.SetItemText(m_htiBeepOnError, GetBeepOnErrorLabel());
		m_ctrlTreeOptions.SetItemText(m_htiShowCopyEd2kLinkCmd, GetShowCopyEd2kLinkCmdLabel());
		m_ctrlTreeOptions.SetItemText(m_htiFollowMajorityFilenameForNewDownloads, GetFollowMajorityFilenameDefaultLabel());
		m_ctrlTreeOptions.SetEditLabel(m_htiFollowMajorityFilenameRequiredPercent, GetFollowMajorityFilenameRequiredPercentLabel());
		m_ctrlTreeOptions.SetEditLabel(m_htiFollowMajorityFilenameMinimumVotes, GetFollowMajorityFilenameMinimumVotesLabel());
		LocalizeItemText(m_htiDetectTCPErrorFlooder, IDS_DETECT_TCP_ERROR_FLOODER);
		LocalizeEditLabel(m_htiTCPErrorFlooderIntervalMinutes, IDS_TCP_ERROR_FLOODER_INTERVAL_MINUTES);
		LocalizeEditLabel(m_htiTCPErrorFlooderThreshold, IDS_TCP_ERROR_FLOODER_THRESHOLD);
		m_ctrlTreeOptions.SetItemText(m_htiHiddenStartup, GetStartupTweaksLabel());
		LocalizeItemText(m_htiKeepUnavailableFixedSharedDirs, IDS_KEEPUNAVAILABLEFIXEDSHAREDDIRS);
		m_ctrlTreeOptions.SetItemText(m_htiLoggingGroup, GetLoggingTweaksLabel());
		m_ctrlTreeOptions.SetItemText(m_htiCreateCrashDump, GetCreateCrashDumpLabel());
		m_ctrlTreeOptions.SetItemText(m_htiCreateCrashDumpDisabled, GetCreateCrashDumpDisabledLabel());
		m_ctrlTreeOptions.SetItemText(m_htiCreateCrashDumpPrompt, GetCreateCrashDumpPromptLabel());
		m_ctrlTreeOptions.SetItemText(m_htiCreateCrashDumpAlways, GetCreateCrashDumpAlwaysLabel());
		m_ctrlTreeOptions.SetEditLabel(m_htiMaxLogFileSize, GetMaxLogFileSizeLabel());
		m_ctrlTreeOptions.SetEditLabel(m_htiMaxLogBuffer, GetMaxLogBufferLabel());
		m_ctrlTreeOptions.SetItemText(m_htiLogFileFormat, GetLogFileFormatLabel());
		m_ctrlTreeOptions.SetItemText(m_htiLogFileFormatUnicode, GetLogFileFormatUnicodeLabel());
		m_ctrlTreeOptions.SetItemText(m_htiLogFileFormatUtf8, GetLogFileFormatUtf8Label());
		LocalizeItemText(m_htiLog2Disk, IDS_LOG2DISK);
		LocalizeItemText(m_htiPerfLog, IDS_ENABLE_PERFLOG);
		m_ctrlTreeOptions.SetItemText(m_htiPerfLogFileFormat, GetPerfLogFileFormatLabel());
		m_ctrlTreeOptions.SetItemText(m_htiPerfLogFileFormatCsv, GetResString(IDS_TWEAKS_PERF_LOG_FORMAT_CSV));
		m_ctrlTreeOptions.SetItemText(m_htiPerfLogFileFormatMrtg, GetResString(IDS_TWEAKS_PERF_LOG_FORMAT_MRTG));
		m_ctrlTreeOptions.SetEditLabel(m_htiPerfLogFile, GetPerfLogFileLabel());
		m_ctrlTreeOptions.SetEditLabel(m_htiPerfLogInterval, GetPerfLogIntervalLabel());
		LocalizeItemText(m_htiLogA4AF, IDS_LOG_A4AF);
		LocalizeItemText(m_htiLogBannedClients, IDS_LOG_BANNED_CLIENTS);
		LocalizeItemText(m_htiLogFileSaving, IDS_LOG_FILE_SAVING);
		LocalizeItemText(m_htiLogFilteredIPs, IDS_LOG_FILTERED_IPS);
		LocalizeItemText(m_htiLogRatingDescReceived, IDS_LOG_RATING_RECV);
		LocalizeItemText(m_htiLogSecureIdent, IDS_LOG_SECURE_IDENT);
		LocalizeItemText(m_htiLogUlDlEvents, IDS_LOG_ULDL_EVENTS);
		LocalizeItemText(m_htiMessageFromValidSourcesOnly, IDS_MESSAGEFROMVALIDSOURCESONLY);
		m_ctrlTreeOptions.SetEditLabel(m_htiMaxChatHistoryLines, GetMaxChatHistoryLinesLabel());
		m_ctrlTreeOptions.SetEditLabel(m_htiMaxMessageSessions, GetMaxMessageSessionsLabel());
		LocalizeItemText(m_htiPartiallyPurgeOldKnownFiles, IDS_PARTIALLYPURGEOLDKNOWNFILES);
		LocalizeItemText(m_htiPreviewCopiedArchives, IDS_PREVIEWCOPIEDARCHIVES);
		LocalizeItemText(m_htiPreviewOnIconDblClk, IDS_PREVIEWONICONDBLCLK);
		LocalizeItemText(m_htiRearrangeKadSearchKeywords, IDS_REARRANGEKADSEARCHKEYWORDS);
		LocalizeItemText(m_htiReBarToolbar, IDS_REBARTOOLBAR);
		LocalizeItemText(m_htiRestoreLastLogPane, IDS_RESTORELASTLOGPANE);
		LocalizeItemText(m_htiRestoreLastMainWndDlg, IDS_RESTORELASTMAINWNDDLG);
		LocalizeItemText(m_htiShareeMule, IDS_SHAREEMULELABEL);
		LocalizeItemText(m_htiShareeMuleMultiUser, IDS_SHAREEMULEMULTI);
		LocalizeItemText(m_htiShareeMuleOldStyle, IDS_SHAREEMULEOLD);
		LocalizeItemText(m_htiShareeMulePublicUser, IDS_SHAREEMULEPUBLIC);
		LocalizeItemText(m_htiShowActiveDownloadsBold, IDS_SHOWACTIVEDOWNLOADSBOLD);
		m_ctrlTreeOptions.SetItemText(m_htiIconFlashOnNewMessage, GetIconFlashOnNewMessageLabel());
		LocalizeItemText(m_htiShowUpDownIconInTaskbar, IDS_SHOWUPDOWNICONINTASKBAR);
		LocalizeItemText(m_htiShowVerticalHourMarkers, IDS_SHOWVERTICALHOURMARKERS);
		LocalizeItemText(m_htiGeoLocationEnabled, IDS_ENABLE_GEOLOCATION);
		LocalizeItemText(m_htiSparsePartFiles, IDS_SPARSEPARTFILES);
		LocalizeItemText(m_htiTCPGroup, IDS_TCPIP_CONNS);
		LocalizeItemText(m_htiUPnP, IDS_UPNP);
		LocalizeItemText(m_htiUPnPBackendMode, IDS_UPNPBACKENDMODE);
		LocalizeItemText(m_htiUPnPBackendModeAutomatic, IDS_AUTOMATIC);
		LocalizeItemText(m_htiUPnPBackendModeIgdOnly, IDS_UPNPBACKENDMODE_IGDONLY);
		LocalizeItemText(m_htiUPnPBackendModePcpNatPmpOnly, IDS_UPNPBACKENDMODE_PCPONLY);
		LocalizeItemText(m_htiUseSystemFontForMainControls, IDS_USESYSTEMFONTFORMAINCONTROLS);
		LocalizeItemText(m_htiVerbose, IDS_ENABLED);
		if (m_htiFullVerbose)
			m_ctrlTreeOptions.SetItemText(m_htiFullVerbose, GetFullVerboseLabel());
		LocalizeItemText(m_htiVerboseGroup, IDS_VERBOSE);
		m_ctrlTreeOptions.SetItemText(m_htiStoragePersistence, GetStoragePersistenceLabel());
		m_ctrlTreeOptions.SetItemText(m_htiAutoBroadbandIO, _T("Auto broadband IO buffers"));
		LocalizeEditLabel(m_htiDateTimeFormat4Lists, IDS_DATETIMEFORMAT4LISTS);
		LocalizeEditLabel(m_htiFileBufferTimeLimit, IDS_FILEBUFFERTIMELIMIT);
		m_ctrlTreeOptions.SetEditLabel(m_htiFileBufferSize, GetFileBufferSizeLabel());
		LocalizeEditLabel(m_htiQueueSize, IDS_QUEUESIZE);
		m_ctrlTreeOptions.SetEditLabel(m_htiGeoLocationCheckDays, GetGeoLocationIntervalLabel());
		LocalizeItemText(m_htiInspectAllFileTypes, IDS_INSPECTALLFILETYPES);
		m_ctrlTreeOptions.SetEditLabel(m_htiDateTimeFormat, GetDateTimeFormatLabel());
		m_ctrlTreeOptions.SetEditLabel(m_htiDateTimeFormat4Log, GetDateTimeFormat4LogLabel());
	}
}

void CPPgTweaks::OnDestroy()
{
	m_ctrlTreeOptions.DeleteAllItems();
	m_ctrlTreeOptions.DestroyWindow();
	m_bInitializedTreeOpts = false;
	m_treeToolTips.clear();
	m_htiTCPGroup = NULL;
	m_htiSearchGroup = NULL;
	m_htiSearchEd2kGroup = NULL;
	m_htiSearchEd2kMaxResults = NULL;
	m_htiSearchEd2kMaxMoreRequests = NULL;
	m_htiSearchKadGroup = NULL;
	m_htiSearchKadFileTotal = NULL;
	m_htiSearchKadKeywordTotal = NULL;
	m_htiSearchKadFileLifetime = NULL;
	m_htiSearchKadKeywordLifetime = NULL;
	m_htiBroadband = NULL;
	m_htiMaxUploadClients = NULL;
	m_htiSlowUploadThreshold = NULL;
	m_htiSlowUploadGrace = NULL;
	m_htiSlowUploadWarmup = NULL;
	m_htiZeroUploadRateGrace = NULL;
	m_htiSlowUploadCooldown = NULL;
	m_htiLowRatioBoost = NULL;
	m_htiLowRatioThreshold = NULL;
	m_htiLowRatioBonus = NULL;
	m_htiLowIDDivisor = NULL;
	m_htiSessionTransferLimit = NULL;
	m_htiSessionTransferLimitDisabled = NULL;
	m_htiSessionTransferLimitPercent = NULL;
	m_htiSessionTransferLimitMiB = NULL;
	m_htiSessionTransferLimitPercentValue = NULL;
	m_htiSessionTransferLimitMiBValue = NULL;
	m_htiSessionTimeLimit = NULL;
	m_htiMaxCon5Sec = NULL;
	m_htiMaxHalfOpen = NULL;
	m_htiConditionalTCPAccept = NULL;
	m_htiAutoTakeEd2kLinks = NULL;
	m_htiVerboseGroup = NULL;
	m_htiVerbose = NULL;
	m_htiFullVerbose = NULL;
	m_htiDebugSourceExchange = NULL;
	m_htiBeepOnError = NULL;
	m_htiCreateCrashDump = NULL;
	m_htiCreateCrashDumpDisabled = NULL;
	m_htiCreateCrashDumpPrompt = NULL;
	m_htiCreateCrashDumpAlways = NULL;
	m_htiDateTimeFormat = NULL;
	m_htiDateTimeFormat4Log = NULL;
	m_htiHighresTimer = NULL;
	m_htiHiddenDisplay = NULL;
	m_htiHiddenFile = NULL;
	m_htiHiddenSecurity = NULL;
	m_htiFollowMajorityFilenameForNewDownloads = NULL;
	m_htiFollowMajorityFilenameRequiredPercent = NULL;
	m_htiFollowMajorityFilenameMinimumVotes = NULL;
	m_htiICH = NULL;
	m_htiIconFlashOnNewMessage = NULL;
	m_htiDetectTCPErrorFlooder = NULL;
	m_htiTCPErrorFlooderIntervalMinutes = NULL;
	m_htiTCPErrorFlooderThreshold = NULL;
	m_htiHiddenStartup = NULL;
	m_htiLogBannedClients = NULL;
	m_htiLogRatingDescReceived = NULL;
	m_htiLogSecureIdent = NULL;
	m_htiLogFilteredIPs = NULL;
	m_htiLogFileSaving = NULL;
	m_htiLogA4AF = NULL;
	m_htiLogLevel = NULL;
	m_htiLogUlDlEvents = NULL;
	m_htiConnectionTimeout = NULL;
	m_htiGeneralAdvanced = NULL;
	m_htiLoggingGroup = NULL;
	m_htiCreditSystem = NULL;
	m_htiDateTimeFormat4Lists = NULL;
	m_htiMaxChatHistoryLines = NULL;
	m_htiMaxMessageSessions = NULL;
	m_htiPreviewCopiedArchives = NULL;
	m_htiPreviewSmallBlocks = NULL;
	m_htiPreviewSmallBlocksDisabled = NULL;
	m_htiPreviewSmallBlocksAllow = NULL;
	m_htiPreviewSmallBlocksForce = NULL;
	m_htiPreviewOnIconDblClk = NULL;
	m_htiShowCopyEd2kLinkCmd = NULL;
	m_htiShowActiveDownloadsBold = NULL;
	m_htiUseSystemFontForMainControls = NULL;
	m_htiReBarToolbar = NULL;
	m_htiShowUpDownIconInTaskbar = NULL;
	m_htiShowVerticalHourMarkers = NULL;
	m_htiForceSpeedsToKB = NULL;
	m_htiGeoLocationEnabled = NULL;
	m_htiGeoLocationCheckDays = NULL;
	m_htiExtraPreviewWithMenu = NULL;
	m_htiKeepUnavailableFixedSharedDirs = NULL;
	m_htiPartiallyPurgeOldKnownFiles = NULL;
	m_htiRearrangeKadSearchKeywords = NULL;
	m_htiMessageFromValidSourcesOnly = NULL;
	m_htiAutoBroadbandIO = NULL;
	m_htiFileBufferTimeLimit = NULL;
	m_htiFileBufferSize = NULL;
	m_htiQueueSize = NULL;
	m_htiDownloadTimeout = NULL;
	m_htiRestoreLastLogPane = NULL;
	m_htiRestoreLastMainWndDlg = NULL;
	m_htiStoragePersistence = NULL;
	m_htiInspectAllFileTypes = NULL;
	m_htiLog2Disk = NULL;
	m_htiPerfLog = NULL;
	m_htiPerfLogFileFormat = NULL;
	m_htiPerfLogFileFormatCsv = NULL;
	m_htiPerfLogFileFormatMrtg = NULL;
	m_htiPerfLogFile = NULL;
	m_htiPerfLogInterval = NULL;
	m_htiLogFileFormat = NULL;
	m_htiLogFileFormatUnicode = NULL;
	m_htiLogFileFormatUtf8 = NULL;
	m_htiMaxLogFileSize = NULL;
	m_htiMaxLogBuffer = NULL;
	m_htiDebug2Disk = NULL;
	m_htiCommit = NULL;
	m_htiCommitNever = NULL;
	m_htiCommitOnShutdown = NULL;
	m_htiCommitAlways = NULL;
	m_htiFilterLANIPs = NULL;
	m_htiServerKeepAliveTimeout = NULL;
	m_htiSparsePartFiles = NULL;
	m_htiFullAlloc = NULL;
	m_htiMinFreeDiskSpaceConfig = NULL;
	m_htiMinFreeDiskSpaceTemp = NULL;
	m_htiMinFreeDiskSpaceIncoming = NULL;
	m_htiYourHostname = NULL;
	m_htiA4AFSaveCpu = NULL;
	m_htiExtractMetaData = NULL;
	m_htiExtractMetaDataNever = NULL;
	m_htiExtractMetaDataID3Lib = NULL;
	m_htiMediaInfoDllPath = NULL;
	m_htiAutoArch = NULL;
	m_htiTxtEditor = NULL;
	m_htiUPnP = NULL;
	m_htiCloseUPnPPorts = NULL;
	m_htiUPnPBackendMode = NULL;
	m_htiUPnPBackendModeAutomatic = NULL;
	m_htiUPnPBackendModeIgdOnly = NULL;
	m_htiUPnPBackendModePcpNatPmpOnly = NULL;
	m_htiShareeMule = NULL;
	m_htiShareeMuleMultiUser = NULL;
	m_htiShareeMulePublicUser = NULL;
	m_htiShareeMuleOldStyle = NULL;
	//m_htiExtractMetaDataMediaDet = NULL;
	CPropertyPage::OnDestroy();
}

LRESULT CPPgTweaks::OnTreeOptsCtrlNotify(WPARAM wParam, LPARAM lParam)
{
	if (wParam == IDC_EXT_OPTS) {
		TREEOPTSCTRLNOTIFY *pton = (TREEOPTSCTRLNOTIFY*)lParam;
		if (m_htiVerbose && pton->hItem == m_htiVerbose) {
			BOOL bCheck;
			if (m_ctrlTreeOptions.GetCheckBox(m_htiVerbose, bCheck)) {
				if (m_htiFullVerbose)
					m_ctrlTreeOptions.SetCheckBoxEnable(m_htiFullVerbose, bCheck);
				if (m_htiDebug2Disk)
					m_ctrlTreeOptions.SetCheckBoxEnable(m_htiDebug2Disk, bCheck);
				if (m_htiDebugSourceExchange)
					m_ctrlTreeOptions.SetCheckBoxEnable(m_htiDebugSourceExchange, bCheck);
				if (m_htiLogBannedClients)
					m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogBannedClients, bCheck);
				if (m_htiLogRatingDescReceived)
					m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogRatingDescReceived, bCheck);
				if (m_htiLogSecureIdent)
					m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogSecureIdent, bCheck);
				if (m_htiLogFilteredIPs)
					m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogFilteredIPs, bCheck);
				if (m_htiLogFileSaving)
					m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogFileSaving, bCheck);
				if (m_htiLogA4AF)
					m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogA4AF, bCheck);
				if (m_htiLogUlDlEvents)
					m_ctrlTreeOptions.SetCheckBoxEnable(m_htiLogUlDlEvents, bCheck);
			}
		} else if (m_htiDetectTCPErrorFlooder && pton->hItem == m_htiDetectTCPErrorFlooder) {
			SetModified();
		} else if (m_htiGeoLocationEnabled && pton->hItem == m_htiGeoLocationEnabled) {
			m_ctrlTreeOptions.Expand(m_htiGeoLocationEnabled, TVE_EXPAND);
		} else if ((m_htiShareeMuleMultiUser  && pton->hItem == m_htiShareeMuleMultiUser)
				|| (m_htiShareeMulePublicUser && pton->hItem == m_htiShareeMulePublicUser)
				|| (m_htiShareeMuleOldStyle   && pton->hItem == m_htiShareeMuleOldStyle))
		{
			if (m_htiShareeMule && !m_bShowedWarning) {
				HTREEITEM tmp;
				int nIndex;
				m_ctrlTreeOptions.GetRadioButton(m_htiShareeMule, nIndex, tmp);
				if (nIndex != thePrefs.m_nCurrentUserDirMode) {
					// TODO offer cancel option
					LocMessageBox(IDS_SHAREEMULEWARNING, MB_ICONINFORMATION | MB_OK, 0);
					m_bShowedWarning = true;
				}
			}
		}
		SetModified();
	}
	return 0;
}

void CPPgTweaks::OnTvnGetInfoTip(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMTVGETINFOTIP pGetInfoTip = reinterpret_cast<LPNMTVGETINFOTIP>(pNMHDR);
	if (pGetInfoTip != NULL && pGetInfoTip->pszText != NULL && pGetInfoTip->cchTextMax > 0) {
		const std::map<HTREEITEM, CString>::const_iterator it = m_treeToolTips.find(pGetInfoTip->hItem);
		if (it != m_treeToolTips.end()) {
			_tcsncpy(pGetInfoTip->pszText, it->second, pGetInfoTip->cchTextMax);
			pGetInfoTip->pszText[pGetInfoTip->cchTextMax - 1] = _T('\0');
		}
	}
	*pResult = 0;
}

void CPPgTweaks::OnHelp()
{
	theApp.ShowHelp(eMule_FAQ_Preferences_Extended_Settings);
}

BOOL CPPgTweaks::OnCommand(WPARAM wParam, LPARAM lParam)
{
	return (wParam == ID_HELP) ? OnHelpInfo(NULL) : __super::OnCommand(wParam, lParam);
}

BOOL CPPgTweaks::OnHelpInfo(HELPINFO*)
{
	OnHelp();
	return TRUE;
}
