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
#include "UserMsgs.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
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

	static void FailTreeValidation(CDataExchange *pDX, CTreeOptionsCtrlEx &ctrl, HTREEITEM hItem, const CString &detail)
	{
		RevealTreeItem(ctrl, hItem);
		const CString label = GetTreeItemLabel(ctrl, hItem);
		CString message;
		message.Format(_T("Invalid value for \"%s\".\n\n%s"), static_cast<LPCTSTR>(label), static_cast<LPCTSTR>(detail));
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
				FailTreeValidation(pDX, ctrl, hItem, _T("Please enter an integer."));
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
				FailTreeValidation(pDX, ctrl, hItem, _T("Please enter an integer."));
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
		CString label(GetResString(IDS_FILEBUFFERSIZE));
		label.Append(_T(" [KiB]"));
		return label;
	}
}


///////////////////////////////////////////////////////////////////////////////
// CPPgTweaks dialog

IMPLEMENT_DYNAMIC(CPPgTweaks, CPropertyPage)

BEGIN_MESSAGE_MAP(CPPgTweaks, CPropertyPage)
	ON_WM_DESTROY()
	ON_WM_SIZE()
	ON_MESSAGE(UM_TREEOPTSCTRL_NOTIFY, OnTreeOptsCtrlNotify)
	ON_WM_HELPINFO()
END_MESSAGE_MAP()

CPPgTweaks::CPPgTweaks()
	: CPropertyPage(CPPgTweaks::IDD)
	, m_ctrlTreeOptions(theApp.m_iDfltImageListColorFlags)
	, m_szBaseClient()
	, m_rcWarning()
	, m_rcTree()
	, m_htiA4AFSaveCpu()
	, m_htiAutoArch()
	, m_htiAutoBrowseRemoteShares()
	, m_htiAutoTakeEd2kLinks()
	, m_htiBroadband()
	, m_htiBBMaxUploadClients()
	, m_htiBBSlowThreshold()
	, m_htiBBSlowGrace()
	, m_htiBBSlowWarmup()
	, m_htiBBZeroRateGrace()
	, m_htiBBCooldown()
	, m_htiBBLowRatioBoost()
	, m_htiBBLowRatioThreshold()
	, m_htiBBLowRatioBonus()
	, m_htiBBLowIdDivisor()
	, m_htiBBSessionTransfer()
	, m_htiBBSessionTransferDisabled()
	, m_htiBBSessionTransferPercent()
	, m_htiBBSessionTransferMiB()
	, m_htiBBSessionTransferPercentValue()
	, m_htiBBSessionTransferMiBValue()
	, m_htiBBSessionTimeLimit()
	, m_htiCheckDiskspace()
	, m_htiCloseUPnPPorts()
	, m_htiCommit()
	, m_htiCommitAlways()
	, m_htiCommitNever()
	, m_htiCommitOnShutdown()
	, m_htiConditionalTCPAccept()
	, m_htiCreditSystem()
	, m_htiDebug2Disk()
	, m_htiDebugSourceExchange()
	, m_htiExtControls()
	, m_htiHiddenDisplay()
	, m_htiHiddenFile()
	, m_htiHiddenSecurity()
	, m_htiHiddenStartup()
	, m_htiExtractMetaData()
	, m_htiExtractMetaDataID3Lib()
	, m_htiExtractMetaDataNever()
	, m_htiFilterLANIPs()
	, m_htiFullAlloc()
	, m_htiImportParts()
	, m_htiInspectAllFileTypes()
	, m_htiLog2Disk()
	, m_htiPerfLog()
	, m_htiLogA4AF()
	, m_htiLogBannedClients()
	, m_htiLogFileSaving()
	, m_htiLogFilteredIPs()
	, m_htiLogLevel()
	, m_htiLogRatingDescReceived()
	, m_htiLogSecureIdent()
	, m_htiLogUlDlEvents()
	, m_htiConnectionTimeout()
	, m_htiMaxCon5Sec()
	, m_htiMaxHalfOpen()
	, m_htiDateTimeFormat4Lists()
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
	, m_htiPreviewOnIconDblClk()
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
	, m_htiAdjustNTFSDaylightFileTime()
	, m_htiDetectTCPErrorFlooder()
	, m_htiTCPErrorFlooderIntervalMinutes()
	, m_htiTCPErrorFlooderThreshold()
	, m_htiRearrangeKadSearchKeywords()
	, m_htiMessageFromValidSourcesOnly()
	, m_htiFileBufferTimeLimit()
	, m_htiFileBufferSize()
	, m_htiQueueSize()
	, m_htiDownloadTimeout()
	, m_htiRestoreLastLogPane()
	, m_htiRestoreLastMainWndDlg()
	, m_htiMinFreeDiskSpace()
	, m_htiServerKeepAliveTimeout()
	, m_htiShareeMule()
	, m_htiShareeMuleMultiUser()
	, m_htiShareeMuleOldStyle()
	, m_htiShareeMulePublicUser()
	, m_htiSkipWANIPSetup()
	, m_htiSkipWANPPPSetup()
	, m_htiSparsePartFiles()
	, m_htiTCPGroup()
	, m_htiUPnP()
	, m_htiVerbose()
	, m_htiVerboseGroup()
	, m_htiYourHostname()
	, m_iMinFreeDiskSpaceGB()
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
	, m_iInspectAllFileTypes()
	, m_iLogLevel()
	, m_iMaxConnPerFive()
	, m_iMaxHalfOpen()
	, m_iShareeMule()
	, m_iBBSessionTransferMode()
	, m_sDateTimeFormat4Lists()
	, m_sBBSlowThresholdFactor()
	, m_sBBLowRatioThreshold()
	, m_bA4AFSaveCpu()
	, m_bAutoArchDisable(true)
	, m_bAutoBrowseRemoteShares()
	, m_bAutoTakeEd2kLinks()
	, m_bBBLowRatioBoost()
	, m_bAdjustNTFSDaylightFileTime()
	, m_bCheckDiskspace()
	, m_bCloseUPnPOnExit(true)
	, m_bConditionalTCPAccept()
	, m_bCreditSystem()
	, m_bDebug2Disk()
	, m_bDebugSourceExchange()
	, m_bExtControls()
	, m_bExtraPreviewWithMenu()
	, m_bFilterLANIPs()
	, m_bFullAlloc()
	, m_bGeoLocationEnabled()
	, m_bImportParts()
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
	, m_bShowUpDownIconInTaskbar()
	, m_bShowVerticalHourMarkers()
	, m_bSkipWANIPSetup()
	, m_bSkipWANPPPSetup()
	, m_bSparsePartFiles()
	, m_bVerbose()
	, m_bUseSystemFontForMainControls()
	, m_bForceSpeedsToKB()
	, m_uFileBufferTimeLimitSeconds()
	, m_uGeoLocationCheckDays()
	, m_iBBMaxUploadClients()
	, m_iBBSlowGraceSeconds()
	, m_iBBSlowWarmupSeconds()
	, m_iBBZeroRateGraceSeconds()
	, m_iBBCooldownSeconds()
	, m_iBBLowRatioBonus()
	, m_iBBLowIdDivisor()
	, m_iBBSessionTransferPercent()
	, m_iBBSessionTransferMiB()
	, m_iBBSessionTimeLimitSeconds()
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
		m_htiBBMaxUploadClients = m_ctrlTreeOptions.InsertItem(GetResString(IDS_BB_MAX_UPLOAD_CLIENTS), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiBroadband);
		m_ctrlTreeOptions.AddEditBox(m_htiBBMaxUploadClients, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiBBSlowThreshold = m_ctrlTreeOptions.InsertItem(GetResString(IDS_BB_SLOW_THRESHOLD_FACTOR), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiBroadband);
		m_ctrlTreeOptions.AddEditBox(m_htiBBSlowThreshold, RUNTIME_CLASS(CTreeOptionsEditEx));
		m_htiBBSlowGrace = m_ctrlTreeOptions.InsertItem(GetResString(IDS_BB_SLOW_GRACE_SECONDS), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiBroadband);
		m_ctrlTreeOptions.AddEditBox(m_htiBBSlowGrace, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiBBSlowWarmup = m_ctrlTreeOptions.InsertItem(GetResString(IDS_BB_SLOW_WARMUP_SECONDS), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiBroadband);
		m_ctrlTreeOptions.AddEditBox(m_htiBBSlowWarmup, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiBBZeroRateGrace = m_ctrlTreeOptions.InsertItem(GetResString(IDS_BB_ZERO_RATE_GRACE_SECONDS), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiBroadband);
		m_ctrlTreeOptions.AddEditBox(m_htiBBZeroRateGrace, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiBBCooldown = m_ctrlTreeOptions.InsertItem(GetResString(IDS_BB_COOLDOWN_SECONDS), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiBroadband);
		m_ctrlTreeOptions.AddEditBox(m_htiBBCooldown, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiBBLowRatioBoost = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_BB_LOW_RATIO_BOOST), m_htiBroadband, m_bBBLowRatioBoost);
		m_htiBBLowRatioThreshold = m_ctrlTreeOptions.InsertItem(GetResString(IDS_BB_RATIO_THRESHOLD), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiBBLowRatioBoost);
		m_ctrlTreeOptions.AddEditBox(m_htiBBLowRatioThreshold, RUNTIME_CLASS(CTreeOptionsEditEx));
		m_htiBBLowRatioBonus = m_ctrlTreeOptions.InsertItem(GetResString(IDS_BB_SCORE_BONUS), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiBBLowRatioBoost);
		m_ctrlTreeOptions.AddEditBox(m_htiBBLowRatioBonus, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiBBLowIdDivisor = m_ctrlTreeOptions.InsertItem(GetResString(IDS_BB_LOWID_DIVISOR), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiBroadband);
		m_ctrlTreeOptions.AddEditBox(m_htiBBLowIdDivisor, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiBBSessionTransfer = m_ctrlTreeOptions.InsertGroup(GetResString(IDS_BB_SESSION_TRANSFER_LIMIT), iImgDynyp, m_htiBroadband);
		m_htiBBSessionTransferDisabled = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_DISABLED), m_htiBBSessionTransfer, m_iBBSessionTransferMode == BBSTM_DISABLED);
		m_htiBBSessionTransferPercent = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_BB_PERCENT_OF_FILE_SIZE), m_htiBBSessionTransfer, m_iBBSessionTransferMode == BBSTM_PERCENT_OF_FILE);
		m_htiBBSessionTransferPercentValue = m_ctrlTreeOptions.InsertItem(GetResString(IDS_BB_SESSION_TRANSFER_PERCENT_VALUE), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiBBSessionTransferPercent);
		m_ctrlTreeOptions.AddEditBox(m_htiBBSessionTransferPercentValue, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiBBSessionTransferMiB = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_BB_ABSOLUTE_MIB), m_htiBBSessionTransfer, m_iBBSessionTransferMode == BBSTM_ABSOLUTE_MIB);
		m_htiBBSessionTransferMiBValue = m_ctrlTreeOptions.InsertItem(GetResString(IDS_BB_SESSION_TRANSFER_MIB_VALUE), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiBBSessionTransferMiB);
		m_ctrlTreeOptions.AddEditBox(m_htiBBSessionTransferMiBValue, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiBBSessionTimeLimit = m_ctrlTreeOptions.InsertItem(GetResString(IDS_BB_SESSION_TIME_LIMIT), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiBroadband);
		m_ctrlTreeOptions.AddEditBox(m_htiBBSessionTimeLimit, RUNTIME_CLASS(CNumTreeOptionsEdit));

		/////////////////////////////////////////////////////////////////////////////
		// Miscellaneous group
		//
		m_htiAutoTakeEd2kLinks = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_AUTOTAKEED2KLINKS), TVI_ROOT, m_bAutoTakeEd2kLinks);
		m_htiAutoBrowseRemoteShares = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_AUTOBROWSEREMOTESHARES), TVI_ROOT, m_bAutoBrowseRemoteShares);
		m_htiCreditSystem = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_USECREDITSYSTEM), TVI_ROOT, m_bCreditSystem);
		m_htiFilterLANIPs = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_PW_FILTER), TVI_ROOT, m_bFilterLANIPs);
		m_htiExtControls = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_SHOWEXTSETTINGS), TVI_ROOT, m_bExtControls);
		m_htiA4AFSaveCpu = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_A4AF_SAVE_CPU), TVI_ROOT, m_bA4AFSaveCpu); // ZZ:DownloadManager
		m_htiAutoArch = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_DISABLE_AUTOARCHPREV), TVI_ROOT, m_bAutoArchDisable);
		m_htiYourHostname = m_ctrlTreeOptions.InsertItem(GetResString(IDS_YOURHOSTNAME), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, TVI_ROOT);
		m_ctrlTreeOptions.AddEditBox(m_htiYourHostname, RUNTIME_CLASS(CTreeOptionsEditEx));

		/////////////////////////////////////////////////////////////////////////////
		// File related group
		//
		m_htiSparsePartFiles = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_SPARSEPARTFILES), TVI_ROOT, m_bSparsePartFiles);
		m_htiFullAlloc = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_FULLALLOC), TVI_ROOT, m_bFullAlloc);
		m_htiCheckDiskspace = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_CHECKDISKSPACE), TVI_ROOT, m_bCheckDiskspace);
		m_ctrlTreeOptions.SetCheckBoxEnable(m_htiCheckDiskspace, false);
		m_htiMinFreeDiskSpace = m_ctrlTreeOptions.InsertItem(GetResString(IDS_MINFREEDISKSPACE), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiCheckDiskspace);
		m_ctrlTreeOptions.AddEditBox(m_htiMinFreeDiskSpace, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiCommit = m_ctrlTreeOptions.InsertGroup(GetResString(IDS_COMMITFILES), iImgBackup, TVI_ROOT);
		m_htiCommitNever = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_NEVER), m_htiCommit, m_iCommitFiles == 0);
		m_htiCommitOnShutdown = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_ONSHUTDOWN), m_htiCommit, m_iCommitFiles == 1);
		m_htiCommitAlways = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_ALWAYS), m_htiCommit, m_iCommitFiles == 2);
		m_htiExtractMetaData = m_ctrlTreeOptions.InsertGroup(GetResString(IDS_EXTRACT_META_DATA), iImgMetaData, TVI_ROOT);
		m_htiExtractMetaDataNever = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_NEVER), m_htiExtractMetaData, m_iExtractMetaData == 0);
		m_htiExtractMetaDataID3Lib = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_META_DATA_ID3LIB), m_htiExtractMetaData, m_iExtractMetaData == 1);
		//m_htiExtractMetaDataMediaDet = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_META_DATA_MEDIADET), m_htiExtractMetaData, m_iExtractMetaData == 2);
		m_htiHiddenStartup = m_ctrlTreeOptions.InsertGroup(GetResString(IDS_HIDDENRUNTIME_STARTUP), iImgConnection, TVI_ROOT);
		m_htiRestoreLastMainWndDlg = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_RESTORELASTMAINWNDDLG), m_htiHiddenStartup, m_bRestoreLastMainWndDlg);
		m_htiRestoreLastLogPane = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_RESTORELASTLOGPANE), m_htiHiddenStartup, m_bRestoreLastLogPane);

		m_htiHiddenFile = m_ctrlTreeOptions.InsertGroup(GetResString(IDS_HIDDENRUNTIME_FILE), iImgBackup, TVI_ROOT);
		m_htiFileBufferTimeLimit = m_ctrlTreeOptions.InsertItem(GetResString(IDS_FILEBUFFERTIMELIMIT), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiHiddenFile);
		m_ctrlTreeOptions.AddEditBox(m_htiFileBufferTimeLimit, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiFileBufferSize = m_ctrlTreeOptions.InsertItem(GetFileBufferSizeLabel(), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiHiddenFile);
		m_ctrlTreeOptions.AddEditBox(m_htiFileBufferSize, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiQueueSize = m_ctrlTreeOptions.InsertItem(GetResString(IDS_QUEUESIZE), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiHiddenFile);
		m_ctrlTreeOptions.AddEditBox(m_htiQueueSize, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiDateTimeFormat4Lists = m_ctrlTreeOptions.InsertItem(GetResString(IDS_DATETIMEFORMAT4LISTS), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiHiddenFile);
		m_ctrlTreeOptions.AddEditBox(m_htiDateTimeFormat4Lists, RUNTIME_CLASS(CTreeOptionsEditEx));
		m_htiPreviewCopiedArchives = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_PREVIEWCOPIEDARCHIVES), m_htiHiddenFile, m_bPreviewCopiedArchives);
		m_htiInspectAllFileTypes = m_ctrlTreeOptions.InsertItem(GetResString(IDS_INSPECTALLFILETYPES), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiHiddenFile);
		m_ctrlTreeOptions.AddEditBox(m_htiInspectAllFileTypes, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiPreviewOnIconDblClk = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_PREVIEWONICONDBLCLK), m_htiHiddenFile, m_bPreviewOnIconDblClk);
		m_htiExtraPreviewWithMenu = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_EXTRAPREVIEWWITHMENU), m_htiHiddenFile, m_bExtraPreviewWithMenu);
		m_htiKeepUnavailableFixedSharedDirs = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_KEEPUNAVAILABLEFIXEDSHAREDDIRS), m_htiHiddenFile, m_bKeepUnavailableFixedSharedDirs);
		m_htiPartiallyPurgeOldKnownFiles = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_PARTIALLYPURGEOLDKNOWNFILES), m_htiHiddenFile, m_bPartiallyPurgeOldKnownFiles);
		m_htiAdjustNTFSDaylightFileTime = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_ADJUSTNTFSDAYLIGHTFILETIME), m_htiHiddenFile, m_bAdjustNTFSDaylightFileTime);

		m_htiHiddenDisplay = m_ctrlTreeOptions.InsertGroup(GetResString(IDS_HIDDENRUNTIME_DISPLAY), iImgLog, TVI_ROOT);
		m_htiShowActiveDownloadsBold = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_SHOWACTIVEDOWNLOADSBOLD), m_htiHiddenDisplay, m_bShowActiveDownloadsBold);
		m_htiUseSystemFontForMainControls = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_USESYSTEMFONTFORMAINCONTROLS), m_htiHiddenDisplay, m_bUseSystemFontForMainControls);
		m_htiReBarToolbar = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_REBARTOOLBAR), m_htiHiddenDisplay, m_bReBarToolbar);
		m_htiShowUpDownIconInTaskbar = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_SHOWUPDOWNICONINTASKBAR), m_htiHiddenDisplay, m_bShowUpDownIconInTaskbar);
		m_htiShowVerticalHourMarkers = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_SHOWVERTICALHOURMARKERS), m_htiHiddenDisplay, m_bShowVerticalHourMarkers);
		m_htiForceSpeedsToKB = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_FORCESPEEDSTOKB), m_htiHiddenDisplay, m_bForceSpeedsToKB);
		m_htiGeoLocationEnabled = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_ENABLE_GEOLOCATION), m_htiHiddenDisplay, m_bGeoLocationEnabled);
		m_htiGeoLocationCheckDays = m_ctrlTreeOptions.InsertItem(GetGeoLocationIntervalLabel(), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiGeoLocationEnabled);
		m_ctrlTreeOptions.AddEditBox(m_htiGeoLocationCheckDays, RUNTIME_CLASS(CNumTreeOptionsEdit));

		m_htiHiddenSecurity = m_ctrlTreeOptions.InsertGroup(GetResString(IDS_HIDDENRUNTIME_SECURITY), iImgConnection, TVI_ROOT);
		m_htiDetectTCPErrorFlooder = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_DETECT_TCP_ERROR_FLOODER), m_htiHiddenSecurity, m_bDetectTCPErrorFlooder);
		m_htiTCPErrorFlooderIntervalMinutes = m_ctrlTreeOptions.InsertItem(GetResString(IDS_TCP_ERROR_FLOODER_INTERVAL_MINUTES), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDetectTCPErrorFlooder);
		m_ctrlTreeOptions.AddEditBox(m_htiTCPErrorFlooderIntervalMinutes, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiTCPErrorFlooderThreshold = m_ctrlTreeOptions.InsertItem(GetResString(IDS_TCP_ERROR_FLOODER_THRESHOLD), TREEOPTSCTRLIMG_EDIT, TREEOPTSCTRLIMG_EDIT, m_htiDetectTCPErrorFlooder);
		m_ctrlTreeOptions.AddEditBox(m_htiTCPErrorFlooderThreshold, RUNTIME_CLASS(CNumTreeOptionsEdit));
		m_htiRearrangeKadSearchKeywords = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_REARRANGEKADSEARCHKEYWORDS), m_htiHiddenSecurity, m_bRearrangeKadSearchKeywords);
		m_htiMessageFromValidSourcesOnly = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_MESSAGEFROMVALIDSOURCESONLY), m_htiHiddenSecurity, m_bMessageFromValidSourcesOnly);

		/////////////////////////////////////////////////////////////////////////////
		// Logging group
		//
		m_htiLog2Disk = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_LOG2DISK), TVI_ROOT, m_bLog2Disk);
		m_htiPerfLog = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_ENABLE_PERFLOG), TVI_ROOT, m_bPerfLogEnabled);
		if (thePrefs.GetEnableVerboseOptions()) {
			m_htiVerboseGroup = m_ctrlTreeOptions.InsertGroup(GetResString(IDS_VERBOSE), iImgLog, TVI_ROOT);
			m_htiVerbose = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_ENABLED), m_htiVerboseGroup, m_bVerbose);
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
		m_htiSkipWANIPSetup = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_UPNPSKIPWANIP), m_htiUPnP, m_bSkipWANIPSetup);
		m_htiSkipWANPPPSetup = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_UPNPSKIPWANPPP), m_htiUPnP, m_bSkipWANPPPSetup);

		/////////////////////////////////////////////////////////////////////////////
		// eMule Shared Use
		//
		m_htiShareeMule = m_ctrlTreeOptions.InsertGroup(GetResString(IDS_SHAREEMULELABEL), iImgShareeMule, TVI_ROOT);
		m_htiShareeMuleMultiUser = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_SHAREEMULEMULTI), m_htiShareeMule, m_iShareeMule == 0);
		m_htiShareeMulePublicUser = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_SHAREEMULEPUBLIC), m_htiShareeMule, m_iShareeMule == 1);
		m_htiShareeMuleOldStyle = m_ctrlTreeOptions.InsertRadioButton(GetResString(IDS_SHAREEMULEOLD), m_htiShareeMule, m_iShareeMule == 2);

		m_htiImportParts = m_ctrlTreeOptions.InsertCheckBox(GetResString(IDS_ENABLEIMPORTPARTS), TVI_ROOT, m_bImportParts);

		m_ctrlTreeOptions.Expand(m_htiTCPGroup, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiSearchGroup, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiSearchEd2kGroup, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiSearchKadGroup, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiBroadband, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiBBLowRatioBoost, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiBBSessionTransfer, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiHiddenStartup, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiHiddenFile, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiHiddenDisplay, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiGeoLocationEnabled, m_bGeoLocationEnabled ? TVE_EXPAND : TVE_COLLAPSE);
		m_ctrlTreeOptions.Expand(m_htiHiddenSecurity, TVE_EXPAND);
		if (m_htiVerboseGroup)
			m_ctrlTreeOptions.Expand(m_htiVerboseGroup, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiCommit, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiCheckDiskspace, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiExtractMetaData, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiUPnP, TVE_EXPAND);
		m_ctrlTreeOptions.Expand(m_htiShareeMule, TVE_EXPAND);
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
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiBBMaxUploadClients, m_iBBMaxUploadClients);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiBBSlowThreshold, m_sBBSlowThresholdFactor);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiBBSlowGrace, m_iBBSlowGraceSeconds);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiBBSlowWarmup, m_iBBSlowWarmupSeconds);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiBBZeroRateGrace, m_iBBZeroRateGraceSeconds);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiBBCooldown, m_iBBCooldownSeconds);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiBBLowRatioBoost, m_bBBLowRatioBoost);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiBBLowRatioThreshold, m_sBBLowRatioThreshold);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiBBLowRatioBonus, m_iBBLowRatioBonus);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiBBLowIdDivisor, m_iBBLowIdDivisor);
	DDX_TreeRadio(pDX, IDC_EXT_OPTS, m_htiBBSessionTransfer, m_iBBSessionTransferMode);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiBBSessionTransferPercentValue, m_iBBSessionTransferPercent);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiBBSessionTransferMiBValue, m_iBBSessionTransferMiB);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiBBSessionTimeLimit, m_iBBSessionTimeLimitSeconds);
	if (pDX->m_bSaveAndValidate) {
		float fParsedValue = 0.0f;
		if (m_iBBMaxUploadClients < 1 || m_iBBMaxUploadClients > 32)
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiBBMaxUploadClients);
		if (!TryParseTreeFloat(m_sBBSlowThresholdFactor, fParsedValue) || fParsedValue < 0.10f || fParsedValue > 1.0f)
			FailTreeValidation(pDX, AFX_IDP_PARSE_REAL, m_htiBBSlowThreshold);
		if (m_iBBSlowGraceSeconds < 5 || m_iBBSlowGraceSeconds > 300)
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiBBSlowGrace);
		if (m_iBBSlowWarmupSeconds < 0 || m_iBBSlowWarmupSeconds > 3600)
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiBBSlowWarmup);
		if (m_iBBZeroRateGraceSeconds < 3 || m_iBBZeroRateGraceSeconds > 120)
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiBBZeroRateGrace);
		if (m_iBBCooldownSeconds < 10 || m_iBBCooldownSeconds > 3600)
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiBBCooldown);
		if (!TryParseTreeFloat(m_sBBLowRatioThreshold, fParsedValue) || fParsedValue < 0.0f || fParsedValue > 2.0f)
			FailTreeValidation(pDX, AFX_IDP_PARSE_REAL, m_htiBBLowRatioThreshold);
		if (m_iBBLowRatioBonus < 0 || m_iBBLowRatioBonus > 500)
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiBBLowRatioBonus);
		if (m_iBBLowIdDivisor < 1 || m_iBBLowIdDivisor > 8)
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiBBLowIdDivisor);
		if (m_iBBSessionTransferMode == BBSTM_PERCENT_OF_FILE
			&& (m_iBBSessionTransferPercent < 1 || m_iBBSessionTransferPercent > 100))
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiBBSessionTransferPercentValue);
		if (m_iBBSessionTransferMode == BBSTM_ABSOLUTE_MIB
			&& (m_iBBSessionTransferMiB < 1 || m_iBBSessionTransferMiB > 4096))
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiBBSessionTransferMiBValue);
		if (m_iBBSessionTimeLimitSeconds < 0 || m_iBBSessionTimeLimitSeconds > 86400)
			FailTreeValidation(pDX, AFX_IDP_PARSE_INT, m_htiBBSessionTimeLimit);
	}

	/////////////////////////////////////////////////////////////////////////////
	// Miscellaneous group
	//
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiAutoTakeEd2kLinks, m_bAutoTakeEd2kLinks);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiCreditSystem, m_bCreditSystem);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiFilterLANIPs, m_bFilterLANIPs);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiExtControls, m_bExtControls);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiA4AFSaveCpu, m_bA4AFSaveCpu);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiYourHostname, m_sYourHostname);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiAutoArch, m_bAutoArchDisable);

	/////////////////////////////////////////////////////////////////////////////
	// File related group
	//
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiSparsePartFiles, m_bSparsePartFiles);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiImportParts, m_bImportParts);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiFullAlloc, m_bFullAlloc);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiCheckDiskspace, m_bCheckDiskspace);
	DDX_Text(pDX, IDC_EXT_OPTS, m_htiMinFreeDiskSpace, m_iMinFreeDiskSpaceGB);
	DDV_MinMaxInt(pDX, m_iMinFreeDiskSpaceGB, static_cast<int>(PartFilePersistenceSeams::kMinDiskSpaceFloorGiB), static_cast<int>(PartFilePersistenceSeams::kMaxDiskSpaceFloorGiB));
	DDX_TreeRadio(pDX, IDC_EXT_OPTS, m_htiCommit, m_iCommitFiles);
	DDX_TreeRadio(pDX, IDC_EXT_OPTS, m_htiExtractMetaData, m_iExtractMetaData);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiRestoreLastMainWndDlg, m_bRestoreLastMainWndDlg);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiRestoreLastLogPane, m_bRestoreLastLogPane);
	DDX_Text(pDX, IDC_EXT_OPTS, m_htiFileBufferTimeLimit, m_uFileBufferTimeLimitSeconds);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiDateTimeFormat4Lists, m_sDateTimeFormat4Lists);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiPreviewCopiedArchives, m_bPreviewCopiedArchives);
	DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiInspectAllFileTypes, m_iInspectAllFileTypes);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiPreviewOnIconDblClk, m_bPreviewOnIconDblClk);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiExtraPreviewWithMenu, m_bExtraPreviewWithMenu);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiKeepUnavailableFixedSharedDirs, m_bKeepUnavailableFixedSharedDirs);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiPartiallyPurgeOldKnownFiles, m_bPartiallyPurgeOldKnownFiles);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiAdjustNTFSDaylightFileTime, m_bAdjustNTFSDaylightFileTime);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiShowActiveDownloadsBold, m_bShowActiveDownloadsBold);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiUseSystemFontForMainControls, m_bUseSystemFontForMainControls);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiReBarToolbar, m_bReBarToolbar);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiShowUpDownIconInTaskbar, m_bShowUpDownIconInTaskbar);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiShowVerticalHourMarkers, m_bShowVerticalHourMarkers);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiForceSpeedsToKB, m_bForceSpeedsToKB);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiGeoLocationEnabled, m_bGeoLocationEnabled);
	ExchangeTreeUInt(pDX, m_ctrlTreeOptions, m_htiGeoLocationCheckDays, m_uGeoLocationCheckDays);
	if (pDX->m_bSaveAndValidate) {
		if (m_uGeoLocationCheckDays != 0
			&& (m_uGeoLocationCheckDays < thePrefs.GetMinGeoLocationCheckDays() || m_uGeoLocationCheckDays > thePrefs.GetMaxGeoLocationCheckDays()))
		{
			CString detail;
			detail.Format(_T("Expected value: 0, or %u..%u."), thePrefs.GetMinGeoLocationCheckDays(), thePrefs.GetMaxGeoLocationCheckDays());
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
	ExchangeTreeUInt(pDX, m_ctrlTreeOptions, m_htiFileBufferSize, m_uFileBufferSizeKiB);
	if (pDX->m_bSaveAndValidate) {
		const UINT uMinFileBufferSizeKiB = thePrefs.GetMinFileBufferSizeBytes() / 1024u;
		const UINT uMaxFileBufferSizeKiB = thePrefs.GetMaxFileBufferSizeBytes() / 1024u;
		if (m_uFileBufferSizeKiB < uMinFileBufferSizeKiB || m_uFileBufferSizeKiB > uMaxFileBufferSizeKiB) {
			CString detail;
			detail.Format(_T("Expected range: %u..%u KiB."), uMinFileBufferSizeKiB, uMaxFileBufferSizeKiB);
			FailTreeValidation(pDX, m_ctrlTreeOptions, m_htiFileBufferSize, detail);
		}
	}
	ExchangeTreeInt(pDX, m_ctrlTreeOptions, m_htiQueueSize, m_iQueueSize);
	if (pDX->m_bSaveAndValidate) {
		if (m_iQueueSize < static_cast<int>(thePrefs.GetMinQueueSize()) || m_iQueueSize > static_cast<int>(thePrefs.GetMaxQueueSize())) {
			CString detail;
			detail.Format(_T("Expected range: %d..%d."), static_cast<int>(thePrefs.GetMinQueueSize()), static_cast<int>(thePrefs.GetMaxQueueSize()));
			FailTreeValidation(pDX, m_ctrlTreeOptions, m_htiQueueSize, detail);
		}
	}

	/////////////////////////////////////////////////////////////////////////////
	// Logging group
	//
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiLog2Disk, m_bLog2Disk);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiPerfLog, m_bPerfLogEnabled);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiAutoBrowseRemoteShares, m_bAutoBrowseRemoteShares);
	if (m_htiLogLevel) {
		DDX_TreeEdit(pDX, IDC_EXT_OPTS, m_htiLogLevel, m_iLogLevel);
		DDV_MinMaxInt(pDX, m_iLogLevel, 1, 5);
	}
	if (m_htiVerbose)
		DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiVerbose, m_bVerbose);
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
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiSkipWANIPSetup, m_bSkipWANIPSetup);
	DDX_TreeCheck(pDX, IDC_EXT_OPTS, m_htiSkipWANPPPSetup, m_bSkipWANPPPSetup);

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
	m_bAutoBrowseRemoteShares = thePrefs.GetAutoBrowseRemoteShares();
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
		m_iLogLevel = 5 - thePrefs.m_byLogLevel;
	}
	m_bLog2Disk = thePrefs.log2disk;
	m_bPerfLogEnabled = thePerfLog.IsEnabled();
	m_bCreditSystem = thePrefs.m_bCreditSystem;
	m_iCommitFiles = thePrefs.m_iCommitFiles;
	m_iExtractMetaData = thePrefs.m_iExtractMetaData;
	m_bFilterLANIPs = thePrefs.filterLANIPs;
	m_bExtControls = thePrefs.m_bExtControls;
	m_uServerKeepAliveTimeout = thePrefs.m_dwServerKeepAliveTimeout / MIN2MS(1);
	m_bSparsePartFiles = thePrefs.GetSparsePartFiles();
	m_bImportParts = thePrefs.m_bImportParts;
	m_bFullAlloc = thePrefs.m_bAllocFull;
	m_bCheckDiskspace = true;
	m_iMinFreeDiskSpaceGB = static_cast<int>(PartFilePersistenceSeams::ConvertDownloadFreeSpaceFloorBytesToDisplayGiB(thePrefs.GetMinFreeDiskSpace()));
	m_sYourHostname = thePrefs.GetYourHostname();
	m_bAutoArchDisable = !thePrefs.m_bAutomaticArcPreviewStart;

	m_bCloseUPnPOnExit = thePrefs.CloseUPnPOnExit();
	m_bSkipWANIPSetup = thePrefs.GetSkipWANIPSetup();
	m_bSkipWANPPPSetup = thePrefs.GetSkipWANPPPSetup();

	m_iShareeMule = thePrefs.m_nCurrentUserDirMode;

	m_bA4AFSaveCpu = thePrefs.GetA4AFSaveCpu();
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
	m_iBBMaxUploadClients = static_cast<int>(thePrefs.GetBBMaxUploadClientsAllowed());
	m_sBBSlowThresholdFactor.Format(_T("%.2f"), thePrefs.GetBBSlowUploadThresholdFactor());
	m_iBBSlowGraceSeconds = static_cast<int>(thePrefs.GetBBSlowUploadGraceSeconds());
	m_iBBSlowWarmupSeconds = static_cast<int>(thePrefs.GetBBSlowUploadWarmupSeconds());
	m_iBBZeroRateGraceSeconds = static_cast<int>(thePrefs.GetBBZeroRateGraceSeconds());
	m_iBBCooldownSeconds = static_cast<int>(thePrefs.GetBBSlowUploadCooldownSeconds());
	m_bBBLowRatioBoost = thePrefs.IsBBLowRatioBoostEnabled();
	m_sBBLowRatioThreshold.Format(_T("%.2f"), thePrefs.GetBBLowRatioThreshold());
	m_iBBLowRatioBonus = static_cast<int>(thePrefs.GetBBLowRatioBonus());
	m_iBBLowIdDivisor = static_cast<int>(thePrefs.GetBBLowIDDivisor());
	m_iBBSessionTransferMode = thePrefs.GetBBSessionTransferMode();
	m_iBBSessionTransferPercent = static_cast<int>(thePrefs.GetBBSessionTransferMode() == BBSTM_PERCENT_OF_FILE ? thePrefs.GetBBSessionTransferValue() : 55);
	m_iBBSessionTransferMiB = static_cast<int>(thePrefs.GetBBSessionTransferMode() == BBSTM_ABSOLUTE_MIB ? thePrefs.GetBBSessionTransferValue() : 0);
	m_iBBSessionTimeLimitSeconds = static_cast<int>(thePrefs.GetBBSessionTimeLimitSeconds());
	m_uFileBufferTimeLimitSeconds = max(1u, thePrefs.GetFileBufferTimeLimit() / SEC2MS(1));
	m_sDateTimeFormat4Lists = thePrefs.GetDateTimeFormat4Lists();
	m_bPreviewCopiedArchives = thePrefs.GetPreviewCopiedArchives();
	m_iInspectAllFileTypes = thePrefs.GetInspectAllFileTypes();
	m_bPreviewOnIconDblClk = thePrefs.GetPreviewOnIconDblClk();
	m_bShowActiveDownloadsBold = thePrefs.GetShowActiveDownloadsBold();
	m_bUseSystemFontForMainControls = thePrefs.GetUseSystemFontForMainControls();
	m_bReBarToolbar = thePrefs.GetReBarToolbar();
	m_bShowUpDownIconInTaskbar = thePrefs.IsShowUpDownIconInTaskbar();
	m_bShowVerticalHourMarkers = thePrefs.m_bShowVerticalHourMarkers;
	m_bForceSpeedsToKB = thePrefs.GetForceSpeedsToKB();
	m_bGeoLocationEnabled = thePrefs.IsGeoLocationEnabled();
	m_uGeoLocationCheckDays = thePrefs.NormalizeGeoLocationCheckDays(thePrefs.GetGeoLocationCheckDays());
	m_bExtraPreviewWithMenu = thePrefs.GetExtraPreviewWithMenu();
	m_bKeepUnavailableFixedSharedDirs = thePrefs.m_bKeepUnavailableFixedSharedDirs;
	m_bPartiallyPurgeOldKnownFiles = thePrefs.DoPartiallyPurgeOldKnownFiles();
	m_bAdjustNTFSDaylightFileTime = thePrefs.GetAdjustNTFSDaylightFileTime();
	m_bRearrangeKadSearchKeywords = thePrefs.GetRearrangeKadSearchKeywords();
	m_bMessageFromValidSourcesOnly = thePrefs.MsgOnlySecure();
	m_iQueueSize = static_cast<int>(thePrefs.GetQueueSize());
	m_uFileBufferSizeKiB = thePrefs.GetFileBufferSize() / 1024u;

	m_ctrlTreeOptions.SetImageListColorFlags(theApp.m_iDfltImageListColorFlags);
	CPropertyPage::OnInitDialog();
	InitWindowStyles(this);
	m_ctrlTreeOptions.SetItemHeight(m_ctrlTreeOptions.GetItemHeight() + 2);

	CaptureBaseLayout();
	Localize();

	return TRUE;  // return TRUE unless you set the focus to the control
				  // EXCEPTION: OCX Property Pages should return FALSE
}

void CPPgTweaks::CaptureBaseLayout()
{
	CRect rectClient;
	GetClientRect(&rectClient);
	m_szBaseClient = rectClient.Size();

	static const UINT aControlIds[] = {
		IDC_WARNING,
		IDC_EXT_OPTS
	};
	CRect *apRects[] = {
		&m_rcWarning,
		&m_rcTree
	};
	for (int i = 0; i < _countof(aControlIds); ++i) {
		CWnd *pWnd = GetDlgItem(aControlIds[i]);
		if (pWnd != NULL) {
			pWnd->GetWindowRect(apRects[i]);
			ScreenToClient(apRects[i]);
		} else
			apRects[i]->SetRectEmpty();
	}
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
	thePrefs.SetBBMaxUploadClientsAllowed(static_cast<UINT>(max(1, m_iBBMaxUploadClients)));
	thePrefs.SetBBSlowUploadThresholdFactor(static_cast<float>(_tstof(m_sBBSlowThresholdFactor)));
	thePrefs.SetBBSlowUploadGraceSeconds(static_cast<UINT>(max(1, m_iBBSlowGraceSeconds)));
	thePrefs.SetBBSlowUploadWarmupSeconds(static_cast<UINT>(max(0, m_iBBSlowWarmupSeconds)));
	thePrefs.SetBBZeroRateGraceSeconds(static_cast<UINT>(max(1, m_iBBZeroRateGraceSeconds)));
	thePrefs.SetBBSlowUploadCooldownSeconds(static_cast<UINT>(max(1, m_iBBCooldownSeconds)));
	thePrefs.SetBBLowRatioBoostEnabled(m_bBBLowRatioBoost);
	thePrefs.SetBBLowRatioThreshold(static_cast<float>(_tstof(m_sBBLowRatioThreshold)));
	thePrefs.SetBBLowRatioBonus(static_cast<UINT>(max(0, m_iBBLowRatioBonus)));
	thePrefs.SetBBLowIDDivisor(static_cast<UINT>(max(1, m_iBBLowIdDivisor)));
	thePrefs.SetBBSessionTransferMode((EBBSessionTransferMode)m_iBBSessionTransferMode);
	if (m_iBBSessionTransferMode == BBSTM_ABSOLUTE_MIB)
		thePrefs.SetBBSessionTransferValue(static_cast<UINT>(max(1, m_iBBSessionTransferMiB)));
	else
		thePrefs.SetBBSessionTransferValue(static_cast<UINT>(max(1, m_iBBSessionTransferPercent)));
	thePrefs.SetBBSessionTimeLimitSeconds(static_cast<UINT>(max(0, m_iBBSessionTimeLimitSeconds)));

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
	thePrefs.SetAutoBrowseRemoteShares(m_bAutoBrowseRemoteShares);

	if (!thePrefs.log2disk && m_bLog2Disk)
		theLog.Open();
	else if (thePrefs.log2disk && !m_bLog2Disk)
		theLog.Close();
	thePrefs.log2disk = m_bLog2Disk;

	if (thePerfLog.IsEnabled() != m_bPerfLogEnabled)
		thePerfLog.SetEnabled(m_bPerfLogEnabled);

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
		thePrefs.m_byLogLevel = 5 - m_iLogLevel;

		thePrefs.m_bVerbose = m_bVerbose; // store after related options were stored!
	}

	thePrefs.m_bCreditSystem = m_bCreditSystem;
	thePrefs.m_iCommitFiles = m_iCommitFiles;
	thePrefs.m_iExtractMetaData = m_iExtractMetaData;
	thePrefs.filterLANIPs = m_bFilterLANIPs;
	thePrefs.SetFileBufferSize(m_uFileBufferSizeKiB * 1024u);
	thePrefs.SetQueueSize(m_iQueueSize);
	m_uFileBufferSizeKiB = thePrefs.GetFileBufferSize() / 1024u;
	m_iQueueSize = static_cast<int>(thePrefs.GetQueueSize());

	bool bUpdateDLmenu = (thePrefs.m_bImportParts != m_bImportParts);
	thePrefs.m_bImportParts = m_bImportParts;
	if (thePrefs.m_bExtControls != m_bExtControls) {
		bUpdateDLmenu = true;
		thePrefs.m_bExtControls = m_bExtControls;
		theApp.emuledlg->searchwnd->CreateMenus();
		theApp.emuledlg->sharedfileswnd->sharedfilesctrl.CreateMenus();
	}
	if (bUpdateDLmenu)
		theApp.emuledlg->transferwnd->GetDownloadList()->CreateMenus();

	thePrefs.m_dwServerKeepAliveTimeout = MIN2MS(m_uServerKeepAliveTimeout);
	thePrefs.m_bSparsePartFiles = m_bSparsePartFiles;
	thePrefs.m_bAllocFull = m_bFullAlloc;
	thePrefs.checkDiskspace = true;
	thePrefs.m_uMinFreeDiskSpace = thePrefs.NormalizeMinFreeDiskSpace(PartFilePersistenceSeams::ConvertDiskSpaceFloorGiBToBytes(thePrefs.NormalizeMinFreeDiskSpaceGiB(static_cast<UINT>(m_iMinFreeDiskSpaceGB))));
	if (thePrefs.GetYourHostname() != m_sYourHostname) {
		thePrefs.SetYourHostname(m_sYourHostname);
		theApp.emuledlg->serverwnd->UpdateMyInfo();
	}
	thePrefs.m_bAutomaticArcPreviewStart = !m_bAutoArchDisable;

	thePrefs.m_bCloseUPnPOnExit = m_bCloseUPnPOnExit;
	thePrefs.SetSkipWANIPSetup(m_bSkipWANIPSetup);
	thePrefs.SetSkipWANPPPSetup(m_bSkipWANPPPSetup);

	thePrefs.ChangeUserDirMode(m_iShareeMule);

	thePrefs.m_bA4AFSaveCpu = m_bA4AFSaveCpu;
	thePrefs.m_bRestoreLastMainWndDlg = m_bRestoreLastMainWndDlg;
	thePrefs.m_bRestoreLastLogPane = m_bRestoreLastLogPane;
	thePrefs.m_uFileBufferTimeLimit = SEC2MS(max(1u, m_uFileBufferTimeLimitSeconds));
	thePrefs.m_strDateTimeFormat4Lists = m_sDateTimeFormat4Lists;
	thePrefs.m_bPreviewCopiedArchives = m_bPreviewCopiedArchives;
	thePrefs.m_iInspectAllFileTypes = max(0, m_iInspectAllFileTypes);
	thePrefs.m_bPreviewOnIconDblClk = m_bPreviewOnIconDblClk;
	thePrefs.m_bShowActiveDownloadsBold = m_bShowActiveDownloadsBold;
	thePrefs.m_bUseSystemFontForMainControls = m_bUseSystemFontForMainControls;
	thePrefs.m_bReBarToolbar = m_bReBarToolbar;
	thePrefs.m_bShowUpDownIconInTaskbar = m_bShowUpDownIconInTaskbar;
	thePrefs.m_bShowVerticalHourMarkers = m_bShowVerticalHourMarkers;
	thePrefs.m_bForceSpeedsToKB = m_bForceSpeedsToKB;
	thePrefs.m_bExtraPreviewWithMenu = m_bExtraPreviewWithMenu;
	thePrefs.m_bKeepUnavailableFixedSharedDirs = m_bKeepUnavailableFixedSharedDirs;
	thePrefs.m_bPartiallyPurgeOldKnownFiles = m_bPartiallyPurgeOldKnownFiles;
	thePrefs.m_bAdjustNTFSDaylightFileTime = m_bAdjustNTFSDaylightFileTime;
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
		LocalizeEditLabel(m_htiMinFreeDiskSpace, IDS_MINFREEDISKSPACE);
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
		LocalizeEditLabel(m_htiBBMaxUploadClients, IDS_BB_MAX_UPLOAD_CLIENTS);
		LocalizeEditLabel(m_htiBBSlowThreshold, IDS_BB_SLOW_THRESHOLD_FACTOR);
		LocalizeEditLabel(m_htiBBSlowGrace, IDS_BB_SLOW_GRACE_SECONDS);
		LocalizeEditLabel(m_htiBBSlowWarmup, IDS_BB_SLOW_WARMUP_SECONDS);
		LocalizeEditLabel(m_htiBBZeroRateGrace, IDS_BB_ZERO_RATE_GRACE_SECONDS);
		LocalizeEditLabel(m_htiBBCooldown, IDS_BB_COOLDOWN_SECONDS);
		LocalizeItemText(m_htiBBLowRatioBoost, IDS_BB_LOW_RATIO_BOOST);
		LocalizeEditLabel(m_htiBBLowRatioThreshold, IDS_BB_RATIO_THRESHOLD);
		LocalizeEditLabel(m_htiBBLowRatioBonus, IDS_BB_SCORE_BONUS);
		LocalizeEditLabel(m_htiBBLowIdDivisor, IDS_BB_LOWID_DIVISOR);
		LocalizeItemText(m_htiBBSessionTransfer, IDS_BB_SESSION_TRANSFER_LIMIT);
		LocalizeItemText(m_htiBBSessionTransferDisabled, IDS_DISABLED);
		LocalizeItemText(m_htiBBSessionTransferPercent, IDS_BB_PERCENT_OF_FILE_SIZE);
		LocalizeItemText(m_htiBBSessionTransferMiB, IDS_BB_ABSOLUTE_MIB);
		LocalizeEditLabel(m_htiBBSessionTransferPercentValue, IDS_BB_SESSION_TRANSFER_PERCENT_VALUE);
		LocalizeEditLabel(m_htiBBSessionTransferMiBValue, IDS_BB_SESSION_TRANSFER_MIB_VALUE);
		LocalizeEditLabel(m_htiBBSessionTimeLimit, IDS_BB_SESSION_TIME_LIMIT);
		LocalizeEditLabel(m_htiYourHostname, IDS_YOURHOSTNAME);	// itsonlyme: hostnameSource
		LocalizeItemText(m_htiA4AFSaveCpu, IDS_A4AF_SAVE_CPU);
		LocalizeItemText(m_htiAutoArch, IDS_DISABLE_AUTOARCHPREV);
		LocalizeItemText(m_htiAutoBrowseRemoteShares, IDS_AUTOBROWSEREMOTESHARES);
		LocalizeItemText(m_htiAutoTakeEd2kLinks, IDS_AUTOTAKEED2KLINKS);
		LocalizeItemText(m_htiCheckDiskspace, IDS_CHECKDISKSPACE);
		LocalizeItemText(m_htiCloseUPnPPorts, IDS_UPNPCLOSEONEXIT);
		LocalizeItemText(m_htiCommit, IDS_COMMITFILES);
		LocalizeItemText(m_htiCommitAlways, IDS_ALWAYS);
		LocalizeItemText(m_htiCommitNever, IDS_NEVER);
		LocalizeItemText(m_htiCommitOnShutdown, IDS_ONSHUTDOWN);
		LocalizeItemText(m_htiConditionalTCPAccept, IDS_CONDTCPACCEPT);
		LocalizeItemText(m_htiCreditSystem, IDS_USECREDITSYSTEM);
		LocalizeItemText(m_htiDebug2Disk, IDS_LOG2DISK);
		LocalizeItemText(m_htiDebugSourceExchange, IDS_DEBUG_SOURCE_EXCHANGE);
		LocalizeItemText(m_htiExtControls, IDS_SHOWEXTSETTINGS);
		LocalizeItemText(m_htiExtraPreviewWithMenu, IDS_EXTRAPREVIEWWITHMENU);
		LocalizeItemText(m_htiExtractMetaData, IDS_EXTRACT_META_DATA);
		LocalizeItemText(m_htiExtractMetaDataID3Lib, IDS_META_DATA_ID3LIB);
		//LocalizeItemText(m_htiExtractMetaDataMediaDet, IDS_META_DATA_MEDIADET);
		LocalizeItemText(m_htiExtractMetaDataNever, IDS_NEVER);
		LocalizeItemText(m_htiFilterLANIPs, IDS_PW_FILTER);
		LocalizeItemText(m_htiForceSpeedsToKB, IDS_FORCESPEEDSTOKB);
		LocalizeItemText(m_htiFullAlloc, IDS_FULLALLOC);
		LocalizeItemText(m_htiHiddenDisplay, IDS_HIDDENRUNTIME_DISPLAY);
		LocalizeItemText(m_htiHiddenFile, IDS_HIDDENRUNTIME_FILE);
		LocalizeItemText(m_htiHiddenSecurity, IDS_HIDDENRUNTIME_SECURITY);
		LocalizeItemText(m_htiDetectTCPErrorFlooder, IDS_DETECT_TCP_ERROR_FLOODER);
		LocalizeEditLabel(m_htiTCPErrorFlooderIntervalMinutes, IDS_TCP_ERROR_FLOODER_INTERVAL_MINUTES);
		LocalizeEditLabel(m_htiTCPErrorFlooderThreshold, IDS_TCP_ERROR_FLOODER_THRESHOLD);
		LocalizeItemText(m_htiHiddenStartup, IDS_HIDDENRUNTIME_STARTUP);
		LocalizeItemText(m_htiImportParts, IDS_ENABLEIMPORTPARTS);
		LocalizeItemText(m_htiKeepUnavailableFixedSharedDirs, IDS_KEEPUNAVAILABLEFIXEDSHAREDDIRS);
		LocalizeItemText(m_htiLog2Disk, IDS_LOG2DISK);
		LocalizeItemText(m_htiPerfLog, IDS_ENABLE_PERFLOG);
		LocalizeItemText(m_htiLogA4AF, IDS_LOG_A4AF);
		LocalizeItemText(m_htiLogBannedClients, IDS_LOG_BANNED_CLIENTS);
		LocalizeItemText(m_htiLogFileSaving, IDS_LOG_FILE_SAVING);
		LocalizeItemText(m_htiLogFilteredIPs, IDS_LOG_FILTERED_IPS);
		LocalizeItemText(m_htiLogRatingDescReceived, IDS_LOG_RATING_RECV);
		LocalizeItemText(m_htiLogSecureIdent, IDS_LOG_SECURE_IDENT);
		LocalizeItemText(m_htiLogUlDlEvents, IDS_LOG_ULDL_EVENTS);
		LocalizeItemText(m_htiMessageFromValidSourcesOnly, IDS_MESSAGEFROMVALIDSOURCESONLY);
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
		LocalizeItemText(m_htiShowUpDownIconInTaskbar, IDS_SHOWUPDOWNICONINTASKBAR);
		LocalizeItemText(m_htiShowVerticalHourMarkers, IDS_SHOWVERTICALHOURMARKERS);
		LocalizeItemText(m_htiGeoLocationEnabled, IDS_ENABLE_GEOLOCATION);
		LocalizeItemText(m_htiSkipWANIPSetup, IDS_UPNPSKIPWANIP);
		LocalizeItemText(m_htiSkipWANPPPSetup, IDS_UPNPSKIPWANPPP);
		LocalizeItemText(m_htiSparsePartFiles, IDS_SPARSEPARTFILES);
		LocalizeItemText(m_htiTCPGroup, IDS_TCPIP_CONNS);
		LocalizeItemText(m_htiUPnP, IDS_UPNP);
		LocalizeItemText(m_htiUseSystemFontForMainControls, IDS_USESYSTEMFONTFORMAINCONTROLS);
		LocalizeItemText(m_htiVerbose, IDS_ENABLED);
		LocalizeItemText(m_htiVerboseGroup, IDS_VERBOSE);
		LocalizeItemText(m_htiAdjustNTFSDaylightFileTime, IDS_ADJUSTNTFSDAYLIGHTFILETIME);
		LocalizeEditLabel(m_htiDateTimeFormat4Lists, IDS_DATETIMEFORMAT4LISTS);
		LocalizeEditLabel(m_htiFileBufferTimeLimit, IDS_FILEBUFFERTIMELIMIT);
		m_ctrlTreeOptions.SetEditLabel(m_htiFileBufferSize, GetFileBufferSizeLabel());
		LocalizeEditLabel(m_htiQueueSize, IDS_QUEUESIZE);
		m_ctrlTreeOptions.SetEditLabel(m_htiGeoLocationCheckDays, GetGeoLocationIntervalLabel());
		LocalizeEditLabel(m_htiInspectAllFileTypes, IDS_INSPECTALLFILETYPES);
	}
}

void CPPgTweaks::OnDestroy()
{
	m_ctrlTreeOptions.DeleteAllItems();
	m_ctrlTreeOptions.DestroyWindow();
	m_bInitializedTreeOpts = false;
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
	m_htiBBMaxUploadClients = NULL;
	m_htiBBSlowThreshold = NULL;
	m_htiBBSlowGrace = NULL;
	m_htiBBSlowWarmup = NULL;
	m_htiBBZeroRateGrace = NULL;
	m_htiBBCooldown = NULL;
	m_htiBBLowRatioBoost = NULL;
	m_htiBBLowRatioThreshold = NULL;
	m_htiBBLowRatioBonus = NULL;
	m_htiBBLowIdDivisor = NULL;
	m_htiBBSessionTransfer = NULL;
	m_htiBBSessionTransferDisabled = NULL;
	m_htiBBSessionTransferPercent = NULL;
	m_htiBBSessionTransferMiB = NULL;
	m_htiBBSessionTransferPercentValue = NULL;
	m_htiBBSessionTransferMiBValue = NULL;
	m_htiBBSessionTimeLimit = NULL;
	m_htiMaxCon5Sec = NULL;
	m_htiMaxHalfOpen = NULL;
	m_htiConditionalTCPAccept = NULL;
	m_htiAutoTakeEd2kLinks = NULL;
	m_htiVerboseGroup = NULL;
	m_htiVerbose = NULL;
	m_htiDebugSourceExchange = NULL;
	m_htiHiddenDisplay = NULL;
	m_htiHiddenFile = NULL;
	m_htiHiddenSecurity = NULL;
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
	m_htiCreditSystem = NULL;
	m_htiDateTimeFormat4Lists = NULL;
	m_htiPreviewCopiedArchives = NULL;
	m_htiPreviewOnIconDblClk = NULL;
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
	m_htiAdjustNTFSDaylightFileTime = NULL;
	m_htiRearrangeKadSearchKeywords = NULL;
	m_htiMessageFromValidSourcesOnly = NULL;
	m_htiFileBufferTimeLimit = NULL;
	m_htiFileBufferSize = NULL;
	m_htiQueueSize = NULL;
	m_htiDownloadTimeout = NULL;
	m_htiRestoreLastLogPane = NULL;
	m_htiRestoreLastMainWndDlg = NULL;
	m_htiInspectAllFileTypes = NULL;
	m_htiLog2Disk = NULL;
	m_htiPerfLog = NULL;
	m_htiDebug2Disk = NULL;
	m_htiCommit = NULL;
	m_htiCommitNever = NULL;
	m_htiCommitOnShutdown = NULL;
	m_htiCommitAlways = NULL;
	m_htiFilterLANIPs = NULL;
	m_htiExtControls = NULL;
	m_htiServerKeepAliveTimeout = NULL;
	m_htiSparsePartFiles = NULL;
	m_htiImportParts = NULL;
	m_htiFullAlloc = NULL;
	m_htiCheckDiskspace = NULL;
	m_htiMinFreeDiskSpace = NULL;
	m_htiYourHostname = NULL;
	m_htiA4AFSaveCpu = NULL;
	m_htiExtractMetaData = NULL;
	m_htiExtractMetaDataNever = NULL;
	m_htiExtractMetaDataID3Lib = NULL;
	m_htiAutoArch = NULL;
	m_htiAutoBrowseRemoteShares = NULL;
	m_htiUPnP = NULL;
	m_htiCloseUPnPPorts = NULL;
	m_htiSkipWANIPSetup = NULL;
	m_htiSkipWANPPPSetup = NULL;
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
			BOOL bCheck = FALSE;
			if (m_ctrlTreeOptions.GetCheckBox(m_htiGeoLocationEnabled, bCheck))
				m_ctrlTreeOptions.Expand(m_htiGeoLocationEnabled, bCheck ? TVE_EXPAND : TVE_COLLAPSE);
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

void CPPgTweaks::OnSize(UINT nType, int cx, int cy)
{
	CPropertyPage::OnSize(nType, cx, cy);

	if (m_szBaseClient.cx == 0 || m_szBaseClient.cy == 0 || !::IsWindow(m_hWnd))
		return;

	const int dx = cx - m_szBaseClient.cx;
	const int dy = cy - m_szBaseClient.cy;

	CRect rectWarning(m_rcWarning);
	rectWarning.right += dx;
	if (CWnd *pWarning = GetDlgItem(IDC_WARNING))
		pWarning->MoveWindow(rectWarning);

	CRect rectTree(m_rcTree);
	rectTree.right += dx;
	rectTree.bottom += dy;
	if (CWnd *pTree = GetDlgItem(IDC_EXT_OPTS))
		pTree->MoveWindow(rectTree);

	UNREFERENCED_PARAMETER(nType);
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
