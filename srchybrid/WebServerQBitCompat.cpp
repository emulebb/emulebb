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
#include "WebServerQBitCompat.h"

#include <bcrypt.h>
#include <map>
#include <string>
#include <vector>

#include "Emule.h"
#include "Opcodes.h"
#include "Log.h"
#include "OtherFunctions.h"
#include "DownloadQueue.h"
#include "PartFile.h"
#include "Preferences.h"
#include "WebApiSurfaceSeams.h"
#include "WebServerJson.h"
#include "WebServerJsonSeams.h"
#include "WebServerQBitCompatSeams.h"
#include "WebSocket.h"

using json = nlohmann::json;

namespace
{
CCriticalSection g_qbitSessionIdLock;

void SendTextResponse(CWebSocket *pSocket, const int iStatusCode, LPCSTR pszReason, const CStringA &rBody, LPCSTR pszExtraHeaders = NULL)
{
	if (pSocket == NULL)
		return;

	CStringA strHeader;
	strHeader.Format(
		"HTTP/1.1 %d %s\r\n"
		"%s"
		"Cache-Control: no-store\r\n"
		"Connection: close\r\n"
		"%s"
		"Content-Length: %u\r\n\r\n",
		iStatusCode,
		pszReason != NULL ? pszReason : "OK",
		WebServerQBitCompatSeams::kQBitTextContentTypeHeader,
		pszExtraHeaders != NULL ? pszExtraHeaders : "",
		static_cast<UINT>(rBody.GetLength()));
	pSocket->SendData(strHeader, strHeader.GetLength());
	if (!rBody.IsEmpty())
		pSocket->SendData(rBody, rBody.GetLength());
}

void SendJsonResponse(CWebSocket *pSocket, const int iStatusCode, LPCSTR pszReason, const json &rPayload)
{
	if (pSocket == NULL)
		return;

	const CStringA strBody(WebServerJson::SerializeJsonUtf8(rPayload));
	CStringA strHeader;
	strHeader.Format(
		"HTTP/1.1 %d %s\r\n"
		"%s"
		"Cache-Control: no-store\r\n"
		"Connection: close\r\n"
		"Content-Length: %u\r\n\r\n",
		iStatusCode,
		pszReason != NULL ? pszReason : "OK",
		WebServerQBitCompatSeams::kQBitJsonContentTypeHeader,
		static_cast<UINT>(strBody.GetLength()));
	pSocket->SendData(strHeader, strHeader.GetLength());
	if (!strBody.IsEmpty())
		pSocket->SendData(strBody, strBody.GetLength());
}

std::string HexEncode(const BYTE *pData, const size_t uSize)
{
	static const char s_hex[] = "0123456789abcdef";
	std::string result;
	result.reserve(uSize * 2);
	for (size_t i = 0; i < uSize; ++i) {
		result.push_back(s_hex[(pData[i] >> 4) & 0x0F]);
		result.push_back(s_hex[pData[i] & 0x0F]);
	}
	return result;
}

std::string GetSessionId()
{
	CSingleLock lock(&g_qbitSessionIdLock, TRUE);
	static std::string s_strSessionId;
	if (s_strSessionId.empty()) {
		BYTE random[16] = {};
		if (::BCryptGenRandom(NULL, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
			return std::string();
		s_strSessionId = HexEncode(random, sizeof(random));
	}
	return s_strSessionId;
}

bool HasValidSessionCookie(const CStringA &rCookie)
{
	return WebServerQBitCompatSeams::HasCookiePair(WebServerJson::ToStdString(rCookie), "SID", GetSessionId());
}

bool ExecuteBridgeCommand(const json &rCommand, json &rResult, CString &rErrorMessage)
{
	CStringA strErrorCode;
	if (WebServerJson::ExecuteInternalCommand(rCommand, rResult, strErrorCode, rErrorMessage))
		return true;
	if (!strErrorCode.IsEmpty())
		AddLogLine(false, _T("eMule BB qBittorrent Compatibility: native command failed (%hs)"), (LPCSTR)strErrorCode);
	return false;
}

bool TryGetConfiguredCategories(json &rCategories)
{
	CString strErrorMessage;
	return ExecuteBridgeCommand(WebServerJson::BuildInternalCommand("categories/list", json::object()), rCategories, strErrorMessage) && rCategories.is_array();
}

json BuildQBitCategoriesJson()
{
	json categories = json::object();
	json nativeCategories;
	if (!TryGetConfiguredCategories(nativeCategories)) {
		categories.emplace("Default", json{{"name", "Default"}, {"savePath", ""}});
		return categories;
	}

	for (const json &rCategory : nativeCategories) {
		if (!rCategory.is_object() || !rCategory.contains("name") || !rCategory["name"].is_string())
			continue;
		const std::string strName(rCategory["name"].get<std::string>());
		if (strName.empty())
			continue;
		json category;
		category["name"] = strName;
		category["savePath"] = rCategory.contains("path") && !rCategory["path"].is_null() && rCategory["path"].is_string() ? rCategory["path"].get<std::string>() : std::string();
		categories.emplace(strName, std::move(category));
	}
	return categories;
}

bool EnsureCategoryExists(const std::string &rCategory, CString &rErrorMessage)
{
	if (rCategory.empty())
		return true;

	json categories;
	if (TryGetConfiguredCategories(categories)) {
		for (const json &rExisting : categories) {
			if (rExisting.is_object() && rExisting.value("name", std::string()) == rCategory)
				return true;
		}
	}

	json ignored;
	return ExecuteBridgeCommand(WebServerJson::BuildInternalCommand("categories/create", json{{"name", rCategory}}), ignored, rErrorMessage);
}

CString GetPartFileCategoryName(const UINT uCategory)
{
	const Category_Struct *const pCategory = thePrefs.GetCategory(static_cast<INT_PTR>(uCategory));
	return pCategory != NULL ? pCategory->strTitle : CString();
}

json BuildQBitTorrentJson(const CPartFile &rPartFile)
{
	const CString strName(rPartFile.GetFileName());
	const CString strCategory(GetPartFileCategoryName(const_cast<CPartFile&>(rPartFile).GetCategory()));
	const uint64 uSize = static_cast<uint64>(rPartFile.GetFileSize());
	const uint64 uCompleted = static_cast<uint64>(rPartFile.GetCompletedSize());
	const std::string strState = rPartFile.GetStatus() == PS_PAUSED || rPartFile.IsStopped() ? "pausedDL" : "downloading";
	return json{
		{"hash", HexEncode(rPartFile.GetFileHash(), MDX_DIGEST_SIZE)},
		{"name", WebServerJson::ToStdUtf8(strName)},
		{"size", uSize},
		{"progress", WebApiSurfaceSeams::BuildTransferProgressRatio(uCompleted, uSize)},
		{"eta", -1},
		{"state", strState},
		{"label", WebServerJson::ToStdUtf8(strCategory)},
		{"category", WebServerJson::ToStdUtf8(strCategory)},
		{"save_path", ""},
		{"content_path", WebServerJson::ToStdUtf8(strName)},
		{"ratio", 0},
		{"ratio_limit", 0},
		{"seeding_time", 0},
		{"seeding_time_limit", 0},
		{"inactive_seeding_time_limit", 0},
		{"downloaded", uCompleted},
		{"completed", uCompleted},
		{"dlspeed", rPartFile.GetDatarate()},
		{"upspeed", 0},
		{"num_leechs", rPartFile.GetSourceCount()},
		{"num_incomplete", rPartFile.GetSourceCount()},
		{"num_seeds", 0},
		{"num_complete", 0}
	};
}

json BuildQBitTorrentsJson(const std::string &rCategory)
{
	if (theApp.downloadqueue == NULL)
		return json::array();

	json torrents = json::array();
	POSITION pos = NULL;
	for (INT_PTR i = 0, iCount = theApp.downloadqueue->GetFileCount(); i < iCount; ++i) {
		CPartFile *const pPartFile = theApp.downloadqueue->GetFileNext(pos);
		if (pPartFile == NULL)
			break;
		if (!rCategory.empty() && WebServerJson::ToStdUtf8(GetPartFileCategoryName(pPartFile->GetCategory())) != rCategory)
			continue;
		torrents.push_back(BuildQBitTorrentJson(*pPartFile));
	}
	return torrents;
}

void HandleLogin(const ThreadData &rData)
{
	if (thePrefs.GetWSApiKey().IsEmpty()) {
		SendTextResponse(rData.pSocket, 503, "Service Unavailable", WebServerQBitCompatSeams::kQBitFailureBody);
		return;
	}

	std::map<std::string, std::string> form;
	std::string strError;
	if (!WebServerQBitCompatSeams::TryParseFormBody(WebServerJson::ToStdString(rData.strRequestBody), form, strError)) {
		SendTextResponse(rData.pSocket, 400, "Bad Request", WebServerQBitCompatSeams::kQBitFailureBody);
		return;
	}

	if (!WebServerQBitCompatSeams::IsValidLoginForm(form, "emule", WebServerJson::ToStdUtf8(thePrefs.GetWSApiKey()))) {
		SendTextResponse(rData.pSocket, 200, "OK", WebServerQBitCompatSeams::kQBitFailureBody);
		return;
	}

	const std::string strSessionId(GetSessionId());
	if (strSessionId.empty()) {
		SendTextResponse(rData.pSocket, 503, "Service Unavailable", WebServerQBitCompatSeams::kQBitFailureBody);
		return;
	}

	CStringA strCookieHeader;
	strCookieHeader.Format("Set-Cookie: SID=%s; Path=/; HttpOnly\r\n", strSessionId.c_str());
	SendTextResponse(rData.pSocket, 200, "OK", WebServerQBitCompatSeams::kQBitSuccessBody, strCookieHeader);
}

void HandleTorrentAdd(const ThreadData &rData)
{
	WebServerQBitCompatSeams::SQBitTorrentAddRequest request;
	std::string strError;
	if (!WebServerQBitCompatSeams::TryParseTorrentAddRequest(WebServerJson::ToStdString(rData.strRequestBody), request, strError)) {
		SendTextResponse(rData.pSocket, 400, "Bad Request", WebServerQBitCompatSeams::kQBitFailureBody);
		return;
	}

	CString strErrorMessage;
	if (!EnsureCategoryExists(request.strCategory, strErrorMessage)) {
		SendTextResponse(rData.pSocket, 400, "Bad Request", WebServerQBitCompatSeams::kQBitFailureBody);
		return;
	}

	json params{
		{"link", request.strUrl},
		{"paused", request.bPaused}
	};
	if (!request.strCategory.empty())
		params["categoryName"] = request.strCategory;

	json result;
	if (!ExecuteBridgeCommand(WebServerJson::BuildInternalCommand("transfers/add", params), result, strErrorMessage)) {
		SendTextResponse(rData.pSocket, 400, "Bad Request", WebServerQBitCompatSeams::kQBitFailureBody);
		return;
	}

	SendTextResponse(rData.pSocket, 200, "OK", WebServerQBitCompatSeams::kQBitSuccessBody);
}

bool ExecuteHashBulkCommand(const char *pszCommand, const std::vector<std::string> &rHashes, const json &rExtraParams, CString &rErrorMessage)
{
	json params = rExtraParams;
	params["hashes"] = rHashes;
	json result;
	return ExecuteBridgeCommand(WebServerJson::BuildInternalCommand(pszCommand, params), result, rErrorMessage);
}

void HandleTorrentDelete(const ThreadData &rData)
{
	WebServerQBitCompatSeams::SQBitHashMutationRequest request;
	std::string strError;
	if (!WebServerQBitCompatSeams::TryParseDeleteRequest(WebServerJson::ToStdString(rData.strRequestBody), request, strError)) {
		SendTextResponse(rData.pSocket, 400, "Bad Request", WebServerQBitCompatSeams::kQBitFailureBody);
		return;
	}

	CString strErrorMessage;
	if (!ExecuteHashBulkCommand("transfers/delete", request.hashes, json{{"deleteFiles", request.bDeleteFiles}}, strErrorMessage)) {
		SendTextResponse(rData.pSocket, 400, "Bad Request", WebServerQBitCompatSeams::kQBitFailureBody);
		return;
	}
	SendTextResponse(rData.pSocket, 200, "OK", WebServerQBitCompatSeams::kQBitSuccessBody);
}

void HandleTorrentSetCategory(const ThreadData &rData)
{
	WebServerQBitCompatSeams::SQBitHashMutationRequest request;
	std::string strError;
	if (!WebServerQBitCompatSeams::TryParseSetCategoryRequest(WebServerJson::ToStdString(rData.strRequestBody), request, strError)) {
		SendTextResponse(rData.pSocket, 400, "Bad Request", WebServerQBitCompatSeams::kQBitFailureBody);
		return;
	}

	CString strErrorMessage;
	if (!EnsureCategoryExists(request.strCategory, strErrorMessage)) {
		SendTextResponse(rData.pSocket, 400, "Bad Request", WebServerQBitCompatSeams::kQBitFailureBody);
		return;
	}

	for (const std::string &rHash : request.hashes) {
		json ignored;
		if (!ExecuteBridgeCommand(WebServerJson::BuildInternalCommand("transfers/set_category", json{{"hash", rHash}, {"categoryName", request.strCategory}}), ignored, strErrorMessage)) {
			SendTextResponse(rData.pSocket, 400, "Bad Request", WebServerQBitCompatSeams::kQBitFailureBody);
			return;
		}
	}
	SendTextResponse(rData.pSocket, 200, "OK", WebServerQBitCompatSeams::kQBitSuccessBody);
}

void HandleTorrentStateMutation(const ThreadData &rData, const char *pszCommand)
{
	WebServerQBitCompatSeams::SQBitHashMutationRequest request;
	std::string strError;
	if (!WebServerQBitCompatSeams::TryParseHashesOnlyRequest(WebServerJson::ToStdString(rData.strRequestBody), request, strError)) {
		SendTextResponse(rData.pSocket, 400, "Bad Request", WebServerQBitCompatSeams::kQBitFailureBody);
		return;
	}

	CString strErrorMessage;
	if (!ExecuteHashBulkCommand(pszCommand, request.hashes, json::object(), strErrorMessage)) {
		SendTextResponse(rData.pSocket, 400, "Bad Request", WebServerQBitCompatSeams::kQBitFailureBody);
		return;
	}
	SendTextResponse(rData.pSocket, 200, "OK", WebServerQBitCompatSeams::kQBitSuccessBody);
}

void HandleAcceptedTorrentNoopMutation(const ThreadData &rData, const bool bValidateForceStartValue)
{
	WebServerQBitCompatSeams::SQBitHashMutationRequest request;
	std::string strError;
	const std::string strRequestBody(WebServerJson::ToStdString(rData.strRequestBody));
	const bool bParsed = bValidateForceStartValue
		? WebServerQBitCompatSeams::TryParseForceStartRequest(strRequestBody, request, strError)
		: WebServerQBitCompatSeams::TryParseHashesOnlyRequest(strRequestBody, request, strError);
	if (!bParsed) {
		SendTextResponse(rData.pSocket, 400, "Bad Request", WebServerQBitCompatSeams::kQBitFailureBody);
		return;
	}
	SendTextResponse(rData.pSocket, 200, "OK", WebServerQBitCompatSeams::kQBitSuccessBody);
}
}

bool WebServerQBitCompat::IsCompatRequest(const ThreadData &rData)
{
	return WebServerQBitCompatSeams::IsQBitRequestTarget(WebServerJson::ToStdString(rData.strRequestTarget));
}

void WebServerQBitCompat::ProcessRequest(const ThreadData &rData)
{
	if (rData.pSocket == NULL)
		return;

	const std::string strRequestTarget(WebServerJson::ToStdString(rData.strRequestTarget));
	std::string strPath;
	std::string strPathError;
	if (!WebServerQBitCompatSeams::TryGetQBitRequestPathLower(strRequestTarget, strPath, strPathError)) {
		SendTextResponse(rData.pSocket, 400, "Bad Request", WebServerQBitCompatSeams::kQBitFailureBody);
		return;
	}
	const std::string strMethodRaw(WebServerJson::ToStdString(rData.strMethod));
	if (strMethodRaw != "GET" && strMethodRaw != "POST") {
		SendTextResponse(rData.pSocket, 404, "Not Found", WebServerQBitCompatSeams::kQBitNotFoundBody);
		return;
	}
	const WebServerQBitCompatSeams::SQBitRouteSpec *const pRouteSpec = WebServerQBitCompatSeams::FindQBitRouteSpec(strMethodRaw, strPath);
	if (pRouteSpec == NULL) {
		SendTextResponse(rData.pSocket, 404, "Not Found", WebServerQBitCompatSeams::kQBitNotFoundBody);
		return;
	}
	if (strMethodRaw == "POST") {
		std::string strMetadataError;
		if (!WebServerQBitCompatSeams::TryValidateFormRequestMetadata(
			WebServerJson::ToStdString(rData.strRequestBody),
			WebServerJson::ToStdString(rData.strContentType),
			strMetadataError))
		{
			SendTextResponse(rData.pSocket, 400, "Bad Request", WebServerQBitCompatSeams::kQBitFailureBody);
			return;
		}
	}

	if (strPath == "/api/v2/app/webapiversion") {
		SendTextResponse(rData.pSocket, 200, "OK", "2.11.0");
		return;
	}

	if (strPath == "/api/v2/auth/login") {
		HandleLogin(rData);
		return;
	}

	if (pRouteSpec->bRequiresAuth && thePrefs.GetWSApiKey().IsEmpty()) {
		SendTextResponse(rData.pSocket, 503, "Service Unavailable", "eMule REST API key is not configured");
		return;
	}

	if (pRouteSpec->bRequiresAuth && !HasValidSessionCookie(rData.strCookie)) {
		SendTextResponse(rData.pSocket, 403, "Forbidden", "Forbidden");
		return;
	}

	if (strPath == "/api/v2/app/version") {
		SendTextResponse(rData.pSocket, 200, "OK", "v4.6.0-emulebb");
		return;
	}

	if (strPath == "/api/v2/app/preferences") {
		SendJsonResponse(rData.pSocket, 200, "OK", json{
			{"save_path", ""},
			{"max_ratio_enabled", true},
			{"max_ratio", 0},
			{"max_seeding_time_enabled", true},
			{"max_seeding_time", 0},
			{"max_inactive_seeding_time_enabled", true},
			{"max_inactive_seeding_time", 0},
			{"max_ratio_act", 0},
			{"queueing_enabled", true},
			{"dht", true}
		});
		return;
	}

	if (strPath == "/api/v2/torrents/categories") {
		SendJsonResponse(rData.pSocket, 200, "OK", BuildQBitCategoriesJson());
		return;
	}

	if (strPath == "/api/v2/torrents/createcategory") {
		std::string strError;
		std::string strCategory;
		if (!WebServerQBitCompatSeams::TryParseCreateCategoryRequest(WebServerJson::ToStdString(rData.strRequestBody), strCategory, strError)) {
			SendTextResponse(rData.pSocket, 400, "Bad Request", WebServerQBitCompatSeams::kQBitFailureBody);
			return;
		}
		CString strErrorMessage;
		if (!EnsureCategoryExists(strCategory, strErrorMessage)) {
			SendTextResponse(rData.pSocket, 400, "Bad Request", WebServerQBitCompatSeams::kQBitFailureBody);
			return;
		}
		SendTextResponse(rData.pSocket, 200, "OK", WebServerQBitCompatSeams::kQBitSuccessBody);
		return;
	}

	if (strPath == "/api/v2/torrents/info") {
		std::string strCategory;
		std::string strError;
		if (!WebServerQBitCompatSeams::TryGetOptionalCategoryQueryParam(strRequestTarget, strCategory, strError)) {
			SendTextResponse(rData.pSocket, 400, "Bad Request", WebServerQBitCompatSeams::kQBitFailureBody);
			return;
		}
		SendJsonResponse(rData.pSocket, 200, "OK", BuildQBitTorrentsJson(strCategory));
		return;
	}

	if (strPath == "/api/v2/torrents/properties") {
		std::string strHash;
		std::string strError;
		if (!WebServerQBitCompatSeams::TryGetRequiredHashQueryParam(strRequestTarget, strHash, strError)) {
			SendTextResponse(rData.pSocket, 400, "Bad Request", "hash is required");
			return;
		}
		json transfer;
		CString strErrorMessage;
		if (!ExecuteBridgeCommand(WebServerJson::BuildInternalCommand("transfers/get", json{{"hash", strHash}}), transfer, strErrorMessage) || !transfer.is_object()) {
			SendTextResponse(rData.pSocket, 404, "Not Found", WebServerQBitCompatSeams::kQBitNotFoundBody);
			return;
		}
		SendJsonResponse(rData.pSocket, 200, "OK", json{
			{"hash", strHash},
			{"save_path", ""},
			{"seeding_time", 0}
		});
		return;
	}

	if (strPath == "/api/v2/torrents/files") {
		std::string strHash;
		std::string strError;
		if (!WebServerQBitCompatSeams::TryGetRequiredHashQueryParam(strRequestTarget, strHash, strError)) {
			SendTextResponse(rData.pSocket, 400, "Bad Request", "hash is required");
			return;
		}
		json transfer;
		CString strErrorMessage;
		if (!ExecuteBridgeCommand(WebServerJson::BuildInternalCommand("transfers/get", json{{"hash", strHash}}), transfer, strErrorMessage) || !transfer.is_object()) {
			SendTextResponse(rData.pSocket, 404, "Not Found", WebServerQBitCompatSeams::kQBitNotFoundBody);
			return;
		}
		SendJsonResponse(rData.pSocket, 200, "OK", json::array({json{{"name", transfer.value("name", std::string())}}}));
		return;
	}

	if (strPath == "/api/v2/torrents/add") {
		HandleTorrentAdd(rData);
		return;
	}

	if (strPath == "/api/v2/torrents/delete") {
		HandleTorrentDelete(rData);
		return;
	}

	if (strPath == "/api/v2/torrents/setcategory") {
		HandleTorrentSetCategory(rData);
		return;
	}

	if (strPath == "/api/v2/torrents/pause" || strPath == "/api/v2/torrents/stop") {
		HandleTorrentStateMutation(rData, "transfers/pause");
		return;
	}

	if (strPath == "/api/v2/torrents/resume" || strPath == "/api/v2/torrents/start") {
		HandleTorrentStateMutation(rData, "transfers/resume");
		return;
	}

	if (strPath == "/api/v2/torrents/setsharelimits" || strPath == "/api/v2/torrents/topprio") {
		HandleAcceptedTorrentNoopMutation(rData, false);
		return;
	}

	if (strPath == "/api/v2/torrents/setforcestart") {
		HandleAcceptedTorrentNoopMutation(rData, true);
		return;
	}

	SendTextResponse(rData.pSocket, 404, "Not Found", WebServerQBitCompatSeams::kQBitNotFoundBody);
}
