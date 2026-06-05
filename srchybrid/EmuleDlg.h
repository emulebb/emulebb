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
#pragma once
#include <memory>

#include "MeterIcon.h"
#include "TaskbarNotifier.h"
#include "TitledMenu.h"
#include "TrayDialog.h"
#include "WindowsToastNotifier.h"
#include "AppMainIconSeams.h"

namespace Kademlia
{
	class CSearch;
	class CContact;
	class CEntry;
	class CUInt128;
};

namespace VersionCheckLaunchSeams
{
	struct SQueuedState;
}

class CChatWnd;
class CIrcWnd;
class CKademliaWnd;
class CKnownFileList;
class CMainFrameDropTarget;
class CMiniMuleDlg;
class CMuleStatusBarCtrl;
class CMuleSystrayDlg;
class CMuleToolbarCtrl;
class CPreferencesDlg;
class CSearchDlg;
class CServerWnd;
class CSharedFilesWnd;
class CStatisticsDlg;
class CTransferDlg;
struct Status;

extern UINT g_uMainThreadId;

// emuleapp <-> emuleapp
#define OP_ED2KLINK				12000
#define OP_CLCOMMAND			12001
#define OP_COLLECTION			12002

#define	EMULEBB_EXIT_ACCEL			'x'
#define	EMULEBB_HOTMENU_ACCEL		'u'
#define	EMULSKIN_BASEEXT		_T("eMuleSkin")

class CemuleDlg : public CTrayDialog
{
	friend class CMuleToolbarCtrl;

	enum
	{
		IDD = IDD_EMULE_DIALOG
	};

	//Client icons for all windows
	CImageList m_IconList;
	CReBarCtrl m_ctlMainTopReBar;
	CTaskbarNotifier m_wndTaskbarNotifier;
	CWindowsToastNotifier m_wndWindowsToastNotifier;
	void SetClientIconList();
public:
	explicit CemuleDlg(CWnd *pParent = NULL);
	~CemuleDlg();

	CImageList& GetClientIconList()				{ return m_IconList; }
	void ShowConnectionState();
	void ShowNetworkAddressState();
	void ShowNotifier(LPCTSTR pszText, TbnMsg nMsgType, LPCTSTR pszLink = NULL, bool bForceSoundOFF = false);
	void SendNotificationMail(TbnMsg nMsgType, LPCTSTR pszText);
	void ShowUserCount();
	void ShowMessageState(UINT nIcon);
	void SetActiveDialog(CWnd *dlg);
	CWnd* GetActiveDialog() const				{ return activewnd; }
	void ShowTransferRate(bool bForceAll = false, bool bTitleOnly = false);
	void UpdateTrayVisibility();
	void Localize();
	bool IsTransientDialogActive() const		{ return m_bTransientDialogActive; }
	/**
	 * @brief Clears the modeless MiniMule pointer after the popup destroys itself.
	 */
	void OnMiniMuleDestroyed(CMiniMuleDlg *pMiniMule);

#ifdef HAVE_WIN7_SDK_H
	void UpdateStatusBarProgress();
	void UpdateThumbBarButtons(bool initialAddToDlg = false);
	void OnTBBPressed(UINT id);
	void EnableTaskbarGoodies(bool enable);

	enum TBBIDS
	{
		TBB_FIRST,
		TBB_CONNECT = TBB_FIRST,
		TBB_DISCONNECT,
		TBB_THROTTLE,
		TBB_UNTHROTTLE,
		TBB_PREFERENCES,
		TBB_LAST = TBB_PREFERENCES
	};
#endif

	// Logging
	/**
	 * @brief Routes one already-formatted log message into the visible UI and disk sinks.
	 */
	void AddLogText(UINT uFlags, LPCTSTR pszText, const CTime *pTimestamp = NULL);
	void BeginLogBatchUpdate();
	void EndLogBatchUpdate();
	void AddServerMessageLine(UINT uFlags, LPCTSTR pszLine);
	void ResetLog();
	void ResetDebugLog();
	void ResetServerInfo();
	CString GetLastLogEntry();
	CString	GetLastDebugLogEntry();
	CString	GetAllLogEntries();
	CString	GetAllDebugLogEntries();
	CString GetServerInfoText();

	CString	GetConnectionStateString();
	bool IsNetworkAddressBlocked() const;
	CString	GetNetworkAddressStateString() const;
	UINT GetConnectionStateIconIndex() const;
	CString	GetTransferRateString();
	CString	GetUpDatarateString(UINT uUpDatarate = UINT_MAX);
	CString	GetDownDatarateString(UINT uDownDatarate = UINT_MAX);

	void StopTimer();
	void DoVersioncheck(bool manual);
	/// Starts or stops the runtime bind-loss monitor according to the current preferences.
	void UpdateBindLossMonitor(bool bForceVpnGuardHttpProbe = true);
	bool CanUseP2PConnectionCommands() const;
	void LogP2PConnectionCommandBlocked() const;
	void ApplyHyperTextFont(LPLOGFONT pFont);
	void ApplyLogFont(LPLOGFONT pFont);
	void ProcessED2KLink(LPCTSTR pszData);
	void SetStatusBarPartsSize();
	INT_PTR	ShowPreferences(UINT uStartPageID = UINT_MAX);
	bool IsPreferencesDlgOpen() const;
	bool IsTrayIconToFlash()				{ return m_iMsgIcon != 0; }
	void SetToolTipsDelay(UINT uMilliseconds);
	void StartUPnP(bool bReset = true, uint16 nForceTCPPort = 0, uint16 nForceUDPPort = 0);
	void RefreshUPnP(bool bRequestAnswer = false);
	/// Captures an operator-requested diagnostic dump for support triage.
	void CaptureDiagnosticDump(bool bFullMemoryDump);
	/// Copies a diagnostic JSON snapshot to the clipboard for local or shareable triage.
	void CopyDiagnosticSnapshotToClipboard(bool bRedacted);
	/// Runs the explicit elevated Windows Firewall repair action.
	void RepairWindowsFirewallRules();
	/// Runs the explicit elevated Windows long-path policy action.
	void EnableWindowsLongPaths();
	/// Runs the explicit elevated Microsoft Defender folder exclusion action.
	void ExcludeDownloadFoldersFromDefender();
	/// Launches the bundled interactive Prowlarr registration script.
	void RegisterProwlarrIntegration();
	/// Launches the bundled interactive Radarr or Sonarr registration script.
	void RegisterArrIntegration(LPCTSTR pszTargetName);
	/// Starts a clean app restart through the hidden same-executable sidecar.
	void RestartApp();
	HBRUSH GetCtlColor(CDC*, CWnd*, UINT);

	virtual void OnTrayRButtonUp(CPoint pt);
	virtual void OnTrayLButtonUp();
	virtual void TrayMinimizeToTrayChange();
	virtual void RestoreWindow();

	CTransferDlg	*transferwnd;
	CServerWnd		*serverwnd;
	CPreferencesDlg	*preferenceswnd;
	CSharedFilesWnd	*sharedfileswnd;
	CSearchDlg		*searchwnd;
	CChatWnd		*chatwnd;
	CMuleStatusBarCtrl *statusbar;
	CStatisticsDlg	*statisticswnd;
	CIrcWnd			*ircwnd;
	CMuleToolbarCtrl *toolbar;
	CKademliaWnd	*kademliawnd;
	CDialog			*m_pStartupProgressDlg;
	bool			m_bStartupProgressFinished;
	CWnd			*activewnd;
	uint8			status;

protected:
	WINDOWPLACEMENT m_wpFirstRestore;
	HICON			m_hIcon;
	HICON			m_hLowIDIcon;
	AppMainIconSeams::EConnectionIcon m_eMainConnectionIcon;
	HICON			m_connicons[9];
	HICON			transicons[4];
	HICON			imicons[3];
	HICON			m_icoSysTrayCurrent;
	HICON			usericon;
	CMeterIcon		m_TrayIcon;
	HICON			m_icoSysTrayConnected;		// do not use this icon for anything but the system tray!!!
	HICON			m_icoSysTrayDisconnected;	// do not use this icon for anything but the system tray!!!
	HICON			m_icoSysTrayLowID;			// do not use this icon for anything but the system tray!!!
	CImageList		imagelist;
	CTitledMenu		trayPopup;
	CMiniMuleDlg	*m_pMiniMule;
	CMuleSystrayDlg	*m_pSystrayDlg;
	CMainFrameDropTarget *m_pDropTarget;
	CMenu			m_SysMenuOptions;
	CMenu			m_menuUploadCtrl;
	CMenu			m_menuDownloadCtrl;
	int				m_iMsgIcon;
	UINT			m_uLastSysTrayIconCookie;
	uint32			m_uUpDatarate;
	uint32			m_uDownDatarate;
	std::shared_ptr<VersionCheckLaunchSeams::SQueuedState> m_pVersionCheckState;
	bool			m_bStartMinimizedChecked;
	bool			m_bStartMinimized;
	bool			m_bMsgBlinkState;
	bool			m_bConnectRequestDelayedForUPnP;
	bool			m_bKadSuspendDisconnect;
	bool			m_bEd2kSuspendDisconnect;
	bool			m_bTrayBalloonFallbackForSession;
	bool			m_bTransientDialogActive;
	bool			m_bInitedCOM;
	bool			m_bBindLossMonitorActive;
	bool			m_bBindLossShutdown;
	bool			m_bVpnGuardStartupProbePending;
	bool			m_bVpnGuardStartupApproved;
	bool			m_bVpnGuardRuntimeProbePending;
	uint32			m_uVpnGuardProbeGeneration;
	ULONGLONG		m_ullLastVpnGuardRuntimeProbeTick;
	UINT_PTR		m_uBindLossWatchdogTimer;
	UINT_PTR		m_uTransferRateDisplayTimer;
	HANDLE			m_hBindLossInterfaceNotification;
	HANDLE			m_hBindLossAddressNotification;
	CWinThread		*m_pAICHSyncThread;
#ifdef HAVE_WIN7_SDK_H
	CComPtr<ITaskbarList3>	m_pTaskbarList;
	THUMBBUTTON		m_thbButtons[TBB_LAST + 1];

	TBPFLAG			m_currentTBP_state;
	float			m_prevProgress;
	HICON			m_ovlIcon;
#endif

	// Lifecycle progress
	void ShowStartupProgress();
	void UpdateStartupProgress(UINT uPercent, UINT uStepStringId, UINT uDetailStringId, bool bMarquee = false);
	void DestroyStartupProgress();
	void FinishStartupProgress();
	void CloseStartupProgressIfRunning();
	bool ShouldShowLifecycleProgressDialog(int iMode, bool bStartup) const;
	void ApplyDesktopUiRefreshIntervalMs(UINT uIntervalMs);
	void StartTransferRateDisplayTimer();
	void StopTransferRateDisplayTimer();
	void RunDesktopPresentationTick();
	void DestroyMiniMule();
	void StartAICHSyncThread();
	void WaitForAICHSyncThreadShutdown();

	CMap<UINT, UINT, LPCTSTR, LPCTSTR> m_mapTbarCmdToIcon;
	void CreateToolbarCmdIconMap();
	LPCTSTR GetIconFromCmdId(UINT uId);

	// Startup Timer
	void OnStartupTimer() noexcept;
	void StopBindLossMonitor();
	void CheckBindLossMonitor();
	void CheckVpnGuardHttpMonitor(bool bForce);
	bool StartVpnGuardProbe(const CString& strPurpose, bool bRuntime);
	void ExitForVpnGuardFailure(const CString &strReason);
	void ExitForBindLoss(const CString &strReason);
	bool IsBindLossMonitorConfigured() const;

	// UPnP TimeOutTimer
	UINT_PTR m_hUPnPTimeOutTimer;
	static void CALLBACK UPnPTimeOutTimer(HWND hwnd, UINT uiMsg, UINT_PTR idEvent, DWORD dwTime) noexcept;

	void StartConnection();
	void CloseConnection();
	void MinimizeWindow(bool bForceTray = false);
	void PostStartupMinimized();
	void UpdateTrayIcon(int iPercent);
	HICON GetMainConnectionStateIcon(AppMainIconSeams::EConnectionIcon eIcon) const;
	void ShowMainConnectionStateIcon();
	void ShowConnectionStateIcon();
	void ShowTransferStateIcon();
	void ShowUserStateIcon();
	void AddSpeedSelectorMenus(CMenu *addToMenu);
	int  GetRecMaxUpload();
	void LoadNotifier(const CString &configuration);
	void ShowNotificationPopup(LPCTSTR pszText, TbnMsg nMsgType, LPCTSTR pszLink);
	bool ShowTrayBalloonNotification(LPCTSTR pszText, TbnMsg nMsgType);
	void HandleNotifierClicked(TbnMsg nMsgType, LPARAM lParam);
	bool ShouldTrayIconBeVisible();
	void ForceTrayBalloonFallbackForSession();
	bool notifierenabled;
	void ShowToolPopup(bool toolsonly = false);
	void ShowToolPopupAt(bool toolsonly, CPoint pt, bool bTrayMenu);
	void ShowTrayToolPopup(CPoint pt);
	void ApplyViewPresetCommand(UINT uCommandId);
	void SetAllIcons();
	bool CanClose(UINT uPromptStringID = IDS_MAIN_EXIT);
	void CloseApp(bool bRestart);
	bool TryStartRestartSidecar();
	int MapWindowToToolbarButton(CWnd *pWnd) const;
	CWnd* MapToolbarButtonToWindow(int iButtonID) const;
	int GetNextWindowToolbarButton(int iButtonID, int iDirection = 1) const;
	bool IsWindowToolbarButton(int iButtonID) const;
	void SetTaskbarIconColor();

	virtual void DoDataExchange(CDataExchange *pDX);
	virtual BOOL OnInitDialog();
	virtual void OnCancel();
	virtual void OnOK();
	virtual void OnTrayBalloonUserClick();
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
	virtual BOOL PreTranslateMessage(MSG *pMsg);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnClose();
	afx_msg void OnDestroy();
	afx_msg void OnActivate(UINT nState, CWnd *pWndOther, BOOL bMinimized);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg LRESULT OnStartupNextStage(WPARAM, LPARAM);
	afx_msg LRESULT OnGeoLocationUpdated(WPARAM, LPARAM);
	afx_msg LRESULT OnIPFilterUpdated(WPARAM, LPARAM);
	afx_msg LRESULT OnBindInterfaceChanged(WPARAM, LPARAM);
	afx_msg LRESULT OnVpnGuardProbeResult(WPARAM, LPARAM);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnBnClickedConnect();
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnBnClickedHotmenu();
	afx_msg LRESULT OnMenuChar(UINT nChar, UINT nFlags, CMenu *pMenu);
	afx_msg void OnMenuSelect(UINT nItemID, UINT nFlags, HMENU hSysMenu);
	afx_msg void OnSysColorChange();
	afx_msg HBRUSH OnCtlColor(CDC *pDC, CWnd *pWnd, UINT nCtlColor);
	afx_msg void OnSettingChange(UINT uFlags, LPCTSTR lpszSection);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg BOOL OnDeviceChange(UINT nEventType, DWORD_PTR dwData);
	afx_msg BOOL OnQueryEndSession();
	afx_msg void OnEndSession(BOOL bEnding);
	afx_msg LRESULT OnUserChanged(WPARAM, LPARAM);
	afx_msg LRESULT OnKickIdle(WPARAM, LPARAM lIdleCount);
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg BOOL OnChevronPushed(UINT id, LPNMHDR pNMHDR, LRESULT *plResult);
	afx_msg LRESULT OnPowerBroadcast(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnDisplayChange(WPARAM, LPARAM);

	// quick-speed changer -- based on xrmb
	afx_msg void QuickSpeedUpload(UINT nID);
	afx_msg void QuickSpeedDownload(UINT nID);
	afx_msg void QuickSpeedBoth(UINT nID);
	afx_msg void QuickSpeedOther(UINT nID);
	// end of quick-speed changer

	afx_msg LRESULT OnTaskbarNotifierClicked(WPARAM, LPARAM lParam);
	afx_msg LRESULT OnWindowsToastClicked(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnWMData(WPARAM, LPARAM lParam);
	afx_msg LRESULT OnFileHashed(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnHashFailed(WPARAM, LPARAM lParam);
	afx_msg LRESULT OnSharedFileHashed(WPARAM, LPARAM lParam);
	afx_msg LRESULT OnSharedHashFailed(WPARAM, LPARAM lParam);
	afx_msg LRESULT OnSharedHashResultsAvailable(WPARAM, LPARAM);
	afx_msg LRESULT OnFileAllocExc(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnFileCompleted(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnFileOpProgress(WPARAM wParam, LPARAM lParam);

	afx_msg LRESULT OnPeerPreviewFinished(WPARAM wParam, LPARAM lParam);

	afx_msg LRESULT OnAreYouEmule(WPARAM, LPARAM);

#ifdef HAVE_WIN7_SDK_H
	afx_msg LRESULT OnTaskbarBtnCreated(WPARAM, LPARAM);
#endif

	afx_msg LRESULT OnVersionCheckResponse(WPARAM wParam, LPARAM lParam);
	//Web Interface
	afx_msg LRESULT OnWebGUIInteraction(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnWebServerClearCompleted(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnWebRestApiCommand(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnWebServerFileRename(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnWebAddDownloads(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnWebSetCatPrio(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnAddRemoveFriend(WPARAM wParam, LPARAM lParam);
	// Terminal Services
	afx_msg LRESULT OnConsoleThreadEvent(WPARAM wParam, LPARAM lParam);
	// UPnP
	afx_msg LRESULT OnUPnPResult(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnPartFileDisplayUpdate(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnClientDisplayUpdate(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnPartFileProgressUpdate(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnStartupCacheSaveComplete(WPARAM wParam, LPARAM lParam);
};

#ifdef _DEBUG
///////////////////////////////////////////////////////////////////////////////
// Suppress null document warning in Output (CFrameWnd::Create)
//
class CFrameDoc : public CDocument
{
public:
	CFrameDoc() = default;
	BOOL OnNewDocument() { return CDocument::OnNewDocument(); }
};
#endif

enum EEMuleAppMsgs
{
	//thread messages
	TM_FINISHEDHASHING = WM_APP + 10,
	TM_HASHFAILED,
	TM_SHAREDFILEHASHED,
	TM_SHAREDFILEHASHFAILED,
	TM_SHAREDHASHRESULTSAVAILABLE,
	TM_PEERPREVIEWFINISHED,
	TM_FILEALLOCEXC,
	TM_FILECOMPLETED,
	TM_FILEOPPROGRESS,
	TM_CONSOLETHREADEVENT,
	TM_VIDEOTHUMBNAILFINISHED
};

enum EWebinterfaceOrders
{
	WEBGUIIA_UPDATEMYINFO = 1,
	WEBGUIIA_WINFUNC,
	WEBGUIIA_UPD_CATTABS,
	WEBGUIIA_UPD_SFUPDATE,
	WEBGUIIA_UPDATESERVER,
	WEBGUIIA_STOPCONNECTING,
	WEBGUIIA_CONNECTTOSERVER,
	WEBGUIIA_DISCONNECT,
	WEBGUIIA_SERVER_REMOVE,
	WEBGUIIA_SHARED_FILES_RELOAD,
	WEBGUIIA_ADD_TO_STATIC,
	WEBGUIIA_REMOVE_FROM_STATIC,
	WEBGUIIA_UPDATESERVERMETFROMURL,
	WEBGUIIA_UPDATENODESDATFROMURL,
	WEBGUIIA_SHOWSTATISTICS,
	WEBGUIIA_DELETEALLSEARCHES,
	WEBGUIIA_KAD_BOOTSTRAP,
	WEBGUIIA_KAD_START,
	WEBGUIIA_KAD_STOP,
	WEBGUIIA_KAD_RCFW
};
