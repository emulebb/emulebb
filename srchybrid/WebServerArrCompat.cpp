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

#include <algorithm>
#include <ctime>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "Log.h"
#include "Preferences.h"
#include "WebServerArrCompatSeams.h"
#include "WebServerHttpResponse.h"
#include "WebServerJson.h"
#include "WebSocket.h"

using json = nlohmann::json;

namespace
{
constexpr ULONGLONG ARR_COMPAT_CACHE_TTL_MS = 10ULL * 60ULL * 1000ULL;
constexpr ULONGLONG ARR_COMPAT_BUSY_WAIT_MS = 15ULL * 1000ULL;
constexpr DWORD ARR_COMPAT_POLL_SLEEP_MS = 750;
constexpr DWORD ARR_COMPAT_BUSY_POLL_SLEEP_MS = 250;
constexpr size_t ARR_COMPAT_MAX_RETURNED_PAGE_ITEMS = 100U;
constexpr size_t ARR_COMPAT_MAX_CACHE_ENTRIES = 32U;

struct SArrCompatResult
{
	std::string strHash;
	std::string strName;
	std::string strDownloadLink;
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

struct SNativeSearchNetworkAvailability
{
	bool bGlobalConnected = false;
	bool bKadConnected = false;
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

	bool TryAcquire()
	{
		if (!m_bAcquired)
			m_bAcquired = ::InterlockedCompareExchange(&g_lArrCompatSearchInFlight, 1, 0) == 0;
		return m_bAcquired;
	}

private:
	bool m_bAcquired;
};

void SendXmlResponse(CWebSocket *pSocket, const int iStatusCode, LPCSTR pszReason, const std::string &rBody)
{
	WebServerHttpResponse::SendBody(pSocket, iStatusCode, pszReason, "OK", WebServerArrCompatSeams::kTorznabXmlContentTypeHeader, rBody.c_str(), rBody.size(), NULL, false);
}

std::string BuildCapsXml()
{
	return
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<caps>\n"
		"  <server app=\"eMuleBB *arr Compatibility\" version=\"1\" title=\"eMuleBB\" />\n"
		"  <limits max=\"100\" default=\"100\" />\n"
		"  <searching>\n"
		"    <search available=\"yes\" supportedParams=\"q,cat\" />\n"
		"    <tv-search available=\"yes\" supportedParams=\"q,cat,season,ep\" />\n"
		"    <movie-search available=\"yes\" supportedParams=\"q,cat,year\" />\n"
		"    <music-search available=\"yes\" supportedParams=\"q,cat\" />\n"
		"    <book-search available=\"yes\" supportedParams=\"q,cat\" />\n"
		"  </searching>\n"
		"  <categories>\n"
		"    <category id=\"2000\" name=\"Movies\" />\n"
		"    <category id=\"3000\" name=\"Audio\" />\n"
		"    <category id=\"4000\" name=\"PC\" />\n"
		"    <category id=\"5000\" name=\"TV\" />\n"
		"    <category id=\"6000\" name=\"XXX\" />\n"
		"    <category id=\"7000\" name=\"Books\" />\n"
		"    <category id=\"8000\" name=\"Other\" />\n"
		"  </categories>\n"
		"</caps>\n";
}

std::string BuildErrorXml(const int iStatusCode, const std::string &rDescription)
{
	std::ostringstream xml;
	xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		<< "<error code=\"" << iStatusCode << "\" description=\"" << WebServerArrCompatSeams::XmlEscape(rDescription) << "\" />\n";
	return xml.str();
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
	case WebServerArrCompatSeams::ETorznabFamily::Adult:
		return 6000;
	case WebServerArrCompatSeams::ETorznabFamily::Other:
		return 8000;
	case WebServerArrCompatSeams::ETorznabFamily::Any:
	case WebServerArrCompatSeams::ETorznabFamily::Unknown:
	default:
		return 8000;
	}
}

/// Orders Arr-facing releases by native eMule availability before Torznab paging.
bool IsMoreAvailableResult(const SArrCompatResult &rLeft, const SArrCompatResult &rRight)
{
	if (rLeft.ullSeeders != rRight.ullSeeders)
		return rLeft.ullSeeders > rRight.ullSeeders;
	if (rLeft.ullPeers != rRight.ullPeers)
		return rLeft.ullPeers > rRight.ullPeers;
	if (rLeft.ullSize != rRight.ullSize)
		return rLeft.ullSize < rRight.ullSize;
	return rLeft.strName < rRight.strName;
}

/// Sorts native search results so Arr sees the most available releases first.
void SortResultsByAvailability(std::vector<SArrCompatResult> &rResults)
{
	std::stable_sort(rResults.begin(), rResults.end(), IsMoreAvailableResult);
}

std::string BuildFeedXml(const WebServerArrCompatSeams::STorznabRequest &rRequest, const std::vector<SArrCompatResult> &rResults)
{
	std::ostringstream xml;
	const std::string strPubDate(FormatPubDate());
	const size_t uTotal = rResults.size();
	const size_t uStart = (std::min)(static_cast<size_t>(rRequest.uOffset), uTotal);
	const size_t uLimit = (std::min)(static_cast<size_t>(rRequest.uLimit), ARR_COMPAT_MAX_RETURNED_PAGE_ITEMS);
	const size_t uEnd = (std::min)(uTotal, uStart + uLimit);
	const size_t uAdvertisedTotal = uTotal;
	xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		<< "<rss version=\"2.0\" xmlns:torznab=\"http://torznab.com/schemas/2015/feed\">\n"
		<< "  <channel>\n"
		<< "    <title>eMuleBB *arr Compatibility</title>\n"
		<< "    <description>Native eMule search results exposed as Torznab</description>\n"
		<< "    <link>http://localhost/indexer/emulebb/api</link>\n"
		<< "    <language>en-us</language>\n"
		<< "    <torznab:response offset=\"" << rRequest.uOffset << "\" total=\"" << uAdvertisedTotal << "\" />\n";

	for (size_t uIndex = uStart; uIndex < uEnd; ++uIndex) {
		const SArrCompatResult &rResult = rResults[uIndex];
		const int iCategory = GetPrimaryTorznabCategory(rResult.eFamily == WebServerArrCompatSeams::ETorznabFamily::Any ? rRequest.eFamily : rResult.eFamily);
		xml << "    <item>\n"
			<< "      <title>" << WebServerArrCompatSeams::XmlEscape(rResult.strName) << "</title>\n"
			<< "      <guid isPermaLink=\"false\">ed2k:" << WebServerArrCompatSeams::XmlEscape(rResult.strHash) << "</guid>\n"
			<< "      <pubDate>" << strPubDate << "</pubDate>\n"
			<< "      <category>" << iCategory << "</category>\n"
			<< "      <comments>ed2k:" << WebServerArrCompatSeams::XmlEscape(rResult.strHash) << "</comments>\n"
			<< "      <description>" << WebServerArrCompatSeams::XmlEscape(rResult.strName) << "</description>\n"
			<< "      <link>" << WebServerArrCompatSeams::XmlEscape(rResult.strDownloadLink) << "</link>\n"
			<< "      <enclosure url=\"" << WebServerArrCompatSeams::XmlEscape(rResult.strDownloadLink) << "\" length=\"" << rResult.ullSize << "\" type=\"" << WebServerArrCompatSeams::kTorznabTorrentContentMimeType << "\" />\n"
			<< "      <torznab:attr name=\"size\" value=\"" << rResult.ullSize << "\" />\n"
			<< "      <torznab:attr name=\"magneturl\" value=\"" << WebServerArrCompatSeams::XmlEscape(rResult.strDownloadLink) << "\" />\n"
			<< "      <torznab:attr name=\"seeders\" value=\"" << rResult.ullSeeders << "\" />\n"
			<< "      <torznab:attr name=\"peers\" value=\"" << rResult.ullPeers << "\" />\n"
			<< "      <torznab:attr name=\"grabs\" value=\"" << rResult.ullGrabs << "\" />\n"
			<< "      <torznab:attr name=\"category\" value=\"" << iCategory << "\" />\n"
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

void PurgeExpiredCachedResults(const ULONGLONG ullNow)
{
	for (auto it = g_arrCompatCache.begin(); it != g_arrCompatCache.end();) {
		if (ullNow - it->second.ullTick > ARR_COMPAT_CACHE_TTL_MS)
			it = g_arrCompatCache.erase(it);
		else
			++it;
	}
}

void EnforceCacheEntryLimit()
{
	while (g_arrCompatCache.size() > ARR_COMPAT_MAX_CACHE_ENTRIES) {
		auto itOldest = g_arrCompatCache.begin();
		for (auto it = g_arrCompatCache.begin(); it != g_arrCompatCache.end(); ++it) {
			if (it->second.ullTick < itOldest->second.ullTick)
				itOldest = it;
		}
		g_arrCompatCache.erase(itOldest);
	}
}

void StoreCachedResults(const std::string &rCacheKey, const std::vector<SArrCompatResult> &rResults)
{
	CSingleLock lock(&g_arrCompatCacheLock, TRUE);
	const ULONGLONG ullNow = ::GetTickCount64();
	// WHY: Arr controllers can issue many distinct query keys during long
	// sessions. Key-local expiry on lookup does not reclaim cold stale vectors,
	// so stores are the central mutation point that can bound memory regardless
	// of which searches are repeated.
	PurgeExpiredCachedResults(ullNow);
	if (!WebServerArrCompatSeams::ShouldCacheTorznabResults(rResults.size())) {
		g_arrCompatCache.erase(rCacheKey);
		return;
	}
	SArrCompatCacheEntry &rEntry = g_arrCompatCache[rCacheKey];
	rEntry.ullTick = ullNow;
	rEntry.results = rResults;
	EnforceCacheEntryLimit();
}

bool ExecuteBridgeCommand(const json &rCommand, json &rResult)
{
	CStringA strErrorCode;
	CString strErrorMessage;
	if (WebServerJson::ExecuteInternalCommand(rCommand, rResult, strErrorCode, strErrorMessage))
		return true;
	if (!strErrorCode.IsEmpty())
		AddLogLine(false, _T("eMuleBB *arr Compatibility: native command failed (%hs)"), (LPCSTR)strErrorCode);
	return false;
}

uint64_t JsonUInt64Value(const json &rObject, const char *pszName)
{
	if (!rObject.contains(pszName))
		return 0;
	uint64_t ullValue = 0;
	return WebServerJsonSeams::TryParseJsonUInt64(rObject[pszName], ullValue, true) ? ullValue : 0;
}

bool JsonBoolValue(const json &rObject, const char *pszName)
{
	return rObject.contains(pszName) && rObject[pszName].is_boolean() && rObject[pszName].get<bool>();
}

SNativeSearchNetworkAvailability ReadNativeSearchNetworkAvailability()
{
	SNativeSearchNetworkAvailability availability;

	json statusResult;
	if (!ExecuteBridgeCommand(WebServerJson::BuildInternalCommand("status/get", json::object()), statusResult))
		return availability;

	if (statusResult.contains("servers") && statusResult["servers"].is_object())
		availability.bGlobalConnected = JsonBoolValue(statusResult["servers"], "connected");
	if (statusResult.contains("kad") && statusResult["kad"].is_object())
		availability.bKadConnected = JsonBoolValue(statusResult["kad"], "connected");
	return availability;
}

std::vector<std::string> BuildAvailableNativeSearchMethods(const WebServerArrCompatSeams::ETorznabFamily eFamily)
{
	const std::vector<std::string> candidateMethods(WebServerArrCompatSeams::BuildNativeSearchMethodNames(eFamily));
	bool bRequiresNetworkStatus = false;
	for (const std::string &rMethod : candidateMethods) {
		if (WebServerArrCompatSeams::IsConnectedNetworkSearchMethod(rMethod)) {
			bRequiresNetworkStatus = true;
			break;
		}
	}
	if (!bRequiresNetworkStatus)
		return candidateMethods;

	const SNativeSearchNetworkAvailability availability(ReadNativeSearchNetworkAvailability());
	return WebServerArrCompatSeams::BuildAvailableNativeSearchMethodNames(eFamily, availability.bGlobalConnected, availability.bKadConnected);
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
		item.strDownloadLink = WebServerArrCompatSeams::BuildEd2kMagnetDownloadLink(item.strHash, item.strName, item.ullSize);
		if (item.strDownloadLink.empty())
			continue;

		rSeenHashes.insert(strHash);
		rResults.push_back(item);
		if (rResults.size() >= 100)
			return;
	}
}

std::vector<SArrCompatResult> BuildValidationProbeResults(const WebServerArrCompatSeams::STorznabRequest &rRequest)
{
	std::vector<SArrCompatResult> results;
	SArrCompatResult item;
	item.strHash = "00000000000000000000000000000001";
	item.strName = "eMuleBB Arr indexer validation probe.txt";
	item.ullSize = 1;
	item.ullSeeders = 1;
	item.ullPeers = 1;
	item.ullGrabs = 0;
	item.eFamily = rRequest.eFamily;
	item.strDownloadLink = WebServerArrCompatSeams::BuildEd2kMagnetDownloadLink(item.strHash, item.strName, item.ullSize);
	if (!item.strDownloadLink.empty())
		results.push_back(item);
	return results;
}

void DeleteNativeSearch(const std::string &rSearchId)
{
	if (rSearchId.empty())
		return;
	json ignored;
	(void)ExecuteBridgeCommand(WebServerJson::BuildInternalCommand("search/delete", json{{"searchId", rSearchId}}), ignored);
}

std::vector<SArrCompatResult> RunOneNativeSearch(
	const std::string &rQuery,
	const WebServerArrCompatSeams::ETorznabFamily eFamily,
	const std::string &rMethod,
	const std::string &rSearchType,
	const ULONGLONG ullDeadline,
	std::set<std::string> &rSeenHashes)
{
	std::vector<SArrCompatResult> results;
	if (::GetTickCount64() >= ullDeadline)
		return results;

	json startResult;
	if (!ExecuteBridgeCommand(
			WebServerJson::BuildInternalCommand("search/start", json{
				{"query", rQuery},
				{"method", rMethod},
				{"type", rSearchType},
				{"clearExisting", false}
			}),
			startResult))
		return results;
	if (!startResult.contains("id") || !startResult["id"].is_string())
		return results;

	const std::string strSearchId(startResult["id"].get<std::string>());
	while (::GetTickCount64() < ullDeadline) {
		json pollResult;
		// Arr only needs compact rows for Torznab conversion and may poll while
		// a native search is still filling. Keep every poll bounded to the
		// remaining rows so broad searches do not rebuild fake-file evidence for
		// thousands of results that the compatibility layer will discard.
		const size_t uRemaining = 100 - results.size();
		if (!ExecuteBridgeCommand(WebServerJson::BuildInternalCommand("search/results", json{
				{"searchId", strSearchId},
				{"_offset", 0},
				{"_limit", static_cast<int>((std::min)(uRemaining, static_cast<size_t>(100)))},
				{"exactTotal", false},
				{"includeEvidence", false}
			}), pollResult))
			break;
		AppendResultsFromJson(pollResult, eFamily, results, rSeenHashes);
		if (pollResult.contains("status") && pollResult["status"].is_string() && pollResult["status"].get<std::string>() == "complete")
			break;
		if (results.size() >= 100)
			break;
		::Sleep(ARR_COMPAT_POLL_SLEEP_MS);
	}

	DeleteNativeSearch(strSearchId);
	return results;
}

std::vector<SArrCompatResult> RunNativeSearches(const WebServerArrCompatSeams::STorznabRequest &rRequest, const std::vector<std::string> &rMethods)
{
	std::vector<SArrCompatResult> results;
	if (rRequest.eFamily == WebServerArrCompatSeams::ETorznabFamily::Unknown || rMethods.empty())
		return results;

	std::set<std::string> seenHashes;
	const ULONGLONG ullDeadline = ::GetTickCount64() + static_cast<ULONGLONG>(WebServerArrCompatSeams::GetNativeSearchTimeoutMilliseconds(rRequest.eFamily));
	const std::vector<std::string> searchTypes(WebServerArrCompatSeams::BuildRestSearchTypeNames(rRequest.eFamily));
	for (const std::string &rQuery : WebServerArrCompatSeams::BuildNativeQueries(rRequest)) {
		for (const std::string &rSearchType : searchTypes) {
			for (size_t uMethodIndex = 0; uMethodIndex < rMethods.size(); ++uMethodIndex) {
				const ULONGLONG ullNow = ::GetTickCount64();
				if (ullNow >= ullDeadline)
					break;
				const size_t uRemainingMethods = rMethods.size() - uMethodIndex;
				const ULONGLONG ullMethodProbeTimeout = static_cast<ULONGLONG>(WebServerArrCompatSeams::GetNativeSearchMethodProbeTimeoutMilliseconds(rRequest.eFamily, uRemainingMethods));
				const ULONGLONG ullProbeDeadline = (std::min)(ullDeadline, ullNow + ullMethodProbeTimeout);
				const std::string &rMethod = rMethods[uMethodIndex];
				const std::vector<SArrCompatResult> queryResults(RunOneNativeSearch(rQuery, rRequest.eFamily, rMethod, rSearchType, ullProbeDeadline, seenHashes));
				results.insert(results.end(), queryResults.begin(), queryResults.end());
				if (results.size() >= 100)
					break;
			}
			if (results.size() >= 100 || ::GetTickCount64() >= ullDeadline)
				break;
			if (!results.empty())
				break;
		}
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

	// Compare the presented key against the configured REST API key in constant
	// time to avoid leaking it through response timing.
	const std::string strConfiguredKey(WebServerJson::ToStdUtf8(thePrefs.GetWSApiKey()));

	const auto apiKeyIt = rNormalizedQuery.find("apikey");
	if (apiKeyIt != rNormalizedQuery.end())
		return WebServerJsonSeams::ConstantTimeSecretEquals(strConfiguredKey, apiKeyIt->second);

	return !rData.strApiKey.IsEmpty()
		&& WebServerJsonSeams::ConstantTimeSecretEquals(strConfiguredKey, WebServerJson::ToStdString(rData.strApiKey));
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
		SendXmlResponse(rData.pSocket, WebServerArrCompatSeams::kTorznabParseErrorHttpStatus, "Bad Request", BuildErrorXml(WebServerArrCompatSeams::kTorznabParseErrorHttpStatus, strError.empty() ? "malformed Torznab request path" : strError));
		return;
	}
	if (strPath != "/indexer/emulebb/api" && strPath != "/indexer/emulebb/api/") {
		SendXmlResponse(rData.pSocket, 404, "Not Found", BuildErrorXml(404, "Torznab API route not found"));
		return;
	}
	if (WebServerJson::ToStdString(rData.strMethod) != "GET") {
		SendXmlResponse(rData.pSocket, 404, "Not Found", BuildErrorXml(404, "Torznab API route not found"));
		return;
	}

	if (thePrefs.GetWSApiKey().IsEmpty()) {
		SendXmlResponse(rData.pSocket, 503, "Service Unavailable", BuildErrorXml(503, "native REST API key is not configured"));
		return;
	}

	std::map<std::string, std::string> normalizedQuery;
	if (!WebServerArrCompatSeams::TryParseTorznabQueryParameters(strRequestTarget, normalizedQuery, strError)) {
		SendXmlResponse(rData.pSocket, WebServerArrCompatSeams::kTorznabParseErrorHttpStatus, "Bad Request", BuildErrorXml(WebServerArrCompatSeams::kTorznabParseErrorHttpStatus, strError.empty() ? "malformed Torznab query" : strError));
		return;
	}
	if (!HasValidTorznabApiKey(rData, normalizedQuery)) {
		SendXmlResponse(rData.pSocket, 401, "Unauthorized", BuildErrorXml(401, "missing or invalid API key"));
		return;
	}

	WebServerArrCompatSeams::STorznabRequest request;
	if (!WebServerArrCompatSeams::TryParseTorznabRequest(strRequestTarget, request, strError)) {
		SendXmlResponse(rData.pSocket, WebServerArrCompatSeams::kTorznabParseErrorHttpStatus, "Bad Request", BuildErrorXml(WebServerArrCompatSeams::kTorznabParseErrorHttpStatus, strError.empty() ? "invalid Torznab request" : strError));
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
	if (WebServerArrCompatSeams::IsArrIndexerValidationProbe(request)) {
		// WHY: Radarr/Sonarr refuse to persist an otherwise valid Torznab
		// indexer when their category-only validation probe returns zero rows.
		// Real searches still require a title query and run through native eMule.
		SendXmlResponse(rData.pSocket, 200, "OK", BuildFeedXml(request, BuildValidationProbeResults(request)));
		return;
	}

	const std::vector<std::string> nativeSearchMethods(BuildAvailableNativeSearchMethods(request.eFamily));
	const std::string strCacheKey(WebServerArrCompatSeams::BuildCacheKey(request, nativeSearchMethods));
	if (TryGetCachedResults(strCacheKey, results)) {
		SendXmlResponse(rData.pSocket, 200, "OK", BuildFeedXml(request, results));
		return;
	}
	if (request.uOffset > 0) {
		SendXmlResponse(rData.pSocket, 200, "OK", BuildFeedXml(request, results));
		return;
	}

	CArrCompatSearchReservation searchReservation;
	if (!searchReservation.IsAcquired()) {
		const ULONGLONG ullBusyDeadline = ::GetTickCount64() + ARR_COMPAT_BUSY_WAIT_MS;
		while (!searchReservation.TryAcquire() && ::GetTickCount64() < ullBusyDeadline)
			::Sleep(ARR_COMPAT_BUSY_POLL_SLEEP_MS);
		if (!searchReservation.IsAcquired()) {
			if (TryGetCachedResults(strCacheKey, results))
				SendXmlResponse(rData.pSocket, 200, "OK", BuildFeedXml(request, results));
			else
				SendXmlResponse(rData.pSocket, WebServerArrCompatSeams::kTorznabBusyHttpStatus, "Service Unavailable", BuildErrorXml(WebServerArrCompatSeams::kTorznabBusyHttpStatus, "native search bridge is busy"));
			return;
		}
	}
	if (TryGetCachedResults(strCacheKey, results)) {
		SendXmlResponse(rData.pSocket, 200, "OK", BuildFeedXml(request, results));
		return;
	}

	results = RunNativeSearches(request, nativeSearchMethods);
	SortResultsByAvailability(results);
	StoreCachedResults(strCacheKey, results);
	SendXmlResponse(rData.pSocket, 200, "OK", BuildFeedXml(request, results));
}
