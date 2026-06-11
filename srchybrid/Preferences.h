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
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#pragma once

#include <string>
#include <unordered_set>

#include "BindAddressResolver.h"
#include "DisplayRefreshSeams.h"
#include "PartFilePersistenceSeams.h"
#include "PreferenceValidationSeams.h"
#include "Opcodes.h"
#include "VpnGuardSeams.h"

class CIni;

extern LPCTSTR const strDefaultToolbar;

enum EViewSharedFilesAccess
{
	vsfaEverybody,
	vsfaFriends,
	vsfaNobody
};

enum ENotifierSoundType
{
	ntfstNoSound,
	ntfstSoundFile,
	ntfstSpeech
};

enum ENotifierDisplayMode
{
	ntfdmCustomPopup,
	ntfdmWindowsToast,
	ntfdmTrayBalloon
};

enum ELifecycleProgressDialogMode
{
	LPDM_OFF = 0,
	LPDM_WHEN_VISIBLE = 1,
	LPDM_ALWAYS = 2
};

enum TLSmode: byte
{
	MODE_NONE,
	MODE_SSL_TLS,
	MODE_STARTTLS
};

enum SMTPauth: byte
{
	AUTH_NONE,
	AUTH_PLAIN,
	AUTH_LOGIN /*,
	AUTH_GSSAPI,
	AUTH_DIGEST,
	AUTH_MD5,
	AUTH_CRAM,
	AUTH_OAUTH1,
	AUTH_OAUTH2 */
};

enum class ESessionTransferLimitMode : uint8
{
	Disabled = 0,
	PercentOfFile = 1,
	AbsoluteMiB = 2
};

/**
 * Selects the NAT mapping backend policy used by the UPnP wrapper.
 */
enum EUPnPBackendMode : uint8
{
	UPNP_BACKEND_AUTOMATIC = 0,
	UPNP_BACKEND_IGD_ONLY = 1,
	UPNP_BACKEND_PCP_NATPMP_ONLY = 2
};


enum EDefaultDirectory
{
	EMULE_CONFIGDIR = 0,
	EMULE_TEMPDIR = 1,
	EMULE_INCOMINGDIR = 2,
	EMULE_LOGDIR = 3,
	EMULE_ADDLANGDIR = 4, // directories with languages installed by the eMule (parent: EMULE_EXPANSIONDIR)
	EMULE_INSTLANGDIR = 5, // directories with languages installed by the user or installer (parent: EMULE_EXECUTABLEDIR)
	EMULE_WEBSERVERDIR = 6,
	EMULE_SKINDIR = 7,
	EMULE_DATABASEDIR = 8, // the parent directory of the incoming/temp folder
	EMULE_CONFIGBASEDIR = 9, // the parent directory of the config folder
	EMULE_EXECUTABLEDIR = 10, // assumed to be non-writable (!)
	EMULE_TOOLBARDIR = 11,
	EMULE_EXPANSIONDIR = 12 // this is a base directory accessible to all users for things eMule installs
};

enum EToolbarLabelType : uint8;
enum ELogFileFormat : uint8;

// DO NOT EDIT VALUES like changing uint16 to uint32, or insert any value. ONLY append new vars
#pragma pack(push, 1)
struct Preferences_Ext_Struct
{
	uint8	version;
	uchar	userhash[16];
	WINDOWPLACEMENT EmuleWindowPlacement;
};
#pragma pack(pop)

//email notifier
struct EmailSettings
{
	CString	sServer;
	CString	sFrom;
	CString	sTo;
	CString	sUser;
	CString	sPass;
	uint16	uPort;
	SMTPauth uAuth;
	TLSmode uTLS;
	bool	bSendMail;
};


// deadlake PROXYSUPPORT
struct ProxySettings
{
	CString	host;
	CString	user;
	CString	password;
	uint16	type;
	uint16	port;
	bool	bEnablePassword;
	bool	bUseProxy;
};

struct Category_Struct
{
	CString	strIncomingPath;
	CString	strTitle;
	CString	strComment;
	CString autocat;
	CString	regexp;
	COLORREF color;
	UINT	prio;
	int		filter;
	bool	filterNeg;
	bool	care4all;
	bool	ac_regexpeval;
	bool	downloadInAlphabeticalOrder; // ZZ:DownloadManager
};

class CPreferences
{
	friend class CPreferencesWnd;
	friend class CPPgConnection;
	friend class CPPgDebug;
	friend class CPPgDirectories;
	friend class CPPgDisplay;
	friend class CPPgFiles;
	friend class CPPgGeneral;
	friend class CPPgIRC;
	friend class CPPgNotify;
	friend class CPPgScheduler;
	friend class CPPgSecurity;
	friend class CPPgServer;
	friend class CPPgTweaks;
	friend class Wizard;

	static LPCSTR	m_pszBindAddrA;
	static LPCWSTR	m_pszBindAddrW;
public:
	static CString	strNick;
	static uint32	m_maxupload;
	static uint32	m_maxdownload;
	static uint32	m_uSessionMaxUpload;
	static uint32	m_uSessionMaxDownload;
	static CString	m_strConfiguredBindAddr;
	static CString	m_strBindInterface;
	static CString	m_strBindInterfaceName;
	static CString	m_strActiveConfiguredBindAddr;
	static CString	m_strActiveBindInterface;
	static CString	m_strActiveBindInterfaceName;
	static DWORD	m_dwActiveBindInterfaceIndex;
	static CStringA m_strBindAddrA;
	static CStringW m_strBindAddrW;
	static EBindAddressResolveResult m_eActiveBindAddrResolveResult;
	static bool		m_bActiveStartupBindBlockEnabled;
	static VpnGuardSeams::EMode m_eVpnGuardMode;
	static CString	m_strVpnGuardAllowedPublicIpCidrs;
	static bool		m_bRandomizePortsOnStartup;
	static uint16	port;
	static uint16	udpport;
	static uint16	nServerUDPPort;
	static UINT		maxconnections;
	static UINT		maxhalfconnections;
	static bool		m_bConditionalTCPAccept;
	static bool		reconnect;
	static bool		m_bUseServerPriorities;
	static bool		m_bUseUserSortedServerList;
	static CString	m_strIncomingDir;
	static CStringArray	tempdir;
	static bool		ICH;
	static bool		m_bAutoUpdateServerList;
	static bool		m_bUpdateNotify;
	static bool		mintotray;
	static bool		autoconnect;
	static bool		m_bAutoConnectToStaticServersOnly; // Barry
	static bool		autotakeed2klinks;	   // Barry
	static bool		addnewfilespaused;	   // Barry
	static UINT		depth3D;			   // Barry
	static int		m_iStraightWindowStyles;
	static bool		m_bUseSystemFontForMainControls;
	static bool		m_bRTLWindowsLayout;
	static CString	m_strSkinProfile;
	static CString	m_strSkinProfileDir;
	static bool		m_bAddServersFromServer;
	static bool		m_bAddServersFromClients;
	static UINT		maxsourceperfile;
	static UINT		trafficOMeterInterval;
	static UINT		statsInterval;
	static bool		m_bFillGraphs;
	static uchar	userhash[16];
	static WINDOWPLACEMENT EmuleWindowPlacement;
	static uint32	maxGraphDownloadRate;
	static bool		beepOnError;
	static bool		confirmExit;
	static DWORD	m_adwStatsColors[15];
	static bool		m_bHasCustomTaskIconColor;
	static bool		m_bIconflashOnNewMessage;

	static int		m_iStartupProgressDialogMode;
	static int		m_iShutdownProgressDialogMode;
	static bool		m_bDailyConfigBackup;
	static UINT		m_uDesktopUiRefreshIntervalMs;
	static bool		filterLANIPs;
	static bool		m_bAllocLocalHostIP;
	static bool		onlineSig;

	// -khaos--+++> Struct Members for Storing Statistics

	// Saved stats for cumulative downline overhead...
	static uint64	cumDownOverheadTotal;
	static uint64	cumDownOverheadFileReq;
	static uint64	cumDownOverheadSrcEx;
	static uint64	cumDownOverheadServer;
	static uint64	cumDownOverheadKad;
	static uint64	cumDownOverheadTotalPackets;
	static uint64	cumDownOverheadFileReqPackets;
	static uint64	cumDownOverheadSrcExPackets;
	static uint64	cumDownOverheadServerPackets;
	static uint64	cumDownOverheadKadPackets;

	// Saved stats for cumulative upline overhead...
	static uint64	cumUpOverheadTotal;
	static uint64	cumUpOverheadFileReq;
	static uint64	cumUpOverheadSrcEx;
	static uint64	cumUpOverheadServer;
	static uint64	cumUpOverheadKad;
	static uint64	cumUpOverheadTotalPackets;
	static uint64	cumUpOverheadFileReqPackets;
	static uint64	cumUpOverheadSrcExPackets;
	static uint64	cumUpOverheadServerPackets;
	static uint64	cumUpOverheadKadPackets;

	// Saved stats for cumulative upline data...
	static uint32	cumUpSuccessfulSessions;
	static uint32	cumUpFailedSessions;
	static uint32	cumUpAvgTime;
	// Cumulative client breakdown stats for sent bytes...
	static uint64	cumUpData_EDONKEY;
	static uint64	cumUpData_EDONKEYHYBRID;
	static uint64	cumUpData_EMULE;
	static uint64	cumUpData_MLDONKEY;
	static uint64	cumUpData_AMULE;
	static uint64	cumUpData_EMULECOMPAT;
	static uint64	cumUpData_SHAREAZA;
	// Session client breakdown stats for sent bytes...
	static uint64	sesUpData_EDONKEY;
	static uint64	sesUpData_EDONKEYHYBRID;
	static uint64	sesUpData_EMULE;
	static uint64	sesUpData_MLDONKEY;
	static uint64	sesUpData_AMULE;
	static uint64	sesUpData_EMULECOMPAT;
	static uint64	sesUpData_SHAREAZA;

	// Cumulative port breakdown stats for sent bytes...
	static uint64	cumUpDataPort_4662;
	static uint64	cumUpDataPort_OTHER;
	// Session port breakdown stats for sent bytes...
	static uint64	sesUpDataPort_4662;
	static uint64	sesUpDataPort_OTHER;

	// Cumulative source breakdown stats for sent bytes...
	static uint64	cumUpData_File;
	static uint64	cumUpData_Partfile;
	// Session source breakdown stats for sent bytes...
	static uint64	sesUpData_File;
	static uint64	sesUpData_Partfile;

	// Saved stats for cumulative downline data...
	static uint32	cumDownCompletedFiles;
	static uint32	cumDownSuccessfulSessions;
	static uint32	cumDownFailedSessions;
	static uint32	cumDownAvgTime;

	// Cumulative statistics for saved due to compression/lost due to corruption
	static uint64	cumLostFromCorruption;
	static uint64	cumSavedFromCompression;
	static uint32	cumPartsSavedByICH;

	// Session statistics for download sessions
	static uint32	sesDownSuccessfulSessions;
	static uint32	sesDownFailedSessions;
	static uint32	sesDownAvgTime;
	static uint32	sesDownCompletedFiles;
	static uint64	sesLostFromCorruption;
	static uint64	sesSavedFromCompression;
	static uint32	sesPartsSavedByICH;

	// Cumulative client breakdown stats for received bytes...
	static uint64	cumDownData_EDONKEY;
	static uint64	cumDownData_EDONKEYHYBRID;
	static uint64	cumDownData_EMULE;
	static uint64	cumDownData_MLDONKEY;
	static uint64	cumDownData_AMULE;
	static uint64	cumDownData_EMULECOMPAT;
	static uint64	cumDownData_SHAREAZA;
	// Session client breakdown stats for received bytes...
	static uint64	sesDownData_EDONKEY;
	static uint64	sesDownData_EDONKEYHYBRID;
	static uint64	sesDownData_EMULE;
	static uint64	sesDownData_MLDONKEY;
	static uint64	sesDownData_AMULE;
	static uint64	sesDownData_EMULECOMPAT;
	static uint64	sesDownData_SHAREAZA;

	// Cumulative port breakdown stats for received bytes...
	static uint64	cumDownDataPort_4662;
	static uint64	cumDownDataPort_OTHER;
	// Session port breakdown stats for received bytes...
	static uint64	sesDownDataPort_4662;
	static uint64	sesDownDataPort_OTHER;

	// Saved stats for cumulative connection data...
	static float	cumConnAvgDownRate;
	static float	cumConnMaxAvgDownRate;
	static float	cumConnMaxDownRate;
	static float	cumConnAvgUpRate;
	static float	cumConnMaxAvgUpRate;
	static float	cumConnMaxUpRate;
	static time_t	cumConnRunTime;
	static uint32	cumConnNumReconnects;
	static uint32	cumConnAvgConnections;
	static uint32	cumConnMaxConnLimitReached;
	static uint32	cumConnPeakConnections;
	static uint32	cumConnTransferTime;
	static uint32	cumConnDownloadTime;
	static uint32	cumConnUploadTime;
	static uint32	cumConnServerDuration;

	// Saved records for servers / network...
	static uint32	cumSrvrsMostWorkingServers;
	static uint32	cumSrvrsMostUsersOnline;
	static uint32	cumSrvrsMostFilesAvail;

	// Saved records for shared files...
	static uint32	cumSharedMostFilesShared;
	static uint64	cumSharedLargestShareSize;
	static uint64	cumSharedLargestAvgFileSize;
	static uint64	cumSharedLargestFileSize;

	// Save the date when the statistics were last reset...
	static time_t	stat_datetimeLastReset;

	// Save new preferences for PPgStats
	static UINT		statsConnectionsGraphRatio; // This will store the divisor, i.e. for 1:3 it will be 3, for 1:20 it will be 20.
	// Save the expanded branches of the stats tree
	static CString	m_strStatsExpandedTreeItems;

	static bool		m_bShowVerticalHourMarkers;
	static UINT		statsSaveInterval;
	// <-----khaos- End Statistics Members


	// Original Stats Stuff
	static uint64	totalDownloadedBytes;
	static uint64	totalUploadedBytes;
	// End Original Stats Stuff
	static WORD		m_wLanguageID;
	static bool		transferDoubleclick;
	static EViewSharedFilesAccess m_iSeeShares;
	static UINT		m_iToolDelayTime;	// tooltip delay time in seconds
	static bool		bringtoforeground;
	static UINT		splitterbarPosition;
	static UINT		splitterbarPositionSvr;

	static UINT		m_uTransferWnd1;
	static UINT		m_uTransferWnd2;
	//MORPH START - Added by SiRoB, Splitting Bar [O²]
	static UINT		splitterbarPositionStat;
	static UINT		splitterbarPositionStat_HL;
	static UINT		splitterbarPositionStat_HR;
	static UINT		splitterbarPositionFriend;
	static UINT		splitterbarPositionIRC;
	static UINT		splitterbarPositionShared;
	//MORPH END - Added by SiRoB, Splitting Bar [O²]
	static UINT		m_uDeadServerRetries;
	static DWORD	m_dwServerKeepAliveTimeout;
	static DWORD	m_dwConnectionTimeout;
	static DWORD	m_dwDownloadTimeout;
	// -khaos--+++> Changed data type to avoid overflows
	static UINT		statsMax;
	// <-----khaos-
	static UINT		statsAverageMinutes;

	static CString	notifierConfiguration;
	static bool		notifierOnDownloadFinished;
	static bool		notifierOnNewDownload;
	static bool		notifierOnChat;
	static bool		notifierOnLog;
	static bool		notifierOnImportantError;
	static bool		notifierOnEveryChatMsg;
	static bool		notifierOnNewVersion;
	static ENotifierDisplayMode notifierDisplayMode;
	static ENotifierSoundType notifierSoundType;
	static CString	notifierSoundFile;

	static CString	m_strIRCServer;
	static CString	m_strIRCNick;
	static CString	m_strIRCChannelFilter;
	static bool		m_bIRCAddTimeStamp;
	static bool		m_bIRCUseChannelFilter;
	static UINT		m_uIRCChannelUserFilter;
	static CString	m_strIRCPerformString;
	static bool		m_bIRCUsePerform;
	static bool		m_bIRCGetChannelsOnConnect;
	static bool		m_bIRCAcceptLinks;
	static bool		m_bIRCAcceptLinksFriendsOnly;
	static bool		m_bIRCPlaySoundEvents;
	static bool		m_bIRCIgnoreMiscMessages;
	static bool		m_bIRCIgnoreJoinMessages;
	static bool		m_bIRCIgnorePartMessages;
	static bool		m_bIRCIgnoreQuitMessages;
	static bool		m_bIRCIgnorePingPongMessages;
	static bool		m_bIRCIgnoreEmuleAddFriendMsgs;
	static bool		m_bIRCAllowEmuleAddFriend;
	static bool		m_bIRCIgnoreEmuleSendLinkMsgs;
	static bool		m_bIRCJoinHelpChannel;
	static bool		m_bIRCEnableSmileys;
	static bool		m_bMessageEnableSmileys;
	static bool		m_bIRCEnableUTF8;

	static bool		m_bRemove2bin;
	static bool		m_bShowCopyEd2kLinkCmd;
	static bool		m_bpreviewprio;
	static bool		m_bSmartServerIdCheck;
	static uint8	smartidstate;
	static bool		m_bSafeServerConnect;
	static bool		startMinimized;
	static bool		m_bAutoStart;
	static bool		m_bRestoreLastMainWndDlg;
	static int		m_iLastMainWndDlgID;
	static bool		m_bRestoreLastLogPane;
	static int		m_iLastLogPaneID;
	static UINT		MaxConperFive;
	static ULONGLONG m_uMinFreeDiskSpaceConfig;
	static ULONGLONG m_uMinFreeDiskSpaceTemp;
	static ULONGLONG m_uMinFreeDiskSpaceIncoming;
	static bool		m_bSparsePartFiles;
	static CString	m_strYourHostname;
	static bool		m_bEnableVerboseOptions;
	static bool		m_bVerbose;
	static bool		m_bFullVerbose;
	static int		m_byLogLevel;
	static bool		m_bDebugSourceExchange; // Sony April 23. 2003, button to keep source exchange msg out of verbose log
	static bool		m_bLogBannedClients;
	static bool		m_bLogRatingDescReceived;
	static bool		m_bLogSecureIdent;
	static bool		m_bLogFilteredIPs;
	static bool		m_bLogFileSaving;
	static bool		m_bLogA4AF; // ZZ:DownloadManager
	static bool		m_bLogUlDlEvents;
	static bool		m_bUseDebugDevice;
	static int		m_iDebugServerTCPLevel;
	static int		m_iDebugServerUDPLevel;
	static int		m_iDebugServerSourcesLevel;
	static int		m_iDebugServerSearchesLevel;
	static int		m_iDebugClientTCPLevel;
	static int		m_iDebugClientUDPLevel;
	static int		m_iDebugClientKadUDPLevel;
	static int		m_iDebugSearchResultDetailLevel;
	static bool		m_bupdatequeuelist;
	static bool		m_bManualAddedServersHighPriority;
	static bool		m_btransferfullchunks;
	static int		m_istartnextfile;
	static bool		m_bshowoverhead;
	static bool		m_bDAP;
	static bool		m_bUAP;
	static bool		m_bDisableKnownClientList;
	static bool		m_bDisableQueueList;
	static bool		m_bTransflstRemain;

	static UINT		m_uUpdateDays;
	static bool		showRatesInTitle;
	static int		m_iSearchResultsFileSizeFormat;

	static CString	m_strTxtEditor;
	static CString	m_strVideoPlayer;
	static CString	m_strVideoPlayerArgs;
	static CString	m_strVideoThumbnailFfmpegPath;
	static UINT		m_uVideoThumbnailIntervalSeconds;
	static bool		m_bAllowPeerPreview;
	static CString	m_strMediaInfoDllPath;
	static bool		m_bRunCommandOnFileCompletion;
	static CString	m_strFileCompletionProgram;
	static CString	m_strFileCompletionArguments;
	static bool		m_bVideoPreviewThumbnails;
	static int		m_iPreviewSmallBlocks;
	static bool		m_bPreviewCopiedArchives;
	static bool		m_bInspectAllFileTypes;
	static bool		m_bPreviewOnIconDblClk;
	static bool		m_bCheckFileOpen;
	static bool		indicateratings;
	static bool		watchclipboard;
	static bool		m_bIPFilterEnabled;
	static bool		filterserverbyip;
	static bool		m_bFirstStart;
	static bool		m_bDisableFirstTimeWizard;
	static bool		m_bCreditSystem;

	static bool		log2disk;
	static bool		debug2disk;
	static int		iMaxLogBuff;
	static UINT		uMaxLogFileSize;
	static ELogFileFormat m_iLogFileFormat;
	static int		m_iCreateCrashDumpMode;
	static bool		m_bCaptureFullCrashDump;
	static bool		scheduler;
	static bool		msgonlyfriends;
	static bool		msgsecure;
	static bool		m_bUseChatCaptchas;

	static UINT		filterlevel;
	static UINT		m_uFileBufferSize;
	static bool		m_bDownloadAutoBroadbandIO;
	static INT_PTR	m_iQueueSize;
	static UINT		m_uEd2kSearchMaxResults;
	static UINT		m_uEd2kSearchMaxMoreRequests;
	static UINT		m_uKadFileSearchTotal;
	static UINT		m_uKadKeywordSearchTotal;
	static UINT		m_uKadFileSearchLifetimeSeconds;
	static UINT		m_uKadKeywordSearchLifetimeSeconds;
	static bool		m_bDetectTCPErrorFlooder;
	static UINT		m_uTCPErrorFlooderIntervalMinutes;
	static UINT		m_uTCPErrorFlooderThreshold;
	static int		m_iCommitFiles;
	static DWORD	m_uFileBufferTimeLimit;

	static UINT		maxmsgsessions;
	static time_t	versioncheckLastAutomatic;
	static CString	messageFilter;
	static CString	commentFilter;
	static CString	filenameCleanups;
	static CString	m_strDateTimeFormat;
	static CString	m_strDateTimeFormat4Log;
	static CString	m_strDateTimeFormat4Lists;
	static LOGFONT	m_lfHyperText;
	static LOGFONT	m_lfLogText;
	static COLORREF m_crLogError;
	static COLORREF m_crLogWarning;
	static COLORREF m_crLogSuccess;
	static int		m_iExtractMetaData;
	static bool		m_bRearrangeKadSearchKeywords;
	static bool		m_bAllocFull;
	static bool		m_bShowSharedFilesDetails;
	static bool		m_bShowWin7TaskbarGoodies;
	static bool		m_bShowUpDownIconInTaskbar;
	static bool		m_bAlwaysShowTrayIcon;
	static bool		m_bEnableMiniMule;
	static bool		m_bForceSpeedsToKB;
	static bool		m_bAutoShowLookups;
	static bool		m_bExtraPreviewWithMenu;

	// Web Server [kuchin]
	static CString	m_strWebPassword;
	static CString	m_strWebLowPassword;
	static CString	m_strWebApiKey;
	static CString	m_strWebBindAddr;
	static uint16	m_nWebPort;
	static bool		m_bWebUseUPnP;
	static bool		m_bWebEnabled;
	static bool		m_bLegacyWebUiEnabled;
	static bool		m_bWebUseGzip;
	static int		m_nWebPageRefresh;
	static bool		m_bWebLowEnabled;
	static int		m_iWebTimeoutMins;
	static int		m_iWebFileUploadSizeLimitMB;
	static CString	m_strTemplateFile;
	static ProxySettings proxy; // deadlake PROXYSUPPORT
	static bool		m_bAllowAdminHiLevFunc;
	static bool		m_bDiagnosticRestEndpointsEnabled;
	static CUIntArray m_aAllowedRemoteAccessIPs;
	static bool		m_bWebUseHttps;
	static CString	m_sWebHttpsCertificate;
	static CString	m_sWebHttpsKey;

	static bool		showCatTabInfos;
	static bool		resumeSameCat;
	static bool		dontRecreateGraphs;
	static bool		autofilenamecleanup;
	//static int	allcatType;
	//static bool	allcatTypeNeg;
	static bool		m_bUseAutocompl;
	static bool		m_bShowDwlPercentage;
	static bool		m_bRemoveFinishedDownloads;
	static INT_PTR	m_iMaxChatHistory;
	static bool		m_bShowActiveDownloadsBold;

	static int		m_iSearchMethod;
	static bool		m_bAdvancedSpamfilter;
	static bool		m_bUseSecureIdent;

	static bool		networkkademlia;
	static bool		networked2k;

	// toolbar
	static EToolbarLabelType m_nToolbarLabels;
	static CString	m_sToolbarBitmap;
	static CString	m_sToolbarBitmapFolder;
	static CString	m_sToolbarSettings;
	static bool		m_bReBarToolbar;
	static CSize	m_sizToolbarIconSize;

	static bool		m_bWinaTransToolbar;
	static bool		m_bShowDownloadToolbar;

	//preview
	static bool		m_bPreviewEnabled;
	static bool		m_bAutomaticArcPreviewStart;

	static bool		m_bA4AFSaveCpu; // ZZ:DownloadManager

	static bool		m_bHighresTimer;

	/// Guards ownership of the live shared-directory list.
	static CCriticalSection m_csSharedDirList;
	static CStringList shareddir_list;
	static CStringList monitored_shareddir_list;
	static CStringList monitor_owned_shareddir_list;
	static std::unordered_set<std::wstring> shareddir_lookup_keys;
	static std::unordered_set<std::wstring> monitored_shareddir_lookup_keys;
	static std::unordered_set<std::wstring> monitor_owned_shareddir_lookup_keys;
	static CStringList addresses_list;
	static bool		m_bKeepUnavailableFixedSharedDirs;

	static int		m_iDbgHeap;
	static UINT		m_nWebMirrorAlertLevel;

	static bool		m_bUseOldTimeRemaining;

	// files
	static bool		m_bRememberCancelledFiles;
	static bool		m_bRememberDownloadedFiles;
	static bool		m_bPartiallyPurgeOldKnownFiles;
	static bool		m_bFollowMajorityFilenameForNewDownloads;
	static UINT		m_uFollowMajorityFilenameRequiredPercent;
	static UINT		m_uFollowMajorityFilenameMinimumVotes;
	static UINT		m_uMaxUploadClientsAllowed;
	static UINT		m_uUploadSlotElasticPercent;
	static float	m_fSlowUploadThresholdFactor;
	static UINT		m_uSlowUploadGraceSeconds;
	static UINT		m_uSlowUploadWarmupSeconds;
	static UINT		m_uZeroUploadRateGraceSeconds;
	static UINT		m_uSlowUploadCooldownSeconds;
	static bool		m_bLowRatioBoostEnabled;
	static float	m_fLowRatioThreshold;
	static UINT		m_uLowRatioBonus;
	static UINT		m_uLowIDDivisor;
	static ESessionTransferLimitMode m_eSessionTransferLimitMode;
	static UINT		m_uSessionTransferLimitValue;
	static UINT		m_uSessionTimeLimitSeconds;

	//email notifier
	static EmailSettings m_email;

	// encryption / obfuscation / verification
	static bool		m_bCryptLayerRequested;
	static bool		m_bCryptLayerSupported;
	static bool		m_bCryptLayerRequired;
	static uint8	m_byCryptTCPPaddingLength;
	static uint32   m_dwKadUDPKey;

	// UPnP
	static bool		m_bEnableUPnP;
	static bool		m_bCloseUPnPOnExit;
	static uint8	m_uUPnPBackendMode;

	// Spam
	static bool		m_bEnableSearchResultFilter;

	// Search results: bitmask of already-known states hidden from results (ESearchKnownHideFlags)
	static uint8	m_uSearchHideKnownStates;

	static BOOL		m_bIsRunningAeroGlass;
	static bool		m_bPreventStandby;
	static bool		m_bStoreSearches;
	static bool		m_bGeoLocationEnabled;
	static UINT		m_uGeoLocationCheckDays;
	static __time64_t m_tGeoLocationLastCheckTime;
	static CString	m_strGeoLocationUpdateUrl;
	static bool		m_bAutoIPFilterUpdate;
	static UINT		m_uIPFilterUpdatePeriodDays;
	static __time64_t m_tIPFilterLastUpdateTime;
	static CString	m_strIPFilterUpdateUrl;


	enum Table
	{
		tableDownload,
		tableUpload,
		tableQueue,
		tableSearch,
		tableShared,
		tableServer,
		tableClientList,
		tableFilenames,
		tableIrcMain,
		tableIrcChannels,
		tableDownloadClients
	};

	CPreferences();
	~CPreferences();

	static void	Init();
	static void	Uninit();

	static LPCTSTR	GetTempDir(INT_PTR id = 0)			{ return (LPCTSTR)tempdir[(id < tempdir.GetCount()) ? id : 0]; }
	static INT_PTR	GetTempDirCount()					{ return tempdir.GetCount(); }
	/// Copies the live shared-directory list into a caller-owned snapshot.
	static void		CopySharedDirectoryList(CStringList &out);
	/// Replaces the live shared-directory list with a prepared snapshot.
	static void		ReplaceSharedDirectoryList(const CStringList &in);
	/// Adds a shared directory if no equivalent path is already listed.
	static bool		AddSharedDirectoryIfAbsent(const CString &dir);
	/// Removes one shared directory if it is currently listed.
	static bool		RemoveSharedDirectory(const CString &dir);
	/// Checks whether an equivalent shared-directory path is already listed.
	static bool		IsSharedDirectoryListed(const CString &dir);
	/// Copies the live monitored-root list into a caller-owned snapshot.
	static void		CopyMonitoredSharedRootList(CStringList &out);
	/// Replaces the live monitored-root list with a prepared snapshot.
	static void		ReplaceMonitoredSharedRootList(const CStringList &in);
	/// Adds one monitored shared root if no equivalent path is already listed.
	static bool		AddMonitoredSharedRootIfAbsent(const CString &dir);
	/// Removes one monitored shared root if it is currently listed.
	static bool		RemoveMonitoredSharedRoot(const CString &dir);
	/// Checks whether an equivalent monitored-root path is already listed.
	static bool		IsMonitoredSharedRootListed(const CString &dir);
	/// Copies the live monitor-owned descendant list into a caller-owned snapshot.
	static void		CopyMonitorOwnedDirectoryList(CStringList &out);
	/// Replaces the live monitor-owned descendant list with a prepared snapshot.
	static void		ReplaceMonitorOwnedDirectoryList(const CStringList &in);
	/// Adds one monitor-owned descendant if no equivalent path is already listed.
	static bool		AddMonitorOwnedDirectoryIfAbsent(const CString &dir);
	/// Removes one monitor-owned descendant if it is currently listed.
	static bool		RemoveMonitorOwnedDirectory(const CString &dir);
	/// Removes all monitor-owned descendants that live below the specified root.
	static bool		RemoveMonitorOwnedDirectoriesUnderRoot(const CString &dir);
	/// Checks whether an equivalent monitor-owned descendant path is already listed.
	static bool		IsMonitorOwnedDirectoryListed(const CString &dir);
	static bool		CanFSHandleLargeFiles(int nForCat);
	static LPCTSTR	GetConfigFile();
	static const CString& GetFileCommentsFilePath()		{ return m_strFileCommentsFilePath; }
	static CString	GetMuleDirectory(EDefaultDirectory eDirectory, bool bCreate = true);
	static void		SetMuleDirectory(EDefaultDirectory eDirectory, const CString &strNewDir);
	/// Clears cached default directories after the startup diagnostics base directory changes.
	static void		ResetDefaultDirectoryCache();
	static void		ChangeUserDirMode(int nNewMode);

	static bool		IsTempFile(const CString &rstrDirectory, const CString &rstrName);
	static bool		IsShareableDirectory(const CString &rstrDir);
	static bool		IsInstallationDirectory(const CString &rstrDir);

	static bool		Save();
	static void		SaveCats();
	/**
	 * @brief Reloads user-authored shared-file ignore rules from shareignore.dat.
	 */
	static void		ReloadSharedIgnoreRules();
	/**
	 * @brief Reloads startup server.met update URLs from addresses.dat.
	 */
	static void		ReloadServerMetAddressList();

	static bool		GetUseServerPriorities()			{ return m_bUseServerPriorities; }
	static bool		GetUseUserSortedServerList()		{ return m_bUseUserSortedServerList; }
	static bool		Reconnect()							{ return reconnect; }
	static const CString& GetUserNick()					{ return strNick; }
	static void		SetUserNick(LPCTSTR pszNick);
	static int		GetMaxUserNickLength()				{ return 50; }

	static LPCSTR	GetBindAddrA()						{ return m_pszBindAddrA; }
	static LPCWSTR	GetBindAddrW()						{ return m_pszBindAddrW; }
	static const CString& GetConfiguredBindAddr()		{ return m_strConfiguredBindAddr; }
	static const CString& GetBindInterface()			{ return m_strBindInterface; }
	static const CString& GetBindInterfaceName()		{ return m_strBindInterfaceName; }
	static EBindAddressResolveResult GetBindAddressResolveResult() { return m_eActiveBindAddrResolveResult; }
	static bool		IsVpnGuardEnabled()					{ return m_eVpnGuardMode == VpnGuardSeams::EMode::Block; }
	static VpnGuardSeams::EMode GetVpnGuardMode()		{ return m_eVpnGuardMode; }
	static void		SetVpnGuardMode(VpnGuardSeams::EMode eMode) { m_eVpnGuardMode = eMode; }
	static const CString& GetVpnGuardAllowedPublicIpCidrs() { return m_strVpnGuardAllowedPublicIpCidrs; }
	static void		SetVpnGuardAllowedPublicIpCidrs(const CString& strCidrs) { m_strVpnGuardAllowedPublicIpCidrs = strCidrs; }
	static bool		IsStartupBindBlockEnabled()			{ return IsVpnGuardEnabled(); }
	static const CString& GetActiveConfiguredBindAddr()	{ return m_strActiveConfiguredBindAddr; }
	static const CString& GetActiveBindInterface()		{ return m_strActiveBindInterface; }
	static const CString& GetActiveBindInterfaceName()	{ return m_strActiveBindInterfaceName; }
	static DWORD	GetActiveBindInterfaceIndex()		{ return m_dwActiveBindInterfaceIndex; }
	static EBindAddressResolveResult GetActiveBindAddressResolveResult() { return m_eActiveBindAddrResolveResult; }
	static bool		IsActiveStartupBindBlockEnabled()	{ return m_bActiveStartupBindBlockEnabled; }
#ifdef UNICODE
#define GetBindAddr  GetBindAddrW
#else
#define GetBindAddr  GetBindAddrA
#endif // !UNICODE
	static void		SetBindNetworkSelection(const CString &strInterfaceName, const CString &strAddress);

	static uint16	GetPort()							{ return port; }
	static uint16	GetUDPPort()						{ return udpport; }
	static uint16	GetServerUDPPort()					{ return nServerUDPPort; }
	static uchar*	GetUserHash()						{ return userhash; }
	static uint32	GetConfiguredMaxUpload()			{ return m_maxupload; }
	static uint32	GetSessionMaxUpload()				{ return m_uSessionMaxUpload; }
	static bool		HasSessionMaxUpload()				{ return m_uSessionMaxUpload != 0; }
	static uint32	GetMaxUpload()						{ return m_uSessionMaxUpload != 0 ? m_uSessionMaxUpload : m_maxupload; }
	static bool		IsICHEnabled()						{ return ICH; }
	static bool		GetAutoUpdateServerList()			{ return m_bAutoUpdateServerList; }
	static bool		UpdateNotify()						{ return m_bUpdateNotify; }
	static void		SetUpdateNotify(bool bUpdateNotify)	{ m_bUpdateNotify = bUpdateNotify; }
	static bool		GetMinToTray()						{ return mintotray; }
	static bool		DoAutoConnect()						{ return autoconnect; }
	static void		SetAutoConnect(bool inautoconnect)	{ autoconnect = inautoconnect; }
	static bool		GetAddServersFromServer()			{ return m_bAddServersFromServer; }
	static bool		GetAddServersFromClients()			{ return m_bAddServersFromClients; }
	static bool*	GetMinTrayPTR()						{ return &mintotray; }
	static UINT		GetTrafficOMeterInterval()			{ return trafficOMeterInterval; }
	static UINT		GetDefaultTrafficOMeterInterval()	{ return 3; }
	static UINT		GetMaxTrafficOMeterInterval()		{ return 200; }
	static UINT		NormalizeTrafficOMeterInterval(UINT in);
	static void		SetTrafficOMeterInterval(UINT in);
	static UINT		GetStatsInterval()					{ return statsInterval; }
	static UINT		GetDefaultStatsInterval()			{ return 5; }
	static UINT		GetMaxStatsInterval()				{ return 200; }
	static UINT		NormalizeStatsInterval(UINT in);
	static void		SetStatsInterval(UINT in);
	static bool		GetFillGraphs()						{ return m_bFillGraphs; }
	static void		SetFillGraphs(bool bFill)			{ m_bFillGraphs = bFill; }

	// -khaos--+++> Many, many, many, many methods.
	static void		SaveStats(int bBackUp = 0);
	static void		SetRecordStructMembers();
	static void		SaveCompletedDownloadsStat();
	static bool		LoadStats(int loadBackUp = 0);
	static void		ResetCumulativeStatistics();

	static void		Add2DownCompletedFiles()			{ ++cumDownCompletedFiles; }
	static void		SetConnMaxAvgDownRate(float in)		{ cumConnMaxAvgDownRate = in; }
	static void		SetConnMaxDownRate(float in)		{ cumConnMaxDownRate = in; }
	static void		SetConnAvgUpRate(float in)			{ cumConnAvgUpRate = in; }
	static void		SetConnMaxAvgUpRate(float in)		{ cumConnMaxAvgUpRate = in; }
	static void		SetConnMaxUpRate(float in)			{ cumConnMaxUpRate = in; }
	static void		SetConnPeakConnections(int in)		{ cumConnPeakConnections = in; }
	static void		SetUpAvgTime(int in)				{ cumUpAvgTime = in; }
	static void		Add2DownSAvgTime(int in)			{ sesDownAvgTime += in; }
	static void		SetDownCAvgTime(int in)				{ cumDownAvgTime = in; }
	static void		Add2ConnTransferTime(int in)		{ cumConnTransferTime += in; }
	static void		Add2ConnDownloadTime(int in)		{ cumConnDownloadTime += in; }
	static void		Add2ConnUploadTime(int in)			{ cumConnUploadTime += in; }
	static void		Add2DownSessionCompletedFiles()		{ ++sesDownCompletedFiles; }
	static void		Add2SessionTransferData(UINT uClientID, UINT uClientPort, BOOL bFromPF, BOOL bUpDown, uint32 bytes, bool sentToFriend = false);
	static void		Add2DownSuccessfulSessions()		{ ++sesDownSuccessfulSessions;
														  ++cumDownSuccessfulSessions; }
	static void		Add2DownFailedSessions()			{ ++sesDownFailedSessions;
														  ++cumDownFailedSessions; }
	static void		Add2LostFromCorruption(uint64 in)	{ sesLostFromCorruption += in; }
	static void		Add2SavedFromCompression(uint64 in)	{ sesSavedFromCompression += in; }
	static void		Add2SessionPartsSavedByICH(int in)	{ sesPartsSavedByICH += in; }

	// Saved stats for cumulative downline overhead
	static uint64	GetDownOverheadTotal()				{ return cumDownOverheadTotal; }
	static uint64	GetDownOverheadFileReq()			{ return cumDownOverheadFileReq; }
	static uint64	GetDownOverheadSrcEx()				{ return cumDownOverheadSrcEx; }
	static uint64	GetDownOverheadServer()				{ return cumDownOverheadServer; }
	static uint64	GetDownOverheadKad()				{ return cumDownOverheadKad; }
	static uint64	GetDownOverheadTotalPackets()		{ return cumDownOverheadTotalPackets; }
	static uint64	GetDownOverheadFileReqPackets()		{ return cumDownOverheadFileReqPackets; }
	static uint64	GetDownOverheadSrcExPackets()		{ return cumDownOverheadSrcExPackets; }
	static uint64	GetDownOverheadServerPackets()		{ return cumDownOverheadServerPackets; }
	static uint64	GetDownOverheadKadPackets()			{ return cumDownOverheadKadPackets; }

	// Saved stats for cumulative upline overhead
	static uint64	GetUpOverheadTotal()				{ return cumUpOverheadTotal; }
	static uint64	GetUpOverheadFileReq()				{ return cumUpOverheadFileReq; }
	static uint64	GetUpOverheadSrcEx()				{ return cumUpOverheadSrcEx; }
	static uint64	GetUpOverheadServer()				{ return cumUpOverheadServer; }
	static uint64	GetUpOverheadKad()					{ return cumUpOverheadKad; }
	static uint64	GetUpOverheadTotalPackets()			{ return cumUpOverheadTotalPackets; }
	static uint64	GetUpOverheadFileReqPackets()		{ return cumUpOverheadFileReqPackets; }
	static uint64	GetUpOverheadSrcExPackets()			{ return cumUpOverheadSrcExPackets; }
	static uint64	GetUpOverheadServerPackets()		{ return cumUpOverheadServerPackets; }
	static uint64	GetUpOverheadKadPackets()			{ return cumUpOverheadKadPackets; }

	// Saved stats for cumulative upline data
	static uint32	GetUpSuccessfulSessions()			{ return cumUpSuccessfulSessions; }
	static uint32	GetUpFailedSessions()				{ return cumUpFailedSessions; }
	static uint32	GetUpAvgTime()						{ return cumUpSuccessfulSessions ? cumConnUploadTime / cumUpSuccessfulSessions : 0; }

	// Saved stats for cumulative downline data
	static uint32	GetDownCompletedFiles()				{ return cumDownCompletedFiles; }
	static uint32	GetDownC_SuccessfulSessions()		{ return cumDownSuccessfulSessions; }
	static uint32	GetDownC_FailedSessions()			{ return cumDownFailedSessions; }
	static uint32	GetDownC_AvgTime()					{ return cumDownSuccessfulSessions ? cumConnDownloadTime / cumDownSuccessfulSessions : 0; }

	// Session download stats
	static uint32	GetDownSessionCompletedFiles()		{ return sesDownCompletedFiles; }
	static uint32	GetDownS_SuccessfulSessions()		{ return sesDownSuccessfulSessions; }
	static uint32	GetDownS_FailedSessions()			{ return sesDownFailedSessions; }
	static uint32	GetDownS_AvgTime()					{ return sesDownSuccessfulSessions ? sesDownAvgTime / sesDownSuccessfulSessions : 0; }

	// Saved stats for corruption/compression
	static uint64	GetCumLostFromCorruption()			{ return cumLostFromCorruption; }
	static uint64	GetCumSavedFromCompression()		{ return cumSavedFromCompression; }
	static uint64	GetSesLostFromCorruption()			{ return sesLostFromCorruption; }
	static uint64	GetSesSavedFromCompression()		{ return sesSavedFromCompression; }
	static uint32	GetCumPartsSavedByICH()				{ return cumPartsSavedByICH; }
	static uint32	GetSesPartsSavedByICH()				{ return sesPartsSavedByICH; }

	// Cumulative client breakdown stats for sent bytes
	static uint64	GetUpTotalClientData()				{ return  GetCumUpData_EDONKEY()
																+ GetCumUpData_EDONKEYHYBRID()
																+ GetCumUpData_EMULE()
																+ GetCumUpData_MLDONKEY()
																+ GetCumUpData_AMULE()
																+ GetCumUpData_EMULECOMPAT()
																+ GetCumUpData_SHAREAZA(); }
	static uint64	GetCumUpData_EDONKEY()				{ return cumUpData_EDONKEY +		sesUpData_EDONKEY; }
	static uint64	GetCumUpData_EDONKEYHYBRID()		{ return cumUpData_EDONKEYHYBRID +	sesUpData_EDONKEYHYBRID; }
	static uint64	GetCumUpData_EMULE()				{ return cumUpData_EMULE +			sesUpData_EMULE; }
	static uint64	GetCumUpData_MLDONKEY()				{ return cumUpData_MLDONKEY +		sesUpData_MLDONKEY; }
	static uint64	GetCumUpData_AMULE()				{ return cumUpData_AMULE +			sesUpData_AMULE; }
	static uint64	GetCumUpData_EMULECOMPAT()			{ return cumUpData_EMULECOMPAT +	sesUpData_EMULECOMPAT; }
	static uint64	GetCumUpData_SHAREAZA()				{ return cumUpData_SHAREAZA +		sesUpData_SHAREAZA; }

	// Session client breakdown stats for sent bytes
	static uint64	GetUpSessionClientData()			{ return  sesUpData_EDONKEY
																+ sesUpData_EDONKEYHYBRID
																+ sesUpData_EMULE
																+ sesUpData_MLDONKEY
																+ sesUpData_AMULE
																+ sesUpData_EMULECOMPAT
																+ sesUpData_SHAREAZA; }
	static uint64	GetUpData_EDONKEY()					{ return sesUpData_EDONKEY; }
	static uint64	GetUpData_EDONKEYHYBRID()			{ return sesUpData_EDONKEYHYBRID; }
	static uint64	GetUpData_EMULE()					{ return sesUpData_EMULE; }
	static uint64	GetUpData_MLDONKEY()				{ return sesUpData_MLDONKEY; }
	static uint64	GetUpData_AMULE()					{ return sesUpData_AMULE; }
	static uint64	GetUpData_EMULECOMPAT()				{ return sesUpData_EMULECOMPAT; }
	static uint64	GetUpData_SHAREAZA()				{ return sesUpData_SHAREAZA; }

	// Cumulative port breakdown stats for sent bytes...
	static uint64	GetUpTotalPortData()				{ return  GetCumUpDataPort_4662()
																+ GetCumUpDataPort_OTHER(); }
	static uint64	GetCumUpDataPort_4662()				{ return cumUpDataPort_4662 +		sesUpDataPort_4662; }
	static uint64	GetCumUpDataPort_OTHER()			{ return cumUpDataPort_OTHER +		sesUpDataPort_OTHER; }

	// Session port breakdown stats for sent bytes...
	static uint64	GetUpSessionPortData()				{ return  sesUpDataPort_4662
																+ sesUpDataPort_OTHER; }
	static uint64	GetUpDataPort_4662()				{ return sesUpDataPort_4662; }
	static uint64	GetUpDataPort_OTHER()				{ return sesUpDataPort_OTHER; }

	// Cumulative DS breakdown stats for sent bytes...
	static uint64	GetUpTotalDataFile()				{ return GetCumUpData_File() +	GetCumUpData_Partfile(); }
	static uint64	GetCumUpData_File()					{ return cumUpData_File +		sesUpData_File; }
	static uint64	GetCumUpData_Partfile()				{ return cumUpData_Partfile +	sesUpData_Partfile; }
	// Session DS breakdown stats for sent bytes...
	static uint64	GetUpSessionDataFile()				{ return sesUpData_File +		sesUpData_Partfile; }
	static uint64	GetUpData_File()					{ return sesUpData_File; }
	static uint64	GetUpData_Partfile()				{ return sesUpData_Partfile; }

	// Cumulative client breakdown stats for received bytes
	static uint64	GetDownTotalClientData()			{ return  GetCumDownData_EDONKEY()
																+ GetCumDownData_EDONKEYHYBRID()
																+ GetCumDownData_EMULE()
																+ GetCumDownData_MLDONKEY()
																+ GetCumDownData_AMULE()
																+ GetCumDownData_EMULECOMPAT()
																+ GetCumDownData_SHAREAZA(); }
	static uint64	GetCumDownData_EDONKEY()			{ return cumDownData_EDONKEY +		sesDownData_EDONKEY; }
	static uint64	GetCumDownData_EDONKEYHYBRID()		{ return cumDownData_EDONKEYHYBRID + sesDownData_EDONKEYHYBRID; }
	static uint64	GetCumDownData_EMULE()				{ return cumDownData_EMULE +		sesDownData_EMULE; }
	static uint64	GetCumDownData_MLDONKEY()			{ return cumDownData_MLDONKEY +		sesDownData_MLDONKEY; }
	static uint64	GetCumDownData_AMULE()				{ return cumDownData_AMULE +		sesDownData_AMULE; }
	static uint64	GetCumDownData_EMULECOMPAT()		{ return cumDownData_EMULECOMPAT +	sesDownData_EMULECOMPAT; }
	static uint64	GetCumDownData_SHAREAZA()			{ return cumDownData_SHAREAZA +		sesDownData_SHAREAZA; }

	// Session client breakdown stats for received bytes
	static uint64	GetDownSessionClientData()			{ return  sesDownData_EDONKEY
																+ sesDownData_EDONKEYHYBRID
																+ sesDownData_EMULE
																+ sesDownData_MLDONKEY
																+ sesDownData_AMULE
																+ sesDownData_EMULECOMPAT
																+ sesDownData_SHAREAZA; }
	static uint64	GetDownData_EDONKEY()				{ return sesDownData_EDONKEY; }
	static uint64	GetDownData_EDONKEYHYBRID()			{ return sesDownData_EDONKEYHYBRID; }
	static uint64	GetDownData_EMULE()					{ return sesDownData_EMULE; }
	static uint64	GetDownData_MLDONKEY()				{ return sesDownData_MLDONKEY; }
	static uint64	GetDownData_AMULE()					{ return sesDownData_AMULE; }
	static uint64	GetDownData_EMULECOMPAT()			{ return sesDownData_EMULECOMPAT; }
	static uint64	GetDownData_SHAREAZA()				{ return sesDownData_SHAREAZA; }

	// Cumulative port breakdown stats for received bytes...
	static uint64	GetDownTotalPortData()				{ return  GetCumDownDataPort_4662()
																+ GetCumDownDataPort_OTHER(); }
	static uint64	GetCumDownDataPort_4662()			{ return cumDownDataPort_4662		+ sesDownDataPort_4662; }
	static uint64	GetCumDownDataPort_OTHER()			{ return cumDownDataPort_OTHER		+ sesDownDataPort_OTHER; }

	// Session port breakdown stats for received bytes...
	static uint64	GetDownSessionDataPort()			{ return   sesDownDataPort_4662
																+ sesDownDataPort_OTHER; }
	static uint64	GetDownDataPort_4662()				{ return sesDownDataPort_4662; }
	static uint64	GetDownDataPort_OTHER()				{ return sesDownDataPort_OTHER; }

	// Saved stats for cumulative connection data
	static float	GetConnAvgDownRate()				{ return cumConnAvgDownRate; }
	static float	GetConnMaxAvgDownRate()				{ return cumConnMaxAvgDownRate; }
	static float	GetConnMaxDownRate()				{ return cumConnMaxDownRate; }
	static float	GetConnAvgUpRate()					{ return cumConnAvgUpRate; }
	static float	GetConnMaxAvgUpRate()				{ return cumConnMaxAvgUpRate; }
	static float	GetConnMaxUpRate()					{ return cumConnMaxUpRate; }
	static time_t	GetConnRunTime()					{ return cumConnRunTime; }
	static uint32	GetConnNumReconnects()				{ return cumConnNumReconnects; }
	static uint32	GetConnAvgConnections()				{ return cumConnAvgConnections; }
	static uint32	GetConnMaxConnLimitReached()		{ return cumConnMaxConnLimitReached; }
	static uint32	GetConnPeakConnections()			{ return cumConnPeakConnections; }
	static uint32	GetConnTransferTime()				{ return cumConnTransferTime; }
	static uint32	GetConnDownloadTime()				{ return cumConnDownloadTime; }
	static uint32	GetConnUploadTime()					{ return cumConnUploadTime; }
	static uint32	GetConnServerDuration()				{ return cumConnServerDuration; }

	// Saved records for servers / network
	static uint32	GetSrvrsMostWorkingServers()		{ return cumSrvrsMostWorkingServers; }
	static uint32	GetSrvrsMostUsersOnline()			{ return cumSrvrsMostUsersOnline; }
	static uint32	GetSrvrsMostFilesAvail()			{ return cumSrvrsMostFilesAvail; }

	// Saved records for shared files
	static uint32	GetSharedMostFilesShared()			{ return cumSharedMostFilesShared; }
	static uint64	GetSharedLargestShareSize()			{ return cumSharedLargestShareSize; }
	static uint64	GetSharedLargestAvgFileSize()		{ return cumSharedLargestAvgFileSize; }
	static uint64	GetSharedLargestFileSize()			{ return cumSharedLargestFileSize; }

	// Get the long date/time when the stats were last reset
	static time_t	GetStatsLastResetLng()				{ return stat_datetimeLastReset; }
	static CString	GetStatsLastResetStr(bool formatLong = true);
	static UINT		GetStatsSaveInterval()				{ return statsSaveInterval; }

	// Get and Set our new preferences
	static UINT		GetDefaultStatsMax()				{ return 100; }
	static UINT		NormalizeStatsMax(UINT in);
	static void		SetStatsMax(UINT in);
	static UINT		GetDefaultStatsConnectionsGraphRatio() { return 3; }
	static UINT		NormalizeStatsConnectionsGraphRatio(UINT in);
	static void		SetStatsConnectionsGraphRatio(UINT in);
	static UINT		GetStatsConnectionsGraphRatio()		{ return statsConnectionsGraphRatio; }
	static void		SetExpandedTreeItems(const CString &in)	{ m_strStatsExpandedTreeItems = in; }
	static const CString& GetExpandedTreeItems()		{ return m_strStatsExpandedTreeItems; }

	static uint64	GetTotalDownloaded()				{ return totalDownloadedBytes; }
	static uint64	GetTotalUploaded()					{ return totalUploadedBytes; }

	static bool		IsErrorBeepEnabled()				{ return beepOnError; }
	static bool		IsConfirmExitEnabled()				{ return confirmExit; }
	static void		SetConfirmExit(bool bVal)			{ confirmExit = bVal; }
	static int		NormalizeLifecycleProgressDialogMode(int iMode);
	static int		GetDefaultLifecycleProgressDialogMode() { return LPDM_ALWAYS; }
	static int		GetStartupProgressDialogMode()		{ return m_iStartupProgressDialogMode; }
	static void		SetStartupProgressDialogMode(int iMode) { m_iStartupProgressDialogMode = NormalizeLifecycleProgressDialogMode(iMode); }
	static int		GetShutdownProgressDialogMode()		{ return m_iShutdownProgressDialogMode; }
	static void		SetShutdownProgressDialogMode(int iMode) { m_iShutdownProgressDialogMode = NormalizeLifecycleProgressDialogMode(iMode); }
	static LPCTSTR	GetDailyConfigBackupIniKey()		{ return _T("DailyConfigBackup"); }
	static bool		GetDefaultDailyConfigBackup()		{ return true; }
	static bool		GetDailyConfigBackup()				{ return m_bDailyConfigBackup; }
	static void		SetDailyConfigBackup(bool bEnabled) { m_bDailyConfigBackup = bEnabled; }
	static UINT		GetDefaultDesktopUiRefreshIntervalMs() { return DESKTOP_UI_REFRESH_BELOWNORMAL_MS; }
	static LPCTSTR	GetDesktopUiRefreshIntervalIniKey() { return _T("DesktopUiRefreshIntervalMs"); }
	static UINT		GetDesktopUiRefreshIntervalMs()		{ return m_uDesktopUiRefreshIntervalMs; }
	static void		SetDesktopUiRefreshIntervalMs(UINT uIntervalMs) { m_uDesktopUiRefreshIntervalMs = NormalizeDesktopUiRefreshIntervalMs(uIntervalMs); }
	static bool		FilterLANIPs()						{ return filterLANIPs; }
	static bool		GetAllowLocalHostIP()				{ return m_bAllocLocalHostIP; }
	static bool		IsOnlineSignatureEnabled()			{ return onlineSig; }
	static uint32	GetMaxGraphDownloadRate()			{ return maxGraphDownloadRate; }
	static void		SetMaxGraphDownloadRate(uint32 in);

	static uint32	GetConfiguredMaxDownload()			{ return m_maxdownload; }
	static uint32	GetSessionMaxDownload()				{ return m_uSessionMaxDownload; }
	static bool		HasSessionMaxDownload()				{ return m_uSessionMaxDownload != 0; }
	static uint32	GetMaxDownload();
	static uint64	GetMaxDownloadInBytesPerSec(bool dynamic = false);
	static UINT		GetMaxConnections()					{ return maxconnections; }
	static UINT		GetMaxHalfConnections()				{ return maxhalfconnections; }
	static UINT		GetDefaultMaxHalfConnections()		{ return 50; }
	static UINT		GetMaxSourcePerFileDefault()		{ return GetDefaultMaxSourcesPerFile(); }
	static UINT		GetConfiguredMaxSourcesPerFile()	{ return maxsourceperfile; }
	static UINT		GetDefaultMaxSourcesPerFile()		{ return 600; }
	static UINT		GetDeadServerRetries()				{ return m_uDeadServerRetries; }
	static DWORD	GetServerKeepAliveTimeout()			{ return m_dwServerKeepAliveTimeout; }
	static DWORD	GetConnectionTimeout()				{ return m_dwConnectionTimeout; }
	static DWORD	GetDownloadTimeout()				{ return m_dwDownloadTimeout; }
	static UINT		GetEd2kSearchMaxResults()			{ return m_uEd2kSearchMaxResults; }
	static UINT		GetEd2kSearchMaxMoreRequests()		{ return m_uEd2kSearchMaxMoreRequests; }
	static UINT		GetKadFileSearchTotal()			{ return m_uKadFileSearchTotal; }
	static UINT		GetKadKeywordSearchTotal()		{ return m_uKadKeywordSearchTotal; }
	static UINT		GetKadFileSearchLifetimeSeconds()	{ return m_uKadFileSearchLifetimeSeconds; }
	static UINT		GetKadKeywordSearchLifetimeSeconds() { return m_uKadKeywordSearchLifetimeSeconds; }
	static bool		IsDetectTCPErrorFlooder()			{ return m_bDetectTCPErrorFlooder; }
	static UINT		GetTCPErrorFlooderIntervalMinutes() { return m_uTCPErrorFlooderIntervalMinutes; }
	static UINT		GetTCPErrorFlooderThreshold()		{ return m_uTCPErrorFlooderThreshold; }
	static bool		GetConditionalTCPAccept()			{ return m_bConditionalTCPAccept; }

	static LANGID	GetLanguageID()						{ return m_wLanguageID; }
	static void		SetLanguageID(LANGID lid)			{ m_wLanguageID = lid; }
	static void		GetLanguages(CWordArray &aLanguageIDs);
	static void		SetLanguage();
	static bool		IsLanguageSupported(LANGID lidSelected);
	static CString	GetLangDLLNameByID(LANGID lidSelected);
	static void		InitThreadLocale();
	static CString GetHtmlCharset();

	static bool		IsDoubleClickEnabled()				{ return transferDoubleclick; }
	static EViewSharedFilesAccess CanSeeShares()		{ return m_iSeeShares; }
	static UINT		GetToolTipDelay()					{ return m_iToolDelayTime; }
	static UINT		GetDefaultToolTipDelaySeconds()		{ return 1; }
	static UINT		GetMaxToolTipDelaySeconds()			{ return 32; }
	static UINT		NormalizeToolTipDelaySeconds(UINT in);
	static void		SetToolTipDelay(UINT in);
	static bool		IsBringToFront()					{ return bringtoforeground; }

	static UINT		GetSplitterbarPosition()			{ return splitterbarPosition; }
	static void		SetSplitterbarPosition(UINT pos)	{ splitterbarPosition = pos; }
	static UINT		GetSplitterbarPositionServer()		{ return splitterbarPositionSvr; }
	static void		SetSplitterbarPositionServer(UINT pos) { splitterbarPositionSvr = pos; }
	static UINT		GetTransferWnd1()					{ return m_uTransferWnd1; }
	static void		SetTransferWnd1(UINT uWnd1)			{ m_uTransferWnd1 = uWnd1; }
	static UINT		GetTransferWnd2()					{ return m_uTransferWnd2; }
	static void		SetTransferWnd2(UINT uWnd2)			{ m_uTransferWnd2 = uWnd2; }
	//MORPH START - Added by SiRoB, Splitting Bar [O²]
	static UINT		GetSplitterbarPositionStat()		{ return splitterbarPositionStat; }
	static void		SetSplitterbarPositionStat(UINT pos) { splitterbarPositionStat = pos; }
	static UINT		GetSplitterbarPositionStat_HL()		{ return splitterbarPositionStat_HL; }
	static void		SetSplitterbarPositionStat_HL(UINT pos)	{ splitterbarPositionStat_HL = pos; }
	static UINT		GetSplitterbarPositionStat_HR()		{ return splitterbarPositionStat_HR; }
	static void		SetSplitterbarPositionStat_HR(UINT pos)	{ splitterbarPositionStat_HR = pos; }
	static UINT		GetSplitterbarPositionFriend()		{ return splitterbarPositionFriend; }
	static void		SetSplitterbarPositionFriend(UINT pos)	{ splitterbarPositionFriend = pos; }
	static UINT		GetSplitterbarPositionIRC()			{ return splitterbarPositionIRC; }
	static void		SetSplitterbarPositionIRC(UINT pos)	{ splitterbarPositionIRC = pos; }
	static UINT		GetSplitterbarPositionShared()		{ return splitterbarPositionShared; }
	static void		SetSplitterbarPositionShared(UINT pos)	{ splitterbarPositionShared = pos; }
	//MORPH END   - Added by SiRoB, Splitting Bar [O²]
	// -khaos--+++> Changed datatype to avoid overflows
	static UINT		GetStatsMax()						{ return statsMax; }
	// <-----khaos-
	static bool		UseFlatBar()						{ return !depth3D; }
	static int		GetStraightWindowStyles()			{ return m_iStraightWindowStyles; }
	static bool		GetUseSystemFontForMainControls()	{ return m_bUseSystemFontForMainControls; }

	static const CString& GetSkinProfile()				{ return m_strSkinProfile; }
	static void		SetSkinProfile(LPCTSTR pszProfile)	{ m_strSkinProfile = pszProfile; }

	static UINT		GetStatsAverageMinutes()			{ return statsAverageMinutes; }
	static UINT		GetDefaultStatsAverageMinutes()		{ return 5; }
	static UINT		GetMaxStatsAverageMinutes()			{ return 100; }
	static UINT		NormalizeStatsAverageMinutes(UINT in);
	static void		SetStatsAverageMinutes(UINT in);

	static const CString& GetNotifierConfiguration()	{ return notifierConfiguration; }
	static void		SetNotifierConfiguration(LPCTSTR pszConfigPath)	{ notifierConfiguration = pszConfigPath; }
	static bool		GetNotifierOnDownloadFinished()		{ return notifierOnDownloadFinished; }
	static bool		GetNotifierOnNewDownload()			{ return notifierOnNewDownload; }
	static bool		GetNotifierOnChat()					{ return notifierOnChat; }
	static bool		GetNotifierOnLog()					{ return notifierOnLog; }
	static bool		GetNotifierOnImportantError()		{ return notifierOnImportantError; }
	static bool		GetNotifierOnEveryChatMsg()			{ return notifierOnEveryChatMsg; }
	static bool		GetNotifierOnNewVersion()			{ return notifierOnNewVersion; }
	static ENotifierDisplayMode GetNotifierDisplayMode() { return notifierDisplayMode; }
	static ENotifierSoundType GetNotifierSoundType()	{ return notifierSoundType; }
	static const CString& GetNotifierSoundFile()		{ return notifierSoundFile; }

	static bool		GetRTLWindowsLayout()				{ return m_bRTLWindowsLayout; }

	static const CString& GetIRCNick()					{ return m_strIRCNick; }
	static void		SetIRCNick(LPCTSTR pszNick)			{ m_strIRCNick = pszNick; }
	static const CString& GetIRCServer()				{ return m_strIRCServer; }
	static bool		GetIRCAddTimeStamp()				{ return m_bIRCAddTimeStamp; }
	static bool		GetIRCUseChannelFilter()			{ return m_bIRCUseChannelFilter; }
	static const CString& GetIRCChannelFilter()			{ return m_strIRCChannelFilter; }
	static UINT		GetIRCChannelUserFilter()			{ return m_uIRCChannelUserFilter; }
	static bool		GetIRCUsePerform()					{ return m_bIRCUsePerform; }
	static const CString& GetIRCPerformString()			{ return m_strIRCPerformString; }
	static bool		GetIRCJoinHelpChannel()				{ return m_bIRCJoinHelpChannel; }
	static bool		GetIRCGetChannelsOnConnect()		{ return m_bIRCGetChannelsOnConnect; }
	static bool		GetIRCPlaySoundEvents()				{ return m_bIRCPlaySoundEvents; }
	static bool		GetIRCIgnoreMiscMessages()			{ return m_bIRCIgnoreMiscMessages; }
	static bool		GetIRCIgnoreJoinMessages()			{ return m_bIRCIgnoreJoinMessages; }
	static bool		GetIRCIgnorePartMessages()			{ return m_bIRCIgnorePartMessages; }
	static bool		GetIRCIgnoreQuitMessages()			{ return m_bIRCIgnoreQuitMessages; }
	static bool		GetIRCIgnorePingPongMessages()		{ return m_bIRCIgnorePingPongMessages; }
	static bool		GetIRCIgnoreEmuleAddFriendMsgs()	{ return m_bIRCIgnoreEmuleAddFriendMsgs; }
	static bool		GetIRCIgnoreEmuleSendLinkMsgs()		{ return m_bIRCIgnoreEmuleSendLinkMsgs; }
	static bool		GetIRCAllowEmuleAddFriend()			{ return m_bIRCAllowEmuleAddFriend; }
	static bool		GetIRCAcceptLinks()					{ return m_bIRCAcceptLinks; }
	static bool		GetIRCAcceptLinksFriendsOnly()		{ return m_bIRCAcceptLinksFriendsOnly; }
	static bool		GetIRCEnableSmileys()				{ return m_bIRCEnableSmileys; }
	static bool		GetMessageEnableSmileys()			{ return m_bMessageEnableSmileys; }
	static bool		GetIRCEnableUTF8()					{ return m_bIRCEnableUTF8; }

	static bool		IsRunningAeroGlassTheme();
	static bool		GetStartMinimized()					{ return startMinimized; }
	static void		SetStartMinimized( bool instartMinimized) { startMinimized = instartMinimized; }
	static bool		GetAutoStart()						{ return m_bAutoStart; }
	static void		SetAutoStart( bool val)				{ m_bAutoStart = val; }

	static bool		GetRestoreLastMainWndDlg()			{ return m_bRestoreLastMainWndDlg; }
	static int		GetLastMainWndDlgID()				{ return m_iLastMainWndDlgID; }
	static void		SetLastMainWndDlgID(int iID)		{ m_iLastMainWndDlgID = iID; }

	static bool		GetRestoreLastLogPane()				{ return m_bRestoreLastLogPane; }
	static int		GetLastLogPaneID()					{ return m_iLastLogPaneID; }
	static void		SetLastLogPaneID(int iID)			{ m_iLastLogPaneID = iID; }

	static bool		GetSmartIdCheck()					{ return m_bSmartServerIdCheck; }
	static void		SetSmartIdCheck(bool in_smartidcheck) { m_bSmartServerIdCheck = in_smartidcheck; }
	static uint8	GetSmartIdState()					{ return smartidstate; }
	static void		SetSmartIdState(uint8 in_smartidstate) { smartidstate = in_smartidstate; }
	static bool		GetPreviewPrio()					{ return m_bpreviewprio; }
	static void		SetPreviewPrio(bool in)				{ m_bpreviewprio = in; }
	static bool		GetUpdateQueueList()				{ return m_bupdatequeuelist; }
	static bool		GetManualAddedServersHighPriority()	{ return m_bManualAddedServersHighPriority; }
	static bool		TransferFullChunks()				{ return m_btransferfullchunks; }
	static void		SetTransferFullChunks( bool m_bintransferfullchunks ) { m_btransferfullchunks = m_bintransferfullchunks; }
	static int		StartNextFile()						{ return m_istartnextfile; }
	static bool		ShowOverhead()						{ return m_bshowoverhead; }
	static void		SetNewAutoUp(bool m_bInUAP)			{ m_bUAP = m_bInUAP; }
	static bool		GetNewAutoUp()						{ return m_bUAP; }
	static void		SetNewAutoDown(bool m_bInDAP)		{ m_bDAP = m_bInDAP; }
	static bool		GetNewAutoDown()					{ return m_bDAP; }
	static bool		IsKnownClientListDisabled()			{ return m_bDisableKnownClientList; }
	static bool		IsQueueListDisabled()				{ return m_bDisableQueueList; }
	static bool		IsFirstStart()						{ return m_bFirstStart; }
	static bool		IsFirstTimeWizardDisabled()			{ return m_bDisableFirstTimeWizard; }
	static bool		UseCreditSystem()					{ return m_bCreditSystem; }
	static void		SetCreditSystem(bool m_bInCreditSystem) { m_bCreditSystem = m_bInCreditSystem; }

	static const CString& GetTxtEditor()				{ return m_strTxtEditor; }
	static const CString& GetVideoPlayer()				{ return m_strVideoPlayer; }
	static const CString& GetVideoPlayerArgs()			{ return m_strVideoPlayerArgs; }
	static const CString& GetVideoThumbnailFfmpegPath()	{ return m_strVideoThumbnailFfmpegPath; }
	static UINT		GetVideoThumbnailIntervalSeconds()	{ return m_uVideoThumbnailIntervalSeconds; }
	static bool		IsPeerPreviewAllowed()				{ return m_bAllowPeerPreview; }
	static bool		GetRunCommandOnFileCompletion()		{ return m_bRunCommandOnFileCompletion; }
	static const CString& GetFileCompletionProgram()		{ return m_strFileCompletionProgram; }
	static const CString& GetFileCompletionArguments()	{ return m_strFileCompletionArguments; }

	static UINT		GetFileBufferSize()					{ return m_uFileBufferSize; }
	static UINT		GetDefaultFileBufferSizeBytes()		{ return 64u * 1024u * 1024u; }
	static UINT		GetMinFileBufferSizeBytes()			{ return 16u * 1024u; }
	static UINT		GetMaxFileBufferSizeBytes();
	static UINT		NormalizeFileBufferSizeBytes(UINT bytes);
	static void		SetFileBufferSize(UINT bytes)		{ m_uFileBufferSize = NormalizeFileBufferSizeBytes(bytes); }
	static bool		IsDownloadAutoBroadbandIOEnabled()			{ return m_bDownloadAutoBroadbandIO; }
	static void		SetDownloadAutoBroadbandIOEnabled(bool in)	{ m_bDownloadAutoBroadbandIO = in; }
	static DWORD	GetFileBufferTimeLimit()			{ return m_uFileBufferTimeLimit; }
	static INT_PTR	GetQueueSize()						{ return m_iQueueSize; }
	static INT_PTR	GetDefaultQueueSize()				{ return static_cast<INT_PTR>(PreferenceValidationSeams::kDefaultQueueSize); }
	static INT_PTR	GetMinQueueSize()					{ return static_cast<INT_PTR>(PreferenceValidationSeams::kMinQueueSize); }
	static INT_PTR	GetMaxQueueSize()					{ return static_cast<INT_PTR>(PreferenceValidationSeams::kMaxQueueSize); }
	static INT_PTR	NormalizeQueueSize(INT_PTR size);
	static void		SetQueueSize(INT_PTR size)			{ m_iQueueSize = NormalizeQueueSize(size); }
	static int		GetCommitFiles()					{ return m_iCommitFiles; }
	static bool		GetShowCopyEd2kLinkCmd()			{ return m_bShowCopyEd2kLinkCmd; }

	// Barry
	static UINT		Get3DDepth()						{ return depth3D; }
	static bool		AutoTakeED2KLinks()					{ return autotakeed2klinks; }
	static bool		AddNewFilesPaused()					{ return addnewfilespaused; }

	static bool		TransferlistRemainSortStyle()		{ return m_bTransflstRemain; }
	static void		TransferlistRemainSortStyle(bool in) { m_bTransflstRemain = in; }

	static DWORD	GetStatsColor(int index)			{ return m_adwStatsColors[index]; }
	static void		SetStatsColor(int index, DWORD value) { m_adwStatsColors[index] = value; }
	static int		GetNumStatsColors()					{ return _countof(m_adwStatsColors); }
	static void		GetAllStatsColors(int iCount, LPDWORD pdwColors);
	static bool		SetAllStatsColors(int iCount, const LPDWORD pdwColors);
	static void		ResetStatsColor(int index);
	static bool		HasCustomTaskIconColor()			{ return m_bHasCustomTaskIconColor; }

	static void		SetMaxConsPerFive(UINT in);
	static LPLOGFONT GetHyperTextLogFont()				{ return &m_lfHyperText; }
	static void		SetHyperTextFont(LPLOGFONT plf)		{ m_lfHyperText = *plf; }
	static LPLOGFONT GetLogFont()						{ return &m_lfLogText; }
	static void		SetLogFont(LPLOGFONT plf)			{ m_lfLogText = *plf; }
	static COLORREF GetLogErrorColor()					{ return m_crLogError; }
	static COLORREF GetLogWarningColor()				{ return m_crLogWarning; }
	static COLORREF GetLogSuccessColor()				{ return m_crLogSuccess; }

	static UINT		GetMaxConperFive()					{ return MaxConperFive; }
	static UINT		GetDefaultMaxConperFive();
	static UINT		GetMinTimeoutSeconds()				{ return 5; }
	static UINT		GetDefaultConnectionTimeoutSeconds()	{ return 30; }
	static UINT		GetDefaultDownloadTimeoutSeconds()	{ return 75; }
	static UINT		GetDefaultEd2kSearchMaxResults()	{ return 0; }
	static UINT		GetDefaultEd2kSearchMaxMoreRequests() { return 0; }
	static UINT		GetDefaultKadFileSearchTotal()		{ return 750; }
	static UINT		GetDefaultKadKeywordSearchTotal()	{ return 750; }
	static UINT		GetDefaultKadFileSearchLifetimeSeconds() { return 90; }
	static UINT		GetDefaultKadKeywordSearchLifetimeSeconds() { return 90; }
	static bool		GetDefaultDetectTCPErrorFlooder()	{ return true; }
	static UINT		GetDefaultTCPErrorFlooderIntervalMinutes() { return 60; }
	static UINT		GetDefaultTCPErrorFlooderThreshold() { return 10; }
	static UINT		GetMinKadSearchTotal()				{ return 100; }
	static UINT		GetMaxKadSearchTotal()				{ return 5000; }
	static UINT		GetMinKadSearchLifetimeSeconds()	{ return 30; }
	static UINT		GetMaxKadSearchLifetimeSeconds()	{ return 180; }
	static UINT		GetMinTCPErrorFlooderIntervalMinutes() { return 1; }
	static UINT		GetMaxTCPErrorFlooderIntervalMinutes() { return 1440; }
	static UINT		GetMinTCPErrorFlooderThreshold()	{ return 3; }
	static UINT		GetMaxTCPErrorFlooderThreshold()	{ return 1000; }
	static DWORD	NormalizeTimeoutSeconds(UINT seconds, UINT defaultSeconds);
	static UINT		TimeoutMsToSeconds(DWORD milliseconds) { return milliseconds / SEC2MS(1); }
	static uint16	NormalizePortValue(int value, uint16 defaultPort, bool bAllowZero = false);
	static uint16	NormalizeServerUDPPortValue(int value);
	static UINT		NormalizeRetryCount(UINT uValue, UINT uDefault, UINT uMin, UINT uMax);
	static UINT		NormalizePositivePreference(UINT uValue, UINT uDefault);
	static UINT		NormalizeServerKeepAliveTimeoutMinutes(UINT in);
	static void		SetServerKeepAliveTimeoutMinutes(UINT in);
	static DWORD	NormalizeServerKeepAliveTimeoutMilliseconds(DWORD in);
	static void		SetServerKeepAliveTimeoutMilliseconds(DWORD in);
	static UINT		NormalizeFileBufferTimeLimitSeconds(UINT in);
	static void		SetFileBufferTimeLimitSeconds(UINT in);

	static bool		IsSafeServerConnectEnabled()		{ return m_bSafeServerConnect; }
	static void		SetSafeServerConnectEnabled(bool in) { m_bSafeServerConnect = in; }
	static int		GetSearchResultsFileSizeFormat()	{ return m_iSearchResultsFileSizeFormat; }
	static void		SetSearchResultsFileSizeFormat(int format);
	static bool		IsVideoThumbnailGenerationEnabled()	{ return m_uVideoThumbnailIntervalSeconds > 0; }
	static bool		UseVideoPreviewThumbnails()			{ return IsVideoThumbnailGenerationEnabled(); }
	static DWORD	GetVideoThumbnailIntervalMs()		{ return SEC2MS(m_uVideoThumbnailIntervalSeconds); }
	static int		GetPreviewSmallBlocks()				{ return m_iPreviewSmallBlocks; }
	static bool		GetPreviewCopiedArchives()			{ return m_bPreviewCopiedArchives; }
	static bool		GetInspectAllFileTypes()			{ return m_bInspectAllFileTypes; }
	static const CString& GetMediaInfoDllPath()			{ return m_strMediaInfoDllPath; }
	static void		SetMediaInfoDllPath(const CString &path) { m_strMediaInfoDllPath = path; }
	static int		GetExtractMetaData()				{ return m_iExtractMetaData; }
	static bool		GetRearrangeKadSearchKeywords()		{ return m_bRearrangeKadSearchKeywords; }

	static const CString& GetYourHostname()				{ return m_strYourHostname; }
	static void		SetYourHostname(LPCTSTR pszHostname) { m_strYourHostname = pszHostname; }
	static ULONGLONG GetMinFreeDiskSpaceConfigFloor()	{ return PartFilePersistenceSeams::kMinConfigFreeBytes; }
	static ULONGLONG GetMinFreeDiskSpaceTempFloor()		{ return PartFilePersistenceSeams::kMinTempFreeBytes; }
	static ULONGLONG GetMinFreeDiskSpaceIncomingFloor()	{ return PartFilePersistenceSeams::kMinIncomingFreeBytes; }
	static ULONGLONG GetMaxFreeDiskSpaceFloor()			{ return PartFilePersistenceSeams::kMaxDownloadFreeBytes; }
	static ULONGLONG NormalizeMinFreeDiskSpaceConfig(ULONGLONG nBytes) { return PartFilePersistenceSeams::NormalizeDiskSpaceFloor(nBytes, GetMinFreeDiskSpaceConfigFloor(), GetMaxFreeDiskSpaceFloor()); }
	static ULONGLONG NormalizeMinFreeDiskSpaceTemp(ULONGLONG nBytes) { return PartFilePersistenceSeams::NormalizeDiskSpaceFloor(nBytes, GetMinFreeDiskSpaceTempFloor(), GetMaxFreeDiskSpaceFloor()); }
	static ULONGLONG NormalizeMinFreeDiskSpaceIncoming(ULONGLONG nBytes) { return PartFilePersistenceSeams::NormalizeDiskSpaceFloor(nBytes, GetMinFreeDiskSpaceIncomingFloor(), GetMaxFreeDiskSpaceFloor()); }
	static UINT		NormalizeMinFreeDiskSpaceConfigGiB(UINT nGiB) { return static_cast<UINT>(PartFilePersistenceSeams::NormalizeDiskSpaceFloorGiB(nGiB, PartFilePersistenceSeams::kMinConfigDiskSpaceFloorGiB)); }
	static UINT		NormalizeMinFreeDiskSpaceTempGiB(UINT nGiB) { return static_cast<UINT>(PartFilePersistenceSeams::NormalizeDiskSpaceFloorGiB(nGiB, PartFilePersistenceSeams::kMinTempDiskSpaceFloorGiB)); }
	static UINT		NormalizeMinFreeDiskSpaceIncomingGiB(UINT nGiB) { return static_cast<UINT>(PartFilePersistenceSeams::NormalizeDiskSpaceFloorGiB(nGiB, PartFilePersistenceSeams::kMinIncomingDiskSpaceFloorGiB)); }
	static ULONGLONG GetMinFreeDiskSpaceConfig()		{ return NormalizeMinFreeDiskSpaceConfig(m_uMinFreeDiskSpaceConfig); }
	static ULONGLONG GetMinFreeDiskSpaceTemp()			{ return NormalizeMinFreeDiskSpaceTemp(m_uMinFreeDiskSpaceTemp); }
	static ULONGLONG GetMinFreeDiskSpaceIncoming()		{ return NormalizeMinFreeDiskSpaceIncoming(m_uMinFreeDiskSpaceIncoming); }
	static void		SetMinFreeDiskSpaceConfig(ULONGLONG nBytes) { m_uMinFreeDiskSpaceConfig = NormalizeMinFreeDiskSpaceConfig(nBytes); }
	static void		SetMinFreeDiskSpaceTemp(ULONGLONG nBytes) { m_uMinFreeDiskSpaceTemp = NormalizeMinFreeDiskSpaceTemp(nBytes); }
	static void		SetMinFreeDiskSpaceIncoming(ULONGLONG nBytes) { m_uMinFreeDiskSpaceIncoming = NormalizeMinFreeDiskSpaceIncoming(nBytes); }
	static void		SetMinFreeDiskSpaceConfigGiB(UINT nGiB) { SetMinFreeDiskSpaceConfig(PartFilePersistenceSeams::ConvertDiskSpaceFloorGiBToBytes(NormalizeMinFreeDiskSpaceConfigGiB(nGiB))); }
	static void		SetMinFreeDiskSpaceTempGiB(UINT nGiB) { SetMinFreeDiskSpaceTemp(PartFilePersistenceSeams::ConvertDiskSpaceFloorGiBToBytes(NormalizeMinFreeDiskSpaceTempGiB(nGiB))); }
	static void		SetMinFreeDiskSpaceIncomingGiB(UINT nGiB) { SetMinFreeDiskSpaceIncoming(PartFilePersistenceSeams::ConvertDiskSpaceFloorGiBToBytes(NormalizeMinFreeDiskSpaceIncomingGiB(nGiB))); }
	static ULONGLONG GetEffectiveMinFreeDiskSpaceForPath(LPCTSTR pszPath);
	static bool		GetSparsePartFiles();
	static void		SetSparsePartFiles(bool bEnable)	{ m_bSparsePartFiles = bEnable; }
	static bool		IsShowUpDownIconInTaskbar()			{ return m_bShowUpDownIconInTaskbar; }
	static bool		IsAlwaysShowTrayIcon()				{ return m_bAlwaysShowTrayIcon; }
	static bool		GetEnableMiniMule()					{ return m_bEnableMiniMule; }
	static bool		IsWin7TaskbarGoodiesEnabled()		{ return m_bShowWin7TaskbarGoodies; }
	static void		SetWin7TaskbarGoodiesEnabled(bool flag)	{ m_bShowWin7TaskbarGoodies = flag; }

	static void		SetMaxUpload(uint32 val);
	static void		SetMaxDownload(uint32 val);
	/// Applies a temporary upload cap for this process without changing persisted preferences.
	static void		SetSessionMaxUpload(uint32 val);
	/// Applies a temporary download cap for this process without changing persisted preferences.
	static void		SetSessionMaxDownload(uint32 val);
	/// Clears the temporary upload quick-speed cap.
	static void		ClearSessionMaxUpload();
	/// Clears the temporary download quick-speed cap.
	static void		ClearSessionMaxDownload();
	/// Clears temporary upload/download quick-speed caps.
	static void		ClearSessionMaxLimits();

	static WINDOWPLACEMENT GetEmuleWindowPlacement()	{ return EmuleWindowPlacement; }
	static void		SetWindowLayout(const WINDOWPLACEMENT &in) { EmuleWindowPlacement = in; }

	static bool		GetAutoConnectToStaticServersOnly()	{ return m_bAutoConnectToStaticServersOnly; }
	static UINT		GetUpdateDays()						{ return m_uUpdateDays; }
	static UINT		GetDefaultUpdateDays()				{ return 5; }
	static UINT		GetMinUpdateDays()					{ return 1; }
	static UINT		GetMaxUpdateDays()					{ return 30; }
	static UINT		NormalizeUpdateDays(UINT in);
	static void		SetUpdateDays(UINT in);
	static time_t	GetLastVC()							{ return versioncheckLastAutomatic; }
	static void		UpdateLastVC();
	static bool		IsIPFilterEnabled()				{ return m_bIPFilterEnabled; }
	static void		SetIPFilterEnabled(bool bEnable)	{ m_bIPFilterEnabled = bEnable; }
	static int		GetIPFilterLevel()					{ return filterlevel; }
	/**
	 * @brief Returns whether automatic IP-filter downloads are enabled.
	 */
	static bool		GetAutoIPFilterUpdate()				{ return m_bAutoIPFilterUpdate; }
	/**
	 * @brief Stores whether automatic IP-filter downloads are enabled.
	 */
	static void		SetAutoIPFilterUpdate(bool bEnable)	{ m_bAutoIPFilterUpdate = bEnable; }
	/**
	 * @brief Returns the configured automatic IP-filter update interval in days.
	 */
	static UINT		GetIPFilterUpdatePeriodDays()		{ return m_uIPFilterUpdatePeriodDays; }
	static UINT		GetDefaultIPFilterUpdatePeriodDays();
	static UINT		GetMinIPFilterUpdatePeriodDays();
	static UINT		GetMaxIPFilterUpdatePeriodDays();
	static UINT		NormalizeIPFilterUpdatePeriodDays(UINT uDays);
	/**
	 * @brief Returns the last attempted automatic IP-filter update time.
	 */
	static __time64_t GetIPFilterLastUpdateTime()		{ return m_tIPFilterLastUpdateTime; }
	/**
	 * @brief Returns the configured IP-filter update URL.
	 */
	static const CString& GetIPFilterUpdateUrl()		{ return m_strIPFilterUpdateUrl; }
	/**
	 * @brief Returns the built-in default URL for IP-filter update downloads.
	 */
	static LPCTSTR	GetDefaultIPFilterUpdateUrl()		{ return _T("https://upd.emule-security.org/ipfilter.zip"); }
	/**
	 * @brief Returns the built-in default URL for server.met update downloads.
	 */
	static LPCTSTR	GetDefaultServerMetUrl()			{ return _T("https://upd.emule-security.org/server.met"); }
	/**
	 * @brief Returns the built-in default URL for nodes.dat Kad bootstrap downloads.
	 */
	static LPCTSTR	GetDefaultNodesDatUrl()				{ return _T("https://upd.emule-security.org/nodes.dat"); }
	/**
	 * @brief Stores the automatic IP-filter update interval in days.
	 */
	static void		SetIPFilterUpdatePeriodDays(UINT uDays);
	/**
	 * @brief Stores the last attempted automatic IP-filter update time and optionally persists it immediately.
	 */
	static void		SetIPFilterLastUpdateTime(__time64_t tTimestamp, bool bPersist = false);
	/**
	 * @brief Stores the IP-filter update URL and optionally persists it immediately.
	 */
	static void		SetIPFilterUpdateUrl(const CString& strUrl, bool bPersist = false);
	static const CString& GetMessageFilter()			{ return messageFilter; }
	static const CString& GetCommentFilter()			{ return commentFilter; }
	static void		SetCommentFilter(const CString &strFilter) { commentFilter = strFilter; }
	static const CString& GetFilenameCleanups()			{ return filenameCleanups; }

	static bool		ShowRatesOnTitle()					{ return showRatesInTitle; }
	/**
	 * @brief Returns whether the UI should resolve and display IP geolocation data.
	 */
	static bool		IsGeoLocationEnabled()				{ return m_bGeoLocationEnabled; }
	/**
	 * @brief Returns the configured automatic geolocation DB check interval in days.
	 */
	static UINT		GetGeoLocationCheckDays()			{ return m_uGeoLocationCheckDays; }
	/**
	 * @brief Returns whether IP geolocation is enabled for profiles without an explicit value.
	 */
	static bool		GetDefaultGeoLocationEnabled()		{ return true; }
	static UINT		GetDefaultGeoLocationCheckDays()	{ return 30; }
	static UINT		GetMinGeoLocationCheckDays()		{ return 7; }
	static UINT		GetMaxGeoLocationCheckDays()		{ return 365; }
	static UINT		NormalizeGeoLocationCheckDays(UINT uDays);
	/**
	 * @brief Returns the last attempted automatic or manual geolocation DB refresh time.
	 */
	static __time64_t GetGeoLocationLastCheckTime()		{ return m_tGeoLocationLastCheckTime; }
	/**
	 * @brief Returns the configured geolocation DB update URL template.
	 */
	static const CString& GetGeoLocationUpdateUrlTemplate()	{ return m_strGeoLocationUpdateUrl; }
	/**
	 * @brief Stores the automatic geolocation DB check interval in days.
	 */
	static void		SetGeoLocationCheckDays(UINT uDays);
	/**
	 * @brief Stores the last attempted geolocation DB refresh time and optionally persists it immediately.
	 */
	static void		SetGeoLocationLastCheckTime(__time64_t tTimestamp, bool bPersist = false);
	static void		LoadCats();
	static const CString& GetDateTimeFormat()			{ return m_strDateTimeFormat; }
	static const CString& GetDateTimeFormat4Log()		{ return m_strDateTimeFormat4Log; }
	static const CString& GetDateTimeFormat4Lists()		{ return m_strDateTimeFormat4Lists; }

	// Download Categories (Ornis)
	static INT_PTR	AddCat(Category_Struct *cat)		{ catArr.Add(cat); return catArr.GetCount() - 1; }
	static bool		MoveCat(INT_PTR from, INT_PTR to);
	static void		RemoveCat(INT_PTR index);
	static INT_PTR	GetCatCount()						{ return catArr.GetCount(); }
	static bool		SetCatFilter(INT_PTR index, int filter);
	static int		GetCatFilter(INT_PTR index);
	static bool		GetCatFilterNeg(INT_PTR index);
	static void		SetCatFilterNeg(INT_PTR index, bool val);
	static Category_Struct* GetCategory(INT_PTR index)	{ return (index >= 0 && index<catArr.GetCount()) ? catArr[index] : NULL; }
	static const CString& GetCatPath(INT_PTR index)		{ return catArr[index]->strIncomingPath; }
	static DWORD	GetCatColor(INT_PTR index, int nDefault = COLOR_BTNTEXT);

	static bool		GetPreviewOnIconDblClk()			{ return m_bPreviewOnIconDblClk; }
	static bool		GetCheckFileOpen()					{ return m_bCheckFileOpen; }
	static bool		ShowRatingIndicator()				{ return indicateratings; }
	static bool		WatchClipboard4ED2KLinks()			{ return watchclipboard; }
	static bool		GetRemoveToBin()					{ return m_bRemove2bin; }
	static bool		GetFilterServerByIP()				{ return filterserverbyip; }
	static bool		GetKeepUnavailableFixedSharedDirs()	{ return m_bKeepUnavailableFixedSharedDirs; }

	static bool		GetLog2Disk()						{ return log2disk; }
	static bool		GetDebug2Disk()						{ return m_bVerbose && debug2disk; }
	static UINT		GetMaxUploadClientsAllowed()		{ return m_uMaxUploadClientsAllowed; }
	static void		SetMaxUploadClientsAllowed(UINT uVal) { m_uMaxUploadClientsAllowed = PreferenceValidationSeams::NormalizeUploadSlots(uVal); }
	static UINT		GetUploadSlotElasticPercent()		{ return m_uUploadSlotElasticPercent; }
	static void		SetUploadSlotElasticPercent(UINT uVal) { m_uUploadSlotElasticPercent = PreferenceValidationSeams::NormalizeUploadSlotElasticPercent(uVal); }
	static UINT		GetElasticMaxUploadClientsAllowed()	{ return PreferenceValidationSeams::GetElasticUploadSlotCap(m_uMaxUploadClientsAllowed, m_uUploadSlotElasticPercent); }
	static float	GetSlowUploadThresholdFactor()	{ return m_fSlowUploadThresholdFactor; }
	static void		SetSlowUploadThresholdFactor(float fVal) { m_fSlowUploadThresholdFactor = PreferenceValidationSeams::NormalizeSlowUploadThresholdFactor(fVal); }
	static UINT		GetSlowUploadGraceSeconds()		{ return m_uSlowUploadGraceSeconds; }
	static void		SetSlowUploadGraceSeconds(UINT uVal) { m_uSlowUploadGraceSeconds = PreferenceValidationSeams::NormalizeSlowUploadGraceSeconds(uVal); }
	static UINT		GetSlowUploadWarmupSeconds()		{ return m_uSlowUploadWarmupSeconds; }
	static void		SetSlowUploadWarmupSeconds(UINT uVal) { m_uSlowUploadWarmupSeconds = PreferenceValidationSeams::NormalizeSlowUploadWarmupSeconds(uVal); }
	static UINT		GetZeroUploadRateGraceSeconds()			{ return m_uZeroUploadRateGraceSeconds; }
	static void		SetZeroUploadRateGraceSeconds(UINT uVal) { m_uZeroUploadRateGraceSeconds = PreferenceValidationSeams::NormalizeZeroUploadRateGraceSeconds(uVal); }
	static UINT		GetSlowUploadCooldownSeconds()	{ return m_uSlowUploadCooldownSeconds; }
	static void		SetSlowUploadCooldownSeconds(UINT uVal) { m_uSlowUploadCooldownSeconds = PreferenceValidationSeams::NormalizeSlowUploadCooldownSeconds(uVal); }
	static bool		IsLowRatioBoostEnabled()			{ return m_bLowRatioBoostEnabled; }
	static void		SetLowRatioBoostEnabled(bool bVal) { m_bLowRatioBoostEnabled = bVal; }
	static float	GetLowRatioThreshold()			{ return m_fLowRatioThreshold; }
	static void		SetLowRatioThreshold(float fVal) { m_fLowRatioThreshold = PreferenceValidationSeams::NormalizeLowRatioThreshold(fVal); }
	static UINT		GetLowRatioBonus()				{ return m_uLowRatioBonus; }
	static void		SetLowRatioBonus(UINT uVal)		{ m_uLowRatioBonus = PreferenceValidationSeams::NormalizeLowRatioBonus(uVal); }
	static UINT		GetLowIDDivisor()					{ return m_uLowIDDivisor; }
	static void		SetLowIDDivisor(UINT uVal)		{ m_uLowIDDivisor = PreferenceValidationSeams::NormalizeLowIDDivisor(uVal); }
	static ESessionTransferLimitMode GetSessionTransferLimitMode() { return m_eSessionTransferLimitMode; }
	static ESessionTransferLimitMode NormalizeSessionTransferLimitMode(ESessionTransferLimitMode eMode);
	static UINT		NormalizeSessionTransferLimitValue(ESessionTransferLimitMode eMode, UINT uVal);
	static void		SetSessionTransferLimitMode(ESessionTransferLimitMode eMode);
	static UINT		GetSessionTransferLimitValue()			{ return m_uSessionTransferLimitValue; }
	static void		SetSessionTransferLimitValue(UINT uVal);
	static UINT		GetSessionTimeLimitSeconds()		{ return m_uSessionTimeLimitSeconds; }
	static void		SetSessionTimeLimitSeconds(UINT uVal) { m_uSessionTimeLimitSeconds = PreferenceValidationSeams::NormalizeSessionTimeLimitSeconds(uVal); }
	static int		GetMaxLogBuff()						{ return iMaxLogBuff; }
	static UINT		GetMaxLogFileSize()					{ return uMaxLogFileSize; }
	static ELogFileFormat GetLogFileFormat()			{ return m_iLogFileFormat; }
	static int		GetCreateCrashDumpMode()			{ return m_iCreateCrashDumpMode; }
	static bool		GetCaptureFullCrashDump()			{ return m_bCaptureFullCrashDump; }
	/**
	 * @brief Returns the crash-dump mode used when no preference is stored.
	 */
	static int		GetDefaultCreateCrashDumpMode()		{ return 1; }
	static bool		GetDefaultCaptureFullCrashDump()	{ return false; }
	static void		SetCreateCrashDumpMode(int iMode);
	static void		SetCaptureFullCrashDump(bool bEnabled) { m_bCaptureFullCrashDump = bEnabled; }

	// Web Server
	static uint16	GetWSPort()							{ return m_nWebPort; }
	static bool		GetWSUseUPnP()						{ return m_bWebUseUPnP && GetWSIsEnabled(); }
	static uint16	GetDefaultWSPort()					{ return 4711; }
	static void		SetWSPort(uint16 uPort);
	static const CString& GetWebBindAddr()				{ return m_strWebBindAddr; }
	static void		SetWebBindAddr(const CString &strAddr)	{ m_strWebBindAddr = strAddr; }
	static const CString& GetWSPass()					{ return m_strWebPassword; }
	static void		SetWSPass(const CString &strNewPass);
	static const CString& GetWSApiKey()				{ return m_strWebApiKey; }
	static void		SetWSApiKey(const CString &strNewKey);
	static bool		GetWSIsEnabled()					{ return m_bWebEnabled; }
	static void		SetWSIsEnabled(bool bEnable)		{ m_bWebEnabled = bEnable; }
	static bool		GetLegacyWebUiEnabled()				{ return m_bLegacyWebUiEnabled; }
	static void		SetLegacyWebUiEnabled(bool bEnable)	{ m_bLegacyWebUiEnabled = bEnable; }
	static bool		GetWebUseGzip()						{ return m_bWebUseGzip; }
	static void		SetWebUseGzip(bool bUse)			{ m_bWebUseGzip = bUse; }
	static UINT		GetWebPageRefresh()					{ return static_cast<UINT>(m_nWebPageRefresh); }
	static UINT		GetDefaultWebPageRefresh()			{ return 120; }
	static UINT		GetMaxWebPageRefresh()				{ return 3600; }
	static UINT		NormalizeWebPageRefresh(UINT nRefresh);
	static void		SetWebPageRefresh(UINT nRefresh);
	static bool		GetWSIsLowUserEnabled()				{ return m_bWebLowEnabled; }
	static void		SetWSIsLowUserEnabled(bool in)		{ m_bWebLowEnabled = in; }
	static const CString& GetWSLowPass()				{ return m_strWebLowPassword; }
	static UINT		GetWebTimeoutMins()					{ return static_cast<UINT>(m_iWebTimeoutMins); }
	static UINT		GetDefaultWebTimeoutMins()			{ return 5; }
	static UINT		GetMaxWebTimeoutMins()				{ return 1440; }
	static UINT		NormalizeWebTimeoutMins(UINT nTimeoutMins);
	static void		SetWebTimeoutMins(UINT nTimeoutMins);
	static bool		GetWebAdminAllowedHiLevFunc()		{ return m_bAllowAdminHiLevFunc; }
	static bool		GetDiagnosticRestEndpointsEnabled()	{ return m_bDiagnosticRestEndpointsEnabled; }
	static void		SetWSLowPass(const CString &strNewPass);
	static const CUIntArray& GetAllowedRemoteAccessIPs() { return m_aAllowedRemoteAccessIPs; }
	static CString	GetAllowedRemoteAccessIPsString();
	static bool		SetAllowedRemoteAccessIPsString(const CString &strAllowedIPs, CString &strInvalidToken);
	static uint32	GetMaxWebUploadFileSizeMB()			{ return static_cast<uint32>(m_iWebFileUploadSizeLimitMB); }
	static uint32	GetDefaultMaxWebUploadFileSizeMB()	{ return 5; }
	static uint32	GetMaxWebUploadFileSizeLimitMB()	{ return 65535; }
	static uint32	NormalizeMaxWebUploadFileSizeMB(uint32 uFileSizeMB);
	static void		SetMaxWebUploadFileSizeMB(uint32 uFileSizeMB);
	static bool		GetWebUseHttps()					{ return m_bWebUseHttps; }
	static void		SetWebUseHttps(bool bUse)			{ m_bWebUseHttps = bUse; }
	static const CString& GetWebCertPath()				{ return m_sWebHttpsCertificate; }
	static void		SetWebCertPath(const CString &path)		{ m_sWebHttpsCertificate = path; }
	static const CString& GetWebKeyPath()				{ return m_sWebHttpsKey; }
	static void		SetWebKeyPath(const CString &path)	{ m_sWebHttpsKey = path; }

	static void		SetMaxSourcesPerFile(UINT in);
	static void		SetMaxConnections(UINT in);
	static void		SetMaxHalfConnections(UINT in);
	static void		SetConnectionTimeout(DWORD in)		{ m_dwConnectionTimeout = in; }
	static void		SetDownloadTimeout(DWORD in)		{ m_dwDownloadTimeout = in; }
	static void		SetEd2kSearchMaxResults(UINT in)	{ m_uEd2kSearchMaxResults = in; }
	static void		SetEd2kSearchMaxMoreRequests(UINT in) { m_uEd2kSearchMaxMoreRequests = in; }
	static void		SetKadFileSearchTotal(UINT in)		{ m_uKadFileSearchTotal = in; }
	static void		SetKadKeywordSearchTotal(UINT in)	{ m_uKadKeywordSearchTotal = in; }
	static void		SetKadFileSearchLifetimeSeconds(UINT in) { m_uKadFileSearchLifetimeSeconds = in; }
	static void		SetKadKeywordSearchLifetimeSeconds(UINT in) { m_uKadKeywordSearchLifetimeSeconds = in; }
	static bool		IsSchedulerEnabled()				{ return scheduler; }
	static void		SetSchedulerEnabled(bool in)		{ scheduler = in; }
	static bool		MsgOnlyFriends()					{ return msgonlyfriends; }
	static bool		MsgOnlySecure()						{ return msgsecure; }
	static UINT		GetMsgSessionsMax()					{ return maxmsgsessions; }
	static UINT		GetDefaultMsgSessionsMax()			{ return 50; }
	static void		SetMsgSessionsMax(UINT in);
	static bool		IsSecureIdentEnabled()				{ return m_bUseSecureIdent; } // use client credits->CryptoAvailable() to check if encryption is really available and not this function
	static bool		IsAdvSpamfilterEnabled()			{ return m_bAdvancedSpamfilter; }
	static bool		IsChatCaptchaEnabled()				{ return IsAdvSpamfilterEnabled() && m_bUseChatCaptchas; }
	static const CString& GetTemplate()					{ return m_strTemplateFile; }
	static void		SetTemplate(const CString &in)		{ m_strTemplateFile = in; }
	static bool		GetNetworkKademlia()				{ return networkkademlia && udpport > 0; }
	static void		SetNetworkKademlia(bool val)		{ networkkademlia = val; }
	static bool		GetNetworkED2K()					{ return networked2k; }
	static void		SetNetworkED2K(bool val)			{ networked2k = val; }

	// deadlake PROXYSUPPORT
	static const ProxySettings& GetProxySettings()		{ return proxy; }
	static void		SetProxySettings(const ProxySettings &proxysettings);

	static bool		ShowCatTabInfos()					{ return showCatTabInfos; }
	static void		ShowCatTabInfos(bool in)			{ showCatTabInfos = in; }

	static bool		AutoFilenameCleanup()				{ return autofilenamecleanup; }
	static void		AutoFilenameCleanup(bool in)		{ autofilenamecleanup = in; }
	static void		SetFilenameCleanups(const CString &in) { filenameCleanups = in; }
	static bool		GetFollowMajorityFilenameForNewDownloads() { return m_bFollowMajorityFilenameForNewDownloads; }
	static void		SetFollowMajorityFilenameForNewDownloads(bool in) { m_bFollowMajorityFilenameForNewDownloads = in; }
	static UINT		GetFollowMajorityFilenameRequiredPercent() { return m_uFollowMajorityFilenameRequiredPercent; }
	static UINT		GetDefaultFollowMajorityFilenameRequiredPercent() { return 51; }
	static UINT		GetMinFollowMajorityFilenameRequiredPercent() { return 1; }
	static UINT		GetMaxFollowMajorityFilenameRequiredPercent() { return 100; }
	static void		SetFollowMajorityFilenameRequiredPercent(UINT in) { m_uFollowMajorityFilenameRequiredPercent = min(max(in, GetMinFollowMajorityFilenameRequiredPercent()), GetMaxFollowMajorityFilenameRequiredPercent()); }
	static UINT		GetFollowMajorityFilenameMinimumVotes() { return m_uFollowMajorityFilenameMinimumVotes; }
	static UINT		GetDefaultFollowMajorityFilenameMinimumVotes() { return 0; }
	static UINT		GetMaxFollowMajorityFilenameMinimumVotes() { return 1000; }
	static void		SetFollowMajorityFilenameMinimumVotes(UINT in) { m_uFollowMajorityFilenameMinimumVotes = min(in, GetMaxFollowMajorityFilenameMinimumVotes()); }

	static bool		GetResumeSameCat()					{ return resumeSameCat; }
	static bool		IsGraphRecreateDisabled()			{ return dontRecreateGraphs; }
	static bool		GetRemoveFinishedDownloads()		{ return m_bRemoveFinishedDownloads; }

	static INT_PTR	GetMaxChatHistoryLines()			{ return m_iMaxChatHistory; }
	static bool		GetUseAutocompletion()				{ return m_bUseAutocompl; }
	static bool		GetUseDwlPercentage()				{ return m_bShowDwlPercentage; }
	static void		SetUseDwlPercentage(bool in)		{ m_bShowDwlPercentage = in; }
	static bool		GetShowActiveDownloadsBold()		{ return m_bShowActiveDownloadsBold; }
	static bool		GetShowSharedFilesDetails()			{ return m_bShowSharedFilesDetails; }
	static void		SetShowSharedFilesDetails(bool bIn)	{ m_bShowSharedFilesDetails = bIn; }
	static bool		GetAutoShowLookups()				{ return m_bAutoShowLookups; }
	static void		SetAutoShowLookups(bool bIn)		{ m_bAutoShowLookups = bIn; }
	static bool		GetForceSpeedsToKB()				{ return m_bForceSpeedsToKB; }
	static bool		GetExtraPreviewWithMenu()			{ return m_bExtraPreviewWithMenu; }

	//Toolbar
	static const CString& GetToolbarSettings()			{ return m_sToolbarSettings; }
	static void		SetToolbarSettings(const CString &in) { m_sToolbarSettings = in; }
	static const CString& GetToolbarBitmapSettings()	{ return m_sToolbarBitmap; }
	static void		SetToolbarBitmapSettings(const CString &path) { m_sToolbarBitmap = path; }
	static EToolbarLabelType GetToolbarLabelSettings()	{ return m_nToolbarLabels; }
	static void		SetToolbarLabelSettings(EToolbarLabelType eLabelType)	{ m_nToolbarLabels = eLabelType; }
	static bool		GetReBarToolbar()					{ return m_bReBarToolbar; }
	static bool		GetUseReBarToolbar();
	static CSize	GetToolbarIconSize()				{ return m_sizToolbarIconSize; }
	static void		SetToolbarIconSize(CSize siz)		{ m_sizToolbarIconSize = siz; }

	static bool		IsTransToolbarEnabled()				{ return m_bWinaTransToolbar; }
	static bool		IsDownloadToolbarEnabled()			{ return m_bShowDownloadToolbar; }
	static void		SetDownloadToolbar(bool bShow)		{ m_bShowDownloadToolbar = bShow; }

	static int		GetSearchMethod()					{ return m_iSearchMethod; }
	static void		SetSearchMethod(int iMethod)		{ m_iSearchMethod = iMethod; }

	static bool		GetA4AFSaveCpu()					{ return m_bA4AFSaveCpu; } // ZZ:DownloadManager

	static bool		GetHighresTimer()					{ return m_bHighresTimer; }

	static CString	GetHomepageBaseURL()				{ return GetHomepageBaseURLForLevel(GetWebMirrorAlertLevel()); }
	static CString	GetOnlineHelpURL();
	static CString	GetVersionCheckBaseURL();
	static CString	GetVersionCheckApiURL();
	static CString	GetVersionCheckURL();
	static CString	GetVersionCheckLandingURL();
	static void		SetWebMirrorAlertLevel(uint8 newValue) { m_nWebMirrorAlertLevel = newValue; }
	static bool		IsDefaultNick(const CString &strCheck);
	static UINT		GetWebMirrorAlertLevel();
	static bool		UseSimpleTimeRemainingComputation()	{ return m_bUseOldTimeRemaining; }

	// Verbose log options
	static bool		GetEnableVerboseOptions()			{ return m_bEnableVerboseOptions; }
	static bool		GetVerbose()						{ return m_bVerbose; }
	static bool		GetFullVerbose()					{ return m_bVerbose && m_bFullVerbose; }
	static bool		GetDebugSourceExchange()			{ return m_bVerbose && m_bDebugSourceExchange; }
	static bool		GetLogBannedClients()				{ return m_bVerbose && m_bLogBannedClients; }
	static bool		GetLogRatingDescReceived()			{ return m_bVerbose && m_bLogRatingDescReceived; }
	static bool		GetLogSecureIdent()					{ return m_bVerbose && m_bLogSecureIdent; }
	static bool		GetLogFilteredIPs()					{ return m_bVerbose && m_bLogFilteredIPs; }
	static bool		GetLogFileSaving()					{ return m_bVerbose && m_bLogFileSaving; }
	static bool		GetLogA4AF()						{ return m_bVerbose && m_bLogA4AF; } // ZZ:DownloadManager
	static bool		GetLogUlDlEvents()					{ return m_bVerbose && m_bLogUlDlEvents; }
	static bool		GetLogKadSecurityEvents()			{ return m_bVerbose; }
	static bool		GetUseDebugDevice()					{ return m_bUseDebugDevice; }
	static int		GetVerboseLogPriority()				{ return m_byLogLevel; }
#ifdef _DEBUG
	static int		GetDebugServerTCPLevel()			{ return m_iDebugServerTCPLevel; }
	static int		GetDebugServerUDPLevel()			{ return m_iDebugServerUDPLevel; }
	static int		GetDebugServerSourcesLevel()		{ return m_iDebugServerSourcesLevel; }
	static int		GetDebugServerSearchesLevel()		{ return m_iDebugServerSearchesLevel; }
	static int		GetDebugClientTCPLevel()			{ return m_iDebugClientTCPLevel; }
	static int		GetDebugClientUDPLevel()			{ return m_iDebugClientUDPLevel; }
	static int		GetDebugClientKadUDPLevel()			{ return m_iDebugClientKadUDPLevel; }
	static int		GetDebugSearchResultDetailLevel()	{ return m_iDebugSearchResultDetailLevel; }
#else
	// release builds optimise out the corresponding debug-only code
	static int		GetDebugServerTCPLevel()			{ return 0; }
	static int		GetDebugServerUDPLevel()			{ return 0; }
	static int		GetDebugServerSourcesLevel()		{ return 0; }
	static int		GetDebugServerSearchesLevel()		{ return 0; }
	static int		GetDebugClientTCPLevel()			{ return 0; }
	static int		GetDebugClientUDPLevel()			{ return 0; }
	static int		GetDebugClientKadUDPLevel()			{ return 0; }
	static int		GetDebugSearchResultDetailLevel()	{ return 0; }
#endif

	static bool		IsRememberingDownloadedFiles()		{ return m_bRememberDownloadedFiles; }
	static bool		IsRememberingCancelledFiles()		{ return m_bRememberCancelledFiles; }
	static bool		DoPartiallyPurgeOldKnownFiles()		{ return m_bPartiallyPurgeOldKnownFiles; }
	static void		SetRememberDownloadedFiles(bool nv)	{ m_bRememberDownloadedFiles = nv; }
	static void		SetRememberCancelledFiles(bool nv)	{ m_bRememberCancelledFiles = nv; }
	// mail notifier
	static const EmailSettings &GetEmailSettings()		{ return m_email; }
	static void		SetEmailSettings(const EmailSettings &settings) { m_email = settings; }

	static bool		IsNotifierSendMailEnabled()			{ return m_email.bSendMail; }

	static void		SetNotifierSendMail(bool nv)		{ m_email.bSendMail = nv; }
	static bool		DoFlashOnNewMessage()				{ return m_bIconflashOnNewMessage; }
	static void		IniCopy(const CString &si, const CString &di);
	static void		ApplyBBPreferenceMigrations(CIni &ini);
	static bool		GetAllocCompleteMode()				{ return m_bAllocFull; }
	static void		SetAllocCompleteMode(bool in)		{ m_bAllocFull = in; }

	// encryption
	static bool		IsCryptLayerEnabled()				{ return m_bCryptLayerSupported; }
	static bool		IsCryptLayerPreferred()				{ return IsCryptLayerEnabled() && m_bCryptLayerRequested; }
	static bool		IsCryptLayerRequired()				{ return IsCryptLayerPreferred() && m_bCryptLayerRequired; }
	static bool		IsCryptLayerRequiredStrict()			{ return false; } // not even incoming test connections will be answered
	static uint32	GetKadUDPKey()						{ return m_dwKadUDPKey; }
	static uint8	GetCryptTCPPaddingLength()			{ return m_byCryptTCPPaddingLength; }
	/// Returns whether eMule should choose fresh listen ports during startup.
	static bool		IsPortRandomizationOnStartupEnabled() { return m_bRandomizePortsOnStartup; }
	/// Configures whether eMule should choose fresh listen ports during startup.
	static void		SetPortRandomizationOnStartupEnabled(bool bEnabled) { m_bRandomizePortsOnStartup = bEnabled; }

	// UPnP
	static bool		IsUPnPEnabled()						{ return m_bEnableUPnP; }
	static bool		CloseUPnPOnExit()					{ return m_bCloseUPnPOnExit; }
	static uint8	GetUPnPBackendMode()				{ return m_uUPnPBackendMode; }
	static void		SetUPnPBackendMode(uint8 val)		{ m_uUPnPBackendMode = val; }

	// Spam filter
	static bool		IsSearchSpamFilterEnabled()			{ return m_bEnableSearchResultFilter; }

	// Search results: hide already-known states (bitmask of ESearchKnownHideFlags)
	static uint8	GetSearchHideKnownStates()			{ return m_uSearchHideKnownStates; }
	static void		SetSearchHideKnownStates(uint8 val)	{ m_uSearchHideKnownStates = val; }

	static bool		IsStoringSearchesEnabled()			{ return m_bStoreSearches; }
	static bool		GetPreventStandby()					{ return m_bPreventStandby; }
	static uint16	GetRandomTCPPort();
	static uint16	GetRandomUDPPort();

protected:
	static CString	m_strFileCommentsFilePath;
	static Preferences_Ext_Struct *prefsExt;
	static CArray<Category_Struct*,Category_Struct*> catArr;
	static CString	m_astrDefaultDirs[13];
	static bool		m_abDefaultDirsCreated[13];
	static int		m_nCurrentUserDirMode; // Only for PPgTweaks

	static void		CreateUserHash();
	static void		SetStandardValues();
	static UINT		GetRecommendedMaxConnections();
	static void		LoadPreferences();
	static void		SavePreferences();
	static CString	GetHomepageBaseURLForLevel(int nLevel);
	static CString	GetDefaultDirectory(EDefaultDirectory eDirectory, bool bCreate = true);
};

extern CPreferences thePrefs;
extern bool g_bLowColorDesktop;
