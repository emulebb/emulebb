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
#define MMNODRV			// mmsystem: Installable driver support
//#define MMNOSOUND		// mmsystem: Sound support
#define MMNOWAVE		// mmsystem: Waveform support
#define MMNOMIDI		// mmsystem: MIDI support
#define MMNOAUX			// mmsystem: Auxiliary audio support
#define MMNOMIXER		// mmsystem: Mixer support
#define MMNOTIMER		// mmsystem: Timer support
#define MMNOJOY			// mmsystem: Joystick support
#define MMNOMCI			// mmsystem: MCI support
#define MMNOMMIO		// mmsystem: Multimedia file I/O support
#define MMNOMMSYSTEM	// mmsystem: General MMSYSTEM functions
#include <Mmsystem.h>
#include <share.h>
#include <dbt.h>
#include <dwmapi.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <TlHelp32.h>
#include <Ws2tcpip.h>
#include <uxtheme.h>
#include <memory>
#include <vector>
#include "emule.h"
#include "emuleDlg.h"
#include "otherfunctions.h"
#include "ServerWnd.h"
#include "KademliaWnd.h"
#include "GeoLocation.h"
#include "DownloadListCtrl.h"
#include "TransferDlg.h"
#include "SearchDlg.h"
#include "SearchResultsWnd.h"
#include "SharedFilesWnd.h"
#include "ChatWnd.h"
#include "IrcWnd.h"
#include "StatisticsDlg.h"
#include "CreditsDlg.h"
#include "PreferencesDlg.h"
#include "ServerConnect.h"
#include "DownloadQueue.h"
#include "ClientUDPSocket.h"
#include "EMSocket.h"
#include "UpDownClient.h"
#include "KnownFileList.h"
#include "KnownFilePointerValidation.h"
#include "ServerList.h"
#include "Opcodes.h"
#include "ProtocolGuards.h"
#include "PublicIpProbe.h"
#include "SharedFileList.h"
#include "SharedFileListSeams.h"
#include "ED2KLink.h"
#include "Exceptions.h"
#include "FakeFileDetector.h"
#include "BroadbandIoSeams.h"
#include "SocketIoSeams.h"
#include "SearchList.h"
#include "HTRichEditCtrl.h"
#include "Preview.h"
#include "kademlia/kademlia/kademlia.h"
#include "PerfLog.h"
#include "DropTarget.h"
#include "WebServer.h"
#include "WebSocketHttpSeams.h"
#include "DownloadQueue.h"
#include "ClientUDPSocket.h"
#include "UploadQueue.h"
#include "ClientList.h"
#include "UploadBandwidthThrottler.h"
#include "FriendList.h"
#include "IPFilter.h"
#include "IPFilterUpdater.h"
#include "Statistics.h"
#include "MuleToolbarCtrl.h"
#include "TaskbarNotifier.h"
#include "MuleStatusbarCtrl.h"
#include "ListenSocket.h"
#include "Server.h"
#include "PartFile.h"
#include "StatusBarInfo.h"
#include "Scheduler.h"
#include "MenuCmds.h"
#include "MenuShortcutLabels.h"
#include "Ini2.h"
#include "MiniMuleDlg.h"
#include "MuleSystrayDlg.h"
#include "MuleListCtrlViewPresets.h"
#include "SpeedQuickActionsSeams.h"
#include "IPFilterDlg.h"
#include "WebServices.h"
#include "DirectDownloadDlg.h"
#include "StringConversion.h"
#include "BindStartupPolicy.h"
#include "BindRuntimeLossPolicy.h"
#include "AppKeyboardShortcutsSeams.h"
#include "AppWebLinksSeams.h"
#include "DiagnosticSnapshotSeams.h"
#include "TrayNotificationSeams.h"
#include "AICHSyncThreadSeams.h"
#include "aichsyncthread.h"
#include "HelperThreadLaunchSeams.h"
#include "Log.h"
#include "ProcessLaunchSeams.h"
#include "RestartAppSeams.h"
#include "UserMsgs.h"
#include "TextToSpeech.h"
#include "Collection.h"
#include "CollectionViewDialog.h"
#include "UPnPImpl.h"
#include "UPnPImplWrapper.h"
#include "ExitBox.h"
#include "UploadDiskIOThread.h"
#include "PartFileWriteThread.h"
#include "ClientCredits.h"
#include "ReleaseUpdateCheck.h"
#include "VersionCheckLaunchSeams.h"
#include "Version.h"
#include "WebServerJson.h"
#include "Mdump.h"
#include "PathHelpers.h"
#include "ElevatedPowerShellAction.h"
#include "WindowsFirewallRepair.h"
#include "WindowsMaintenanceActions.h"
#include "Win32CallbackTimerSeams.h"
#include "LifecycleProgressDlg.h"
#include "VpnGuardPolicySeams.h"
#include "VpnGuardSeams.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern BOOL FirstTimeWizard();

#define	SYS_TRAY_ICON_COOKIE_FORCE_UPDATE	UINT_MAX

UINT g_uMainThreadId = 0;
static const UINT UWM_ARE_YOU_EMULE = RegisterWindowMessage(EMULE_GUID);

#ifdef HAVE_WIN7_SDK_H
static const UINT UWM_TASK_BUTTON_CREATED = RegisterWindowMessage(_T("TaskbarButtonCreated"));
#endif

namespace
{
	static const UINT_PTR kBindLossWatchdogTimerId = 0xB10D;
	static const UINT kBindLossWatchdogIntervalMs = SEC2MS(10);
	static const UINT_PTR kTransferRateDisplayTimerId = 0xB10E;
	static const UINT kVpnGuardHttpIntervalMs = MIN2MS(5);

	static bool IsKeyboardShortcutModalContext(const CWnd &wnd)
	{
		return wnd.GetSafeHwnd() != NULL && !::IsWindowEnabled(wnd.GetSafeHwnd());
	}

	static UINT GetMainShellShortcutCommandId(AppKeyboardShortcutsSeams::ECommand eShortcutCommand)
	{
		// Keep app-level keyboard policy testable and resource-free in
		// AppKeyboardShortcutsSeams.h. The real dialog maps policy commands onto
		// the same toolbar command IDs used by mouse clicks so button state,
		// help routing, and existing side effects stay in one command path.
		switch (eShortcutCommand) {
		case AppKeyboardShortcutsSeams::ECommand::ShowConnect:
			return TBBTN_CONNECT;
		case AppKeyboardShortcutsSeams::ECommand::ShowKad:
			return TBBTN_KAD;
		case AppKeyboardShortcutsSeams::ECommand::ShowServer:
			return TBBTN_SERVER;
		case AppKeyboardShortcutsSeams::ECommand::ShowTransfers:
			return TBBTN_TRANSFERS;
		case AppKeyboardShortcutsSeams::ECommand::ShowSearch:
			return TBBTN_SEARCH;
		case AppKeyboardShortcutsSeams::ECommand::ShowSharedFiles:
			return TBBTN_SHARED;
		case AppKeyboardShortcutsSeams::ECommand::ShowMessages:
			return TBBTN_MESSAGES;
		case AppKeyboardShortcutsSeams::ECommand::ShowIrc:
			return TBBTN_IRC;
		case AppKeyboardShortcutsSeams::ECommand::ShowStatistics:
			return TBBTN_STATS;
		case AppKeyboardShortcutsSeams::ECommand::ShowOptions:
			return TBBTN_OPTIONS;
		case AppKeyboardShortcutsSeams::ECommand::ShowHelp:
			return TBBTN_HELP;
		case AppKeyboardShortcutsSeams::ECommand::ShowToolsMenu:
			return TBBTN_TOOLS;
		default:
			return 0;
		}
	}

	static BindStartupPolicy::CBindStartupPolicyText GetBindStartupPolicyText()
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

	static BindRuntimeLossPolicy::CBindRuntimeLossPolicyText GetBindRuntimeLossPolicyText()
	{
		BindRuntimeLossPolicy::CBindRuntimeLossPolicyText text;
		text.startupText = GetBindStartupPolicyText();
		text.strInterfaceChangedFormat = GetResString(IDS_VPN_GUARD_RUNTIME_INTERFACE_CHANGED_FMT);
		text.strInterfaceUnavailable = GetResString(IDS_VPN_GUARD_RUNTIME_INTERFACE_UNAVAILABLE);
		text.strStartupDisabledPrefix = GetResString(IDS_BIND_STARTUP_DISABLED_PREFIX);
		text.strRuntimeExitPrefix = GetResString(IDS_BIND_EXIT_PREFIX);
		return text;
	}

	static bool TryLoadVpnGuardAllowedRanges(std::vector<VpnGuardSeams::SAllowedPublicIpv4Range>& rRanges, CString& rstrError)
	{
		return VpnGuardSeams::TryParseAllowedPublicIpv4Ranges(thePrefs.GetVpnGuardAllowedPublicIpCidrs(), rRanges, rstrError);
	}

	static CString FormatVpnGuardPublicIpFailure(bool bRuntime, const PublicIpProbe::SBoundPublicIpv4ProbeResult& result, const CString& strDetail)
	{
		CString strReason;
		if (result.bSucceeded) {
			strReason.Format(GetResString(bRuntime ? IDS_VPN_GUARD_RUNTIME_PUBLIC_IP_MISMATCH_FMT : IDS_VPN_GUARD_STARTUP_PUBLIC_IP_MISMATCH_FMT),
				result.strPublicAddress.GetString(),
				(LPCTSTR)result.strProviderUrl,
				(LPCTSTR)thePrefs.GetVpnGuardAllowedPublicIpCidrs());
		} else {
			strReason.Format(GetResString(bRuntime ? IDS_VPN_GUARD_RUNTIME_PROBE_FAILED_FMT : IDS_VPN_GUARD_STARTUP_PROBE_FAILED_FMT),
				(LPCTSTR)thePrefs.GetVpnGuardAllowedPublicIpCidrs(),
				(LPCTSTR)(strDetail.IsEmpty() ? result.strError : strDetail));
		}
		return strReason;
	}

	static CString GetConfigFilePath(LPCTSTR pszLeafName)
	{
		return thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + pszLeafName;
	}

	static UINT GetExistingFileMenuFlags(const CString &rstrPath)
	{
		return MF_STRING | (LongPathSeams::PathExists(rstrPath) ? 0 : MF_GRAYED);
	}

	static bool TryGetModuleFilePath(CString &rstrPath)
	{
		rstrPath.Empty();
		DWORD dwCapacity = MAX_PATH;
		for (;;) {
			LPTSTR pszBuffer = rstrPath.GetBuffer(dwCapacity);
			const DWORD dwLength = ::GetModuleFileName(NULL, pszBuffer, dwCapacity);
			if (dwLength == 0) {
				rstrPath.ReleaseBuffer(0);
				return false;
			}
			if (dwLength < dwCapacity - 1) {
				rstrPath.ReleaseBuffer(dwLength);
				return true;
			}
			rstrPath.ReleaseBuffer(0);
			dwCapacity *= 2;
			if (dwCapacity > 32768)
				return false;
		}
	}

	static std::string JsonUtf8FromCString(const CString &rstrValue)
	{
		const CStringA strUtf8(StrToUtf8(rstrValue));
		return std::string(strUtf8.GetString(), static_cast<size_t>(strUtf8.GetLength()));
	}

	static bool TryWriteRestartRequest(
		const CString &rstrRequestPath,
		const CString &rstrExecutablePath,
		const CString &rstrWorkingDirectory,
		const std::vector<CString> &raRestartArguments,
		CString &rstrError)
	{
		try {
			nlohmann::json arguments = nlohmann::json::array();
			for (const CString &rArgument : raRestartArguments)
				arguments.push_back(JsonUtf8FromCString(rArgument));

			const nlohmann::json request = {
				{"schema", "emulebb.restartRequest.v1"},
				{"parentProcessId", static_cast<unsigned long>(::GetCurrentProcessId())},
				{"executablePath", JsonUtf8FromCString(rstrExecutablePath)},
				{"workingDirectory", JsonUtf8FromCString(rstrWorkingDirectory)},
				{"arguments", arguments}
			};
			const std::string strSerialized = request.dump(2) + "\n";

			CFile file;
			if (!file.Open(rstrRequestPath, CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite | CFile::typeBinary)) {
				rstrError.Format(_T("could not write restart request %s"), (LPCTSTR)rstrRequestPath);
				return false;
			}
			file.Write(strSerialized.data(), static_cast<UINT>(strSerialized.size()));
		} catch (CFileException *ex) {
			const LONG lOsError = ex->m_lOsError;
			ex->Delete();
			rstrError.Format(_T("could not write restart request %s (error %ld)"), (LPCTSTR)rstrRequestPath, lOsError);
			return false;
		} catch (const nlohmann::json::exception &rException) {
			rstrError.Format(_T("could not build restart request JSON: %hs"), rException.what());
			return false;
		}
		return true;
	}

	static std::string JsonString(const CString &rstrText)
	{
		return WebServerJson::ToStdUtf8(rstrText);
	}

	static std::string JsonNetworkAddress(const CString &rstrAddress, DiagnosticSnapshotSeams::ESnapshotPrivacyMode ePrivacyMode)
	{
		return JsonString(DiagnosticSnapshotSeams::IsRedacted(ePrivacyMode) ? DiagnosticSnapshotSeams::RedactNetworkAddress(rstrAddress) : rstrAddress);
	}

	static std::string JsonPath(const CString &rstrPath, DiagnosticSnapshotSeams::ESnapshotPrivacyMode ePrivacyMode)
	{
		return JsonString(DiagnosticSnapshotSeams::IsRedacted(ePrivacyMode) ? DiagnosticSnapshotSeams::RedactPath(rstrPath) : rstrPath);
	}

	static WindowsFirewallRepair::CRepairLaunchResult s_lastFirewallRepairResult;

	static uint64 FileTimeToUInt64(const FILETIME &rFileTime)
	{
		ULARGE_INTEGER value;
		value.LowPart = rFileTime.dwLowDateTime;
		value.HighPart = rFileTime.dwHighDateTime;
		return value.QuadPart;
	}

	static CString GetModulePath()
	{
		return PathHelpers::GetModuleFilePath(NULL);
	}

	static CString GetCurrentDirectoryPath()
	{
		return PathHelpers::GetCurrentDirectoryPath();
	}

	static nlohmann::json BuildProcessInfoJson(DiagnosticSnapshotSeams::ESnapshotPrivacyMode ePrivacyMode)
	{
		FILETIME creationTime = {};
		FILETIME exitTime = {};
		FILETIME kernelTime = {};
		FILETIME userTime = {};
		const bool bHasTimes = ::GetProcessTimes(::GetCurrentProcess(), &creationTime, &exitTime, &kernelTime, &userTime) != FALSE;

		return nlohmann::json{
			{"pid", ::GetCurrentProcessId()},
			{"mainThreadId", g_uMainThreadId},
			{"executablePath", JsonPath(GetModulePath(), ePrivacyMode)},
			{"currentDirectory", JsonPath(GetCurrentDirectoryPath(), ePrivacyMode)},
			{"commandLine", DiagnosticSnapshotSeams::IsRedacted(ePrivacyMode) ? std::string("[redacted]") : JsonString(::GetCommandLine())},
			{"tickCount64Ms", static_cast<uint64>(::GetTickCount64())},
			{"creationTimeFileTime", bHasTimes ? nlohmann::json(FileTimeToUInt64(creationTime)) : nlohmann::json(nullptr)},
			{"kernelTime100ns", bHasTimes ? nlohmann::json(FileTimeToUInt64(kernelTime)) : nlohmann::json(nullptr)},
			{"userTime100ns", bHasTimes ? nlohmann::json(FileTimeToUInt64(userTime)) : nlohmann::json(nullptr)}
		};
	}

	static nlohmann::json BuildPathsJson(DiagnosticSnapshotSeams::ESnapshotPrivacyMode ePrivacyMode)
	{
		return nlohmann::json{
			{"executableDir", JsonPath(thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR), ePrivacyMode)},
			{"configDir", JsonPath(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR), ePrivacyMode)},
			{"tempDir", JsonPath(thePrefs.GetMuleDirectory(EMULE_TEMPDIR), ePrivacyMode)},
			{"incomingDir", JsonPath(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR), ePrivacyMode)},
			{"logDir", JsonPath(thePrefs.GetMuleDirectory(EMULE_LOGDIR), ePrivacyMode)},
			{"webserverDir", JsonPath(thePrefs.GetMuleDirectory(EMULE_WEBSERVERDIR), ePrivacyMode)},
			{"skinDir", JsonPath(thePrefs.GetMuleDirectory(EMULE_SKINDIR), ePrivacyMode)},
			{"toolbarDir", JsonPath(thePrefs.GetMuleDirectory(EMULE_TOOLBARDIR), ePrivacyMode)},
			{"emuleLog", JsonPath(theLog.GetFilePath(), ePrivacyMode)},
			{"verboseLog", JsonPath(theVerboseLog.GetFilePath(), ePrivacyMode)},
			{"addressesDat", JsonPath(GetConfigFilePath(_T("addresses.dat")), ePrivacyMode)},
			{"webservicesDat", JsonPath(theWebServices.GetDefaultServicesFile(), ePrivacyMode)}
		};
	}

	static CString SocketAddressToHostString(const SOCKET_ADDRESS &rAddress)
	{
		TCHAR szHost[NI_MAXHOST] = {};
		if (rAddress.lpSockaddr == NULL || ::GetNameInfo(rAddress.lpSockaddr, rAddress.iSockaddrLength, szHost, _countof(szHost), NULL, 0, NI_NUMERICHOST) != 0)
			return CString();
		return CString(szHost);
	}

	static nlohmann::json BuildAdaptersJson(DiagnosticSnapshotSeams::ESnapshotPrivacyMode ePrivacyMode)
	{
		ULONG ulSize = 15 * 1024;
		std::vector<BYTE> buffer(ulSize);
		ULONG ulResult = ::GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()), &ulSize);
		if (ulResult == ERROR_BUFFER_OVERFLOW) {
			buffer.resize(ulSize);
			ulResult = ::GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()), &ulSize);
		}

		nlohmann::json adapters = nlohmann::json::array();
		if (ulResult != NO_ERROR)
			return adapters;

		for (PIP_ADAPTER_ADDRESSES pAdapter = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()); pAdapter != NULL; pAdapter = pAdapter->Next) {
			nlohmann::json addresses = nlohmann::json::array();
			for (PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pAdapter->FirstUnicastAddress; pUnicast != NULL; pUnicast = pUnicast->Next) {
				const CString strHost(SocketAddressToHostString(pUnicast->Address));
				if (!strHost.IsEmpty())
					addresses.push_back(JsonNetworkAddress(strHost, ePrivacyMode));
			}

			adapters.push_back(nlohmann::json{
				{"name", JsonString(CString(pAdapter->AdapterName))},
				{"friendlyName", JsonString(CString(pAdapter->FriendlyName != NULL ? pAdapter->FriendlyName : L""))},
				{"description", JsonString(CString(pAdapter->Description != NULL ? pAdapter->Description : L""))},
				{"ifType", pAdapter->IfType},
				{"operStatus", pAdapter->OperStatus},
				{"mtu", pAdapter->Mtu},
				{"addresses", addresses}
			});
		}
		return adapters;
	}

	static const char *TcpStateName(DWORD dwState)
	{
		switch (dwState) {
		case MIB_TCP_STATE_CLOSED:
			return "closed";
		case MIB_TCP_STATE_LISTEN:
			return "listen";
		case MIB_TCP_STATE_SYN_SENT:
			return "synSent";
		case MIB_TCP_STATE_SYN_RCVD:
			return "synReceived";
		case MIB_TCP_STATE_ESTAB:
			return "established";
		case MIB_TCP_STATE_FIN_WAIT1:
			return "finWait1";
		case MIB_TCP_STATE_FIN_WAIT2:
			return "finWait2";
		case MIB_TCP_STATE_CLOSE_WAIT:
			return "closeWait";
		case MIB_TCP_STATE_CLOSING:
			return "closing";
		case MIB_TCP_STATE_LAST_ACK:
			return "lastAck";
		case MIB_TCP_STATE_TIME_WAIT:
			return "timeWait";
		case MIB_TCP_STATE_DELETE_TCB:
			return "deleteTcb";
		default:
			return "unknown";
		}
	}

	static uint16 NetworkPortFromDword(DWORD dwPort)
	{
		return ntohs(static_cast<u_short>(dwPort));
	}

	static nlohmann::json BuildProcessTcpSocketsJson(DiagnosticSnapshotSeams::ESnapshotPrivacyMode ePrivacyMode)
	{
		DWORD dwSize = 0;
		(void)::GetExtendedTcpTable(NULL, &dwSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
		std::vector<BYTE> buffer(dwSize);
		if (dwSize == 0 || ::GetExtendedTcpTable(buffer.data(), &dwSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR)
			return nlohmann::json::array();

		const DWORD dwPid = ::GetCurrentProcessId();
		nlohmann::json sockets = nlohmann::json::array();
		const PMIB_TCPTABLE_OWNER_PID pTable = reinterpret_cast<PMIB_TCPTABLE_OWNER_PID>(buffer.data());
		for (DWORD i = 0; i < pTable->dwNumEntries; ++i) {
			const MIB_TCPROW_OWNER_PID &row = pTable->table[i];
			if (row.dwOwningPid != dwPid)
				continue;

			sockets.push_back(nlohmann::json{
				{"state", TcpStateName(row.dwState)},
				{"localAddress", JsonNetworkAddress(ipstr(row.dwLocalAddr), ePrivacyMode)},
				{"localPort", NetworkPortFromDword(row.dwLocalPort)},
				{"remoteAddress", JsonNetworkAddress(ipstr(row.dwRemoteAddr), ePrivacyMode)},
				{"remotePort", NetworkPortFromDword(row.dwRemotePort)}
			});
		}
		return sockets;
	}

	static nlohmann::json BuildProcessUdpSocketsJson(DiagnosticSnapshotSeams::ESnapshotPrivacyMode ePrivacyMode)
	{
		DWORD dwSize = 0;
		(void)::GetExtendedUdpTable(NULL, &dwSize, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
		std::vector<BYTE> buffer(dwSize);
		if (dwSize == 0 || ::GetExtendedUdpTable(buffer.data(), &dwSize, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0) != NO_ERROR)
			return nlohmann::json::array();

		const DWORD dwPid = ::GetCurrentProcessId();
		nlohmann::json sockets = nlohmann::json::array();
		const PMIB_UDPTABLE_OWNER_PID pTable = reinterpret_cast<PMIB_UDPTABLE_OWNER_PID>(buffer.data());
		for (DWORD i = 0; i < pTable->dwNumEntries; ++i) {
			const MIB_UDPROW_OWNER_PID &row = pTable->table[i];
			if (row.dwOwningPid != dwPid)
				continue;

			sockets.push_back(nlohmann::json{
				{"localAddress", JsonNetworkAddress(ipstr(row.dwLocalAddr), ePrivacyMode)},
				{"localPort", NetworkPortFromDword(row.dwLocalPort)}
			});
		}
		return sockets;
	}

	static nlohmann::json BuildDesiredFirewallRulesJson(DiagnosticSnapshotSeams::ESnapshotPrivacyMode)
	{
		nlohmann::json rules = nlohmann::json::array();
		for (const WindowsFirewallRepairSeams::CFirewallRuleSpec &rule : WindowsFirewallRepairSeams::BuildDesiredRules(
			thePrefs.GetPort(),
			thePrefs.GetUDPPort(),
			thePrefs.GetWSIsEnabled(),
			thePrefs.GetWSPort(),
			thePrefs.GetWebBindAddr())) {
			rules.push_back(nlohmann::json{
				{"name", JsonString(rule.strName)},
				{"direction", JsonString(rule.strDirection)},
				{"protocol", JsonString(rule.strProtocol)},
				{"localPort", "Any"},
				{"remotePort", "Any"},
				{"localAddress", "Any"},
				{"remoteAddress", "Any"},
				{"profiles", nlohmann::json::array({"Domain", "Private", "Public"})}
			});
		}
		return rules;
	}

	static nlohmann::json BuildLastFirewallRepairJson(DiagnosticSnapshotSeams::ESnapshotPrivacyMode ePrivacyMode)
	{
		if (!s_lastFirewallRepairResult.bStarted && s_lastFirewallRepairResult.dwLastError == ERROR_SUCCESS && s_lastFirewallRepairResult.rules.empty())
			return nullptr;

		nlohmann::json rules = nlohmann::json::array();
		for (const WindowsFirewallRepairSeams::CFirewallRuleSpec &rule : s_lastFirewallRepairResult.rules) {
			rules.push_back(nlohmann::json{
				{"name", JsonString(rule.strName)},
				{"direction", JsonString(rule.strDirection)},
				{"protocol", JsonString(rule.strProtocol)},
				{"localPort", "Any"},
				{"remotePort", "Any"},
				{"localAddress", "Any"},
				{"remoteAddress", "Any"},
				{"profiles", nlohmann::json::array({"Domain", "Private", "Public"})}
			});
		}

		return nlohmann::json{
			{"started", s_lastFirewallRepairResult.bStarted},
			{"succeeded", s_lastFirewallRepairResult.bSucceeded},
			{"cancelled", s_lastFirewallRepairResult.bCancelled},
			{"lastError", s_lastFirewallRepairResult.dwLastError},
			{"exitCode", s_lastFirewallRepairResult.dwExitCode == STILL_ACTIVE ? nlohmann::json(nullptr) : nlohmann::json(s_lastFirewallRepairResult.dwExitCode)},
			{"rules", rules},
			{"tempDir", JsonPath(s_lastFirewallRepairResult.strTempDir, ePrivacyMode)},
			{"scriptPath", JsonPath(s_lastFirewallRepairResult.strScriptPath, ePrivacyMode)},
			{"resultPath", JsonPath(s_lastFirewallRepairResult.strResultPath, ePrivacyMode)},
			{"errorText", JsonString(s_lastFirewallRepairResult.strErrorText)},
			{"resultJson", DiagnosticSnapshotSeams::IsRedacted(ePrivacyMode) ? std::string("[redacted]") : JsonString(s_lastFirewallRepairResult.strResultJson)}
		};
	}

	static nlohmann::json BuildModulesJson(DiagnosticSnapshotSeams::ESnapshotPrivacyMode ePrivacyMode)
	{
		nlohmann::json modules = nlohmann::json::array();
		const DWORD dwPid = ::GetCurrentProcessId();
		HANDLE hSnapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, dwPid);
		if (hSnapshot == INVALID_HANDLE_VALUE)
			return modules;

		MODULEENTRY32 module = {};
		module.dwSize = sizeof(module);
		if (::Module32First(hSnapshot, &module)) {
			do {
				modules.push_back(nlohmann::json{
					{"name", JsonString(CString(module.szModule))},
					{"path", JsonPath(CString(module.szExePath), ePrivacyMode)},
					{"baseAddress", static_cast<uint64>(reinterpret_cast<uintptr_t>(module.modBaseAddr))},
					{"sizeBytes", module.modBaseSize}
				});
			} while (::Module32Next(hSnapshot, &module));
		}
		::CloseHandle(hSnapshot);
		return modules;
	}

	static nlohmann::json BuildCurrentServerJson(DiagnosticSnapshotSeams::ESnapshotPrivacyMode ePrivacyMode)
	{
		CServer *const pCurrentServer = theApp.serverconnect != NULL ? theApp.serverconnect->GetCurrentServer() : NULL;
		if (pCurrentServer == NULL)
			return nullptr;

		return nlohmann::json{
			{"name", JsonString(pCurrentServer->GetListName())},
			{"address", JsonNetworkAddress(pCurrentServer->GetAddress(), ePrivacyMode)},
			{"ip", pCurrentServer->GetIP() != 0 ? nlohmann::json(JsonNetworkAddress(ipstr(pCurrentServer->GetIP()), ePrivacyMode)) : nlohmann::json(nullptr)},
			{"dynIp", JsonNetworkAddress(pCurrentServer->GetDynIP(), ePrivacyMode)},
			{"port", pCurrentServer->GetPort()},
			{"description", JsonString(pCurrentServer->GetDescription())},
			{"version", JsonString(pCurrentServer->GetVersion())}
		};
	}

	static nlohmann::json BuildNetworkJson(DiagnosticSnapshotSeams::ESnapshotPrivacyMode ePrivacyMode)
	{
		const uint32 dwPublicIP = theApp.GetPublicIP();
		return nlohmann::json{
			{"connected", theApp.IsConnected()},
			{"firewalled", theApp.IsFirewalled()},
			{"publicIp", dwPublicIP != 0 ? nlohmann::json(JsonNetworkAddress(ipstr(dwPublicIP), ePrivacyMode)) : nlohmann::json(nullptr)},
			{"ports", nlohmann::json{
				{"tcp", thePrefs.GetPort()},
				{"udp", thePrefs.GetUDPPort()},
				{"serverUdp", thePrefs.GetServerUDPPort()}
			}},
			{"upnpEnabled", thePrefs.IsUPnPEnabled()},
			{"windowsFirewall", nlohmann::json{
				{"desiredRules", BuildDesiredFirewallRulesJson(ePrivacyMode)},
				{"lastRepair", BuildLastFirewallRepairJson(ePrivacyMode)}
			}},
			{"binding", nlohmann::json{
				{"configuredAddress", JsonNetworkAddress(thePrefs.GetConfiguredBindAddr(), ePrivacyMode)},
				{"configuredInterfaceId", JsonString(thePrefs.GetBindInterface())},
				{"configuredInterfaceName", JsonString(thePrefs.GetBindInterfaceName())},
				{"activeConfiguredAddress", JsonNetworkAddress(thePrefs.GetActiveConfiguredBindAddr(), ePrivacyMode)},
				{"activeInterfaceId", JsonString(thePrefs.GetActiveBindInterface())},
				{"activeInterfaceName", JsonString(thePrefs.GetActiveBindInterfaceName())},
				{"activeInterfaceIndex", thePrefs.GetActiveBindInterfaceIndex()},
				{"resolveResult", static_cast<int>(thePrefs.GetActiveBindAddressResolveResult())}
			}},
			{"vpnGuard", nlohmann::json{
				{"enabled", thePrefs.IsVpnGuardEnabled()},
				{"mode", JsonString(VpnGuardSeams::GetModePreferenceText(thePrefs.GetVpnGuardMode()))},
				{"allowedPublicIpCidrs", JsonString(thePrefs.GetVpnGuardAllowedPublicIpCidrs())},
				{"startupBlocked", theApp.IsStartupBindBlocked()},
				{"startupBlockReason", JsonString(theApp.GetStartupBindBlockReason())}
			}},
			{"ed2k", nlohmann::json{
				{"connected", theApp.serverconnect != NULL && theApp.serverconnect->IsConnected()},
				{"connecting", theApp.serverconnect != NULL && theApp.serverconnect->IsConnecting()},
				{"lowId", theApp.serverconnect != NULL && theApp.serverconnect->IsConnected() ? nlohmann::json(theApp.serverconnect->IsLowID()) : nlohmann::json(nullptr)},
				{"currentServer", BuildCurrentServerJson(ePrivacyMode)}
			}},
			{"kad", nlohmann::json{
				{"running", Kademlia::CKademlia::IsRunning()},
				{"connected", Kademlia::CKademlia::IsConnected()},
				{"firewalled", Kademlia::CKademlia::IsConnected() ? nlohmann::json(Kademlia::CKademlia::IsFirewalled()) : nlohmann::json(nullptr)},
				{"users", Kademlia::CKademlia::IsConnected() ? nlohmann::json(Kademlia::CKademlia::GetKademliaUsers()) : nlohmann::json(nullptr)},
				{"files", Kademlia::CKademlia::IsConnected() ? nlohmann::json(Kademlia::CKademlia::GetKademliaFiles()) : nlohmann::json(nullptr)}
			}},
			{"adapters", BuildAdaptersJson(ePrivacyMode)},
			{"processTcpSockets", BuildProcessTcpSocketsJson(ePrivacyMode)},
			{"processUdpSockets", BuildProcessUdpSocketsJson(ePrivacyMode)}
		};
	}

	static nlohmann::json BuildTransfersJson()
	{
		return nlohmann::json{
			{"downloadSpeedBytesPerSec", theApp.downloadqueue != NULL ? nlohmann::json(theApp.downloadqueue->GetDatarate()) : nlohmann::json(nullptr)},
			{"uploadSpeedBytesPerSec", theApp.uploadqueue != NULL ? nlohmann::json(theApp.uploadqueue->GetDatarate()) : nlohmann::json(nullptr)},
			{"sessionDownloadedBytes", theStats.sessionReceivedBytes},
			{"sessionUploadedBytes", theStats.sessionSentBytes},
			{"downloadCount", theApp.downloadqueue != NULL ? nlohmann::json(static_cast<int64_t>(theApp.downloadqueue->GetFileCount())) : nlohmann::json(nullptr)},
			{"activeUploads", theApp.uploadqueue != NULL ? nlohmann::json(static_cast<int64_t>(theApp.uploadqueue->GetActiveUploadsCount())) : nlohmann::json(nullptr)},
			{"waitingUploads", theApp.uploadqueue != NULL ? nlohmann::json(static_cast<int64_t>(theApp.uploadqueue->GetWaitingUserCount())) : nlohmann::json(nullptr)}
		};
	}

	static const char *GetStartupCacheSavePhaseName(const CSharedFileList::StartupCacheSavePhase ePhase)
	{
		switch (ePhase)
		{
		case CSharedFileList::StartupCacheSavePhase::BuildingRecords:
			return "buildingRecords";
		case CSharedFileList::StartupCacheSavePhase::WritingFile:
			return "writingFile";
		case CSharedFileList::StartupCacheSavePhase::ApplyingResult:
			return "applyingResult";
		case CSharedFileList::StartupCacheSavePhase::Idle:
		default:
			return "idle";
		}
	}

	static nlohmann::json BuildSharedStartupCacheJson()
	{
		if (theApp.sharedfiles == NULL)
			return nlohmann::json{{"ready", true}, {"available", false}};

		CSharedFileList::StartupCacheStatus status = {};
		theApp.sharedfiles->GetStartupCacheStatus(status);
		return nlohmann::json{
			{"available", true},
			{"ready", status.bReady},
			{"filePresent", status.bCacheFilePresent},
			{"loaded", status.bCacheLoaded},
			{"rejected", status.bCacheRejected},
			{"removed", status.bCacheRemoved},
			{"rejectCode", status.strCacheRejectCode.IsEmpty() ? nlohmann::json(nullptr) : nlohmann::json(status.strCacheRejectCode.GetString())},
			{"recordsLoaded", status.uRecordsLoaded},
			{"volumesLoaded", status.uVolumesLoaded},
			{"duplicatePathCache", nlohmann::json{
				{"loaded", status.bDuplicatePathCacheLoaded},
				{"rejected", status.bDuplicatePathCacheRejected},
				{"removed", status.bDuplicatePathCacheRemoved},
				{"rejectCode", status.strDuplicatePathCacheRejectCode.IsEmpty() ? nlohmann::json(nullptr) : nlohmann::json(status.strDuplicatePathCacheRejectCode.GetString())},
				{"recordsLoaded", status.uDuplicatePathRecordsLoaded}
			}},
			{"scan", nlohmann::json{
				{"requestedDirectories", status.scanStats.uRequestedDirectories},
				{"dedupedDirectories", status.scanStats.uDedupedDirectories},
				{"duplicateDirectories", status.scanStats.uDuplicateDirectories},
				{"directoriesFromCache", status.scanStats.uDirectoriesFromCache},
				{"directoriesRescanned", status.scanStats.uDirectoriesRescanned},
				{"inaccessibleDirectories", status.scanStats.uInaccessibleDirectories},
				{"directoriesOver248Chars", status.scanStats.uDirectoriesOver248Chars},
				{"pathsOver260Chars", status.scanStats.uPathsOver260Chars},
				{"knownFilesAccepted", status.scanStats.uKnownFilesAccepted},
				{"duplicatePathsReused", status.scanStats.uDuplicatePathsReused},
				{"filesQueuedForHash", status.scanStats.uFilesQueuedForHash},
				{"filesIgnored", status.scanStats.uFilesIgnored}
			}},
			{"save", nlohmann::json{
				{"running", status.saveProgress.bRunning},
				{"dirty", status.saveProgress.bDirty},
				{"waitingForFollowUp", status.saveProgress.bWaitingForFollowUp},
				{"phase", GetStartupCacheSavePhaseName(status.saveProgress.ePhase)},
				{"completedDirectories", status.saveProgress.uCompletedDirectories},
				{"totalDirectories", status.saveProgress.uTotalDirectories}
			}},
			{"hashingCount", static_cast<int64_t>(status.iHashingCount)},
			{"deferredHashingActive", status.bDeferredHashingActive},
			{"interruptedHashingInvalidatedCache", status.bInterruptedHashingInvalidatedCache}
		};
	}

	static nlohmann::json BuildSocketBufferJson()
	{
		nlohmann::json udpReceive = {
			{"requestedBytes", kBroadbandUdpReceiveBufferBytes},
			{"actualBytes", nullptr}
		};

		if (theApp.clientudp != NULL) {
			int iValue = 0;
			int iValueLen = sizeof iValue;
			if (theApp.clientudp->GetSockOpt(SO_RCVBUF, &iValue, &iValueLen))
				udpReceive["actualBytes"] = iValue;
		}

		nlohmann::json tcpSamples = nlohmann::json::array();
		if (theApp.uploadqueue != NULL) {
			for (POSITION pos = theApp.uploadqueue->GetFirstFromUploadList(); pos != NULL;) {
				CUpDownClient *const pClient = theApp.uploadqueue->GetNextFromUploadList(pos);
				CEMSocket *const pSocket = pClient != NULL ? pClient->GetFileUploadSocket() : NULL;
				if (pSocket == NULL)
					continue;

				int iValue = 0;
				int iValueLen = sizeof iValue;
				if (pSocket->GetSockOpt(SO_SNDBUF, &iValue, &iValueLen)) {
					tcpSamples.push_back(nlohmann::json{
						{"actualBytes", iValue},
						{"meetsTarget", iValue >= static_cast<int>(kBroadbandTcpUploadSendBufferBytes)}
					});
				}
			}
		}

		return nlohmann::json{
			{"udpReceive", udpReceive},
			{"tcpUploadSend", nlohmann::json{
				{"targetBytes", kBroadbandTcpUploadSendBufferBytes},
				{"activeSocketCount", tcpSamples.size()},
				{"activeSockets", tcpSamples}
			}}
		};
	}

	static nlohmann::json BuildIoJson()
	{
		const uint64 uEffectiveBufferBytes = theApp.downloadqueue != NULL
			? theApp.downloadqueue->GetEffectiveFileBufferSizeBytes()
			: thePrefs.GetFileBufferSize();
		const UploadTimerRuntimeStats uploadTimerStats = CUploadQueue::GetUploadTimerRuntimeStats();

		return nlohmann::json{
			{"autoBroadbandIo", thePrefs.IsAutoBroadbandIOEnabled()},
			{"globalDownloadBufferBudgetBytes", BroadbandIoSeams::kDefaultGlobalDownloadBufferBudgetBytes},
			{"configuredFileBufferBytes", thePrefs.GetFileBufferSize()},
			{"effectiveFileBufferBytes", uEffectiveBufferBytes},
			{"fileBufferTimeLimitMs", thePrefs.GetFileBufferTimeLimit()},
			{"totalBufferedDownloadBytes", theApp.downloadqueue != NULL ? nlohmann::json(theApp.downloadqueue->GetBufferedDownloadBytes()) : nlohmann::json(nullptr)},
			{"activeBufferedDownloadFiles", theApp.downloadqueue != NULL ? nlohmann::json(theApp.downloadqueue->GetBufferedDownloadFileCount()) : nlohmann::json(nullptr)},
			{"legacyMetadataFileBuffers", nlohmann::json{
				{"standardBytes", BroadbandIoSeams::kLegacyStandardMetadataFileBufferBytes},
				{"largeBytes", BroadbandIoSeams::kLegacyLargeMetadataFileBufferBytes}
			}},
			{"transferTimer", nlohmann::json{
				{"intervalMs", SEC2MS(1) / 10},
				{"lastDurationMs", uploadTimerStats.uLastDurationMs},
				{"maxDurationMs", uploadTimerStats.uMaxDurationMs},
				{"slowLoopCount", uploadTimerStats.uSlowLoopCount},
				{"slowLoopThresholdMs", uploadTimerStats.uSlowLoopThresholdMs}
			}},
			{"webAcceptedClient", nlohmann::json{
				{"readBufferBytes", WebSocketHttpSeams::kAcceptedClientReadBufferBytes},
				{"ioTimeoutMs", WebSocketHttpSeams::kAcceptedClientIoTimeoutMs},
				{"maxThreads", WebSocketHttpSeams::kMaxAcceptedClientThreads}
			}},
			{"socketBuffers", BuildSocketBufferJson()}
		};
	}

	static nlohmann::json BuildSystemJson()
	{
		MEMORYSTATUSEX memory = {};
		memory.dwLength = sizeof(memory);
		const bool bHasMemory = ::GlobalMemoryStatusEx(&memory) != FALSE;
		return nlohmann::json{
			{"processorArchitecture",
#if defined(_M_ARM64)
				"arm64"
#elif defined(_M_X64)
				"x64"
#else
				"unknown"
#endif
			},
			{"numberOfProcessors", static_cast<uint32>(::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS))},
			{"memoryLoadPercent", bHasMemory ? nlohmann::json(memory.dwMemoryLoad) : nlohmann::json(nullptr)},
			{"totalPhysBytes", bHasMemory ? nlohmann::json(memory.ullTotalPhys) : nlohmann::json(nullptr)},
			{"availPhysBytes", bHasMemory ? nlohmann::json(memory.ullAvailPhys) : nlohmann::json(nullptr)}
		};
	}

	static nlohmann::json BuildDiagnosticSnapshotJson(DiagnosticSnapshotSeams::ESnapshotPrivacyMode ePrivacyMode)
	{
		const CTime capturedAt = CTime::GetCurrentTime();
		return nlohmann::json{
			{"schema", "emulebb.diagnosticSnapshot.v1"},
			{"redacted", DiagnosticSnapshotSeams::IsRedacted(ePrivacyMode)},
			{"capturedAtLocal", JsonString(capturedAt.Format(_T("%Y-%m-%dT%H:%M:%S%z")))},
			{"app", nlohmann::json{
				{"productName", JsonString(CString(MOD_RELEASE_PRODUCT_NAME))},
				{"version", JsonString(theApp.m_strCurVersionLong)},
				{"debugVersion", JsonString(theApp.m_strCurVersionLongDbg)}
			}},
			{"system", BuildSystemJson()},
			{"process", BuildProcessInfoJson(ePrivacyMode)},
			{"paths", BuildPathsJson(ePrivacyMode)},
			{"network", BuildNetworkJson(ePrivacyMode)},
			{"sharedStartupCache", BuildSharedStartupCacheJson()},
			{"io", BuildIoJson()},
			{"transfers", BuildTransfersJson()},
			{"modules", BuildModulesJson(ePrivacyMode)}
		};
	}

	static void EditTextFile(const CString &rstrPath)
	{
		CString strQuotedPath;
		strQuotedPath.Format(_T("\"%s\""), (LPCTSTR)rstrPath);
		ShellOpen(thePrefs.GetTxtEditor(), strQuotedPath);
	}

	static bool BuildViewPresetColumnOrder(const MuleListCtrlViewPresets::SListControlViewPresetProfile &profile, MuleListCtrlViewPresets::ETableViewPreset ePreset, std::vector<int> &rColumnOrder)
	{
		rColumnOrder.assign(static_cast<size_t>(profile.iColumnCount), 0);
		if (ePreset == MuleListCtrlViewPresets::ETableViewPreset::Stock) {
			for (int i = 0; i < profile.iColumnCount; ++i)
				rColumnOrder[static_cast<size_t>(i)] = i;
			return true;
		}
		if (!MuleListCtrlSeams::IsCompleteColumnOrder(profile.piExtendedOrder, profile.iColumnCount))
			return false;
		rColumnOrder.assign(profile.piExtendedOrder, profile.piExtendedOrder + profile.iColumnCount);
		return true;
	}

	static bool IsViewPresetHiddenColumn(const MuleListCtrlViewPresets::SListControlViewPresetProfile &profile, MuleListCtrlViewPresets::ETableViewPreset ePreset, int iColumn)
	{
		if (ePreset == MuleListCtrlViewPresets::ETableViewPreset::Full)
			return false;
		const int *piHiddenColumns = ePreset == MuleListCtrlViewPresets::ETableViewPreset::Stock
			? profile.piStockHiddenColumns
			: profile.piExtendedHiddenColumns;
		const int iHiddenColumnCount = ePreset == MuleListCtrlViewPresets::ETableViewPreset::Stock
			? profile.iStockHiddenColumnCount
			: profile.iExtendedHiddenColumnCount;
		for (int i = 0; i < iHiddenColumnCount; ++i) {
			if (piHiddenColumns[i] == iColumn)
				return true;
		}
		return false;
	}

	static void PersistViewPresetProfile(const MuleListCtrlViewPresets::SListControlViewPresetProfile &profile, MuleListCtrlViewPresets::ETableViewPreset ePreset, MuleListCtrlViewPresets::EColumnWidthMode eWidthMode)
	{
		if (!MuleListCtrlViewPresets::IsProfileValid(profile))
			return;

		static const TCHAR *const s_apszPresetResetSuffixes[] = {
			_T("ColumnOrders"),
			_T("ColumnHidden"),
			_T("ColumnWidths"),
			_T("TableSortItem"),
			_T("TableSortAscending"),
			_T("SortHistory"),
		};

		CIni ini(thePrefs.GetConfigFile(), _T("ListControlSetup"));
		for (LPCTSTR pszSuffix : s_apszPresetResetSuffixes) {
			if (MuleListCtrlViewPresets::ShouldResetPresetSuffix(pszSuffix, eWidthMode)) {
				CString strKey(profile.pszControlName);
				strKey += pszSuffix;
				ini.DeleteKey(strKey);
			}
		}

		std::vector<int> columnOrder;
		if (!BuildViewPresetColumnOrder(profile, ePreset, columnOrder))
			return;
		ini.SerGet(false, columnOrder.data(), static_cast<int>(columnOrder.size()), CString(profile.pszControlName) + _T("ColumnOrders"));

		std::vector<int> columnHidden(static_cast<size_t>(profile.iColumnCount), 0);
		for (int i = 1; i < profile.iColumnCount; ++i)
			columnHidden[static_cast<size_t>(i)] = IsViewPresetHiddenColumn(profile, ePreset, i) ? 1 : 0;
		ini.SerGet(false, columnHidden.data(), static_cast<int>(columnHidden.size()), CString(profile.pszControlName) + _T("ColumnHidden"));

		ini.WriteInt(CString(profile.pszControlName) + _T("TableSortItem"), 0);
		ini.WriteInt(CString(profile.pszControlName) + _T("TableSortAscending"), 1);
		int iSortHistory[] = {1};
		ini.SerGet(false, iSortHistory, _countof(iSortHistory), CString(profile.pszControlName) + _T("SortHistory"));
	}

	static void UpdateServerMetFromConfiguredAddresses(CServerWnd *pServerWnd)
	{
		if (pServerWnd == NULL)
			return;
		if (thePrefs.addresses_list.IsEmpty()) {
			AddLogLine(true, GetResString(IDS_SRV_NOURLAV));
			return;
		}
		for (POSITION pos = thePrefs.addresses_list.GetHeadPosition(); pos != NULL;)
			if (pServerWnd->UpdateServerMetFromURL(thePrefs.addresses_list.GetNext(pos)))
				break;
	}

	/** Adds a display-time menu mnemonic without changing localized resources. */
	static CString WithMenuMnemonic(const CString &label, TCHAR chMnemonic)
	{
		if (label.Find(_T('&')) >= 0)
			return label;

		CString result(label);
		const TCHAR chTarget = static_cast<TCHAR>(_totlower(chMnemonic));
		for (int i = 0; i < result.GetLength(); ++i) {
			if (_totlower(result[i]) == chTarget) {
				result.Insert(i, _T('&'));
				return result;
			}
		}

		result.Insert(0, _T('&'));
		return result;
	}

	static LPCTSTR GetMainShellShortcutHint(UINT uCommandId)
	{
		switch (uCommandId) {
		case TBBTN_CONNECT:
		case MP_HM_CON:
			return _T("Alt+1 / Alt+C");
		case TBBTN_KAD:
		case MP_HM_KAD:
			return _T("Alt+2 / Alt+K");
		case TBBTN_SERVER:
		case MP_HM_SRVR:
			return _T("Alt+3 / Alt+V");
		case TBBTN_TRANSFERS:
		case MP_HM_TRANSFER:
			return _T("Alt+4 / Alt+T");
		case TBBTN_SEARCH:
		case MP_HM_SEARCH:
			return _T("Alt+5 / Alt+S");
		case TBBTN_SHARED:
		case MP_HM_FILES:
			return _T("Alt+6 / Alt+F");
		case TBBTN_MESSAGES:
		case MP_HM_MSGS:
			return _T("Alt+7 / Alt+M");
		case TBBTN_IRC:
		case MP_HM_IRC:
			return _T("Alt+8 / Alt+I");
		case TBBTN_STATS:
		case MP_HM_STATS:
			return _T("Alt+9 / Alt+A");
		case TBBTN_OPTIONS:
		case MP_HM_PREFS:
			return _T("Alt+0 / Alt+O");
		case TBBTN_TOOLS:
			return _T("Alt+W");
		case TBBTN_HELP:
		case MP_HM_HELP:
			return _T("Alt+H");
		default:
			return NULL;
		}
	}

	static CString AddMainShellShortcutLabel(const CString &label, UINT uCommandId)
	{
		return AddMenuShortcutLabel(label, GetMainShellShortcutHint(uCommandId));
	}

	static CString StripMenuMnemonics(const CString &label)
	{
		CString result;
		for (int i = 0; i < label.GetLength(); ++i) {
			if (label[i] != _T('&')) {
				result.AppendChar(label[i]);
				continue;
			}
			if (i + 1 < label.GetLength() && label[i + 1] == _T('&')) {
				result.AppendChar(_T('&'));
				++i;
			}
		}
		return result;
	}

	static CString FormatCompoundMenuLabel(UINT uFirstStringID, UINT uSecondStringID)
	{
		CString label(StripMenuMnemonics(GetResString(uFirstStringID)));
		label += _T(" / ");
		label += StripMenuMnemonics(GetResString(uSecondStringID));
		return label;
	}

	static CString FormatAllTransferCommandLabel(UINT uActionStringID)
	{
		CString label(GetResString(uActionStringID));
		label += _T(" ");
		label += GetResString(IDS_ALL);
		return label;
	}

	static LPCTSTR GetDocumentationLinkURL(UINT uCommandID)
	{
		size_t uLinkCount = 0;
		const AppWebLinksSeams::SWebLink *pLinks = AppWebLinksSeams::GetDocumentationLinks(uLinkCount);
		for (size_t i = 0; i < uLinkCount; ++i) {
			if (pLinks[i].uCommandID == uCommandID)
				return pLinks[i].pszUrl;
		}
		return NULL;
	}

	static void AppendLinksMenuEntries(CTitledMenu &rMenu)
	{
		rMenu.AppendMenu(MF_STRING, MP_HM_LINK1, GetResString(IDS_HM_LINKHP), _T("WEB"));
		rMenu.AppendMenu(MF_STRING, MP_HM_LINK2, GetResString(IDS_HM_LINKFAQ), _T("WEB"));
		rMenu.AppendMenu(MF_STRING, MP_HM_LINK3, GetResString(IDS_HM_LINKVC), _T("WEB"));
		rMenu.AppendMenu(MF_SEPARATOR);

		size_t uLinkCount = 0;
		const AppWebLinksSeams::SWebLink *pLinks = AppWebLinksSeams::GetDocumentationLinks(uLinkCount);
		for (size_t i = 0; i < uLinkCount; ++i)
			rMenu.AppendMenu(MF_STRING, pLinks[i].uCommandID, GetResString(pLinks[i].uLabelStringID), _T("WEB"));

		rMenu.AppendMenu(MF_SEPARATOR);
		if (theWebServices.GetGeneralMenuEntries(&rMenu) > 0)
			rMenu.AppendMenu(MF_SEPARATOR);
		rMenu.AppendMenu(MF_STRING, MP_WEBSVC_EDIT, GetResString(IDS_WEBSVEDIT));
	}

	static UINT GetToolsMenuStatusStringID(UINT nItemID)
	{
		switch (nItemID) {
		case MP_HM_CON:
			return IDS_TOOLS_STATUS_SESSION_CONNECT;
		case MP_HM_SRVR:
			return IDS_TOOLS_STATUS_SHOW_SERVER;
		case MP_HM_TRANSFER:
			return IDS_TOOLS_STATUS_SHOW_TRANSFERS;
		case MP_HM_SEARCH:
			return IDS_TOOLS_STATUS_SHOW_SEARCH;
		case MP_HM_FILES:
			return IDS_TOOLS_STATUS_SHOW_SHARED_FILES;
		case MP_HM_MSGS:
			return IDS_TOOLS_STATUS_SHOW_MESSAGES;
		case MP_HM_IRC:
			return IDS_TOOLS_STATUS_SHOW_IRC;
		case MP_HM_STATS:
			return IDS_TOOLS_STATUS_SHOW_STATISTICS;
		case MP_HM_PREFS:
			return IDS_TOOLS_STATUS_SHOW_OPTIONS;
		case MP_MINIMIZETOTRAY:
			return IDS_TOOLS_STATUS_MINIMIZE_TO_TRAY;
		case MP_HM_REFRESH_INTERVAL_FAST:
		case MP_HM_REFRESH_INTERVAL_NORMAL:
		case MP_HM_REFRESH_INTERVAL_BELOWNORMAL:
		case MP_HM_REFRESH_INTERVAL_SLOW:
		case MP_HM_REFRESH_INTERVAL_VERYSLOW:
			return IDS_TOOLS_STATUS_REFRESH_INTERVAL;
		case MP_HM_RESTART_APP:
			return IDS_TOOLS_STATUS_RESTART;
		case MP_HM_EXIT:
			return IDS_TOOLS_STATUS_EXIT;
		case MP_HM_EDIT_PREFERENCES_INI:
			return IDS_TOOLS_STATUS_EDIT_PREFERENCES_INI;
		case MP_HM_EDIT_IPFILTER_DAT:
			return IDS_TOOLS_STATUS_EDIT_IPFILTER_DAT;
		case MP_HM_RELOAD_IPFILTER_DAT:
			return IDS_TOOLS_STATUS_RELOAD_IPFILTER_DAT;
		case MP_HM_EDIT_FAKEFILEFILTER_DAT:
			return IDS_TOOLS_STATUS_EDIT_FAKEFILEFILTER_DAT;
		case MP_HM_RELOAD_FAKEFILEFILTER:
			return IDS_TOOLS_STATUS_RELOAD_FAKEFILEFILTER;
		case MP_HM_EDIT_ADDRESSES_DAT:
			return IDS_TOOLS_STATUS_EDIT_ADDRESSES_DAT;
		case MP_HM_EDIT_WEBSERVICES_DAT:
			return IDS_TOOLS_STATUS_EDIT_WEBSERVICES_DAT;
		case MP_HM_EDIT_STATIC_SERVERS_DAT:
			return IDS_TOOLS_STATUS_EDIT_STATIC_SERVERS_DAT;
		case MP_HM_EDIT_SHAREDDIR_DAT:
			return IDS_TOOLS_STATUS_EDIT_SHAREDDIR_DAT;
		case MP_HM_EDIT_MONITORED_SHAREDDIR_DAT:
			return IDS_TOOLS_STATUS_EDIT_MONITORED_SHAREDDIR_DAT;
		case MP_HM_EDIT_MONITOR_OWNED_SHAREDDIR_DAT:
			return IDS_TOOLS_STATUS_EDIT_MONITOR_OWNED_SHAREDDIR_DAT;
		case MP_HM_EDIT_SHAREIGNORE_DAT:
			return IDS_TOOLS_STATUS_EDIT_SHAREIGNORE_DAT;
		case MP_HM_MANAGE_CATEGORIES:
			return IDS_TOOLS_STATUS_MANAGE_CATEGORIES;
		case MP_HM_EDIT_CATEGORY_INI:
			return IDS_TOOLS_STATUS_EDIT_CATEGORY_INI;
		case MP_HM_EDIT_NOTIFIER_INI:
			return IDS_TOOLS_STATUS_EDIT_NOTIFIER_INI;
		case MP_HM_EDIT_FILEINFO_INI:
			return IDS_TOOLS_STATUS_EDIT_FILEINFO_INI;
		case MP_HM_EDIT_STATISTICS_INI:
			return IDS_TOOLS_STATUS_EDIT_STATISTICS_INI;
		case MP_HM_RELOAD_SHAREIGNORE_DAT:
			return IDS_TOOLS_STATUS_RELOAD_SHAREIGNORE_DAT;
		case MP_HM_RESCAN_SHARED_FILES:
			return IDS_TOOLS_STATUS_RESCAN_SHARED_FILES;
		case MP_HM_SAVE_PREFERENCES_NOW:
			return IDS_TOOLS_STATUS_SAVE_PREFERENCES_NOW;
		case MP_HM_UPDATE_SERVERMET_FROM_ADDRESSES:
			return IDS_TOOLS_STATUS_UPDATE_SERVERMET_FROM_ADDRESSES;
		case MP_HM_OPEN_EMULE_LOG:
			return IDS_TOOLS_STATUS_OPEN_EMULE_LOG;
		case MP_HM_OPEN_VERBOSE_LOG:
			return IDS_TOOLS_STATUS_OPEN_VERBOSE_LOG;
		case MP_HM_COPY_DIAGNOSTIC_SNAPSHOT_JSON:
			return IDS_TOOLS_STATUS_COPY_DIAGNOSTIC_SNAPSHOT_JSON;
		case MP_HM_COPY_REDACTED_DIAGNOSTIC_SNAPSHOT_JSON:
			return IDS_TOOLS_STATUS_COPY_REDACTED_DIAGNOSTIC_SNAPSHOT_JSON;
		case MP_HM_REPAIR_WINDOWS_FIREWALL:
			return IDS_TOOLS_STATUS_REPAIR_WINDOWS_FIREWALL_RULES;
		case MP_HM_REGISTER_PROWLARR:
			return IDS_TOOLS_STATUS_REGISTER_PROWLARR;
		case MP_HM_REGISTER_RADARR:
			return IDS_TOOLS_STATUS_REGISTER_RADARR;
		case MP_HM_REGISTER_SONARR:
			return IDS_TOOLS_STATUS_REGISTER_SONARR;
		case MP_HM_ENABLE_WINDOWS_LONG_PATHS:
			return IDS_TOOLS_STATUS_ENABLE_WINDOWS_LONG_PATHS;
		case MP_HM_DEFENDER_EXCLUDE_DOWNLOAD_FOLDERS:
			return IDS_TOOLS_STATUS_DEFENDER_EXCLUDE_DOWNLOAD_FOLDERS;
		case MP_HM_VIEW_PRESET_STOCK_KEEP_WIDTHS:
		case MP_HM_VIEW_PRESET_STOCK_RESET_WIDTHS:
			return IDS_TOOLS_STATUS_VIEW_PRESET_STOCK;
		case MP_HM_VIEW_PRESET_EXTENDED_KEEP_WIDTHS:
		case MP_HM_VIEW_PRESET_EXTENDED_RESET_WIDTHS:
			return IDS_TOOLS_STATUS_VIEW_PRESET_EXTENDED;
		case MP_HM_VIEW_PRESET_FULL_KEEP_WIDTHS:
		case MP_HM_VIEW_PRESET_FULL_RESET_WIDTHS:
			return IDS_TOOLS_STATUS_VIEW_PRESET_FULL;
		default:
			return 0;
		}
	}

	static CString BuildLocalEmulebbWebBaseUrl()
	{
		CString strHost(thePrefs.GetWebBindAddr());
		strHost.Trim();
		if (strHost.IsEmpty() || strHost == _T("0.0.0.0"))
			strHost = _T("127.0.0.1");
		CString strBaseUrl;
		strBaseUrl.Format(_T("http://%s:%u"), (LPCTSTR)strHost, thePrefs.GetWSPort());
		return strBaseUrl;
	}

	static CString FormatLogDefault(const CString& strValue, LPCTSTR pszDefault)
	{
		CString strText(strValue);
		strText.Trim();
		return strText.IsEmpty() ? CString(pszDefault) : strText;
	}

	static CString FormatActiveP2PBindAddress()
	{
		if (thePrefs.GetBindAddr() == NULL || *thePrefs.GetBindAddr() == _T('\0'))
			return _T("default");
		return thePrefs.GetBindAddr();
	}

	static void LogStartupNetworkGuardSummary()
	{
		AddLogLine(false, _T("Active P2P binding: interface=%s configuredAddress=%s resolvedAddress=%s resolveResult=%u"),
			(LPCTSTR)FormatLogDefault(thePrefs.GetActiveBindInterfaceName(), _T("default")),
			(LPCTSTR)FormatLogDefault(thePrefs.GetActiveConfiguredBindAddr(), _T("auto")),
			(LPCTSTR)FormatActiveP2PBindAddress(),
			static_cast<UINT>(thePrefs.GetActiveBindAddressResolveResult()));

		CString strAllowedCidrs(thePrefs.GetVpnGuardAllowedPublicIpCidrs());
		strAllowedCidrs.Trim();
		const bool bHasAllowedCidrs = !strAllowedCidrs.IsEmpty();
		const CString strAllowedCidrsDisplay(bHasAllowedCidrs ? strAllowedCidrs : CString(_T("none")));
		AddLogLine(false, _T("VPN Guard: mode=%s allowedCIDRs=%s behavior=%s"),
			(LPCTSTR)VpnGuardSeams::GetModePreferenceText(thePrefs.GetVpnGuardMode()),
			(LPCTSTR)strAllowedCidrsDisplay,
			bHasAllowedCidrs
				? _T("block P2P startup/runtime when the detected public IP is outside the allowed ranges")
				: _T("bind-interface guard only; public IP range check disabled"));
	}

	static bool LaunchBundledInteractiveScript(LPCTSTR pszScriptName, CString strArguments, ElevatedPowerShellAction::CLaunchResult &rResult)
	{
		if (!ElevatedPowerShellAction::PrepareBundledScript(_T("eMuleBB-Tools"), pszScriptName, CString(), rResult))
			return false;
		return ElevatedPowerShellAction::RunBundledScript(strArguments, false, false, rResult);
	}

	static CString GetNotifierFallbackTitle(TbnMsg nMsgType)
	{
		switch (nMsgType) {
		case TBN_CHAT:
			return _T("Chat");
		case TBN_DOWNLOADFINISHED:
			return _T("Download finished");
		case TBN_DOWNLOADADDED:
			return _T("Download added");
		case TBN_LOG:
			return _T("Log");
		case TBN_IMPORTANTEVENT:
			return _T("Important event");
		case TBN_NEWVERSION:
			return _T("New version");
		default:
			return MOD_RELEASE_PRODUCT_NAME;
		}
	}

	static void SplitNotifierText(LPCTSTR pszText, TbnMsg nMsgType, CString &strTitle, CString &strBody)
	{
		CString strText(pszText != NULL ? pszText : _T(""));
		strText.Replace(_T("\r\n"), _T("\n"));
		strText.Replace(_T("\r"), _T("\n"));
		strText.Trim();

		const int iBreak = strText.Find(_T('\n'));
		if (iBreak >= 0) {
			strTitle = strText.Left(iBreak);
			strBody = strText.Mid(iBreak + 1);
		} else {
			strTitle = GetNotifierFallbackTitle(nMsgType);
			strBody = strText;
		}

		strTitle.Trim();
		strBody.Trim();
		if (strTitle.IsEmpty())
			strTitle = GetNotifierFallbackTitle(nMsgType);
		if (strBody.IsEmpty())
			strBody = strTitle;
	}

	static DWORD GetTrayBalloonInfoFlags(TbnMsg nMsgType)
	{
		return nMsgType == TBN_IMPORTANTEVENT ? NIIF_WARNING : NIIF_INFO;
	}

	static TrayNotificationSeams::ENotifierDisplayMode MapTrayNotifierDisplayMode(ENotifierDisplayMode eDisplayMode)
	{
		switch (eDisplayMode) {
		case ntfdmWindowsToast:
			return TrayNotificationSeams::ENotifierDisplayMode::WindowsToast;
		case ntfdmTrayBalloon:
			return TrayNotificationSeams::ENotifierDisplayMode::TrayBalloon;
		case ntfdmCustomPopup:
		default:
			return TrayNotificationSeams::ENotifierDisplayMode::CustomPopup;
		}
	}

	static void PostBindInterfaceChanged(PVOID pContext)
	{
		const HWND hWnd = reinterpret_cast<HWND>(pContext);
		if (hWnd != NULL && ::IsWindow(hWnd))
			(void)::PostMessage(hWnd, UM_BIND_INTERFACE_CHANGED, 0, 0);
	}

	static VOID CALLBACK BindLossIpInterfaceChangeCallback(PVOID pContext, PMIB_IPINTERFACE_ROW, MIB_NOTIFICATION_TYPE) noexcept
	{
		PostBindInterfaceChanged(pContext);
	}

	static VOID CALLBACK BindLossUnicastAddressChangeCallback(PVOID pContext, PMIB_UNICASTIPADDRESS_ROW, MIB_NOTIFICATION_TYPE) noexcept
	{
		PostBindInterfaceChanged(pContext);
	}

	static void AddUniqueDefenderDirectory(std::vector<CString> &paths, const CString &rstrPath)
	{
		CString strPath(rstrPath);
		strPath.Trim();
		if (strPath.IsEmpty())
			return;

		strPath = PathHelpers::CanonicalizeDirectoryPath(strPath);
		for (const CString &strExisting : paths) {
			if (PathHelpers::ArePathsEquivalent(strExisting, strPath))
				return;
		}
		paths.push_back(strPath);
	}

	static std::vector<CString> BuildDefenderDownloadFolderExclusions()
	{
		std::vector<CString> paths;
		AddUniqueDefenderDirectory(paths, thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
		for (INT_PTR iTempDir = 0; iTempDir < thePrefs.GetTempDirCount(); ++iTempDir)
			AddUniqueDefenderDirectory(paths, thePrefs.GetTempDir(iTempDir));
		for (INT_PTR iCategory = 0; iCategory < thePrefs.GetCatCount(); ++iCategory) {
			const Category_Struct *pCategory = thePrefs.GetCategory(iCategory);
			if (pCategory != NULL)
				AddUniqueDefenderDirectory(paths, pCategory->strIncomingPath);
		}
		return paths;
	}

	struct SVersionCheckContext
	{
		HWND hNotifyWnd = NULL;
		std::shared_ptr<VersionCheckLaunchSeams::SQueuedState> pQueuedState;
		bool bManual = false;
	};

	struct SVersionCheckResult
	{
		bool bManual = false;
		ReleaseUpdateCheck::SUpdateCheckResult result;
	};

	UINT AFX_CDECL VersionCheckThreadProc(LPVOID pParam)
	{
		std::unique_ptr<SVersionCheckContext> pContext(reinterpret_cast<SVersionCheckContext*>(pParam));
		if (pContext.get() == NULL)
			return 0;

		std::unique_ptr<SVersionCheckResult> pResult(new SVersionCheckResult);
		pResult->bManual = pContext->bManual;
		pResult->result = ReleaseUpdateCheck::CheckLatestRelease();

		const VersionCheckLaunchSeams::SCompletionPostResult postResult = VersionCheckLaunchSeams::PostCompletion(
			pContext->hNotifyWnd,
			UM_VERSIONCHECK_RESPONSE,
			reinterpret_cast<LPARAM>(pResult.get()),
			pContext->pQueuedState);
		if (postResult.bDelivered)
			(void)pResult.release();
		else
			AddDebugLogLine(false, _T("Version check: failed to deliver background update result (%lu)."), postResult.dwLastError);
		return 0;
	}

	CUpDownClient* ResolveQueuedClient(const CClientDisplayUpdateRequest &request)
	{
		if (theApp.clientlist == NULL)
			return NULL;
		if (!isnulmd4(request.userHash)) {
			if (CUpDownClient *pClient = theApp.clientlist->FindClientByUserHash(request.userHash, request.connectIP, request.userPort))
				return pClient;
		}
		if (request.connectIP != 0 && request.userPort != 0) {
			if (CUpDownClient *pClient = theApp.clientlist->FindClientByIP(request.connectIP, request.userPort))
				return pClient;
		}
		if (request.connectIP != 0)
			return theApp.clientlist->FindClientByIP(request.connectIP);
		return NULL;
	}

	CString FormatStartupCacheShutdownDetail(const CSharedFileList::StartupCacheSaveProgress &progress)
	{
		CString strDetail;
		switch (progress.ePhase) {
		case CSharedFileList::StartupCacheSavePhase::BuildingRecords:
			strDetail.Format(_T("Saving shared startup cache: scanning directories (%I64u/%I64u)."), progress.uCompletedDirectories, progress.uTotalDirectories);
			break;
		case CSharedFileList::StartupCacheSavePhase::WritingFile:
			strDetail = _T("Saving shared startup cache: writing cache file to disk.");
			break;
		case CSharedFileList::StartupCacheSavePhase::ApplyingResult:
			strDetail = _T("Saving shared startup cache: finalizing cache metadata.");
			break;
		default:
			strDetail = progress.bDirty
				? _T("Saving shared startup cache: waiting for the final dirty snapshot to start.")
				: _T("Saving shared startup cache: waiting for the background worker to finish.");
			break;
		}
		if (progress.bWaitingForFollowUp)
			strDetail += _T(" A follow-up save is queued because shared state changed during the current run.");
		return strDetail;
	}
}



///////////////////////////////////////////////////////////////////////////
// CemuleDlg Dialog

IMPLEMENT_DYNAMIC(CMsgBoxException, CException)

BEGIN_MESSAGE_MAP(CemuleDlg, CTrayDialog)
	///////////////////////////////////////////////////////////////////////////
	// Windows messages
	//
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_ENDSESSION()
	ON_WM_ACTIVATE()
	ON_WM_SIZE()
	ON_WM_CLOSE()
	ON_WM_MENUCHAR()
	ON_WM_MENUSELECT()
	ON_WM_QUERYENDSESSION()
	ON_WM_SYSCOLORCHANGE()
	ON_WM_CTLCOLOR()
	ON_MESSAGE(WM_COPYDATA, OnWMData)
	ON_MESSAGE(WM_KICKIDLE, OnKickIdle)
	ON_MESSAGE(WM_USERCHANGED, OnUserChanged)
	ON_MESSAGE(UM_STARTUP_NEXT_STAGE, OnStartupNextStage)
	ON_MESSAGE(UM_VERSIONCHECK_RESPONSE, OnVersionCheckResponse)
	ON_MESSAGE(UM_GEOLOCATION_UPDATED, OnGeoLocationUpdated)
	ON_MESSAGE(UM_IPFILTER_UPDATED, OnIPFilterUpdated)
	ON_MESSAGE(UM_BIND_INTERFACE_CHANGED, OnBindInterfaceChanged)
	ON_MESSAGE(UM_VPN_GUARD_PROBE_RESULT, OnVpnGuardProbeResult)
	ON_WM_SHOWWINDOW()
	ON_WM_DESTROY()
	ON_WM_SETTINGCHANGE()
	ON_WM_TIMER()
	ON_WM_DEVICECHANGE()
	ON_MESSAGE(WM_DISPLAYCHANGE, OnDisplayChange)
	ON_MESSAGE(WM_POWERBROADCAST, OnPowerBroadcast)

	///////////////////////////////////////////////////////////////////////////
	// WM_COMMAND messages
	//
	ON_COMMAND(MP_CONNECT, StartConnection)
	ON_COMMAND(MP_DISCONNECT, CloseConnection)
	ON_COMMAND(MP_EXIT, OnClose)
	ON_COMMAND(MP_RESTORE, RestoreWindow)
	// quick-speed changer --
	ON_COMMAND_RANGE(MP_QS_U10, MP_QS_UP10, QuickSpeedUpload)
	ON_COMMAND_RANGE(MP_QS_D10, MP_QS_DC, QuickSpeedDownload)
	ON_COMMAND_RANGE(MP_QS_B10, MP_QS_B90, QuickSpeedBoth)
	//--- quickspeed - paralize all ---
	ON_COMMAND_RANGE(MP_QS_PA, MP_QS_UA, QuickSpeedOther)
	// quick-speed changer -- based on xrmb
	ON_NOTIFY_EX_RANGE(RBN_CHEVRONPUSHED, 0, 0xffff, OnChevronPushed)

	ON_REGISTERED_MESSAGE(UWM_ARE_YOU_EMULE, OnAreYouEmule)
	ON_BN_CLICKED(IDC_EXIT, OnClose)
	ON_BN_CLICKED(IDC_HOTMENU, OnBnClickedHotmenu)

	///////////////////////////////////////////////////////////////////////////
	// WM_USER messages
	//
	ON_MESSAGE(UM_TASKBARNOTIFIERCLICKED, OnTaskbarNotifierClicked)
	ON_MESSAGE(UM_WINDOWS_TOAST_CLICKED, OnWindowsToastClicked)

	// Web Server messages
	ON_MESSAGE(WEB_GUI_INTERACTION, OnWebGUIInteraction)
	ON_MESSAGE(WEB_CLEAR_COMPLETED, OnWebServerClearCompleted)
	ON_MESSAGE(WEB_REST_API_COMMAND, OnWebRestApiCommand)
	ON_MESSAGE(WEB_FILE_RENAME, OnWebServerFileRename)
	ON_MESSAGE(WEB_ADDDOWNLOADS, OnWebAddDownloads)
	ON_MESSAGE(WEB_CATPRIO, OnWebSetCatPrio)
	ON_MESSAGE(WEB_ADDREMOVEFRIEND, OnAddRemoveFriend)

	// UPnP
	ON_MESSAGE(UM_UPNP_RESULT, OnUPnPResult)
	ON_MESSAGE(UM_PARTFILE_DISPLAY_UPDATE, OnPartFileDisplayUpdate)
	ON_MESSAGE(UM_CLIENT_DISPLAY_UPDATE, OnClientDisplayUpdate)
	ON_MESSAGE(UM_PARTFILE_PROGRESS_UPDATE, OnPartFileProgressUpdate)
	ON_MESSAGE(UM_STARTUP_CACHE_SAVE_COMPLETE, OnStartupCacheSaveComplete)

	///////////////////////////////////////////////////////////////////////////
	// WM_APP messages
	//
	ON_MESSAGE(TM_FINISHEDHASHING, OnFileHashed)
	ON_MESSAGE(TM_FILEOPPROGRESS, OnFileOpProgress)
	ON_MESSAGE(TM_HASHFAILED, OnHashFailed)
	ON_MESSAGE(TM_SHAREDFILEHASHED, OnSharedFileHashed)
	ON_MESSAGE(TM_SHAREDFILEHASHFAILED, OnSharedHashFailed)
	ON_MESSAGE(TM_SHAREDHASHRESULTSAVAILABLE, OnSharedHashResultsAvailable)
	ON_MESSAGE(TM_PEERPREVIEWFINISHED, OnPeerPreviewFinished)
	ON_MESSAGE(TM_FILEALLOCEXC, OnFileAllocExc)
	ON_MESSAGE(TM_FILECOMPLETED, OnFileCompleted)
	ON_MESSAGE(TM_CONSOLETHREADEVENT, OnConsoleThreadEvent)

#ifdef HAVE_WIN7_SDK_H
	ON_REGISTERED_MESSAGE(UWM_TASK_BUTTON_CREATED, OnTaskbarBtnCreated)
#endif

END_MESSAGE_MAP()

CemuleDlg::CemuleDlg(CWnd *pParent /*=NULL*/)
	: CTrayDialog(CemuleDlg::IDD, pParent)
	, m_pStartupProgressDlg()
	, m_bStartupProgressFinished()
	, activewnd()
	, status()
	, m_wpFirstRestore()
	, m_hIcon()
	, m_hLowIDIcon()
	, m_eMainConnectionIcon(AppMainIconSeams::EConnectionIcon::Unknown)
	, m_connicons()
	, transicons()
	, imicons()
	, m_icoSysTrayCurrent()
	, usericon()
	, m_icoSysTrayConnected()
	, m_icoSysTrayDisconnected()
	, m_icoSysTrayLowID()
	, m_pMiniMule()
	, m_pSystrayDlg()
	, m_pDropTarget()
	, m_iMsgIcon()
	, m_uLastSysTrayIconCookie(SYS_TRAY_ICON_COOKIE_FORCE_UPDATE)
	, m_uUpDatarate()
	, m_uDownDatarate()
	, m_pVersionCheckState(std::make_shared<VersionCheckLaunchSeams::SQueuedState>())
	, m_bStartMinimizedChecked()
	, m_bStartMinimized()
	, m_bMsgBlinkState()
	, m_bConnectRequestDelayedForUPnP()
	, m_bKadSuspendDisconnect()
	, m_bEd2kSuspendDisconnect()
	, m_bTrayBalloonFallbackForSession()
	, m_bTransientDialogActive()
	, m_bInitedCOM()
	, m_bBindLossMonitorActive()
	, m_bBindLossShutdown()
	, m_bVpnGuardStartupProbePending()
	, m_bVpnGuardStartupApproved()
	, m_bVpnGuardRuntimeProbePending()
	, m_uVpnGuardProbeGeneration()
	, m_ullLastVpnGuardRuntimeProbeTick()
	, m_uBindLossWatchdogTimer()
	, m_uTransferRateDisplayTimer()
	, m_hBindLossInterfaceNotification()
	, m_hBindLossAddressNotification()
	, m_pAICHSyncThread()
	, m_thbButtons()
	, m_currentTBP_state(TBPF_NOPROGRESS)
	, m_prevProgress()
	, m_ovlIcon()
	, m_hUPnPTimeOutTimer()
	, notifierenabled()
{
	g_uMainThreadId = GetCurrentThreadId();
	SetClientIconList();
	preferenceswnd = new CPreferencesDlg;
	serverwnd = new CServerWnd;
	kademliawnd = new CKademliaWnd;
	transferwnd = new CTransferDlg;
	sharedfileswnd = new CSharedFilesWnd;
	searchwnd = new CSearchDlg;
	chatwnd = new CChatWnd;
	ircwnd = new CIrcWnd;
	statisticswnd = new CStatisticsDlg;
	toolbar = new CMuleToolbarCtrl;
	statusbar = new CMuleStatusBarCtrl;
	m_pDropTarget = new CMainFrameDropTarget;
}

void CemuleDlg::StartAICHSyncThread()
{
	if (m_pAICHSyncThread != NULL || theApp.IsClosing())
		return;

	CWinThread *pThread = AfxBeginThread(RUNTIME_CLASS(CAICHSyncThread), THREAD_PRIORITY_IDLE, 0, CREATE_SUSPENDED);
	DWORD dwResumeError = ERROR_SUCCESS;
	if (!HelperThreadLaunchSeams::OwnAndResumeSuspendedThread(m_pAICHSyncThread, pThread, dwResumeError)) {
		if (dwResumeError != ERROR_SUCCESS)
			DebugLogWarning(_T("Failed to resume AICH sync thread - Error %lu"), dwResumeError);
		else
			DebugLogWarning(_T("Failed to create AICH sync thread"));
	}
}

void CemuleDlg::WaitForAICHSyncThreadShutdown()
{
	CWinThread *pThread = m_pAICHSyncThread;
	if (pThread == NULL)
		return;

	const HANDLE hThread = pThread->m_hThread;
	if (hThread != NULL) {
		const DWORD dwWait = ::WaitForSingleObject(hThread, kAICHSyncThreadShutdownWaitMs);
		const EAICHSyncThreadShutdownWaitAction eAction = GetAICHSyncThreadShutdownWaitAction(dwWait);
		if (eAction == EAICHSyncThreadShutdownWaitAction::TimedOut) {
			DebugLogError(_T("AICH sync thread exceeded shutdown wait budget; preserving shared/known-file lifetime until it exits."));
			(void)::WaitForSingleObject(hThread, INFINITE);
		} else if (eAction == EAICHSyncThreadShutdownWaitAction::Failed) {
			const DWORD dwLastError = (dwWait == WAIT_FAILED) ? ::GetLastError() : ERROR_SUCCESS;
			DebugLogWarning(_T("AICH sync thread shutdown wait failed (%lu); waiting without a timeout."), dwLastError);
			(void)::WaitForSingleObject(hThread, INFINITE);
		}
	}

	delete pThread;
	m_pAICHSyncThread = NULL;
}

void CemuleDlg::SetClientIconList()
{
	m_IconList.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	m_IconList.Add(CTempIconLoader(_T("ClientEDonkey")));			//0 - eDonkey
	m_IconList.Add(CTempIconLoader(_T("ClientEDonkeyPlus")));
	m_IconList.Add(CTempIconLoader(_T("ClientCompatible")));		//2 - Compat
	m_IconList.Add(CTempIconLoader(_T("ClientCompatiblePlus")));
	m_IconList.Add(CTempIconLoader(_T("Friend")));					//4 - friend
	m_IconList.Add(CTempIconLoader(_T("ClientMLDonkey")));			//5 - ML
	m_IconList.Add(CTempIconLoader(_T("ClientMLDonkeyPlus")));
	m_IconList.Add(CTempIconLoader(_T("ClientEDonkeyHybrid")));	//7 - Hybrid
	m_IconList.Add(CTempIconLoader(_T("ClientEDonkeyHybridPlus")));
	m_IconList.Add(CTempIconLoader(_T("ClientShareaza")));			//9 - Shareaza
	m_IconList.Add(CTempIconLoader(_T("ClientShareazaPlus")));
	m_IconList.Add(CTempIconLoader(_T("ClientAMule")));			//11 - amule
	m_IconList.Add(CTempIconLoader(_T("ClientAMulePlus")));
	m_IconList.Add(CTempIconLoader(_T("ClientLPhant")));			//13 - Lphant
	m_IconList.Add(CTempIconLoader(_T("ClientLPhantPlus")));
	m_IconList.Add(CTempIconLoader(_T("Server")));					//15 - http source
	m_IconList.SetOverlayImage(m_IconList.Add(CTempIconLoader(_T("ClientSecureOvl"))), 1);
	m_IconList.SetOverlayImage(m_IconList.Add(CTempIconLoader(_T("OverlayObfu"))), 2);
	m_IconList.SetOverlayImage(m_IconList.Add(CTempIconLoader(_T("OverlaySecureObfu"))), 3);
}

CemuleDlg::~CemuleDlg()
{
	DestroyStartupProgress();
	DestroyMiniMule();
	CloseTTS();
	if (m_icoSysTrayCurrent)
		VERIFY(::DestroyIcon(m_icoSysTrayCurrent));
	if (m_hIcon)
		VERIFY(::DestroyIcon(m_hIcon));
	if (m_hLowIDIcon)
		VERIFY(::DestroyIcon(m_hLowIDIcon));
	DestroyIconsArr(m_connicons, _countof(m_connicons));
	DestroyIconsArr(transicons, _countof(transicons));
	DestroyIconsArr(imicons, _countof(imicons));
	if (m_icoSysTrayConnected)
		VERIFY(::DestroyIcon(m_icoSysTrayConnected));
	if (m_icoSysTrayDisconnected)
		VERIFY(::DestroyIcon(m_icoSysTrayDisconnected));
	if (m_icoSysTrayLowID)
		VERIFY(::DestroyIcon(m_icoSysTrayLowID));
	if (usericon)
		VERIFY(::DestroyIcon(usericon));

#ifdef HAVE_WIN7_SDK_H
	if (m_pTaskbarList != NULL) {
		m_pTaskbarList.Release();
		ASSERT(m_bInitedCOM);
	}
	if (m_bInitedCOM)
		::CoUninitialize();
#endif

	// already destroyed by windows?
	//VERIFY(m_menuUploadCtrl.DestroyMenu());
	//VERIFY(m_menuDownloadCtrl.DestroyMenu());
	//VERIFY(m_SysMenuOptions.DestroyMenu());

	delete m_pDropTarget;
	delete statusbar;
	delete toolbar;
	delete statisticswnd;
	delete ircwnd;
	delete chatwnd;
	delete sharedfileswnd;
	delete kademliawnd;
	delete serverwnd;
	delete preferenceswnd;
}

void CemuleDlg::DoDataExchange(CDataExchange *pDX)
{
	CTrayDialog::DoDataExchange(pDX);
}

LRESULT CemuleDlg::OnAreYouEmule(WPARAM, LPARAM)
{
	return UWM_ARE_YOU_EMULE;
}

static void DialogCreateIndirect(CDialog *pWnd, UINT uID)
{
	pWnd->Create(uID);
}

BOOL CemuleDlg::OnInitDialog()
{
#if EMULEBB_HAS_STARTUP_PROFILING
	const ULONGLONG ullDialogInitStart = theApp.GetStartupProfileTimestampUs();
#endif
	theStats.starttime = ::GetTickCount64();
#ifdef HAVE_WIN7_SDK_H
	// allow the TaskbarButtonCreated- & (tbb-)WM_COMMAND message to be sent to our window if our app is running elevated
	m_bInitedCOM = SUCCEEDED(::CoInitialize(NULL));
	if (m_bInitedCOM) {
		::ChangeWindowMessageFilter(UWM_TASK_BUTTON_CREATED, MSGFLT_ADD);
		::ChangeWindowMessageFilter(WM_COMMAND, MSGFLT_ADD);
	} else
		ASSERT(0);
#endif

	// temporary disable the 'startup minimized' option, otherwise no window will be shown at all
	if (!thePrefs.IsFirstStart())
		m_bStartMinimized = thePrefs.GetStartMinimized() || theApp.DidWeAutoStart();

	// Show startup progress as early as possible while the main window is being prepared.
	ShowStartupProgress();

	// Create global GUI objects
#if EMULEBB_HAS_STARTUP_PROFILING
	ULONGLONG ullPhaseStart = theApp.GetStartupProfileTimestampUs();
#endif
	theApp.CreateAllFonts();
	theApp.CreateBackwardDiagonalBrush();
	m_wndTaskbarNotifier.SetTextDefaultFont();
	CTrayDialog::OnInitDialog();
	InitWindowStyles(this);
	CreateToolbarCmdIconMap();
	UpdateStartupProgress(8, IDS_STARTUP_PROGRESS_STARTING, IDS_STARTUP_PROGRESS_CREATING_UI);
#if EMULEBB_HAS_STARTUP_PROFILING
	theApp.AppendStartupProfileLine(_T("CemuleDlg::OnInitDialog base window/font init"), theApp.GetStartupProfileElapsedUs(ullPhaseStart));
#endif

	CMenu *pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL) {
		pSysMenu->AppendMenu(MF_SEPARATOR);

		ASSERT((MP_ABOUTBOX & 0xFFF0) == MP_ABOUTBOX && MP_ABOUTBOX < 0xF000);
		pSysMenu->AppendMenu(MF_STRING, MP_ABOUTBOX, GetResString(IDS_ABOUTBOX));

		ASSERT((MP_VERSIONCHECK & 0xFFF0) == MP_VERSIONCHECK && MP_VERSIONCHECK < 0xF000);
		pSysMenu->AppendMenu(MF_STRING, MP_VERSIONCHECK, GetResString(IDS_VERSIONCHECK));

		// remaining system menu entries are created later...
	}

	CWnd *pwndToolbarX = toolbar;
	if (toolbar->Create(WS_CHILD | WS_VISIBLE, RECT(), this, IDC_TOOLBAR)) {
		toolbar->Init();
		if (thePrefs.GetUseReBarToolbar()) {
			if (m_ctlMainTopReBar.Create(WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
				RBS_BANDBORDERS | RBS_AUTOSIZE | CCS_NODIVIDER,
				RECT(), this, AFX_IDW_REBAR))
			{
				SIZE sizeBar;
				VERIFY(toolbar->GetMaxSize(&sizeBar));
				REBARBANDINFO rbbi = {};
				rbbi.cbSize = (UINT)sizeof rbbi;
				rbbi.fMask = RBBIM_STYLE | RBBIM_SIZE | RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_IDEALSIZE | RBBIM_ID;
				rbbi.fStyle = RBBS_NOGRIPPER | RBBS_BREAK | RBBS_USECHEVRON;
				rbbi.hwndChild = toolbar->m_hWnd;
				rbbi.cxMinChild = sizeBar.cy;
				rbbi.cyMinChild = sizeBar.cy;
				rbbi.cxIdeal = sizeBar.cx;
				rbbi.cx = rbbi.cxIdeal;
				//rbbi.wID = 0;
				VERIFY(m_ctlMainTopReBar.InsertBand(UINT_MAX, &rbbi));
				toolbar->SaveCurHeight();
				toolbar->UpdateBackground();

				pwndToolbarX = &m_ctlMainTopReBar;
			}
		}
	}

	// set title
	SetWindowText(CString(MOD_RELEASE_PRODUCT_NAME) + _T(" ") + theApp.m_strCurVersionLong);

	// Init taskbar notifier
	m_wndTaskbarNotifier.CreateWnd(this);
	LoadNotifier(thePrefs.GetNotifierConfiguration());
	UpdateStartupProgress(14, IDS_STARTUP_PROGRESS_STARTING, IDS_STARTUP_PROGRESS_CREATING_UI);

	// set statusbar
	// the statusbar control is created as a custom control in the dialog resource,
	// this solves font and sizing problems when using large system fonts
	statusbar->SubclassWindow(GetDlgItem(IDC_STATUSBAR)->m_hWnd);
	statusbar->EnableToolTips(true);
	SetStatusBarPartsSize();
	ShowNetworkAddressState();

	// create main window dialog pages
	UpdateStartupProgress(20, IDS_STARTUP_PROGRESS_CREATING_UI, IDS_STARTUP_PROGRESS_CREATING_PAGES);
#if EMULEBB_HAS_STARTUP_PROFILING
	ullPhaseStart = theApp.GetStartupProfileTimestampUs();
#endif
	DialogCreateIndirect(serverwnd, IDD_SERVER);
#if EMULEBB_HAS_STARTUP_PROFILING
	theApp.AppendStartupProfileLine(_T("CemuleDlg::OnInitDialog create server window"), theApp.GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = theApp.GetStartupProfileTimestampUs();
#endif
	DialogCreateIndirect(sharedfileswnd, IDD_FILES);
#if EMULEBB_HAS_STARTUP_PROFILING
	theApp.AppendStartupProfileLine(_T("CemuleDlg::OnInitDialog create shared files window"), theApp.GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = theApp.GetStartupProfileTimestampUs();
#endif
	searchwnd->CreateWnd(this); // can not use 'DialogCreateIndirect' for the SearchWnd, grrr...
#if EMULEBB_HAS_STARTUP_PROFILING
	theApp.AppendStartupProfileLine(_T("CemuleDlg::OnInitDialog create search window"), theApp.GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = theApp.GetStartupProfileTimestampUs();
#endif
	DialogCreateIndirect(chatwnd, IDD_CHAT);
#if EMULEBB_HAS_STARTUP_PROFILING
	theApp.AppendStartupProfileLine(_T("CemuleDlg::OnInitDialog create chat window"), theApp.GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = theApp.GetStartupProfileTimestampUs();
#endif
	transferwnd->CreateWnd(this);
#if EMULEBB_HAS_STARTUP_PROFILING
	theApp.AppendStartupProfileLine(_T("CemuleDlg::OnInitDialog create transfer window"), theApp.GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = theApp.GetStartupProfileTimestampUs();
#endif
	DialogCreateIndirect(statisticswnd, IDD_STATISTICS);
#if EMULEBB_HAS_STARTUP_PROFILING
	theApp.AppendStartupProfileLine(_T("CemuleDlg::OnInitDialog create statistics window"), theApp.GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = theApp.GetStartupProfileTimestampUs();
#endif
	DialogCreateIndirect(kademliawnd, IDD_KADEMLIAWND);
#if EMULEBB_HAS_STARTUP_PROFILING
	theApp.AppendStartupProfileLine(_T("CemuleDlg::OnInitDialog create kad window"), theApp.GetStartupProfileElapsedUs(ullPhaseStart));
	ullPhaseStart = theApp.GetStartupProfileTimestampUs();
#endif
	DialogCreateIndirect(ircwnd, IDD_IRC);
#if EMULEBB_HAS_STARTUP_PROFILING
	theApp.AppendStartupProfileLine(_T("CemuleDlg::OnInitDialog create IRC window"), theApp.GetStartupProfileElapsedUs(ullPhaseStart));
	theApp.AppendStartupProfileLine(_T("CemuleDlg::OnInitDialog child page creation total"), theApp.GetStartupProfileElapsedUs(ullDialogInitStart));
#endif

	// with the top rebar control, some XP themes look better with additional lite borders, some not.
	//serverwnd->ModifyStyleEx(0, WS_EX_STATICEDGE);
	//sharedfileswnd->ModifyStyleEx(0, WS_EX_STATICEDGE);
	//searchwnd->ModifyStyleEx(0, WS_EX_STATICEDGE);
	//chatwnd->ModifyStyleEx(0, WS_EX_STATICEDGE);
	//transferwnd->ModifyStyleEx(0, WS_EX_STATICEDGE);
	//statisticswnd->ModifyStyleEx(0, WS_EX_STATICEDGE);
	//kademliawnd->ModifyStyleEx(0, WS_EX_STATICEDGE);
	//ircwnd->ModifyStyleEx(0, WS_EX_STATICEDGE);

	// optional: restore last used main window dialog
	if (thePrefs.GetRestoreLastMainWndDlg()) {
		CWnd *activate;
		switch (thePrefs.GetLastMainWndDlgID()) {
		case IDD_FILES:
			activate = sharedfileswnd;
			break;
		case IDD_SEARCH:
			activate = searchwnd;
			break;
		case IDD_CHAT:
			activate = chatwnd;
			break;
		case IDD_TRANSFER:
			activate = transferwnd;
			break;
		case IDD_STATISTICS:
			activate = statisticswnd;
			break;
		case IDD_KADEMLIAWND:
			activate = kademliawnd;
			break;
		case IDD_IRC:
			activate = ircwnd;
			break;
		//case IDD_SERVER:
		default:
			activate = serverwnd;
		}
		SetActiveDialog(activate);
	}
	// if still no active window, activate server window
	if (activewnd == NULL)
		SetActiveDialog(serverwnd);

	SetAllIcons();
	Localize();
	UpdateStartupProgress(38, IDS_STARTUP_PROGRESS_CREATING_UI, IDS_STARTUP_PROGRESS_RESTORING_UI);

	// set update interval of graphic rate display (in seconds)
	//ShowConnectionState(false);

	// adjust all main window sizes for toolbar height and maximize the child windows
	CRect rcClient, rcToolbar, rcStatusbar;
	GetClientRect(&rcClient);
	pwndToolbarX->GetWindowRect(&rcToolbar);
	statusbar->GetWindowRect(&rcStatusbar);
	rcClient.top += rcToolbar.Height();
	rcClient.bottom -= rcStatusbar.Height();

	CWnd *const apWnds[] =
	{
		serverwnd,
		kademliawnd,
		transferwnd,
		sharedfileswnd,
		searchwnd,
		chatwnd,
		ircwnd,
		statisticswnd
	};
	for (unsigned i = 0; i < _countof(apWnds); ++i) {
		apWnds[i]->SetWindowPos(NULL, rcClient.left, rcClient.top, rcClient.Width(), rcClient.Height(), SWP_NOZORDER);
		AddAnchor(*apWnds[i], TOP_LEFT, BOTTOM_RIGHT);
	}

	// anchor bars
	AddAnchor(*pwndToolbarX, TOP_LEFT, TOP_RIGHT);
	AddAnchor(*statusbar, BOTTOM_LEFT, BOTTOM_RIGHT);

	statisticswnd->ShowInterval();

	// tray icon
	TraySetMinimizeToTray(thePrefs.GetMinTrayPTR());
	TrayMinimizeToTrayChange();

	ShowTransferRate(true);
	UpdateTrayVisibility();
	searchwnd->UpdateCatTabs();
	StartTransferRateDisplayTimer();

	///////////////////////////////////////////////////////////////////////////
	// Restore saved window placement
	//
	WINDOWPLACEMENT wp;
	wp.length = (UINT)sizeof wp;
	wp = thePrefs.GetEmuleWindowPlacement();
	if (m_bStartMinimized) {
		// To avoid the window flickering during startup we try to set the proper window show state right here.
		if (*thePrefs.GetMinTrayPTR()) {
			// Minimize to System Tray
			//
			// Unfortunately this does not work. The eMule main window is a modal dialog which is invoked
			// by CDialog::DoModal which eventually calls CWnd::RunModalLoop. Look at 'MLF_SHOWONIDLE' and
			// 'bShowIdle' in the above noted functions to see why it's not possible to create the window
			// right in hidden state.

			//--- attempt #1
			//wp.showCmd = SW_HIDE;
			//TrayShow();
			//--- doesn't work at all

			//--- attempt #2
			//if (wp.showCmd == SW_SHOWMAXIMIZED)
			//	wp.flags = WPF_RESTORETOMAXIMIZED;
			//m_bStartMinimizedChecked = false; // post-hide the window
			//--- creates window flickering

			//--- attempt #3
			// Minimize the window into the task bar and later move it into the tray
			if (wp.showCmd == SW_SHOWMAXIMIZED)
				wp.flags = WPF_RESTORETOMAXIMIZED;
			wp.showCmd = SW_MINIMIZE;
			m_bStartMinimizedChecked = false;

			// to get properly restored from tray bar (after attempt #3) we have to use a patched 'restore' window cmd
			m_wpFirstRestore = thePrefs.GetEmuleWindowPlacement();
			m_wpFirstRestore.length = (UINT)sizeof m_wpFirstRestore;
			if (m_wpFirstRestore.showCmd != SW_SHOWMAXIMIZED)
				m_wpFirstRestore.showCmd = SW_SHOWNORMAL;
		} else {
			// Minimize to System Taskbar
			if (wp.showCmd == SW_SHOWMAXIMIZED)
				wp.flags = WPF_RESTORETOMAXIMIZED;
			wp.showCmd = SW_MINIMIZE; // Minimize window but do not activate it.
			m_bStartMinimizedChecked = true;
		}
	} else {
		// Allow only SW_SHOWNORMAL and SW_SHOWMAXIMIZED. Ignore SW_SHOWMINIMIZED to make sure
		// the window becomes visible.
		// If user wants SW_SHOWMINIMIZED, we already have an explicit option for this (see above).
		if (wp.showCmd != SW_SHOWMAXIMIZED)
			wp.showCmd = SW_SHOWNORMAL;
		m_bStartMinimizedChecked = true;
	}
	SetWindowPlacement(&wp);

	(void)FakeFileDetector::ReloadRules();

	if (thePrefs.GetWSIsEnabled())
		theApp.webserver->StartServer();
	UpdateStartupProgress(45, IDS_STARTUP_PROGRESS_STARTING_SERVICES, IDS_STARTUP_PROGRESS_QUEUING_STAGES);

	VERIFY(PostMessage(UM_STARTUP_NEXT_STAGE) != 0);
	if (thePrefs.GetVerbose())
		AddDebugLogLine(false, _T("Queued startup stage sequencer message."));

	// Start UPnP port forwarding
	if (thePrefs.IsUPnPEnabled() && !theApp.IsStartupBindBlocked() && !thePrefs.IsVpnGuardEnabled())
		StartUPnP();

	if (thePrefs.IsFirstStart() && !thePrefs.IsFirstTimeWizardDisabled()) {
		// temporary disable the 'startup minimized' option, otherwise no window will be shown at all
		m_bStartMinimized = false;
		DestroyStartupProgress();
		FirstTimeWizard();
		ShowStartupProgress();
		UpdateStartupProgress(45, IDS_STARTUP_PROGRESS_STARTING_SERVICES, IDS_STARTUP_PROGRESS_QUEUING_STAGES);
	}

	VERIFY(m_pDropTarget->Register(this));
#if EMULEBB_HAS_STARTUP_PROFILING
	theApp.AppendStartupProfileLine(_T("CemuleDlg::OnInitDialog complete"), theApp.GetStartupProfileElapsedUs(ullDialogInitStart));
#endif

	StartAICHSyncThread();

	// debug info
	DebugLog(_T("Using '%s' as config directory"), (LPCTSTR)thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	LogStartupNetworkGuardSummary();

	if (!thePrefs.HasCustomTaskIconColor())
		SetTaskbarIconColor();

	return TRUE;
}

void CemuleDlg::DoVersioncheck(bool manual)
{
	if (!manual && thePrefs.GetLastVC() != 0) {
		CTime last(thePrefs.GetLastVC());
		struct tm tmTemp;
		time_t tLast = safe_mktime(last.GetLocalTm(&tmTemp));
		time_t tNow = safe_mktime(CTime::GetCurrentTime().GetLocalTm(&tmTemp));
		if (difftime(tNow, tLast) / DAY2S(1) < thePrefs.GetUpdateDays())
			return;
	}

	if (!m_pVersionCheckState || !VersionCheckLaunchSeams::TryMarkQueued(*m_pVersionCheckState))
		return;

	const HWND hNotifyWnd = m_hWnd;
	std::unique_ptr<SVersionCheckContext> pContext(new SVersionCheckContext);
	pContext->hNotifyWnd = hNotifyWnd;
	pContext->pQueuedState = m_pVersionCheckState;
	pContext->bManual = manual;

	CWinThread *pThread = AfxBeginThread(VersionCheckThreadProc, pContext.get(), THREAD_PRIORITY_BELOW_NORMAL, 0, 0, NULL);
	if (pThread == NULL) {
		VersionCheckLaunchSeams::ClearQueued(*m_pVersionCheckState);
		if (manual)
			AddLogLine(true, GetResString(IDS_NEWVERSIONFAILED));
		else
			AddDebugLogLine(false, _T("Version check: failed to start background update thread."));
		return;
	}

	// AfxBeginThread starts function workers immediately with MFC auto-delete enabled.
	// Do not mutate pThread after launch; a fast worker may already have exited.
	thePrefs.UpdateLastVC();
	(void)pContext.release();
}

LRESULT CemuleDlg::OnVersionCheckResponse(WPARAM, LPARAM lParam)
{
	std::unique_ptr<SVersionCheckResult> pResult(reinterpret_cast<SVersionCheckResult*>(lParam));
	VersionCheckLaunchSeams::ClearQueuedOnOwnerTeardown(m_pVersionCheckState);
	if (pResult.get() == NULL || theApp.IsClosing())
		return 0;

	switch (pResult->result.eStatus) {
	case ReleaseUpdateCheck::EUpdateCheckStatus::NewerVersionAvailable:
	{
		CString strReleaseUrl(pResult->result.strReleaseUrl);
		if (strReleaseUrl.IsEmpty())
			strReleaseUrl = thePrefs.GetVersionCheckURL();

		CString strLog(GetResString(IDS_NEWVERSIONAVL));
		if (!pResult->result.strLatestVersion.IsEmpty())
			strLog.AppendFormat(_T(" (%s)"), (LPCTSTR)pResult->result.strLatestVersion);
		Log(LOG_SUCCESS | LOG_STATUSBAR, _T("%s"), (LPCTSTR)strLog);
		ShowNotifier(GetResString(IDS_NEWVERSIONAVLPOPUP), TBN_NEWVERSION, strReleaseUrl);
		if (!thePrefs.GetNotifierOnNewVersion() && AfxMessageBox(GetResString(IDS_NEWVERSIONAVL) + GetResString(IDS_VISITVERSIONCHECK), MB_YESNO) == IDYES)
			BrowserOpen(strReleaseUrl, thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
		break;
	}
	case ReleaseUpdateCheck::EUpdateCheckStatus::NoNewerVersion:
		if (pResult->bManual)
			AddLogLine(true, GetResString(IDS_NONEWERVERSION));
		break;
	case ReleaseUpdateCheck::EUpdateCheckStatus::Failed:
	default:
		if (pResult->bManual) {
			AddLogLine(true, GetResString(IDS_NEWVERSIONFAILED));
		} else if (!pResult->result.strError.IsEmpty())
			AddDebugLogLine(false, _T("Version check failed: %s"), (LPCTSTR)pResult->result.strError);
		else
			AddDebugLogLine(false, _T("Version check failed."));
		break;
	}
	return 0;
}

void CemuleDlg::OnStartupTimer() noexcept
{
	// NOTE: Always handle all type of MFC exceptions in TimerProcs - otherwise we'll get mem leaks
	try {
#if EMULEBB_HAS_STARTUP_PROFILING
		const ULONGLONG ullPhaseStart = theApp.GetStartupProfileTimestampUs();
		CString strPhase;
		strPhase.Format(_T("StartupTimer enter status=%u"), static_cast<unsigned>(status));
		theApp.AppendStartupProfileLine(strPhase, 0);
#endif
		switch (status) {
		case 0:
			UpdateStartupProgress(50, IDS_STARTUP_PROGRESS_LOADING_FILES, IDS_STARTUP_PROGRESS_ATTACHING_SHARED_FILES);
			status = 2;
#if EMULEBB_HAS_STARTUP_PROFILING
			theApp.sharedfiles->SetOutputCtrl(&sharedfileswnd->sharedfilesctrl);
			theApp.AppendStartupProfileLine(_T("StartupTimer stage 0: attach shared-files output control"), theApp.GetStartupProfileElapsedUs(ullPhaseStart));
#else
			theApp.sharedfiles->SetOutputCtrl(&sharedfileswnd->sharedfilesctrl);
#endif
			break;
		case 2:
			UpdateStartupProgress(62, IDS_STARTUP_PROGRESS_LOADING_SERVERS, IDS_STARTUP_PROGRESS_LOADING_SERVER_LIST);
			status = 4;
			{
#if EMULEBB_HAS_STARTUP_PROFILING
				const ULONGLONG ullServerInitStart = theApp.GetStartupProfileTimestampUs();
#endif
			try {
				theApp.serverlist->Init();
			} catch (CException *ex) {
				LogError(LOG_STATUSBAR, _T("Failed to initialize server list%s"), (LPCTSTR)CExceptionStrDash(*ex));
				ex->Delete();
			} catch (...) {
				ASSERT(0);
				LogError(LOG_STATUSBAR, _T("Failed to initialize server list - Unknown exception"));
			}
#if EMULEBB_HAS_STARTUP_PROFILING
				theApp.AppendStartupProfileLine(_T("StartupTimer stage 2: serverlist->Init"), theApp.GetStartupProfileElapsedUs(ullServerInitStart));
#endif
			}
			break;
		case 4:
			{
				UpdateStartupProgress(74, IDS_STARTUP_PROGRESS_STARTING_NETWORK, IDS_STARTUP_PROGRESS_LOADING_DOWNLOADS);
				if (VpnGuardPolicySeams::ShouldRunStartupProbe(thePrefs.GetVpnGuardMode(), theApp.IsStartupBindBlocked(), m_bVpnGuardStartupApproved)) {
					if (m_bVpnGuardStartupProbePending || StartVpnGuardProbe(_T("startup"), false))
						return;
				}
				status = 5;
				bool bError = false;
#if EMULEBB_HAS_STARTUP_PROFILING
				const ULONGLONG ullDownloadInitStart = theApp.GetStartupProfileTimestampUs();
#endif

				// NOTE: If we have an unhandled exception in CDownloadQueue::Init, MFC will silently catch it
				// and the creation of the TCP and the UDP socket will not be done -> client will get a LowID!
				try {
					theApp.downloadqueue->Init();
				} catch (CException *ex) {
					LogError(LOG_STATUSBAR, _T("Failed to initialize download queue%s"), (LPCTSTR)CExceptionStrDash(*ex));
					ex->Delete();
					bError = true;
				} catch (...) {
					ASSERT(0);
					LogError(LOG_STATUSBAR, _T("Failed to initialize download queue - Unknown exception"));
					bError = true;
				}
#if EMULEBB_HAS_STARTUP_PROFILING
				theApp.AppendStartupProfileLine(_T("StartupTimer stage 4: downloadqueue->Init"), theApp.GetStartupProfileElapsedUs(ullDownloadInitStart));
				const ULONGLONG ullSocketInitStart = theApp.GetStartupProfileTimestampUs();
#endif
				if (theApp.IsStartupBindBlocked()) {
					LogError(LOG_STATUSBAR, _T("%s"), (LPCTSTR)theApp.GetStartupBindBlockReason());
				} else {
					if (!theApp.listensocket->StartListening()) {
						CString strError;
						strError.Format(GetResString(IDS_MAIN_SOCKETERROR), thePrefs.GetPort());
						LogError(LOG_STATUSBAR, _T("%s"), (LPCTSTR)strError);
						if (thePrefs.GetNotifierOnImportantError())
							theApp.emuledlg->ShowNotifier(strError, TBN_IMPORTANTEVENT);
						bError = true;
					}
					if (!theApp.clientudp->Create()) {
						CString strError;
						strError.Format(GetResString(IDS_MAIN_SOCKETERROR), thePrefs.GetUDPPort());
						LogError(LOG_STATUSBAR, _T("%s"), (LPCTSTR)strError);
						if (thePrefs.GetNotifierOnImportantError())
							theApp.emuledlg->ShowNotifier(strError, TBN_IMPORTANTEVENT);
					}
					if (!thePrefs.IsVpnGuardEnabled())
						PublicIpProbe::StartBoundPublicIpv4Probe();
					else if (thePrefs.IsUPnPEnabled())
						StartUPnP();
				}
#if EMULEBB_HAS_STARTUP_PROFILING
				theApp.AppendStartupProfileLine(_T("StartupTimer stage 4: socket startup"), theApp.GetStartupProfileElapsedUs(ullSocketInitStart));
#endif

				if (!bError) // show the success msg, only if we had no serious error
					AddLogLine(true, GetResString(IDS_MAIN_READY), (LPCTSTR)theApp.m_strCurVersionLong);

				theApp.m_app_state = APP_STATE_RUNNING; //initialization completed
				CloseStartupProgressIfRunning();
				UpdateBindLossMonitor(false);
				const bool bStartupConnectionCommandsEnabled = VpnGuardPolicySeams::CanUseStartupConnectionCommands(
					thePrefs.GetVpnGuardMode(), theApp.IsStartupBindBlocked(), m_bBindLossMonitorActive);
				toolbar->EnableButton(TBBTN_CONNECT, bStartupConnectionCommandsEnabled);
				m_SysMenuOptions.EnableMenuItem(MP_CONNECT, bStartupConnectionCommandsEnabled ? MF_ENABLED : MF_GRAYED);
				serverwnd->GetDlgItem(IDC_ED2KCONNECT)->EnableWindow(bStartupConnectionCommandsEnabled);
				kademliawnd->UpdateControlsState(); //application state change is not tracked - force update

				if (VpnGuardPolicySeams::CanPostStartupAutoConnect(thePrefs.DoAutoConnect()
					, thePrefs.GetVpnGuardMode(), theApp.IsStartupBindBlocked(), m_bBindLossMonitorActive))
					PostMessage(WM_COMMAND, MP_CONNECT, 0);

#ifdef HAVE_WIN7_SDK_H
				UpdateStatusBarProgress();
#endif
#if EMULEBB_HAS_STARTUP_PROFILING
				theApp.AppendStartupProfileLine(_T("StartupTimer stage 4: finalize running state"), theApp.GetStartupProfileElapsedUs(ullPhaseStart));
#endif
			}
			break;
		case 5:
			status = 6;
			// WHY: Stored-search restore is optional post-running UI hydration. Keep the
			// startup lifecycle dialog from surviving while a large restore populates tabs.
			CloseStartupProgressIfRunning();
			{
#if EMULEBB_HAS_STARTUP_PROFILING
				const ULONGLONG ullStoredSearchesStart = theApp.GetStartupProfileTimestampUs();
#endif
			try {
				if (thePrefs.IsStoringSearchesEnabled())
					theApp.searchlist->LoadSearches();
			} catch (CException *ex) {
				LogError(LOG_STATUSBAR, _T("Failed to restore stored searches%s"), (LPCTSTR)CExceptionStrDash(*ex));
				ex->Delete();
			} catch (...) {
				ASSERT(0);
				LogError(LOG_STATUSBAR, _T("Failed to restore stored searches - Unknown exception"));
			}
#if EMULEBB_HAS_STARTUP_PROFILING
				theApp.AppendStartupProfileLine(_T("StartupTimer stage 5: stored searches"), theApp.GetStartupProfileElapsedUs(ullStoredSearchesStart));
#endif
			}
			// WHY: Stored searches are the final blocking startup load. Finish the sequence
			// here so a stale progress dialog cannot survive behind a missed queued hop.
			[[fallthrough]];
		default:
			CloseStartupProgressIfRunning();
			serverwnd->serverlistctrl.Visible();
			theApp.sharedfiles->StartDeferredHashing();
			theApp.MarkStartupComplete();
#if EMULEBB_HAS_STARTUP_PROFILING
			theApp.AppendStartupProfileLine(_T("StartupTimer finalize: start deferred shared hashing"), 0);
			theApp.AppendStartupProfileLine(_T("StartupTimer complete"), theApp.GetStartupProfileElapsedUs(ullPhaseStart));
			if (sharedfileswnd != NULL)
				sharedfileswnd->OnStartupProfileStartupComplete();
#endif
			StopTimer();
			DestroyStartupProgress();
			return;
		}
		VERIFY(PostMessage(UM_STARTUP_NEXT_STAGE) != 0);
	}
	catch (CException *e) {
#if EMULEBB_HAS_STARTUP_PROFILING
		TCHAR szError[1024];
		GetExceptionMessage(*e, szError, _countof(szError));
		CString strPhase;
		strPhase.Format(_T("StartupTimer CException status=%u (%s)"), static_cast<unsigned>(status), szError);
		theApp.AppendStartupProfileLine(strPhase, 0);
#endif
		if (thePrefs.GetVerbose())
			DebugLogError(LOG_STATUSBAR, _T("Unknown CException in CemuleDlg::OnStartupTimer"));
		e->Delete();
	}
	catch (const CString &sError) {
#if EMULEBB_HAS_STARTUP_PROFILING
		CString strPhase;
		strPhase.Format(_T("StartupTimer CStringException status=%u (%s)"), static_cast<unsigned>(status), (LPCTSTR)sError);
		theApp.AppendStartupProfileLine(strPhase, 0);
#endif
		if (thePrefs.GetVerbose())
			DebugLogError(LOG_STATUSBAR, _T("Unknown CString exception in CemuleDlg::OnStartupTimer - %s"), (LPCTSTR)sError);
	}
}

void CemuleDlg::StopTimer()
{
	if (thePrefs.UpdateNotify())
		DoVersioncheck(false);

	if (theApp.geolocation != NULL)
		theApp.geolocation->QueueBackgroundRefresh();
	if (theApp.ipfilterUpdater != NULL)
		theApp.ipfilterUpdater->QueueBackgroundRefresh();

	theApp.StartSharedDirectoryMonitor();

	if (!theApp.m_strPendingLink.IsEmpty()) {
		OnWMData(NULL, (LPARAM)&theApp.sendstruct);
		theApp.m_strPendingLink.Empty();
	}

	UpdateBindLossMonitor(false);
}

bool CemuleDlg::IsBindLossMonitorConfigured() const
{
	return theApp.IsRunning()
		&& thePrefs.IsVpnGuardEnabled()
		&& !theApp.IsStartupBindBlocked()
		&& !thePrefs.GetActiveBindInterface().IsEmpty()
		&& thePrefs.GetActiveBindAddressResolveResult() == BARR_Resolved
		&& thePrefs.GetBindAddr() != NULL
		&& *thePrefs.GetBindAddr() != _T('\0');
}

bool CemuleDlg::CanUseP2PConnectionCommands() const
{
	return VpnGuardPolicySeams::CanUseP2PConnectionCommands(
		thePrefs.GetVpnGuardMode(), theApp.IsStartupBindBlocked(), m_bBindLossMonitorActive);
}

void CemuleDlg::LogP2PConnectionCommandBlocked() const
{
	if (theApp.IsStartupBindBlocked()) {
		LogWarning(LOG_STATUSBAR, _T("%s"), (LPCTSTR)theApp.GetStartupBindBlockReason());
		return;
	}
	if (VpnGuardPolicySeams::IsRuntimeMonitorRequired(thePrefs.GetVpnGuardMode(), false) && !m_bBindLossMonitorActive)
		LogWarning(LOG_STATUSBAR, _T("%s"), (LPCTSTR)GetResString(IDS_VPN_GUARD_COMMANDS_BLOCKED_MONITOR));
}

void CemuleDlg::UpdateBindLossMonitor(bool bForceVpnGuardHttpProbe)
{
	StopBindLossMonitor();
	if (!IsBindLossMonitorConfigured())
		return;

	const DWORD dwInterfaceNotify = NotifyIpInterfaceChange(AF_INET, BindLossIpInterfaceChangeCallback, m_hWnd, FALSE, &m_hBindLossInterfaceNotification);
	if (dwInterfaceNotify != NO_ERROR) {
		m_hBindLossInterfaceNotification = NULL;
		DebugLogWarning(_T("Bind-loss protection: NotifyIpInterfaceChange registration failed with error %lu"), dwInterfaceNotify);
	}

	const DWORD dwAddressNotify = NotifyUnicastIpAddressChange(AF_INET, BindLossUnicastAddressChangeCallback, m_hWnd, FALSE, &m_hBindLossAddressNotification);
	if (dwAddressNotify != NO_ERROR) {
		m_hBindLossAddressNotification = NULL;
		DebugLogWarning(_T("Bind-loss protection: NotifyUnicastIpAddressChange registration failed with error %lu"), dwAddressNotify);
	}

	m_uBindLossWatchdogTimer = SetTimer(kBindLossWatchdogTimerId, kBindLossWatchdogIntervalMs, NULL);
	if (m_uBindLossWatchdogTimer == 0)
		DebugLogWarning(_T("Bind-loss protection: failed to start the watchdog timer."));

	m_bBindLossMonitorActive = m_uBindLossWatchdogTimer != 0
		|| m_hBindLossInterfaceNotification != NULL
		|| m_hBindLossAddressNotification != NULL;
	CheckBindLossMonitor();
	CheckVpnGuardHttpMonitor(bForceVpnGuardHttpProbe);
}

void CemuleDlg::StopBindLossMonitor()
{
	if (m_uBindLossWatchdogTimer != 0) {
		VERIFY(KillTimer(m_uBindLossWatchdogTimer));
		m_uBindLossWatchdogTimer = 0;
	}
	if (m_hBindLossInterfaceNotification != NULL) {
		(void)CancelMibChangeNotify2(m_hBindLossInterfaceNotification);
		m_hBindLossInterfaceNotification = NULL;
	}
	if (m_hBindLossAddressNotification != NULL) {
		(void)CancelMibChangeNotify2(m_hBindLossAddressNotification);
		m_hBindLossAddressNotification = NULL;
	}
	m_bBindLossMonitorActive = false;
}

void CemuleDlg::StartTransferRateDisplayTimer()
{
	StopTransferRateDisplayTimer();
	m_uTransferRateDisplayTimer = SetTimer(kTransferRateDisplayTimerId, GetDesktopPresentationTimerDelayMs(thePrefs.GetDesktopUiRefreshIntervalMs()), NULL);
	if (thePrefs.GetVerbose() && m_uTransferRateDisplayTimer == 0)
		AddDebugLogLine(true, _T("Failed to create desktop presentation timer - %s"), (LPCTSTR)GetErrorMessage(::GetLastError()));
}

void CemuleDlg::StopTransferRateDisplayTimer()
{
	if (m_uTransferRateDisplayTimer != 0) {
		VERIFY(KillTimer(m_uTransferRateDisplayTimer));
		m_uTransferRateDisplayTimer = 0;
	}
}

void CemuleDlg::CheckBindLossMonitor()
{
	if (m_bBindLossShutdown || !m_bBindLossMonitorActive || !IsBindLossMonitorConfigured())
		return;

	CString strResolvedAddress;
	CString strResolvedInterfaceName;
	const EBindAddressResolveResult eResult = CBindAddressResolver::ResolveBindAddress(thePrefs.GetActiveBindInterface()
		, thePrefs.GetActiveConfiguredBindAddr(), strResolvedAddress, &strResolvedInterfaceName);

	CString strActiveBindAddress;
	if (thePrefs.GetBindAddr() != NULL)
		strActiveBindAddress = thePrefs.GetBindAddr();

	if (!BindRuntimeLossPolicy::ShouldExitForRuntimeBindLoss(true, eResult, strResolvedAddress, strActiveBindAddress))
		return;

	const CString strReason = BindRuntimeLossPolicy::FormatRuntimeBindLossReason(strResolvedInterfaceName
		, thePrefs.GetActiveBindInterfaceName()
		, thePrefs.GetActiveBindInterface()
		, thePrefs.GetActiveConfiguredBindAddr()
		, eResult
		, strResolvedAddress
		, strActiveBindAddress
		, GetBindRuntimeLossPolicyText());
	ExitForVpnGuardFailure(strReason);
}

void CemuleDlg::CheckVpnGuardHttpMonitor(bool bForce)
{
	if (m_bBindLossShutdown
		|| m_bVpnGuardRuntimeProbePending
		|| thePrefs.GetVpnGuardMode() != VpnGuardSeams::EMode::Block
		|| theApp.IsStartupBindBlocked()
		|| !m_bVpnGuardStartupApproved)
		return;

	const ULONGLONG ullNow = ::GetTickCount64();
	if (!bForce && m_ullLastVpnGuardRuntimeProbeTick != 0 && ullNow - m_ullLastVpnGuardRuntimeProbeTick < kVpnGuardHttpIntervalMs)
		return;
	m_ullLastVpnGuardRuntimeProbeTick = ullNow;
	(void)StartVpnGuardProbe(_T("runtime"), true);
}

bool CemuleDlg::StartVpnGuardProbe(const CString& strPurpose, bool bRuntime)
{
	if (thePrefs.GetVpnGuardMode() != VpnGuardSeams::EMode::Block || theApp.IsStartupBindBlocked())
		return false;

	std::vector<VpnGuardSeams::SAllowedPublicIpv4Range> ranges;
	CString strError;
	if (!TryLoadVpnGuardAllowedRanges(ranges, strError)) {
		const CString strReason = FormatVpnGuardPublicIpFailure(bRuntime, PublicIpProbe::SBoundPublicIpv4ProbeResult(), strError);
		if (bRuntime)
			ExitForVpnGuardFailure(strReason);
		else {
			theApp.BlockStartupNetworkingForSession(strReason);
			LogError(LOG_STATUSBAR, _T("%s"), (LPCTSTR)strReason);
		}
		return false;
	}
	if (ranges.empty()) {
		LPCTSTR pszLocalBind = thePrefs.GetBindAddr() != NULL ? thePrefs.GetBindAddr() : _T("default");
		Log(_T("VPN Guard %s public IP check skipped: no allowed CIDRs configured; bind-interface guard remains active for interface=%s localBind=%s"),
			(LPCTSTR)strPurpose,
			(LPCTSTR)thePrefs.GetActiveBindInterfaceName(),
			pszLocalBind);
		if (!bRuntime)
			m_bVpnGuardStartupApproved = true;
		return false;
	}

	const uint32 uGeneration = ++m_uVpnGuardProbeGeneration;
	if (!PublicIpProbe::StartBoundPublicIpv4Probe(m_hWnd, UM_VPN_GUARD_PROBE_RESULT, uGeneration, strPurpose, strError)) {
		const CString strReason = FormatVpnGuardPublicIpFailure(bRuntime, PublicIpProbe::SBoundPublicIpv4ProbeResult(), strError);
		if (bRuntime)
			ExitForVpnGuardFailure(strReason);
		else {
			theApp.BlockStartupNetworkingForSession(strReason);
			LogError(LOG_STATUSBAR, _T("%s"), (LPCTSTR)strReason);
		}
		return false;
	}

	m_bVpnGuardStartupProbePending = !bRuntime;
	m_bVpnGuardRuntimeProbePending = bRuntime;
	LPCTSTR pszLocalBind = thePrefs.GetBindAddr() != NULL ? thePrefs.GetBindAddr() : _T("default");
	Log(_T("VPN Guard %s public IP check pending: allowedCIDRs=%s bindInterface=%s localBind=%s"),
		(LPCTSTR)strPurpose,
		(LPCTSTR)thePrefs.GetVpnGuardAllowedPublicIpCidrs(),
		(LPCTSTR)thePrefs.GetActiveBindInterfaceName(),
		pszLocalBind);
	return true;
}

void CemuleDlg::ExitForVpnGuardFailure(const CString &strReason)
{
	ExitForBindLoss(strReason);
}

void CemuleDlg::ExitForBindLoss(const CString &strReason)
{
	if (m_bBindLossShutdown)
		return;

	m_bBindLossShutdown = true;
	StopBindLossMonitor();
	m_bVpnGuardStartupProbePending = false;
	m_bVpnGuardRuntimeProbePending = false;
	LogError(LOG_STATUSBAR, _T("%s"), (LPCTSTR)strReason);
	PostMessage(WM_CLOSE);
}

LRESULT CemuleDlg::OnStartupNextStage(WPARAM, LPARAM)
{
	OnStartupTimer();
	CloseStartupProgressIfRunning();
	return 0;
}

LRESULT CemuleDlg::OnBindInterfaceChanged(WPARAM, LPARAM)
{
	CheckBindLossMonitor();
	CheckVpnGuardHttpMonitor(true);
	return 0;
}

LRESULT CemuleDlg::OnVpnGuardProbeResult(WPARAM wParam, LPARAM lParam)
{
	std::unique_ptr<PublicIpProbe::SBoundPublicIpv4ProbeResult> pResult(reinterpret_cast<PublicIpProbe::SBoundPublicIpv4ProbeResult*>(lParam));
	if (m_bBindLossShutdown || pResult.get() == NULL)
		return 0;
	if (static_cast<uint32>(wParam) != m_uVpnGuardProbeGeneration || pResult->uGeneration != m_uVpnGuardProbeGeneration)
		return 0;

	const bool bRuntime = m_bVpnGuardRuntimeProbePending;
	const bool bStartup = m_bVpnGuardStartupProbePending;
	m_bVpnGuardStartupProbePending = false;
	m_bVpnGuardRuntimeProbePending = false;

	std::vector<VpnGuardSeams::SAllowedPublicIpv4Range> ranges;
	CString strError;
	const bool bRangesLoaded = TryLoadVpnGuardAllowedRanges(ranges, strError);
	const bool bPublicIpCheckRequired = bRangesLoaded && !ranges.empty();
	const bool bPublicIpAllowed = bPublicIpCheckRequired
		&& VpnGuardSeams::IsPublicIpv4Allowed(pResult->uPublicAddress, ranges);
	const bool bAllowed = bRangesLoaded
		&& VpnGuardPolicySeams::IsProbeResultAllowed(bPublicIpCheckRequired, pResult->bSucceeded, bPublicIpAllowed);
	if (bAllowed) {
		Log(_T("VPN Guard %s public IP check passed: provider=%s publicIp=%S allowedCIDRs=%s"),
			(LPCTSTR)pResult->strPurpose,
			(LPCTSTR)pResult->strProviderUrl,
			pResult->strPublicAddress.GetString(),
			(LPCTSTR)thePrefs.GetVpnGuardAllowedPublicIpCidrs());
		if (bStartup) {
			m_bVpnGuardStartupApproved = true;
			m_ullLastVpnGuardRuntimeProbeTick = ::GetTickCount64();
			VERIFY(PostMessage(UM_STARTUP_NEXT_STAGE) != 0);
		}
		return 0;
	}

	const CString strReason = FormatVpnGuardPublicIpFailure(bRuntime, *pResult, strError);
	if (bRuntime)
		ExitForVpnGuardFailure(strReason);
	else {
		theApp.BlockStartupNetworkingForSession(strReason);
		LogError(LOG_STATUSBAR, _T("%s"), (LPCTSTR)strReason);
		if (bStartup)
			VERIFY(PostMessage(UM_STARTUP_NEXT_STAGE) != 0);
	}
	return 0;
}

LRESULT CemuleDlg::OnGeoLocationUpdated(WPARAM wParam, LPARAM)
{
	if (theApp.geolocation != NULL)
		theApp.geolocation->HandleBackgroundRefreshResult(wParam != 0);
	return 0;
}

LRESULT CemuleDlg::OnIPFilterUpdated(WPARAM wParam, LPARAM)
{
	if (theApp.ipfilterUpdater != NULL)
		theApp.ipfilterUpdater->HandleBackgroundRefreshResult(wParam != 0);
	return 0;
}

void CemuleDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	const AppKeyboardShortcutsSeams::ECommand eShortcutCommand =
		AppKeyboardShortcutsSeams::ClassifySystemKeyMenu(nID, lParam, IsKeyboardShortcutModalContext(*this));
	if (eShortcutCommand == AppKeyboardShortcutsSeams::ECommand::ExitApp) {
		OnClose();
		return;
	}
	if (eShortcutCommand == AppKeyboardShortcutsSeams::ECommand::ShowHotMenu) {
		OnBnClickedHotmenu();
		return;
	}
	const UINT uCommandId = GetMainShellShortcutCommandId(eShortcutCommand);
	if (uCommandId != 0) {
		OnCommand(static_cast<WPARAM>(uCommandId), 0);
		return;
	}

	// System menu - Speed selector
	if (nID >= MP_QS_U10 && nID <= MP_QS_UP10) {
		QuickSpeedUpload(nID);
		return;
	}
	if (nID >= MP_QS_D10 && nID <= MP_QS_DC) {
		QuickSpeedDownload(nID);
		return;
	}
	if (nID >= MP_QS_B10 && nID <= MP_QS_B90) {
		QuickSpeedBoth(nID);
		return;
	}
	if (nID == MP_QS_PA || nID == MP_QS_UA) {
		QuickSpeedOther(nID);
		return;
	}
	if ((nID & 0xFFF0) == SC_MINIMIZE) {
		MinimizeWindow();
		ShowTransferRate(true);
		transferwnd->UpdateCatTabTitles();
		return;
	}
	if ((nID & 0xFFF0) == MP_MINIMIZETOTRAY) {
		MinimizeWindow(true);
		ShowTransferRate(true);
		transferwnd->UpdateCatTabTitles();
		return;
	}

	switch (nID) {
	case MP_ABOUTBOX:
		{
			CCreditsDlg dlgAbout;
			m_bTransientDialogActive = true;
			dlgAbout.DoModal();
			m_bTransientDialogActive = m_pStartupProgressDlg != NULL;
			break;
		}
	case MP_VERSIONCHECK:
		DoVersioncheck(true);
		break;
	case MP_CONNECT:
		StartConnection();
		break;
	case MP_DISCONNECT:
		CloseConnection();
		break;
	default:
		CTrayDialog::OnSysCommand(nID, lParam);
	}

	switch (nID & 0xFFF0) {
	case SC_MINIMIZE:
	case MP_MINIMIZETOTRAY:
	case SC_RESTORE:
	case SC_MAXIMIZE:
		ShowTransferRate(true);
		transferwnd->UpdateCatTabTitles();
	}
}

void CemuleDlg::PostStartupMinimized()
{
	if (!m_bStartMinimizedChecked) {
		//TODO: Use full initialized 'WINDOWPLACEMENT' and remove the 'OnCancel' call...
		// Isn't that easy. Read comments in OnInitDialog.
		m_bStartMinimizedChecked = true;
		if (m_bStartMinimized) {
			if (theApp.DidWeAutoStart() && !thePrefs.mintotray) {
				thePrefs.mintotray = true;
				MinimizeWindow();
				thePrefs.mintotray = false;
			} else
				MinimizeWindow();
		}
	}
}

void CemuleDlg::OnPaint()
{
	if (IsIconic()) {
		CPaintDC dc(this);

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		int cxIcon = ::GetSystemMetrics(SM_CXICON);
		int cyIcon = ::GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		dc.DrawIcon(x, y, GetMainConnectionStateIcon(m_eMainConnectionIcon));
	} else
		CTrayDialog::OnPaint();
}

HCURSOR CemuleDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(GetMainConnectionStateIcon(m_eMainConnectionIcon));
}

void CemuleDlg::OnBnClickedConnect()
{
	if (!theApp.IsConnected() && !theApp.serverconnect->IsConnecting() && !Kademlia::CKademlia::IsRunning())
		//connect if not currently connected or connecting
		StartConnection();
	else
		CloseConnection();
}

void CemuleDlg::ResetServerInfo()
{
	serverwnd->servermsgbox->Reset();
}

void CemuleDlg::ResetLog()
{
	serverwnd->logbox->Reset();
}

void CemuleDlg::ResetDebugLog()
{
	serverwnd->debuglog->Reset();
}

namespace
{
constexpr int kMaxUiLogLineChars = 1000;

CString BuildUiLogLine(const CTime &timestamp, LPCTSTR pszText)
{
	CString strLogLine;
	strLogLine.Format(_T("%s: %s\r\n"), (LPCTSTR)timestamp.Format(thePrefs.GetDateTimeFormat4Log()), pszText != NULL ? pszText : _T(""));
	if (strLogLine.GetLength() > kMaxUiLogLineChars)
		strLogLine.Truncate(kMaxUiLogLineChars);
	return strLogLine;
}
}

void CemuleDlg::AddLogText(UINT uFlags, LPCTSTR pszText, const CTime *pTimestamp)
{
	if (GetCurrentThreadId() != g_uMainThreadId) {
		theApp.QueueLogLineEx(uFlags, _T("%s"), pszText);
		return;
	}

	if (uFlags & LOG_STATUSBAR) {
		if (statusbar->m_hWnd) {
			if (!theApp.IsClosing())
				statusbar->SetText(pszText, SBarLog, 0);
		} else
			AfxMessageBox(pszText);
	}
#ifdef _DEBUG
	Debug(_T("%s\n"), pszText);
#endif

	if ((uFlags & LOG_DEBUG) && !thePrefs.GetVerbose())
		return;

	const CTime timestamp = (pTimestamp != NULL) ? *pTimestamp : CTime::GetCurrentTime();
	const CString strLogLine(BuildUiLogLine(timestamp, pszText));
	const int iLen = strLogLine.GetLength();
	if (iLen > 0) {
		if (!(uFlags & LOG_DEBUG)) {
			serverwnd->logbox->AddTyped(strLogLine, iLen, uFlags & LOGMSGTYPEMASK);
			if (::IsWindow(serverwnd->StatusSelector) && serverwnd->StatusSelector.GetCurSel() != CServerWnd::PaneLog)
				serverwnd->StatusSelector.HighlightItem(CServerWnd::PaneLog, TRUE);
			if (!(uFlags & LOG_DONTNOTIFY) && status) //status!=0 means this dialog has been created
				ShowNotifier(pszText, TBN_LOG);
			if (thePrefs.GetLog2Disk())
				theLog.Log(strLogLine, iLen);
		}

		if (thePrefs.GetVerbose() && ((uFlags & LOG_DEBUG) || thePrefs.GetFullVerbose())) {
			serverwnd->debuglog->AddTyped(strLogLine, iLen, uFlags & LOGMSGTYPEMASK);
			if (::IsWindow(serverwnd->StatusSelector) && serverwnd->StatusSelector.GetCurSel() != CServerWnd::PaneVerboseLog)
				serverwnd->StatusSelector.HighlightItem(CServerWnd::PaneVerboseLog, TRUE);

			if (thePrefs.GetDebug2Disk())
				theVerboseLog.Log(strLogLine, iLen);
		}
	}
}

void CemuleDlg::BeginLogBatchUpdate()
{
	if (serverwnd == NULL)
		return;
	if (serverwnd->logbox != NULL)
		serverwnd->logbox->BeginUpdateBatch();
	if (serverwnd->debuglog != NULL)
		serverwnd->debuglog->BeginUpdateBatch();
}

void CemuleDlg::EndLogBatchUpdate()
{
	if (serverwnd == NULL)
		return;
	if (serverwnd->debuglog != NULL)
		serverwnd->debuglog->EndUpdateBatch();
	if (serverwnd->logbox != NULL)
		serverwnd->logbox->EndUpdateBatch();
}

CString CemuleDlg::GetLastLogEntry()
{
	return serverwnd->logbox->GetLastLogEntry();
}

CString CemuleDlg::GetAllLogEntries()
{
	return serverwnd->logbox->GetAllLogEntries();
}

CString CemuleDlg::GetLastDebugLogEntry()
{
	return serverwnd->debuglog->GetLastLogEntry();
}

CString CemuleDlg::GetAllDebugLogEntries()
{
	return serverwnd->debuglog->GetAllLogEntries();
}

CString CemuleDlg::GetServerInfoText()
{
	return serverwnd->servermsgbox->GetText();
}

void CemuleDlg::AddServerMessageLine(UINT uFlags, LPCTSTR pszLine)
{
	if (pszLine == NULL)
		return;
	CString strTrimmedLine(pszLine);
	strTrimmedLine.Trim();
	if (strTrimmedLine.IsEmpty())
		return;
	CString strMsgLine(strTrimmedLine);
	strMsgLine += _T('\n');
	if ((uFlags & LOGMSGTYPEMASK) == LOG_INFO)
		serverwnd->servermsgbox->AppendText(strMsgLine);
	else
		serverwnd->servermsgbox->AddTyped(strMsgLine, strMsgLine.GetLength(), uFlags & LOGMSGTYPEMASK);
	if (::IsWindow(serverwnd->StatusSelector) && serverwnd->StatusSelector.GetCurSel() != CServerWnd::PaneServerInfo)
		serverwnd->StatusSelector.HighlightItem(CServerWnd::PaneServerInfo, TRUE);
}

UINT CemuleDlg::GetConnectionStateIconIndex() const
{
	//Calculate index in 'm_connicons' array
	//3 KAD states per group: "disconnected", "firewalled", "open"
	//Groups correspond to ED2K states: "disconnected", "low ID", "high ID"
	UINT idx = static_cast<UINT>(Kademlia::CKademlia::IsConnected());
	if (idx)
		idx += static_cast<UINT>(!Kademlia::CKademlia::IsFirewalled());
	if (theApp.serverconnect->IsConnected())
		idx += theApp.serverconnect->IsLowID() ? 3 : 6;
	return idx;
}

void CemuleDlg::ShowConnectionStateIcon()
{
	UINT uIconIdx = GetConnectionStateIconIndex();
	ASSERT(uIconIdx < _countof(m_connicons));
	statusbar->SetIcon(SBarConnected, m_connicons[uIconIdx]);
}

HICON CemuleDlg::GetMainConnectionStateIcon(AppMainIconSeams::EConnectionIcon eIcon) const
{
	if (eIcon == AppMainIconSeams::EConnectionIcon::LowID && m_hLowIDIcon != NULL)
		return m_hLowIDIcon;
	return m_hIcon;
}

void CemuleDlg::ShowMainConnectionStateIcon()
{
	if (!::IsWindow(m_hWnd))
		return;

	const AppMainIconSeams::EConnectionIcon eNextIcon =
		AppMainIconSeams::SelectConnectionIcon(theApp.IsConnected(), theApp.IsFirewalled());
	if (!AppMainIconSeams::ShouldApplyConnectionIcon(m_eMainConnectionIcon, eNextIcon))
		return;

	HICON hIcon = GetMainConnectionStateIcon(eNextIcon);
	if (hIcon == NULL)
		return;

	SetIcon(hIcon, TRUE);
	SetIcon(hIcon, FALSE);
	m_eMainConnectionIcon = eNextIcon;
}

CString CemuleDlg::GetConnectionStateString()
{
	UINT ed2k, kad;
	if (theApp.serverconnect->IsConnected())
		ed2k = IDS_CONNECTED;
	else
		ed2k = theApp.serverconnect->IsConnecting() ? IDS_CONNECTING : IDS_DISCONNECTED;

	if (Kademlia::CKademlia::IsConnected())
		kad = IDS_CONNECTED;
	else
		kad = Kademlia::CKademlia::IsRunning() ? IDS_CONNECTING : IDS_DISCONNECTED;

	CString state;
	state.Format(_T("eD2K:%s|Kad:%s"), (LPCTSTR)GetResString(ed2k), (LPCTSTR)GetResString(kad));
	return state;
}

CString CemuleDlg::GetNetworkAddressStateString() const
{
	CString strBindAddress;
	if (thePrefs.GetBindAddr() != NULL)
		strBindAddress = thePrefs.GetBindAddr();
	return StatusBarInfo::FormatNetworkAddressPaneText(strBindAddress
		, theApp.GetPublicIP()
		, GetResString(IDS_STATUS_BIND_IP_COMPACT_LABEL)
		, GetResString(IDS_STATUS_PUBLIC_IP_COMPACT_LABEL)
		, GetResString(IDS_STATUS_ANY_COMPACT)
		, GetResString(IDS_STATUS_UNKNOWN_COMPACT)
		, GetResString(IDS_STATUS_NETWORK_ADDRESS_TEXT_FMT));
}

void CemuleDlg::ShowNetworkAddressState()
{
	if (theApp.IsClosing() || statusbar == NULL || !::IsWindow(statusbar->m_hWnd))
		return;
	statusbar->SetText(GetNetworkAddressStateString(), SBarIP, 0);
}

void CemuleDlg::ShowConnectionState()
{
	if (theApp.IsClosing())
		return;
	theApp.downloadqueue->OnConnectionState(theApp.IsConnected());
	serverwnd->UpdateMyInfo();
	serverwnd->UpdateControlsState();
	kademliawnd->UpdateControlsState();

	ShowConnectionStateIcon();
	ShowMainConnectionStateIcon();
	statusbar->SetText(GetConnectionStateString(), SBarConnected, 0);
	ShowNetworkAddressState();

	TBBUTTONINFO tbbi;
	tbbi.cbSize = (UINT)sizeof(TBBUTTONINFO);
	tbbi.dwMask = TBIF_IMAGE | TBIF_TEXT;

	if (theApp.IsConnected()) {
		CString strPane(GetResString(IDS_MAIN_BTN_DISCONNECT));
		tbbi.iImage = 1;
		tbbi.pszText = const_cast<LPTSTR>((LPCTSTR)strPane);
		toolbar->SetButtonInfo(TBBTN_CONNECT, &tbbi);
		strPane.Remove(_T('&'));
		if (!theApp.emuledlg->m_SysMenuOptions.ModifyMenuW(MP_CONNECT, MF_STRING, MP_DISCONNECT, strPane))
			theApp.emuledlg->m_SysMenuOptions.ModifyMenuW(MP_DISCONNECT, MF_STRING, MP_DISCONNECT, strPane); //replace "Cancel" with "Disconnect"
	} else {
		if (theApp.serverconnect->IsConnecting() || Kademlia::CKademlia::IsRunning()) {
			CString strPane(GetResString(IDS_MAIN_BTN_CANCEL));
			tbbi.iImage = 2;
			tbbi.pszText = const_cast<LPTSTR>((LPCTSTR)strPane);
			toolbar->SetButtonInfo(TBBTN_CONNECT, &tbbi);
			strPane.Remove(_T('&'));
			theApp.emuledlg->m_SysMenuOptions.ModifyMenuW(MP_CONNECT, MF_STRING, MP_DISCONNECT, strPane);
		} else {
			CString strPane(GetResString(IDS_MAIN_BTN_CONNECT));
			tbbi.iImage = 0;
			tbbi.pszText = const_cast<LPTSTR>((LPCTSTR)strPane);
			toolbar->SetButtonInfo(TBBTN_CONNECT, &tbbi);
			strPane.Remove(_T('&'));
			theApp.emuledlg->m_SysMenuOptions.ModifyMenuW(MP_DISCONNECT, MF_STRING, MP_CONNECT, strPane);
		}
	}
	const bool bCanStopOrCancel = theApp.IsConnected() || theApp.serverconnect->IsConnecting() || Kademlia::CKademlia::IsRunning();
	const bool bCanUseConnectionCommand = bCanStopOrCancel || CanUseP2PConnectionCommands();
	toolbar->EnableButton(TBBTN_CONNECT, bCanUseConnectionCommand);
	theApp.emuledlg->m_SysMenuOptions.EnableMenuItem(bCanStopOrCancel ? MP_DISCONNECT : MP_CONNECT,
		bCanUseConnectionCommand ? MF_ENABLED : MF_GRAYED);
	ShowUserCount();
#ifdef HAVE_WIN7_SDK_H
	UpdateThumbBarButtons();
#endif
}

void CemuleDlg::ShowUserCount()
{
	uint32 totaluser, totalfile;
	theApp.serverlist->GetUserFileStatus(totaluser, totalfile);
	CString buffer;
	if (theApp.serverconnect->IsConnected() && Kademlia::CKademlia::IsRunning() && Kademlia::CKademlia::IsConnected())
		buffer.Format(_T("%s:%s(%s)|%s:%s(%s)"), (LPCTSTR)GetResString(IDS_UUSERS), (LPCTSTR)CastItoIShort(totaluser, false, 1), (LPCTSTR)CastItoIShort(Kademlia::CKademlia::GetKademliaUsers(), false, 1), (LPCTSTR)GetResString(IDS_FILES), (LPCTSTR)CastItoIShort(totalfile, false, 1), (LPCTSTR)CastItoIShort(Kademlia::CKademlia::GetKademliaFiles(), false, 1));
	else if (theApp.serverconnect->IsConnected())
		buffer.Format(_T("%s:%s|%s:%s"), (LPCTSTR)GetResString(IDS_UUSERS), (LPCTSTR)CastItoIShort(totaluser, false, 1), (LPCTSTR)GetResString(IDS_FILES), (LPCTSTR)CastItoIShort(totalfile, false, 1));
	else if (Kademlia::CKademlia::IsRunning() && Kademlia::CKademlia::IsConnected())
		buffer.Format(_T("%s:%s|%s:%s"), (LPCTSTR)GetResString(IDS_UUSERS), (LPCTSTR)CastItoIShort(Kademlia::CKademlia::GetKademliaUsers(), false, 1), (LPCTSTR)GetResString(IDS_FILES), (LPCTSTR)CastItoIShort(Kademlia::CKademlia::GetKademliaFiles(), false, 1));
	else
		buffer.Format(_T("%s:0|%s:0"), (LPCTSTR)GetResString(IDS_UUSERS), (LPCTSTR)GetResString(IDS_FILES));
	statusbar->SetText(buffer, SBarUsers, 0);
}

void CemuleDlg::ShowMessageState(UINT nIcon)
{
	m_iMsgIcon = nIcon;
	statusbar->SetIcon(SBarChatMsg, imicons[m_iMsgIcon]);
}

void CemuleDlg::ShowTransferStateIcon()
{
	int i = (m_uDownDatarate ? 1 : 0) | (m_uUpDatarate ? 2 : 0);
	statusbar->SetIcon(SBarUpDown, transicons[i]);
}

CString CemuleDlg::GetUpDatarateString(UINT uUpDatarate)
{
	m_uUpDatarate = (uUpDatarate != UINT_MAX) ? uUpDatarate : theApp.uploadqueue->GetDatarate();
	CString szBuff;
	if (thePrefs.ShowOverhead())
		szBuff.Format(_T("%.1f (%.1f)"), m_uUpDatarate / 1024.0, theStats.GetUpDatarateOverhead() / 1024.0);
	else
		szBuff.Format(_T("%.1f"), m_uUpDatarate / 1024.0);
	return szBuff;
}

CString CemuleDlg::GetDownDatarateString(UINT uDownDatarate)
{
	m_uDownDatarate = uDownDatarate != UINT_MAX ? uDownDatarate : theApp.downloadqueue->GetDatarate();
	CString szBuff;
	if (thePrefs.ShowOverhead())
		szBuff.Format(_T("%.1f (%.1f)"), m_uDownDatarate / 1024.0, theStats.GetDownDatarateOverhead() / 1024.0);
	else
		szBuff.Format(_T("%.1f"), m_uDownDatarate / 1024.0);
	return szBuff;
}

CString CemuleDlg::GetTransferRateString()
{
	CString szBuff;
	if (thePrefs.ShowOverhead())
		szBuff.Format(GetResString(IDS_UPDOWN)
			, m_uUpDatarate / 1024.0, theStats.GetUpDatarateOverhead() / 1024.0
			, m_uDownDatarate / 1024.0, theStats.GetDownDatarateOverhead() / 1024.0);
	else
		szBuff.Format(GetResString(IDS_UPDOWNSMALL), m_uUpDatarate / 1024.0, m_uDownDatarate / 1024.0);
	return szBuff;
}

void CemuleDlg::ShowTransferRate(bool bForceAll, bool bTitleOnly)
{
	if (bForceAll)
		m_uLastSysTrayIconCookie = SYS_TRAY_ICON_COOKIE_FORCE_UPDATE;

	m_uDownDatarate = theApp.downloadqueue->GetDatarate();
	m_uUpDatarate = theApp.uploadqueue->GetDatarate();

	const CString &strTransferRate = GetTransferRateString();
	if (!bTitleOnly && (TrayIconVisible() || bForceAll)) {
		// set tray icon
		int iDownRatePercent = (int)ceil((m_uDownDatarate / 10.24) / thePrefs.GetMaxGraphDownloadRate());
		UpdateTrayIcon(min(iDownRatePercent, 100));

		CString buffer;
		buffer.Format(_T("%s %s (%s)\r\n%s")
			, MOD_RELEASE_PRODUCT_NAME
			, (LPCTSTR)theApp.m_strCurVersionLong
			, (LPCTSTR)GetResString(theApp.IsConnected() ? IDS_CONNECTED : IDS_DISCONNECTED)
			, (LPCTSTR)strTransferRate);

		TraySetToolTip(buffer);
	}

	if (!bTitleOnly && (IsWindowVisible() || bForceAll)) {
		statusbar->SetText(strTransferRate, SBarUpDown, 0);
		ShowTransferStateIcon();
	}
	if (IsWindowVisible() && thePrefs.ShowRatesOnTitle()) {
		CString szBuff;
		szBuff.Format(_T("(U:%.1f D:%.1f) %s %s"), m_uUpDatarate / 1024.0f, m_uDownDatarate / 1024.0f, MOD_RELEASE_PRODUCT_NAME, (LPCTSTR)theApp.m_strCurVersionLong);
		SetWindowText(szBuff);
	}
	if (!bTitleOnly && m_pMiniMule != NULL && m_pMiniMule->GetSafeHwnd() != NULL && m_pMiniMule->IsWindowVisible())
		m_pMiniMule->UpdateContent(m_uUpDatarate, m_uDownDatarate);
}

void CemuleDlg::OnOK()
{
}

void CemuleDlg::OnCancel()
{
	if (!thePrefs.GetStraightWindowStyles())
		MinimizeWindow();
}

void CemuleDlg::MinimizeWindow(bool bForceTray)
{
	if (bForceTray || *thePrefs.GetMinTrayPTR()) {
		WINDOWPLACEMENT wp = {};
		wp.length = (UINT)sizeof wp;
		if (GetWindowPlacement(&wp)) {
			if (wp.showCmd == SW_SHOWMINIMIZED && (wp.flags & WPF_RESTORETOMAXIMIZED))
				wp.showCmd = SW_SHOWMAXIMIZED;
			if (wp.showCmd == SW_SHOWMAXIMIZED || wp.showCmd == SW_SHOWNORMAL) {
				wp.flags = 0;
				m_wpFirstRestore = wp;
			}
		}
		if (bForceTray) {
			if (TrayShow())
				ShowWindow(SW_HIDE);
		} else {
			ShowWindow(SW_HIDE);
			UpdateTrayVisibility();
		}
	} else
		ShowWindow(SW_MINIMIZE);

	ShowTransferRate();
	if (transferwnd != NULL && !theApp.IsClosing())
		transferwnd->RefreshTransferDisplayRefreshState(false);
}

void CemuleDlg::SetActiveDialog(CWnd *dlg)
{
	if (dlg == activewnd)
		return;
	if (activewnd)
		activewnd->ShowWindow(SW_HIDE);
	dlg->ShowWindow(SW_SHOW);
	dlg->SetFocus();
	activewnd = dlg;
	int iToolbarButtonID = MapWindowToToolbarButton(dlg);
	if (iToolbarButtonID != -1)
		toolbar->PressMuleButton(iToolbarButtonID);
	if (dlg == transferwnd) {
		if (thePrefs.ShowCatTabInfos())
			transferwnd->UpdateCatTabTitles();
		transferwnd->RefreshTransferDisplayRefreshState();
	} else if (dlg == chatwnd)
		chatwnd->chatselector.ShowChat();
	else if (dlg == statisticswnd)
		statisticswnd->ShowStatistics();
	if (dlg != transferwnd && transferwnd != NULL)
		transferwnd->RefreshTransferDisplayRefreshState(false);
}

void CemuleDlg::SetStatusBarPartsSize()
{
	RECT rect;
	statusbar->GetClientRect(&rect);
	int aiWidths[6] =
	{
		rect.right - 940,
		rect.right - 695,
		rect.right - 450,
		rect.right - 250,
		rect.right - 25,
		-1
	};
	statusbar->SetParts(_countof(aiWidths), aiWidths);
}

void CemuleDlg::OnSize(UINT nType, int cx, int cy)
{
	CTrayDialog::OnSize(nType, cx, cy);
	SetStatusBarPartsSize();
	// we might receive this message during shutdown -> bad
	if (transferwnd != NULL && !theApp.IsClosing()) {
		transferwnd->VerifyCatTabSize();
		transferwnd->RefreshTransferDisplayRefreshState(nType != SIZE_MINIMIZED);
	}
}

void CemuleDlg::OnActivate(UINT nState, CWnd *pWndOther, BOOL bMinimized)
{
	CTrayDialog::OnActivate(nState, pWndOther, bMinimized);
	if (transferwnd != NULL && !theApp.IsClosing())
		transferwnd->RefreshTransferDisplayRefreshState(nState != WA_INACTIVE && !bMinimized);
}

void CemuleDlg::ProcessED2KLink(LPCTSTR pszData)
{
	try {
		CString link(pszData);
		link.Replace(_T("%7c"), _T("|"));
		CED2KLink *pLink = CED2KLink::CreateLinkFromUrl(OptUtf8ToStr(URLDecode(link)));
		ASSERT(pLink);
		switch (pLink->GetKind()) {
		case CED2KLink::kFile:
			{
				CED2KFileLink *pFileLink = pLink->GetFileLink();
				ASSERT(pFileLink);
				theApp.downloadqueue->AddFileLinkToDownload(*pFileLink, searchwnd->GetSelectedCat());
			}
			break;
		case CED2KLink::kServerList:
			{
				CED2KServerListLink *pListLink = pLink->GetServerListLink();
				ASSERT(pListLink);
				const CString &strAddress(pListLink->GetAddress());
				if (!strAddress.IsEmpty())
					serverwnd->UpdateServerMetFromURL(strAddress);
			}
			break;
		case CED2KLink::kNodesList:
			{
				const CED2KNodesListLink *pListLink = pLink->GetNodesListLink();
				ASSERT(pListLink);
				const CString &strAddress(pListLink->GetAddress());
				// Because the nodes.dat is vital for kad and its routing and doesn't need to be
				// updated in general, we request a confirm to avoid accidental / malicious updating
				// of this file. This is a bit inconsistent as the same kinda applies to the server.met,
				// but those require more updates and are easier to understand
				if (!strAddress.IsEmpty()) {
					CString strConfirm;
					strConfirm.Format(GetResString(IDS_CONFIRMNODESDOWNLOAD), (LPCTSTR)strAddress);
					if (AfxMessageBox(strConfirm, MB_YESNO | MB_ICONQUESTION, 0) == IDYES)
						kademliawnd->UpdateNodesDatFromURL(strAddress);
				}
			}
			break;
		case CED2KLink::kServer:
			{
				CED2KServerLink *pSrvLink = pLink->GetServerLink();
				ASSERT(pSrvLink);
				CServer *pSrv = new CServer(pSrvLink->GetPort(), pSrvLink->GetAddress());
				ASSERT(pSrv);
				CString defName;
				pSrvLink->GetDefaultName(defName);
				pSrv->SetListName(defName);

				// Barry - Default all new servers to high priority
				if (thePrefs.GetManualAddedServersHighPriority())
					pSrv->SetPreference(SRV_PR_HIGH);

				if (!serverwnd->serverlistctrl.AddServer(pSrv, true))
					delete pSrv;
				else
					AddLogLine(true, GetResString(IDS_SERVERADDED), (LPCTSTR)pSrv->GetListName());
			}
			break;
		case CED2KLink::kSearch:
			{
				CED2KSearchLink *pListLink = pLink->GetSearchLink();
				ASSERT(pListLink);
				SetActiveDialog(searchwnd);
				searchwnd->ProcessEd2kSearchLinkRequest(pListLink->GetSearchTerm());
			}
			[[fallthrough]];
		default:
			break;
		}
		delete pLink;
	} catch (const CString &strError) {
		LogWarning(LOG_STATUSBAR, _T("%s - %s"), (LPCTSTR)GetResString(IDS_LINKNOTADDED), (LPCTSTR)strError);
	} catch (...) {
		LogWarning(LOG_STATUSBAR, GetResString(IDS_LINKNOTADDED));
	}
}

#pragma warning(push)
#pragma warning(disable:4774)
LRESULT CemuleDlg::OnWMData(WPARAM, LPARAM lParam)
{
	PCOPYDATASTRUCT data = (PCOPYDATASTRUCT)lParam;
	ULONG_PTR op = data->dwData;
	if ((op == OP_ED2KLINK && thePrefs.IsBringToFront()) || op == OP_COLLECTION) {
		if (IsIconic())
			ShowWindow(SW_SHOWNORMAL);
		else
			RestoreWindow();
		FlashWindow(TRUE);
	}
	switch (op) {
	case OP_ED2KLINK:
		ProcessED2KLink((LPCTSTR)data->lpData);
		break;
	case OP_COLLECTION:
		{
			CCollection *pCollection = new CCollection();
			const CString &strPath((LPCTSTR)data->lpData);
			if (pCollection->InitCollectionFromFile(strPath, strPath.Right(strPath.GetLength() - 1 - strPath.ReverseFind(_T('\\'))))) {
				CCollectionViewDialog dialog;
				dialog.SetCollection(pCollection);
				dialog.DoModal();
			}
			delete pCollection;
		}
		break;
	case OP_CLCOMMAND:
		{
			// command line command received
			CString clcommand((LPCTSTR)data->lpData);
			clcommand.MakeLower();
			AddLogLine(true, _T("CLI: %s"), (LPCTSTR)clcommand);

			if (clcommand == _T("connect"))
				StartConnection();
			else if (clcommand == _T("disconnect"))
				theApp.serverconnect->Disconnect();
			else if (clcommand == _T("exit")) {
				theApp.m_app_state = APP_STATE_SHUTTINGDOWN; // do no ask to close
				OnClose();
			} else if (clcommand == _T("help") || clcommand == _T("/?"))
				; // show usage
			else if (clcommand.Left(7) == _T("limits=") && clcommand.GetLength() > 8) {
				clcommand.Delete(0, 7);
				int pos = clcommand.Find(_T(','));
				if (pos > 0) {
					if (clcommand[pos + 1])
						thePrefs.SetMaxDownload(_tstoi(CPTR(clcommand, pos + 1)));
					clcommand.Truncate(pos);
				}
				if (!clcommand.IsEmpty())
					thePrefs.SetMaxUpload(_tstoi(clcommand));
			} else if (clcommand == _T("reloadipf"))
				theApp.ipfilter->LoadFromDefaultFile();
			else if (clcommand == _T("restore"))
				RestoreWindow();
			else if (clcommand == _T("resume"))
				theApp.downloadqueue->StartNextFile();
			else if (clcommand == _T("status")) {
				FILE *file = LongPathSeams::OpenFileStreamDenyWriteLongPath(thePrefs.GetMuleDirectory(EMULE_CONFIGBASEDIR) + _T("status.log"), _T("wt"));
				if (file) {
					UINT uid;
					if (theApp.serverconnect->IsConnected())
						uid = IDS_CONNECTED;
					else if (theApp.serverconnect->IsConnecting())
						uid = IDS_CONNECTING;
					else
						uid = IDS_DISCONNECTED;
					_ftprintf(file, _T("%s\n"), (LPCTSTR)GetResString(uid));
					_ftprintf(file, (LPCTSTR)GetResString(IDS_UPDOWNSMALL), theApp.uploadqueue->GetDatarate() / 1024.0f, theApp.downloadqueue->GetDatarate() / 1024.0f);
					// next string (getTextList) is already prefixed with '\n'!
					_ftprintf(file, _T("%s\n"), (LPCTSTR)transferwnd->GetDownloadList()->getTextList());

					fclose(file);
				}
			}
			//else show "unknown command"; Or "usage"
		}
	}
	return TRUE;
}
#pragma warning(pop)

LRESULT CemuleDlg::OnFileHashed(WPARAM wParam, LPARAM lParam)
{
	CKnownFile *result = reinterpret_cast<CKnownFile*>(lParam);
	if (theApp.IsClosing()) {
		delete result;
		return FALSE;
	}

	if (result == NULL)
		return FALSE;
	ASSERT(result->IsKindOf(RUNTIME_CLASS(CKnownFile)));

	if (wParam) {
		// File hashing finished for a part file when:
		// - part file just completed
		// - part file was rehashed at startup because the file date of part.met did not match the part file date

		CPartFile *requester = reinterpret_cast<CPartFile*>(wParam);

		// SLUGFILLER: SafeHash - could have been cancelled
		if (theApp.downloadqueue != NULL && theApp.downloadqueue->IsPartFile(requester)) {
			ASSERT(requester->IsKindOf(RUNTIME_CLASS(CPartFile)));
			if (requester->GetFileOp() == PFOP_HASHING)
				requester->SetFileOp(PFOP_NONE);
			requester->PartFileHashFinished(result);
		} else
			delete result;
		// SLUGFILLER: SafeHash
	} else {
		ASSERT(!result->IsKindOf(RUNTIME_CLASS(CPartFile)));

		// File hashing finished for a shared file (not a partfile) when:
		//	- reading shared directories at startup and hashing files which were not found in known.met
		//	- reading shared directories during runtime (user hit Reload button, added a shared directory, ...)
		if (theApp.sharedfiles != NULL)
			theApp.sharedfiles->FileHashingFinished(result);
		else
			delete result;
	}
	return TRUE;
}

LRESULT CemuleDlg::OnFileOpProgress(WPARAM wParam, LPARAM lParam)
{
	if (!theApp.IsClosing()) {
		CKnownFile *pKnownFile = reinterpret_cast<CKnownFile*>(lParam);
		ASSERT(pKnownFile->IsKindOf(RUNTIME_CLASS(CKnownFile)));

		if (pKnownFile->IsKindOf(RUNTIME_CLASS(CPartFile))) {
			CPartFile *pPartFile = static_cast<CPartFile*>(pKnownFile);
			pPartFile->SetFileOpProgress(wParam);
			pPartFile->UpdateDisplayedInfo(true);
		}
	}
	return 0;
}

// SLUGFILLER: SafeHash
LRESULT CemuleDlg::OnHashFailed(WPARAM, LPARAM lParam)
{
	UnknownFile_Struct *pHashed = reinterpret_cast<UnknownFile_Struct*>(lParam);
	if (!theApp.IsClosing() && theApp.sharedfiles != NULL)
		theApp.sharedfiles->HashFailed(pHashed);
	else
		delete pHashed;
	return 0;
}
// SLUGFILLER: SafeHash

LRESULT CemuleDlg::OnSharedFileHashed(WPARAM, LPARAM lParam)
{
	CSharedFileHashResult *pResult = reinterpret_cast<CSharedFileHashResult*>(lParam);
	if (pResult == NULL)
		return 0;

	if (!theApp.IsClosing() && theApp.sharedfiles != NULL && pResult->pOwner == theApp.sharedfiles && !theApp.sharedfiles->IsSharedHashWorkerShuttingDown())
		theApp.sharedfiles->FileHashingFinished(pResult);
	else
		delete pResult->pKnownFile;
	delete pResult;
	return TRUE;
}

LRESULT CemuleDlg::OnSharedHashFailed(WPARAM, LPARAM lParam)
{
	CSharedFileHashResult *pResult = reinterpret_cast<CSharedFileHashResult*>(lParam);
	if (pResult == NULL)
		return 0;

	if (!theApp.IsClosing() && theApp.sharedfiles != NULL && pResult->pOwner == theApp.sharedfiles && !theApp.sharedfiles->IsSharedHashWorkerShuttingDown())
		theApp.sharedfiles->HashFailed(pResult);
	delete pResult->pKnownFile;
	delete pResult;
	return TRUE;
}

LRESULT CemuleDlg::OnSharedHashResultsAvailable(WPARAM, LPARAM)
{
	if (!theApp.IsClosing() && theApp.sharedfiles != NULL)
		theApp.sharedfiles->DrainDeferredSharedHashResults();
	return TRUE;
}

LRESULT CemuleDlg::OnFileAllocExc(WPARAM wParam, LPARAM lParam)
{
	if (lParam == 0)
		reinterpret_cast<CPartFile*>(wParam)->FlushBuffersExceptionHandler();
	else
		reinterpret_cast<CPartFile*>(wParam)->FlushBuffersExceptionHandler(reinterpret_cast<CFileException*>(lParam));
	return 0;
}

LRESULT CemuleDlg::OnFileCompleted(WPARAM wParam, LPARAM lParam)
{
	(void)wParam;
	void *pCompletionResult = reinterpret_cast<void*>(lParam);
	CPartFile *partfile = CPartFile::GetCompletionResultFile(pCompletionResult);
	if (partfile == NULL) {
		CPartFile::DiscardCompletionResult(pCompletionResult);
		return 0;
	}
	if (theApp.downloadqueue == NULL || !theApp.downloadqueue->IsPartFile(partfile)) {
		ASSERT(0);
		CPartFile::DiscardCompletionResult(pCompletionResult);
		return 0;
	}
	partfile->SetFileOp(PFOP_NONE);
	partfile->PerformFileCompleteEnd(pCompletionResult);
	return 0;
}

#ifdef _DEBUG
static void BeBusy(UINT uSeconds, LPCSTR pszCaller)
{
	UINT s = 0;
	while (uSeconds--) {
		theVerboseLog.Logf(_T("%hs: called=%hs, waited %u sec."), __FUNCTION__, pszCaller, s++);
		::Sleep(SEC2MS(1));
	}
}
#endif

BOOL CemuleDlg::OnQueryEndSession()
{
	AddDebugLogLine(DLP_VERYLOW, _T("%hs"), __FUNCTION__);
	if (!CTrayDialog::OnQueryEndSession())
		return FALSE;

	AddDebugLogLine(DLP_VERYLOW, _T("%hs: returning TRUE"), __FUNCTION__);
	return TRUE;
}

void CemuleDlg::OnEndSession(BOOL bEnding)
{
	AddDebugLogLine(DLP_VERYLOW, _T("%hs: bEnding=%d"), __FUNCTION__, bEnding);
	if (bEnding && !theApp.IsClosing()) {
		// If eMule was *not* started with "RUNAS":
		// When user is logging of (or reboots or shutdown system), Windows sends the
		// WM_QUERYENDSESSION/WM_ENDSESSION to all top level windows.
		// Here we can consume as much time as we need to perform our shutdown. Even if we
		// take longer than 20 seconds, Windows will just show a dialog box that 'emule'
		// is not terminating in time and gives the user a chance to cancel that. If the user
		// does not cancel the Windows dialog, Windows will though wait until eMule has
		// terminated by itself - no data loss, no file corruption, everything is fine.
		theApp.m_app_state = APP_STATE_SHUTTINGDOWN;
		OnClose();
	}

	CTrayDialog::OnEndSession(bEnding);
	AddDebugLogLine(DLP_VERYLOW, _T("%hs: returning"), __FUNCTION__);
}

LRESULT CemuleDlg::OnUserChanged(WPARAM, LPARAM)
{
	AddDebugLogLine(DLP_VERYLOW, _T("%hs"), __FUNCTION__);
	// Just want to know if we ever get this message. Maybe it helps us to handle the
	// logoff/reboot/shutdown problem when eMule was started with "RUNAS".
	return Default();
}

LRESULT CemuleDlg::OnConsoleThreadEvent(WPARAM wParam, LPARAM lParam)
{
	AddDebugLogLine(DLP_VERYLOW, _T("%hs: nEvent=%u, nThreadID=%u"), __FUNCTION__, wParam, lParam);

	// If eMule was started with "RUNAS":
	// This message handler receives a 'console event' from the concurrently and thus
	// asynchronously running console control handler thread which was spawned by Windows
	// in case the user logs off/reboots/shutdown. Even if the console control handler thread
	// is waiting on the result from this message handler (is waiting until the main thread
	// has finished processing this inter-application message), the application will get
	// forcefully terminated by Windows after 20 seconds! There is no known way to prevent
	// that. This means, that if we would invoke our standard shutdown code ('OnClose') here
	// and the shutdown takes longer than 20 sec, we will get forcefully terminated by
	// Windows, regardless of what we are doing. This means, MET-file and PART-file corruption
	// may occur. Because the shutdown code in 'OnClose' does also shutdown Kad (which takes
	// a noticeable amount of time) it is not that unlikely that we run into problems with
	// not being finished with our shutdown in 20 seconds.
	//
	if (!theApp.IsClosing()) {
#if 1
		// And it really should be OK to expect that emule can shutdown in 20 sec on almost
		// all computers. So, use the proper shutdown.
		theApp.m_app_state = APP_STATE_SHUTTINGDOWN;
		OnClose();	// do not invoke if shutdown takes longer than 20 sec, read above
#else
		// As a minimum action we at least set the 'shutting down' flag, this will help e.g.
		// the CUploadQueue::UploadTimer to not start any file save actions which could get
		// interrupted by windows and which would then lead to corrupted MET-files.
		// Setting this flag also helps any possible running threads to stop their work.
		theApp.m_app_state = APP_STATE_SHUTTINGDOWN;

#ifdef _DEBUG
		// Simulate some work.
		//
		// NOTE: If the console thread has already exited, Windows may terminate the process
		// even before the 20 sec. timeout!
		//BeBusy(70, __FUNCTION__);
#endif

		// Actually, just calling 'ExitProcess' should be the most safe thing which we can
		// do here. Because we received this message via the main message queue we are
		// totally in-sync with the application and therefore we know that we are currently
		// not within a file save action and thus we simply can not cause any file corruption
		// when we exit right now.
		//
		// Of course, there may be some data loss. But it's the same amount of data loss which
		// could occur if we keep running. But if we keep running and wait until Windows
		// terminates us after 20 sec, there is also the chance for file corruption.
		if (thePrefs.GetDebug2Disk()) {
			theVerboseLog.Logf(_T("%hs: ExitProcess"), __FUNCTION__);
			theVerboseLog.Close();
		}
		ExitProcess(0);
#endif
	}

	AddDebugLogLine(DLP_VERYLOW, _T("%hs: returning"), __FUNCTION__);
	return 1;
}

void CemuleDlg::OnDestroy()
{
	AddDebugLogLine(DLP_VERYLOW, _T("%hs"), __FUNCTION__);
	DiscardPostedDisplayRefreshRequests(GetSafeHwnd());
	StopTransferRateDisplayTimer();
	StopBindLossMonitor();
	m_wndWindowsToastNotifier.Shutdown();

	// If eMule was started with "RUNAS":
	// When user is logging of (or reboots or shutdown system), Windows may or may not send
	// a WM_DESTROY (depends on how long the application needed to process the
	// CTRL_LOGOFF_EVENT). But, regardless of what happened and regardless of how long any
	// application specific shutdown took, Windows fill forcefully terminate the process
	// after 1-2 seconds after WM_DESTROY! So, we can not use WM_DESTROY for any lengthy
	// shutdown actions in that case.
	CTrayDialog::OnDestroy();
}

bool CemuleDlg::CanClose(UINT uPromptStringID)
{
	if (m_bBindLossShutdown)
		return true;

	if (theApp.m_app_state == APP_STATE_RUNNING && thePrefs.IsConfirmExitEnabled()) {
		theApp.m_app_state = APP_STATE_ASKCLOSE; //disable tray menu
		RestoreWindow(); // make sure the window is in foreground for this prompt
		ExitBox request(this, uPromptStringID);
		request.DoModal();
		if (request.WasCancelled()) {
			if (theApp.m_app_state == APP_STATE_ASKCLOSE) //if the application state has not changed
				theApp.m_app_state = APP_STATE_RUNNING; //then keep running
			return false;
		}
	}
	return true;
}

void CemuleDlg::OnClose()
{
	CloseApp(false);
}

void CemuleDlg::RestartApp()
{
	CloseApp(true);
}

bool CemuleDlg::TryStartRestartSidecar()
{
	CString strExecutablePath;
	if (!TryGetModuleFilePath(strExecutablePath)) {
		AddLogLine(false, _T("Failed to restart eMuleBB: could not resolve the executable path."));
		return false;
	}

	const CString strWorkingDirectory(thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
	CString strRequestPath;
	strRequestPath.Format(_T("%semulebb-restart-request-pid%lu.json"),
		(LPCTSTR)thePrefs.GetMuleDirectory(EMULE_CONFIGDIR),
		static_cast<unsigned long>(::GetCurrentProcessId()));

	const std::vector<CString> restartArguments =
		RestartAppSeams::BuildProfileRestartArguments(
			theApp.HasStartupConfigBaseDirOverride(),
			theApp.GetStartupConfigBaseDirOverride());

	CString strError;
	if (!TryWriteRestartRequest(strRequestPath, strExecutablePath, strWorkingDirectory, restartArguments, strError)) {
		AddLogLine(false, _T("Failed to restart eMuleBB: %s."), (LPCTSTR)strError);
		return false;
	}

	const std::vector<CString> sidecarArguments = {
		CString(_T("--restart-sidecar")),
		CString(_T("--request")),
		strRequestPath
	};
	const CString strSidecarCommandLine(RestartAppSeams::BuildCommandLine(strExecutablePath, sidecarArguments));
	const ProcessLaunchSeams::DetachedLaunchResult result =
		ProcessLaunchSeams::LaunchDetachedProcess(strExecutablePath, strSidecarCommandLine, strWorkingDirectory, SW_HIDE, CREATE_NO_WINDOW);
	if (!result.Started) {
		(void)::DeleteFile(strRequestPath);
		AddLogLine(false, _T("Failed to restart eMuleBB: restart sidecar launch failed with error %lu."), result.LastError);
		return false;
	}
	return true;
}

void CemuleDlg::CloseApp(bool bRestart)
{
	static LONG closing = 0;
	if (::InterlockedExchange(&closing, 1))
		return; //already closing
	if (!CanClose(bRestart ? IDS_MAIN_RESTART : IDS_MAIN_EXIT)) {
		::InterlockedExchange(&closing, 0);
		return;
	}
	if (bRestart && !TryStartRestartSidecar()) {
		if (theApp.m_app_state == APP_STATE_ASKCLOSE)
			theApp.m_app_state = APP_STATE_RUNNING;
		::InterlockedExchange(&closing, 0);
		return;
	}
	if (bRestart)
		AddLogLine(true, GetResString(IDS_RESTARTING_EMULE));

	DiscardPostedDisplayRefreshRequests(GetSafeHwnd());
	theApp.ReleaseStandbyPrevention();
	StopBindLossMonitor();
	notifierenabled = false;
	DestroyMiniMule();
	DestroyStartupProgress();

	CLifecycleProgressDlg shutdownProgress(IDS_SHUTTING_DOWN_EMULE, IDS_EMULE_IS_SHUTTING_DOWN, this);
	if (ShouldShowLifecycleProgressDialog(thePrefs.GetShutdownProgressDialogMode(), false) && shutdownProgress.Create(IDD_SHUTDOWNPROGRESS, this)) {
		m_bTransientDialogActive = true;
		shutdownProgress.CenterWindow();
		shutdownProgress.ShowWindow(SW_SHOW);
		shutdownProgress.SetPhase(2, _T("Closing eMuleBB"), _T("Preparing shutdown."), false);
		PumpLifecycleProgressMessages(&shutdownProgress);
	}

	const auto updateShutdownPhase = [&shutdownProgress](UINT uPercent, LPCTSTR pszStep, LPCTSTR pszDetail, bool bMarquee = false) {
		if (shutdownProgress.GetSafeHwnd() == NULL)
			return;
		shutdownProgress.SetPhase(uPercent, CString(pszStep), CString(pszDetail), bMarquee);
		PumpLifecycleProgressMessages(&shutdownProgress);
	};

	const auto sleepAndPumpSharedShutdownPoll = [&shutdownProgress](const SharedFileListSeams::SharedShutdownPollState &rState) {
		const DWORD dwSleepMs = SharedFileListSeams::GetSharedShutdownPollSleepMs(rState);
		if (dwSleepMs != 0)
			::Sleep(dwSleepMs);
		PumpLifecycleProgressMessages(&shutdownProgress);
	};

	theApp.m_app_state = APP_STATE_SHUTTINGDOWN;
	// WHY: The notification-area entry belongs to this HWND. Once shutdown is
	// committed, remove it before long teardown work or pumped messages can
	// leave Explorer with a stale icon after a clean close.
	TrayHide();
	VersionCheckLaunchSeams::ClearQueuedOnOwnerTeardown(m_pVersionCheckState);
	updateShutdownPhase(3, _T("Closing eMuleBB"), _T("Stopping AICH sync thread."), true);
	WaitForAICHSyncThreadShutdown();

	const bool bSharedHashingWasActiveOnClose = (theApp.sharedfiles != NULL && theApp.sharedfiles->HasSharedHashingWork());
	if (theApp.sharedfiles != NULL) {
		CString strHashLeaf;
		CString strHashPath;
		CString strHashDetail(GetResString(IDS_SHAREDHASHWAITING));
		bool bSharedHashShutdownTimedOut = false;
		if (theApp.sharedfiles->GetActiveSharedHashFile(strHashLeaf, strHashPath)) {
			strHashDetail.Format(GetResString(IDS_SHAREDHASHWAITINGFILE), (LPCTSTR)strHashLeaf);
			DebugLog(_T("Shutdown waiting for shared-file hashing: \"%s\""), (LPCTSTR)strHashPath);
		}
		updateShutdownPhase(4, _T("Closing eMuleBB"), strHashDetail, true);
		const ULONGLONG ullHashShutdownStartTick = ::GetTickCount64();
		ULONGLONG ullLastHashWaitUpdate = ullHashShutdownStartTick;
		CString strLastHashPath(strHashPath);
		while (!theApp.sharedfiles->ShutdownSharedHashWorkerStep(SharedFileListSeams::kSharedShutdownPollIntervalMs)) {
			const ULONGLONG ullNow = ::GetTickCount64();
			const SharedFileListSeams::SharedHashShutdownWaitState waitState = {
				(ullNow >= ullHashShutdownStartTick) ? (ullNow - ullHashShutdownStartTick) : 0ui64,
				SharedFileListSeams::kSharedHashShutdownWaitMs
			};
			if (!SharedFileListSeams::ShouldKeepWaitingForSharedHashWorkerShutdown(waitState)) {
				bSharedHashShutdownTimedOut = true;
				DebugLogError(_T("Timed out waiting %lu ms for shared-file hash worker shutdown; abandoning shared-file state for process exit."), SharedFileListSeams::kSharedHashShutdownWaitMs);
				if (!strHashPath.IsEmpty())
					DebugLogError(_T("Shared-file hash worker still active on \"%s\""), (LPCTSTR)strHashPath);
				updateShutdownPhase(4, _T("Closing eMuleBB"), _T("Shared-file hashing did not stop in time; abandoning shared-file cleanup for process exit."), true);
				theApp.sharedfiles = NULL;
				break;
			}
			if (ullNow >= ullLastHashWaitUpdate + 500) {
				strHashLeaf.Empty();
				strHashPath.Empty();
				strHashDetail = GetResString(IDS_SHAREDHASHWAITING);
				if (theApp.sharedfiles->GetActiveSharedHashFile(strHashLeaf, strHashPath)) {
					strHashDetail.Format(GetResString(IDS_SHAREDHASHWAITINGFILE), (LPCTSTR)strHashLeaf);
					if (strLastHashPath.CompareNoCase(strHashPath) != 0) {
						DebugLog(_T("Shutdown waiting for shared-file hashing: \"%s\""), (LPCTSTR)strHashPath);
						strLastHashPath = strHashPath;
					}
				}
				updateShutdownPhase(4, _T("Closing eMuleBB"), strHashDetail, true);
				ullLastHashWaitUpdate = ullNow;
			}
			sleepAndPumpSharedShutdownPoll({
				waitState.ullElapsedMs,
				waitState.ullWaitBudgetMs,
				SharedFileListSeams::kSharedShutdownPollIntervalMs
			});
		}
		if (bSharedHashShutdownTimedOut)
			updateShutdownPhase(6, _T("Closing eMuleBB"), _T("Continuing shutdown without shared-file cleanup after hash-worker timeout."));
	}

	//flush queued messages
	theApp.HandleDebugLogQueue();
	theApp.HandleLogQueue();

	updateShutdownPhase(6, _T("Closing eMuleBB"), _T("Disconnecting network services and revoking shell integration."));
	Log(_T("Closing eMuleBB"));
	CloseTTS();
	m_pDropTarget->Revoke();
	theApp.serverconnect->Disconnect();
	theApp.OnlineSig(); // Added By Bouc7

	// get main window placement
	WINDOWPLACEMENT wp = {};
	bool bHaveWindowPlacement = false;
	if (m_wpFirstRestore.length) {
		wp = m_wpFirstRestore;
		bHaveWindowPlacement = true;
	} else {
		wp.length = (UINT)sizeof wp;
		bHaveWindowPlacement = GetWindowPlacement(&wp) != FALSE;
	}
	if (bHaveWindowPlacement) {
		ASSERT(wp.showCmd == SW_SHOWMAXIMIZED || wp.showCmd == SW_SHOWMINIMIZED || wp.showCmd == SW_SHOWNORMAL);
		if (wp.showCmd == SW_SHOWMINIMIZED && (wp.flags & WPF_RESTORETOMAXIMIZED))
			wp.showCmd = SW_SHOWMAXIMIZED;
		wp.flags = 0;
		thePrefs.SetWindowLayout(wp);
	}

	// get active main window dialog
	if (activewnd) {
		if (activewnd->IsKindOf(RUNTIME_CLASS(CServerWnd)))
			thePrefs.SetLastMainWndDlgID(IDD_SERVER);
		else if (activewnd->IsKindOf(RUNTIME_CLASS(CSharedFilesWnd)))
			thePrefs.SetLastMainWndDlgID(IDD_FILES);
		else if (activewnd->IsKindOf(RUNTIME_CLASS(CSearchDlg)))
			thePrefs.SetLastMainWndDlgID(IDD_SEARCH);
		else if (activewnd->IsKindOf(RUNTIME_CLASS(CChatWnd)))
			thePrefs.SetLastMainWndDlgID(IDD_CHAT);
		else if (activewnd->IsKindOf(RUNTIME_CLASS(CTransferDlg)))
			thePrefs.SetLastMainWndDlgID(IDD_TRANSFER);
		else if (activewnd->IsKindOf(RUNTIME_CLASS(CStatisticsDlg)))
			thePrefs.SetLastMainWndDlgID(IDD_STATISTICS);
		else if (activewnd->IsKindOf(RUNTIME_CLASS(CKademliaWnd)))
			thePrefs.SetLastMainWndDlgID(IDD_KADEMLIAWND);
		else if (activewnd->IsKindOf(RUNTIME_CLASS(CIrcWnd)))
			thePrefs.SetLastMainWndDlgID(IDD_IRC);
		else {
			ASSERT(0);
			thePrefs.SetLastMainWndDlgID(0);
		}
	}

	updateShutdownPhase(18, _T("Closing eMuleBB"), _T("Starting background shared startup-cache save."));
	if (theApp.sharedfiles != NULL && SharedFileListSeams::ShouldPersistStartupCacheOnShutdown(bSharedHashingWasActiveOnClose))
		(void)theApp.sharedfiles->RequestStartupCacheSave(true);

	updateShutdownPhase(22, _T("Closing eMuleBB"), _T("Stopping Kad and waiting for the hashing thread to acknowledge shutdown."));
	Kademlia::CKademlia::Stop();	// couple of data files are written

	// try to wait until the hashing thread notices that we are shutting down
	CSingleLock sLock1(&theApp.hashing_mut); // only one file hash at a time
	sLock1.Lock(SEC2MS(2));

	updateShutdownPhase(30, _T("Closing eMuleBB"), _T("Stopping upload disk thread; keeping part-file writer alive for download teardown."));
	theApp.m_pUploadDiskIOThread->EndThread();

	// saving data & stuff
	updateShutdownPhase(40, _T("Closing eMuleBB"), _T("Finalizing completed part files before saving known-file state."), true);
	if (theApp.downloadqueue != NULL) {
		// WHY: completion workers move the finished file and delete .part.met
		// before posting TM_FILECOMPLETED back to the UI thread. Saving known.met
		// before those success results are applied can make durable metadata lag
		// behind the filesystem on shutdown, so drain the workers and pump their
		// posted finalization messages before known-file persistence starts.
		theApp.downloadqueue->DrainFileCompletionWorkersForShutdown();
		PumpLifecycleProgressMessages(&shutdownProgress);
	}

	updateShutdownPhase(42, _T("Closing eMuleBB"), _T("Saving known-file state and user interface settings."));
	theApp.emuledlg->preferenceswnd->m_wndSecurity.DeleteDDB();

	theApp.knownfiles->Save();
	if (theApp.sharedfiles != NULL) {
		if (SharedFileListSeams::ShouldPersistStartupCacheOnShutdown(bSharedHashingWasActiveOnClose)) {
			const ULONGLONG ullStartupCacheSaveShutdownStartTick = ::GetTickCount64();
			bool bStartupCacheSaveShutdownTimedOut = false;
			while (theApp.sharedfiles->HasPendingStartupCacheSaveWork()) {
				if (!theApp.sharedfiles->IsStartupCacheSaveRunning())
					(void)theApp.sharedfiles->RequestStartupCacheSave(true);

				CSharedFileList::StartupCacheSaveProgress progress = {};
				theApp.sharedfiles->GetStartupCacheSaveProgress(progress);
				updateShutdownPhase(54, _T("Closing eMuleBB"), FormatStartupCacheShutdownDetail(progress), true);
				const ULONGLONG ullNow = ::GetTickCount64();
				const SharedFileListSeams::StartupCacheSaveShutdownWaitState waitState = {
					(ullNow >= ullStartupCacheSaveShutdownStartTick) ? (ullNow - ullStartupCacheSaveShutdownStartTick) : 0ui64,
					SharedFileListSeams::kStartupCacheSaveShutdownWaitMs
				};
				if (!SharedFileListSeams::ShouldKeepWaitingForStartupCacheSaveShutdown(waitState)) {
					bStartupCacheSaveShutdownTimedOut = true;
					const bool bKeepSharedFilesAlive = theApp.sharedfiles->AbandonStartupCacheSaveForShutdown();
					DebugLogError(_T("Timed out waiting %lu ms for shared startup-cache save shutdown; abandoning startup-cache persistence state."), SharedFileListSeams::kStartupCacheSaveShutdownWaitMs);
					if (bKeepSharedFilesAlive) {
						DebugLogError(_T("Shared startup-cache save worker is still active; abandoning shared-file cleanup for process exit."));
						updateShutdownPhase(54, _T("Closing eMuleBB"), _T("Shared startup-cache save did not stop in time; abandoning shared-file cleanup for process exit."), true);
						theApp.sharedfiles = NULL;
					} else {
						updateShutdownPhase(54, _T("Closing eMuleBB"), _T("Shared startup-cache save did not finish in time; continuing shutdown without waiting for it."), false);
					}
					break;
				}
				sleepAndPumpSharedShutdownPoll({
					waitState.ullElapsedMs,
					waitState.ullWaitBudgetMs,
					SharedFileListSeams::kSharedShutdownPollIntervalMs
				});
			}
			if (bStartupCacheSaveShutdownTimedOut && theApp.sharedfiles == NULL)
				updateShutdownPhase(58, _T("Closing eMuleBB"), _T("Continuing shutdown without shared-file cleanup after startup-cache save timeout."));
		} else {
			updateShutdownPhase(54, _T("Closing eMuleBB"), _T("Skipping shared startup-cache save because shutdown interrupted hashing."), false);
		}
		if (theApp.sharedfiles != NULL) {
			if (bSharedHashingWasActiveOnClose)
				theApp.sharedfiles->PurgeInterruptedHashStartupCaches();
			updateShutdownPhase(62, _T("Closing eMuleBB"), _T("Saving shared file list configuration."));
			theApp.sharedfiles->Save();
		}
	}
	searchwnd->SaveAllSettings();
	serverwnd->SaveAllSettings();
	kademliawnd->SaveAllSettings();

	updateShutdownPhase(72, _T("Closing eMuleBB"), _T("Saving preferences and tearing down service integrations."));
	theApp.scheduler->RestoreOriginals();
	theApp.searchlist->SaveSpamFilter();
	FakeFileDetector::SaveCache();
	if (thePrefs.IsStoringSearchesEnabled())
		theApp.searchlist->StoreSearches();

	// close uPnP Ports
	theApp.m_pUPnPFinder->GetImplementation()->StopAsyncFind();
	if (thePrefs.CloseUPnPOnExit() && !theApp.m_pUPnPFinder->GetImplementation()->MustAbandonDiscoveryOwner())
		theApp.m_pUPnPFinder->GetImplementation()->DeletePorts();

	thePrefs.Save();
	thePerfLog.Shutdown();

	// explicitly delete all listview items which may hold ptrs to objects which will get deleted
	// by the dtors (some lines below) to avoid potential problems during application shutdown.
	updateShutdownPhase(82, _T("Closing eMuleBB"), _T("Clearing UI lists and pending conversion jobs."));
	const auto deleteAllListItemsIfLive = [](CListCtrl *pCtrl) {
		if (pCtrl != NULL && ::IsWindow(pCtrl->GetSafeHwnd()))
			pCtrl->DeleteAllItems();
	};
	deleteAllListItemsIfLive(transferwnd->GetDownloadList());
	chatwnd->chatselector.DeleteAllItems();
	deleteAllListItemsIfLive(&chatwnd->m_FriendListCtrl);
	theApp.clientlist->DeleteAll();
	searchwnd->DeleteAllSearchListCtrlItems();
	deleteAllListItemsIfLive(&sharedfileswnd->sharedfilesctrl);
	deleteAllListItemsIfLive(transferwnd->GetQueueList());
	deleteAllListItemsIfLive(transferwnd->GetClientList());
	deleteAllListItemsIfLive(transferwnd->GetUploadList());
	deleteAllListItemsIfLive(transferwnd->GetDownloadClientsList());
	deleteAllListItemsIfLive(&serverwnd->serverlistctrl);

	updateShutdownPhase(90, _T("Closing eMuleBB"), _T("Stopping bandwidth throttling and closing remaining child windows."));
	theApp.uploadBandwidthThrottler->EndThread();

	if (theApp.sharedfiles != NULL)
		theApp.sharedfiles->DeletePartFileInstances();

	searchwnd->SendMessage(WM_CLOSE);
	transferwnd->SendMessage(WM_CLOSE);

	// NOTE: Do not move those dtors into 'CemuleApp::InitInstance' (although they should be there). The
	// dtors are indirectly calling functions which access several windows which would not be available
	// after we have closed the main window -> crash!
	delete theApp.listensocket;				theApp.listensocket = NULL;
	delete theApp.clientudp;				theApp.clientudp = NULL;
	delete theApp.sharedfiles;				theApp.sharedfiles = NULL;
	delete theApp.serverconnect;			theApp.serverconnect = NULL;
	delete theApp.serverlist;				theApp.serverlist = NULL;		// CServerList::SaveServermetToFile
	delete theApp.knownfiles;				theApp.knownfiles = NULL;
	delete theApp.searchlist;				theApp.searchlist = NULL;
	delete theApp.clientcredits;			theApp.clientcredits = NULL;	// CClientCreditsList::SaveList
	delete theApp.downloadqueue;			theApp.downloadqueue = NULL;	// N * (CPartFile::FlushBuffer + CPartFile::SavePartFile)
	if (theApp.m_pPartFileWriteThread != NULL)
		theApp.m_pPartFileWriteThread->EndThread();
	delete theApp.uploadqueue;				theApp.uploadqueue = NULL;
	delete theApp.clientlist;				theApp.clientlist = NULL;
	delete theApp.friendlist;				theApp.friendlist = NULL;		// CFriendList::SaveList
	delete theApp.scheduler;				theApp.scheduler = NULL;
	delete theApp.ipfilterUpdater;			theApp.ipfilterUpdater = NULL;
	delete theApp.ipfilter;					theApp.ipfilter = NULL;			// CIPFilter::SaveToDefaultFile
	delete theApp.webserver;				theApp.webserver = NULL;
	delete theApp.geolocation;				theApp.geolocation = NULL;
	delete theApp.uploadBandwidthThrottler;	theApp.uploadBandwidthThrottler = NULL;
	delete theApp.m_pUPnPFinder;			theApp.m_pUPnPFinder = NULL;
	delete theApp.m_pUploadDiskIOThread;	theApp.m_pUploadDiskIOThread = NULL;
	delete theApp.m_pPartFileWriteThread;	theApp.m_pPartFileWriteThread = NULL;

	updateShutdownPhase(100, _T("Closing eMuleBB"), _T("Finalizing shutdown."));
	if (shutdownProgress.GetSafeHwnd() != NULL) {
		shutdownProgress.DestroyWindow();
		m_bTransientDialogActive = false;
	}
	// WHY: The tray icon can hold one of the dialog-owned HICON handles until a
	// NIM_DELETE reaches Explorer. Remove it while the main HWND and icons are
	// still valid so clean exits do not leave a stale shell icon behind.
	TrayHide();
	thePrefs.Uninit();
	theApp.m_app_state = APP_STATE_DONE;
	CTrayDialog::OnCancel();
	//flush queued messages
	theApp.HandleDebugLogQueue();
	theApp.HandleLogQueue();
	AddDebugLogLine(DLP_VERYLOW, _T("Closed eMuleBB"));
}

void CemuleDlg::OnTrayLButtonUp()
{
	if (theApp.IsClosing())
		return;

	// Avoid re-entrance problems with the main window, options dialog and MiniMule window.
	if (IsPreferencesDlgOpen()) {
		MessageBeep(MB_OK);
		preferenceswnd->SetForegroundWindow();
		preferenceswnd->BringWindowToTop();
		return;
	}

	if (m_pMiniMule != NULL) {
		if (m_pMiniMule->GetSafeHwnd() != NULL) {
			m_pMiniMule->ShowWindow(SW_SHOW);
			m_pMiniMule->SetForegroundWindow();
			m_pMiniMule->BringWindowToTop();
			m_pMiniMule->UpdateContent(m_uUpDatarate, m_uDownDatarate);
		}
		return;
	}

	if (thePrefs.GetEnableMiniMule()) {
		CMiniMuleDlg *pMiniMule = new CMiniMuleDlg(this);
		if (pMiniMule->Create(IDD_MINIMULE, this)) {
			m_pMiniMule = pMiniMule;
			m_pMiniMule->UpdateContent(m_uUpDatarate, m_uDownDatarate);
			m_pMiniMule->SetForegroundWindow();
			m_pMiniMule->BringWindowToTop();
		} else
			delete pMiniMule;
	}
}

void CemuleDlg::OnTrayRButtonUp(CPoint pt)
{
	if (theApp.m_app_state != APP_STATE_RUNNING)
		return;

	// Avoid re-entrance problems with the main window and options dialog.
	if (IsPreferencesDlgOpen()) {
		MessageBeep(MB_OK);
		preferenceswnd->SetForegroundWindow();
		preferenceswnd->BringWindowToTop();
		return;
	}

	ShowTrayToolPopup(pt);
}

void CemuleDlg::AddSpeedSelectorMenus(CMenu *addToMenu)
{
	const CString &kbyps(GetResString(IDS_KBYTESPERSEC));
	// Create UploadPopup Menu
	ASSERT(m_menuUploadCtrl.m_hMenu == NULL);
	CString text;
	if (m_menuUploadCtrl.CreateMenu()) {
		for (const SpeedQuickActionsSeams::CQuickSpeedPercentAction &action : SpeedQuickActionsSeams::kUploadPercentActions) {
			text.Format(
				GetResString(IDS_SPEED_LIMIT_UPLOAD_FMT),
				action.uPercent,
				SpeedQuickActionsSeams::CalculatePercentLimitKiB(thePrefs.GetConfiguredMaxUpload(), action.uPercent),
				(LPCTSTR)kbyps);
			m_menuUploadCtrl.AppendMenu(MF_STRING, action.uCommandId, text);
		}
		m_menuUploadCtrl.AppendMenu(MF_SEPARATOR);

		if (GetRecMaxUpload() > 0) {
			text.Format(GetResString(IDS_PW_MINREC) + GetResString(IDS_KBYTESPERSEC), GetRecMaxUpload());
			m_menuUploadCtrl.AppendMenu(MF_STRING, MP_QS_UP10, text);
		}

		text = GetResString(IDS_PW_UPL) + _T(':');
		addToMenu->AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_menuUploadCtrl.m_hMenu, text);
	}

	// Create DownloadPopup Menu
	ASSERT(m_menuDownloadCtrl.m_hMenu == NULL);
	if (m_menuDownloadCtrl.CreateMenu()) {
		for (const SpeedQuickActionsSeams::CQuickSpeedPercentAction &action : SpeedQuickActionsSeams::kDownloadPercentActions) {
			text.Format(
				GetResString(IDS_SPEED_LIMIT_DOWNLOAD_FMT),
				action.uPercent,
				SpeedQuickActionsSeams::CalculatePercentLimitKiB(thePrefs.GetConfiguredMaxDownload(), action.uPercent),
				(LPCTSTR)kbyps);
			m_menuDownloadCtrl.AppendMenu(MF_STRING, action.uCommandId, text);
		}

		text = GetResString(IDS_PW_DOWNL) + _T(':');
		addToMenu->AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_menuDownloadCtrl.m_hMenu, text);
	}

	addToMenu->AppendMenu(MF_SEPARATOR);
	addToMenu->AppendMenu(MF_STRING, MP_QS_PA, GetResString(IDS_SPEED_ACTION_ALL_TO_MIN));
	addToMenu->AppendMenu(MF_STRING, MP_QS_UA, GetResString(IDS_SPEED_ACTION_ALL_TO_MAX));
}

void CemuleDlg::StartConnection()
{
	if (!CanUseP2PConnectionCommands()) {
		LogP2PConnectionCommandBlocked();
		return;
	}

	if ((!theApp.serverconnect->IsConnecting() && !theApp.serverconnect->IsConnected()) || !Kademlia::CKademlia::IsRunning()) {
		// UPnP is still trying to open the ports. In order to not get a LowID by connecting to the servers / kad before
		// the ports are opened we delay the connection until UPnP gets a result or the timeout is reached
		// If the user clicks two times on the button, let him have his will and connect regardless
		m_bConnectRequestDelayedForUPnP = m_hUPnPTimeOutTimer != 0 && !m_bConnectRequestDelayedForUPnP;
		if (m_bConnectRequestDelayedForUPnP) {
			AddLogLine(false, GetResString(IDS_DELAYEDBYUPNP));
			AddLogLine(true, GetResString(IDS_DELAYEDBYUPNP2));
			return;
		}
		if (m_hUPnPTimeOutTimer != 0) {
			VERIFY(Win32CallbackTimerSeams::StopNullWindowCallbackTimer(m_hUPnPTimeOutTimer) != Win32CallbackTimerSeams::ETimerStopResult::Failed);
		}
		AddLogLine(true, GetResString(IDS_CONNECTING));

		// ed2k
		if ((thePrefs.GetNetworkED2K() || m_bEd2kSuspendDisconnect) && !theApp.serverconnect->IsConnecting() && !theApp.serverconnect->IsConnected())
			theApp.serverconnect->ConnectToAnyServer();

		// kad
		if ((thePrefs.GetNetworkKademlia() || m_bKadSuspendDisconnect) && !Kademlia::CKademlia::IsRunning())
			Kademlia::CKademlia::Start();

		ShowConnectionState();
	}
	m_bEd2kSuspendDisconnect = false;
	m_bKadSuspendDisconnect = false;
}

void CemuleDlg::CloseConnection()
{
	AddLogLine(false, GetResString(IDS_DISCONNECT_SOFT_STACK_NOTICE));
	theApp.serverconnect->StopConnectionTry();
	theApp.serverconnect->Disconnect();

	Kademlia::CKademlia::Stop();
	theApp.OnlineSig(); // Added By Bouc7
	ShowConnectionState();
}

void CemuleDlg::RestoreWindow()
{
	if (IsPreferencesDlgOpen()) {
		MessageBeep(MB_OK);
		preferenceswnd->SetForegroundWindow();
		preferenceswnd->BringWindowToTop();
		return;
	}

	DestroyMiniMule();
	if (m_wpFirstRestore.length) {
		SetWindowPlacement(&m_wpFirstRestore);
		memset(&m_wpFirstRestore, 0, sizeof m_wpFirstRestore);
		SetForegroundWindow();
		BringWindowToTop();
	} else
		CTrayDialog::RestoreWindow();
	UpdateTrayVisibility();
}

void CemuleDlg::DestroyMiniMule()
{
	if (m_pMiniMule == NULL)
		return;

	CMiniMuleDlg *pMiniMule = m_pMiniMule;
	m_pMiniMule = NULL;
	if (pMiniMule->GetSafeHwnd() != NULL)
		pMiniMule->DestroyWindow();
	else
		delete pMiniMule;
}

void CemuleDlg::OnMiniMuleDestroyed(CMiniMuleDlg *pMiniMule)
{
	if (m_pMiniMule == pMiniMule)
		m_pMiniMule = NULL;
}

void CemuleDlg::UpdateTrayIcon(int iPercent)
{
	// compute an id of the icon to be generated
	UINT uSysTrayIconCookie = (iPercent > 0) ? (16 - ((iPercent * 15 / 100) + 1)) : 0;
	if (theApp.IsConnected()) {
		if (!theApp.IsFirewalled())
			uSysTrayIconCookie += 50;
	} else
		uSysTrayIconCookie += 100;

	// don't update if the same icon as displayed would be generated
	if (m_uLastSysTrayIconCookie == uSysTrayIconCookie)
		return;
	m_uLastSysTrayIconCookie = uSysTrayIconCookie;

	// prepare it up
	if (m_iMsgIcon != 0 && thePrefs.DoFlashOnNewMessage()) {
		m_bMsgBlinkState = !m_bMsgBlinkState;

		if (m_bMsgBlinkState)
			m_TrayIcon.Init(imicons[1], 100, 1, 1, 16, 16, thePrefs.GetStatsColor(11));
	} else
		m_bMsgBlinkState = false;

	if (!m_bMsgBlinkState) {
		HICON trayicon;
		if (theApp.IsConnected())
			trayicon = theApp.IsFirewalled() ? m_icoSysTrayLowID : m_icoSysTrayConnected;
		else
			trayicon = m_icoSysTrayDisconnected;
		m_TrayIcon.Init(trayicon, 100, 1, 1, 16, 16, thePrefs.GetStatsColor(11));
	}

	// load our limit and color info
	static const int aiLimits[1] = {100}; // set the limits of where the bar color changes (low-high)
	COLORREF aColors[1] = {thePrefs.GetStatsColor(11)}; // set the corresponding color for each level
	m_TrayIcon.SetColorLevels(aiLimits, aColors, _countof(aiLimits));

	// generate the icon (do *not* destroy that icon using DestroyIcon(), that's done in 'TrayUpdate')
	int aiVals[1] = {iPercent};
	m_icoSysTrayCurrent = m_TrayIcon.Create(aiVals);
	ASSERT(m_icoSysTrayCurrent != NULL);
	if (m_icoSysTrayCurrent)
		TraySetIcon(m_icoSysTrayCurrent, true);
	TrayUpdate();
}

int CemuleDlg::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	return CTrayDialog::OnCreate(lpCreateStruct);
}

void CemuleDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	if (!theApp.IsClosing()) {
		ShowTransferRate(true);

		if (bShow && activewnd == chatwnd)
			chatwnd->chatselector.ShowChat();
		if (transferwnd != NULL)
			transferwnd->RefreshTransferDisplayRefreshState(bShow != FALSE);
	}
	CTrayDialog::OnShowWindow(bShow, nStatus);
}

bool CemuleDlg::ShouldTrayIconBeVisible()
{
	TrayNotificationSeams::CTrayVisibilityState state;
	state.bAlwaysShowTrayIcon = thePrefs.IsAlwaysShowTrayIcon();
	state.eNotifierDisplayMode = MapTrayNotifierDisplayMode(thePrefs.GetNotifierDisplayMode());
	state.bTrayBalloonFallbackForSession = m_bTrayBalloonFallbackForSession;
	state.bMainWindowVisible = IsWindowVisible() != FALSE;
	state.bMinimizeToTray = thePrefs.GetMinToTray();
	return TrayNotificationSeams::ShouldTrayIconBeVisible(state);
}

void CemuleDlg::UpdateTrayVisibility()
{
	if (ShouldTrayIconBeVisible()) {
		if (!TrayIconVisible()) {
			ShowTransferRate(true);
			TrayShow();
			ShowTransferRate(true);
		}
	} else if (TrayIconVisible())
		TrayHide();
}

void CemuleDlg::ForceTrayBalloonFallbackForSession()
{
	m_bTrayBalloonFallbackForSession = true;
	UpdateTrayVisibility();
}

void CemuleDlg::ShowNotifier(LPCTSTR pszText, TbnMsg nMsgType, LPCTSTR pszLink, bool bForceSoundOFF)
{
	if (!notifierenabled)
		return;

	LPCTSTR pszSoundEvent = NULL;
	int iSoundPrio = 0;
	bool bShowIt = false;
	switch (nMsgType) {
	case TBN_CHAT:
		if (thePrefs.GetNotifierOnChat()) {
			ShowNotificationPopup(pszText, nMsgType, pszLink);
			bShowIt = true;
			pszSoundEvent = _T("eMule_Chat");
			iSoundPrio = 1;
		}
		break;
	case TBN_DOWNLOADFINISHED:
		if (thePrefs.GetNotifierOnDownloadFinished()) {
			ShowNotificationPopup(pszText, nMsgType, pszLink);
			bShowIt = true;
			pszSoundEvent = _T("eMule_DownloadFinished");
			iSoundPrio = 1;
			SendNotificationMail(nMsgType, pszText);
		}
		break;
	case TBN_DOWNLOADADDED:
		if (thePrefs.GetNotifierOnNewDownload()) {
			ShowNotificationPopup(pszText, nMsgType, pszLink);
			bShowIt = true;
			pszSoundEvent = _T("eMule_DownloadAdded");
			iSoundPrio = 1;
		}
		break;
	case TBN_LOG:
		if (thePrefs.GetNotifierOnLog()) {
			ShowNotificationPopup(pszText, nMsgType, pszLink);
			bShowIt = true;
			pszSoundEvent = _T("eMule_LogEntryAdded");
		}
		break;
	case TBN_IMPORTANTEVENT:
		if (thePrefs.GetNotifierOnImportantError()) {
			ShowNotificationPopup(pszText, nMsgType, pszLink);
			bShowIt = true;
			pszSoundEvent = _T("eMule_Urgent");
			iSoundPrio = 1;
			SendNotificationMail(nMsgType, pszText);
		}
		break;
	case TBN_NEWVERSION:
		if (thePrefs.GetNotifierOnNewVersion()) {
			ShowNotificationPopup(pszText, nMsgType, pszLink);
			bShowIt = true;
			pszSoundEvent = _T("eMule_NewVersion");
			iSoundPrio = 1;
		}
		break;
	case TBN_NULL:
		ShowNotificationPopup(pszText, nMsgType, pszLink);
		bShowIt = true;
	}

	if (bShowIt && !bForceSoundOFF && thePrefs.GetNotifierSoundType() != ntfstNoSound) {
		bool bNotifiedWithAudio = false;
		if (thePrefs.GetNotifierSoundType() == ntfstSpeech)
			bNotifiedWithAudio = Speak(pszText);

		if (!bNotifiedWithAudio) {
			if (!thePrefs.GetNotifierSoundFile().IsEmpty())
				PlaySound(thePrefs.GetNotifierSoundFile(), NULL, SND_FILENAME | SND_NOSTOP | SND_NOWAIT | SND_ASYNC);
			else if (pszSoundEvent) {
				// use 'SND_NOSTOP' only for low priority events, otherwise the 'Log message' event may overrule
				// a more important event which is fired nearly at the same time.
				PlaySound(pszSoundEvent, NULL, SND_APPLICATION | SND_ASYNC | SND_NODEFAULT | SND_NOWAIT | ((iSoundPrio > 0) ? 0 : SND_NOSTOP));
			}
		}
	}
}

void CemuleDlg::ShowNotificationPopup(LPCTSTR pszText, TbnMsg nMsgType, LPCTSTR pszLink)
{
	switch (thePrefs.GetNotifierDisplayMode()) {
	case ntfdmWindowsToast:
		if (m_wndWindowsToastNotifier.Show(m_hWnd, pszText, nMsgType, pszLink))
			return;
		ForceTrayBalloonFallbackForSession();
		if (ShowTrayBalloonNotification(pszText, nMsgType))
			return;
		break;
	case ntfdmTrayBalloon:
		UpdateTrayVisibility();
		if (ShowTrayBalloonNotification(pszText, nMsgType))
			return;
		break;
	default:
		break;
	}

	m_wndTaskbarNotifier.Show(pszText, nMsgType, pszLink);
}

bool CemuleDlg::ShowTrayBalloonNotification(LPCTSTR pszText, TbnMsg nMsgType)
{
	if (!TrayIconVisible())
		UpdateTrayVisibility();
	if (!TrayIconVisible())
		return false;

	CString strTitle;
	CString strBody;
	SplitNotifierText(pszText, nMsgType, strTitle, strBody);
	return TrayShowBalloon(strTitle, strBody, GetTrayBalloonInfoFlags(nMsgType));
}

void CemuleDlg::LoadNotifier(const CString &configuration)
{
	notifierenabled = m_wndTaskbarNotifier.LoadConfiguration(configuration);
}

LRESULT CemuleDlg::OnTaskbarNotifierClicked(WPARAM, LPARAM lParam)
{
	HandleNotifierClicked(static_cast<TbnMsg>(m_wndTaskbarNotifier.GetMessageType()), lParam);
	return 0;
}

LRESULT CemuleDlg::OnWindowsToastClicked(WPARAM wParam, LPARAM lParam)
{
	HandleNotifierClicked(static_cast<TbnMsg>(wParam), lParam);
	return 0;
}

void CemuleDlg::OnTrayBalloonUserClick()
{
	RestoreWindow();
}

void CemuleDlg::HandleNotifierClicked(TbnMsg nMsgType, LPARAM lParam)
{
	if (lParam) {
		ShellDefaultVerb((LPTSTR)lParam);
		free((void*)lParam);
	}

	switch (nMsgType) {
	case TBN_CHAT:
		RestoreWindow();
		SetActiveDialog(chatwnd);
		break;
	case TBN_DOWNLOADFINISHED:
		// if we had a link and opened the downloaded file, don't restore the app window
		if (lParam == 0) {
			RestoreWindow();
			SetActiveDialog(transferwnd);
		}
		break;
	case TBN_DOWNLOADADDED:
		RestoreWindow();
		SetActiveDialog(transferwnd);
		break;
	case TBN_IMPORTANTEVENT:
	case TBN_LOG:
		RestoreWindow();
		SetActiveDialog(serverwnd);
		break;
	case TBN_NEWVERSION:
		BrowserOpen(thePrefs.GetVersionCheckURL(), thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
	}
}

void CemuleDlg::OnSettingChange(UINT uFlags, LPCTSTR lpszSection)
{
	TRACE(_T("CemuleDlg::OnSettingChange: uFlags=0x%08x  lpszSection=\"%s\"\n"), lpszSection);
	// Do not update the Shell's large icon size, because we still have an image list
	// from the shell which contains the old large icon size.
	//theApp.UpdateLargeIconSize();
	theApp.UpdateDesktopColorDepth();
	CTrayDialog::OnSettingChange(uFlags, lpszSection);
}

void CemuleDlg::OnSysColorChange()
{
	theApp.UpdateDesktopColorDepth();
	CTrayDialog::OnSysColorChange();
	SetAllIcons();
}

HBRUSH CemuleDlg::OnCtlColor(CDC *pDC, CWnd *pWnd, UINT nCtlColor)
{
	HBRUSH hbr = GetCtlColor(pDC, pWnd, nCtlColor);
	return hbr ? hbr : __super::OnCtlColor(pDC, pWnd, nCtlColor);
}

HBRUSH CemuleDlg::GetCtlColor(CDC* /*pDC*/, CWnd* /*pWnd*/, UINT /*nCtlColor*/)
{
	// This function could have been used to give the entire eMule (at least all of the main windows)
	// a somewhat more Vista like look by giving them all a bright background color.
	// However, again, the owner drawn tab controls are noticeably disturbing that attempt. They
	// do not change their background color accordingly. They don't use NMCUSTOMDRAW nor to they
	// use WM_CTLCOLOR...
	//
	//if (theApp.IsModernThemedControlsActive() && (nCtlColor == CTLCOLOR_DLG || nCtlColor == CTLCOLOR_STATIC))
	//	return ::GetSysColorBrush(COLOR_WINDOW);
	return NULL;
}

void CemuleDlg::SetAllIcons()
{
	// application icon (although it's not customizable, we may need to load a different color resolution)
	if (m_hIcon)
		VERIFY(::DestroyIcon(m_hIcon));
	if (m_hLowIDIcon)
		VERIFY(::DestroyIcon(m_hLowIDIcon));
	// NOTE: the application icon name is prefixed with "AAA" to make sure it's alphabetically sorted by the
	// resource compiler as the 1st icon in the resource table!
	m_hIcon = AfxGetApp()->LoadIcon(_T("AAAEMULEAPP"));
	m_hLowIDIcon = AfxGetApp()->LoadIcon(_T("APPLOWID"));
	m_eMainConnectionIcon = AppMainIconSeams::EConnectionIcon::Unknown;
	ShowMainConnectionStateIcon();

	// connection state
	DestroyIconsArr(m_connicons, _countof(m_connicons));
	m_connicons[0] = theApp.LoadIcon(_T("ConnectedNotNot"), 16, 16);
	m_connicons[1] = theApp.LoadIcon(_T("ConnectedNotLow"), 16, 16);
	m_connicons[2] = theApp.LoadIcon(_T("ConnectedNotHigh"), 16, 16);
	m_connicons[3] = theApp.LoadIcon(_T("ConnectedLowNot"), 16, 16);
	m_connicons[4] = theApp.LoadIcon(_T("ConnectedLowLow"), 16, 16);
	m_connicons[5] = theApp.LoadIcon(_T("ConnectedLowHigh"), 16, 16);
	m_connicons[6] = theApp.LoadIcon(_T("ConnectedHighNot"), 16, 16);
	m_connicons[7] = theApp.LoadIcon(_T("ConnectedHighLow"), 16, 16);
	m_connicons[8] = theApp.LoadIcon(_T("ConnectedHighHigh"), 16, 16);
	ShowConnectionStateIcon();

	// transfer state
	DestroyIconsArr(transicons, _countof(transicons));
	transicons[0] = theApp.LoadIcon(_T("UP0DOWN0"), 16, 16);
	transicons[1] = theApp.LoadIcon(_T("UP0DOWN1"), 16, 16);
	transicons[2] = theApp.LoadIcon(_T("UP1DOWN0"), 16, 16);
	transicons[3] = theApp.LoadIcon(_T("UP1DOWN1"), 16, 16);
	ShowTransferStateIcon();

	// users state
	if (usericon)
		VERIFY(::DestroyIcon(usericon));
	usericon = theApp.LoadIcon(_T("StatsClients"), 16, 16);
	ShowUserStateIcon();

	// system tray icons
	if (m_icoSysTrayConnected)
		VERIFY(::DestroyIcon(m_icoSysTrayConnected));
	if (m_icoSysTrayDisconnected)
		VERIFY(::DestroyIcon(m_icoSysTrayDisconnected));
	if (m_icoSysTrayLowID)
		VERIFY(::DestroyIcon(m_icoSysTrayLowID));
	m_icoSysTrayConnected = theApp.LoadIcon(_T("TrayConnected"), 16, 16);
	m_icoSysTrayDisconnected = theApp.LoadIcon(_T("TrayNotConnected"), 16, 16);
	m_icoSysTrayLowID = theApp.LoadIcon(_T("TrayLowID"), 16, 16);
	ShowTransferRate(true);

	DestroyIconsArr(imicons, _countof(imicons));
	imicons[0] = NULL;
	imicons[1] = theApp.LoadIcon(_T("Message"), 16, 16);
	imicons[2] = theApp.LoadIcon(_T("MessagePending"), 16, 16);
	ShowMessageState(m_iMsgIcon);
}

void CemuleDlg::Localize()
{
	CMenu *pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu) {
		VERIFY(pSysMenu->ModifyMenu(MP_ABOUTBOX, MF_BYCOMMAND | MF_STRING, MP_ABOUTBOX, GetResString(IDS_ABOUTBOX)));
		VERIFY(pSysMenu->ModifyMenu(MP_VERSIONCHECK, MF_BYCOMMAND | MF_STRING, MP_VERSIONCHECK, GetResString(IDS_VERSIONCHECK)));

		// localize the 'speed control' sub menus by deleting the current menus and creating a new ones.

		// remove any already available 'speed control' menus from system menu
		UINT uOptMenuPos = pSysMenu->GetMenuItemCount() - 1;
		CMenu *pAccelMenu = pSysMenu->GetSubMenu(uOptMenuPos);
		if (pAccelMenu) {
			ASSERT(pAccelMenu->m_hMenu == m_SysMenuOptions.m_hMenu);
			VERIFY(pSysMenu->RemoveMenu(uOptMenuPos, MF_BYPOSITION));
		}

		// destroy all 'speed control' menus
		if (m_menuUploadCtrl)
			VERIFY(m_menuUploadCtrl.DestroyMenu());
		if (m_menuDownloadCtrl)
			VERIFY(m_menuDownloadCtrl.DestroyMenu());
		if (m_SysMenuOptions)
			VERIFY(m_SysMenuOptions.DestroyMenu());

		// create new 'speed control' menus
		if (m_SysMenuOptions.CreateMenu()) {
			AddSpeedSelectorMenus(&m_SysMenuOptions);
			pSysMenu->AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_SysMenuOptions.m_hMenu, GetResString(IDS_EM_PREFS));
		}
	}

	ShowUserStateIcon();
	toolbar->Localize();
	ShowConnectionState();
	ShowTransferRate(true);
	if (m_pMiniMule != NULL && m_pMiniMule->GetSafeHwnd() != NULL) {
		m_pMiniMule->Localize();
		m_pMiniMule->UpdateContent(m_uUpDatarate, m_uDownDatarate);
	}
	ShowUserCount();
}

void CemuleDlg::ShowUserStateIcon()
{
	statusbar->SetIcon(SBarUsers, usericon);
}

void CemuleDlg::QuickSpeedOther(UINT nID)
{
	if (nID == MP_QS_PA) {
		thePrefs.SetSessionMaxUpload(1);
		thePrefs.SetSessionMaxDownload(1);
		AddLogLine(false, _T("Temporary session upload and download limits set to minimum."));
	} else if (nID == MP_QS_UA) {
		thePrefs.ClearSessionMaxLimits();
		AddLogLine(false, _T("Temporary session speed limits cleared; configured limits restored."));
	}
}


void CemuleDlg::QuickSpeedUpload(UINT nID)
{
	const unsigned int uPercent = SpeedQuickActionsSeams::GetPercentForCommand(nID);
	if (uPercent != 0) {
		const uint32 uLimit = SpeedQuickActionsSeams::CalculatePercentLimitKiB(thePrefs.GetConfiguredMaxUpload(), uPercent);
		const CString strKiBps(GetResString(IDS_KBYTESPERSEC));
		thePrefs.SetSessionMaxUpload(uLimit);
		AddLogLine(false, _T("Temporary session upload limit set to %u%% (%u %s)."), uPercent, uLimit, (LPCTSTR)strKiBps);
		return;
	}

	switch (nID) {
	case MP_QS_U100:
		thePrefs.ClearSessionMaxUpload();
		AddLogLine(false, _T("Temporary session upload limit cleared; configured upload limit restored."));
		return;
	case MP_QS_UPC:
	default:
		return;
	case MP_QS_UP10:
		{
			const uint32 uLimit = GetRecMaxUpload();
			if (uLimit == 0)
				return;
			const CString strKiBps(GetResString(IDS_KBYTESPERSEC));
			thePrefs.SetSessionMaxUpload(uLimit);
			AddLogLine(false, _T("Temporary session upload limit set to recommended minimum (%u %s)."), uLimit, (LPCTSTR)strKiBps);
		}
		return;
	}
}

void CemuleDlg::QuickSpeedDownload(UINT nID)
{
	const unsigned int uPercent = SpeedQuickActionsSeams::GetPercentForCommand(nID);
	if (uPercent != 0) {
		const uint32 uLimit = SpeedQuickActionsSeams::CalculatePercentLimitKiB(thePrefs.GetConfiguredMaxDownload(), uPercent);
		const CString strKiBps(GetResString(IDS_KBYTESPERSEC));
		thePrefs.SetSessionMaxDownload(uLimit);
		AddLogLine(false, _T("Temporary session download limit set to %u%% (%u %s)."), uPercent, uLimit, (LPCTSTR)strKiBps);
		return;
	}

	switch (nID) {
	case MP_QS_D100:
		thePrefs.ClearSessionMaxDownload();
		AddLogLine(false, _T("Temporary session download limit cleared; configured download limit restored."));
		return;
	case MP_QS_DC:
//		thePrefs.SetMaxDownload(UNLIMITED);
	default:
		return;
	}
}

void CemuleDlg::QuickSpeedBoth(UINT nID)
{
	const unsigned int uPercent = SpeedQuickActionsSeams::GetPercentForCommand(nID);
	if (uPercent == 0)
		return;

	const uint32 uUploadLimit = SpeedQuickActionsSeams::CalculatePercentLimitKiB(thePrefs.GetConfiguredMaxUpload(), uPercent);
	const uint32 uDownloadLimit = SpeedQuickActionsSeams::CalculatePercentLimitKiB(thePrefs.GetConfiguredMaxDownload(), uPercent);
	const CString strKiBps(GetResString(IDS_KBYTESPERSEC));
	thePrefs.SetSessionMaxUpload(uUploadLimit);
	thePrefs.SetSessionMaxDownload(uDownloadLimit);
	AddLogLine(
		false,
		_T("Temporary session upload and download limits set to %u%% (up %u %s, down %u %s)."),
		uPercent,
		uUploadLimit,
		(LPCTSTR)strKiBps,
		uDownloadLimit,
		(LPCTSTR)strKiBps);
}

// quick-speed changer -- based on xrmb
int CemuleDlg::GetRecMaxUpload()
{
	int rate = thePrefs.GetConfiguredMaxUpload();
	if (rate < 7)
		return 0;
	if (rate < 15)
		return rate - 3;
	return rate - 4;
}

BOOL CemuleDlg::OnCommand(WPARAM wParam, LPARAM lParam)
{
	switch (wParam) {
	case TBBTN_CONNECT:
	case MP_HM_CON:
		OnBnClickedConnect();
		break;
	case TBBTN_KAD:
	case MP_HM_KAD:
		SetActiveDialog(kademliawnd);
		break;
	case TBBTN_SERVER:
	case MP_HM_SRVR:
		SetActiveDialog(serverwnd);
		break;
	case TBBTN_TRANSFERS:
	case MP_HM_TRANSFER:
		SetActiveDialog(transferwnd);
		break;
	case TBBTN_SEARCH:
	case MP_HM_SEARCH:
		SetActiveDialog(searchwnd);
		break;
	case TBBTN_SHARED:
	case MP_HM_FILES:
		SetActiveDialog(sharedfileswnd);
		break;
	case TBBTN_MESSAGES:
	case MP_HM_MSGS:
		SetActiveDialog(chatwnd);
		break;
	case TBBTN_IRC:
	case MP_HM_IRC:
		SetActiveDialog(ircwnd);
		break;
	case TBBTN_STATS:
	case MP_HM_STATS:
		SetActiveDialog(statisticswnd);
		break;
	case TBBTN_OPTIONS:
	case MP_HM_PREFS:
		toolbar->CheckButton(TBBTN_OPTIONS, TRUE);
		ShowPreferences();
		toolbar->CheckButton(TBBTN_OPTIONS, FALSE);
		break;
	case TBBTN_TOOLS:
		ShowToolPopup(true);
		break;
	case MP_SELECTTOOLBARBITMAPDIR:
	case MP_SELECTTOOLBARBITMAP:
	case MP_CUSTOMIZETOOLBAR:
	case MP_LARGEICONS:
	case MP_SMALLICONS:
	case MP_NOTEXTLABELS:
	case MP_TEXTLABELS:
	case MP_TEXTLABELSONRIGHT:
	case MP_SELECT_SKIN_DIR:
	case MP_SELECT_SKIN_FILE:
	case MP_HM_RESET_DISPLAY:
		if (toolbar != NULL)
			toolbar->ExecuteCommand(wParam);
		break;
	case MP_MINIMIZETOTRAY:
		SendMessage(WM_SYSCOMMAND, MP_MINIMIZETOTRAY, 0);
		break;
	case MP_HM_OPENINC:
		ShellOpenFile(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
		break;
	case MP_HM_OPEN_TEMPDIR:
		ShellOpenFile(thePrefs.GetMuleDirectory(EMULE_TEMPDIR));
		break;
	case MP_HM_OPENCONFIGDIR:
		ShellOpenFile(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
		break;
	case MP_HM_OPENLOGDIR:
		ShellOpenFile(thePrefs.GetMuleDirectory(EMULE_LOGDIR));
		break;
	case MP_HM_OPEN_WEBSERVERDIR:
		ShellOpenFile(thePrefs.GetMuleDirectory(EMULE_WEBSERVERDIR));
		break;
	case MP_HM_OPEN_SKINDIR:
		ShellOpenFile(thePrefs.GetMuleDirectory(EMULE_SKINDIR));
		break;
	case MP_HM_OPEN_TOOLBARDIR:
		ShellOpenFile(thePrefs.GetMuleDirectory(EMULE_TOOLBARDIR));
		break;
	case MP_HM_OPEN_EXECUTABLEDIR:
		ShellOpenFile(thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
		break;
	case MP_HM_EDIT_PREFERENCES_INI:
		EditTextFile(GetConfigFilePath(_T("preferences.ini")));
		break;
	case MP_HM_EDIT_IPFILTER_DAT:
		EditTextFile(CIPFilter::GetDefaultFilePath());
		break;
	case MP_HM_RELOAD_IPFILTER_DAT:
		{
			CWaitCursor curHourglass;
			theApp.ipfilter->LoadFromDefaultFile();
			if (thePrefs.GetFilterServerByIP())
				serverwnd->serverlistctrl.RemoveAllFilteredServers();
		}
		break;
	case MP_HM_EDIT_FAKEFILEFILTER_DAT:
		EditTextFile(FakeFileDetector::GetRuleFilePath());
		break;
	case MP_HM_EDIT_ADDRESSES_DAT:
		EditTextFile(GetConfigFilePath(_T("addresses.dat")));
		break;
	case MP_HM_EDIT_WEBSERVICES_DAT:
		EditTextFile(theWebServices.GetDefaultServicesFile());
		break;
	case MP_HM_EDIT_STATIC_SERVERS_DAT:
		EditTextFile(GetConfigFilePath(_T("staticservers.dat")));
		break;
	case MP_HM_EDIT_SHAREDDIR_DAT:
		EditTextFile(GetConfigFilePath(_T("shareddir.dat")));
		break;
	case MP_HM_EDIT_MONITORED_SHAREDDIR_DAT:
		EditTextFile(GetConfigFilePath(_T("shareddir.monitored.dat")));
		break;
	case MP_HM_EDIT_MONITOR_OWNED_SHAREDDIR_DAT:
		EditTextFile(GetConfigFilePath(_T("shareddir.monitor-owned.dat")));
		break;
	case MP_HM_EDIT_SHAREIGNORE_DAT:
		EditTextFile(GetConfigFilePath(_T("shareignore.dat")));
		break;
	case MP_HM_MANAGE_CATEGORIES:
		if (transferwnd != NULL)
			transferwnd->ManageCategoriesInteractive();
		break;
	case MP_HM_EDIT_CATEGORY_INI:
		EditTextFile(GetConfigFilePath(_T("Category.ini")));
		break;
	case MP_HM_EDIT_NOTIFIER_INI:
		EditTextFile(GetConfigFilePath(_T("Notifier.ini")));
		break;
	case MP_HM_EDIT_FILEINFO_INI:
		EditTextFile(thePrefs.GetFileCommentsFilePath());
		break;
	case MP_HM_EDIT_STATISTICS_INI:
		EditTextFile(GetConfigFilePath(_T("statistics.ini")));
		break;
	case MP_HM_CAPTURE_MINIDUMP:
		CaptureDiagnosticDump(false);
		break;
	case MP_HM_CAPTURE_FULLDUMP:
		CaptureDiagnosticDump(true);
		break;
	case MP_HM_COPY_DIAGNOSTIC_SNAPSHOT_JSON:
		CopyDiagnosticSnapshotToClipboard(false);
		break;
	case MP_HM_COPY_REDACTED_DIAGNOSTIC_SNAPSHOT_JSON:
		CopyDiagnosticSnapshotToClipboard(true);
		break;
	case MP_HM_REPAIR_WINDOWS_FIREWALL:
		RepairWindowsFirewallRules();
		break;
	case MP_HM_ENABLE_WINDOWS_LONG_PATHS:
		EnableWindowsLongPaths();
		break;
	case MP_HM_DEFENDER_EXCLUDE_DOWNLOAD_FOLDERS:
		ExcludeDownloadFoldersFromDefender();
		break;
	case MP_HM_REGISTER_PROWLARR:
		RegisterProwlarrIntegration();
		break;
	case MP_HM_REGISTER_RADARR:
		RegisterArrIntegration(_T("Radarr"));
		break;
	case MP_HM_REGISTER_SONARR:
		RegisterArrIntegration(_T("Sonarr"));
		break;
	case MP_HM_RESUME_ALL_DOWNLOADS:
	case MP_HM_PAUSE_ALL_DOWNLOADS:
	case MP_HM_STOP_ALL_DOWNLOADS:
		if (theApp.downloadqueue != NULL) {
			const int iNewStatus = wParam == MP_HM_RESUME_ALL_DOWNLOADS ? MP_RESUME : (wParam == MP_HM_PAUSE_ALL_DOWNLOADS ? MP_PAUSE : MP_STOP);
			theApp.downloadqueue->SetCatStatus((UINT)-1, iNewStatus);
			if (transferwnd != NULL) {
				transferwnd->UpdateCatTabTitles();
				if (transferwnd->GetDownloadList() != NULL) {
					transferwnd->GetDownloadList()->UpdateCurrentCategoryView();
					transferwnd->GetDownloadList()->MarkAvailableCommandsDirty();
					transferwnd->GetDownloadList()->Invalidate();
				}
			}
		}
		break;
	case MP_HM_CLEAR_COMPLETED_DOWNLOADS:
		if (transferwnd != NULL && transferwnd->GetDownloadList() != NULL) {
			transferwnd->GetDownloadList()->ClearCompleted(-1);
			transferwnd->GetDownloadList()->MarkAvailableCommandsDirty();
		}
		break;
	case MP_HM_VIEW_PRESET_STOCK_KEEP_WIDTHS:
	case MP_HM_VIEW_PRESET_STOCK_RESET_WIDTHS:
	case MP_HM_VIEW_PRESET_EXTENDED_KEEP_WIDTHS:
	case MP_HM_VIEW_PRESET_EXTENDED_RESET_WIDTHS:
	case MP_HM_VIEW_PRESET_FULL_KEEP_WIDTHS:
	case MP_HM_VIEW_PRESET_FULL_RESET_WIDTHS:
		ApplyViewPresetCommand((UINT)wParam);
		break;
	case MP_HM_OPEN_EMULE_LOG:
		ShellOpenFile(theLog.GetFilePath());
		break;
	case MP_HM_OPEN_VERBOSE_LOG:
		ShellOpenFile(theVerboseLog.GetFilePath());
		break;
	case TBBTN_HELP:
	case MP_HM_HELP:
		if (activewnd != NULL) {
			HELPINFO hi = {};
			hi.cbSize = (UINT)sizeof(HELPINFO);
			activewnd->SendMessage(WM_HELP, 0, (LPARAM)&hi);
		} else
			wParam = ID_HELP;
		break;
	case MP_HM_EXIT:
		OnClose();
		break;
	case MP_HM_RESTART_APP:
		RestartApp();
		break;
	case MP_HM_LINK1: // MOD: don't remove!
		BrowserOpen(thePrefs.GetHomepageBaseURL(), thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
		break;
	case MP_HM_LINK2:
		BrowserOpen(thePrefs.GetOnlineHelpURL(), thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
		break;
	case MP_HM_LINK3:
		BrowserOpen(thePrefs.GetVersionCheckURL(), thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
		break;
	case MP_HM_LINK_DOC_FAQ:
	case MP_HM_LINK_DOC_SETUP:
	case MP_HM_LINK_DOC_NETWORK:
	case MP_HM_LINK_DOC_SHARING:
	case MP_HM_LINK_DOC_DOWNLOADS_SEARCH:
	case MP_HM_LINK_DOC_TOOLS_MENU:
	case MP_HM_LINK_DOC_CONTROLLERS_REST:
	case MP_HM_LINK_DOC_TROUBLESHOOTING:
		BrowserOpen(GetDocumentationLinkURL((UINT)wParam), thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
		break;
	case MP_WEBSVC_EDIT:
		theWebServices.Edit();
		break;
	case MP_HM_SCHEDONOFF:
		thePrefs.SetSchedulerEnabled(!thePrefs.IsSchedulerEnabled());
		theApp.scheduler->Check(true);
		break;
	case MP_HM_1STSWIZARD:
		FirstTimeWizard();
		break;
	case MP_HM_IPFILTER:
		{
			CIPFilterDlg dlg;
			dlg.DoModal();
		}
		break;
	case MP_HM_DIRECT_DOWNLOAD:
		{
			CDirectDownloadDlg dlg;
			dlg.DoModal();
		}
		break;
	case MP_HM_CHECK_OPEN_PORTS:
		TriggerPortTest(thePrefs.GetPort(), thePrefs.GetUDPPort());
		break;
	case MP_HM_RELOAD_FAKEFILEFILTER:
		if (FakeFileDetector::ReloadRules())
			AddLogLine(false, _T("Reloaded fake-file filter rules."));
		else
			AddLogLine(false, _T("Failed to reload fake-file filter rules."));
		break;
	case MP_HM_RELOAD_SHAREIGNORE_DAT:
		thePrefs.ReloadSharedIgnoreRules();
		AddLogLine(false, _T("Reloaded shared-file ignore rules."));
		break;
	case MP_HM_RESCAN_SHARED_FILES:
		{
			CWaitCursor curWait;
			if (sharedfileswnd != NULL)
				(void)sharedfileswnd->Reload(true);
			else if (theApp.sharedfiles != NULL)
				theApp.sharedfiles->Reload();
		}
		break;
	case MP_HM_SAVE_PREFERENCES_NOW:
		if (thePrefs.Save())
			AddLogLine(false, _T("Failed to save preferences and config files."));
		else
			AddLogLine(false, _T("Saved preferences and config files."));
		break;
	case MP_HM_REFRESH_INTERVAL_PAUSED:
		ApplyDesktopUiRefreshIntervalMs(DESKTOP_UI_REFRESH_PAUSED_MS);
		break;
	case MP_HM_REFRESH_INTERVAL_FAST:
		ApplyDesktopUiRefreshIntervalMs(DESKTOP_UI_REFRESH_FAST_MS);
		break;
	case MP_HM_REFRESH_INTERVAL_NORMAL:
		ApplyDesktopUiRefreshIntervalMs(DESKTOP_UI_REFRESH_NORMAL_MS);
		break;
	case MP_HM_REFRESH_INTERVAL_BELOWNORMAL:
		ApplyDesktopUiRefreshIntervalMs(DESKTOP_UI_REFRESH_BELOWNORMAL_MS);
		break;
	case MP_HM_REFRESH_INTERVAL_SLOW:
		ApplyDesktopUiRefreshIntervalMs(DESKTOP_UI_REFRESH_SLOW_MS);
		break;
	case MP_HM_REFRESH_INTERVAL_VERYSLOW:
		ApplyDesktopUiRefreshIntervalMs(DESKTOP_UI_REFRESH_VERYSLOW_MS);
		break;
	case MP_HM_UPDATE_SERVERMET_FROM_ADDRESSES:
		thePrefs.ReloadServerMetAddressList();
		UpdateServerMetFromConfiguredAddresses(serverwnd);
		break;
	case MP_HM_GEOLOCATION_DOWNLOAD:
		if (theApp.geolocation != NULL)
			theApp.geolocation->QueueManualRefresh();
		break;
	}
	if (((wParam >= MP_TOOLBARBITMAP && wParam < MP_TOOLBARBITMAP + 100)
			|| (wParam >= MP_SKIN_PROFILE && wParam < MP_SKIN_PROFILE + 100))
		&& toolbar != NULL
		&& toolbar->ExecuteCommand(wParam))
		return TRUE;
	if (wParam >= MP_WEBURL && wParam <= MP_WEBURL + 99)
		theWebServices.RunURL(NULL, (UINT)wParam);
	else if (wParam >= MP_SCHACTIONS && wParam <= MP_SCHACTIONS + 99) {
		theApp.scheduler->ActivateSchedule(wParam - MP_SCHACTIONS);
		theApp.scheduler->SaveOriginals(); // use the new settings as original
#ifdef HAVE_WIN7_SDK_H
	} else if (HIWORD(wParam) == THBN_CLICKED) {
		OnTBBPressed(LOWORD(wParam));
		return TRUE;
#endif
	}

	return CTrayDialog::OnCommand(wParam, lParam);
}

LRESULT CemuleDlg::OnMenuChar(UINT nChar, UINT nFlags, CMenu *pMenu)
{
	UINT nCmdID;
	if (toolbar->MapAccelerator((TCHAR)nChar, &nCmdID)) {
		OnCommand(nCmdID, 0);
		return MAKELONG(0, MNC_CLOSE);
	}
	return CTrayDialog::OnMenuChar(nChar, nFlags, pMenu);
}

void CemuleDlg::OnMenuSelect(UINT nItemID, UINT nFlags, HMENU hSysMenu)
{
	CTrayDialog::OnMenuSelect(nItemID, nFlags, hSysMenu);

	if ((nFlags & MF_POPUP) != 0 || nFlags == 0xFFFF)
		return;

	const UINT uStatusStringID = GetToolsMenuStatusStringID(nItemID);
	if (uStatusStringID != 0 && statusbar != NULL && ::IsWindow(statusbar->m_hWnd))
		statusbar->SetText(GetResString(uStatusStringID), SBarLog, 0);
}

void CemuleDlg::OnBnClickedHotmenu()
{
	ShowToolPopup(false);
}

void CemuleDlg::ShowToolPopup(bool toolsonly)
{
	POINT point{};
	::GetCursorPos(&point);
	ShowToolPopupAt(toolsonly, CPoint(point), false);
}

void CemuleDlg::ShowTrayToolPopup(CPoint pt)
{
	ShowToolPopupAt(true, pt, true);
}

void CemuleDlg::ApplyViewPresetCommand(UINT uCommandId)
{
	MuleListCtrlViewPresets::ETableViewPreset ePreset;
	MuleListCtrlViewPresets::EColumnWidthMode eWidthMode;
	if (!MuleListCtrlViewPresets::TryGetViewPresetCommand(uCommandId, ePreset, eWidthMode))
		return;

	for (const MuleListCtrlViewPresets::SListControlViewPresetProfile &profile : MuleListCtrlViewPresets::kProfiles)
		PersistViewPresetProfile(profile, ePreset, eWidthMode);

	CMuleListCtrl *apLiveLists[] = {
		transferwnd != NULL ? transferwnd->GetDownloadList() : NULL,
		transferwnd != NULL ? transferwnd->GetUploadList() : NULL,
		transferwnd != NULL ? transferwnd->GetQueueList() : NULL,
		transferwnd != NULL ? transferwnd->GetClientList() : NULL,
		transferwnd != NULL ? transferwnd->GetDownloadClientsList() : NULL,
		serverwnd != NULL ? &serverwnd->serverlistctrl : NULL,
		searchwnd != NULL && searchwnd->m_pwndResults != NULL ? &searchwnd->m_pwndResults->searchlistctrl : NULL,
		sharedfileswnd != NULL ? &sharedfileswnd->sharedfilesctrl : NULL,
	};
	for (CMuleListCtrl *pList : apLiveLists) {
		if (pList != NULL)
			pList->ApplyViewPreset(ePreset, eWidthMode);
	}
}

void CemuleDlg::ShowToolPopupAt(bool toolsonly, CPoint pt, bool bTrayMenu)
{
	CTitledMenu menu;
	menu.CreatePopupMenu();
	menu.AddMenuTitle(toolsonly ? AddMenuShortcutLabel(GetResString(IDS_TOOLS), _T("Alt+W")) : GetResString(IDS_HOTMENU), true);

	CTitledMenu Links;
	Links.CreateMenu();
	Links.AddMenuTitle(NULL, true);
	AppendLinksMenuEntries(Links);

	const auto appendConnectionItem = [this](CTitledMenu &targetMenu) {
		if (theApp.serverconnect->IsConnected())
			targetMenu.AppendMenu(MF_STRING, MP_HM_CON, AddMainShellShortcutLabel(GetResString(IDS_MAIN_BTN_DISCONNECT), MP_HM_CON), _T("DISCONNECT"));
		else if (theApp.serverconnect->IsConnecting())
			targetMenu.AppendMenu(MF_STRING, MP_HM_CON, AddMainShellShortcutLabel(GetResString(IDS_MAIN_BTN_CANCEL), MP_HM_CON), _T("STOPCONNECTING"));
		else
			targetMenu.AppendMenu(MF_STRING | (CanUseP2PConnectionCommands() ? MF_ENABLED : MF_GRAYED),
				MP_HM_CON,
				AddMainShellShortcutLabel(GetResString(IDS_MAIN_BTN_CONNECT), MP_HM_CON),
				_T("CONNECT"));
	};

	if (!toolsonly) {
		appendConnectionItem(menu);

		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING, MP_HM_KAD, AddMainShellShortcutLabel(GetResString(IDS_EM_KADEMLIA), MP_HM_KAD), _T("KADEMLIA"));
		menu.AppendMenu(MF_STRING, MP_HM_SRVR, AddMainShellShortcutLabel(GetResString(IDS_EM_SERVER), MP_HM_SRVR), _T("SERVER"));
		menu.AppendMenu(MF_STRING, MP_HM_TRANSFER, AddMainShellShortcutLabel(GetResString(IDS_EM_TRANS), MP_HM_TRANSFER), _T("TRANSFER"));
		menu.AppendMenu(MF_STRING, MP_HM_SEARCH, AddMainShellShortcutLabel(GetResString(IDS_EM_SEARCH), MP_HM_SEARCH), _T("SEARCH"));
		menu.AppendMenu(MF_STRING, MP_HM_FILES, AddMainShellShortcutLabel(GetResString(IDS_EM_FILES), MP_HM_FILES), _T("SharedFiles"));
		menu.AppendMenu(MF_STRING, MP_HM_MSGS, AddMainShellShortcutLabel(GetResString(IDS_EM_MESSAGES), MP_HM_MSGS), _T("MESSAGES"));
		menu.AppendMenu(MF_STRING, MP_HM_STATS, AddMainShellShortcutLabel(GetResString(IDS_EM_STATISTIC), MP_HM_STATS), _T("STATISTICS"));
		menu.AppendMenu(MF_STRING, MP_HM_PREFS, AddMainShellShortcutLabel(GetResString(IDS_EM_PREFS), MP_HM_PREFS), _T("PREFERENCES"));
		menu.AppendMenu(MF_STRING, MP_HM_HELP, AddMainShellShortcutLabel(GetResString(IDS_EM_HELP), MP_HM_HELP), _T("HELP"));
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING, MP_HM_DIRECT_DOWNLOAD, GetResString(IDS_SW_DIRECTDOWNLOAD) + _T("..."), _T("PASTELINK"));
		menu.AppendMenu(MF_STRING, MP_HM_IPFILTER, GetResString(IDS_IPFILTER) + _T("..."), _T("IPFILTER"));
		menu.AppendMenu(MF_SEPARATOR);
	}

	CTitledMenu session;
	session.CreateMenu();
	session.AddMenuTitle(NULL, true);

	CTitledMenu transfers;
	transfers.CreateMenu();
	transfers.AddMenuTitle(NULL, true);

	CTitledMenu folders;
	folders.CreateMenu();
	folders.AddMenuTitle(NULL, true);

	CTitledMenu categories;
	categories.CreateMenu();
	categories.AddMenuTitle(NULL, true);

	CTitledMenu filesCategories;
	filesCategories.CreateMenu();
	filesCategories.AddMenuTitle(NULL, true);

	CTitledMenu editConfigFiles;
	editConfigFiles.CreateMenu();
	editConfigFiles.AddMenuTitle(NULL, true);

	CTitledMenu networkUpdates;
	networkUpdates.CreateMenu();
	networkUpdates.AddMenuTitle(NULL, true);

	CTitledMenu controllersIntegrations;
	controllersIntegrations.CreateMenu();
	controllersIntegrations.AddMenuTitle(NULL, true);

	CTitledMenu maintenance;
	maintenance.CreateMenu();
	maintenance.AddMenuTitle(NULL, true);

	CTitledMenu viewPresets;
	viewPresets.CreateMenu();
	viewPresets.AddMenuTitle(NULL, true);

	CTitledMenu display;
	display.CreateMenu();
	display.AddMenuTitle(NULL, true);

	CTitledMenu displayViews;
	displayViews.CreateMenu();
	displayViews.AddMenuTitle(NULL, true);

	CTitledMenu diagnostics;
	diagnostics.CreateMenu();
	diagnostics.AddMenuTitle(NULL, true);

	CTitledMenu speedQuickActions;
	speedQuickActions.CreateMenu();
	speedQuickActions.AddMenuTitle(NULL, true);

	CTitledMenu refreshInterval;
	refreshInterval.CreateMenu();
	refreshInterval.AddMenuTitle(NULL, true);

	CTitledMenu helpLegacy;
	helpLegacy.CreateMenu();
	helpLegacy.AddMenuTitle(NULL, true);

	if (toolsonly) {
		const bool bExpandedToolsMenu = !bTrayMenu;
		UINT uGeoLocationMenuFlags = MF_STRING;
		if (!thePrefs.IsGeoLocationEnabled())
			uGeoLocationMenuFlags |= MF_GRAYED;

		if (bTrayMenu) {
			menu.AppendMenu(MF_STRING, MP_RESTORE, GetResString(IDS_MAIN_POPUP_RESTORE), _T("RESTORE"));
			menu.AppendMenu(MF_STRING, MP_MINIMIZETOTRAY, GetResString(IDS_PW_TRAY), _T("TOOLS"));
			appendConnectionItem(menu);
			menu.AppendMenu(MF_SEPARATOR);
		} else {
			appendConnectionItem(session);
			session.AppendMenu(MF_SEPARATOR);
		}
		session.AppendMenu(MF_STRING, MP_HM_SRVR, AddMainShellShortcutLabel(GetResString(IDS_EM_SERVER), MP_HM_SRVR), _T("SERVER"));
		session.AppendMenu(MF_STRING, MP_HM_TRANSFER, AddMainShellShortcutLabel(GetResString(IDS_EM_TRANS), MP_HM_TRANSFER), _T("TRANSFER"));
		session.AppendMenu(MF_STRING, MP_HM_SEARCH, AddMainShellShortcutLabel(GetResString(IDS_EM_SEARCH), MP_HM_SEARCH), _T("SEARCH"));
		session.AppendMenu(MF_STRING, MP_HM_FILES, AddMainShellShortcutLabel(GetResString(IDS_EM_FILES), MP_HM_FILES), _T("SharedFiles"));
		session.AppendMenu(MF_STRING, MP_HM_MSGS, AddMainShellShortcutLabel(GetResString(IDS_EM_MESSAGES), MP_HM_MSGS), _T("MESSAGES"));
		session.AppendMenu(MF_STRING, MP_HM_IRC, AddMainShellShortcutLabel(GetResString(IDS_IRC), MP_HM_IRC), _T("IRC"));
		session.AppendMenu(MF_STRING, MP_HM_STATS, AddMainShellShortcutLabel(GetResString(IDS_EM_STATISTIC), MP_HM_STATS), _T("STATISTICS"));
		session.AppendMenu(MF_STRING, MP_HM_PREFS, AddMainShellShortcutLabel(GetResString(IDS_EM_PREFS), MP_HM_PREFS), _T("PREFERENCES"));
		if (!bTrayMenu) {
			session.AppendMenu(MF_SEPARATOR);
			session.AppendMenu(MF_STRING, MP_MINIMIZETOTRAY, GetResString(IDS_PW_TRAY), _T("TOOLS"));
		}

		int iAllFilesToPause = 0;
		int iAllFilesToResume = 0;
		int iAllFilesToStop = 0;
		int iAllFilesTotal = 0;
		int iAllCompleted = 0;
		if (transferwnd != NULL && transferwnd->GetDownloadList() != NULL) {
			transferwnd->GetDownloadList()->CountTransferCommandsInCategory(-1, iAllFilesToPause, iAllFilesToResume, iAllFilesToStop);
			iAllCompleted = transferwnd->GetDownloadList()->GetCompleteDownloads(-1, iAllFilesTotal);
		}
		transfers.AppendMenu(MF_STRING | (iAllFilesToResume > 0 ? MF_ENABLED : MF_GRAYED), MP_HM_RESUME_ALL_DOWNLOADS, FormatAllTransferCommandLabel(IDS_DL_RESUME), _T("RESUMEALL"));
		transfers.AppendMenu(MF_STRING | (iAllFilesToPause > 0 ? MF_ENABLED : MF_GRAYED), MP_HM_PAUSE_ALL_DOWNLOADS, FormatAllTransferCommandLabel(IDS_DL_PAUSE), _T("PAUSEALL"));
		transfers.AppendMenu(MF_STRING | (iAllFilesToStop > 0 ? MF_ENABLED : MF_GRAYED), MP_HM_STOP_ALL_DOWNLOADS, FormatAllTransferCommandLabel(IDS_DL_STOP), _T("STOPALL"));
		transfers.AppendMenu(MF_SEPARATOR);
		transfers.AppendMenu(MF_STRING | (iAllCompleted > 0 ? MF_ENABLED : MF_GRAYED), MP_HM_CLEAR_COMPLETED_DOWNLOADS, GetResString(IDS_DL_CLEAR), _T("CLEARCOMPLETE"));

		const CString &kbyps(GetResString(IDS_KBYTESPERSEC));
		CString text;
		speedQuickActions.AppendMenu(MF_STRING | MF_GRAYED, 0, GetResString(IDS_SPEED_ACTION_BOTH_UPLOAD_DOWNLOAD));
		for (const SpeedQuickActionsSeams::CQuickSpeedPercentAction &action : SpeedQuickActionsSeams::kBothPercentActions) {
			text.Format(
				GetResString(IDS_SPEED_LIMIT_BOTH_FMT),
				action.uPercent,
				SpeedQuickActionsSeams::CalculatePercentLimitKiB(thePrefs.GetConfiguredMaxUpload(), action.uPercent),
				(LPCTSTR)kbyps,
				SpeedQuickActionsSeams::CalculatePercentLimitKiB(thePrefs.GetConfiguredMaxDownload(), action.uPercent),
				(LPCTSTR)kbyps);
			speedQuickActions.AppendMenu(MF_STRING, action.uCommandId, text, _T("SPEED"));
		}
		speedQuickActions.AppendMenu(MF_SEPARATOR);
		speedQuickActions.AppendMenu(MF_STRING | MF_GRAYED, 0, GetResString(IDS_SPEED_ACTION_UPLOAD));
		for (const SpeedQuickActionsSeams::CQuickSpeedPercentAction &action : SpeedQuickActionsSeams::kUploadPercentActions) {
			text.Format(
				GetResString(IDS_SPEED_LIMIT_UPLOAD_FMT),
				action.uPercent,
				SpeedQuickActionsSeams::CalculatePercentLimitKiB(thePrefs.GetConfiguredMaxUpload(), action.uPercent),
				(LPCTSTR)kbyps);
			speedQuickActions.AppendMenu(MF_STRING, action.uCommandId, text, _T("UPLOAD"));
		}
		speedQuickActions.AppendMenu(MF_SEPARATOR);
		speedQuickActions.AppendMenu(MF_STRING | MF_GRAYED, 0, GetResString(IDS_SPEED_ACTION_DOWNLOAD));
		for (const SpeedQuickActionsSeams::CQuickSpeedPercentAction &action : SpeedQuickActionsSeams::kDownloadPercentActions) {
			text.Format(
				GetResString(IDS_SPEED_LIMIT_DOWNLOAD_FMT),
				action.uPercent,
				SpeedQuickActionsSeams::CalculatePercentLimitKiB(thePrefs.GetConfiguredMaxDownload(), action.uPercent),
				(LPCTSTR)kbyps);
			speedQuickActions.AppendMenu(MF_STRING, action.uCommandId, text, _T("DOWNLOAD"));
		}
		speedQuickActions.AppendMenu(MF_SEPARATOR);
		speedQuickActions.AppendMenu(MF_STRING, MP_QS_PA, GetResString(IDS_SPEED_ACTION_ALL_TO_MIN_MNEMONIC), _T("SPEEDMIN"));
		speedQuickActions.AppendMenu(MF_STRING, MP_QS_UA, GetResString(IDS_SPEED_ACTION_ALL_TO_MAX_MNEMONIC), _T("SPEEDMAX"));

		const UINT uDesktopRefreshIntervalMs = thePrefs.GetDesktopUiRefreshIntervalMs();
		const auto getRefreshIntervalFlags = [uDesktopRefreshIntervalMs](UINT uIntervalMs) -> UINT {
			return MF_STRING | (uDesktopRefreshIntervalMs == uIntervalMs ? MF_CHECKED : 0);
		};
		refreshInterval.AppendMenu(getRefreshIntervalFlags(DESKTOP_UI_REFRESH_PAUSED_MS), MP_HM_REFRESH_INTERVAL_PAUSED, GetResString(IDS_REFRESH_INTERVAL_PAUSED), _T("PAUSE"));
		refreshInterval.AppendMenu(getRefreshIntervalFlags(DESKTOP_UI_REFRESH_FAST_MS), MP_HM_REFRESH_INTERVAL_FAST, GetResString(IDS_REFRESH_INTERVAL_FAST), _T("STATSTIME"));
		refreshInterval.AppendMenu(getRefreshIntervalFlags(DESKTOP_UI_REFRESH_NORMAL_MS), MP_HM_REFRESH_INTERVAL_NORMAL, GetResString(IDS_REFRESH_INTERVAL_NORMAL), _T("STATSTIME"));
		refreshInterval.AppendMenu(getRefreshIntervalFlags(DESKTOP_UI_REFRESH_BELOWNORMAL_MS), MP_HM_REFRESH_INTERVAL_BELOWNORMAL, GetResString(IDS_REFRESH_INTERVAL_BELOWNORMAL), _T("STATSTIME"));
		refreshInterval.AppendMenu(getRefreshIntervalFlags(DESKTOP_UI_REFRESH_SLOW_MS), MP_HM_REFRESH_INTERVAL_SLOW, GetResString(IDS_REFRESH_INTERVAL_SLOW), _T("STATSTIME"));
		refreshInterval.AppendMenu(getRefreshIntervalFlags(DESKTOP_UI_REFRESH_VERYSLOW_MS), MP_HM_REFRESH_INTERVAL_VERYSLOW, GetResString(IDS_REFRESH_INTERVAL_VERYSLOW), _T("STATSTIME"));

		if (toolbar != NULL) {
			CTitledMenu &displayTarget = bExpandedToolsMenu ? displayViews : display;
			displayTarget.AppendMenu(MF_STRING | MF_GRAYED, 0, GetResString(IDS_TOOLBARSKINS));
			toolbar->AppendToolbarBitmapMenu(displayTarget);
			displayTarget.AppendMenu(MF_SEPARATOR);
			displayTarget.AppendMenu(MF_STRING | MF_GRAYED, 0, GetResString(IDS_SKIN_PROF));
			toolbar->AppendSkinProfileMenu(displayTarget);
			displayTarget.AppendMenu(MF_SEPARATOR);
			displayTarget.AppendMenu(MF_STRING | MF_GRAYED, 0, GetResString(IDS_TEXTLABELS));
			toolbar->AppendTextLabelMenu(displayTarget);
			displayTarget.AppendMenu(MF_SEPARATOR);
			displayTarget.AppendMenu(MF_STRING, MP_HM_RESET_DISPLAY, GetResString(IDS_PW_RESET), _T("DISPLAY_RESET"));
			displayTarget.AppendMenu(MF_STRING, MP_CUSTOMIZETOOLBAR, GetResString(IDS_CUSTOMIZETOOLBAR), _T("CUSTOMIZE_TOOLBAR"));
		}

		folders.AppendMenu(MF_STRING, MP_HM_OPENINC, GetResString(IDS_OPENINC) + _T("..."), _T("INCOMING"));
		folders.AppendMenu(MF_STRING, MP_HM_OPEN_TEMPDIR, GetResString(IDS_OPEN_TEMP_DIR) + _T("..."), _T("OPENFOLDER"));
		folders.AppendMenu(MF_STRING, MP_HM_OPENCONFIGDIR, GetResString(IDS_OPENCONFIGDIR) + _T("..."), _T("OPENFOLDER"));
		folders.AppendMenu(MF_STRING, MP_HM_OPENLOGDIR, GetResString(IDS_OPENLOGDIR) + _T("..."), _T("OPENFOLDER"));
		folders.AppendMenu(MF_SEPARATOR);
		folders.AppendMenu(MF_STRING, MP_HM_OPEN_WEBSERVERDIR, GetResString(IDS_OPEN_WEBSERVER_DIR) + _T("..."), _T("OPENFOLDER"));
		folders.AppendMenu(MF_STRING, MP_HM_OPEN_SKINDIR, GetResString(IDS_OPEN_SKINS_DIR) + _T("..."), _T("OPENFOLDER"));
		folders.AppendMenu(MF_STRING, MP_HM_OPEN_TOOLBARDIR, GetResString(IDS_OPEN_TOOLBAR_DIR) + _T("..."), _T("OPENFOLDER"));
		folders.AppendMenu(MF_STRING, MP_HM_OPEN_EXECUTABLEDIR, GetResString(IDS_OPEN_EXECUTABLE_DIR) + _T("..."), _T("OPENFOLDER"));

		categories.AppendMenu(MF_STRING, MP_HM_MANAGE_CATEGORIES, GetResString(IDS_MANAGE_CATEGORIES), _T("CATEGORY"));
		categories.AppendMenu(MF_SEPARATOR);
		categories.AppendMenu(MF_STRING, MP_HM_EDIT_CATEGORY_INI, GetResString(IDS_EDIT_CATEGORY_INI), _T("PREFERENCES"));

		if (bExpandedToolsMenu) {
			filesCategories.AppendMenu(MF_STRING, MP_HM_OPENINC, GetResString(IDS_OPENINC) + _T("..."), _T("INCOMING"));
			filesCategories.AppendMenu(MF_STRING, MP_HM_OPEN_TEMPDIR, GetResString(IDS_OPEN_TEMP_DIR) + _T("..."), _T("OPENFOLDER"));
			filesCategories.AppendMenu(MF_STRING, MP_HM_OPENCONFIGDIR, GetResString(IDS_OPENCONFIGDIR) + _T("..."), _T("OPENFOLDER"));
			filesCategories.AppendMenu(MF_STRING, MP_HM_OPENLOGDIR, GetResString(IDS_OPENLOGDIR) + _T("..."), _T("OPENFOLDER"));
			filesCategories.AppendMenu(MF_SEPARATOR);
			filesCategories.AppendMenu(MF_STRING, MP_HM_OPEN_WEBSERVERDIR, GetResString(IDS_OPEN_WEBSERVER_DIR) + _T("..."), _T("OPENFOLDER"));
			filesCategories.AppendMenu(MF_STRING, MP_HM_OPEN_SKINDIR, GetResString(IDS_OPEN_SKINS_DIR) + _T("..."), _T("OPENFOLDER"));
			filesCategories.AppendMenu(MF_STRING, MP_HM_OPEN_TOOLBARDIR, GetResString(IDS_OPEN_TOOLBAR_DIR) + _T("..."), _T("OPENFOLDER"));
			filesCategories.AppendMenu(MF_STRING, MP_HM_OPEN_EXECUTABLEDIR, GetResString(IDS_OPEN_EXECUTABLE_DIR) + _T("..."), _T("OPENFOLDER"));
			filesCategories.AppendMenu(MF_SEPARATOR);
			filesCategories.AppendMenu(MF_STRING, MP_HM_MANAGE_CATEGORIES, GetResString(IDS_MANAGE_CATEGORIES), _T("CATEGORY"));
			filesCategories.AppendMenu(MF_STRING, MP_HM_EDIT_CATEGORY_INI, GetResString(IDS_EDIT_CATEGORY_INI), _T("PREFERENCES"));
		}

		editConfigFiles.AppendMenu(MF_STRING, MP_HM_EDIT_PREFERENCES_INI, GetResString(IDS_EDIT_PREFERENCES_INI), _T("PREFERENCES"));
		editConfigFiles.AppendMenu(MF_SEPARATOR);
		editConfigFiles.AppendMenu(MF_STRING, MP_HM_EDIT_IPFILTER_DAT, GetResString(IDS_EDIT_IPFILTER_DAT), _T("IPFILTER"));
		editConfigFiles.AppendMenu(MF_STRING, MP_HM_EDIT_FAKEFILEFILTER_DAT, GetResString(IDS_EDIT_FAKEFILEFILTER_DAT), _T("TOOLS"));
		editConfigFiles.AppendMenu(MF_STRING, MP_HM_EDIT_SHAREIGNORE_DAT, GetResString(IDS_EDIT_SHAREIGNORE_DAT), _T("TOOLS"));
		editConfigFiles.AppendMenu(MF_STRING, MP_HM_EDIT_ADDRESSES_DAT, GetResString(IDS_EDIT_ADDRESSES_DAT), _T("SERVER"));
		editConfigFiles.AppendMenu(MF_STRING, MP_HM_EDIT_STATIC_SERVERS_DAT, GetResString(IDS_EDIT_STATIC_SERVERS_DAT), _T("SERVER"));
		editConfigFiles.AppendMenu(MF_STRING, MP_HM_EDIT_WEBSERVICES_DAT, GetResString(IDS_EDIT_WEBSERVICES_DAT), _T("WEB"));
		editConfigFiles.AppendMenu(MF_SEPARATOR);
		editConfigFiles.AppendMenu(MF_STRING, MP_HM_EDIT_SHAREDDIR_DAT, GetResString(IDS_EDIT_SHAREDDIR_DAT), _T("OPENFOLDER"));
		editConfigFiles.AppendMenu(MF_STRING, MP_HM_EDIT_MONITORED_SHAREDDIR_DAT, GetResString(IDS_EDIT_MONITORED_SHAREDDIR_DAT), _T("OPENFOLDER"));
		editConfigFiles.AppendMenu(MF_STRING, MP_HM_EDIT_MONITOR_OWNED_SHAREDDIR_DAT, GetResString(IDS_EDIT_MONITOR_OWNED_SHAREDDIR_DAT), _T("OPENFOLDER"));
		editConfigFiles.AppendMenu(MF_SEPARATOR);
		editConfigFiles.AppendMenu(MF_STRING, MP_HM_EDIT_NOTIFIER_INI, GetResString(IDS_EDIT_NOTIFIER_INI), _T("TOOLS"));
		editConfigFiles.AppendMenu(MF_STRING, MP_HM_EDIT_FILEINFO_INI, GetResString(IDS_EDIT_FILEINFO_INI), _T("FILECOMMENTS"));
		editConfigFiles.AppendMenu(MF_STRING, MP_HM_EDIT_STATISTICS_INI, GetResString(IDS_EDIT_STATISTICS_INI), _T("STATISTICS"));

		networkUpdates.AppendMenu(MF_STRING, MP_HM_IPFILTER, GetResString(IDS_IPFILTER) + _T("..."), _T("IPFILTER"));
		networkUpdates.AppendMenu(MF_STRING, MP_HM_DIRECT_DOWNLOAD, GetResString(IDS_SW_DIRECTDOWNLOAD) + _T("..."), _T("PASTELINK"));
		networkUpdates.AppendMenu(MF_SEPARATOR);
		networkUpdates.AppendMenu(MF_STRING, MP_HM_UPDATE_SERVERMET_FROM_ADDRESSES, GetResString(IDS_UPDATE_SERVERMET_FROM_ADDRESSES), _T("SERVER"));
		networkUpdates.AppendMenu(MF_STRING, MP_HM_CHECK_OPEN_PORTS, GetResString(IDS_CHECK_OPEN_PORTS), _T("WEB"));
		networkUpdates.AppendMenu(MF_STRING, MP_HM_REPAIR_WINDOWS_FIREWALL, GetResString(IDS_REPAIR_WINDOWS_FIREWALL_RULES), _T("FIREWALL"));
		networkUpdates.AppendMenu(MF_SEPARATOR);
		networkUpdates.AppendMenu(uGeoLocationMenuFlags, MP_HM_GEOLOCATION_DOWNLOAD, GetResString(IDS_GEOLOCATION_DOWNLOAD_DB), _T("DOWNLOAD"));

		controllersIntegrations.AppendMenu(MF_STRING, MP_HM_REGISTER_PROWLARR, GetResString(IDS_REGISTER_PROWLARR_INTEGRATION), _T("PROWLARR"));
		controllersIntegrations.AppendMenu(MF_STRING, MP_HM_REGISTER_RADARR, GetResString(IDS_REGISTER_RADARR_INTEGRATION), _T("RADARR"));
		controllersIntegrations.AppendMenu(MF_STRING, MP_HM_REGISTER_SONARR, GetResString(IDS_REGISTER_SONARR_INTEGRATION), _T("SONARR"));

		maintenance.AppendMenu(MF_STRING, MP_HM_RELOAD_IPFILTER_DAT, GetResString(IDS_RELOAD_IPFILTER_DAT), _T("IPFILTER"));
		maintenance.AppendMenu(MF_STRING, MP_HM_RELOAD_FAKEFILEFILTER, GetResString(IDS_RELOADFAKEFILEFILTER), _T("TOOLS"));
		maintenance.AppendMenu(MF_STRING, MP_HM_RELOAD_SHAREIGNORE_DAT, GetResString(IDS_RELOAD_SHAREIGNORE_DAT), _T("TOOLS"));
		maintenance.AppendMenu(MF_SEPARATOR);
		maintenance.AppendMenu(MF_STRING, MP_HM_RESCAN_SHARED_FILES, GetResString(IDS_RESCAN_SHARED_FILES), _T("SharedFiles"));
		maintenance.AppendMenu(MF_STRING, MP_HM_SAVE_PREFERENCES_NOW, GetResString(IDS_SAVE_PREFERENCES_NOW), _T("PREFERENCES"));
		maintenance.AppendMenu(MF_SEPARATOR);
		maintenance.AppendMenu(MF_STRING, MP_HM_ENABLE_WINDOWS_LONG_PATHS, GetResString(IDS_ENABLE_WINDOWS_LONG_PATHS), _T("LONGPATHS"));
		maintenance.AppendMenu(MF_STRING, MP_HM_DEFENDER_EXCLUDE_DOWNLOAD_FOLDERS, GetResString(IDS_DEFENDER_EXCLUDE_DOWNLOAD_FOLDERS), _T("SECURITY"));

		if (bExpandedToolsMenu) {
			if (displayViews.GetMenuItemCount() > 0)
				displayViews.AppendMenu(MF_SEPARATOR);
			displayViews.AppendMenu(MF_STRING, MP_HM_VIEW_PRESET_STOCK_KEEP_WIDTHS, GetResString(IDS_VIEW_PRESET_STOCK_KEEP_WIDTHS), _T("VIEW_STOCK"));
			displayViews.AppendMenu(MF_STRING, MP_HM_VIEW_PRESET_STOCK_RESET_WIDTHS, GetResString(IDS_VIEW_PRESET_STOCK_RESET_WIDTHS), _T("WIDTH_RESET"));
			displayViews.AppendMenu(MF_SEPARATOR);
			displayViews.AppendMenu(MF_STRING, MP_HM_VIEW_PRESET_EXTENDED_KEEP_WIDTHS, GetResString(IDS_VIEW_PRESET_EXTENDED_KEEP_WIDTHS), _T("VIEW_EXTENDED"));
			displayViews.AppendMenu(MF_STRING, MP_HM_VIEW_PRESET_EXTENDED_RESET_WIDTHS, GetResString(IDS_VIEW_PRESET_EXTENDED_RESET_WIDTHS), _T("WIDTH_RESET"));
			displayViews.AppendMenu(MF_SEPARATOR);
			displayViews.AppendMenu(MF_STRING, MP_HM_VIEW_PRESET_FULL_KEEP_WIDTHS, GetResString(IDS_VIEW_PRESET_FULL_KEEP_WIDTHS), _T("VIEW_FULL"));
			displayViews.AppendMenu(MF_STRING, MP_HM_VIEW_PRESET_FULL_RESET_WIDTHS, GetResString(IDS_VIEW_PRESET_FULL_RESET_WIDTHS), _T("WIDTH_RESET"));
		} else {
			viewPresets.AppendMenu(MF_STRING, MP_HM_VIEW_PRESET_STOCK_KEEP_WIDTHS, GetResString(IDS_VIEW_PRESET_STOCK_KEEP_WIDTHS), _T("VIEW_STOCK"));
			viewPresets.AppendMenu(MF_STRING, MP_HM_VIEW_PRESET_STOCK_RESET_WIDTHS, GetResString(IDS_VIEW_PRESET_STOCK_RESET_WIDTHS), _T("WIDTH_RESET"));
			viewPresets.AppendMenu(MF_SEPARATOR);
			viewPresets.AppendMenu(MF_STRING, MP_HM_VIEW_PRESET_EXTENDED_KEEP_WIDTHS, GetResString(IDS_VIEW_PRESET_EXTENDED_KEEP_WIDTHS), _T("VIEW_EXTENDED"));
			viewPresets.AppendMenu(MF_STRING, MP_HM_VIEW_PRESET_EXTENDED_RESET_WIDTHS, GetResString(IDS_VIEW_PRESET_EXTENDED_RESET_WIDTHS), _T("WIDTH_RESET"));
			viewPresets.AppendMenu(MF_SEPARATOR);
			viewPresets.AppendMenu(MF_STRING, MP_HM_VIEW_PRESET_FULL_KEEP_WIDTHS, GetResString(IDS_VIEW_PRESET_FULL_KEEP_WIDTHS), _T("VIEW_FULL"));
			viewPresets.AppendMenu(MF_STRING, MP_HM_VIEW_PRESET_FULL_RESET_WIDTHS, GetResString(IDS_VIEW_PRESET_FULL_RESET_WIDTHS), _T("WIDTH_RESET"));
		}

		diagnostics.AppendMenu(GetExistingFileMenuFlags(theLog.GetFilePath()), MP_HM_OPEN_EMULE_LOG, GetResString(IDS_OPEN_EMULE_LOG), _T("LOG"));
		diagnostics.AppendMenu(GetExistingFileMenuFlags(theVerboseLog.GetFilePath()), MP_HM_OPEN_VERBOSE_LOG, GetResString(IDS_OPEN_VERBOSE_LOG), _T("LOG"));
		diagnostics.AppendMenu(MF_SEPARATOR);
		diagnostics.AppendMenu(MF_STRING, MP_HM_COPY_DIAGNOSTIC_SNAPSHOT_JSON, GetResString(IDS_DIAG_COPY_SNAPSHOT_JSON), _T("DIAGNOSTICS"));
		diagnostics.AppendMenu(MF_STRING, MP_HM_COPY_REDACTED_DIAGNOSTIC_SNAPSHOT_JSON, GetResString(IDS_DIAG_COPY_REDACTED_SNAPSHOT_JSON), _T("DIAGNOSTICS"));
		diagnostics.AppendMenu(MF_SEPARATOR);
		diagnostics.AppendMenu(MF_STRING, MP_HM_CAPTURE_MINIDUMP, GetResString(IDS_DIAG_CAPTURE_MINIDUMP), _T("DUMP"));
		diagnostics.AppendMenu(MF_STRING, MP_HM_CAPTURE_FULLDUMP, GetResString(IDS_DIAG_CAPTURE_FULLDUMP), _T("DUMP"));

		if (bExpandedToolsMenu) {
			AppendLinksMenuEntries(helpLegacy);

			menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)session.m_hMenu, WithMenuMnemonic(GetResString(IDS_TOOLS_SESSION), _T('S')), _T("CONNECT"));
			menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)transfers.m_hMenu, GetResString(IDS_EM_TRANS), _T("TRANSFER"));
			menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)speedQuickActions.m_hMenu, WithMenuMnemonic(GetResString(IDS_TOOLS_SPEED_QUICK_ACTIONS), _T('P')), _T("SPEED"));
			menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)refreshInterval.m_hMenu, GetResString(IDS_TOOLS_REFRESH_INTERVAL), _T("STATSTIME"));
			menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)filesCategories.m_hMenu, WithMenuMnemonic(FormatCompoundMenuLabel(IDS_TOOLS_FOLDERS, IDS_TOOLS_CATEGORIES), _T('F')), _T("FOLDERS"));
			menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)networkUpdates.m_hMenu, WithMenuMnemonic(GetResString(IDS_TOOLS_NETWORK_UPDATES), _T('N')), _T("SERVER"));
			menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)controllersIntegrations.m_hMenu, WithMenuMnemonic(GetResString(IDS_TOOLS_CONTROLLERS_INTEGRATIONS), _T('I')), _T("CONTROLLERS"));
			menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)maintenance.m_hMenu, WithMenuMnemonic(GetResString(IDS_TOOLS_MAINTENANCE), _T('M')), _T("TOOLS"));
			menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)diagnostics.m_hMenu, WithMenuMnemonic(GetResString(IDS_TOOLS_DIAGNOSTICS), _T('D')), _T("DIAGNOSTICS"));
			menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)displayViews.m_hMenu, WithMenuMnemonic(FormatCompoundMenuLabel(IDS_PW_DISPLAY, IDS_TOOLS_VIEW_PRESETS), _T('V')), _T("DISPLAY"));
			menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)editConfigFiles.m_hMenu, WithMenuMnemonic(GetResString(IDS_TOOLS_EDIT_CONFIG_FILES), _T('E')), _T("PREFERENCES"));
			menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)helpLegacy.m_hMenu, WithMenuMnemonic(GetResString(IDS_LINKS), _T('H')), _T("HELP"));
		} else {
			menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)session.m_hMenu, WithMenuMnemonic(GetResString(IDS_TOOLS_SESSION), _T('S')), _T("CONNECT"));
			menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)transfers.m_hMenu, GetResString(IDS_EM_TRANS), _T("TRANSFER"));
			menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)speedQuickActions.m_hMenu, WithMenuMnemonic(GetResString(IDS_TOOLS_SPEED_QUICK_ACTIONS), _T('P')), _T("SPEED"));
			menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)refreshInterval.m_hMenu, GetResString(IDS_TOOLS_REFRESH_INTERVAL), _T("STATSTIME"));
			if (toolbar != NULL)
				menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)display.m_hMenu, WithMenuMnemonic(GetResString(IDS_PW_DISPLAY), _T('D')), _T("DISPLAY"));
			menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)folders.m_hMenu, WithMenuMnemonic(GetResString(IDS_TOOLS_FOLDERS), _T('F')), _T("OPENFOLDER"));
			menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)categories.m_hMenu, WithMenuMnemonic(GetResString(IDS_TOOLS_CATEGORIES), _T('C')), _T("CATEGORY"));
			menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)editConfigFiles.m_hMenu, WithMenuMnemonic(GetResString(IDS_TOOLS_EDIT_CONFIG_FILES), _T('E')), _T("PREFERENCES"));
			menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)networkUpdates.m_hMenu, WithMenuMnemonic(GetResString(IDS_TOOLS_NETWORK_UPDATES), _T('N')), _T("SERVER"));
			menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)controllersIntegrations.m_hMenu, WithMenuMnemonic(GetResString(IDS_TOOLS_CONTROLLERS_INTEGRATIONS), _T('I')), _T("CONTROLLERS"));
			menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)maintenance.m_hMenu, WithMenuMnemonic(GetResString(IDS_TOOLS_MAINTENANCE), _T('M')), _T("TOOLS"));
			menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)viewPresets.m_hMenu, WithMenuMnemonic(GetResString(IDS_TOOLS_VIEW_PRESETS), _T('V')), _T("VIEW_PRESETS"));
			menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)diagnostics.m_hMenu, WithMenuMnemonic(GetResString(IDS_TOOLS_DIAGNOSTICS), _T('D')), _T("DIAGNOSTICS"));
		}
	} else {
		menu.AppendMenu(MF_STRING, MP_HM_OPENINC, GetResString(IDS_OPENINC) + _T("..."), _T("INCOMING"));
		menu.AppendMenu(MF_STRING, MP_HM_OPENCONFIGDIR, GetResString(IDS_OPENCONFIGDIR) + _T("..."), _T("OPENFOLDER"));
		menu.AppendMenu(MF_STRING, MP_HM_OPENLOGDIR, GetResString(IDS_OPENLOGDIR) + _T("..."), _T("OPENFOLDER"));
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)Links.m_hMenu, GetResString(IDS_LINKS), _T("WEB"));
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING, MP_HM_IRC, AddMainShellShortcutLabel(GetResString(IDS_IRC), MP_HM_IRC), _T("IRC"));
	}

	if (bTrayMenu) {
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)Links.m_hMenu, GetResString(IDS_LINKS), _T("WEB"));
	}

	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, MP_HM_RESTART_APP, GetResString(IDS_RESTART_EMULE), _T("TOOLS"));
	menu.AppendMenu(MF_STRING, MP_HM_EXIT, GetResString(IDS_EXIT) + _T("\tAlt+X"), _T("EXIT"));
	if (bTrayMenu)
		SetForegroundWindow();
	menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, this);
	if (bTrayMenu)
		PostMessage(WM_NULL, 0, 0);
	VERIFY(session.DestroyMenu());
	VERIFY(transfers.DestroyMenu());
	VERIFY(folders.DestroyMenu());
	VERIFY(categories.DestroyMenu());
	VERIFY(filesCategories.DestroyMenu());
	VERIFY(editConfigFiles.DestroyMenu());
	VERIFY(networkUpdates.DestroyMenu());
	VERIFY(controllersIntegrations.DestroyMenu());
	VERIFY(maintenance.DestroyMenu());
	VERIFY(viewPresets.DestroyMenu());
	VERIFY(display.DestroyMenu());
	VERIFY(displayViews.DestroyMenu());
	VERIFY(diagnostics.DestroyMenu());
	VERIFY(speedQuickActions.DestroyMenu());
	VERIFY(refreshInterval.DestroyMenu());
	VERIFY(helpLegacy.DestroyMenu());
	VERIFY(Links.DestroyMenu());
	VERIFY(menu.DestroyMenu());
}


void CemuleDlg::CaptureDiagnosticDump(bool bFullMemoryDump)
{
	const CString strDumpKind(GetResString(bFullMemoryDump ? IDS_DIAG_DUMP_KIND_FULL : IDS_DIAG_DUMP_KIND_MINI));
	const CMiniDumper::SManualDumpResult result = CMiniDumper::CreateManualDump(
		theApp.m_strCurVersionLongDbg,
		thePrefs.GetMuleDirectory(EMULE_LOGDIR),
		bFullMemoryDump);

	if (result.bSuccess) {
		AddLogLine(false, GetResString(IDS_DIAG_DUMP_LOG_SUCCESS), (LPCTSTR)strDumpKind, (LPCTSTR)result.strDumpPath);

		CString strMessage;
		strMessage.Format(
			GetResString(bFullMemoryDump ? IDS_DIAG_FULLDUMP_SUCCESS : IDS_DIAG_MINIDUMP_SUCCESS),
			(LPCTSTR)result.strDumpPath);
		AfxMessageBox(strMessage, MB_ICONINFORMATION | MB_OK);
		return;
	}

	const CString strError(GetErrorMessage(result.dwError, 1));
	AddLogLine(false, GetResString(IDS_DIAG_DUMP_LOG_FAILURE), (LPCTSTR)strDumpKind, (LPCTSTR)strError);

	CString strMessage;
	strMessage.Format(GetResString(IDS_DIAG_DUMP_FAILURE), (LPCTSTR)strDumpKind, (LPCTSTR)strError);
	AfxMessageBox(strMessage, MB_ICONERROR | MB_OK);
}


void CemuleDlg::CopyDiagnosticSnapshotToClipboard(bool bRedacted)
{
	const DiagnosticSnapshotSeams::ESnapshotPrivacyMode ePrivacyMode = bRedacted
		? DiagnosticSnapshotSeams::ESnapshotPrivacyMode::Redacted
		: DiagnosticSnapshotSeams::ESnapshotPrivacyMode::Raw;
	const nlohmann::json snapshot = BuildDiagnosticSnapshotJson(ePrivacyMode);
	const CStringA strSerialized(WebServerJson::SerializeJsonUtf8(snapshot));
	const CString strClipboardText(WebServerJson::FromStdUtf8(std::string(static_cast<LPCSTR>(strSerialized), strSerialized.GetLength())));
	const CString strKind(GetResString(bRedacted ? IDS_DIAG_SNAPSHOT_KIND_REDACTED : IDS_DIAG_SNAPSHOT_KIND_RAW));

	if (theApp.CopyTextToClipboard(strClipboardText)) {
		AddLogLine(false, GetResString(IDS_DIAG_SNAPSHOT_COPY_SUCCESS), (LPCTSTR)strKind);
		return;
	}

	AddLogLine(true, GetResString(IDS_DIAG_SNAPSHOT_COPY_FAILURE), (LPCTSTR)strKind);
}

void CemuleDlg::RepairWindowsFirewallRules()
{
	const std::vector<WindowsFirewallRepairSeams::CFirewallRuleSpec> rules = WindowsFirewallRepairSeams::BuildDesiredRules(
		thePrefs.GetPort(),
		thePrefs.GetUDPPort(),
		thePrefs.GetWSIsEnabled(),
		thePrefs.GetWSPort(),
		thePrefs.GetWebBindAddr());
	if (rules.empty()) {
		s_lastFirewallRepairResult = WindowsFirewallRepair::CRepairLaunchResult{};
		s_lastFirewallRepairResult.bSucceeded = true;
		AddLogLine(false, GetResString(IDS_FIREWALL_REPAIR_NO_RULES));
		return;
	}

	CString strProgramPath(PathHelpers::GetModuleFilePath(NULL));
	if (strProgramPath.IsEmpty())
		strProgramPath = GetModulePath();

	CWaitCursor waitCursor;
	const bool bSucceeded = WindowsFirewallRepair::RunElevatedRepair(strProgramPath, rules, s_lastFirewallRepairResult);
	if (bSucceeded) {
		AddLogLine(false, GetResString(IDS_FIREWALL_REPAIR_SUCCESS), static_cast<UINT>(rules.size()));
		return;
	}

	if (s_lastFirewallRepairResult.bCancelled) {
		AddLogLine(false, GetResString(IDS_FIREWALL_REPAIR_CANCELLED));
		return;
	}

	CString strError(s_lastFirewallRepairResult.strErrorText);
	if (strError.IsEmpty())
		strError = GetErrorMessage(s_lastFirewallRepairResult.dwLastError != ERROR_SUCCESS ? s_lastFirewallRepairResult.dwLastError : s_lastFirewallRepairResult.dwExitCode);
	AddLogLine(true, GetResString(IDS_FIREWALL_REPAIR_FAILURE), (LPCTSTR)strError);
}

void CemuleDlg::EnableWindowsLongPaths()
{
	WindowsMaintenanceActions::CMaintenanceLaunchResult result;
	CWaitCursor waitCursor;
	const bool bSucceeded = WindowsMaintenanceActions::RunElevatedEnableLongPaths(result);
	if (bSucceeded) {
		AddLogLine(false, GetResString(IDS_LONG_PATHS_ENABLE_SUCCESS));
		return;
	}

	if (result.bCancelled) {
		AddLogLine(false, GetResString(IDS_LONG_PATHS_ENABLE_CANCELLED));
		return;
	}

	CString strError(result.strErrorText);
	if (strError.IsEmpty())
		strError = GetErrorMessage(result.dwLastError != ERROR_SUCCESS ? result.dwLastError : result.dwExitCode);
	AddLogLine(true, GetResString(IDS_LONG_PATHS_ENABLE_FAILURE), (LPCTSTR)strError);
}

void CemuleDlg::ExcludeDownloadFoldersFromDefender()
{
	const std::vector<CString> paths = BuildDefenderDownloadFolderExclusions();
	if (paths.empty()) {
		AddLogLine(false, GetResString(IDS_DEFENDER_EXCLUSIONS_NO_PATHS));
		return;
	}

	WindowsMaintenanceActions::CMaintenanceLaunchResult result;
	CWaitCursor waitCursor;
	const bool bSucceeded = WindowsMaintenanceActions::RunElevatedDefenderExclusions(paths, result);
	if (bSucceeded) {
		AddLogLine(false, GetResString(IDS_DEFENDER_EXCLUSIONS_SUCCESS), static_cast<UINT>(paths.size()));
		return;
	}

	if (result.bCancelled) {
		AddLogLine(false, GetResString(IDS_DEFENDER_EXCLUSIONS_CANCELLED));
		return;
	}

	CString strError(result.strErrorText);
	if (strError.IsEmpty())
		strError = GetErrorMessage(result.dwLastError != ERROR_SUCCESS ? result.dwLastError : result.dwExitCode);
	AddLogLine(true, GetResString(IDS_DEFENDER_EXCLUSIONS_FAILURE), (LPCTSTR)strError);
}

void CemuleDlg::RegisterProwlarrIntegration()
{
	ElevatedPowerShellAction::CLaunchResult result;
	CString strArguments;
	strArguments.Format(
		_T("-EmulebbBaseUrl %s -EmulebbApiKey %s"),
		(LPCTSTR)ElevatedPowerShellAction::QuotePowerShellArgument(BuildLocalEmulebbWebBaseUrl()),
		(LPCTSTR)ElevatedPowerShellAction::QuotePowerShellArgument(thePrefs.GetWSApiKey()));
	if (LaunchBundledInteractiveScript(_T("register-prowlarr.ps1"), strArguments, result)) {
		AddLogLine(false, _T("Started eMuleBB Prowlarr registration script."));
		return;
	}

	CString strError(result.strErrorText);
	if (strError.IsEmpty())
		strError = GetErrorMessage(result.dwLastError);
	AddLogLine(true, _T("Failed to start eMuleBB Prowlarr registration script: %s"), (LPCTSTR)strError);
}

void CemuleDlg::RegisterArrIntegration(LPCTSTR pszTargetName)
{
	ElevatedPowerShellAction::CLaunchResult result;
	CString strArguments;
	strArguments.Format(
		_T("-Target %s -EmulebbBaseUrl %s -EmulebbApiKey %s"),
		(LPCTSTR)ElevatedPowerShellAction::QuotePowerShellArgument(pszTargetName),
		(LPCTSTR)ElevatedPowerShellAction::QuotePowerShellArgument(BuildLocalEmulebbWebBaseUrl()),
		(LPCTSTR)ElevatedPowerShellAction::QuotePowerShellArgument(thePrefs.GetWSApiKey()));
	if (LaunchBundledInteractiveScript(_T("register-arr-stack.ps1"), strArguments, result)) {
		AddLogLine(false, _T("Started eMuleBB %s registration script."), pszTargetName);
		return;
	}

	CString strError(result.strErrorText);
	if (strError.IsEmpty())
		strError = GetErrorMessage(result.dwLastError);
	AddLogLine(true, _T("Failed to start eMuleBB %s registration script: %s"), pszTargetName, (LPCTSTR)strError);
}


void CemuleDlg::ApplyHyperTextFont(LPLOGFONT pFont)
{
	theApp.m_fontHyperText.DeleteObject();
	if (theApp.m_fontHyperText.CreateFontIndirect(pFont)) {
		thePrefs.SetHyperTextFont(pFont);
		chatwnd->chatselector.UpdateFonts(&theApp.m_fontHyperText);
		ircwnd->UpdateFonts(&theApp.m_fontHyperText);
	}
}

void CemuleDlg::ApplyLogFont(LPLOGFONT pFont)
{
	theApp.m_fontLog.DeleteObject();
	if (theApp.m_fontLog.CreateFontIndirect(pFont)) {
		thePrefs.SetLogFont(pFont);
		serverwnd->servermsgbox->SetFont(&theApp.m_fontLog);
		serverwnd->logbox->SetFont(&theApp.m_fontLog);
		serverwnd->debuglog->SetFont(&theApp.m_fontLog);
	}
}

LRESULT CemuleDlg::OnPeerPreviewFinished(WPARAM wParam, LPARAM lParam)
{
	CKnownFile *pOwner = reinterpret_cast<CKnownFile*>(wParam);
	PeerPreviewResult_Struct *result = reinterpret_cast<PeerPreviewResult_Struct*>(lParam);
	if (result == NULL)
		return 0;

	if (IsLiveKnownFilePointer(pOwner))
	{
		pOwner->PeerPreviewFinished(result->imgResults, result->nImagesGrabbed, result->pSender);
		result->ReleaseFrames();
	}
	else if (theApp.clientlist != NULL && theApp.clientlist->ContainsClientPointer(result->pSender))
		result->pSender->SendPreviewAnswer(NULL, NULL, 0);
	else
		ASSERT(0);

	delete result;
	return 0;
}

static void StraightWindowStyles(CWnd *pWnd)
{
	for (CWnd *pWndChild = pWnd->GetWindow(GW_CHILD); pWndChild != NULL; pWndChild = pWndChild->GetNextWindow())
		StraightWindowStyles(pWndChild);

	TCHAR szClassName[MAX_PATH];
	if (::GetClassName(*pWnd, szClassName, _countof(szClassName))) {
		if (_tcsicmp(szClassName, _T("Button")) == 0)
			pWnd->ModifyStyle(BS_FLAT, 0);
		else if (_tcsicmp(szClassName, _T("EDIT")) == 0 && (pWnd->GetExStyle() & WS_EX_STATICEDGE)
			|| _tcsicmp(szClassName, _T("SysListView32")) == 0
			|| _tcsicmp(szClassName, _T("msctls_trackbar32")) == 0)
		{
			pWnd->ModifyStyleEx(WS_EX_STATICEDGE, WS_EX_CLIENTEDGE);
		}
		//else if (_tcsicmp(szClassName, _T("SysTreeView32")) == 0)
		//	pWnd->ModifyStyleEx(WS_EX_STATICEDGE, WS_EX_CLIENTEDGE);
	}
}

static void ApplySystemFont(CWnd *pWnd)
{
	for (CWnd *pWndChild = pWnd->GetWindow(GW_CHILD); pWndChild != NULL; pWndChild = pWndChild->GetNextWindow())
		ApplySystemFont(pWndChild);

	TCHAR szClassName[MAX_PATH];
	if (::GetClassName(*pWnd, szClassName, _countof(szClassName))
		&& (_tcsicmp(szClassName, _T("SysListView32")) == 0 || _tcsicmp(szClassName, _T("SysTreeView32")) == 0))
	{
		pWnd->SendMessage(WM_SETFONT, NULL, FALSE);
	}
}

static bool s_bIsXPStyle;

static void FlatWindowStyles(CWnd *pWnd)
{
	for (CWnd *pWndChild = pWnd->GetWindow(GW_CHILD); pWndChild != NULL; pWndChild = pWndChild->GetNextWindow())
		FlatWindowStyles(pWndChild);

	TCHAR szClassName[MAX_PATH];
	if (GetClassName(*pWnd, szClassName, _countof(szClassName)))
		if (_tcsicmp(szClassName, _T("Button")) == 0) {
			if (!s_bIsXPStyle || (pWnd->GetStyle() & BS_ICON) == 0)
				pWnd->ModifyStyle(0, BS_FLAT);
		} else if (_tcsicmp(szClassName, _T("SysListView32")) == 0 || _tcsicmp(szClassName, _T("SysTreeView32")) == 0)
			pWnd->ModifyStyleEx(WS_EX_CLIENTEDGE, WS_EX_STATICEDGE);
}

void InitWindowStyles(CWnd *pWnd)
{
	//ApplySystemFont(pWnd);
	if (thePrefs.GetStraightWindowStyles() < 0)
		return;
	if (thePrefs.GetStraightWindowStyles() > 0)
		/*StraightWindowStyles(pWnd)*/;	// no longer needed
	else {
		s_bIsXPStyle = ::IsAppThemed() && ::IsThemeActive();
		if (!s_bIsXPStyle)
			FlatWindowStyles(pWnd);
	}
}

struct SStartupProgressWindowCleanupContext
{
	HWND hMainWnd;
	CString strTitle;
};

static BOOL CALLBACK DestroyOrphanedStartupProgressWindowProc(HWND hWnd, LPARAM lParam)
{
	SStartupProgressWindowCleanupContext *pContext = reinterpret_cast<SStartupProgressWindowCleanupContext*>(lParam);
	if (pContext == NULL || hWnd == NULL || hWnd == pContext->hMainWnd)
		return TRUE;
	if (::GetDlgItem(hWnd, IDC_SHUTDOWN_STEP) == NULL || ::GetDlgItem(hWnd, IDC_PROGRESS1) == NULL)
		return TRUE;

	TCHAR szTitle[256] = {};
	(void)::GetWindowText(hWnd, szTitle, _countof(szTitle));
	if (pContext->strTitle.Compare(szTitle) != 0)
		return TRUE;

	::ShowWindow(hWnd, SW_HIDE);
	(void)::DestroyWindow(hWnd);
	return TRUE;
}

static void DestroyOrphanedStartupProgressWindows(HWND hMainWnd)
{
	SStartupProgressWindowCleanupContext context = { hMainWnd, GetResString(IDS_STARTING_EMULE) };
	if (context.strTitle.IsEmpty())
		return;

	(void)::EnumThreadWindows(::GetCurrentThreadId(), DestroyOrphanedStartupProgressWindowProc, reinterpret_cast<LPARAM>(&context));
}

bool CemuleDlg::ShouldShowLifecycleProgressDialog(int iMode, bool bStartup) const
{
	switch (CPreferences::NormalizeLifecycleProgressDialogMode(iMode)) {
	case LPDM_ALWAYS:
		return true;
	case LPDM_WHEN_VISIBLE:
		return bStartup ? !m_bStartMinimized : IsWindowVisible() != FALSE;
	default:
		return false;
	}
}

void CemuleDlg::ShowStartupProgress()
{
	if (m_bStartupProgressFinished)
		return;
	if (m_pStartupProgressDlg != NULL)
		return;
	if (!ShouldShowLifecycleProgressDialog(thePrefs.GetStartupProgressDialogMode(), true)) {
		theApp.DestroyEarlyStartupProgress();
		return;
	}

	theApp.DestroyEarlyStartupProgress();

	CLifecycleProgressDlg *pDialog = NULL;
	try {
		pDialog = new CLifecycleProgressDlg(IDS_STARTING_EMULE, IDS_EMULE_IS_STARTING, this);
	} catch (...) {
		return;
	}
	if (pDialog->Create(IDD_SHUTDOWNPROGRESS, this)) {
		m_pStartupProgressDlg = pDialog;
		m_bTransientDialogActive = true;
		pDialog->CenterWindow();
		pDialog->ShowWindow(SW_SHOW);
		pDialog->SetPhase(2, GetResString(IDS_STARTUP_PROGRESS_STARTING), GetResString(IDS_STARTUP_PROGRESS_PREPARING), false);
		PumpLifecycleProgressMessages(pDialog);
#if EMULEBB_HAS_STARTUP_PROFILING
		theApp.AppendStartupProfileLine(_T("CemuleDlg::ShowStartupProgress"), 0);
#endif
	} else
		delete pDialog;
}

void CemuleDlg::UpdateStartupProgress(UINT uPercent, UINT uStepStringId, UINT uDetailStringId, bool bMarquee)
{
	if (m_bStartupProgressFinished)
		return;
	if (m_pStartupProgressDlg == NULL)
		return;

	static_cast<CLifecycleProgressDlg*>(m_pStartupProgressDlg)->SetPhase(uPercent, GetResString(uStepStringId), GetResString(uDetailStringId), bMarquee);
	PumpLifecycleProgressMessages(m_pStartupProgressDlg);
}

void CemuleDlg::DestroyStartupProgress()
{
	if (m_pStartupProgressDlg != NULL) {
#if EMULEBB_HAS_STARTUP_PROFILING
		const ULONGLONG ullPhaseStart = theApp.GetStartupProfileTimestampUs();
#endif
		if (m_pStartupProgressDlg->GetSafeHwnd() != NULL) {
			m_pStartupProgressDlg->ShowWindow(SW_HIDE);
			m_pStartupProgressDlg->DestroyWindow();
		}
		delete m_pStartupProgressDlg;
		m_pStartupProgressDlg = NULL;
		m_bTransientDialogActive = false;
		PumpLifecycleProgressMessages(NULL);
#if EMULEBB_HAS_STARTUP_PROFILING
		theApp.AppendStartupProfileLine(_T("CemuleDlg::DestroyStartupProgress"), theApp.GetStartupProfileElapsedUs(ullPhaseStart));
#endif
	}
}

void CemuleDlg::CloseStartupProgressIfRunning()
{
	if (!theApp.IsRunning())
		return;

	if (!m_bStartupProgressFinished) {
		// WHY: Startup progress is only valid before APP_STATE_RUNNING. If a nested
		// message pump or failed sequencer hop leaves it alive, close it on the next
		// normal UI turn instead of letting a stale lifecycle dialog cover the app.
		m_bStartupProgressFinished = true;
		theApp.DestroyEarlyStartupProgress();
	}
	DestroyStartupProgress();
	// WHY: a lost modeless progress pointer or missed queued startup hop can leave
	// the lifecycle dialog visible after APP_STATE_RUNNING. Sweep only same-thread
	// startup progress dialogs with the expected controls.
	DestroyOrphanedStartupProgressWindows(m_hWnd);
}

BOOL CemuleApp::IsIdleMessage(MSG *pMsg)
{
	// This function is closely related to 'CemuleDlg::OnKickIdle'.
	//
	// * See MFC source code for 'CWnd::RunModalLoop' to see how those functions are related
	//	 to each other.
	//
	// * See MFC documentation for 'CWnd::IsIdleMessage' to see why WM_TIMER messages are
	//	 filtered here.
	//
	// Generally we want to filter WM_TIMER messages because they are triggering idle
	// processing (e.g. cleaning up temp. MFC maps) and because they are occurring very often
	// in eMule (we have a rather high frequency timer in upload queue). To save CPU load but
	// do not miss the chance to cleanup MFC temp. maps and other stuff, we do not use each
	// occurring WM_TIMER message -- that would just be overkill! However, we can not simply
	// filter all WM_TIMER messages. If eMule is running in taskbar the only messages which
	// are received by main window are those WM_TIMER messages, thus those messages are the
	// only chance to trigger some idle processing. So, we must use at last some of those
	// messages because otherwise we would not do any idle processing at all in some cases.

	static ULONGLONG s_ullLastIdleMessage;
	if (pMsg->message == WM_TIMER) {
		// Allow this WM_TIMER message to trigger idle processing only if we did not do so
		// since some seconds.
		const ULONGLONG curTick = ::GetTickCount64();
		if (curTick >= s_ullLastIdleMessage + SEC2MS(5)) {
			s_ullLastIdleMessage = curTick;
			return TRUE;// Request idle processing (will send a WM_KICKIDLE)
		}
		return FALSE;	// No idle processing
	}

	if (!CWinApp::IsIdleMessage(pMsg))
		return FALSE;	// No idle processing

	s_ullLastIdleMessage = ::GetTickCount64();
	return TRUE;		// Request idle processing (will send a WM_KICKIDLE)
}

LRESULT CemuleDlg::OnKickIdle(WPARAM, LPARAM lIdleCount)
{
	LRESULT lResult = 0;
#if EMULEBB_HAS_STARTUP_PROFILING
	const ULONGLONG ullPhaseStart = theApp.GetStartupProfileTimestampUs();
	static bool s_bLoggedFirstKickIdle = false;
#endif

	if (m_bStartMinimized)
		PostStartupMinimized();

	if (searchwnd && searchwnd->m_hWnd) {
		if (!theApp.IsClosing()) {
			// NOTE: See also 'CemuleApp::IsIdleMessage'. If 'CemuleApp::IsIdleMessage'
			// would not filter most of the WM_TIMER messages we might get a performance
			// problem here because the idle processing would be performed very, very often.
			//
			// The default MFC implementation of 'CWinApp::OnIdle' is sufficient for us. We
			// will get called with 'lIdleCount=0' and with 'lIdleCount=1'.
			//
			// CWinApp::OnIdle(0)	takes care about pending MFC GUI stuff and returns 'TRUE'
			//						to request another invocation to perform more idle processing
			// CWinApp::OnIdle(>=1)	frees temporary internally MFC maps and returns 'FALSE'
			//						because no more idle processing is needed.
			lResult = theApp.OnIdle((LONG)lIdleCount);
		}
	}

	#if EMULEBB_HAS_STARTUP_PROFILING
	const ULONGLONG ullDuration = theApp.GetStartupProfileElapsedUs(ullPhaseStart);
	if (!s_bLoggedFirstKickIdle) {
		s_bLoggedFirstKickIdle = true;
		theApp.AppendStartupProfileLine(_T("CemuleDlg::OnKickIdle first"), ullDuration);
	} else if (ullDuration >= 250000) {
		CString strPhase;
		strPhase.Format(_T("CemuleDlg::OnKickIdle long (idle=%Id)"), lIdleCount);
		theApp.AppendStartupProfileLine(strPhase, ullDuration);
	}
	#endif

	return lResult;
}

int CemuleDlg::MapWindowToToolbarButton(CWnd *pWnd) const
{
	if (pWnd == transferwnd)
		return TBBTN_TRANSFERS;
	if (pWnd == serverwnd)
		return TBBTN_SERVER;
	if (pWnd == sharedfileswnd)
		return TBBTN_SHARED;
	if (pWnd == searchwnd)
		return TBBTN_SEARCH;
	if (pWnd == statisticswnd)
		return TBBTN_STATS;
	if (pWnd == kademliawnd)
		return TBBTN_KAD;
	if (pWnd == ircwnd)
		return TBBTN_IRC;
	if (pWnd == chatwnd)
		return TBBTN_MESSAGES;
	ASSERT(0);
	return -1;
}

CWnd* CemuleDlg::MapToolbarButtonToWindow(int iButtonID) const
{
	switch (iButtonID) {
	case TBBTN_TRANSFERS:
		return transferwnd;
	case TBBTN_SERVER:
		return serverwnd;
	case TBBTN_SHARED:
		return sharedfileswnd;
	case TBBTN_SEARCH:
		return searchwnd;
	case TBBTN_STATS:
		return statisticswnd;
	case TBBTN_KAD:
		return kademliawnd;
	case TBBTN_IRC:
		return ircwnd;
	case TBBTN_MESSAGES:
		return chatwnd;
	}
	ASSERT(0);
	return NULL;
}

bool CemuleDlg::IsWindowToolbarButton(int iButtonID) const
{
	switch (iButtonID) {
	case TBBTN_TRANSFERS:
	case TBBTN_SERVER:
	case TBBTN_SHARED:
	case TBBTN_SEARCH:
	case TBBTN_STATS:
	case TBBTN_KAD:
	case TBBTN_IRC:
	case TBBTN_MESSAGES:
		return true;
	}
	return false;
}

int CemuleDlg::GetNextWindowToolbarButton(int iButtonID, int iDirection) const
{
	ASSERT(iDirection == 1 || iDirection == -1);
	int iButtonCount = toolbar->GetButtonCount();
	if (iButtonCount > 0) {
		int iButtonIdx = toolbar->CommandToIndex(iButtonID);
		if (iButtonIdx >= 0 && iButtonIdx < iButtonCount) {
			int iEvaluatedButtons = 0;
			while (iEvaluatedButtons < iButtonCount) {
				iButtonIdx = iButtonIdx + iDirection;
				if (iButtonIdx < 0)
					iButtonIdx = iButtonCount - 1;
				else if (iButtonIdx >= iButtonCount)
					iButtonIdx = 0;

				TBBUTTON tbbt = {};
				if (toolbar->GetButton(iButtonIdx, &tbbt)) {
					if (IsWindowToolbarButton(tbbt.idCommand))
						return tbbt.idCommand;
				}
				++iEvaluatedButtons;
			}
		}
	}
	return -1;
}

BOOL CemuleDlg::PreTranslateMessage(MSG *pMsg)
{
	BOOL bResult = CTrayDialog::PreTranslateMessage(pMsg);

	// Handle Ctrl+Tab and Ctrl+Shift+Tab
	if (pMsg->message == WM_KEYDOWN)
		if (pMsg->wParam == VK_TAB && GetKeyState(VK_CONTROL) < 0) {
			int iButtonID = MapWindowToToolbarButton(activewnd);
			if (iButtonID != -1) {
				int iNextButtonID = GetNextWindowToolbarButton(iButtonID, GetKeyState(VK_SHIFT) < 0 ? -1 : 1);
				if (iNextButtonID != -1) {
					CWnd *pWndNext = MapToolbarButtonToWindow(iNextButtonID);
					if (pWndNext) {
						SetActiveDialog(pWndNext);
						return TRUE;
					}
				}
			}
		}

	return bResult;
}

void CemuleDlg::CreateToolbarCmdIconMap()
{
	m_mapTbarCmdToIcon[TBBTN_CONNECT] = _T("Connect");
	m_mapTbarCmdToIcon[TBBTN_KAD] = _T("Kademlia");
	m_mapTbarCmdToIcon[TBBTN_SERVER] = _T("Server");
	m_mapTbarCmdToIcon[TBBTN_TRANSFERS] = _T("Transfer");
	m_mapTbarCmdToIcon[TBBTN_SEARCH] = _T("Search");
	m_mapTbarCmdToIcon[TBBTN_SHARED] = _T("SharedFiles");
	m_mapTbarCmdToIcon[TBBTN_MESSAGES] = _T("Messages");
	m_mapTbarCmdToIcon[TBBTN_IRC] = _T("IRC");
	m_mapTbarCmdToIcon[TBBTN_STATS] = _T("Statistics");
	m_mapTbarCmdToIcon[TBBTN_OPTIONS] = _T("Preferences");
	m_mapTbarCmdToIcon[TBBTN_TOOLS] = _T("Tools");
	m_mapTbarCmdToIcon[TBBTN_HELP] = _T("Help");
}

LPCTSTR CemuleDlg::GetIconFromCmdId(UINT uId)
{
	LPCTSTR pszIconId;
	return m_mapTbarCmdToIcon.Lookup(uId, pszIconId) ? pszIconId : NULL;
}

BOOL CemuleDlg::OnChevronPushed(UINT id, LPNMHDR pNMHDR, LRESULT *plResult)
{
	UNREFERENCED_PARAMETER(id);
	if (!thePrefs.GetUseReBarToolbar())
		return FALSE;

	NMREBARCHEVRON *pnmrc = (NMREBARCHEVRON*)pNMHDR;

	ASSERT(id == AFX_IDW_REBAR);
	ASSERT(pnmrc->uBand == 0);
	ASSERT(pnmrc->wID == 0);
	ASSERT(!m_mapTbarCmdToIcon.IsEmpty());

	// get visible area of rebar/toolbar
	CRect rcVisibleButtons;
	toolbar->GetClientRect(&rcVisibleButtons);

	// search the first toolbar button which is not fully visible
	int iButtons = toolbar->GetButtonCount();
	int i = 0;
	for (; i < iButtons; ++i) {
		RECT rcButton;
		toolbar->GetItemRect(i, &rcButton);

		CRect rcVisible;
		if (!rcVisible.IntersectRect(&rcVisibleButtons, &rcButton) || !::EqualRect(&rcButton, rcVisible))
			break;
	}

	// create menu for all toolbar buttons which are not (fully) visible
	BOOL bLastMenuItemIsSep = TRUE;
	CTitledMenu menu;
	menu.CreatePopupMenu();
	menu.AddMenuTitle(_T("eMuleBB"), true);

	TCHAR szString[256];
	TBBUTTONINFO tbbi;
	tbbi.cbSize = (UINT)sizeof tbbi;
	tbbi.dwMask = TBIF_BYINDEX | TBIF_COMMAND | TBIF_STYLE | TBIF_STATE | TBIF_TEXT;
	tbbi.cchText = _countof(szString);
	tbbi.pszText = szString;

	for (; i < iButtons; ++i)
		if (toolbar->GetButtonInfo(i, &tbbi) >= 0)
			if (tbbi.fsStyle & TBSTYLE_SEP) {
				if (!bLastMenuItemIsSep)
					bLastMenuItemIsSep = menu.AppendMenu(MF_SEPARATOR, 0, (LPCTSTR)NULL);
			} else if (*szString && menu.AppendMenu(MF_STRING, tbbi.idCommand, AddMainShellShortcutLabel(szString, tbbi.idCommand), GetIconFromCmdId(tbbi.idCommand))) {
				bLastMenuItemIsSep = FALSE;
				if (tbbi.fsState & TBSTATE_CHECKED)
					menu.CheckMenuItem(tbbi.idCommand, MF_BYCOMMAND | MF_CHECKED);
				if ((tbbi.fsState & TBSTATE_ENABLED) == 0)
					menu.EnableMenuItem(tbbi.idCommand, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
			}

	CPoint ptMenu(pnmrc->rc.left, pnmrc->rc.top);
	ClientToScreen(&ptMenu);
	ptMenu.y += rcVisibleButtons.Height();
	menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON | TPM_RIGHTBUTTON, ptMenu.x, ptMenu.y, this);
	*plResult = 1;
	return FALSE;
}

bool CemuleDlg::IsPreferencesDlgOpen() const
{
	return preferenceswnd->m_hWnd != NULL;
}

INT_PTR CemuleDlg::ShowPreferences(UINT uStartPageID)
{
	if (IsPreferencesDlgOpen()) {
		preferenceswnd->SetForegroundWindow();
		preferenceswnd->BringWindowToTop();
		return -1;
	}
	if (uStartPageID != UINT_MAX)
		preferenceswnd->SetStartPage(uStartPageID);
	return preferenceswnd->DoModal();
}



//////////////////////////////////////////////////////////////////
// Web Server related

LRESULT CemuleDlg::OnWebAddDownloads(WPARAM wParam, LPARAM lParam)
{
	const CString link((LPCTSTR)wParam);
	if (link.GetLength() == 32 && _tcsnicmp(link, _T("ed2k"), 4) != 0) {
		uchar fileid[MDX_DIGEST_SIZE];
		if (DecodeBase16(link, link.GetLength(), fileid, _countof(fileid)))
			theApp.searchlist->AddFileToDownloadByHash(fileid, (uint8)lParam);
		else
			DebugLogWarning(_T("Web Interface ignored invalid remote download hash '%s'"), (LPCTSTR)link.Left(64));
	} else
		theApp.AddEd2kLinksToDownload(link, (int)lParam);

	return 0;
}

LRESULT CemuleDlg::OnAddRemoveFriend(WPARAM wParam, LPARAM lParam)
{
	if (lParam) // add
		theApp.friendlist->AddFriend(reinterpret_cast<CUpDownClient*>(wParam));
	else		// remove
		theApp.friendlist->RemoveFriend(reinterpret_cast<CFriend*>(wParam));

	return 0;
}

LRESULT CemuleDlg::OnWebSetCatPrio(WPARAM wParam, LPARAM lParam)
{
	theApp.downloadqueue->SetCatPrio((UINT)wParam, (uint8)lParam);
	return 0;
}

LRESULT CemuleDlg::OnPartFileDisplayUpdate(WPARAM wParam, LPARAM)
{
	std::unique_ptr<CPartFileDisplayUpdateRequest> pRequest = TakePostedDisplayRefreshRequest<CPartFileDisplayUpdateRequest>(wParam);
	if (pRequest.get() == NULL)
		return 0;

	CPartFile *pPartFile = theApp.downloadqueue != NULL ? theApp.downloadqueue->GetFileByID(pRequest->fileHash) : NULL;
	const bool bForce = pRequest->force;

	if (pPartFile != NULL)
		pPartFile->DispatchQueuedDisplayUpdate(bForce);
	return 0;
}

LRESULT CemuleDlg::OnClientDisplayUpdate(WPARAM wParam, LPARAM)
{
	std::unique_ptr<CClientDisplayUpdateRequest> pRequest = TakePostedDisplayRefreshRequest<CClientDisplayUpdateRequest>(wParam);
	if (pRequest.get() == NULL)
		return 0;

	CUpDownClient *pClient = ResolveQueuedClient(*pRequest);
	const bool bForce = pRequest->force;

	if (pClient != NULL)
		pClient->DispatchQueuedDisplayUpdate(bForce);
	return 0;
}

LRESULT CemuleDlg::OnPartFileProgressUpdate(WPARAM wParam, LPARAM)
{
	std::unique_ptr<CPartFileProgressUpdateRequest> pRequest = TakePostedDisplayRefreshRequest<CPartFileProgressUpdateRequest>(wParam);
	if (pRequest.get() == NULL)
		return 0;

	CPartFile *pPartFile = theApp.downloadqueue != NULL ? theApp.downloadqueue->GetFileByID(pRequest->fileHash) : NULL;
	if (pPartFile != NULL
		&& IsCompatibleKnownFileProgressOwner(true, pRequest->fileSize, static_cast<uint64>(pPartFile->GetFileSize())))
	{
		pPartFile->SetFileOpProgress(static_cast<WPARAM>(pRequest->progress));
		pPartFile->UpdateDisplayedInfo(true);
	}

	return 0;
}

LRESULT CemuleDlg::OnStartupCacheSaveComplete(WPARAM wParam, LPARAM lParam)
{
	void *pCompletion = CSharedFileList::TakeStartupCacheSaveCompletion(wParam);
	if (pCompletion == NULL && lParam != 0)
		pCompletion = reinterpret_cast<void*>(lParam);
	if (pCompletion == NULL)
		return 0;
	if (theApp.sharedfiles != NULL)
		theApp.sharedfiles->HandleStartupCacheSaveCompletion(pCompletion);
	else
		CSharedFileList::DiscardStartupCacheSaveCompletion(pCompletion);
	return 0;
}

LRESULT CemuleDlg::OnWebServerClearCompleted(WPARAM wParam, LPARAM lParam)
{
	if (wParam) {
		uchar *pFileHash = reinterpret_cast<uchar*>(lParam);
		CPartFile *file = theApp.downloadqueue != NULL ? theApp.downloadqueue->GetFileByID(pFileHash) : NULL;
		if (file)
			transferwnd->GetDownloadList()->RemoveFile(file);
		delete[] pFileHash;
	} else
		transferwnd->GetDownloadList()->ClearCompleted(static_cast<int>(lParam));

	return 0;
}

LRESULT CemuleDlg::OnWebServerFileRename(WPARAM wParam, LPARAM lParam)
{
	reinterpret_cast<CPartFile*>(wParam)->SetFileName((LPCTSTR)lParam);
	reinterpret_cast<CPartFile*>(wParam)->SavePartFile();
	reinterpret_cast<CPartFile*>(wParam)->UpdateDisplayedInfo();
	sharedfileswnd->sharedfilesctrl.UpdateFile(reinterpret_cast<CKnownFile*>(wParam));

	return 0;
}

void CemuleDlg::ApplyDesktopUiRefreshIntervalMs(UINT uIntervalMs)
{
	thePrefs.SetDesktopUiRefreshIntervalMs(uIntervalMs);
	StartTransferRateDisplayTimer();
	if (transferwnd != NULL)
		transferwnd->RefreshTransferDisplayRefreshState(false);
}

void CemuleDlg::RunDesktopPresentationTick()
{
	const UINT uDesktopRefreshIntervalMs = thePrefs.GetDesktopUiRefreshIntervalMs();
	if (ShouldRefreshRoutineDesktopPresentation(theApp.IsClosing(), IsWindowVisible() != FALSE, uDesktopRefreshIntervalMs)) {
		if (transferwnd != NULL) {
			transferwnd->RefreshTransferDisplayRefreshState(false);
			transferwnd->FlushVisibleDisplayRefreshes();
		}
		if (sharedfileswnd != NULL)
			sharedfileswnd->sharedfilesctrl.FlushDisplayRefreshes();
		ShowTransferRate();
	} else if (ShouldRefreshPausedTitlePresentation(theApp.IsClosing(), IsWindowVisible() != FALSE, uDesktopRefreshIntervalMs))
		ShowTransferRate(false, true);
}

LRESULT CemuleDlg::OnWebRestApiCommand(WPARAM, LPARAM lParam)
{
	WebServerJson::RunDispatchedCommand(reinterpret_cast<void*>(lParam));
	return 0;
}

LRESULT CemuleDlg::OnWebGUIInteraction(WPARAM wParam, LPARAM lParam)
{

	switch (wParam) {
	case WEBGUIIA_UPDATEMYINFO:
		serverwnd->UpdateMyInfo();
		break;
	case WEBGUIIA_WINFUNC:
		if (thePrefs.GetWebAdminAllowedHiLevFunc()) {
			try {
				struct SScopedTokenHandle
				{
					HANDLE hToken;
					SScopedTokenHandle()
						: hToken(NULL)
					{
					}
					~SScopedTokenHandle()
					{
						if (hToken != NULL)
							VERIFY(::CloseHandle(hToken));
					}
				} token;
				TOKEN_PRIVILEGES tkp;	// Get a token for this process.

				if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token.hToken))
					throw 0; //parameterless throw not allowed here
				LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
				tkp.PrivilegeCount = 1;  // one privilege to set
				tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;	// Get the shutdown privilege for this process.
				AdjustTokenPrivileges(token.hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES)NULL, 0);

				if (lParam == 1) // shutdown
					ExitWindowsEx(EWX_SHUTDOWN | EWX_FORCE, 0);
				else if (lParam == 2)
					ExitWindowsEx(EWX_REBOOT | EWX_FORCE, 0);
			} catch (...) {
				AddLogLine(true, GetResString(IDS_WEB_REBOOT) + _T(' ') + GetResString(IDS_FAILED));
			}
		} else
			AddLogLine(true, GetResString(IDS_WEB_REBOOT) + _T(' ') + GetResString(IDS_ACCESSDENIED));
		break;
	case WEBGUIIA_UPD_CATTABS:
		theApp.emuledlg->transferwnd->UpdateCatTabTitles();
		break;
	case WEBGUIIA_UPD_SFUPDATE:
		if (lParam)
			theApp.sharedfiles->UpdateFile((CKnownFile*)lParam);
		break;
	case WEBGUIIA_UPDATESERVER:
		serverwnd->serverlistctrl.RefreshServer((CServer*)lParam);
		break;
	case WEBGUIIA_STOPCONNECTING:
		theApp.serverconnect->StopConnectionTry();
		break;
	case WEBGUIIA_CONNECTTOSERVER:
		if (!lParam)
			theApp.serverconnect->ConnectToAnyServer();
		else
			theApp.serverconnect->ConnectToServer(reinterpret_cast<CServer*>(lParam), false, false, true);
		break;
	case WEBGUIIA_DISCONNECT:
		AddLogLine(false, GetResString(IDS_DISCONNECT_SOFT_STACK_NOTICE));
		theApp.serverconnect->StopConnectionTry();
		if (lParam != 2)	// !KAD
			theApp.serverconnect->Disconnect();
		if (lParam != 1)	// !ED2K
			Kademlia::CKademlia::Stop();
		ShowConnectionState();
		break;
	case WEBGUIIA_SERVER_REMOVE:
		serverwnd->serverlistctrl.RemoveServer(reinterpret_cast<CServer*>(lParam));
		break;
	case WEBGUIIA_SHARED_FILES_RELOAD:
		if (sharedfileswnd != NULL)
			(void)sharedfileswnd->Reload(false);
		else if (theApp.sharedfiles != NULL) {
			if (theApp.sharedfiles->HasSharedHashingWork())
				AddLogLine(false, GetResString(IDS_SF_RELOADDEFERRED));
			else
				theApp.sharedfiles->Reload();
		}
		break;
	case WEBGUIIA_ADD_TO_STATIC:
		serverwnd->serverlistctrl.StaticServerFileAppend(reinterpret_cast<CServer*>(lParam));
		break;
	case WEBGUIIA_REMOVE_FROM_STATIC:
		serverwnd->serverlistctrl.StaticServerFileRemove(reinterpret_cast<CServer*>(lParam));
		break;
	case WEBGUIIA_UPDATESERVERMETFROMURL:
		return theApp.emuledlg->serverwnd->UpdateServerMetFromURL((TCHAR*)lParam);
	case WEBGUIIA_UPDATENODESDATFROMURL:
		return theApp.emuledlg->kademliawnd->UpdateNodesDatFromURL((TCHAR*)lParam);
	case WEBGUIIA_SHOWSTATISTICS:
		theApp.emuledlg->statisticswnd->ShowStatistics(lParam != 0);
		break;
	case WEBGUIIA_DELETEALLSEARCHES:
		theApp.emuledlg->searchwnd->DeleteAllSearches();
		break;
	case WEBGUIIA_KAD_BOOTSTRAP:
		if (!CanUseP2PConnectionCommands()) {
			LogP2PConnectionCommandBlocked();
			break;
		}
		{
			CString ip((LPCTSTR)lParam);
			int pos = ip.Find(_T(':'));
			if (pos >= 0) {
				uint16 port = (uint16)_tstoi(CPTR(ip, pos + 1));
				ip.Truncate(pos);
				Kademlia::CKademlia::Bootstrap(ip, port);
			}
		}
		break;
	case WEBGUIIA_KAD_START:
		Kademlia::CKademlia::Start();
		ShowConnectionState();
		break;
	case WEBGUIIA_KAD_STOP:
		Kademlia::CKademlia::Stop();
		ShowConnectionState();
		break;
	case WEBGUIIA_KAD_RCFW:
		Kademlia::CKademlia::RecheckFirewalled();
		ShowConnectionState();
	}

	return 0;
}

void CemuleDlg::TrayMinimizeToTrayChange()
{
	CMenu *pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL) {
		if (!thePrefs.GetMinToTray()) {
			// just for safety, ensure that we are not adding duplicate menu entries
			if (pSysMenu->EnableMenuItem(MP_MINIMIZETOTRAY, MF_BYCOMMAND | MF_ENABLED) == UINT_MAX) {
				ASSERT((MP_MINIMIZETOTRAY & 0xFFF0) == MP_MINIMIZETOTRAY && MP_MINIMIZETOTRAY < 0xF000);
				VERIFY(pSysMenu->InsertMenu(SC_MINIMIZE, MF_BYCOMMAND, MP_MINIMIZETOTRAY, GetResString(IDS_PW_TRAY)));
			} else
				ASSERT(0);
		} else
			(void)pSysMenu->RemoveMenu(MP_MINIMIZETOTRAY, MF_BYCOMMAND);
	}
	CTrayDialog::TrayMinimizeToTrayChange();
	UpdateTrayVisibility();
}

void CemuleDlg::SetToolTipsDelay(UINT uMilliseconds)
{
	//searchwnd->SetToolTipsDelay(uMilliseconds);
	transferwnd->SetToolTipsDelay(uMilliseconds);
	sharedfileswnd->SetToolTipsDelay(uMilliseconds);
}

void CALLBACK CemuleDlg::UPnPTimeOutTimer(HWND /*hwnd*/, UINT /*uiMsg*/, UINT_PTR idEvent, DWORD /*dwTime*/) noexcept
{
	const UINT_PTR uLiveTimerId = theApp.emuledlg != NULL ? theApp.emuledlg->m_hUPnPTimeOutTimer : 0;
	if (!Win32CallbackTimerSeams::ShouldDispatchUPnPTimeoutTimer(theApp.emuledlg != NULL, theApp.IsClosing(), uLiveTimerId, idEvent))
		return;
	// WHY: null-window timer callbacks can already be queued when KillTimer
	// clears m_hUPnPTimeOutTimer and a fallback backend starts. Only the current
	// live timer may stop the current NAT backend; stale callbacks belong to an
	// older attempt and must be ignored.
	theApp.emuledlg->PostMessage(UM_UPNP_RESULT, (WPARAM)CUPnPImpl::UPNP_TIMEOUT, 0);
}

LRESULT CemuleDlg::OnUPnPResult(WPARAM wParam, LPARAM lParam)
{
	CUPnPImpl::SResultMessage *pPostedResult = reinterpret_cast<CUPnPImpl::SResultMessage*>(lParam);
	bool bWasRefresh = pPostedResult != NULL && pPostedResult->bWasRefresh;
	int nResult = pPostedResult != NULL ? pPostedResult->nResult : static_cast<int>(wParam);
	CUPnPImpl *impl = theApp.m_pUPnPFinder != NULL ? theApp.m_pUPnPFinder->GetImplementation() : NULL;
	if (pPostedResult != NULL && pPostedResult->pImplementation != impl) {
		// WHY: asynchronous NAT discovery results can arrive after the wrapper
		// has failed over to another backend. Applying an old backend result to
		// the active backend can stop/delete the wrong mappings or report success
		// for a backend which never completed.
		delete pPostedResult;
		return 0;
	}
	delete pPostedResult;
	if (impl == NULL)
		return 0;

//>>> WiZaRd - handle "UPNP_TIMEOUT" events!
	if (!bWasRefresh && nResult != CUPnPImpl::UPNP_OK) {
		//just to be sure, stop any running services and also delete the forwarded ports (if necessary)
		if (nResult == CUPnPImpl::UPNP_TIMEOUT) {
			impl->StopAsyncFind();
			if (!impl->MustAbandonDiscoveryOwner())
				impl->DeletePorts();
		}
		DebugLogWarning(_T("NAT mapping backend '%s' did not complete successfully"), impl->GetImplementationName());
		// NAT mapping failed, check if we can retry it with another backend
		if (theApp.m_pUPnPFinder->SwitchImplentation()) {
			// WHY: fallback is a fresh backend attempt. Reusing the expired timer
			// from the previous backend can immediately fire another timeout, while
			// leaving no timer at all would hide a stuck fallback discovery.
			VERIFY(Win32CallbackTimerSeams::StopNullWindowCallbackTimer(m_hUPnPTimeOutTimer) != Win32CallbackTimerSeams::ETimerStopResult::Failed);
			DebugLog(_T("Trying fallback NAT mapping backend '%s'"), theApp.m_pUPnPFinder->GetImplementation()->GetImplementationName());
			StartUPnP(false);
			return 0;
		}

		DebugLog(_T("No more available NAT mapping backends left"));
	}

	if (m_hUPnPTimeOutTimer != 0) {
		VERIFY(Win32CallbackTimerSeams::StopNullWindowCallbackTimer(m_hUPnPTimeOutTimer) != Win32CallbackTimerSeams::ETimerStopResult::Failed);
	}
	if (!bWasRefresh)
		if (nResult == CUPnPImpl::UPNP_OK) {
			Log(GetResString(IDS_UPNPSUCCESS), impl->GetUsedTCPPort(), impl->GetUsedUDPPort());
		} else
			LogWarning(GetResString(IDS_UPNPFAILED));

		if (theApp.IsRunning() && m_bConnectRequestDelayedForUPnP)
			StartConnection();

		return 0;
}

LRESULT CemuleDlg::OnPowerBroadcast(WPARAM wParam, LPARAM lParam)
{
	//DebugLog(_T("DEBUG:Power state change. wParam=%d lPararm=%ld"),wParam,lParam);
	switch (wParam) {
	case PBT_APMRESUMEAUTOMATIC:
		theApp.ResetStandbyOff();
		if (m_bEd2kSuspendDisconnect || m_bKadSuspendDisconnect) {
			DebugLog(_T("Reconnect after Power state change. wParam=%d lPararm=%ld"), wParam, lParam);
			RefreshUPnP(true);
			PostMessage(WM_SYSCOMMAND, MP_CONNECT, 0); // tell to connect. a sec later...
		}
		return TRUE; // message processed.
	case PBT_APMSUSPEND:
		DebugLog(_T("System is going is suspending operation, disconnecting. wParam=%d lPararm=%ld"), wParam, lParam);
		m_bEd2kSuspendDisconnect = theApp.serverconnect->IsConnected();
		m_bKadSuspendDisconnect = Kademlia::CKademlia::IsConnected();
		CloseConnection();
		return TRUE; // message processed.
	}
	return FALSE; // we do not process this message
}

void CemuleDlg::StartUPnP(bool bReset, uint16 nForceTCPPort, uint16 nForceUDPPort)
{
	if (theApp.IsStartupBindBlocked())
		return;

	if (theApp.m_pUPnPFinder != NULL && (m_hUPnPTimeOutTimer == 0 || !bReset)) {
		if (bReset) {
			LPCTSTR pszBackendMode = _T("Automatic");
			switch (thePrefs.GetUPnPBackendMode()) {
				case UPNP_BACKEND_IGD_ONLY:
					pszBackendMode = _T("UPnP IGD only");
					break;
				case UPNP_BACKEND_PCP_NATPMP_ONLY:
					pszBackendMode = _T("PCP/NAT-PMP only");
					break;
				case UPNP_BACKEND_AUTOMATIC:
				default:
					break;
			}
			theApp.m_pUPnPFinder->Reset();
			Log(GetResString(IDS_UPNPSETUP));
			DebugLog(_T("NAT mapping backend mode: %s"), pszBackendMode);
		}
		CString strImplementationName(_T("<unknown>"));
		try {
			CUPnPImpl *impl = theApp.m_pUPnPFinder->GetImplementation();
			if (impl != NULL)
				strImplementationName = impl->GetImplementationName();
			if (impl->IsReady()) {
				DebugLog(_T("Attempting NAT mapping backend '%s'"), impl->GetImplementationName());
				impl->SetMessageOnResult(this, UM_UPNP_RESULT);
				if (m_hUPnPTimeOutTimer == 0)
					VERIFY(Win32CallbackTimerSeams::TryStartNullWindowCallbackTimer(m_hUPnPTimeOutTimer, SEC2MS(40), UPnPTimeOutTimer));
				impl->StartDiscovery((nForceTCPPort ? nForceTCPPort : thePrefs.GetPort())
					, (nForceUDPPort ? nForceUDPPort : thePrefs.GetUDPPort())
					, (thePrefs.GetWSUseUPnP() ? thePrefs.GetWSPort() : 0));
			} else
				/*theApp.emuledlg->*/PostMessage(UM_UPNP_RESULT, (WPARAM)CUPnPImpl::UPNP_FAILED, 0);
		} catch (const CUPnPImpl::UPnPError&) {
			DebugLogWarning(_T("NAT mapping startup failed in backend '%s'"), (LPCTSTR)strImplementationName);
		} catch (CException *ex) {
			DebugLogWarning(_T("NAT mapping startup failed in backend '%s'%s"), (LPCTSTR)strImplementationName, (LPCTSTR)CExceptionStrDash(*ex));
			ex->Delete();
		}
	} else
		ASSERT(0);
}

void CemuleDlg::RefreshUPnP(bool bRequestAnswer)
{
	if (theApp.IsStartupBindBlocked())
		return;
	if (!thePrefs.IsUPnPEnabled())
		return;
	if (theApp.m_pUPnPFinder != NULL && m_hUPnPTimeOutTimer == 0) {
		CString strImplementationName(_T("<unknown>"));
		try {
			CUPnPImpl *impl = theApp.m_pUPnPFinder->GetImplementation();
			if (impl != NULL)
				strImplementationName = impl->GetImplementationName();
			if (impl->IsReady()) {
				if (bRequestAnswer)
					impl->SetMessageOnResult(this, UM_UPNP_RESULT);
				if (impl->CheckAndRefresh() && bRequestAnswer)
					VERIFY(Win32CallbackTimerSeams::TryStartNullWindowCallbackTimer(m_hUPnPTimeOutTimer, SEC2MS(10), UPnPTimeOutTimer));
				else
					impl->SetMessageOnResult(NULL, 0);
			} else
				DebugLogWarning(_T("RefreshUPnP, implementation not ready"));
		} catch (const CUPnPImpl::UPnPError&) {
			DebugLogWarning(_T("NAT mapping refresh failed in backend '%s'"), (LPCTSTR)strImplementationName);
		} catch (CException *ex) {
			DebugLogWarning(_T("NAT mapping refresh failed in backend '%s'%s"), (LPCTSTR)strImplementationName, (LPCTSTR)CExceptionStrDash(*ex));
			ex->Delete();
		}
	} else
		ASSERT(0);
}

void CemuleDlg::OnTimer(UINT_PTR nIDEvent)
{
	CloseStartupProgressIfRunning();

	if (nIDEvent == kTransferRateDisplayTimerId) {
		RunDesktopPresentationTick();
		return;
	}
	if (nIDEvent == kBindLossWatchdogTimerId) {
		CheckBindLossMonitor();
		CheckVpnGuardHttpMonitor(false);
		return;
	}
	__super::OnTimer(nIDEvent);
}

BOOL CemuleDlg::OnDeviceChange(UINT nEventType, DWORD_PTR dwData)
{
	// WM_DEVICECHANGE is sent for:
	//	Drives which where created/deleted with "SUBST" command (handled like network drives)
	//	Drives which where created/deleted as regular network drives.
	//
	// WM_DEVICECHANGE is *NOT* sent for:
	//	Floppy disk drives
	//	ZIP disk drives (although Windows Explorer recognises a changed media, we do not get a message)
	//	CD-ROM drives (although MSDN says different...)
	//
	if ((nEventType == DBT_DEVICEARRIVAL || nEventType == DBT_DEVICEREMOVECOMPLETE) && dwData != 0) {
		const DEV_BROADCAST_HDR *pHdr = reinterpret_cast<const DEV_BROADCAST_HDR*>(dwData);
		if (pHdr->dbch_size >= sizeof(DEV_BROADCAST_HDR)) {
#ifdef _DEBUG
			CString strMsg(nEventType == DBT_DEVICEARRIVAL ? _T("DBT_DEVICEARRIVAL") : _T("DBT_DEVICEREMOVECOMPLETE"));
#endif
			if (pHdr->dbch_devicetype == DBT_DEVTYP_VOLUME && pHdr->dbch_size >= sizeof(DEV_BROADCAST_VOLUME)) {
				const DEV_BROADCAST_VOLUME *pVol = reinterpret_cast<const DEV_BROADCAST_VOLUME*>(pHdr);
#ifdef _DEBUG
				strMsg += _T(" Volume");
				if (pVol->dbcv_flags & DBTF_MEDIA)
					strMsg += _T(" Media");
				if (pVol->dbcv_flags & DBTF_NET)
					strMsg += _T(" Net");
				if ((pVol->dbcv_flags & ~(DBTF_NET | DBTF_MEDIA)) != 0)
					strMsg.AppendFormat(_T(" flags=0x%08x"), pVol->dbcv_flags);
#endif
				bool bVolumesChanged = false;
				for (UINT uDrive = 0; uDrive <= 25; ++uDrive) {
					UINT uMask = 1 << uDrive;
					if (pVol->dbcv_unitmask & uMask) {
						DEBUG_ONLY(strMsg.AppendFormat(_T(" %c:"), _T('A') + uDrive));
						if (pVol->dbcv_flags & (DBTF_MEDIA | DBTF_NET))
							ClearVolumeInfoCache(uDrive);
						bVolumesChanged = true;
					}
				}
				if (bVolumesChanged && sharedfileswnd)
					sharedfileswnd->OnVolumesChanged();
			} else
				DEBUG_ONLY(strMsg.AppendFormat(_T(" devicetype=0x%08x"), pHdr->dbch_devicetype));

#ifdef _DEBUG
			TRACE(_T("CemuleDlg::OnDeviceChange: %s\n"), (LPCTSTR)strMsg);
#endif
		} else
			TRACE(_T("CemuleDlg::OnDeviceChange: nEventType=0x%08x  dwData=%p  dbch_size=0x%08x\n"), nEventType, reinterpret_cast<const void*>(dwData), pHdr->dbch_size);
	} else
		TRACE(_T("CemuleDlg::OnDeviceChange: nEventType=0x%08x  dwData=%p\n"), nEventType, reinterpret_cast<const void*>(dwData));
	return __super::OnDeviceChange(nEventType, dwData);
}

LRESULT CemuleDlg::OnDisplayChange(WPARAM, LPARAM)
{
	TrayReset();
	return 0;
}


//////////////////////////////////////////////////////////////////
// Taskbar integration goodies

#ifdef HAVE_WIN7_SDK_H
// update thumbbarbutton structs and add/update the GUI thumbbar
void CemuleDlg::UpdateThumbBarButtons(bool initialAddToDlg)
{
	if (!m_pTaskbarList)
		return;

	THUMBBUTTONMASK dwMask = THB_ICON | THB_FLAGS;
	for (int i = TBB_FIRST; i <= TBB_LAST; ++i) {
		m_thbButtons[i].dwMask = dwMask;
		m_thbButtons[i].iId = i;
		m_thbButtons[i].iBitmap = 0;
		m_thbButtons[i].dwFlags = THBF_DISMISSONCLICK;

		UINT uid;
		switch (i) {
		case TBB_CONNECT:
			m_thbButtons[i].hIcon = theApp.LoadIcon(_T("CONNECT"), 16, 16);
			uid = IDS_MAIN_BTN_CONNECT;
			if (theApp.IsConnected())
				m_thbButtons[i].dwFlags |= THBF_DISABLED;
			break;
		case TBB_DISCONNECT:
			m_thbButtons[i].hIcon = theApp.LoadIcon(_T("DISCONNECT"), 16, 16);
			uid = IDS_MAIN_BTN_DISCONNECT;
			if (!theApp.IsConnected())
				m_thbButtons[i].dwFlags |= THBF_DISABLED;
			break;
		case TBB_THROTTLE:
			m_thbButtons[i].hIcon = theApp.LoadIcon(_T("SPEEDMIN"), 16, 16);
			uid = IDS_PW_PA;
			break;
		case TBB_UNTHROTTLE:
			m_thbButtons[i].hIcon = theApp.LoadIcon(_T("SPEEDMAX"), 16, 16);
			uid = IDS_PW_UA;
			break;
		case TBB_PREFERENCES:
			m_thbButtons[i].hIcon = theApp.LoadIcon(_T("PREFERENCES"), 16, 16);
			uid = IDS_EM_PREFS;
			break;
		default:
			uid = 0;
		}
		// set tooltips in widechar
		if (uid) {
			CString tooltip(GetResNoAmp(uid));
			if (i == TBB_THROTTLE) {
				tooltip.Format(
					GetResString(IDS_SPEED_LIMIT_BOTH_FMT),
					10u,
					SpeedQuickActionsSeams::CalculatePercentLimitKiB(thePrefs.GetConfiguredMaxUpload(), 10u),
					(LPCTSTR)GetResString(IDS_KBYTESPERSEC),
					SpeedQuickActionsSeams::CalculatePercentLimitKiB(thePrefs.GetConfiguredMaxDownload(), 10u),
					(LPCTSTR)GetResString(IDS_KBYTESPERSEC));
			}
			wcscpy_s(m_thbButtons[i].szTip, _countof(m_thbButtons[i].szTip), tooltip);
			m_thbButtons[i].dwMask |= THB_TOOLTIP;
		}
	}

	if (initialAddToDlg)
		m_pTaskbarList->ThumbBarAddButtons(m_hWnd, ARRAYSIZE(m_thbButtons), m_thbButtons);
	else
		m_pTaskbarList->ThumbBarUpdateButtons(m_hWnd, ARRAYSIZE(m_thbButtons), m_thbButtons);

	// clean up icons, they were copied in the previous call
	for (int i = TBB_FIRST; i <= TBB_LAST; ++i)
		::DestroyIcon(m_thbButtons[i].hIcon);
}

// Handle thumbbar buttons
void CemuleDlg::OnTBBPressed(UINT id)
{
	switch (id) {
	case TBB_CONNECT:
		OnBnClickedConnect();
		break;
	case TBB_DISCONNECT:
		CloseConnection();
		break;
	case TBB_THROTTLE:
		QuickSpeedBoth(MP_QS_B10);
		break;
	case TBB_UNTHROTTLE:
		QuickSpeedOther(MP_QS_UA);
		break;
	case TBB_PREFERENCES:
		ShowPreferences();
	}
}

// When Windows tells us, the taskbar button was created, it is safe to initialize our taskbar stuff
LRESULT CemuleDlg::OnTaskbarBtnCreated(WPARAM, LPARAM)
{
	if (!theApp.IsClosing()) {
		if (m_pTaskbarList)
			m_pTaskbarList.Release();

		if (m_pTaskbarList.CoCreateInstance(CLSID_TaskbarList) == S_OK) {
			m_pTaskbarList->SetProgressState(m_hWnd, TBPF_NOPROGRESS);

			m_currentTBP_state = TBPF_NOPROGRESS;
			m_prevProgress = 0;
			m_ovlIcon = NULL;

			UpdateThumbBarButtons(true);
			UpdateStatusBarProgress();
		} else
			ASSERT(0);
	}
	return 0;
}

// Updates global progress and /down state overlay icon
// Overlay icon looks rather annoying than useful, so it's disabled by default for the common user and can be enabled by ini setting only (Ornis)
void CemuleDlg::EnableTaskbarGoodies(bool enable)
{
	if (m_pTaskbarList) {
		m_pTaskbarList->SetOverlayIcon(m_hWnd, NULL, _T(""));
		if (!enable) {
			m_pTaskbarList->SetProgressState(m_hWnd, TBPF_NOPROGRESS);
			m_currentTBP_state = TBPF_NOPROGRESS;
			m_prevProgress = 0;
			m_ovlIcon = NULL;
		} else
			UpdateStatusBarProgress();
	}
}

void CemuleDlg::UpdateStatusBarProgress()
{
	if (m_pTaskbarList && thePrefs.IsWin7TaskbarGoodiesEnabled()) {
		// calc global progress & status
		float finishedsize = theApp.emuledlg->transferwnd->GetDownloadList()->GetFinishedSize();
		float globalSize = theStats.m_fGlobalSize + finishedsize;

		if (globalSize == 0) {
			// if there is no download, disable progress
			if (m_currentTBP_state != TBPF_NOPROGRESS)
				m_currentTBP_state = TBPF_NOPROGRESS;
		} else {
			TBPFLAG new_state;
			if (theStats.m_dwOverallStatus & STATE_ERROROUS) // an error
				new_state = TBPF_ERROR;
			else if (theStats.m_dwOverallStatus & STATE_DOWNLOADING) // something downloading
				new_state = TBPF_NORMAL;
			else
				new_state = TBPF_PAUSED;

			if (new_state != m_currentTBP_state)
				m_currentTBP_state = new_state;

			float globalDone = theStats.m_fGlobalDone + finishedsize;
			float overallProgress = CalculateProgressRatio(globalDone, globalSize);
			if (overallProgress != m_prevProgress) {
				m_prevProgress = overallProgress;
				m_pTaskbarList->SetProgressValue(m_hWnd, (ULONGLONG)(overallProgress * 100), 100);
			}
		}
		m_pTaskbarList->SetProgressState(m_hWnd, m_currentTBP_state);

		// overlay up/down-speed
		if (thePrefs.IsShowUpDownIconInTaskbar()) {
			bool bUp = theApp.emuledlg->transferwnd->GetUploadList()->GetItemCount() > 0;
			bool bDown = theStats.m_dwOverallStatus & STATE_DOWNLOADING;

			HICON newicon;
			if (bUp && bDown)
				newicon = transicons[3];
			else if (bUp)
				newicon = transicons[2];
			else if (bDown)
				newicon = transicons[1];
			else
				newicon = NULL;

			if (m_ovlIcon != newicon) {
				m_ovlIcon = newicon;
				m_pTaskbarList->SetOverlayIcon(m_hWnd, m_ovlIcon, _T("eMuleBB Up/Down Indicator"));
			}
		}
	}
}
#endif

void CemuleDlg::SetTaskbarIconColor()
{
	bool bBrightTaskbarIconSpeed = false;
	bool bTransparent = false;
	COLORREF cr = RGB(0, 0, 0);
	if (thePrefs.IsRunningAeroGlassTheme()) {
		DWORD dwGlassColor = 0;
		BOOL bOpaque = FALSE;
		if (::DwmGetColorizationColor(&dwGlassColor, &bOpaque) == S_OK) {
			uint8 byAlpha = (uint8)(dwGlassColor >> 24);
			cr = 0xFFFFFF & dwGlassColor;
			if (byAlpha < 200 && !bOpaque) {
				// on transparent themes we can never figure out what exact color is shown
				// (if we could in real time?), but given that a color is blended against
				// the background, it is a good guess that a bright speedbar will be
				// the best solution in most cases
				bTransparent = true;
			}
		}
	} else if (::IsThemeActive() && ::IsAppThemed()) {
		CWnd tmpWnd;
		VERIFY(tmpWnd.Create(_T("STATIC"), _T("Tmp"), 0, CRect(0, 0, 10, 10), this, 1235));
		VERIFY(::SetWindowTheme(tmpWnd.GetSafeHwnd(), L"TrayNotifyHoriz", NULL) == S_OK);
		HTHEME hTheme = ::OpenThemeData(tmpWnd.GetSafeHwnd(), L"TrayNotify");
		if (hTheme != NULL) {
			VERIFY(SUCCEEDED(::GetThemeColor(hTheme, TNP_BACKGROUND, 0, TMT_FILLCOLORHINT, &cr)));
			::CloseThemeData(hTheme);
		} else
			ASSERT(0);
		tmpWnd.DestroyWindow();
	} else {
		DEBUG_ONLY(DebugLog(_T("Taskbar Notifier Color: ::GetSysColor() used")));
		cr = ::GetSysColor(COLOR_3DFACE);
	}
	uint8 iRed = GetRValue(cr);
	uint8 iBlue = GetBValue(cr);
	uint8 iGreen = GetGValue(cr);
	uint16 iBrightness = (uint16)sqrt(((iRed * iRed * 0.241f) + (iGreen * iGreen * 0.691f) + (iBlue * iBlue * 0.068f)));
	ASSERT(iBrightness <= 255);
	bBrightTaskbarIconSpeed = iBrightness < 132;
	DebugLog(_T("Taskbar Notifier Color: R:%u G:%u B:%u, Brightness: %u, Transparent: %s"), iRed, iGreen, iBlue, iBrightness, bTransparent ? _T("Yes") : _T("No"));
	thePrefs.SetStatsColor(11, ((bBrightTaskbarIconSpeed || bTransparent) ? RGB(255, 255, 255) : RGB(0, 0, 0)));
}
