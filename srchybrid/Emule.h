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
#ifndef __AFXWIN_H__
#error include 'stdafx.h' before including this file for PCH
#endif
#include <vector>
#include "resource.h"

#define	DEFAULT_NICK			thePrefs.GetHomepageBaseURL()
#define	DEFAULT_TCP_PORT_OLD	4662
#define	DEFAULT_UDP_PORT_OLD	(DEFAULT_TCP_PORT_OLD + 10)

#define PORTTESTURL			_T("https://porttest.emule-project.net/connectiontest.php?tcpport=%i&udpport=%i&lang=%i")

#if defined(_DEBUG) || defined(EMULE_ENABLE_STARTUP_PROFILING)
#define EMULE_COMPILED_STARTUP_PROFILING 1
#else
#define EMULE_COMPILED_STARTUP_PROFILING 0
#endif

class CSearchList;
class CUploadQueue;
class CListenSocket;
class CDownloadQueue;
class CScheduler;
class UploadBandwidthThrottler;
class CemuleDlg;
class CClientList;
class CKnownFileList;
class CServerConnect;
class CServerList;
class CSharedFileList;
class CClientCreditsList;
class CFriendList;
class CClientUDPSocket;
class CIPFilter;
class CWebServer;
class CAbstractFile;
class CUpDownClient;
class CUPnPImplWrapper;
class CUploadDiskIOThread;
class CPartFileWriteThread;
class CGeoLocation;
class CIPFilterUpdater;

struct SLogItem;

enum AppState : uint8
{
	APP_STATE_STARTING = 0,	//initialization phase
	APP_STATE_RUNNING,
	APP_STATE_ASKCLOSE,		//exit dialog is on screen
	APP_STATE_SHUTTINGDOWN,
	APP_STATE_DONE			//shutdown has completed
};

/**
 * @brief One Chrome Trace Event row captured for startup profiling.
 */
struct SStartupProfileTraceEvent
{
	enum class EType : uint8
	{
		Complete = 0,
		Instant,
		Counter
	};

	CString		strName;
	CString		strCategory;
	CString		strStableId;
	CString		strCounterValueKey;
	EType		eType = EType::Instant;
	ULONGLONG	ullTimestampUs = 0;
	ULONGLONG	ullDurationUs = 0;
	ULONGLONG	ullCounterValue = 0;
};

/**
 * @brief One background monitored-share refresh batch posted back to the Shared Files UI.
 */
struct SMonitoredSharedDirectoryUpdate
{
	SMonitoredSharedDirectoryUpdate() = default;
	CStringList liNewDirectories;
	CStringList liRemovedDirectories;
	CStringList liDowngradedRoots;
	bool bForceTreeReload = false;
	bool bReloadSharedFiles = false;
};

/**
 * @brief One persisted USN checkpoint for an actively monitored shared root.
 */
struct SMonitoredSharedRootJournalState
{
	CString		strRootPath;
	ULONGLONG	ullUsnJournalId = 0;
	LONGLONG	llCheckpointUsn = 0;
};

class CemuleApp : public CWinApp
{
public:
	explicit CemuleApp(LPCTSTR lpszAppName = NULL);
	// Barry - To find out if app is running or shutting/shut down
	bool IsRunning() const	{ return m_app_state == APP_STATE_RUNNING || m_app_state == APP_STATE_ASKCLOSE; }
	bool IsClosing() const	{ return m_app_state == APP_STATE_SHUTTINGDOWN || m_app_state == APP_STATE_DONE; }
	bool IsStartupBindBlocked() const						{ return m_bStartupBindBlocked; }
	const CString& GetStartupBindBlockReason() const		{ return m_strStartupBindBlockReason; }
	bool CanWritePartMetFiles(LPCTSTR pszPath, bool bForceRefresh = false, bool bBypassDiskSpaceFloor = false);
	void InvalidatePartMetWriteGuardCache(LPCTSTR pszPath = NULL);
	/**
	 * @brief Resolves a part.met path to a stable volume identity using the
	 *        write-guard cache to avoid repeated Win32 volume probes.
	 */
	bool TryResolvePartMetWriteGuardVolume(LPCTSTR pszPath, bool bForceRefresh, CString &rstrVolumeRoot);
	/**
	 * @brief Returns whether env-gated startup phase profiling is enabled for this process.
	 */
	bool IsStartupProfilingEnabled() const						{ return m_bStartupProfilingEnabled; }
	/**
	 * @brief Reports whether the startup timer already emitted the canonical startup-complete milestone.
	 */
	bool HasStartupProfileReachedStartupComplete() const		{ return m_bStartupProfileStartupComplete; }
	/**
	 * @brief Resets the startup phase profiler output and timing baseline.
	 */
	void ResetStartupProfile();
	/**
	 * @brief Returns one startup-profile timestamp in microseconds derived from QueryPerformanceCounter.
	 */
	ULONGLONG GetStartupProfileTimestampUs() const;
	/**
	 * @brief Returns one startup-profile elapsed duration in microseconds from a previously captured timestamp.
	 */
	ULONGLONG GetStartupProfileElapsedUs(ULONGLONG ullStartTimestampUs) const;
	/**
	 * @brief Appends one startup phase timing sample when profiling is enabled.
	 */
	void AppendStartupProfileLine(LPCTSTR pszPhase, ULONGLONG ullDurationUs, ULONGLONG ullAbsoluteUs = static_cast<ULONGLONG>(-1));
	/**
	 * @brief Appends one numeric startup profiling counter sample when profiling is enabled.
	 */
	void AppendStartupProfileCounter(LPCTSTR pszCounterName, ULONGLONG ullValue, LPCTSTR pszValueKey = _T("value"));
	/**
	 * @brief Rewrites the startup profiling trace with the samples captured so far.
	 */
	void FlushStartupProfileTrace();
	/**
	 * @brief Reports whether startup redirected config-backed files to an alternate base directory.
	 */
	bool HasStartupConfigBaseDirOverride() const				{ return !m_strStartupConfigBaseDir.IsEmpty(); }
	/**
	 * @brief Returns the normalized override base directory selected through `-c`.
	 */
	const CString& GetStartupConfigBaseDirOverride() const		{ return m_strStartupConfigBaseDir; }

	UploadBandwidthThrottler *uploadBandwidthThrottler;
	CemuleDlg			*emuledlg;
	CClientList			*clientlist;
	CKnownFileList		*knownfiles;
	CServerConnect		*serverconnect;
	CServerList			*serverlist;
	CSharedFileList		*sharedfiles;
	CSearchList			*searchlist;
	CListenSocket		*listensocket;
	CUploadQueue		*uploadqueue;
	CDownloadQueue		*downloadqueue;
	CClientCreditsList	*clientcredits;
	CFriendList			*friendlist;
	CClientUDPSocket	*clientudp;
	CIPFilter			*ipfilter;
	CIPFilterUpdater	*ipfilterUpdater;
	CWebServer			*webserver;
	CScheduler			*scheduler;
	CUPnPImplWrapper	*m_pUPnPFinder;
	CUploadDiskIOThread	*m_pUploadDiskIOThread;
	CPartFileWriteThread *m_pPartFileWriteThread;
	CGeoLocation		*geolocation;


	static const UINT	m_nVersionMjr;
	static const UINT	m_nVersionMin;
	static const UINT	m_nVersionUpd;
	static const UINT	m_nVersionBld;
	static const TCHAR	*m_sPlatform;

	HANDLE		m_hMutexOneInstance;
	CFont		m_fontHyperText;
	CFont		m_fontDefaultBold;
	CFont		m_fontSymbol;
	CFont		m_fontLog;
	CFont		m_fontChatEdit;
	CBrush		m_brushBackwardDiagonal;
	DWORD		m_dwProductVersionMS;
	DWORD		m_dwProductVersionLS;
	CString		m_strCurVersionLong;
	CString		m_strCurVersionLongDbg;
	UINT		m_uCurVersionShort;
	UINT		m_uCurVersionCheck;
	ULONGLONG	m_ullComCtrlVer;
	CMutex		hashing_mut;
	CString		m_strPendingLink;
	COPYDATASTRUCT sendstruct;
	int			m_iDfltImageListColorFlags;
	AppState	m_app_state; // defines application state

// Implementierung
	virtual BOOL InitInstance();
	virtual int	ExitInstance();
	virtual BOOL IsIdleMessage(MSG *pMsg);

	// ed2k link functions
	void		AddEd2kLinksToDownload(const CString &strLinks, int cat);
	void		SearchClipboard();
	void		IgnoreClipboardLinks(const CString &strLinks)	{ m_strLastClipboardContents = strLinks; }
	void		PasteClipboard(int cat = 0);
	bool		IsEd2kFileLinkInClipboard();
	bool		IsEd2kServerLinkInClipboard();
	bool		IsEd2kLinkInClipboard(LPCSTR pszLinkType, int iLinkTypeLen);
	LPCTSTR		GetProfileFile()								{ return m_pszProfileName; }

	CString		CreateKadSourceLink(const CAbstractFile *f);
	void		StartSharedDirectoryMonitor();
	void		StopSharedDirectoryMonitor();
	void		WakeSharedDirectoryMonitor();

	// clipboard (text)
	bool		CopyTextToClipboard(const CString &strText);
	CString		CopyTextFromClipboard();

	void		OnlineSig();
	void		UpdateReceivedBytes(uint32 bytesToAdd);
	void		UpdateSentBytes(uint32 bytesToAdd, bool sentToFriend = false);
	int			GetFileTypeSystemImageIdx(LPCTSTR pszFilePath, int iLength = -1, bool bNormalsSize = false);
	HIMAGELIST	GetSystemImageList() const						{ return m_hSystemImageList; }
	HIMAGELIST	GetBigSystemImageList() const					{ return m_hBigSystemImageList; }
	CSize		GetSmallSytemIconSize() const					{ return m_sizSmallSystemIcon; }
	CSize		GetBigSytemIconSize() const						{ return m_sizBigSystemIcon; }
	void		CreateBackwardDiagonalBrush();
	void		CreateAllFonts();
	const CString& GetDefaultFontFaceName();
	bool		IsPortchangeAllowed();
	bool		IsConnected(bool bIgnoreEd2k = false, bool bIgnoreKad = false);
	bool		IsFirewalled();
	bool		CanDoCallback(CUpDownClient *client);
	uint32		GetID();
	// return current (valid) public IP or 0 if unknown (ignore KAD connection)
	uint32		GetED2KPublicIP() const							{ return m_dwPublicIP; }
	uint32		GetPublicIP() const;		// return current (valid) public IP or 0 if unknown
	void		SetPublicIP(const uint32 dwIP);
	void		ResetStandByIdleTimer();

	// because nearly all icons we are loading are 16x16, the default size is specified as 16 and not as 32 nor LR_DEFAULTSIZE
	HICON		LoadIcon(LPCTSTR lpszResourceName, int cx = 16, int cy = 16, UINT uFlags = LR_DEFAULTCOLOR) const;
	HICON		LoadIcon(UINT nIDResource) const;
	HBITMAP		LoadImage(LPCTSTR lpszResourceName, LPCTSTR pszResourceType) const;
//	HBITMAP		LoadImage(UINT nIDResource, LPCTSTR pszResourceType) const;
	bool		LoadSkinColor(LPCTSTR pszKey, COLORREF &crColor) const;
	bool		LoadSkinColorAlt(LPCTSTR pszKey, LPCTSTR pszAlternateKey, COLORREF &crColor) const;
	CString		GetSkinFileItem(LPCTSTR lpszResourceName, LPCTSTR pszResourceType) const;
	void		ApplySkin(LPCTSTR pszSkinProfile);
	void		EnableRTLWindowsLayout();
	void		DisableRTLWindowsLayout();
	void		UpdateDesktopColorDepth();
	void		UpdateLargeIconSize();
	bool		IsLegacyThemedControlsActive() const;
	bool		IsModernThemedControlsActive() const;
	void		RefreshStartupBindBlockState();

	void		ShowHelp(UINT uTopic, UINT uCmd = HELP_CONTEXT);

	// Elandal:ThreadSafeLogging -->
	// thread safe log calls
	void		QueueDebugLogLine(bool bAddToStatusBar, LPCTSTR line, ...);
	void		QueueDebugLogLineEx(UINT uFlags, LPCTSTR line, ...);
	void		HandleDebugLogQueue();
	void		ClearDebugLogQueue(bool bDebugPendingMsgs = false);

	void		QueueLogLine(bool bAddToStatusBar, LPCTSTR line, ...);
	void		QueueLogLineEx(UINT uFlags, LPCTSTR line, ...);
	void		HandleLogQueue();
	void		ClearLogQueue(bool bDebugPendingMsgs = false);
	// Elandal:ThreadSafeLogging <--

	bool		DidWeAutoStart() const							{ return m_bAutoStart; }
	void		ResetStandbyOff()								{ m_bStandbyOff = false; }

protected:
	/**
	 * @brief Parses and validates the optional startup config-root override before any profile-backed settings are read.
	 */
	bool InitializeStartupConfigBaseDirOverride();
	bool ProcessCommandline();
	void SetTimeOnTransfer();
	static BOOL CALLBACK SearchEmuleWindow(HWND hWnd, LPARAM lParam) noexcept;

	HIMAGELIST	m_hSystemImageList;
	CMapStringToPtr m_aExtToSysImgIdx;
	CSize		m_sizSmallSystemIcon;

	HIMAGELIST	m_hBigSystemImageList;
	CMapStringToPtr m_aBigExtToSysImgIdx;
	CSize		m_sizBigSystemIcon;

	CString		m_strDefaultFontFaceName;
	CString		m_strLastClipboardContents;

	// Elandal:ThreadSafeLogging -->
	// thread safe log calls
	CCriticalSection m_queueLock;
	CTypedPtrList<CPtrList, SLogItem*> m_QueueDebugLog;
	CTypedPtrList<CPtrList, SLogItem*> m_QueueLog;
	UINT m_uDroppedDebugLogEntries;
	UINT m_uDroppedLogEntries;
	// Elandal:ThreadSafeLogging <--
	CCriticalSection m_partMetWriteGuardLock;
	CMapStringToPtr m_aPartMetWriteGuardByVolume;
	CMapStringToString m_aPartMetWriteGuardPathToVolume;

	WSADATA		m_wsaData;
	uint32		m_dwPublicIP;
	CString		m_strStartupConfigBaseDir;
	CString		m_strStartupBindBlockReason;
	bool		m_bGuardClipboardPrompt;
	bool		m_bAutoStart;
	bool		m_bStartupBindBlocked;

	DECLARE_MESSAGE_MAP()
	afx_msg void OnHelp();

private:
	bool WriteStartupProfileTrace() const;
	void FinalizeStartupProfileTrace();
	static UINT AFX_CDECL SharedDirectoryMonitorThreadProc(LPVOID pParam);
	void RunSharedDirectoryMonitorLoop();
	/// Loads the persisted per-root USN checkpoints used for startup monitored-share catch-up.
	bool LoadSharedDirectoryMonitorJournalState();
	/// Persists the current per-root USN checkpoints for the next startup monitored-share catch-up.
	bool SaveSharedDirectoryMonitorJournalState() const;
	UINT		m_wTimerRes;
	bool		m_bStandbyOff;
	bool		m_bStartupProfilingEnabled;
	bool		m_bStartupProfileStartupComplete;
	bool		m_bStartupProfileCompleted;
	CCriticalSection m_startupProfileLock;
	ULONGLONG	m_ullStartupProfileBeginQpc;
	ULONGLONG	m_ullStartupProfileFrequency;
	CString		m_strStartupProfilePath;
	std::vector<SStartupProfileTraceEvent> m_aStartupProfileTraceEvents;
	CWinThread	*m_pSharedDirectoryMonitorThread;
	HANDLE		m_hSharedDirectoryMonitorStopEvent;
	HANDLE		m_hSharedDirectoryMonitorWakeEvent;
	std::vector<SMonitoredSharedRootJournalState> m_aSharedDirectoryMonitorJournalStates;
};

extern CemuleApp theApp;


//////////////////////////////////////////////////////////////////////////////
// CTempIconLoader

class CTempIconLoader
{
	HICON m_hIcon;

public:
	// because nearly all icons we are loading are 16x16, the default size is specified as 16 and not as 32 nor LR_DEFAULTSIZE
	explicit CTempIconLoader(LPCTSTR pszResourceID, int cx = 16, int cy = 16, UINT uFlags = LR_DEFAULTCOLOR);
	explicit CTempIconLoader(UINT uResourceID, int cx = 16, int cy = 16, UINT uFlags = LR_DEFAULTCOLOR);
	~CTempIconLoader();

	operator HICON() const										{ return m_hIcon; }
};
