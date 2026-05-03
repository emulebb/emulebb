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

#include "Log.h"
#include "Preferences.h"
#include "StringConversion.h"
#include "WebServerJson.h"
#include "WebServerJsonSeams.h"
#include "WebServerQBitCompatSeams.h"
#include "WebSocket.h"

using json = nlohmann::json;

namespace
{
std::string StdStringFromCStringA(const CStringA &rText)
{
	return std::string((LPCSTR)rText, rText.GetLength());
}

std::string StdUtf8FromCString(const CString &rText)
{
	const CStringA utf8(StrToUtf8(rText));
	return std::string((LPCSTR)utf8, utf8.GetLength());
}

CString CStringFromStdUtf8(const std::string &rText)
{
	return OptUtf8ToStr(CStringA(rText.c_str(), static_cast<int>(rText.size())));
}

CStringA JsonDump(const json &rPayload)
{
	const std::string text(rPayload.dump());
	return CStringA(text.c_str(), static_cast<int>(text.size()));
}

void SendTextResponse(CWebSocket *pSocket, const int iStatusCode, LPCSTR pszReason, const CStringA &rBody, LPCSTR pszExtraHeaders = NULL)
{
	if (pSocket == NULL)
		return;

	CStringA strHeader;
	strHeader.Format(
		"HTTP/1.1 %d %s\r\n"
		"Content-Type: text/plain; charset=utf-8\r\n"
		"Cache-Control: no-store\r\n"
		"%s"
		"Content-Length: %u\r\n\r\n",
		iStatusCode,
		pszReason != NULL ? pszReason : "OK",
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

	const CStringA strBody(JsonDump(rPayload));
	CStringA strHeader;
	strHeader.Format(
		"HTTP/1.1 %d %s\r\n"
		"Content-Type: application/json; charset=utf-8\r\n"
		"Cache-Control: no-store\r\n"
		"Content-Length: %u\r\n\r\n",
		iStatusCode,
		pszReason != NULL ? pszReason : "OK",
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
	static std::string s_strSessionId;
	if (s_strSessionId.empty()) {
		BYTE random[16] = {};
		if (::BCryptGenRandom(NULL, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
			const ULONGLONG ullFallback = ::GetTickCount64();
			memcpy(random, &ullFallback, min(sizeof(random), sizeof(ullFallback)));
			const DWORD dwThreadId = ::GetCurrentThreadId();
			memcpy(random + 8, &dwThreadId, min(sizeof(random) - 8, sizeof(dwThreadId)));
		}
		s_strSessionId = HexEncode(random, sizeof(random));
	}
	return s_strSessionId;
}

bool HasValidSessionCookie(const CStringA &rCookie)
{
	const std::string strCookie(StdStringFromCStringA(rCookie));
	const std::string strNeedle("SID=" + GetSessionId());
	return strCookie == strNeedle
		|| strCookie.rfind(strNeedle + ";", 0) == 0
		|| strCookie.find("; " + strNeedle + ";") != std::string::npos
		|| strCookie.size() >= strNeedle.size() + 2 && strCookie.compare(strCookie.size() - strNeedle.size(), strNeedle.size(), strNeedle) == 0;
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

json BuildCommand(const char *pszCommand, const json &rParams)
{
	return json{{"cmd", pszCommand}, {"params", rParams}};
}

bool TryGetConfiguredCategories(json &rCategories)
{
	CString strErrorMessage;
	return ExecuteBridgeCommand(BuildCommand("categories/list", json::object()), rCategories, strErrorMessage) && rCategories.is_array();
}

json BuildQBitCategoriesJson()
{
	json categories = json::object();
	json nativeCategories;
	if (!TryGetConfiguredCategories(nativeCategories)) {
		categories["Default"] = json{{"name", "Default"}, {"savePath", ""}};
		return categories;
	}

	for (const json &rCategory : nativeCategories) {
		if (!rCategory.is_object() || !rCategory.contains("name") || !rCategory["name"].is_string())
			continue;
		const std::string strName(rCategory["name"].get<std::string>());
		if (strName.empty())
			continue;
		categories[strName] = json{
			{"name", strName},
			{"savePath", rCategory.contains("path") && !rCategory["path"].is_null() && rCategory["path"].is_string() ? rCategory["path"].get<std::string>() : std::string()}
		};
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
	return ExecuteBridgeCommand(BuildCommand("categories/create", json{{"name", rCategory}}), ignored, rErrorMessage);
}

json BuildQBitTorrentJson(const json &rTransfer)
{
	const std::string strHash = rTransfer.value("hash", std::string());
	const std::string strName = rTransfer.value("name", std::string());
	const uint64_t ullSize = rTransfer.value("sizeBytes", 0ui64);
	const uint64_t ullCompleted = rTransfer.value("completedBytes", 0ui64);
	const double dProgress = ullSize > 0 ? static_cast<double>(ullCompleted) / static_cast<double>(ullSize) : 0.0;
	const std::string strState = rTransfer.value("state", std::string()) == "paused" || rTransfer.value("stopped", false) ? "pausedDL" : "downloading";
	const std::string strCategory = rTransfer.value("categoryName", std::string());
	return json{
		{"hash", strHash},
		{"name", strName},
		{"size", ullSize},
		{"progress", dProgress},
		{"eta", rTransfer.contains("eta") && !rTransfer["eta"].is_null() ? rTransfer["eta"] : json(-1)},
		{"state", strState},
		{"label", strCategory},
		{"category", strCategory},
		{"save_path", ""},
		{"content_path", strName},
		{"ratio", 0},
		{"ratio_limit", 0},
		{"seeding_time", 0},
		{"seeding_time_limit", 0},
		{"inactive_seeding_time_limit", 0},
		{"last_activity", 0}
	};
}

json BuildQBitTorrentsJson(const std::string &rCategory)
{
	json transfers;
	CString strErrorMessage;
	if (!ExecuteBridgeCommand(BuildCommand("transfers/list", json::object()), transfers, strErrorMessage) || !transfers.is_array())
		return json::array();

	json torrents = json::array();
	for (const json &rTransfer : transfers) {
		if (!rTransfer.is_object())
			continue;
		if (!rCategory.empty() && rTransfer.value("categoryName", std::string()) != rCategory)
			continue;
		torrents.push_back(BuildQBitTorrentJson(rTransfer));
	}
	return torrents;
}

bool TryGetHashQueryParam(const std::string &rRequestTarget, std::string &rHash)
{
	std::map<std::string, std::string> query;
	std::string strError;
	if (!WebServerJsonSeams::TryParseQueryString(rRequestTarget, query, strError))
		return false;
	const auto it = query.find("hash");
	if (it == query.end())
		return false;
	rHash = WebServerJsonSeams::ToLowerAscii(it->second);
	return WebServerJsonSeams::IsLowercaseMd4HexString(rHash);
}

bool TryGetCategoryQueryParam(const std::string &rRequestTarget, std::string &rCategory)
{
	std::map<std::string, std::string> query;
	std::string strError;
	if (!WebServerJsonSeams::TryParseQueryString(rRequestTarget, query, strError))
		return false;
	const auto it = query.find("category");
	if (it == query.end())
		return false;
	rCategory = it->second;
	return true;
}

void HandleLogin(const ThreadData &rData)
{
	if (thePrefs.GetWSApiKey().IsEmpty()) {
		SendTextResponse(rData.pSocket, 503, "Service Unavailable", "Fails.");
		return;
	}

	std::map<std::string, std::string> form;
	std::string strError;
	if (!WebServerQBitCompatSeams::TryParseFormBody(StdStringFromCStringA(rData.strRequestBody), form, strError)) {
		SendTextResponse(rData.pSocket, 400, "Bad Request", "Fails.");
		return;
	}

	const auto passwordIt = form.find("password");
	if (passwordIt == form.end() || CStringFromStdUtf8(passwordIt->second) != thePrefs.GetWSApiKey()) {
		SendTextResponse(rData.pSocket, 200, "OK", "Fails.");
		return;
	}

	CStringA strCookieHeader;
	strCookieHeader.Format("Set-Cookie: SID=%s; Path=/; HttpOnly\r\n", GetSessionId().c_str());
	SendTextResponse(rData.pSocket, 200, "OK", "Ok.", strCookieHeader);
}

void HandleTorrentAdd(const ThreadData &rData)
{
	WebServerQBitCompatSeams::SQBitTorrentAddRequest request;
	std::string strError;
	if (!WebServerQBitCompatSeams::TryParseTorrentAddRequest(StdStringFromCStringA(rData.strRequestBody), request, strError)) {
		SendTextResponse(rData.pSocket, 400, "Bad Request", "Fails.");
		return;
	}

	CString strErrorMessage;
	if (!EnsureCategoryExists(request.strCategory, strErrorMessage)) {
		SendTextResponse(rData.pSocket, 400, "Bad Request", "Fails.");
		return;
	}

	json params{
		{"link", request.strUrl},
		{"paused", request.bPaused}
	};
	if (!request.strCategory.empty())
		params["categoryName"] = request.strCategory;

	json result;
	if (!ExecuteBridgeCommand(BuildCommand("transfers/add", params), result, strErrorMessage)) {
		SendTextResponse(rData.pSocket, 400, "Bad Request", "Fails.");
		return;
	}

	SendTextResponse(rData.pSocket, 200, "OK", "Ok.");
}
}

bool WebServerQBitCompat::IsCompatRequest(const ThreadData &rData)
{
	return WebServerQBitCompatSeams::IsQBitRequestTarget(StdStringFromCStringA(rData.strRequestTarget));
}

void WebServerQBitCompat::ProcessRequest(const ThreadData &rData)
{
	if (rData.pSocket == NULL)
		return;

	const std::string strRequestTarget(StdStringFromCStringA(rData.strRequestTarget));
	const std::string strPath(WebServerJsonSeams::ToLowerAscii(WebServerJsonSeams::GetRequestPath(strRequestTarget)));
	const std::string strMethod(WebServerJsonSeams::ToLowerAscii(StdStringFromCStringA(rData.strMethod)));

	if (strPath == "/api/v2/app/webapiversion") {
		SendTextResponse(rData.pSocket, 200, "OK", "2.11.0");
		return;
	}

	if (strPath == "/api/v2/auth/login" && strMethod == "post") {
		HandleLogin(rData);
		return;
	}

	if (thePrefs.GetWSApiKey().IsEmpty()) {
		SendTextResponse(rData.pSocket, 503, "Service Unavailable", "eMule REST API key is not configured");
		return;
	}

	if (!HasValidSessionCookie(rData.strCookie)) {
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

	if (strPath == "/api/v2/torrents/createcategory" && strMethod == "post") {
		std::map<std::string, std::string> form;
		std::string strError;
		if (!WebServerQBitCompatSeams::TryParseFormBody(StdStringFromCStringA(rData.strRequestBody), form, strError) || form.find("category") == form.end()) {
			SendTextResponse(rData.pSocket, 400, "Bad Request", "Fails.");
			return;
		}
		CString strErrorMessage;
		if (!EnsureCategoryExists(form["category"], strErrorMessage)) {
			SendTextResponse(rData.pSocket, 400, "Bad Request", "Fails.");
			return;
		}
		SendTextResponse(rData.pSocket, 200, "OK", "Ok.");
		return;
	}

	if (strPath == "/api/v2/torrents/info") {
		std::string strCategory;
		(void)TryGetCategoryQueryParam(strRequestTarget, strCategory);
		SendJsonResponse(rData.pSocket, 200, "OK", BuildQBitTorrentsJson(strCategory));
		return;
	}

	if (strPath == "/api/v2/torrents/properties") {
		std::string strHash;
		if (!TryGetHashQueryParam(strRequestTarget, strHash)) {
			SendTextResponse(rData.pSocket, 400, "Bad Request", "hash is required");
			return;
		}
		json transfer;
		CString strErrorMessage;
		if (!ExecuteBridgeCommand(BuildCommand("transfers/get", json{{"hash", strHash}}), transfer, strErrorMessage) || !transfer.is_object()) {
			SendTextResponse(rData.pSocket, 404, "Not Found", "Not found");
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
		if (!TryGetHashQueryParam(strRequestTarget, strHash)) {
			SendTextResponse(rData.pSocket, 400, "Bad Request", "hash is required");
			return;
		}
		json transfer;
		CString strErrorMessage;
		if (!ExecuteBridgeCommand(BuildCommand("transfers/get", json{{"hash", strHash}}), transfer, strErrorMessage) || !transfer.is_object()) {
			SendTextResponse(rData.pSocket, 404, "Not Found", "Not found");
			return;
		}
		SendJsonResponse(rData.pSocket, 200, "OK", json::array({json{{"name", transfer.value("name", std::string())}}}));
		return;
	}

	if (strPath == "/api/v2/torrents/add" && strMethod == "post") {
		HandleTorrentAdd(rData);
		return;
	}

	if ((strPath == "/api/v2/torrents/setsharelimits" || strPath == "/api/v2/torrents/topprio" || strPath == "/api/v2/torrents/setforcestart") && strMethod == "post") {
		SendTextResponse(rData.pSocket, 200, "OK", "Ok.");
		return;
	}

	SendTextResponse(rData.pSocket, 404, "Not Found", "Not found");
}
