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
#include "WebServerJson.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "ClientStateDefs.h"
#include "ClientList.h"
#include "ED2KLink.h"
#include "Emule.h"
#include "EmuleDlg.h"
#include "Friend.h"
#include "FriendList.h"
#include "Log.h"
#include "Mdump.h"
#include "OtherFunctions.h"
#include "PartFile.h"
#include "WebApiCommandSeams.h"
#include "WebApiSurfaceSeams.h"
#include "WebServerJsonSeams.h"
#include "Preferences.h"
#include "SearchDlg.h"
#include "SearchFile.h"
#include "SearchList.h"
#include "SearchParams.h"
#include "SearchResultsWnd.h"
#include "Server.h"
#include "ServerConnect.h"
#include "ServerList.h"
#include "ServerWnd.h"
#include "SharedDirectoryOps.h"
#include "SharedFileList.h"
#include "SharedFilesWnd.h"
#include "Statistics.h"
#include "StringConversion.h"
#include "TransferDlg.h"
#include "TransferWnd.h"
#include "UpDownClient.h"
#include "UploadQueue.h"
#include "UserMsgs.h"
#include "WebServer.h"
#include "DownloadQueue.h"
#include "Opcodes.h"
#include "kademlia/kademlia/Kademlia.h"
#include "kademlia/kademlia/UDPFirewallTester.h"

#pragma warning(push, 0)
#include <nlohmann/json.hpp>
#pragma warning(pop)

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

using json = nlohmann::json;

namespace
{
struct SPipeApiError
{
	CStringA strCode;
	CString strMessage;
};

struct SRestDispatchContext
{
	explicit SRestDispatchContext(const json &rRequest)
		: request(rRequest)
		, hComplete(::CreateEvent(NULL, TRUE, FALSE, NULL))
		, lRefCount(1)
		, lAbandoned(0)
	{
	}

	~SRestDispatchContext()
	{
		if (hComplete != NULL)
			VERIFY(::CloseHandle(hComplete));
	}

	void AddRef()
	{
		::InterlockedIncrement(&lRefCount);
	}

	void Release()
	{
		if (::InterlockedDecrement(&lRefCount) == 0)
			delete this;
	}

	json request;
	json result;
	SPipeApiError error;
	HANDLE hComplete;
	volatile LONG lRefCount;
	volatile LONG lAbandoned;

private:
	SRestDispatchContext(const SRestDispatchContext&);
	SRestDispatchContext& operator=(const SRestDispatchContext&);
};

class CScopedDownloadQueueBulkAdd
{
public:
	explicit CScopedDownloadQueueBulkAdd(CDownloadQueue *pDownloadQueue)
		: m_pDownloadQueue(pDownloadQueue)
	{
		if (m_pDownloadQueue != NULL)
			m_pDownloadQueue->BeginBulkAddDownloads();
	}

	~CScopedDownloadQueueBulkAdd()
	{
		if (m_pDownloadQueue != NULL)
			m_pDownloadQueue->EndBulkAddDownloads();
	}

private:
	CDownloadQueue *m_pDownloadQueue;
};

bool TryDecodeHash(const json &rValue, uchar *pOutHash, SPipeApiError &rError);

struct SPipeApiServerEndpoint
{
	CString strAddress;
	uint16 uPort;
};

struct SPipeApiClientSelector
{
	SPipeApiClientSelector()
		: bHasUserHash(false)
		, bHasEndpoint(false)
		, dwIp(0)
		, uPort(0)
	{
		md4clr(aucUserHash);
	}

	uchar aucUserHash[MDX_DIGEST_SIZE];
	bool bHasUserHash;
	bool bHasEndpoint;
	uint32 dwIp;
	uint16 uPort;
};

/**
 * Converts CString text to a UTF-8 std::string for JSON payloads.
 */
std::string StdUtf8FromCString(const CString &rText)
{
	const CStringA utf8(StrToUtf8(rText));
	return std::string((LPCSTR)utf8, utf8.GetLength());
}

/**
 * Converts a UTF-8 std::string into a Unicode CString.
 */
CString CStringFromStdUtf8(const std::string &rText)
{
	return OptUtf8ToStr(CStringA(rText.c_str(), static_cast<int>(rText.size())));
}

/**
 * Converts a narrow CString into a std::string without re-encoding.
 */
std::string StdStringFromCStringA(const CStringA &rText)
{
	return std::string((LPCSTR)rText, rText.GetLength());
}

/**
 * Normalizes a raw MD4 hash into the public 32-character lowercase hex form.
 */
CString HashToHex(const uchar *pHash)
{
	CString strHash(EncodeBase16(pHash, MDX_DIGEST_SIZE));
	strHash.MakeLower();
	return strHash;
}

/**
 * Returns a JSON hash string or null when the caller has no valid MD4.
 */
json JsonHashOrNull(const uchar *pHash)
{
	if (pHash == NULL || isnulmd4(pHash))
		return json(nullptr);
	return StdUtf8FromCString(HashToHex(pHash));
}

/**
 * Maps the existing CPartFile states to the stable transfer state names.
 */
CString GetTransferStateName(const CPartFile &rPartFile)
{
	switch (rPartFile.GetStatus()) {
	case PS_WAITINGFORHASH:
	case PS_HASHING:
		return _T("checking");
	case PS_ERROR:
		return _T("error");
	case PS_INSUFFICIENT:
		return _T("missing_files");
	case PS_PAUSED:
		return _T("paused");
	case PS_COMPLETING:
		return _T("completing");
	case PS_COMPLETE:
		return _T("completed");
	case PS_READY:
	case PS_EMPTY:
	default:
		if (rPartFile.IsStopped())
			return _T("paused");
		return rPartFile.GetTransferringSrcCount() > 0 ? _T("downloading") : _T("queued");
	}
}

/**
 * Maps the numeric download priority into the stable transfer vocabulary.
 */
CString GetTransferPriorityName(const CPartFile &rPartFile)
{
	if (rPartFile.IsAutoDownPriority())
		return _T("auto");

	switch (rPartFile.GetDownPriority()) {
	case PR_VERYLOW:
		return _T("veryLow");
	case PR_LOW:
		return _T("low");
	case PR_HIGH:
		return _T("high");
	case PR_VERYHIGH:
		return _T("veryHigh");
	case PR_NORMAL:
	default:
		return _T("normal");
	}
}

/**
 * Returns the stable public name for one download category.
 */
CString GetCategoryName(const UINT uCategory)
{
	if (uCategory == 0)
		return _T("Default");

	const Category_Struct *const pCategory = thePrefs.GetCategory(uCategory);
	CString strTitle(pCategory != NULL ? pCategory->strTitle : CString());
	strTitle.Trim();
	if (pCategory == NULL || strTitle.IsEmpty()) {
		CString strFallback;
		strFallback.Format(_T("Category %u"), uCategory);
		return strFallback;
	}

	return strTitle;
}

/**
 * Resolves a public category name to the matching configured category id.
 */
bool TryResolveCategoryName(const CString &rCategoryName, UINT &ruCategory)
{
	CString strName(rCategoryName);
	strName.Trim();
	if (strName.IsEmpty())
		return false;

	if (strName.CompareNoCase(_T("Default")) == 0 || strName.CompareNoCase(_T("All")) == 0) {
		ruCategory = 0;
		return true;
	}

	for (INT_PTR i = 1; i < thePrefs.GetCatCount(); ++i) {
		const Category_Struct *const pCategory = thePrefs.GetCategory(i);
		if (pCategory != NULL && pCategory->strTitle.CompareNoCase(strName) == 0) {
			ruCategory = static_cast<UINT>(i);
			return true;
		}
	}

	return false;
}

/**
 * Parses the optional paused flag used by REST download creation operations.
 */
bool TryParseOptionalDownloadPaused(const json &rParams, uint8 &ruPaused, SPipeApiError &rError)
{
	ruPaused = 2;
	if (!rParams.contains("paused"))
		return true;
	if (!rParams["paused"].is_boolean()) {
		rError.strCode = "INVALID_ARGUMENT";
		rError.strMessage = _T("paused must be a boolean");
		return false;
	}
	ruPaused = rParams["paused"].get<bool>() ? 1 : 0;
	return true;
}

/**
 * Serializes one configured download category for controller UIs.
 */
json BuildCategoryJson(const INT_PTR i)
{
	const Category_Struct *const pCategory = thePrefs.GetCategory(i);
	if (pCategory == NULL)
		return json();

	json item{
		{"id", static_cast<uint64_t>(i)},
		{"name", StdUtf8FromCString(GetCategoryName(static_cast<UINT>(i)))},
		{"path", pCategory->strIncomingPath.IsEmpty() ? json(nullptr) : json(StdUtf8FromCString(pCategory->strIncomingPath))},
		{"comment", StdUtf8FromCString(pCategory->strComment)},
		{"priority", pCategory->prio}
	};
	if (pCategory->color == CLR_NONE)
		item["color"] = nullptr;
	else
		item["color"] = static_cast<uint32_t>(pCategory->color);
	return item;
}

/**
 * Serializes the configured download category list for controller UIs.
 */
json BuildCategoriesJson()
{
	json result = json::array();
	for (INT_PTR i = 0; i < thePrefs.GetCatCount(); ++i) {
		const json item = BuildCategoryJson(i);
		if (!item.is_null())
			result.push_back(item);
	}
	return result;
}

/**
 * Maps the shared-file upload priority into the public API string.
 */
CString GetUploadPriorityName(const CKnownFile &rKnownFile)
{
	if (rKnownFile.IsAutoUpPriority())
		return _T("auto");

	switch (rKnownFile.GetUpPriority()) {
	case PR_VERYLOW:
		return _T("veryLow");
	case PR_LOW:
		return _T("low");
	case PR_HIGH:
		return _T("high");
	case PR_VERYHIGH:
		return _T("veryHigh");
	case PR_NORMAL:
	default:
		return _T("normal");
	}
}

/**
 * Maps the current upload state into a stable API string.
 */
CString GetUploadStateName(const CUpDownClient &rClient)
{
	return CString(WebApiSurfaceSeams::GetUploadStateName(static_cast<uint8_t>(rClient.GetUploadState())));
}

/**
 * Returns a JSON timestamp or null when the source value is not available.
 */
json JsonTimeOrNull(time_t value)
{
	return value > 0 ? json(static_cast<int64_t>(value)) : json(nullptr);
}

/**
 * Returns the most useful printable IP string for a source.
 */
CString GetClientIpString(const CUpDownClient &rClient)
{
	const uint32 dwIp = rClient.GetIP() != 0 ? rClient.GetIP() : rClient.GetConnectIP();
	return dwIp != 0 ? ipstr(dwIp) : CString();
}

/**
 * Serializes one server entry together with current connection flags.
 */
json BuildServerJson(const CServer &rServer)
{
	const CServer *const pCurrentServer = theApp.serverconnect->GetCurrentServer();
	const bool bIsCurrent = pCurrentServer == &rServer;
	const bool bConnected = bIsCurrent && theApp.serverconnect->IsConnected();
	const bool bConnecting = bIsCurrent && theApp.serverconnect->IsConnecting();
	return json{
		{"name", StdUtf8FromCString(rServer.GetListName())},
		{"address", StdUtf8FromCString(rServer.GetAddress())},
		{"port", rServer.GetPort()},
		{"ip", StdUtf8FromCString(rServer.GetIP() != 0 ? ipstr(rServer.GetIP()) : CString())},
		{"dynIp", StdUtf8FromCString(rServer.GetDynIP())},
		{"description", StdUtf8FromCString(rServer.GetDescription())},
		{"version", StdUtf8FromCString(rServer.GetVersion())},
		{"users", rServer.GetUsers()},
		{"files", rServer.GetFiles()},
		{"softFiles", rServer.GetSoftFiles()},
		{"hardFiles", rServer.GetHardFiles()},
		{"ping", rServer.GetPing()},
		{"failedCount", rServer.GetFailedCount()},
		{"priority", WebApiSurfaceSeams::GetServerPriorityName(rServer.GetPreference())},
		{"static", rServer.IsStaticMember()},
		{"current", bIsCurrent},
		{"connected", bConnected},
		{"connecting", bConnecting}
	};
}

/**
 * Serializes the current eD2K connection state and active server details.
 */
json BuildServerStatusJson()
{
	CServer *const pCurrentServer = theApp.serverconnect->GetCurrentServer();
	return json{
		{"connected", theApp.serverconnect->IsConnected()},
		{"connecting", theApp.serverconnect->IsConnecting()},
		{"lowId", theApp.serverconnect->IsConnected() ? json(theApp.serverconnect->IsLowID()) : json(nullptr)},
		{"serverCount", static_cast<int64_t>(theApp.serverlist->GetServerCount())},
		{"currentServer", pCurrentServer != NULL ? BuildServerJson(*pCurrentServer) : json(nullptr)}
	};
}

/**
 * Serializes the current Kad runtime status.
 */
json BuildKadStatusJson()
{
	const bool bRunning = Kademlia::CKademlia::IsRunning();
	const bool bConnected = Kademlia::CKademlia::IsConnected();
	const bool bBootstrapping = bRunning && !bConnected;
	return json{
		{"running", bRunning},
		{"connected", bConnected},
		{"firewalled", bConnected ? json(Kademlia::CKademlia::IsFirewalled()) : json(nullptr)},
		{"bootstrapping", bBootstrapping},
		{"bootstrapProgress", json(0)},
		{"users", bConnected ? json(Kademlia::CKademlia::GetKademliaUsers()) : json(nullptr)},
		{"files", bConnected ? json(Kademlia::CKademlia::GetKademliaFiles()) : json(nullptr)}
	};
}

/**
 * Serializes one part file into the stable transfer payload.
 */
json BuildTransferJson(const CPartFile &rPartFile)
{
	const time_t eta = rPartFile.getTimeRemaining();
	return json{
		{"hash", StdUtf8FromCString(HashToHex(rPartFile.GetFileHash()))},
		{"name", StdUtf8FromCString(rPartFile.GetFileName())},
		{"sizeBytes", static_cast<uint64>(rPartFile.GetFileSize())},
		{"completedBytes", static_cast<uint64>(rPartFile.GetCompletedSize())},
		{"progress", WebApiSurfaceSeams::BuildTransferProgressRatio(
			static_cast<uint64_t>(rPartFile.GetCompletedSize()),
			static_cast<uint64_t>(rPartFile.GetFileSize()))},
		{"state", StdUtf8FromCString(GetTransferStateName(rPartFile))},
		{"priority", StdUtf8FromCString(GetTransferPriorityName(rPartFile))},
		{"autoPriority", rPartFile.IsAutoDownPriority()},
		{"categoryId", const_cast<CPartFile&>(rPartFile).GetCategory()},
		{"categoryName", StdUtf8FromCString(GetCategoryName(const_cast<CPartFile&>(rPartFile).GetCategory()))},
		{"downloadSpeedKiBps", rPartFile.GetDatarate() / 1024.0},
		{"uploadSpeedKiBps", 0},
		{"sources", rPartFile.GetSourceCount()},
		{"sourcesTransferring", rPartFile.GetTransferringSrcCount()},
		{"eta", eta >= 0 ? json(static_cast<int64_t>(eta)) : json(nullptr)},
		{"addedAt", JsonTimeOrNull(rPartFile.GetCrFileDate())},
		{"completedAt", JsonTimeOrNull(rPartFile.GetStatus() == PS_COMPLETE ? rPartFile.GetFileDate() : static_cast<time_t>(-1))},
		{"partsTotal", rPartFile.GetPartCount()},
		{"partsAvailable", rPartFile.GetAvailablePartCount()},
		{"stopped", rPartFile.IsStopped()}
	};
}

/**
 * Serializes one active source for a download details request.
 */
json BuildSourceJson(const CUpDownClient &rClient)
{
	const CString strUserName(rClient.GetUserName() != NULL ? rClient.GetUserName() : _T(""));
	const CString strAddress(GetClientIpString(rClient));
	CString strClientId(HashToHex(rClient.GetUserHash()));
	if (strClientId.IsEmpty())
		strClientId.Format(_T("%s:%u"), static_cast<LPCTSTR>(strAddress), rClient.GetUserPort());
	return json{
		{"clientId", StdUtf8FromCString(strClientId)},
		{"userName", StdUtf8FromCString(strUserName)},
		{"userHash", StdUtf8FromCString(HashToHex(rClient.GetUserHash()))},
		{"clientSoftware", StdUtf8FromCString(rClient.GetClientSoftVer())},
		{"downloadState", StdUtf8FromCString(rClient.DbgGetDownloadState())},
		{"downloadSpeedKiBps", rClient.GetDownloadDatarate() / 1024.0},
		{"availableParts", rClient.GetAvailablePartCount()},
		{"partCount", rClient.GetPartCount()},
		{"address", StdUtf8FromCString(strAddress)},
		{"port", rClient.GetUserPort()},
		{"serverIp", StdUtf8FromCString(rClient.GetServerIP() != 0 ? ipstr(rClient.GetServerIP()) : CString())},
		{"serverPort", rClient.GetServerPort()},
		{"lowId", rClient.HasLowID()},
		{"queueRank", rClient.GetRemoteQueueRank()},
		{"viewSharedFiles", rClient.GetViewSharedFilesSupport()},
		{"sharedFilesRequestPending", rClient.GetFileListRequested() > 0}
	};
}

json BuildTransferSourcesJson(CPartFile &rPartFile)
{
	json result = json::array();
	for (POSITION pos = rPartFile.srclist.GetHeadPosition(); pos != NULL;)
		result.push_back(BuildSourceJson(*rPartFile.srclist.GetNext(pos)));
	return result;
}

json BuildTransferPartsJson(CPartFile &rPartFile)
{
	json parts = json::array();
	const uint64 uFileSize = static_cast<uint64>(rPartFile.GetFileSize());
	const UINT uPartCount = rPartFile.GetPartCount();
	for (UINT uPart = 0; uPart < uPartCount; ++uPart) {
		const uint64 uStart = static_cast<uint64>(uPart) * PARTSIZE;
		const uint64 uEndExclusive = min(uStart + static_cast<uint64>(PARTSIZE), uFileSize);
		if (uEndExclusive <= uStart)
			continue;

		const uint64 uEnd = uEndExclusive - 1;
		const uint64 uSize = uEnd - uStart + 1;
		const uint64 uGapBytes = rPartFile.GetTotalGapSizeInRange(uStart, uEnd);
		UINT uAvailableSources = 0;
		for (POSITION pos = rPartFile.srclist.GetHeadPosition(); pos != NULL;) {
			const CUpDownClient *const pClient = rPartFile.srclist.GetNext(pos);
			if (pClient != NULL && pClient->IsPartAvailable(uPart))
				++uAvailableSources;
		}

		parts.push_back(json{
			{"index", uPart},
			{"start", uStart},
			{"end", uEnd},
			{"size", uSize},
			{"completedBytes", uSize >= uGapBytes ? uSize - uGapBytes : 0},
			{"gapBytes", uGapBytes},
			{"complete", uGapBytes == 0},
			{"requested", rPartFile.IsAlreadyRequested(uStart, uEnd, true)},
			{"corrupted", rPartFile.IsCorruptedPart(uPart)},
			{"availableSources", uAvailableSources}
		});
	}
	return parts;
}

json BuildTransferDetailsJson(CPartFile &rPartFile)
{
	return json{
		{"transfer", BuildTransferJson(rPartFile)},
		{"parts", BuildTransferPartsJson(rPartFile)},
		{"sources", BuildTransferSourcesJson(rPartFile)}
	};
}

/**
 * Serializes one shared file with non-localized counters and share metadata.
 */
json BuildSharedFileJson(const CKnownFile &rKnownFile)
{
	const bool bSharedByRule = theApp.sharedfiles != NULL
		&& WebServerJsonSeams::BuildSharedByRuleFlag(theApp.sharedfiles->ShouldBeShared(rKnownFile.GetSharedDirectory(), NULL, false));

	return json{
		{"hash", StdUtf8FromCString(HashToHex(rKnownFile.GetFileHash()))},
		{"name", StdUtf8FromCString(rKnownFile.GetFileName())},
		{"path", StdUtf8FromCString(rKnownFile.GetFilePath())},
		{"directory", StdUtf8FromCString(rKnownFile.GetPath())},
		{"sizeBytes", static_cast<uint64>(rKnownFile.GetFileSize())},
		{"priority", StdUtf8FromCString(GetUploadPriorityName(rKnownFile))},
		{"autoUploadPriority", rKnownFile.IsAutoUpPriority()},
		{"requests", static_cast<int64_t>(rKnownFile.statistic.GetRequests())},
		{"acceptedRequests", static_cast<int64_t>(rKnownFile.statistic.GetAccepts())},
		{"transferredBytes", static_cast<uint64>(rKnownFile.statistic.GetTransferred())},
		{"allTimeRequests", static_cast<int64_t>(rKnownFile.statistic.GetAllTimeRequests())},
		{"allTimeAccepts", static_cast<int64_t>(rKnownFile.statistic.GetAllTimeAccepts())},
		{"allTimeTransferred", static_cast<uint64>(rKnownFile.statistic.GetAllTimeTransferred())},
		{"partCount", rKnownFile.GetPartCount()},
		{"partFile", rKnownFile.IsPartFile()},
		{"complete", !rKnownFile.IsPartFile()},
		{"comment", StdUtf8FromCString(const_cast<CKnownFile&>(rKnownFile).GetFileComment())},
		{"rating", const_cast<CKnownFile&>(rKnownFile).GetFileRating()},
		{"hasComment", rKnownFile.HasComment()},
		{"userRating", rKnownFile.UserRating()},
		{"publishedEd2k", rKnownFile.GetPublishedED2K()},
		{"sharedByRule", bSharedByRule}
	};
}

/**
 * Serializes one upload or waiting-queue client together with its file hash.
 */
json BuildUploadJson(const CUpDownClient &rClient, const bool bWaitingQueue)
{
	const CString strUserName(rClient.GetUserName() != NULL ? rClient.GetUserName() : _T(""));
	const uchar *const pUploadFileHash = rClient.GetUploadFileID();
	const CKnownFile *const pUploadFile = pUploadFileHash != NULL ? theApp.sharedfiles->GetFileByID(pUploadFileHash) : NULL;
	const CString strAddress(GetClientIpString(rClient));
	CString strClientId;
	if (rClient.HasValidHash())
		strClientId = HashToHex(rClient.GetUserHash());
	else
		strClientId.Format(_T("%s:%u"), static_cast<LPCTSTR>(strAddress), rClient.GetUserPort());
	return json{
		{"clientId", StdUtf8FromCString(strClientId)},
		{"userName", StdUtf8FromCString(strUserName)},
		{"userHash", JsonHashOrNull(rClient.HasValidHash() ? rClient.GetUserHash() : NULL)},
		{"clientSoftware", StdUtf8FromCString(rClient.GetClientSoftVer())},
		{"clientMod", StdUtf8FromCString(rClient.GetClientModVer())},
		{"uploadState", StdUtf8FromCString(GetUploadStateName(rClient))},
		{"uploadSpeedKiBps", rClient.GetUploadDatarate() / 1024.0},
		{"uploadedBytes", static_cast<uint64>(rClient.GetSessionUp())},
		{"queueSessionUploaded", static_cast<uint64>(rClient.GetQueueSessionPayloadUp())},
		{"payloadBuffered", static_cast<uint64>(rClient.GetPayloadInBuffer())},
		{"waitTimeMs", static_cast<uint64>(rClient.GetWaitTime())},
		{"waitStartedTick", static_cast<uint64>(rClient.GetWaitStartTime())},
		{"score", static_cast<int64_t>(rClient.GetScore(false, rClient.IsDownloading()))},
		{"address", StdUtf8FromCString(strAddress)},
		{"port", rClient.GetUserPort()},
		{"serverIp", StdUtf8FromCString(rClient.GetServerIP() != 0 ? ipstr(rClient.GetServerIP()) : CString())},
		{"serverPort", rClient.GetServerPort()},
		{"lowId", rClient.HasLowID()},
		{"friendSlot", rClient.GetFriendSlot()},
		{"uploading", rClient.IsDownloading()},
		{"waitingQueue", bWaitingQueue},
		{"requestedFileHash", JsonHashOrNull(pUploadFileHash)},
		{"requestedFileName", pUploadFile != NULL ? json(StdUtf8FromCString(pUploadFile->GetFileName())) : json(nullptr)},
		{"requestedFileSizeBytes", pUploadFile != NULL ? json(static_cast<uint64>(pUploadFile->GetFileSize())) : json(nullptr)}
	};
}

/**
 * Builds the shared global stats payload used by polling and push events.
 */
json BuildGlobalStatsJson()
{
	return json{
		{"connected", theApp.IsConnected()},
		{"downloadSpeedKiBps", theApp.downloadqueue->GetDatarate() / 1024.0},
		{"uploadSpeedKiBps", theApp.uploadqueue->GetDatarate() / 1024.0},
		{"sessionDownloadedBytes", theStats.sessionReceivedBytes},
		{"sessionUploadedBytes", theStats.sessionSentBytes},
		{"activeUploads", static_cast<int64_t>(theApp.uploadqueue->GetActiveUploadsCount())},
		{"waitingUploads", static_cast<int64_t>(theApp.uploadqueue->GetWaitingUserCount())},
		{"downloadCount", static_cast<int64_t>(theApp.downloadqueue->GetFileCount())},
		{"ed2kConnected", theApp.serverconnect->IsConnected()},
		{"ed2kHighId", theApp.serverconnect->IsConnected() && !theApp.serverconnect->IsLowID()},
		{"kadRunning", Kademlia::CKademlia::IsRunning()},
		{"kadConnected", Kademlia::CKademlia::IsConnected()},
		{"kadFirewalled", Kademlia::CKademlia::IsConnected() ? json(Kademlia::CKademlia::IsFirewalled()) : json(nullptr)}
	};
}

/**
 * Serializes the curated runtime preferences exposed over the local pipe.
 */
json BuildPreferencesJson()
{
	return json{
		{"uploadLimitKiBps", thePrefs.GetMaxUpload()},
		{"downloadLimitKiBps", thePrefs.GetMaxDownload()},
		{"maxConnections", thePrefs.GetMaxConnections()},
		{"maxConnectionsPerFiveSeconds", thePrefs.GetMaxConperFive()},
		{"maxSourcesPerFile", thePrefs.GetConfiguredMaxSourcesPerFile()},
		{"uploadClientDataRate", theApp.uploadqueue->GetTargetClientDataRateBroadband()},
		{"maxUploadSlots", thePrefs.GetMaxUploadClientsAllowed()},
		{"queueSize", static_cast<int64_t>(thePrefs.GetQueueSize())},
		{"autoConnect", thePrefs.DoAutoConnect()},
		{"newAutoUp", thePrefs.GetNewAutoUp()},
		{"newAutoDown", thePrefs.GetNewAutoDown()},
		{"creditSystem", thePrefs.UseCreditSystem()},
		{"safeServerConnect", thePrefs.IsSafeServerConnectEnabled()},
		{"networkKademlia", thePrefs.GetNetworkKademlia()},
		{"networkEd2k", thePrefs.GetNetworkED2K()}
	};
}

/**
 * Wraps redesigned collection responses in a forward-compatible envelope when
 * the resource route requested it.
 */
json ItemsEnvelopeIfRequested(const json &rParams, const json &rItems)
{
	if (!rParams.value("_items_envelope", false))
		return rItems;

	if (!rItems.is_array())
		return json{{"items", rItems}};

	if (!rParams.value("_paged_items_envelope", false))
		return json{{"items", rItems}};

	const int iTotal = static_cast<int>(rItems.size());
	const int iOffset = max(0, rParams.value("_offset", 0));
	const int iLimit = max(1, min(1000, rParams.value("_limit", 100)));
	json items = json::array();
	for (int i = iOffset; i < iTotal && static_cast<int>(items.size()) < iLimit; ++i)
		items.push_back(rItems[static_cast<size_t>(i)]);

	return json{
		{"items", items},
		{"total", iTotal},
		{"offset", iOffset},
		{"limit", iLimit}
	};
}

/**
 * Reports the compile-time build flavor for REST identity responses.
 */
const char *GetRestBuildFlavor()
{
#ifdef _DEBUG
	return "debug";
#else
	return "release";
#endif
}

/**
 * Serializes application identity and build metadata for the public API.
 */
json BuildAppJson(const char *pszBuildFlavor)
{
	return json{
		{"name", "eMule"},
		{"version", StdUtf8FromCString(theApp.m_strCurVersionLong)},
		{"apiVersion", "v1"},
		{"build", pszBuildFlavor},
		{"capabilities", json{
			{"transfers", true},
			{"searches", true},
			{"servers", true},
			{"sharedFiles", true},
			{"sharedDirectories", true},
			{"uploads", true},
			{"logs", true},
			{"categoriesRead", true},
			{"categoryAssignment", true},
			{"categoryCrud", true},
			{"renameFile", true},
			{"transferDetails", true},
			{"fileRatingComment", true},
			{"friends", true},
			{"peerControls", true}
		}},
#if defined(_M_ARM64)
		{"platform", "arm64"}
#else
		{"platform", "x64"}
#endif
	};
}

/**
 * Builds the complete shared-file collection without changing UI state.
 */
json BuildSharedFilesListJson()
{
	CKnownFilesMap sharedFiles;
	theApp.sharedfiles->CopySharedFileMap(sharedFiles);
	json result = json::array();
	for (const CKnownFilesMap::CPair *pair = sharedFiles.PGetFirstAssoc(); pair != NULL; pair = sharedFiles.PGetNextAssoc(pair)) {
		if (pair->value != NULL)
			result.push_back(BuildSharedFileJson(*pair->value));
	}
	return result;
}

/**
 * Builds one shared-directory row with stable flags for REST management UIs.
 */
json BuildSharedDirectoryRowJson(const CString &rDirectory, const CStringList &rMonitoredRoots, const CStringList &rMonitorOwnedDirs)
{
	return json{
		{"path", StdUtf8FromCString(rDirectory)},
		{"recursive", SharedDirectoryOps::ListContainsEquivalentPath(rMonitoredRoots, rDirectory)},
		{"monitorOwned", SharedDirectoryOps::ListContainsEquivalentPath(rMonitorOwnedDirs, rDirectory)},
		{"shareable", thePrefs.IsShareableDirectory(rDirectory)},
		{"accessible", DirAccsess(rDirectory)}
	};
}

/**
 * Builds the native shared-directory management model exposed over REST.
 */
json BuildSharedDirectoriesJson()
{
	CStringList sharedDirs;
	CStringList monitoredRoots;
	CStringList monitorOwnedDirs;
	thePrefs.CopySharedDirectoryList(sharedDirs);
	thePrefs.CopyMonitoredSharedRootList(monitoredRoots);
	thePrefs.CopyMonitorOwnedDirectoryList(monitorOwnedDirs);

	json roots = json::array();
	json items = json::array();
	json monitorOwned = json::array();

	for (POSITION pos = sharedDirs.GetHeadPosition(); pos != NULL;) {
		const CString strDirectory(sharedDirs.GetNext(pos));
		const json row = BuildSharedDirectoryRowJson(strDirectory, monitoredRoots, monitorOwnedDirs);
		items.push_back(row);
		if (!row.value("monitorOwned", false))
			roots.push_back(row);
	}

	for (POSITION pos = monitorOwnedDirs.GetHeadPosition(); pos != NULL;)
		monitorOwned.push_back(StdUtf8FromCString(monitorOwnedDirs.GetNext(pos)));

	return json{
		{"roots", roots},
		{"items", items},
		{"monitorOwned", monitorOwned},
		{"hashingCount", static_cast<int64_t>(theApp.sharedfiles->GetHashingCount())}
	};
}

/**
 * Builds the active upload or waiting queue collection for REST polling.
 */
json BuildUploadsListJson(const bool bWaitingQueue)
{
	json result = json::array();
	if (!bWaitingQueue) {
		for (POSITION pos = theApp.uploadqueue->GetFirstFromUploadList(); pos != NULL;) {
			CUpDownClient *const pClient = theApp.uploadqueue->GetNextFromUploadList(pos);
			if (pClient != NULL)
				result.push_back(BuildUploadJson(*pClient, false));
		}
	} else {
		for (POSITION pos = theApp.uploadqueue->GetFirstFromWaitingList(); pos != NULL;) {
			CUpDownClient *const pClient = theApp.uploadqueue->GetNextFromWaitingList(pos);
			if (pClient != NULL)
				result.push_back(BuildUploadJson(*pClient, true));
		}
	}
	return result;
}

json BuildFriendJson(const CFriend &rFriend)
{
	return json{
		{"userHash", rFriend.HasUserhash() ? json(StdUtf8FromCString(HashToHex(rFriend.m_abyUserhash))) : json(nullptr)},
		{"name", StdUtf8FromCString(rFriend.m_strName)},
		{"lastSeen", JsonTimeOrNull(rFriend.m_tLastSeen)},
		{"address", rFriend.m_dwLastUsedIP != 0 ? json(StdUtf8FromCString(ipstr(rFriend.m_dwLastUsedIP))) : json(nullptr)},
		{"port", rFriend.m_nLastUsedPort}
	};
}

json BuildFriendsListJson()
{
	CArray<CFriend*, CFriend*> friends;
	theApp.friendlist->CopyFriends(friends);
	json result = json::array();
	for (INT_PTR i = 0; i < friends.GetCount(); ++i) {
		if (friends[i] != NULL)
			result.push_back(BuildFriendJson(*friends[i]));
	}
	return result;
}

CFriend* FindFriendByHashParam(const json &rValue, SPipeApiError &rError)
{
	uchar hash[MDX_DIGEST_SIZE];
	if (!TryDecodeHash(rValue, hash, rError))
		return NULL;

	CFriend *const pFriend = theApp.friendlist->SearchFriend(hash, 0, 0);
	if (pFriend == NULL) {
		rError.strCode = "NOT_FOUND";
		rError.strMessage = _T("friend not found");
	}
	return pFriend;
}

/**
 * Builds recent log entries with a caller-bounded maximum length.
 */
json BuildLogEntriesJson(const size_t maxEntries)
{
	const std::vector<SRecentLogEntry> entries = GetRecentLogEntries(maxEntries);
	json result = json::array();
	for (const SRecentLogEntry &entry : entries) {
		CString strLevel(_T("info"));
		switch (entry.uFlags & LOGMSGTYPEMASK) {
		case LOG_WARNING:
			strLevel = _T("warning");
			break;
		case LOG_ERROR:
			strLevel = _T("error");
			break;
		case LOG_SUCCESS:
			strLevel = _T("success");
			break;
		default:
			break;
		}

		result.push_back(json{
			{"timestamp", static_cast<int64_t>(entry.time.GetTime())},
			{"message", StdUtf8FromCString(entry.strText)},
			{"level", StdUtf8FromCString(strLevel)},
			{"debug", (entry.uFlags & LOG_DEBUG) != 0}
		});
	}
	return result;
}

/**
 * Builds the transfer collection with the same filter semantics as the list
 * endpoint.
 */
json BuildTransfersListJson(const json &rParams, SPipeApiError &rError)
{
	WebApiCommandSeams::STransfersListRequest request;
	std::string strError;
	if (!WebApiCommandSeams::TryParseTransfersListRequest(rParams, request, strError)) {
		rError.strCode = "INVALID_ARGUMENT";
		rError.strMessage = CStringFromStdUtf8(strError);
		return json();
	}
	if (request.bHasCategory && request.uCategory >= thePrefs.GetCatCount()) {
		rError.strCode = "INVALID_ARGUMENT";
		rError.strMessage = _T("category is out of range");
		return json();
	}

	const CString strFilter(CStringFromStdUtf8(request.strFilterLower));
	json result = json::array();
	POSITION pos = NULL;
	for (INT_PTR i = 0, iCount = theApp.downloadqueue->GetFileCount(); i < iCount; ++i) {
		CPartFile *pPartFile = theApp.downloadqueue->GetFileNext(pos);
		if (pPartFile == NULL)
			break;
		if (request.bHasCategory && pPartFile->GetCategory() != request.uCategory)
			continue;
		if (!strFilter.IsEmpty()) {
			CString strName(pPartFile->GetFileName());
			strName.MakeLower();
			CString strState(GetTransferStateName(*pPartFile));
			strState.MakeLower();
			if (strName.Find(strFilter) < 0 && strState.Find(strFilter) < 0)
				continue;
		}
		result.push_back(BuildTransferJson(*pPartFile));
	}
	return result;
}

/**
 * Applies the curated mutable preferences and persists them through the
 * normal preferences save path.
 */
bool ApplyPreferencesJson(const json &rPrefs, SPipeApiError &rError)
{
	if (!rPrefs.is_object()) {
		rError.strCode = "INVALID_ARGUMENT";
		rError.strMessage = _T("prefs must be an object");
		return false;
	}

	for (json::const_iterator it = rPrefs.begin(); it != rPrefs.end(); ++it) {
		if (WebApiSurfaceSeams::ParseMutablePreferenceName(it.key().c_str()) == WebApiSurfaceSeams::EMutablePreference::Invalid) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage.Format(_T("unsupported preference key: %hs"), it.key().c_str());
			return false;
		}
	}

	if (rPrefs.contains("uploadLimitKiBps")) {
		uint64_t ullValue = 0;
		if (!WebApiCommandSeams::TryParseNonNegativeUInt64(rPrefs["uploadLimitKiBps"], ullValue) || !WebApiSurfaceSeams::IsFiniteKiBpsPreferenceValue(ullValue)) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("uploadLimitKiBps must be an unsigned number in the range 1..4294967294");
			return false;
		}
		thePrefs.SetMaxUpload(static_cast<uint32>(ullValue));
	}

	if (rPrefs.contains("downloadLimitKiBps")) {
		uint64_t ullValue = 0;
		if (!WebApiCommandSeams::TryParseNonNegativeUInt64(rPrefs["downloadLimitKiBps"], ullValue) || !WebApiSurfaceSeams::IsFiniteKiBpsPreferenceValue(ullValue)) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("downloadLimitKiBps must be an unsigned number in the range 1..4294967294");
			return false;
		}
		thePrefs.SetMaxDownload(static_cast<uint32>(ullValue));
	}

	if (rPrefs.contains("maxConnections")) {
		uint64_t ullValue = 0;
		if (!WebApiCommandSeams::TryParseNonNegativeUInt64(rPrefs["maxConnections"], ullValue) || !WebApiSurfaceSeams::IsPositiveSignedIntPreferenceValue(ullValue)) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("maxConnections must be an unsigned number in the range 1..2147483647");
			return false;
		}
		thePrefs.SetMaxConnections(static_cast<UINT>(ullValue));
	}

	if (rPrefs.contains("maxConnectionsPerFiveSeconds")) {
		uint64_t ullValue = 0;
		if (!WebApiCommandSeams::TryParseNonNegativeUInt64(rPrefs["maxConnectionsPerFiveSeconds"], ullValue) || !WebApiSurfaceSeams::IsPositiveSignedIntPreferenceValue(ullValue)) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("maxConnectionsPerFiveSeconds must be an unsigned number in the range 1..2147483647");
			return false;
		}
		thePrefs.SetMaxConsPerFive(static_cast<UINT>(ullValue));
	}

	if (rPrefs.contains("maxSourcesPerFile")) {
		uint64_t ullValue = 0;
		if (!WebApiCommandSeams::TryParseNonNegativeUInt64(rPrefs["maxSourcesPerFile"], ullValue) || !WebApiSurfaceSeams::IsPositiveSignedIntPreferenceValue(ullValue)) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("maxSourcesPerFile must be an unsigned number in the range 1..2147483647");
			return false;
		}
		thePrefs.SetMaxSourcesPerFile(static_cast<UINT>(ullValue));
	}

	if (rPrefs.contains("uploadClientDataRate")) {
		uint64_t ullRequestedRate = 0;
		if (!WebApiCommandSeams::TryParseNonNegativeUInt64(rPrefs["uploadClientDataRate"], ullRequestedRate) || ullRequestedRate == 0 || ullRequestedRate > UINT_MAX) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("uploadClientDataRate must be an unsigned number in the range 1..4294967295");
			return false;
		}

		const uint32 uBudgetBytesPerSec = max(3u * 1024u, thePrefs.GetMaxUpload() * 1024u);
		const uint32 uRequestedRate = static_cast<uint32>(ullRequestedRate);
		const uint32 uDerivedSlots = max(1u, min(32u, uBudgetBytesPerSec / max(1u, uRequestedRate)));
		thePrefs.SetMaxUploadClientsAllowed(uDerivedSlots);
	}

	if (rPrefs.contains("maxUploadSlots")) {
		uint64_t ullRequestedSlots = 0;
		if (!WebApiCommandSeams::TryParseNonNegativeUInt64(rPrefs["maxUploadSlots"], ullRequestedSlots) || !WebApiSurfaceSeams::IsUploadSlotPreferenceValue(ullRequestedSlots)) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("maxUploadSlots must be an unsigned number in the range 1..32");
			return false;
		}
		thePrefs.SetMaxUploadClientsAllowed(static_cast<UINT>(ullRequestedSlots));
	}

	if (rPrefs.contains("queueSize")) {
		uint64_t ullValue = 0;
		if (!WebApiCommandSeams::TryParseNonNegativeUInt64(rPrefs["queueSize"], ullValue) || !WebApiSurfaceSeams::IsQueueSizePreferenceValue(ullValue)) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("queueSize must be an unsigned number in the range 2000..10000");
			return false;
		}
		thePrefs.SetQueueSize(static_cast<INT_PTR>(ullValue));
	}

	if (rPrefs.contains("autoConnect")) {
		if (!rPrefs["autoConnect"].is_boolean()) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("autoConnect must be a boolean");
			return false;
		}
		thePrefs.SetAutoConnect(rPrefs["autoConnect"].get<bool>());
	}

	if (rPrefs.contains("newAutoUp")) {
		if (!rPrefs["newAutoUp"].is_boolean()) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("newAutoUp must be a boolean");
			return false;
		}
		thePrefs.SetNewAutoUp(rPrefs["newAutoUp"].get<bool>());
	}

	if (rPrefs.contains("newAutoDown")) {
		if (!rPrefs["newAutoDown"].is_boolean()) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("newAutoDown must be a boolean");
			return false;
		}
		thePrefs.SetNewAutoDown(rPrefs["newAutoDown"].get<bool>());
	}

	if (rPrefs.contains("creditSystem")) {
		if (!rPrefs["creditSystem"].is_boolean()) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("creditSystem must be a boolean");
			return false;
		}
		thePrefs.SetCreditSystem(rPrefs["creditSystem"].get<bool>());
	}

	if (rPrefs.contains("safeServerConnect")) {
		if (!rPrefs["safeServerConnect"].is_boolean()) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("safeServerConnect must be a boolean");
			return false;
		}
		thePrefs.SetSafeServerConnectEnabled(rPrefs["safeServerConnect"].get<bool>());
	}

	if (rPrefs.contains("networkKademlia")) {
		if (!rPrefs["networkKademlia"].is_boolean()) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("networkKademlia must be a boolean");
			return false;
		}
		thePrefs.SetNetworkKademlia(rPrefs["networkKademlia"].get<bool>());
	}

	if (rPrefs.contains("networkEd2k")) {
		if (!rPrefs["networkEd2k"].is_boolean()) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("networkEd2k must be a boolean");
			return false;
		}
		thePrefs.SetNetworkED2K(rPrefs["networkEd2k"].get<bool>());
	}

	thePrefs.Save();
	theApp.emuledlg->ShowTransferRate(true);
	theApp.emuledlg->ShowConnectionState();
	return true;
}

/**
 * Formats one internal search identifier for the public pipe payload.
 */
CString FormatSearchId(const uint32 uSearchID)
{
	CString strSearchId;
	strSearchId.Format(_T("%u"), uSearchID);
	return strSearchId;
}

/**
 * Parses the public decimal search id into the native uint32 identifier.
 */
bool TryGetSearchId(const json &rValue, uint32 &ruSearchID, SPipeApiError &rError)
{
	std::string strError;
	uint32_t uSearchID = 0;
	if (!WebApiCommandSeams::TryParseSearchId(rValue, uSearchID, strError)) {
		rError.strCode = "INVALID_ARGUMENT";
		rError.strMessage = CStringFromStdUtf8(strError);
		return false;
	}

	ruSearchID = static_cast<uint32>(uSearchID);
	return true;
}

/**
 * Maps search result known-file state into stable pipe vocabulary.
 */
CString GetSearchKnownTypeName(const CSearchFile &rSearchFile)
{
	switch (rSearchFile.GetKnownType()) {
	case CSearchFile::Shared:
		return _T("shared");
	case CSearchFile::Downloading:
		return _T("downloading");
	case CSearchFile::Downloaded:
		return _T("downloaded");
	case CSearchFile::Cancelled:
		return _T("cancelled");
	case CSearchFile::Unknown:
		return _T("unknown");
	case CSearchFile::NotDetermined:
	default:
		return _T("undetermined");
	}
}

/**
 * Formats the resolved native search transport for REST clients.
 */
const char* GetSearchMethodName(const ESearchType eType)
{
	switch (eType) {
	case SearchTypeEd2kServer:
		return "server";
	case SearchTypeEd2kGlobal:
		return "global";
	case SearchTypeKademlia:
		return "kad";
	case SearchTypeAutomatic:
	default:
		return "automatic";
	}
}

/**
 * Serializes one top-level search result entry into the pipe payload shape.
 */
json BuildSearchResultJson(const CSearchFile &rSearchFile, const SSearchParams *const pSearchParams = NULL)
{
	const int iComplete = rSearchFile.IsComplete();
	json result = json{
		{"searchId", StdUtf8FromCString(FormatSearchId(rSearchFile.GetSearchID()))},
		{"hash", StdUtf8FromCString(HashToHex(rSearchFile.GetFileHash()))},
		{"name", StdUtf8FromCString(rSearchFile.GetFileName())},
		{"sizeBytes", static_cast<uint64>(rSearchFile.GetFileSize())},
		{"fileType", StdUtf8FromCString(rSearchFile.GetFileType())},
		{"sources", rSearchFile.GetSourceCount()},
		{"completeSources", rSearchFile.GetCompleteSourceCount()},
		{"complete", iComplete >= 0 ? json(iComplete != 0) : json(nullptr)},
		{"knownType", StdUtf8FromCString(GetSearchKnownTypeName(rSearchFile))},
		{"directory", rSearchFile.GetDirectory() != NULL ? json(StdUtf8FromCString(rSearchFile.GetDirectory())) : json(nullptr)},
		{"clientIp", StdUtf8FromCString(rSearchFile.GetClientID() != 0 ? ipstr(rSearchFile.GetClientID()) : CString())},
		{"clientPort", rSearchFile.GetClientPort()},
		{"serverIp", StdUtf8FromCString(rSearchFile.GetClientServerIP() != 0 ? ipstr(rSearchFile.GetClientServerIP()) : CString())},
		{"serverPort", rSearchFile.GetClientServerPort()},
		{"clientCount", rSearchFile.GetClientsCount()},
		{"serverCount", rSearchFile.GetServers().GetSize()},
		{"kadPublishInfo", rSearchFile.GetKadPublishInfo()},
		{"rating", rSearchFile.UserRating()},
		{"hasComment", rSearchFile.HasComment()},
		{"spam", rSearchFile.IsConsideredSpam()}
	};
	if (pSearchParams != NULL)
		result["method"] = GetSearchMethodName(pSearchParams->eType);
	return result;
}

/**
 * Serializes one active search session without expanding its result rows.
 */
json BuildSearchSessionJson(const CSearchResultsWnd &rSearchResults, const SSearchParams &rSearchParams)
{
	CArray<const CSearchFile*, const CSearchFile*> aResults;
	const bool bHasVisibleResults = theApp.searchlist != NULL && theApp.searchlist->GetVisibleResults(rSearchParams.dwSearchID, aResults);
	return json{
		{"id", StdUtf8FromCString(FormatSearchId(rSearchParams.dwSearchID))},
		{"query", StdUtf8FromCString(rSearchParams.strExpression)},
		{"method", GetSearchMethodName(rSearchParams.eType)},
		{"status", rSearchResults.IsSearchRunning(rSearchParams.dwSearchID) ? "running" : "complete"},
		{"resultCount", bHasVisibleResults ? static_cast<int64_t>(aResults.GetCount()) : 0}
	};
}

/**
 * Builds the active native search-session collection for controller discovery.
 */
json BuildSearchSessionsJson(CSearchResultsWnd &rSearchResults)
{
	json result = json::array();
	TCITEM item = {};
	item.mask = TCIF_PARAM;
	for (int i = 0; i < rSearchResults.searchselect.GetItemCount(); ++i) {
		if (rSearchResults.searchselect.GetItem(i, &item) && item.lParam != NULL)
			result.push_back(BuildSearchSessionJson(rSearchResults, *reinterpret_cast<const SSearchParams*>(item.lParam)));
	}
	return result;
}

/**
 * Applies a validated rating/comment update to one shared file.
 */
bool TryApplySharedFileRatingComment(CKnownFile &rKnownFile, const json &rParams, SPipeApiError &rError)
{
	WebApiCommandSeams::SSharedFileRatingCommentRequest request;
	std::string strError;
	if (!WebApiCommandSeams::TryParseSharedFileRatingCommentRequest(rParams, request, strError)) {
		rError.strCode = "INVALID_ARGUMENT";
		rError.strMessage = CStringFromStdUtf8(strError);
		return false;
	}

	CString strComment(CStringFromStdUtf8(request.strComment));
	if (strComment.GetLength() > MAXFILECOMMENTLEN)
		strComment.Truncate(MAXFILECOMMENTLEN);

	rKnownFile.SetFileComment(strComment);
	rKnownFile.SetFileRating(static_cast<UINT>(request.iRating));
	rKnownFile.UpdateFileRatingCommentAvail(true);
	return true;
}

/**
 * Parses the upload-priority vocabulary accepted for shared files.
 */
bool TryGetSharedUploadPriorityParam(const json &rValue, uint8 &ruPriority, bool &rbAuto, SPipeApiError &rError)
{
	if (!rValue.is_string()) {
		rError.strCode = "INVALID_ARGUMENT";
		rError.strMessage = _T("priority must be a string");
		return false;
	}

	const std::string strPriority = WebServerJsonSeams::ToLowerAscii(rValue.get<std::string>());
	if (strPriority == "auto") {
		rbAuto = true;
		ruPriority = PR_HIGH;
		return true;
	}
	rbAuto = false;
	if (strPriority == "verylow") {
		ruPriority = PR_VERYLOW;
		return true;
	}
	if (strPriority == "low") {
		ruPriority = PR_LOW;
		return true;
	}
	if (strPriority == "normal") {
		ruPriority = PR_NORMAL;
		return true;
	}
	if (strPriority == "high") {
		ruPriority = PR_HIGH;
		return true;
	}
	if (strPriority == "veryhigh" || strPriority == "release") {
		ruPriority = PR_VERYHIGH;
		return true;
	}

	rError.strCode = "INVALID_ARGUMENT";
	rError.strMessage = _T("priority must be one of auto, veryLow, low, normal, high, veryHigh, release");
	return false;
}

/**
 * Serializes local known-file comment metadata as a stable comments collection.
 */
json BuildSharedFileCommentsJson(CKnownFile &rKnownFile)
{
	json comments = json::array();
	const CString strComment(rKnownFile.GetFileComment());
	const UINT uRating = rKnownFile.GetFileRating();
	if (!strComment.IsEmpty() || uRating != 0) {
		comments.push_back(json{
			{"source", "local"},
			{"userName", nullptr},
			{"fileName", StdUtf8FromCString(rKnownFile.GetFileName())},
			{"comment", StdUtf8FromCString(strComment)},
			{"rating", uRating}
		});
	}
	return comments;
}

/**
 * Locates a visible search result by search id and file hash.
 */
const CSearchFile* FindSearchResultByHash(const uint32 uSearchID, const uchar *pHash, SPipeApiError &rError)
{
	if (theApp.emuledlg == NULL || theApp.emuledlg->searchwnd == NULL || theApp.emuledlg->searchwnd->m_pwndResults == NULL || theApp.searchlist == NULL) {
		rError.strCode = "EMULE_UNAVAILABLE";
		rError.strMessage = _T("search window is not available");
		return NULL;
	}
	if (theApp.emuledlg->searchwnd->m_pwndResults->GetSearchResultsParams(uSearchID) == NULL) {
		rError.strCode = "NOT_FOUND";
		rError.strMessage = _T("search not found");
		return NULL;
	}

	CArray<const CSearchFile*, const CSearchFile*> aResults;
	if (!theApp.searchlist->GetVisibleResults(uSearchID, aResults)) {
		rError.strCode = "NOT_FOUND";
		rError.strMessage = _T("search not found");
		return NULL;
	}
	for (INT_PTR i = 0; i < aResults.GetCount(); ++i) {
		if (memcmp(aResults[i]->GetFileHash(), pHash, MDX_DIGEST_SIZE) == 0)
			return aResults[i];
	}

	rError.strCode = "NOT_FOUND";
	rError.strMessage = _T("search result not found");
	return NULL;
}

/**
 * Parses and validates a 32-character MD4 hash parameter.
 */
bool TryDecodeHash(const json &rValue, uchar *pOutHash, SPipeApiError &rError)
{
	if (!rValue.is_string()) {
		rError.strCode = "INVALID_ARGUMENT";
		rError.strMessage = _T("hash must be a 32-character lowercase hex string");
		return false;
	}

	const std::string strHashUtf8(rValue.get<std::string>());
	const CString strHash(CStringFromStdUtf8(strHashUtf8));
	if (!WebApiCommandSeams::IsLowercaseMd4HexString(strHashUtf8) || !DecodeBase16(strHash, strHash.GetLength(), pOutHash, MDX_DIGEST_SIZE)) {
		rError.strCode = "INVALID_ARGUMENT";
		rError.strMessage = _T("hash must be a valid 32-character lowercase hex string");
		return false;
	}

	return true;
}

/**
 * Resolves one shared file by public hash and emits a standard REST error.
 */
CKnownFile* FindSharedFileByHash(const json &rValue, SPipeApiError &rError)
{
	uchar hash[MDX_DIGEST_SIZE];
	if (!TryDecodeHash(rValue, hash, rError))
		return NULL;

	CKnownFile *const pKnownFile = theApp.sharedfiles->GetFileByID(hash);
	if (pKnownFile == NULL) {
		rError.strCode = "NOT_FOUND";
		rError.strMessage = _T("shared file not found");
	}
	return pKnownFile;
}

/**
 * Looks up a transfer by hash and populates a standard NOT_FOUND error when
 * the entry does not exist.
 */
CPartFile* FindPartFileByHash(const json &rValue, SPipeApiError &rError)
{
	uchar hash[MDX_DIGEST_SIZE];
	if (!TryDecodeHash(rValue, hash, rError))
		return NULL;

	CPartFile *pPartFile = theApp.downloadqueue->GetFileByID(hash);
	if (pPartFile == NULL) {
		rError.strCode = "NOT_FOUND";
		rError.strMessage = _T("transfer not found");
	}
	return pPartFile;
}

/**
 * Parses one non-empty UTF-8 path parameter from a pipe command.
 */
bool TryGetPathParam(const json &rValue, const char *pszFieldName, CString &rPath, SPipeApiError &rError)
{
	std::string strPath;
	std::string strError;
	if (!WebServerJsonSeams::TryParsePathText(rValue, pszFieldName, strPath, strError)) {
		rError.strCode = "INVALID_ARGUMENT";
		rError.strMessage = CStringFromStdUtf8(strError);
		return false;
	}

	rPath = CStringFromStdUtf8(strPath);
	return true;
}

/**
 * Parses one shared-directory root entry accepted by PATCH /shared-directories.
 */
bool TryParseSharedDirectoryRoot(const json &rValue, CString &rDirectory, bool &rbRecursive, SPipeApiError &rError)
{
	rbRecursive = false;
	const json *pPathValue = &rValue;
	if (rValue.is_object()) {
		pPathValue = rValue.contains("path") ? &rValue["path"] : NULL;
		if (rValue.contains("recursive")) {
			if (!rValue["recursive"].is_boolean()) {
				rError.strCode = "INVALID_ARGUMENT";
				rError.strMessage = _T("recursive must be a boolean");
				return false;
			}
			rbRecursive = rValue["recursive"].get<bool>();
		}
	}

	if (pPathValue == NULL || !TryGetPathParam(*pPathValue, "path", rDirectory, rError))
		return false;

	rDirectory = PathHelpers::CanonicalizeDirectoryPath(rDirectory);
	if (!thePrefs.IsShareableDirectory(rDirectory)) {
		rError.strCode = "INVALID_ARGUMENT";
		rError.strMessage = _T("path is not a shareable directory");
		return false;
	}

	return true;
}

/**
 * Parses and materializes the native shared-directory roots payload.
 */
bool TryBuildSharedDirectoryListsFromJson(
	const json &rParams,
	CStringList &rSharedDirs,
	CStringList &rMonitoredRoots,
	CStringList &rMonitorOwnedDirs,
	SPipeApiError &rError)
{
	if (!rParams.contains("roots") || !rParams["roots"].is_array()) {
		rError.strCode = "INVALID_ARGUMENT";
		rError.strMessage = _T("roots must be an array");
		return false;
	}

	rSharedDirs.RemoveAll();
	rMonitoredRoots.RemoveAll();
	rMonitorOwnedDirs.RemoveAll();

	for (const json &rootValue : rParams["roots"]) {
		CString strDirectory;
		bool bRecursive = false;
		if (!TryParseSharedDirectoryRoot(rootValue, strDirectory, bRecursive, rError))
			return false;

		(void)SharedDirectoryOps::AddSharedDirectory(rSharedDirs, strDirectory, bRecursive, [](const CString &rstrDirectory) -> bool {
			return thePrefs.IsShareableDirectory(rstrDirectory);
		});
		if (bRecursive) {
			if (!SharedDirectoryOps::ListContainsEquivalentPath(rMonitoredRoots, strDirectory))
				rMonitoredRoots.AddTail(strDirectory);
			SharedDirectoryOps::CollectDirectorySubtree(rMonitorOwnedDirs, strDirectory, false, [](const CString &rstrDirectory) -> bool {
				return thePrefs.IsShareableDirectory(rstrDirectory);
			});
		}
	}

	return true;
}

/**
 * Parses one public category id carried as a route token.
 */
bool TryGetCategoryIdParam(const json &rValue, UINT &ruCategory, SPipeApiError &rError)
{
	uint64_t uCategory = 0;
	const bool bParsed = rValue.is_string()
		? WebApiCommandSeams::TryParseUnsignedDecimalString(rValue.get<std::string>(), uCategory)
		: WebApiCommandSeams::TryParseNonNegativeUInt64(rValue, uCategory);
	if (!bParsed || uCategory > UINT_MAX) {
		rError.strCode = "INVALID_ARGUMENT";
		rError.strMessage = _T("category id must be an unsigned number");
		return false;
	}
	ruCategory = static_cast<UINT>(uCategory);
	return true;
}

/**
 * Parses a category priority value from the public string or numeric shape.
 */
bool TryGetCategoryPriorityParam(const json &rValue, UINT &ruPriority, SPipeApiError &rError)
{
	if (rValue.is_number_unsigned() || rValue.is_number_integer()) {
		uint64_t uPriority = 0;
		if (!WebApiCommandSeams::TryParseNonNegativeUInt64(rValue, uPriority) || uPriority > UINT_MAX) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("priority must be a supported priority value");
			return false;
		}
		ruPriority = static_cast<UINT>(uPriority);
		return true;
	}

	if (!rValue.is_string()) {
		rError.strCode = "INVALID_ARGUMENT";
		rError.strMessage = _T("priority must be a string or number");
		return false;
	}

	switch (WebApiSurfaceSeams::ParseTransferPriorityName(rValue.get_ref<const std::string&>().c_str())) {
	case WebApiSurfaceSeams::ETransferPriority::VeryLow:
		ruPriority = PR_VERYLOW;
		return true;
	case WebApiSurfaceSeams::ETransferPriority::Low:
		ruPriority = PR_LOW;
		return true;
	case WebApiSurfaceSeams::ETransferPriority::Normal:
		ruPriority = PR_NORMAL;
		return true;
	case WebApiSurfaceSeams::ETransferPriority::High:
		ruPriority = PR_HIGH;
		return true;
	case WebApiSurfaceSeams::ETransferPriority::VeryHigh:
		ruPriority = PR_VERYHIGH;
		return true;
	case WebApiSurfaceSeams::ETransferPriority::Auto:
	case WebApiSurfaceSeams::ETransferPriority::Invalid:
	default:
		rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("priority must be one of veryLow, low, normal, high, veryHigh");
		return false;
	}
}

/**
 * Applies the mutable core fields accepted by the category CRUD REST surface.
 */
bool ApplyCategoryCoreFields(Category_Struct &rCategory, const json &rParams, const bool bRequireName, bool &rbPathChanged, SPipeApiError &rError)
{
	rbPathChanged = false;
	if (bRequireName || rParams.contains("name")) {
		if (!rParams.contains("name") || !rParams["name"].is_string()) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("name must be a non-empty string");
			return false;
		}
		CString strName(CStringFromStdUtf8(rParams["name"].get<std::string>()));
		strName.Trim();
		if (strName.IsEmpty()) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("name must not be empty");
			return false;
		}
		rCategory.strTitle = strName;
	}

	if (rParams.contains("path")) {
		if (rParams["path"].is_null()) {
			rCategory.strIncomingPath = thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR);
			rbPathChanged = true;
		} else {
			CString strPath;
			if (!TryGetPathParam(rParams["path"], "path", strPath, rError))
				return false;
			strPath = PathHelpers::CanonicalizeDirectoryPath(strPath);
			if (!thePrefs.IsShareableDirectory(strPath)) {
				rError.strCode = "INVALID_ARGUMENT";
				rError.strMessage = _T("path is not a shareable directory");
				return false;
			}
			rbPathChanged = !EqualPaths(rCategory.strIncomingPath, strPath);
			rCategory.strIncomingPath = strPath;
		}
	}

	if (rParams.contains("comment")) {
		if (!rParams["comment"].is_string()) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("comment must be a string");
			return false;
		}
		rCategory.strComment = CStringFromStdUtf8(rParams["comment"].get<std::string>());
	}

	if (rParams.contains("color")) {
		if (rParams["color"].is_null())
			rCategory.color = CLR_NONE;
		else {
			uint64_t uColor = 0;
			if (!WebApiCommandSeams::TryParseNonNegativeUInt64(rParams["color"], uColor) || uColor > 0x00FFFFFFui64) {
				rError.strCode = "INVALID_ARGUMENT";
				rError.strMessage = _T("color must be null or an RGB integer");
				return false;
			}
			rCategory.color = static_cast<COLORREF>(uColor);
		}
	}

	if (rParams.contains("priority") && !TryGetCategoryPriorityParam(rParams["priority"], rCategory.prio, rError))
		return false;

	return true;
}

/**
 * Refreshes category tabs and dependent shared-file state after category CRUD.
 */
void RefreshCategoryUi(const bool bReloadSharedFiles)
{
	if (theApp.emuledlg == NULL)
		return;
	if (theApp.emuledlg->transferwnd != NULL)
		theApp.emuledlg->transferwnd->UpdateCatTabTitles(true);
	if (theApp.emuledlg->searchwnd != NULL)
		theApp.emuledlg->searchwnd->UpdateCatTabs();
	if (bReloadSharedFiles && theApp.emuledlg->sharedfileswnd != NULL)
		(void)theApp.emuledlg->sharedfileswnd->Reload(true);
}

/**
 * Persists shared-directory state and reloads the live shared-files model.
 */
void ApplySharedDirectoryLists(const CStringList &rSharedDirs, const CStringList &rMonitoredRoots, const CStringList &rMonitorOwnedDirs)
{
	thePrefs.ReplaceSharedDirectoryList(rSharedDirs);
	thePrefs.ReplaceMonitoredSharedRootList(rMonitoredRoots);
	thePrefs.ReplaceMonitorOwnedDirectoryList(rMonitorOwnedDirs);
	(void)thePrefs.Save();
	theApp.WakeSharedDirectoryMonitor();
	if (theApp.emuledlg != NULL && theApp.emuledlg->sharedfileswnd != NULL)
		(void)theApp.emuledlg->sharedfileswnd->Reload(true);
	else if (theApp.sharedfiles != NULL)
		theApp.sharedfiles->Reload();
}

/**
 * Finds one shared file by full normalized path.
 */
CKnownFile* FindSharedFileByPath(const CString &rFilePath)
{
	CKnownFilesMap sharedFiles;
	theApp.sharedfiles->CopySharedFileMap(sharedFiles);
	for (const CKnownFilesMap::CPair *pair = sharedFiles.PGetFirstAssoc(); pair != NULL; pair = sharedFiles.PGetNextAssoc(pair)) {
		if (pair->value != NULL && pair->value->GetFilePath().CompareNoCase(rFilePath) == 0)
			return pair->value;
	}
	return NULL;
}

/**
 * Refreshes the shared-files widgets after one explicit share mutation.
 */
void RefreshSharedFilesUi()
{
	if (theApp.emuledlg != NULL && theApp.emuledlg->sharedfileswnd != NULL) {
		if (theApp.sharedfiles != NULL && theApp.sharedfiles->HasSharedHashingWork())
			theApp.emuledlg->sharedfileswnd->sharedfilesctrl.ScheduleStartupDeferredReload();
		else
			theApp.emuledlg->sharedfileswnd->sharedfilesctrl.ReloadFileList();
		theApp.emuledlg->sharedfileswnd->OnSingleFileShareStatusChanged();
	}
}

/**
 * Parses one upload-control client selector from a pipe payload.
 */
bool TryGetUploadClientSelector(const json &rParams, SPipeApiClientSelector &rSelector, SPipeApiError &rError)
{
	const bool bHasUserHash = rParams.contains("userHash");
	const bool bHasIp = rParams.contains("ip");
	const bool bHasPort = rParams.contains("port");
	if (!bHasUserHash && !bHasIp && !bHasPort) {
		rError.strCode = "INVALID_ARGUMENT";
		rError.strMessage = _T("userHash or ip and port are required");
		return false;
	}

	if (bHasIp != bHasPort) {
		rError.strCode = "INVALID_ARGUMENT";
		rError.strMessage = _T("ip and port must be provided together");
		return false;
	}

	if (bHasUserHash) {
		if (!TryDecodeHash(rParams["userHash"], rSelector.aucUserHash, rError)) {
			rError.strMessage = _T("userHash must be a valid 32-character lowercase hex string");
			return false;
		}
		rSelector.bHasUserHash = true;
	}

	if (bHasIp) {
		if (!rParams["ip"].is_string() || !rParams["port"].is_number_unsigned()) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("ip must be a string and port must be an unsigned number");
			return false;
		}

		const std::string strIp = rParams["ip"].get<std::string>();
		const unsigned uPort = rParams["port"].get<unsigned>();
		IN_ADDR address = {};
		if (uPort == 0 || uPort > 0xFFFFu || InetPtonA(AF_INET, strIp.c_str(), &address) != 1) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("ip must be a valid IPv4 address and port must be in the range 1..65535");
			return false;
		}

		rSelector.bHasEndpoint = true;
		rSelector.dwIp = address.s_addr;
		rSelector.uPort = static_cast<uint16>(uPort);
	}

	return true;
}

/**
 * Resolves one upload-control selector to a live client instance.
 */
CUpDownClient* FindClientForUploadControl(const SPipeApiClientSelector &rSelector, SPipeApiError &rError)
{
	CUpDownClient *pClient = NULL;
	if (rSelector.bHasUserHash)
		pClient = theApp.clientlist->FindClientByUserHash(rSelector.aucUserHash, rSelector.bHasEndpoint ? rSelector.dwIp : 0, rSelector.bHasEndpoint ? rSelector.uPort : 0);
	else if (rSelector.bHasEndpoint)
		pClient = theApp.clientlist->FindClientByIP(rSelector.dwIp, rSelector.uPort);

	if (pClient == NULL) {
		rError.strCode = "NOT_FOUND";
		rError.strMessage = _T("upload client not found");
	}
	return pClient;
}

/**
 * Reports whether one transfer source matches a REST client selector.
 */
bool TransferSourceMatchesSelector(const CUpDownClient &rClient, const SPipeApiClientSelector &rSelector)
{
	if (rSelector.bHasUserHash && !md4equ(rClient.GetUserHash(), rSelector.aucUserHash))
		return false;
	if (rSelector.bHasEndpoint) {
		const uint32 dwClientIp = rClient.GetIP() != 0 ? rClient.GetIP() : rClient.GetConnectIP();
		if (dwClientIp != rSelector.dwIp || rClient.GetUserPort() != rSelector.uPort)
			return false;
	}
	return true;
}

/**
 * Resolves a source-control selector against the sources attached to one transfer.
 */
CUpDownClient* FindTransferSourceClient(CPartFile &rPartFile, const SPipeApiClientSelector &rSelector, SPipeApiError &rError)
{
	for (POSITION pos = rPartFile.srclist.GetHeadPosition(); pos != NULL;) {
		CUpDownClient *const pClient = rPartFile.srclist.GetNext(pos);
		if (pClient != NULL && TransferSourceMatchesSelector(*pClient, rSelector))
			return pClient;
	}

	rError.strCode = "NOT_FOUND";
	rError.strMessage = _T("transfer source not found");
	return NULL;
}

/**
 * Parses one optional { addr, port } server endpoint from a command payload.
 */
bool TryGetServerEndpoint(const json &rParams, SPipeApiServerEndpoint &rEndpoint, bool &rbHasEndpoint, SPipeApiError &rError)
{
	const bool bHasAddress = rParams.contains("addr");
	const bool bHasPort = rParams.contains("port");
	if (!bHasAddress && !bHasPort) {
		rbHasEndpoint = false;
		return true;
	}

	if (!bHasAddress || !bHasPort || !rParams["addr"].is_string() || !rParams["port"].is_number_unsigned()) {
		rError.strCode = "INVALID_ARGUMENT";
		rError.strMessage = _T("addr and port must be provided together");
		return false;
	}

	const unsigned uPort = rParams["port"].get<unsigned>();
	rEndpoint.strAddress = CStringFromStdUtf8(rParams["addr"].get<std::string>());
	rEndpoint.strAddress.Trim();
	if (rEndpoint.strAddress.IsEmpty() || uPort == 0 || uPort > 0xFFFFu) {
		rError.strCode = "INVALID_ARGUMENT";
		rError.strMessage = _T("addr must be non-empty and port must be in the range 1..65535");
		return false;
	}

	rEndpoint.uPort = static_cast<uint16>(uPort);
	rbHasEndpoint = true;
	return true;
}

/**
 * Resolves one server endpoint against the current server list.
 */
CServer* FindServerByEndpoint(const SPipeApiServerEndpoint &rEndpoint)
{
	CServer *pServer = theApp.serverlist->GetServerByAddress(rEndpoint.strAddress, rEndpoint.uPort);
	if (pServer != NULL)
		return pServer;

	const CStringA strAddressA(CT2A(rEndpoint.strAddress));
	IN_ADDR address = {};
	const uint32 dwIp = InetPtonA(AF_INET, strAddressA, &address) == 1 ? address.s_addr : INADDR_NONE;
	return dwIp != INADDR_NONE ? theApp.serverlist->GetServerByIPTCP(dwIp, rEndpoint.uPort) : NULL;
}

/**
 * Converts a REST server priority token into the legacy server priority enum.
 */
bool TryGetServerPriorityParam(const json &rValue, UINT &ruPriority, SPipeApiError &rError)
{
	if (!rValue.is_string()) {
		rError.strCode = "INVALID_ARGUMENT";
		rError.strMessage = _T("priority must be a string");
		return false;
	}

	const std::string strPriority = WebServerJsonSeams::ToLowerAscii(rValue.get<std::string>());
	if (strPriority == "low") {
		ruPriority = SRV_PR_LOW;
		return true;
	}
	if (strPriority == "normal") {
		ruPriority = SRV_PR_NORMAL;
		return true;
	}
	if (strPriority == "high") {
		ruPriority = SRV_PR_HIGH;
		return true;
	}

	rError.strCode = "INVALID_ARGUMENT";
	rError.strMessage = _T("priority must be one of low, normal, high");
	return false;
}

/**
 * Applies REST server fields that correspond to legacy WebServer server actions.
 */
bool ApplyServerPatchParams(CServer &rServer, const json &rParams, SPipeApiError &rError)
{
	if (rParams.contains("name")) {
		if (!rParams["name"].is_string()) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("name must be a string when provided");
			return false;
		}
		rServer.SetListName(CStringFromStdUtf8(rParams["name"].get<std::string>()));
	}

	if (rParams.contains("priority")) {
		UINT uPriority = SRV_PR_NORMAL;
		if (!TryGetServerPriorityParam(rParams["priority"], uPriority, rError))
			return false;
		rServer.SetPreference(uPriority);
	}

	if (rParams.contains("static")) {
		if (!rParams["static"].is_boolean()) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("static must be a boolean");
			return false;
		}
		rServer.SetIsStaticMember(rParams["static"].get<bool>());
		if (!theApp.serverlist->SaveStaticServers()) {
			rError.strCode = "EMULE_ERROR";
			rError.strMessage = _T("failed to save static server list");
			return false;
		}
	}

	if (!theApp.serverlist->SaveServermetToFile()) {
		rError.strCode = "EMULE_ERROR";
		rError.strMessage = _T("failed to save server.met");
		return false;
	}

	if (theApp.emuledlg != NULL && theApp.emuledlg->serverwnd != NULL)
		theApp.emuledlg->serverwnd->serverlistctrl.RefreshServer(&rServer);

	return true;
}

/**
 * Marshals one legacy web action onto the main UI thread before it mutates
 * connection state owned by the dialog and socket layers.
 */
LRESULT InvokeWebGuiInteraction(const WPARAM wAction, const LPARAM lParam = 0)
{
	if (theApp.emuledlg == NULL || theApp.emuledlg->GetSafeHwnd() == NULL)
		return 0;
	return ::SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, wAction, lParam);
}

/**
 * Queues a legacy web action on the main UI thread without blocking the REST
 * caller on connection-layer work.
 */
bool PostWebGuiInteraction(const WPARAM wAction, const LPARAM lParam = 0)
{
	if (theApp.emuledlg == NULL || theApp.emuledlg->GetSafeHwnd() == NULL)
		return false;
	return ::PostMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, wAction, lParam) != FALSE;
}

/**
 * Starts the existing eMule rehash flow for a part file.
 */
bool StartPartFileRecheck(CPartFile &rPartFile, SPipeApiError &rError)
{
	if (inSet(rPartFile.GetStatus(), PS_WAITINGFORHASH, PS_HASHING, PS_COMPLETING)) {
		rError.strCode = "INVALID_ARGUMENT";
		rError.strMessage = _T("transfer is already being hashed or completed");
		return false;
	}

	CAddFileThread *pAddFileThread = static_cast<CAddFileThread*>(AfxBeginThread(RUNTIME_CLASS(CAddFileThread), THREAD_PRIORITY_BELOW_NORMAL, 0, CREATE_SUSPENDED));
	if (pAddFileThread == NULL) {
		rError.strCode = "EMULE_ERROR";
		rError.strMessage = _T("failed to start recheck thread");
		return false;
	}

	rPartFile.SetStatus(PS_WAITINGFORHASH);
	rPartFile.SetFileOp(PFOP_HASHING);
	rPartFile.SetFileOpProgress(0);
	pAddFileThread->SetValues(NULL, rPartFile.GetPath(), rPartFile.GetPartMetFileName(), _T(""), &rPartFile);
	rPartFile.SetStatus(PS_HASHING);
	pAddFileThread->ResumeThread();
	return true;
}

/**
 * Builds one per-hash bulk-mutation result row.
 */
json BuildMutationResult(const CString &rHash, bool bOk, LPCTSTR pszError = NULL)
{
	json result{
		{"hash", StdUtf8FromCString(rHash)},
		{"ok", bOk}
	};
	if (!bOk && pszError != NULL)
		result["error"] = StdUtf8FromCString(pszError);
	return result;
}

/**
 * Dispatches one parsed request against the eMule data model on the UI thread.
 */
json HandleUiCommand(const json &rRequest, SPipeApiError &rError)
{
	const std::string strCommand = rRequest.value("cmd", std::string());
	const json params = rRequest.value("params", json::object());

	if (strCommand == "app/version") {
		return BuildAppJson(GetRestBuildFlavor());
	}

	if (strCommand == "app/preferences/get")
		return BuildPreferencesJson();

	if (strCommand == "app/preferences/set") {
		if (!ApplyPreferencesJson(params.contains("prefs") ? params["prefs"] : json(), rError))
			return json();
		return BuildPreferencesJson();
	}

	if (strCommand == "app/shutdown") {
		theApp.emuledlg->PostMessage(WM_CLOSE);
		return json{{"ok", true}};
	}

	if (strCommand == "app/crash_test") {
		if (!thePrefs.GetDiagnosticRestEndpointsEnabled()) {
			rError.strCode = "NOT_FOUND";
			rError.strMessage = _T("API route not found");
			return json();
		}
		volatile LONG *pCrash = reinterpret_cast<volatile LONG *>(0);
		*pCrash = 1;
		return json{{"ok", true}};
	}

	if (strCommand == "app/capture_dump") {
		if (!thePrefs.GetDiagnosticRestEndpointsEnabled()) {
			rError.strCode = "NOT_FOUND";
			rError.strMessage = _T("API route not found");
			return json();
		}
		if (params.contains("fullMemory") && !params["fullMemory"].is_boolean()) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("fullMemory must be a boolean");
			return json();
		}

		const bool bFullMemoryDump = params.value("fullMemory", false);
		const CMiniDumper::SManualDumpResult result = CMiniDumper::CreateManualDump(
			theApp.m_strCurVersionLongDbg,
			thePrefs.GetMuleDirectory(EMULE_LOGDIR),
			bFullMemoryDump);
		if (!result.bSuccess) {
			const CString strError(GetErrorMessage(result.dwError, 1));
			rError.strCode = "EMULE_ERROR";
			rError.strMessage.Format(_T("failed to capture diagnostic dump: %s"), (LPCTSTR)strError);
			return json();
		}

		return json{
			{"ok", true},
			{"path", StdUtf8FromCString(result.strDumpPath)},
			{"fullMemory", bFullMemoryDump}
		};
	}

	if (strCommand == "stats/global")
		return BuildGlobalStatsJson();

	if (strCommand == "status/get") {
		return json{
			{"stats", BuildGlobalStatsJson()},
			{"servers", BuildServerStatusJson()},
			{"kad", BuildKadStatusJson()}
		};
	}

	if (strCommand == "categories/list")
		return ItemsEnvelopeIfRequested(params, BuildCategoriesJson());

	if (strCommand == "categories/get") {
		UINT uCategory = 0;
		if (!TryGetCategoryIdParam(params.contains("id") ? params["id"] : json(), uCategory, rError))
			return json();
		if (uCategory >= thePrefs.GetCatCount()) {
			rError.strCode = "NOT_FOUND";
			rError.strMessage = _T("category not found");
			return json();
		}
		return BuildCategoryJson(uCategory);
	}

	if (strCommand == "categories/create") {
		Category_Struct *pCategory = new Category_Struct;
		pCategory->strIncomingPath = thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR);
		pCategory->strTitle.Empty();
		pCategory->strComment.Empty();
		pCategory->autocat.Empty();
		pCategory->regexp.Empty();
		pCategory->color = CLR_NONE;
		pCategory->prio = PR_NORMAL;
		pCategory->filter = 0;
		pCategory->filterNeg = false;
		pCategory->care4all = false;
		pCategory->ac_regexpeval = false;
		pCategory->downloadInAlphabeticalOrder = false;

		bool bPathChanged = false;
		if (!ApplyCategoryCoreFields(*pCategory, params, true, bPathChanged, rError)) {
			delete pCategory;
			return json();
		}

		const INT_PTR iCategory = thePrefs.AddCat(pCategory);
		thePrefs.SaveCats();
		RefreshCategoryUi(bPathChanged);
		return BuildCategoryJson(iCategory);
	}

	if (strCommand == "categories/update") {
		UINT uCategory = 0;
		if (!TryGetCategoryIdParam(params.contains("id") ? params["id"] : json(), uCategory, rError))
			return json();
		if (uCategory == 0 || uCategory >= thePrefs.GetCatCount()) {
			rError.strCode = uCategory == 0 ? "INVALID_ARGUMENT" : "NOT_FOUND";
			rError.strMessage = uCategory == 0 ? _T("default category cannot be updated") : _T("category not found");
			return json();
		}

		Category_Struct *const pCategory = thePrefs.GetCategory(uCategory);
		bool bPathChanged = false;
		if (pCategory == NULL || !ApplyCategoryCoreFields(*pCategory, params, false, bPathChanged, rError))
			return json();

		thePrefs.SaveCats();
		RefreshCategoryUi(bPathChanged);
		return BuildCategoryJson(uCategory);
	}

	if (strCommand == "categories/delete") {
		UINT uCategory = 0;
		if (!TryGetCategoryIdParam(params.contains("id") ? params["id"] : json(), uCategory, rError))
			return json();
		if (uCategory == 0 || uCategory >= thePrefs.GetCatCount()) {
			rError.strCode = uCategory == 0 ? "INVALID_ARGUMENT" : "NOT_FOUND";
			rError.strMessage = uCategory == 0 ? _T("default category cannot be deleted") : _T("category not found");
			return json();
		}

		const CString strIncomingPath(thePrefs.GetCatPath(uCategory));
		const bool bReloadSharedFiles = !EqualPaths(strIncomingPath, thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
		theApp.downloadqueue->ResetCatParts(uCategory);
		thePrefs.RemoveCat(uCategory);
		thePrefs.SaveCats();
		RefreshCategoryUi(bReloadSharedFiles);
		return json{{"ok", true}};
	}

	if (strCommand == "shared_directories/get")
		return BuildSharedDirectoriesJson();

	if (strCommand == "shared_directories/set") {
		CStringList sharedDirs;
		CStringList monitoredRoots;
		CStringList monitorOwnedDirs;
		if (!TryBuildSharedDirectoryListsFromJson(params, sharedDirs, monitoredRoots, monitorOwnedDirs, rError))
			return json();

		ApplySharedDirectoryLists(sharedDirs, monitoredRoots, monitorOwnedDirs);
		return BuildSharedDirectoriesJson();
	}

	if (strCommand == "shared_directories/reload") {
		if (theApp.emuledlg != NULL && theApp.emuledlg->sharedfileswnd != NULL)
			(void)theApp.emuledlg->sharedfileswnd->Reload(true);
		else if (theApp.sharedfiles != NULL)
			theApp.sharedfiles->Reload();
		return json{{"ok", true}};
	}

	if (strCommand == "snapshot/get") {
		SPipeApiError listError;
		json transfers = BuildTransfersListJson(json::object(), listError);
		if (!listError.strCode.IsEmpty()) {
			rError = listError;
			return json();
		}

		json servers = json::array();
		for (INT_PTR i = 0; i < theApp.serverlist->GetServerCount(); ++i) {
			CServer *const pServer = theApp.serverlist->GetServerAt(i);
			if (pServer != NULL)
				servers.push_back(BuildServerJson(*pServer));
		}

		const size_t maxEntries = static_cast<size_t>(max(1, params.value("limit", 200)));
		return json{
			{"app", BuildAppJson(GetRestBuildFlavor())},
			{"status", json{{"stats", BuildGlobalStatsJson()}, {"servers", BuildServerStatusJson()}, {"kad", BuildKadStatusJson()}}},
			{"transfers", transfers},
			{"sharedFiles", BuildSharedFilesListJson()},
			{"uploads", BuildUploadsListJson(false)},
			{"uploadQueue", BuildUploadsListJson(true)},
			{"servers", servers},
			{"kad", BuildKadStatusJson()},
			{"logs", BuildLogEntriesJson(maxEntries)}
		};
	}

	if (strCommand == "servers/list") {
		json result = json::array();
		for (INT_PTR i = 0; i < theApp.serverlist->GetServerCount(); ++i) {
			CServer *const pServer = theApp.serverlist->GetServerAt(i);
			if (pServer != NULL)
				result.push_back(BuildServerJson(*pServer));
		}
		return ItemsEnvelopeIfRequested(params, result);
	}

	if (strCommand == "servers/status")
		return BuildServerStatusJson();

	if (strCommand == "servers/get") {
		SPipeApiServerEndpoint endpoint;
		bool bHasEndpoint = false;
		if (!TryGetServerEndpoint(params, endpoint, bHasEndpoint, rError))
			return json();
		if (!bHasEndpoint) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("addr and port are required");
			return json();
		}

		CServer *const pServer = FindServerByEndpoint(endpoint);
		if (pServer == NULL) {
			rError.strCode = "NOT_FOUND";
			rError.strMessage = _T("server not found");
			return json();
		}
		return BuildServerJson(*pServer);
	}

	if (strCommand == "servers/connect") {
		SPipeApiServerEndpoint endpoint;
		bool bHasEndpoint = false;
		if (!TryGetServerEndpoint(params, endpoint, bHasEndpoint, rError))
			return json();

		if (bHasEndpoint) {
			CServer *const pServer = FindServerByEndpoint(endpoint);
			if (pServer == NULL) {
				rError.strCode = "NOT_FOUND";
				rError.strMessage = _T("server not found");
				return json();
			}
			InvokeWebGuiInteraction(WEBGUIIA_CONNECTTOSERVER, reinterpret_cast<LPARAM>(pServer));
		} else
			InvokeWebGuiInteraction(WEBGUIIA_CONNECTTOSERVER);

		return BuildServerStatusJson();
	}

	if (strCommand == "servers/disconnect") {
		InvokeWebGuiInteraction(WEBGUIIA_DISCONNECT, 1);
		return BuildServerStatusJson();
	}

	if (strCommand == "servers/add") {
		SPipeApiServerEndpoint endpoint;
		bool bHasEndpoint = false;
		if (!TryGetServerEndpoint(params, endpoint, bHasEndpoint, rError))
			return json();
		if (!bHasEndpoint) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("addr and port are required");
			return json();
		}

		CString strName;
		if (params.contains("name")) {
			if (!params["name"].is_string()) {
				rError.strCode = "INVALID_ARGUMENT";
				rError.strMessage = _T("name must be a string when provided");
				return json();
			}
			strName = CStringFromStdUtf8(params["name"].get<std::string>());
		}

		if (theApp.emuledlg->serverwnd == NULL || !theApp.emuledlg->serverwnd->AddServer(endpoint.uPort, endpoint.strAddress, strName, false)) {
			rError.strCode = "EMULE_ERROR";
			rError.strMessage = _T("failed to add server");
			return json();
		}

		CServer *const pServer = FindServerByEndpoint(endpoint);
		if (pServer != NULL) {
			if (!ApplyServerPatchParams(*pServer, params, rError))
				return json();
			if (params.contains("connect") && !params["connect"].is_boolean()) {
				rError.strCode = "INVALID_ARGUMENT";
				rError.strMessage = _T("connect must be a boolean");
				return json();
			}
			if (params.value("connect", false))
				InvokeWebGuiInteraction(WEBGUIIA_CONNECTTOSERVER, reinterpret_cast<LPARAM>(pServer));
			return BuildServerJson(*pServer);
		}

		return json{
			{"name", StdUtf8FromCString(strName)},
			{"address", StdUtf8FromCString(endpoint.strAddress)},
			{"port", endpoint.uPort}
		};
	}

	if (strCommand == "servers/update") {
		SPipeApiServerEndpoint endpoint;
		bool bHasEndpoint = false;
		if (!TryGetServerEndpoint(params, endpoint, bHasEndpoint, rError))
			return json();
		if (!bHasEndpoint) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("addr and port are required");
			return json();
		}

		CServer *const pServer = FindServerByEndpoint(endpoint);
		if (pServer == NULL) {
			rError.strCode = "NOT_FOUND";
			rError.strMessage = _T("server not found");
			return json();
		}
		return ApplyServerPatchParams(*pServer, params, rError) ? BuildServerJson(*pServer) : json();
	}

	if (strCommand == "servers/import_met_url") {
		if (!params.contains("url") || !params["url"].is_string()) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("url must be a string");
			return json();
		}
		const CString strURL(CStringFromStdUtf8(params["url"].get<std::string>()));
		if (theApp.emuledlg == NULL || theApp.emuledlg->serverwnd == NULL) {
			rError.strCode = "EMULE_ERROR";
			rError.strMessage = _T("failed to update server.met from URL");
			return json();
		}
		const bool bImported = InvokeWebGuiInteraction(WEBGUIIA_UPDATESERVERMETFROMURL, reinterpret_cast<LPARAM>(static_cast<LPCTSTR>(strURL))) != 0;
		return json{{"ok", bImported}, {"imported", bImported}};
	}

	if (strCommand == "servers/remove") {
		SPipeApiServerEndpoint endpoint;
		bool bHasEndpoint = false;
		if (!TryGetServerEndpoint(params, endpoint, bHasEndpoint, rError))
			return json();
		if (!bHasEndpoint) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("addr and port are required");
			return json();
		}

		CServer *const pServer = FindServerByEndpoint(endpoint);
		if (pServer == NULL) {
			rError.strCode = "NOT_FOUND";
			rError.strMessage = _T("server not found");
			return json();
		}

		const json result = BuildServerJson(*pServer);
		InvokeWebGuiInteraction(WEBGUIIA_SERVER_REMOVE, reinterpret_cast<LPARAM>(pServer));
		return result;
	}

	if (strCommand == "kad/status")
		return BuildKadStatusJson();

	if (strCommand == "kad/connect") {
		InvokeWebGuiInteraction(WEBGUIIA_KAD_START);
		return BuildKadStatusJson();
	}

	if (strCommand == "kad/import_nodes_url") {
		if (!params.contains("url") || !params["url"].is_string()) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("url must be a string");
			return json();
		}
		const CString strURL(CStringFromStdUtf8(params["url"].get<std::string>()));
		if (theApp.emuledlg == NULL || theApp.emuledlg->kademliawnd == NULL) {
			rError.strCode = "EMULE_ERROR";
			rError.strMessage = _T("failed to update nodes.dat from URL");
			return json();
		}
		const bool bImported = InvokeWebGuiInteraction(WEBGUIIA_UPDATENODESDATFROMURL, reinterpret_cast<LPARAM>(static_cast<LPCTSTR>(strURL))) != 0;
		return json{{"ok", bImported}, {"imported", bImported}};
	}

	if (strCommand == "kad/bootstrap") {
		if (params.contains("address") || params.contains("port")) {
			if (!params.contains("address") || !params["address"].is_string() || !params.contains("port") || !params["port"].is_number_unsigned()) {
				rError.strCode = "INVALID_ARGUMENT";
				rError.strMessage = _T("address and port are required together for Kad bootstrap");
				return json();
			}
			const unsigned uPort = params["port"].get<unsigned>();
			if (uPort == 0 || uPort > 0xFFFFu) {
				rError.strCode = "INVALID_ARGUMENT";
				rError.strMessage = _T("port must be between 1 and 65535");
				return json();
			}
			CString strDest;
			strDest.Format(_T("%s:%u"), static_cast<LPCTSTR>(CStringFromStdUtf8(params["address"].get<std::string>())), uPort);
			InvokeWebGuiInteraction(WEBGUIIA_KAD_BOOTSTRAP, reinterpret_cast<LPARAM>(static_cast<LPCTSTR>(strDest)));
		} else
			InvokeWebGuiInteraction(WEBGUIIA_KAD_START);
		return BuildKadStatusJson();
	}

	if (strCommand == "kad/disconnect") {
		InvokeWebGuiInteraction(WEBGUIIA_DISCONNECT, 2);
		return BuildKadStatusJson();
	}

	if (strCommand == "kad/recheck_firewall") {
		json result = BuildKadStatusJson();
		if (Kademlia::CUDPFirewallTester::IsFWCheckUDPRunning()) {
			result["operationQueued"] = false;
			result["alreadyRunning"] = true;
			return result;
		}
		result["operationQueued"] = PostWebGuiInteraction(WEBGUIIA_KAD_RCFW);
		result["alreadyRunning"] = false;
		return result;
	}

	if (strCommand == "shared/list") {
		return ItemsEnvelopeIfRequested(params, BuildSharedFilesListJson());
	}

	if (strCommand == "shared/add") {
		CString strFilePath;
		if (!TryGetPathParam(params.contains("path") ? params["path"] : json(), "path", strFilePath, rError))
			return json();
		strFilePath = PathHelpers::CanonicalizePath(strFilePath);
		const PathHelpers::ParsedPathRoot fileRoot(PathHelpers::ParsePathRoot(strFilePath));
		if (!fileRoot.bAbsolute) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("path must point to a file inside a shareable directory");
			return json();
		}

		const int iSlash = strFilePath.ReverseFind(_T('\\'));
		const CString strDirectory = iSlash >= 0 ? strFilePath.Left(iSlash) : CString();
		if (strDirectory.IsEmpty() || !thePrefs.IsShareableDirectory(strDirectory)) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("path must point to a file inside a shareable directory");
			return json();
		}

		CKnownFile *const pExistingFile = FindSharedFileByPath(strFilePath);
		if (theApp.sharedfiles->ShouldBeShared(strDirectory, strFilePath, false)) {
			return json{
				{"ok", true},
				{"path", StdUtf8FromCString(strFilePath)},
				{"alreadyShared", true},
				{"queued", false},
				{"file", pExistingFile != NULL ? BuildSharedFileJson(*pExistingFile) : json(nullptr)}
			};
		}

		if (!theApp.sharedfiles->AddSingleSharedFile(strFilePath)) {
			rError.strCode = "EMULE_ERROR";
			rError.strMessage = _T("failed to queue shared file");
			return json();
		}

		RefreshSharedFilesUi();
		CKnownFile *const pSharedFile = FindSharedFileByPath(strFilePath);
		return json{
			{"ok", true},
			{"path", StdUtf8FromCString(strFilePath)},
			{"alreadyShared", false},
			{"queued", pSharedFile == NULL},
			{"file", pSharedFile != NULL ? BuildSharedFileJson(*pSharedFile) : json(nullptr)}
		};
	}

	if (strCommand == "shared/get") {
		CKnownFile *const pKnownFile = FindSharedFileByHash(params.contains("hash") ? params["hash"] : json(), rError);
		if (pKnownFile == NULL)
			return json();
		return BuildSharedFileJson(*pKnownFile);
	}

	if (strCommand == "shared/ed2k_link") {
		CKnownFile *const pKnownFile = FindSharedFileByHash(params.contains("hash") ? params["hash"] : json(), rError);
		if (pKnownFile == NULL)
			return json();
		return json{
			{"hash", StdUtf8FromCString(HashToHex(pKnownFile->GetFileHash()))},
			{"link", StdUtf8FromCString(pKnownFile->GetED2kLink())}
		};
	}

	if (strCommand == "shared/comments") {
		CKnownFile *const pKnownFile = FindSharedFileByHash(params.contains("hash") ? params["hash"] : json(), rError);
		if (pKnownFile == NULL)
			return json();
		return ItemsEnvelopeIfRequested(params, BuildSharedFileCommentsJson(*pKnownFile));
	}

	if (strCommand == "shared/set_rating_comment") {
		CKnownFile *const pKnownFile = FindSharedFileByHash(params.contains("hash") ? params["hash"] : json(), rError);
		if (pKnownFile == NULL)
			return json();
		if (pKnownFile->IsPartFile()) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("part files cannot be updated through this endpoint");
			return json();
		}

		if (params.contains("priority")) {
			uint8 uPriority = PR_NORMAL;
			bool bAuto = false;
			if (!TryGetSharedUploadPriorityParam(params["priority"], uPriority, bAuto, rError))
				return json();
			if (bAuto) {
				pKnownFile->SetAutoUpPriority(true);
				pKnownFile->UpdateAutoUpPriority();
			} else {
				pKnownFile->SetAutoUpPriority(false);
				pKnownFile->SetUpPriority(uPriority);
			}
			InvokeWebGuiInteraction(WEBGUIIA_UPD_SFUPDATE, reinterpret_cast<LPARAM>(pKnownFile));
		}

		if (params.contains("comment") || params.contains("rating")) {
			if (!TryApplySharedFileRatingComment(*pKnownFile, params, rError))
				return json();
		}

		if (!params.contains("priority") && !params.contains("comment") && !params.contains("rating")) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("shared-file PATCH requires priority, comment, or rating");
			return json();
		}

		RefreshSharedFilesUi();
		return BuildSharedFileJson(*pKnownFile);
	}

	if (strCommand == "shared/remove") {
		const bool bHasHash = params.contains("hash");
		const bool bHasPath = params.contains("path");
		if (bHasHash == bHasPath) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("exactly one of hash or path is required");
			return json();
		}
		if (params.contains("deleteFiles") && !params["deleteFiles"].is_boolean()) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("deleteFiles must be a boolean");
			return json();
		}

		CString strFilePath;
		CKnownFile *pKnownFile = NULL;
		if (bHasHash) {
			uchar hash[MDX_DIGEST_SIZE];
			if (!TryDecodeHash(params["hash"], hash, rError))
				return json();

			pKnownFile = theApp.sharedfiles->GetFileByID(hash);
			if (pKnownFile == NULL) {
				rError.strCode = "NOT_FOUND";
				rError.strMessage = _T("shared file not found");
				return json();
			}
			strFilePath = pKnownFile->GetFilePath();
		} else {
			if (!TryGetPathParam(params["path"], "path", strFilePath, rError))
				return json();
			pKnownFile = FindSharedFileByPath(strFilePath);
		}

		const int iSlash = strFilePath.ReverseFind(_T('\\'));
		const CString strDirectory = iSlash >= 0 ? strFilePath.Left(iSlash) : CString();
		const bool bIsShared = !strDirectory.IsEmpty() && theApp.sharedfiles->ShouldBeShared(strDirectory, strFilePath, false);
		const bool bMustRemainShared = !strDirectory.IsEmpty() && theApp.sharedfiles->ShouldBeShared(strDirectory, strFilePath, true);
		const bool bDeleteFiles = params.value("deleteFiles", false);
		const json hashValue = pKnownFile != NULL ? JsonHashOrNull(pKnownFile->GetFileHash()) : json(nullptr);
		if (pKnownFile != NULL && pKnownFile->IsPartFile()) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("part files cannot be unshared individually");
			return json();
		}
		if (!bDeleteFiles && !WebApiSurfaceSeams::CanRemoveSharedFile(bIsShared, bMustRemainShared)) {
			rError.strCode = bMustRemainShared ? "INVALID_ARGUMENT" : "NOT_FOUND";
			rError.strMessage = bMustRemainShared ? _T("file belongs to a mandatory shared directory") : _T("shared file not found");
			return json();
		}
		if (bDeleteFiles && !ShellDeleteFile(strFilePath)) {
			rError.strCode = "EMULE_ERROR";
			rError.strMessage = GetErrorMessage(::GetLastError());
			return json();
		}
		if (bDeleteFiles && pKnownFile != NULL)
			(void)theApp.sharedfiles->RemoveFile(pKnownFile, true);
		else if (!theApp.sharedfiles->ExcludeFile(strFilePath)) {
			rError.strCode = "EMULE_ERROR";
			rError.strMessage = _T("failed to remove shared file");
			return json();
		}

		RefreshSharedFilesUi();
		return json{
			{"ok", true},
			{"deletedFiles", bDeleteFiles},
			{"path", StdUtf8FromCString(strFilePath)},
			{"hash", hashValue}
		};
	}

	if (strCommand == "uploads/list" || strCommand == "uploads/queue") {
		const bool bWaitingQueue = strCommand == "uploads/queue";
		return ItemsEnvelopeIfRequested(params, BuildUploadsListJson(bWaitingQueue));
	}

	if (strCommand == "uploads/remove" || strCommand == "uploads/release_slot") {
		SPipeApiClientSelector selector;
		if (!TryGetUploadClientSelector(params, selector, rError))
			return json();

		CUpDownClient *const pClient = FindClientForUploadControl(selector, rError);
		if (pClient == NULL)
			return json();

		if (strCommand == "uploads/remove") {
			if (theApp.uploadqueue->RemoveFromWaitingQueue(pClient, true))
				return json{{"ok", true}, {"removed", "queue"}};
			if (theApp.uploadqueue->RemoveFromUploadQueue(pClient, _T("Removed by pipe API"), true, true))
				return json{{"ok", true}, {"removed", "slot"}};

			rError.strCode = "NOT_FOUND";
			rError.strMessage = _T("upload client is not active or queued");
			return json();
		}

		if (!theApp.uploadqueue->RemoveFromUploadQueue(pClient, _T("Released upload slot by pipe API"), true, true)) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("client does not currently hold an upload slot");
			return json();
		}
		return json{{"ok", true}};
	}

	if (strCommand == "peers/add-friend" || strCommand == "peers/remove-friend" || strCommand == "peers/ban" || strCommand == "peers/unban" || strCommand == "peers/remove") {
		SPipeApiClientSelector selector;
		if (!TryGetUploadClientSelector(params, selector, rError))
			return json();

		CPartFile *pSourceOwner = NULL;
		CUpDownClient *pClient = NULL;
		if (params.contains("hash")) {
			pSourceOwner = FindPartFileByHash(params["hash"], rError);
			if (pSourceOwner == NULL)
				return json();
			pClient = FindTransferSourceClient(*pSourceOwner, selector, rError);
		} else
			pClient = FindClientForUploadControl(selector, rError);
		if (pClient == NULL)
			return json();

		if (strCommand == "peers/add-friend") {
			CFriend *const pExistingFriend = pClient->GetFriend();
			if (pExistingFriend != NULL)
				return BuildFriendJson(*pExistingFriend);
			if (!theApp.friendlist->AddFriend(pClient)) {
				rError.strCode = "INVALID_STATE";
				rError.strMessage = _T("peer is already a friend or cannot be added");
				return json();
			}
			CFriend *const pFriend = pClient->GetFriend();
			return pFriend != NULL ? BuildFriendJson(*pFriend) : json{{"ok", true}};
		}

		if (strCommand == "peers/remove-friend") {
			CFriend *pFriend = pClient->GetFriend();
			if (pFriend == NULL && pClient->HasValidHash())
				pFriend = theApp.friendlist->SearchFriend(pClient->GetUserHash(), pClient->GetIP(), pClient->GetUserPort());
			if (pFriend == NULL) {
				rError.strCode = "NOT_FOUND";
				rError.strMessage = _T("friend not found");
				return json();
			}
			theApp.friendlist->RemoveFriend(pFriend);
			return json{{"ok", true}};
		}

		if (strCommand == "peers/ban") {
			if (!pClient->IsBanned())
				pClient->Ban(_T("Manual REST ban"));
			return json{{"ok", true}, {"banned", true}};
		}

		if (strCommand == "peers/remove") {
			if (pSourceOwner == NULL) {
				rError.strCode = "INVALID_ARGUMENT";
				rError.strMessage = _T("peer remove requires a transfer source route");
				return json();
			}
			if (!theApp.downloadqueue->RemoveSource(pClient)) {
				rError.strCode = "NOT_FOUND";
				rError.strMessage = _T("transfer source not found");
				return json();
			}
			return json{{"ok", true}};
		}

		if (pClient->IsBanned())
			pClient->UnBan();
		return json{{"ok", true}, {"banned", false}};
	}

	if (strCommand == "transfers/clear_completed") {
		if (theApp.emuledlg == NULL || theApp.emuledlg->GetSafeHwnd() == NULL) {
			rError.strCode = "EMULE_UNAVAILABLE";
			rError.strMessage = _T("main window is not available");
			return json();
		}
		theApp.emuledlg->SendMessage(WEB_CLEAR_COMPLETED, static_cast<WPARAM>(0), static_cast<LPARAM>(-1));
		return json{{"ok", true}};
	}

	if (strCommand == "transfers/list") {
		const json result = BuildTransfersListJson(params, rError);
		if (!rError.strCode.IsEmpty())
			return json();
		return ItemsEnvelopeIfRequested(params, result);
	}

	if (strCommand == "transfers/get") {
		CPartFile *pPartFile = FindPartFileByHash(params.contains("hash") ? params["hash"] : json(), rError);
		return pPartFile != NULL ? BuildTransferJson(*pPartFile) : json();
	}

	if (strCommand == "transfers/sources") {
		CPartFile *pPartFile = FindPartFileByHash(params.contains("hash") ? params["hash"] : json(), rError);
		if (pPartFile == NULL)
			return json();

		return ItemsEnvelopeIfRequested(params, BuildTransferSourcesJson(*pPartFile));
	}

	if (strCommand == "transfers/details") {
		CPartFile *pPartFile = FindPartFileByHash(params.contains("hash") ? params["hash"] : json(), rError);
		return pPartFile != NULL ? BuildTransferDetailsJson(*pPartFile) : json();
	}

	if (strCommand == "transfers/source_browse") {
		CPartFile *const pPartFile = FindPartFileByHash(params.contains("hash") ? params["hash"] : json(), rError);
		if (pPartFile == NULL)
			return json();

		SPipeApiClientSelector selector;
		if (!TryGetUploadClientSelector(params, selector, rError)) {
			if (rError.strMessage == _T("userHash or ip and port are required"))
				rError.strMessage = _T("source userHash or ip and port are required");
			return json();
		}

		CUpDownClient *const pClient = FindTransferSourceClient(*pPartFile, selector, rError);
		if (pClient == NULL)
			return json();
		if (!pClient->GetViewSharedFilesSupport()) {
			rError.strCode = "INVALID_STATE";
			rError.strMessage = _T("transfer source does not support shared-file browsing");
			return json();
		}

		uint32 uSearchID = pClient->GetSearchID();
		if (uSearchID == 0) {
			if (theApp.emuledlg == NULL || theApp.emuledlg->searchwnd == NULL || theApp.emuledlg->searchwnd->m_pwndResults == NULL) {
				rError.strCode = "EMULE_UNAVAILABLE";
				rError.strMessage = _T("search window is not available");
				return json();
			}
			uSearchID = theApp.emuledlg->searchwnd->m_pwndResults->GetNextSearchID();
			pClient->SetSearchID(uSearchID);
		}

		const bool bAlreadyPending = pClient->GetFileListRequested() > 0;
		if (!bAlreadyPending)
			pClient->RequestSharedFileList();
		return json{
			{"ok", true},
			{"alreadyPending", bAlreadyPending},
			{"searchId", StdUtf8FromCString(FormatSearchId(uSearchID))}
		};
	}

	if (strCommand == "transfers/add") {
		UINT uCategory = 0;
		if (params.contains("categoryId")) {
			uint64_t uRequestedCategory = 0;
			if (!WebApiCommandSeams::TryParseNonNegativeUInt64(params["categoryId"], uRequestedCategory) || uRequestedCategory > UINT_MAX) {
				rError.strCode = "INVALID_ARGUMENT";
				rError.strMessage = _T("categoryId must be an unsigned number");
				return json();
			}
			uCategory = static_cast<UINT>(uRequestedCategory);
		} else if (params.contains("categoryName")) {
			if (!params["categoryName"].is_string()) {
				rError.strCode = "INVALID_ARGUMENT";
				rError.strMessage = _T("categoryName must be a string");
				return json();
			}
			if (!TryResolveCategoryName(CStringFromStdUtf8(params["categoryName"].get<std::string>()), uCategory)) {
				rError.strCode = "INVALID_ARGUMENT";
				rError.strMessage = _T("categoryName does not match a configured category");
				return json();
			}
		}
		if (uCategory >= thePrefs.GetCatCount()) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("category is out of range");
			return json();
		}

		uint8 uPaused = 2;
		if (!TryParseOptionalDownloadPaused(params, uPaused, rError))
			return json();

		auto addOneLink = [&](const std::string &rLinkUtf8, CString &rLinkError) -> json
		{
			std::unique_ptr<CED2KLink> pLink;
			try {
				CString strLink(CStringFromStdUtf8(rLinkUtf8));
				if (strLink.IsEmpty())
					throw CString(_T("invalid ed2k link"));
				const bool bSlash = (strLink[strLink.GetLength() - 1] == _T('/'));
				const CString strNormalizedLink(bSlash ? strLink : strLink + _T('/'));
				pLink.reset(CED2KLink::CreateLinkFromUrl(strNormalizedLink));
				if (pLink == NULL || pLink->GetKind() != CED2KLink::kFile)
					throw CString(_T("invalid ed2k link"));

				const CED2KFileLink *const pFileLink = pLink->GetFileLink();
				theApp.downloadqueue->AddFileLinkToDownload(*pFileLink, static_cast<int>(uCategory), uPaused);
				const json result{
					{"hash", StdUtf8FromCString(HashToHex(pFileLink->GetHashKey()))},
					{"name", StdUtf8FromCString(pFileLink->GetName())}
				};
				return result;
			} catch (const CString &rCaughtError) {
				rLinkError = rCaughtError;
				return json();
			}
		};

		if (params.contains("links")) {
			if (!params["links"].is_array()) {
				rError.strCode = "INVALID_ARGUMENT";
				rError.strMessage = _T("links must be a string array");
				return json();
			}

			json results = json::array();
			CScopedDownloadQueueBulkAdd bulkAdd(theApp.downloadqueue);
			for (const json &linkValue : params["links"]) {
				std::string strLinkUtf8;
				std::string strError;
				if (!WebApiCommandSeams::TryParseTransferAddLink(json{{"link", linkValue}}, strLinkUtf8, strError)) {
					results.push_back(json{{"ok", false}, {"error", strError}});
					continue;
				}

				CString strLinkError;
				json added = addOneLink(strLinkUtf8, strLinkError);
				if (!strLinkError.IsEmpty())
					results.push_back(json{{"ok", false}, {"error", StdUtf8FromCString(strLinkError)}});
				else {
					added["ok"] = true;
					results.push_back(added);
				}
			}
			return json{{"items", results}};
		}

		std::string strLinkUtf8;
		std::string strError;
		if (!WebApiCommandSeams::TryParseTransferAddLink(params, strLinkUtf8, strError)) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = CStringFromStdUtf8(strError);
			return json();
		}

		CString strLinkError;
		json result = addOneLink(strLinkUtf8, strLinkError);
		if (!strLinkError.IsEmpty()) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = strLinkError;
			return json();
		}
		return result;
	}

	auto handleTransferBulkMutation = [&](LPCTSTR pszAction) -> json
	{
		WebApiCommandSeams::STransferBulkMutationRequest request;
		std::string strError;
		if (!WebApiCommandSeams::TryParseTransferBulkMutationRequest(params, request, strError)) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = CStringFromStdUtf8(strError);
			return json();
		}

		bool bUpdateTabs = false;
		json results = json::array();
		json singleResource;
		for (const json &hashValue : request.hashes) {
			SPipeApiError itemError = rError;
			CPartFile *pPartFile = FindPartFileByHash(hashValue, itemError);
			if (pPartFile == NULL) {
				results.push_back(json{
					{"hash", hashValue.is_string() ? hashValue : json()},
					{"ok", false},
					{"error", StdUtf8FromCString(itemError.strMessage)}
				});
				continue;
			}

			const CString strHash(HashToHex(pPartFile->GetFileHash()));
			bool bOk = false;
			CString strErrorText;

			if (_tcscmp(pszAction, _T("pause")) == 0) {
				if (pPartFile->CanPauseFile()) {
					pPartFile->PauseFile();
					bOk = true;
				} else
					strErrorText = _T("transfer cannot be paused");
			} else if (_tcscmp(pszAction, _T("resume")) == 0) {
				if (pPartFile->CanResumeFile()) {
					if (pPartFile->GetStatus() == PS_INSUFFICIENT)
						pPartFile->ResumeFileInsufficient();
					else
						pPartFile->ResumeFile();
					bOk = true;
				} else
					strErrorText = _T("transfer cannot be resumed");
			} else if (_tcscmp(pszAction, _T("stop")) == 0) {
				if (pPartFile->CanStopFile()) {
					pPartFile->StopFile();
					bOk = true;
					bUpdateTabs = true;
				} else
					strErrorText = _T("transfer cannot be stopped");
			} else if (_tcscmp(pszAction, _T("delete")) == 0) {
				const bool bDeleteFiles = request.bDeleteFiles;
				if (pPartFile->GetStatus() == PS_COMPLETE) {
					if (!bDeleteFiles) {
						strErrorText = _T("completed transfer deletion requires deleteFiles=true");
					} else if (!ShellDeleteFile(pPartFile->GetFilePath())) {
						strErrorText = GetErrorMessage(::GetLastError());
					} else {
						theApp.sharedfiles->RemoveFile(pPartFile, true);
						if (theApp.emuledlg->transferwnd->GetDownloadList() != NULL)
							theApp.emuledlg->transferwnd->GetDownloadList()->RemoveFile(pPartFile);
						bOk = true;
					}
				} else if (!bDeleteFiles) {
					strErrorText = _T("partial transfer deletion requires deleteFiles=true");
				} else {
					pPartFile->DeletePartFile();
					bOk = true;
				}
			}

			if (request.hashes.size() == 1 && bOk && _tcscmp(pszAction, _T("delete")) != 0)
				singleResource = BuildTransferJson(*pPartFile);
			results.push_back(BuildMutationResult(strHash, bOk, bOk ? NULL : strErrorText));
			(void)pPartFile;
		}

		if (bUpdateTabs)
			theApp.emuledlg->transferwnd->UpdateCatTabTitles();

		return !singleResource.is_null() ? singleResource : json{{"items", results}};
	};

	if (strCommand == "transfers/pause")
		return handleTransferBulkMutation(_T("pause"));
	if (strCommand == "transfers/resume")
		return handleTransferBulkMutation(_T("resume"));
	if (strCommand == "transfers/stop")
		return handleTransferBulkMutation(_T("stop"));
	if (strCommand == "transfers/delete")
		return handleTransferBulkMutation(_T("delete"));

	if (strCommand == "transfers/recheck") {
		CPartFile *pPartFile = FindPartFileByHash(params.contains("hash") ? params["hash"] : json(), rError);
		if (pPartFile == NULL)
			return json();
		if (!StartPartFileRecheck(*pPartFile, rError))
			return json();
		return json{{"ok", true}};
	}

	if (strCommand == "transfers/preview") {
		CPartFile *const pPartFile = FindPartFileByHash(params.contains("hash") ? params["hash"] : json(), rError);
		if (pPartFile == NULL)
			return json();
		if (!pPartFile->IsReadyForPreview()) {
			rError.strCode = "INVALID_STATE";
			rError.strMessage = _T("transfer is not ready for preview");
			return json();
		}
		pPartFile->PreviewFile();
		return BuildTransferJson(*pPartFile);
	}

	if (strCommand == "transfers/set_priority") {
		CPartFile *const pPartFile = FindPartFileByHash(params.contains("hash") ? params["hash"] : json(), rError);
		if (pPartFile == NULL)
			return json();
		if (!params.contains("priority") || !params["priority"].is_string()) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("priority must be a string");
			return json();
		}

		switch (WebApiSurfaceSeams::ParseTransferPriorityName(params["priority"].get_ref<const std::string&>().c_str())) {
		case WebApiSurfaceSeams::ETransferPriority::Auto:
			pPartFile->SetAutoDownPriority(true);
			break;
		case WebApiSurfaceSeams::ETransferPriority::VeryLow:
			pPartFile->SetAutoDownPriority(false);
			pPartFile->SetDownPriority(PR_VERYLOW);
			break;
		case WebApiSurfaceSeams::ETransferPriority::Low:
			pPartFile->SetAutoDownPriority(false);
			pPartFile->SetDownPriority(PR_LOW);
			break;
		case WebApiSurfaceSeams::ETransferPriority::Normal:
			pPartFile->SetAutoDownPriority(false);
			pPartFile->SetDownPriority(PR_NORMAL);
			break;
		case WebApiSurfaceSeams::ETransferPriority::High:
			pPartFile->SetAutoDownPriority(false);
			pPartFile->SetDownPriority(PR_HIGH);
			break;
		case WebApiSurfaceSeams::ETransferPriority::VeryHigh:
			pPartFile->SetAutoDownPriority(false);
			pPartFile->SetDownPriority(PR_VERYHIGH);
			break;
		case WebApiSurfaceSeams::ETransferPriority::Invalid:
		default:
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("priority must be one of auto, veryLow, low, normal, high, veryHigh");
			return json();
		}

		pPartFile->UpdateDisplayedInfo(true);
		return BuildTransferJson(*pPartFile);
	}

	if (strCommand == "transfers/set_category") {
		CPartFile *const pPartFile = FindPartFileByHash(params.contains("hash") ? params["hash"] : json(), rError);
		if (pPartFile == NULL)
			return json();
		uint64_t uRequestedCategory = 0;
		UINT uCategory = 0;
		if (params.contains("categoryId")) {
			if (!WebApiCommandSeams::TryParseNonNegativeUInt64(params["categoryId"], uRequestedCategory) || uRequestedCategory > UINT_MAX) {
				rError.strCode = "INVALID_ARGUMENT";
				rError.strMessage = _T("categoryId must be an unsigned number");
				return json();
			}
			uCategory = static_cast<UINT>(uRequestedCategory);
		} else if (params.contains("categoryName")) {
			if (!params["categoryName"].is_string()) {
				rError.strCode = "INVALID_ARGUMENT";
				rError.strMessage = _T("categoryName must be a string");
				return json();
			}
			if (!TryResolveCategoryName(CStringFromStdUtf8(params["categoryName"].get<std::string>()), uCategory)) {
				rError.strCode = "INVALID_ARGUMENT";
				rError.strMessage = _T("categoryName does not match a configured category");
				return json();
			}
		} else {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("categoryId or categoryName is required");
			return json();
		}

		if (uCategory >= thePrefs.GetCatCount()) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("category is out of range");
			return json();
		}

		pPartFile->SetCategory(uCategory);
		pPartFile->UpdateDisplayedInfo(true);
		theApp.emuledlg->transferwnd->UpdateCatTabTitles();
		return BuildTransferJson(*pPartFile);
	}

	if (strCommand == "transfers/rename") {
		CPartFile *const pPartFile = FindPartFileByHash(params.contains("hash") ? params["hash"] : json(), rError);
		if (pPartFile == NULL)
			return json();

		WebApiCommandSeams::STransferRenameRequest request;
		std::string strError;
		if (!WebApiCommandSeams::TryParseTransferRenameRequest(params, request, strError)) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = CStringFromStdUtf8(strError);
			return json();
		}

		if (pPartFile->GetStatus() == PS_COMPLETE || pPartFile->GetStatus() == PS_COMPLETING) {
			rError.strCode = "INVALID_STATE";
			rError.strMessage = _T("completed transfers cannot be renamed through this endpoint");
			return json();
		}

		const CString strNewName(CStringFromStdUtf8(request.strName));
		if (!IsValidEd2kString(strNewName)) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("name must be a valid eD2K filename");
			return json();
		}

		pPartFile->SetFileName(strNewName, true);
		pPartFile->UpdateDisplayedInfo(true);
		pPartFile->SavePartFile();
		return BuildTransferJson(*pPartFile);
	}

	if (strCommand == "search/start") {
		CSearchResultsWnd *const pSearchResults = theApp.emuledlg->searchwnd != NULL ? theApp.emuledlg->searchwnd->m_pwndResults : NULL;
		if (pSearchResults == NULL) {
			rError.strCode = "EMULE_UNAVAILABLE";
			rError.strMessage = _T("search window is not available");
			return json();
		}
		WebApiCommandSeams::SSearchStartRequest request;
		std::string strError;
		if (!WebApiCommandSeams::TryParseSearchStartRequest(params, request, strError)) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = CStringFromStdUtf8(strError);
			return json();
		}

		SSearchParams *const pSearchParams = new SSearchParams;
		pSearchParams->strExpression = CStringFromStdUtf8(request.strQuery);
		switch (request.eMethod) {
		case WebApiCommandSeams::ESearchMethod::Automatic:
			pSearchParams->eType = SearchTypeAutomatic;
			break;
		case WebApiCommandSeams::ESearchMethod::Server:
			pSearchParams->eType = SearchTypeEd2kServer;
			break;
		case WebApiCommandSeams::ESearchMethod::Global:
			pSearchParams->eType = SearchTypeEd2kGlobal;
			break;
		case WebApiCommandSeams::ESearchMethod::Kad:
			pSearchParams->eType = SearchTypeKademlia;
			break;
		case WebApiCommandSeams::ESearchMethod::Invalid:
		default:
			delete pSearchParams;
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("method must be one of automatic, server, global, kad");
			return json();
		}

		switch (request.eFileType) {
		case WebApiCommandSeams::ESearchFileType::Any:
			pSearchParams->strFileType = _T(ED2KFTSTR_ANY);
			break;
		case WebApiCommandSeams::ESearchFileType::Archive:
			pSearchParams->strFileType = _T(ED2KFTSTR_ARCHIVE);
			break;
		case WebApiCommandSeams::ESearchFileType::Audio:
			pSearchParams->strFileType = _T(ED2KFTSTR_AUDIO);
			break;
		case WebApiCommandSeams::ESearchFileType::CdImage:
			pSearchParams->strFileType = _T(ED2KFTSTR_CDIMAGE);
			break;
		case WebApiCommandSeams::ESearchFileType::Image:
			pSearchParams->strFileType = _T(ED2KFTSTR_IMAGE);
			break;
		case WebApiCommandSeams::ESearchFileType::Program:
			pSearchParams->strFileType = _T(ED2KFTSTR_PROGRAM);
			break;
		case WebApiCommandSeams::ESearchFileType::Video:
			pSearchParams->strFileType = _T(ED2KFTSTR_VIDEO);
			break;
		case WebApiCommandSeams::ESearchFileType::Document:
			pSearchParams->strFileType = _T(ED2KFTSTR_DOCUMENT);
			break;
		case WebApiCommandSeams::ESearchFileType::EmuleCollection:
			pSearchParams->strFileType = _T(ED2KFTSTR_EMULECOLLECTION);
			break;
		case WebApiCommandSeams::ESearchFileType::Invalid:
		default:
			delete pSearchParams;
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("type is not supported");
			return json();
		}

		pSearchParams->strExtension = CStringFromStdUtf8(request.strExtension);
		if (request.bHasMinSize)
			pSearchParams->ullMinSize = request.ullMinSize;
		if (request.bHasMaxSize)
			pSearchParams->ullMaxSize = request.ullMaxSize;
		if (request.bHasMinAvailability)
			pSearchParams->uAvailability = static_cast<UINT>(request.ullMinAvailability);

		CString strSearchError;
		if (request.bClearExisting)
			pSearchResults->DeleteAllSearches();
		if (!pSearchResults->StartSearchFromApi(pSearchParams, strSearchError)) {
			rError.strCode = "EMULE_ERROR";
			rError.strMessage = strSearchError.IsEmpty() ? CString(_T("failed to start search")) : strSearchError;
			return json();
		}

		return json{
			{"id", StdUtf8FromCString(FormatSearchId(pSearchParams->dwSearchID))},
			{"query", StdUtf8FromCString(pSearchParams->strExpression)},
			{"method", GetSearchMethodName(pSearchParams->eType)},
			{"status", "running"},
			{"results", json::array()}
		};
	}

	if (strCommand == "search/list") {
		if (theApp.emuledlg->searchwnd == NULL || theApp.emuledlg->searchwnd->m_pwndResults == NULL) {
			rError.strCode = "EMULE_UNAVAILABLE";
			rError.strMessage = _T("search window is not available");
			return json();
		}

		return ItemsEnvelopeIfRequested(params, BuildSearchSessionsJson(*theApp.emuledlg->searchwnd->m_pwndResults));
	}

	if (strCommand == "search/results") {
		if (theApp.emuledlg->searchwnd == NULL || theApp.emuledlg->searchwnd->m_pwndResults == NULL) {
			rError.strCode = "EMULE_UNAVAILABLE";
			rError.strMessage = _T("search window is not available");
			return json();
		}

		uint32 uSearchID = 0;
		if (!TryGetSearchId(params.contains("searchId") ? params["searchId"] : json(), uSearchID, rError))
			return json();
		const SSearchParams *const pSearchParams = theApp.emuledlg->searchwnd->m_pwndResults->GetSearchResultsParams(uSearchID);
		if (pSearchParams == NULL) {
			rError.strCode = "NOT_FOUND";
			rError.strMessage = _T("search not found");
			return json();
		}

		CArray<const CSearchFile*, const CSearchFile*> aResults;
		if (!theApp.searchlist->GetVisibleResults(uSearchID, aResults)) {
			if (!theApp.emuledlg->searchwnd->m_pwndResults->IsSearchQueued(uSearchID)) {
				rError.strCode = "NOT_FOUND";
				rError.strMessage = _T("search not found");
				return json();
			}
		}

		json results = json::array();
		for (INT_PTR i = 0; i < aResults.GetCount(); ++i)
			results.push_back(BuildSearchResultJson(*aResults[i], pSearchParams));

		return json{
			{"id", StdUtf8FromCString(FormatSearchId(uSearchID))},
			{"query", StdUtf8FromCString(pSearchParams->strExpression)},
			{"method", GetSearchMethodName(pSearchParams->eType)},
			{"status", theApp.emuledlg->searchwnd->m_pwndResults->IsSearchRunning(uSearchID) ? "running" : "complete"},
			{"results", results}
		};
	}

	if (strCommand == "search/stop") {
		if (theApp.emuledlg->searchwnd == NULL || theApp.emuledlg->searchwnd->m_pwndResults == NULL) {
			rError.strCode = "EMULE_UNAVAILABLE";
			rError.strMessage = _T("search window is not available");
			return json();
		}

		uint32 uSearchID = 0;
		if (!TryGetSearchId(params.contains("searchId") ? params["searchId"] : json(), uSearchID, rError))
			return json();
		if (theApp.emuledlg->searchwnd->m_pwndResults->GetSearchResultsParams(uSearchID) == NULL) {
			rError.strCode = "NOT_FOUND";
			rError.strMessage = _T("search not found");
			return json();
		}

		theApp.emuledlg->searchwnd->m_pwndResults->CancelSearch(uSearchID);
		return json{{"ok", true}};
	}

	if (strCommand == "search/clear") {
		if (theApp.emuledlg == NULL || theApp.emuledlg->searchwnd == NULL) {
			rError.strCode = "EMULE_UNAVAILABLE";
			rError.strMessage = _T("search window is not available");
			return json();
		}
		InvokeWebGuiInteraction(WEBGUIIA_DELETEALLSEARCHES);
		return json{{"ok", true}};
	}

	if (strCommand == "search/download_result") {
		uint32 uSearchID = 0;
		if (!TryGetSearchId(params.contains("searchId") ? params["searchId"] : json(), uSearchID, rError))
			return json();

		uchar hash[MDX_DIGEST_SIZE];
		if (!TryDecodeHash(params.contains("hash") ? params["hash"] : json(), hash, rError))
			return json();
		const CSearchFile *const pSearchFile = FindSearchResultByHash(uSearchID, hash, rError);
		if (pSearchFile == NULL)
			return json();

		uint8 uPaused = 2;
		if (!TryParseOptionalDownloadPaused(params, uPaused, rError))
			return json();

		UINT uCategory = 0;
		if (params.contains("categoryId")) {
			uint64_t uRequestedCategory = 0;
			if (!WebApiCommandSeams::TryParseNonNegativeUInt64(params["categoryId"], uRequestedCategory) || uRequestedCategory > UINT_MAX) {
				rError.strCode = "INVALID_ARGUMENT";
				rError.strMessage = _T("categoryId must be an unsigned number");
				return json();
			}
			uCategory = static_cast<UINT>(uRequestedCategory);
		} else if (params.contains("categoryName")) {
			if (!params["categoryName"].is_string()) {
				rError.strCode = "INVALID_ARGUMENT";
				rError.strMessage = _T("categoryName must be a string");
				return json();
			}
			if (!TryResolveCategoryName(CStringFromStdUtf8(params["categoryName"].get<std::string>()), uCategory)) {
				rError.strCode = "INVALID_ARGUMENT";
				rError.strMessage = _T("categoryName does not match a configured category");
				return json();
			}
		}
		if (uCategory >= thePrefs.GetCatCount()) {
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("category is out of range");
			return json();
		}

		std::unique_ptr<CED2KLink> pLink;
		try {
			pLink.reset(CED2KLink::CreateLinkFromUrl(pSearchFile->GetED2kLink()));
			if (pLink == NULL || pLink->GetKind() != CED2KLink::kFile)
				throw CString(_T("invalid search-result ed2k link"));
			theApp.downloadqueue->AddFileLinkToDownload(*pLink->GetFileLink(), static_cast<int>(uCategory), uPaused);
		} catch (const CString &rCaughtError) {
			rError.strCode = "INVALID_STATE";
			rError.strMessage = rCaughtError;
			return json();
		}
		return json{
			{"ok", true},
			{"searchId", StdUtf8FromCString(FormatSearchId(uSearchID))},
			{"hash", StdUtf8FromCString(HashToHex(hash))}
		};
	}

	if (strCommand == "friends/list")
		return ItemsEnvelopeIfRequested(params, BuildFriendsListJson());

	if (strCommand == "friends/add") {
		uchar hash[MDX_DIGEST_SIZE];
		if (!TryDecodeHash(params.contains("userHash") ? params["userHash"] : json(), hash, rError))
			return json();
		CString strName;
		if (params.contains("name")) {
			if (!params["name"].is_string()) {
				rError.strCode = "INVALID_ARGUMENT";
				rError.strMessage = _T("name must be a string");
				return json();
			}
			strName = CStringFromStdUtf8(params["name"].get<std::string>());
		}
		if (!theApp.friendlist->AddFriend(hash, 0, 0, 0, 0, strName, 1)) {
			CFriend *const pExisting = theApp.friendlist->SearchFriend(hash, 0, 0);
			if (pExisting != NULL)
				return BuildFriendJson(*pExisting);
			rError.strCode = "INVALID_ARGUMENT";
			rError.strMessage = _T("friend cannot be added");
			return json();
		}
		CFriend *const pFriend = theApp.friendlist->SearchFriend(hash, 0, 0);
		return pFriend != NULL ? BuildFriendJson(*pFriend) : json{{"userHash", StdUtf8FromCString(HashToHex(hash))}, {"name", StdUtf8FromCString(strName)}};
	}

	if (strCommand == "friends/remove") {
		CFriend *const pFriend = FindFriendByHashParam(params.contains("userHash") ? params["userHash"] : json(), rError);
		if (pFriend == NULL)
			return json();
		theApp.friendlist->RemoveFriend(pFriend);
		return json{{"ok", true}};
	}

	if (strCommand == "log/get") {
		const size_t maxEntries = static_cast<size_t>(max(1, params.value("limit", 200)));
		return ItemsEnvelopeIfRequested(params, BuildLogEntriesJson(maxEntries));
	}

	if (strCommand == "log/clear") {
		if (theApp.emuledlg != NULL) {
			theApp.emuledlg->ResetLog();
			theApp.emuledlg->ResetDebugLog();
		}
		return json{{"ok", true}};
	}

	rError.strCode = "INVALID_ARGUMENT";
	rError.strMessage.Format(_T("unknown command: %hs"), strCommand.c_str());
	return json();
}
}

namespace
{
json BuildResponseMetaJson()
{
	return json{
		{"apiVersion", "v1"}
	};
}

json BuildSuccessEnvelope(const json &rPayload)
{
	return json{
		{"data", rPayload},
		{"meta", BuildResponseMetaJson()}
	};
}

json BuildErrorEnvelope(LPCSTR pszCode, const CString &strMessage)
{
	return WebServerJsonSeams::BuildErrorEnvelopeJson(
		pszCode != NULL ? pszCode : "",
		StdUtf8FromCString(strMessage));
}

/**
 * Handles REST commands which do not need UI-thread-owned runtime state.
 */
bool TryExecuteDirectRestCommand(const json &rRequest, json &rResult, SPipeApiError &rError)
{
	const std::string strCommand = rRequest.value("cmd", std::string());
	if (strCommand == "app/version") {
		rResult = BuildAppJson(GetRestBuildFlavor());
		rError.strCode.Empty();
		rError.strMessage.Empty();
		return true;
	}
	return false;
}

void SendJsonResponse(CWebSocket *pSocket, const int iStatusCode, LPCSTR pszReason, const json &rPayload)
{
	if (pSocket == NULL)
		return;

	const CStringA strBody(WebServerJson::SerializeJsonUtf8(BuildSuccessEnvelope(rPayload)));
	CStringA strHeader;
	strHeader.Format(
		"HTTP/1.1 %d %s\r\n"
		"Content-Type: application/json; charset=utf-8\r\n"
		"Cache-Control: no-store\r\n"
		"Connection: close\r\n"
		"Content-Length: %u\r\n\r\n",
		iStatusCode,
		pszReason != NULL ? pszReason : "OK",
		static_cast<UINT>(strBody.GetLength()));
	pSocket->SendData(strHeader, strHeader.GetLength());
	if (!strBody.IsEmpty())
		pSocket->SendData(strBody, strBody.GetLength());
}

void SendJsonError(CWebSocket *pSocket, const int iStatusCode, LPCSTR pszReason, LPCSTR pszCode, const CString &strMessage)
{
	if (pSocket == NULL)
		return;

	const CStringA strBody(WebServerJson::SerializeJsonUtf8(BuildErrorEnvelope(pszCode, strMessage)));
	CStringA strHeader;
	strHeader.Format(
		"HTTP/1.1 %d %s\r\n"
		"Content-Type: application/json; charset=utf-8\r\n"
		"Cache-Control: no-store\r\n"
		"Connection: close\r\n"
		"Content-Length: %u\r\n\r\n",
		iStatusCode,
		pszReason != NULL ? pszReason : "Error",
		static_cast<UINT>(strBody.GetLength()));
	pSocket->SendData(strHeader, strHeader.GetLength());
	if (!strBody.IsEmpty())
		pSocket->SendData(strBody, strBody.GetLength());
}

LPCSTR GetHttpReasonPhrase(const int iStatusCode)
{
	switch (iStatusCode) {
	case 200:
		return "OK";
	case 400:
		return "Bad Request";
	case 401:
		return "Unauthorized";
	case 404:
		return "Not Found";
	case 405:
		return "Method Not Allowed";
	case 409:
		return "Conflict";
	case 500:
		return "Internal Server Error";
	case 503:
		return "Service Unavailable";
	default:
		return "Error";
	}
}

json ExecuteUiThreadCommand(const json &rRequest, SPipeApiError &rError)
{
	if (theApp.emuledlg == NULL || theApp.emuledlg->GetSafeHwnd() == NULL) {
		rError.strCode = "EMULE_UNAVAILABLE";
		rError.strMessage = _T("main window is not available");
		return json();
	}

	if (::GetCurrentThreadId() == g_uMainThreadId)
		return HandleUiCommand(rRequest, rError);

	SRestDispatchContext *pContext = NULL;
	try {
		pContext = new SRestDispatchContext(rRequest);
	} catch (...) {
		rError.strCode = "EMULE_ERROR";
		rError.strMessage = _T("unable to allocate REST dispatch context");
		return json();
	}

	if (pContext->hComplete == NULL) {
		pContext->Release();
		rError.strCode = "EMULE_ERROR";
		rError.strMessage = _T("unable to create REST dispatch completion event");
		return json();
	}

	pContext->AddRef(); // posted message ownership
	if (!::PostMessage(theApp.emuledlg->m_hWnd, WEB_REST_API_COMMAND, 0, reinterpret_cast<LPARAM>(pContext))) {
		pContext->Release();
		pContext->Release();
		rError.strCode = "EMULE_UNAVAILABLE";
		rError.strMessage = _T("main window did not accept REST command");
		return json();
	}

	const DWORD dwWait = ::WaitForSingleObject(pContext->hComplete, static_cast<DWORD>(WebServerJsonSeams::kRestUiDispatchTimeoutMs));
	if (dwWait == WAIT_OBJECT_0) {
		json result(pContext->result);
		rError = pContext->error;
		pContext->Release();
		return result;
	}

	::InterlockedExchange(&pContext->lAbandoned, 1);
	pContext->Release();
	if (dwWait == WAIT_TIMEOUT) {
		rError.strCode = "EMULE_UNAVAILABLE";
		rError.strMessage.Format(
			_T("main window did not process REST command within %u ms"),
			static_cast<unsigned int>(WebServerJsonSeams::kRestUiDispatchTimeoutMs));
		DebugLogWarning(_T("REST API UI dispatch timed out after %u ms"), static_cast<unsigned int>(WebServerJsonSeams::kRestUiDispatchTimeoutMs));
		return json();
	}

	rError.strCode = "EMULE_UNAVAILABLE";
	rError.strMessage.Format(_T("failed to wait for REST UI dispatch completion - Error %lu"), ::GetLastError());
	return json();
}
}

std::string WebServerJson::ToStdString(const CStringA &rText)
{
	return StdStringFromCStringA(rText);
}

std::string WebServerJson::ToStdUtf8(const CString &rText)
{
	return StdUtf8FromCString(rText);
}

CString WebServerJson::FromStdUtf8(const std::string &rText)
{
	return CStringFromStdUtf8(rText);
}

CStringA WebServerJson::SerializeJsonUtf8(const nlohmann::json &rPayload)
{
	const std::string strSerialized = rPayload.dump(-1, ' ', false, json::error_handler_t::replace);
	return CStringA(strSerialized.c_str(), static_cast<int>(strSerialized.size()));
}

void WebServerJson::RunDispatchedCommand(void *pContext)
{
	SRestDispatchContext *const pDispatch = reinterpret_cast<SRestDispatchContext*>(pContext);
	if (pDispatch == NULL)
		return;

	if (::InterlockedCompareExchange(&pDispatch->lAbandoned, 0, 0) == 0) {
		try {
			pDispatch->result = HandleUiCommand(pDispatch->request, pDispatch->error);
		} catch (const json::exception &rJsonError) {
			pDispatch->error.strCode = "EMULE_ERROR";
			pDispatch->error.strMessage.Format(_T("JSON serialization failure: %hs"), rJsonError.what());
		} catch (CException *pException) {
			TCHAR szError[512] = {0};
			if (pException != NULL) {
				pException->GetErrorMessage(szError, _countof(szError));
				pException->Delete();
			}
			pDispatch->error.strCode = "EMULE_ERROR";
			pDispatch->error.strMessage = szError[0] != _T('\0') ? CString(szError) : CString(_T("REST UI command failed"));
		} catch (...) {
			pDispatch->error.strCode = "EMULE_ERROR";
			pDispatch->error.strMessage = _T("REST UI command failed");
		}
	}
	if (pDispatch->hComplete != NULL)
		VERIFY(::SetEvent(pDispatch->hComplete));
	pDispatch->Release();
}

bool WebServerJson::ExecuteInternalCommand(const nlohmann::json &rRequest, nlohmann::json &rResult, CStringA &rErrorCode, CString &rErrorMessage)
{
	rErrorCode.Empty();
	rErrorMessage.Empty();
	try {
		SPipeApiError error;
		if (!TryExecuteDirectRestCommand(rRequest, rResult, error))
			rResult = ExecuteUiThreadCommand(rRequest, error);
		if (error.strCode.IsEmpty())
			return true;
		rErrorCode = error.strCode;
		rErrorMessage = error.strMessage;
	} catch (const json::exception &rJsonError) {
		rErrorCode = "EMULE_ERROR";
		rErrorMessage.Format(_T("JSON serialization failure: %hs"), rJsonError.what());
	} catch (CException *pException) {
		TCHAR szError[512] = {0};
		if (pException != NULL) {
			pException->GetErrorMessage(szError, _countof(szError));
			pException->Delete();
		}
		rErrorCode = "EMULE_ERROR";
		rErrorMessage = szError[0] != _T('\0') ? CString(szError) : CString(_T("REST UI command failed"));
	} catch (...) {
		rErrorCode = "EMULE_ERROR";
		rErrorMessage = _T("REST UI command failed");
	}
	return false;
}

nlohmann::json WebServerJson::BuildInternalCommand(const char *pszCommand, const nlohmann::json &rParams)
{
	return json{{"cmd", pszCommand != NULL ? pszCommand : ""}, {"params", rParams}};
}

bool WebServerJson::IsApiRequest(const ThreadData &rData)
{
	return WebServerJsonSeams::IsApiRequestTarget(StdStringFromCStringA(rData.strRequestTarget));
}

void WebServerJson::ProcessRequest(const ThreadData &rData)
{
	if (rData.pSocket == NULL)
		return;

	const WebServerJsonSeams::SApiAuthResult auth = WebServerJsonSeams::ValidateApiKey(
		StdUtf8FromCString(thePrefs.GetWSApiKey()),
		StdStringFromCStringA(rData.strApiKey));
	if (!auth.bAllowed) {
		const int iStatus = WebServerJsonSeams::GetHttpStatusForError(auth.strErrorCode);
		SendJsonError(
			rData.pSocket,
			iStatus,
			GetHttpReasonPhrase(iStatus),
			auth.strErrorCode.c_str(),
			CStringFromStdUtf8(auth.strErrorMessage));
		return;
	}

	WebServerJsonSeams::SApiRoute route;
	std::string strRouteErrorCode;
	std::string strRouteErrorMessage;
	if (!WebServerJsonSeams::TryBuildRoute(
		StdStringFromCStringA(rData.strMethod),
		StdStringFromCStringA(rData.strRequestTarget),
		StdStringFromCStringA(rData.strRequestBody),
		route,
		strRouteErrorCode,
		strRouteErrorMessage,
		StdStringFromCStringA(rData.strContentType)))
	{
		const int iStatus = WebServerJsonSeams::GetHttpStatusForError(strRouteErrorCode);
		SendJsonError(
			rData.pSocket,
			iStatus,
			GetHttpReasonPhrase(iStatus),
			strRouteErrorCode.c_str(),
			CStringFromStdUtf8(strRouteErrorMessage));
		return;
	}

	SPipeApiError error;
	const json request{
		{"cmd", route.strCommand},
		{"params", route.params}
	};

	try {
		json result;
		if (!TryExecuteDirectRestCommand(request, result, error))
			result = ExecuteUiThreadCommand(request, error);
		if (error.strCode.IsEmpty())
			SendJsonResponse(rData.pSocket, 200, "OK", result);
		else {
			const int iStatus = WebServerJsonSeams::GetHttpStatusForError(StdStringFromCStringA(error.strCode));
			SendJsonError(rData.pSocket, iStatus, GetHttpReasonPhrase(iStatus), error.strCode, error.strMessage);
		}
	} catch (const json::exception &rJsonError) {
		CString strMessage;
		strMessage.Format(_T("JSON serialization failure: %hs"), rJsonError.what());
		SendJsonError(rData.pSocket, 500, "Internal Server Error", "EMULE_ERROR", strMessage);
	}
}
