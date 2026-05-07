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
#include "WebServerArrCompat.h"

#include <ctime>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "Log.h"
#include "Preferences.h"
#include "WebApiCommandSeams.h"
#include "WebServerArrCompatSeams.h"
#include "WebServerJson.h"
#include "WebSocket.h"

using json = nlohmann::json;

namespace
{
constexpr ULONGLONG ARR_COMPAT_CACHE_TTL_MS = 10ULL * 60ULL * 1000ULL;
constexpr ULONGLONG ARR_COMPAT_SEARCH_TIMEOUT_MS = 12ULL * 1000ULL;
constexpr DWORD ARR_COMPAT_POLL_SLEEP_MS = 750;

struct SArrCompatResult
{
	std::string strHash;
	std::string strName;
	std::string strMagnet;
	uint64_t ullSize = 0;
	uint64_t ullSeeders = 0;
	uint64_t ullPeers = 0;
	uint64_t ullGrabs = 0;
	WebServerArrCompatSeams::ETorznabFamily eFamily = WebServerArrCompatSeams::ETorznabFamily::Any;
};

struct SArrCompatCacheEntry
{
	ULONGLONG ullTick = 0;
	std::vector<SArrCompatResult> results;
};

CCriticalSection g_arrCompatCacheLock;
std::map<std::string, SArrCompatCacheEntry> g_arrCompatCache;
volatile LONG g_lArrCompatSearchInFlight = 0;

class CArrCompatSearchReservation
{
public:
	CArrCompatSearchReservation()
		: m_bAcquired(::InterlockedCompareExchange(&g_lArrCompatSearchInFlight, 1, 0) == 0)
	{
	}

	~CArrCompatSearchReservation()
	{
		if (m_bAcquired)
			::InterlockedExchange(&g_lArrCompatSearchInFlight, 0);
	}

	bool IsAcquired() const
	{
		return m_bAcquired;
	}

private:
	bool m_bAcquired;
};

void SendXmlResponse(CWebSocket *pSocket, const int iStatusCode, LPCSTR pszReason, const std::string &rBody)
{
	if (pSocket == NULL)
		return;

	CStringA strHeader;
	strHeader.Format(
		"HTTP/1.1 %d %s\r\n"
		"%s"
		"Cache-Control: no-store\r\n"
		"Content-Length: %u\r\n\r\n",
		iStatusCode,
		pszReason != NULL ? pszReason : "OK",
		WebServerArrCompatSeams::kTorznabXmlContentTypeHeader,
		static_cast<UINT>(rBody.size()));
	pSocket->SendData(strHeader, strHeader.GetLength());
	if (!rBody.empty())
		pSocket->SendData(rBody.c_str(), static_cast<int>(rBody.size()));
}

std::string BuildCapsXml()
{
	return
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<caps>\n"
		"  <server app=\"eMule BB *arr Compatibility\" version=\"1\" title=\"eMule BB\" />\n"
		"  <limits max=\"100\" default=\"100\" />\n"
		"  <searching>\n"
		"    <search available=\"yes\" supportedParams=\"q,cat\" />\n"
		"    <tv-search available=\"yes\" supportedParams=\"q,cat,season,ep\" />\n"
		"    <movie-search available=\"yes\" supportedParams=\"q,cat,year\" />\n"
		"  </searching>\n"
		"  <categories>\n"
		"    <category id=\"2000\" name=\"Movies\" />\n"
		"    <category id=\"3000\" name=\"Audio\" />\n"
		"    <category id=\"4000\" name=\"PC\" />\n"
		"    <category id=\"5000\" name=\"TV\" />\n"
		"    <category id=\"7000\" name=\"Books\" />\n"
		"    <category id=\"8000\" name=\"Other\" />\n"
		"  </categories>\n"
		"</caps>\n";
}

std::string FormatPubDate()
{
	std::time_t now = std::time(NULL);
	std::tm utc = {};
	if (gmtime_s(&utc, &now) != 0)
		return "Thu, 01 Jan 1970 00:00:00 GMT";
	char buffer[64] = {};
	if (std::strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", &utc) == 0)
		return "Thu, 01 Jan 1970 00:00:00 GMT";
	return buffer;
}

int GetPrimaryTorznabCategory(const WebServerArrCompatSeams::ETorznabFamily eFamily)
{
	switch (eFamily) {
	case WebServerArrCompatSeams::ETorznabFamily::Movie:
		return 2000;
	case WebServerArrCompatSeams::ETorznabFamily::Tv:
		return 5000;
	case WebServerArrCompatSeams::ETorznabFamily::Audio:
		return 3000;
	case WebServerArrCompatSeams::ETorznabFamily::Book:
		return 7000;
	case WebServerArrCompatSeams::ETorznabFamily::Other:
		return 8000;
	case WebServerArrCompatSeams::ETorznabFamily::Any:
	case WebServerArrCompatSeams::ETorznabFamily::Unknown:
	default:
		return 8000;
	}
}

std::string BuildFeedXml(const WebServerArrCompatSeams::STorznabRequest &rRequest, const std::vector<SArrCompatResult> &rResults)
{
	std::ostringstream xml;
	const std::string strPubDate(FormatPubDate());
	xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		<< "<rss version=\"2.0\" xmlns:torznab=\"http://torznab.com/schemas/2015/feed\">\n"
		<< "  <channel>\n"
		<< "    <title>eMule BB *arr Compatibility</title>\n"
		<< "    <description>Native eMule search results exposed as Torznab</description>\n"
		<< "    <link>http://localhost/indexer/emulebb/api</link>\n"
		<< "    <language>en-us</language>\n"
		<< "    <torznab:response offset=\"0\" total=\"" << rResults.size() << "\" />\n";

	for (const SArrCompatResult &rResult : rResults) {
		const int iCategory = GetPrimaryTorznabCategory(rResult.eFamily == WebServerArrCompatSeams::ETorznabFamily::Any ? rRequest.eFamily : rResult.eFamily);
		xml << "    <item>\n"
			<< "      <title>" << WebServerArrCompatSeams::XmlEscape(rResult.strName) << "</title>\n"
			<< "      <guid isPermaLink=\"false\">ed2k:" << WebServerArrCompatSeams::XmlEscape(rResult.strHash) << "</guid>\n"
			<< "      <pubDate>" << strPubDate << "</pubDate>\n"
			<< "      <category>" << iCategory << "</category>\n"
			<< "      <comments>ed2k:" << WebServerArrCompatSeams::XmlEscape(rResult.strHash) << "</comments>\n"
			<< "      <description>" << WebServerArrCompatSeams::XmlEscape(rResult.strName) << "</description>\n"
			<< "      <link>" << WebServerArrCompatSeams::XmlEscape(rResult.strMagnet) << "</link>\n"
			<< "      <enclosure url=\"" << WebServerArrCompatSeams::XmlEscape(rResult.strMagnet) << "\" length=\"" << rResult.ullSize << "\" type=\"application/x-bittorrent;x-scheme-handler/magnet\" />\n"
			<< "      <torznab:attr name=\"size\" value=\"" << rResult.ullSize << "\" />\n"
			<< "      <torznab:attr name=\"seeders\" value=\"" << rResult.ullSeeders << "\" />\n"
			<< "      <torznab:attr name=\"peers\" value=\"" << rResult.ullPeers << "\" />\n"
			<< "      <torznab:attr name=\"grabs\" value=\"" << rResult.ullGrabs << "\" />\n"
			<< "      <torznab:attr name=\"category\" value=\"" << iCategory << "\" />\n"
			<< "      <torznab:attr name=\"magneturl\" value=\"" << WebServerArrCompatSeams::XmlEscape(rResult.strMagnet) << "\" />\n"
			<< "      <torznab:attr name=\"infohash\" value=\"" << WebServerArrCompatSeams::XmlEscape(WebServerArrCompatSeams::BuildFakeBtihHash(rResult.strHash)) << "\" />\n"
			<< "      <torznab:attr name=\"minimumratio\" value=\"0\" />\n"
			<< "      <torznab:attr name=\"minimumseedtime\" value=\"0\" />\n"
			<< "      <torznab:attr name=\"downloadvolumefactor\" value=\"0\" />\n"
			<< "      <torznab:attr name=\"uploadvolumefactor\" value=\"1\" />\n"
			<< "    </item>\n";
	}

	xml << "  </channel>\n</rss>\n";
	return xml.str();
}

bool TryGetCachedResults(const std::string &rCacheKey, std::vector<SArrCompatResult> &rResults)
{
	const ULONGLONG ullNow = ::GetTickCount64();
	CSingleLock lock(&g_arrCompatCacheLock, TRUE);
	const auto it = g_arrCompatCache.find(rCacheKey);
	if (it == g_arrCompatCache.end())
		return false;
	if (ullNow - it->second.ullTick > ARR_COMPAT_CACHE_TTL_MS) {
		g_arrCompatCache.erase(it);
		return false;
	}
	rResults = it->second.results;
	return true;
}

void StoreCachedResults(const std::string &rCacheKey, const std::vector<SArrCompatResult> &rResults)
{
	CSingleLock lock(&g_arrCompatCacheLock, TRUE);
	SArrCompatCacheEntry &rEntry = g_arrCompatCache[rCacheKey];
	rEntry.ullTick = ::GetTickCount64();
	rEntry.results = rResults;
}

json BuildCommand(const char *pszCommand, const json &rParams)
{
	return json{{"cmd", pszCommand}, {"params", rParams}};
}

bool ExecuteBridgeCommand(const json &rCommand, json &rResult)
{
	CStringA strErrorCode;
	CString strErrorMessage;
	if (WebServerJson::ExecuteInternalCommand(rCommand, rResult, strErrorCode, strErrorMessage))
		return true;
	if (!strErrorCode.IsEmpty())
		AddLogLine(false, _T("eMule BB *arr Compatibility: native command failed (%hs)"), (LPCSTR)strErrorCode);
	return false;
}

uint64_t JsonUInt64Value(const json &rObject, const char *pszName)
{
	if (!rObject.contains(pszName))
		return 0;
	uint64_t ullValue = 0;
	return WebServerJsonSeams::TryParseJsonUInt64(rObject[pszName], ullValue, true) ? ullValue : 0;
}

void AppendResultsFromJson(const json &rResultPayload, const WebServerArrCompatSeams::ETorznabFamily eFamily, std::vector<SArrCompatResult> &rResults, std::set<std::string> &rSeenHashes)
{
	if (!rResultPayload.contains("results") || !rResultPayload["results"].is_array())
		return;

	for (const json &rResult : rResultPayload["results"]) {
		if (!rResult.is_object() || !rResult.contains("hash") || !rResult.contains("name") || !rResult["hash"].is_string() || !rResult["name"].is_string())
			continue;
		const std::string strHash(WebServerJsonSeams::ToLowerAscii(rResult["hash"].get<std::string>()));
		const std::string strName(rResult["name"].get<std::string>());
		const uint64_t ullSize = JsonUInt64Value(rResult, "sizeBytes");
		if (strHash.size() != 32 || strName.empty() || !WebServerArrCompatSeams::DoesResultMatchFamily(eFamily, strName, ullSize))
			continue;
		if (rSeenHashes.find(strHash) != rSeenHashes.end())
			continue;

		SArrCompatResult item;
		item.strHash = strHash;
		item.strName = strName;
		item.ullSize = ullSize;
		item.ullSeeders = JsonUInt64Value(rResult, "completeSources");
		item.ullPeers = JsonUInt64Value(rResult, "sources");
		item.ullGrabs = item.ullSeeders;
		item.eFamily = eFamily;
		item.strMagnet = WebServerArrCompatSeams::BuildMagnetFromEd2k(item.strHash, item.strName, item.ullSize);
		if (item.strMagnet.empty())
			continue;

		rSeenHashes.insert(strHash);
		rResults.push_back(item);
		if (rResults.size() >= 100)
			return;
	}
}

void StopNativeSearch(const std::string &rSearchId)
{
	if (rSearchId.empty())
		return;
	json ignored;
	(void)ExecuteBridgeCommand(BuildCommand("search/stop", json{{"searchId", rSearchId}}), ignored);
}

std::vector<SArrCompatResult> RunOneNativeSearch(const std::string &rQuery, const WebServerArrCompatSeams::ETorznabFamily eFamily, const ULONGLONG ullDeadline, std::set<std::string> &rSeenHashes)
{
	std::vector<SArrCompatResult> results;
	if (::GetTickCount64() >= ullDeadline)
		return results;

	static const char *const s_arrSearchMethods[] = {WebApiCommandSeams::GetDefaultSearchMethodName()};

	json startResult;
	for (const char *const pszMethod : s_arrSearchMethods) {
		if (ExecuteBridgeCommand(
				BuildCommand("search/start", json{
					{"query", rQuery},
					{"method", pszMethod},
					{"type", WebServerArrCompatSeams::GetNativeSearchType(eFamily)},
					{"clearExisting", false}
				}),
				startResult))
		{
			break;
		}
		if (::GetTickCount64() >= ullDeadline)
			return results;
	}
	if (!startResult.contains("id") || !startResult["id"].is_string())
		return results;

	const std::string strSearchId(startResult["id"].get<std::string>());
	while (::GetTickCount64() < ullDeadline) {
		json pollResult;
		if (!ExecuteBridgeCommand(BuildCommand("search/results", json{{"searchId", strSearchId}}), pollResult))
			break;
		AppendResultsFromJson(pollResult, eFamily, results, rSeenHashes);
		if (pollResult.contains("status") && pollResult["status"].is_string() && pollResult["status"].get<std::string>() == "complete")
			break;
		if (results.size() >= 100)
			break;
		::Sleep(ARR_COMPAT_POLL_SLEEP_MS);
	}

	StopNativeSearch(strSearchId);
	return results;
}

std::vector<SArrCompatResult> RunNativeSearches(const WebServerArrCompatSeams::STorznabRequest &rRequest)
{
	std::vector<SArrCompatResult> results;
	if (rRequest.eFamily == WebServerArrCompatSeams::ETorznabFamily::Unknown)
		return results;

	std::set<std::string> seenHashes;
	const ULONGLONG ullDeadline = ::GetTickCount64() + ARR_COMPAT_SEARCH_TIMEOUT_MS;
	for (const std::string &rQuery : WebServerArrCompatSeams::BuildNativeQueries(rRequest)) {
		const std::vector<SArrCompatResult> queryResults(RunOneNativeSearch(rQuery, rRequest.eFamily, ullDeadline, seenHashes));
		results.insert(results.end(), queryResults.begin(), queryResults.end());
		if (results.size() >= 100 || ::GetTickCount64() >= ullDeadline)
			break;
	}
	if (results.size() > 100)
		results.resize(100);
	return results;
}

bool HasValidTorznabApiKey(
	const ThreadData &rData,
	const std::map<std::string, std::string> &rNormalizedQuery)
{
	if (thePrefs.GetWSApiKey().IsEmpty())
		return false;

	const auto apiKeyIt = rNormalizedQuery.find("apikey");
	if (apiKeyIt != rNormalizedQuery.end())
		return apiKeyIt->second == WebServerJson::ToStdUtf8(thePrefs.GetWSApiKey());

	return !rData.strApiKey.IsEmpty() && WebServerJson::FromStdUtf8(WebServerJson::ToStdString(rData.strApiKey)) == thePrefs.GetWSApiKey();
}
}

bool WebServerArrCompat::IsCompatRequest(const ThreadData &rData)
{
	return WebServerArrCompatSeams::IsArrCompatRequestTarget(WebServerJson::ToStdString(rData.strRequestTarget));
}

void WebServerArrCompat::ProcessRequest(const ThreadData &rData)
{
	if (rData.pSocket == NULL)
		return;

	const std::string strRequestTarget(WebServerJson::ToStdString(rData.strRequestTarget));
	std::string strPath;
	std::string strError;
	if (!WebServerArrCompatSeams::TryGetArrCompatRequestPathLower(strRequestTarget, strPath, strError)) {
		SendXmlResponse(rData.pSocket, WebServerArrCompatSeams::kTorznabParseErrorHttpStatus, "Bad Request", BuildFeedXml(WebServerArrCompatSeams::STorznabRequest(), std::vector<SArrCompatResult>()));
		return;
	}
	if (strPath != "/indexer/emulebb/api" && strPath != "/indexer/emulebb/api/") {
		SendXmlResponse(rData.pSocket, 404, "Not Found", BuildFeedXml(WebServerArrCompatSeams::STorznabRequest(), std::vector<SArrCompatResult>()));
		return;
	}
	if (WebServerJson::ToStdString(rData.strMethod) != "GET") {
		SendXmlResponse(rData.pSocket, 404, "Not Found", BuildFeedXml(WebServerArrCompatSeams::STorznabRequest(), std::vector<SArrCompatResult>()));
		return;
	}

	if (thePrefs.GetWSApiKey().IsEmpty()) {
		SendXmlResponse(rData.pSocket, 503, "Service Unavailable", BuildFeedXml(WebServerArrCompatSeams::STorznabRequest(), std::vector<SArrCompatResult>()));
		return;
	}

	std::map<std::string, std::string> normalizedQuery;
	if (!WebServerArrCompatSeams::TryParseTorznabQueryParameters(strRequestTarget, normalizedQuery, strError)) {
		SendXmlResponse(rData.pSocket, WebServerArrCompatSeams::kTorznabParseErrorHttpStatus, "Bad Request", BuildFeedXml(WebServerArrCompatSeams::STorznabRequest(), std::vector<SArrCompatResult>()));
		return;
	}
	if (!HasValidTorznabApiKey(rData, normalizedQuery)) {
		SendXmlResponse(rData.pSocket, 401, "Unauthorized", BuildFeedXml(WebServerArrCompatSeams::STorznabRequest(), std::vector<SArrCompatResult>()));
		return;
	}

	WebServerArrCompatSeams::STorznabRequest request;
	if (!WebServerArrCompatSeams::TryParseTorznabRequest(strRequestTarget, request, strError)) {
		SendXmlResponse(rData.pSocket, WebServerArrCompatSeams::kTorznabParseErrorHttpStatus, "Bad Request", BuildFeedXml(request, std::vector<SArrCompatResult>()));
		return;
	}

	if (request.strType == "caps") {
		SendXmlResponse(rData.pSocket, 200, "OK", BuildCapsXml());
		return;
	}

	std::vector<SArrCompatResult> results;
	if (request.eFamily == WebServerArrCompatSeams::ETorznabFamily::Unknown) {
		SendXmlResponse(rData.pSocket, 200, "OK", BuildFeedXml(request, results));
		return;
	}

	const std::string strCacheKey(WebServerArrCompatSeams::BuildCacheKey(request));
	if (TryGetCachedResults(strCacheKey, results)) {
		SendXmlResponse(rData.pSocket, 200, "OK", BuildFeedXml(request, results));
		return;
	}

	CArrCompatSearchReservation searchReservation;
	if (!searchReservation.IsAcquired()) {
		SendXmlResponse(rData.pSocket, 200, "OK", BuildFeedXml(request, results));
		return;
	}
	if (TryGetCachedResults(strCacheKey, results)) {
		SendXmlResponse(rData.pSocket, 200, "OK", BuildFeedXml(request, results));
		return;
	}

	results = RunNativeSearches(request);
	StoreCachedResults(strCacheKey, results);
	SendXmlResponse(rData.pSocket, 200, "OK", BuildFeedXml(request, results));
}
