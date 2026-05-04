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
#include <map>
#include <memory>
#include <share.h>
#include <string>
#include <time.h>
#include <vector>
#include "GeoLocation.h"
#include "DirectDownload.h"
#include "emule.h"
#include "Preferences.h"
#include "UserMsgs.h"
#include "GZipFile.h"
#include "emuledlg.h"
#include "TransferDlg.h"
#include "ServerWnd.h"
#include "KademliaWnd.h"
#include "Log.h"
#include "LongPathSeams.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	const uint32 MAX_MMDB_FILE_SIZE = 256u * 1024u * 1024u;
	const BYTE s_aucMetadataMarker[] = {0xAB, 0xCD, 0xEF, 'M', 'a', 'x', 'M', 'i', 'n', 'd', '.', 'c', 'o', 'm'};
	const BYTE s_aucDataSectionSeparator[16] = {0};
	const LPCTSTR DFLT_GEOLOCATION_DB_FILENAME = _T("dbip-city-lite.mmdb");
	const LPCTSTR DFLT_GEOLOCATION_DB_URL_TEMPLATE = _T("https://download.db-ip.com/free/dbip-city-lite-%Y-%m.mmdb.gz");
	const int FLAG_ICON_WIDTH = 18;
	const int FLAG_ICON_HEIGHT = 16;

	enum MMDBDataType
	{
		MMDB_TYPE_UNKNOWN = 0,
		MMDB_TYPE_POINTER = 1,
		MMDB_TYPE_STRING = 2,
		MMDB_TYPE_DOUBLE = 3,
		MMDB_TYPE_BYTES = 4,
		MMDB_TYPE_UINT16 = 5,
		MMDB_TYPE_UINT32 = 6,
		MMDB_TYPE_MAP = 7,
		MMDB_TYPE_INT32 = 8,
		MMDB_TYPE_UINT64 = 9,
		MMDB_TYPE_UINT128 = 10,
		MMDB_TYPE_ARRAY = 11,
		MMDB_TYPE_CACHE_CONTAINER = 12,
		MMDB_TYPE_END_MARKER = 13,
		MMDB_TYPE_BOOLEAN = 14,
		MMDB_TYPE_FLOAT = 15
	};

	struct MMDBFieldDescriptor
	{
		MMDBDataType eType;
		uint32 dwFieldSize;
		uint32 dwOffset;

		MMDBFieldDescriptor()
			: eType(MMDB_TYPE_UNKNOWN)
			, dwFieldSize(0)
			, dwOffset(0)
		{
		}
	};

	struct MMDBValue
	{
		MMDBDataType eType;
		std::string strValue;
		ULONGLONG ullValue;
		bool bValue;
		std::map<std::string, MMDBValue> mapValue;
		std::vector<MMDBValue> vecValue;

		MMDBValue()
			: eType(MMDB_TYPE_UNKNOWN)
			, ullValue(0)
			, bValue(false)
		{
		}
	};

	/**
	 * @brief One decoded MMDB node payload cached by node id.
	 */
	struct SNodeGeoLocation
	{
		CString strCountryCode;
		CString strCountryName;
		CString strCityName;
	};

	/**
	 * @brief Converts UTF-8 strings from the MMDB payload into UI-safe CString values.
	 */
	CString Utf8StringToCString(const std::string& strValue)
	{
		if (strValue.empty())
			return CString();
#ifdef _UNICODE
		if (strValue.size() > static_cast<size_t>(INT_MAX))
			return CString();

		const int iWideChars = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, strValue.data(), static_cast<int>(strValue.size()), NULL, 0);
		if (iWideChars <= 0)
			return CString();

		CStringW strWide;
		LPWSTR pszWide = strWide.GetBuffer(iWideChars);
		const int iConverted = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, strValue.data(), static_cast<int>(strValue.size()), pszWide, iWideChars);
		strWide.ReleaseBuffer(iConverted > 0 ? iConverted : 0);
		return CString(strWide);
#else
		return CString(strValue.c_str());
#endif
	}

	CString FormatBuildDate(__time64_t tBuildEpoch)
	{
		if (tBuildEpoch <= 0)
			return _T("unknown");

		struct tm tmBuild = {};
		if (_gmtime64_s(&tmBuild, &tBuildEpoch) != 0)
			return _T("unknown");

		CString strDate;
		strDate.Format(_T("%04u-%02u-%02u %02u:%02u UTC"),
			static_cast<unsigned>(tmBuild.tm_year + 1900),
			static_cast<unsigned>(tmBuild.tm_mon + 1),
			static_cast<unsigned>(tmBuild.tm_mday),
			static_cast<unsigned>(tmBuild.tm_hour),
			static_cast<unsigned>(tmBuild.tm_min));
		return strDate;
	}

}

/**
 * @brief Small MMDB reader specialized for DB-IP City Lite lookups.
 */
class CGeoLocation::CMmdbCityDatabase
{
public:
	CMmdbCityDatabase()
		: m_uIPVersion(0)
		, m_uRecordSize(0)
		, m_dwNodeCount(0)
		, m_dwNodeSize(0)
		, m_dwIndexSize(0)
		, m_tBuildEpoch(0)
	{
	}

	bool Load(const CString& strFilePath, CString& strError)
	{
		m_vecData.clear();
		m_mapNodeGeo.clear();
		m_tBuildEpoch = 0;
		m_strDatabaseType.Empty();

		std::vector<unsigned char> vecData;
		if (!LongPathSeams::ReadAllBytes(strFilePath, vecData)) {
			strError.Format(_T("GeoLocation: data file not found: %s"), (LPCTSTR)strFilePath);
			return false;
		}

		const ULONGLONG ullSize = static_cast<ULONGLONG>(vecData.size());
		if (ullSize == 0 || ullSize > MAX_MMDB_FILE_SIZE) {
			strError.Format(_T("GeoLocation: unsupported database file size: %I64u"), ullSize);
			return false;
		}

		m_vecData.swap(vecData);

		const MMDBValue metadata = ReadMetadata();
		if (!ParseMetadata(metadata, strError))
			return false;

		if (!LoadIndex(strError))
			return false;

		return true;
	}

	__time64_t GetBuildEpoch() const
	{
		return m_tBuildEpoch;
	}

	const CString& GetType() const
	{
		return m_strDatabaseType;
	}

	bool LookupGeoRecord(uint32 dwIP, SNodeGeoLocation& out) const
	{
		out = SNodeGeoLocation();
		if (m_vecData.empty())
			return false;

		BYTE aucAddress[16] = {0};
		aucAddress[10] = 0xFF;
		aucAddress[11] = 0xFF;
		aucAddress[12] = static_cast<BYTE>(dwIP & 0xFF);
		aucAddress[13] = static_cast<BYTE>((dwIP >> 8) & 0xFF);
		aucAddress[14] = static_cast<BYTE>((dwIP >> 16) & 0xFF);
		aucAddress[15] = static_cast<BYTE>((dwIP >> 24) & 0xFF);

		uint32 dwNode = 0;
		for (size_t iByte = 0; iByte < _countof(aucAddress); ++iByte) {
			for (int iBit = 0; iBit < 8; ++iBit) {
				const bool bRight = (aucAddress[iByte] & (0x80 >> iBit)) != 0;
				const uint32 dwRecord = ReadNodeRecord(dwNode, bRight ? 1 : 0);
				if (dwRecord == m_dwNodeCount)
					return false;
				if (dwRecord > m_dwNodeCount)
					return LookupGeoRecordByNode(dwRecord, out);
				dwNode = dwRecord;
			}
		}

		return false;
	}

private:
	bool ParseMetadata(const MMDBValue& value, CString& strError)
	{
		if (value.eType != MMDB_TYPE_MAP) {
			strError = _T("GeoLocation: metadata map not found.");
			return false;
		}

		ULONGLONG ullValue = 0;
		if (!GetMapUnsigned(value, "binary_format_major_version", ullValue) || ullValue != 2) {
			strError = _T("GeoLocation: unsupported database version.");
			return false;
		}

		if (!GetMapUnsigned(value, "ip_version", ullValue)) {
			strError = _T("GeoLocation: metadata ip_version missing.");
			return false;
		}
		m_uIPVersion = static_cast<uint16>(ullValue);
		if (m_uIPVersion != 6) {
			strError.Format(_T("GeoLocation: unsupported IP version: %u"), m_uIPVersion);
			return false;
		}

		if (!GetMapUnsigned(value, "record_size", ullValue)) {
			strError = _T("GeoLocation: metadata record_size missing.");
			return false;
		}
		m_uRecordSize = static_cast<uint16>(ullValue);
		if (m_uRecordSize != 24 && m_uRecordSize != 28 && m_uRecordSize != 32) {
			strError.Format(_T("GeoLocation: unsupported record size: %u"), m_uRecordSize);
			return false;
		}
		m_dwNodeSize = (m_uRecordSize == 24) ? 6u : ((m_uRecordSize == 28) ? 7u : 8u);

		if (!GetMapUnsigned(value, "node_count", ullValue)) {
			strError = _T("GeoLocation: metadata node_count missing.");
			return false;
		}
		m_dwNodeCount = static_cast<uint32>(ullValue);
		m_dwIndexSize = m_dwNodeCount * m_dwNodeSize;

		std::string strDatabaseType;
		if (!GetMapString(value, "database_type", strDatabaseType)) {
			strError = _T("GeoLocation: metadata database_type missing.");
			return false;
		}
		m_strDatabaseType = CString(strDatabaseType.c_str());

		if (!GetMapUnsigned(value, "build_epoch", ullValue)) {
			strError = _T("GeoLocation: metadata build_epoch missing.");
			return false;
		}
		m_tBuildEpoch = static_cast<__time64_t>(ullValue);
		return true;
	}

	bool LoadIndex(CString& strError) const
	{
		if (m_vecData.size() < (m_dwIndexSize + sizeof(s_aucDataSectionSeparator))) {
			strError = _T("GeoLocation: database index is truncated.");
			return false;
		}
		if (memcmp(&m_vecData[m_dwIndexSize], s_aucDataSectionSeparator, sizeof(s_aucDataSectionSeparator)) != 0) {
			strError = _T("GeoLocation: no MMDB data section found.");
			return false;
		}
		return true;
	}

	MMDBValue ReadMetadata() const
	{
		if (m_vecData.size() < sizeof(s_aucMetadataMarker))
			return MMDBValue();

		size_t uOffset = m_vecData.size() - sizeof(s_aucMetadataMarker);
		for (;;) {
			if (memcmp(&m_vecData[uOffset], s_aucMetadataMarker, sizeof(s_aucMetadataMarker)) == 0) {
				uint32 dwDataOffset = static_cast<uint32>(uOffset + sizeof(s_aucMetadataMarker));
				return ReadDataField(dwDataOffset);
			}
			if (uOffset == 0)
				break;
			--uOffset;
		}
		return MMDBValue();
	}

	MMDBValue ReadDataField(uint32& dwOffset) const
	{
		MMDBFieldDescriptor descriptor;
		if (!ReadDataFieldDescriptor(dwOffset, descriptor))
			return MMDBValue();

		uint32 dwLocalOffset = dwOffset;
		bool bPointer = false;
		if (descriptor.eType == MMDB_TYPE_POINTER) {
			bPointer = true;
			dwLocalOffset = descriptor.dwOffset + m_dwIndexSize + sizeof(s_aucDataSectionSeparator);
			if (!ReadDataFieldDescriptor(dwLocalOffset, descriptor))
				return MMDBValue();
		}

		MMDBValue value;
		value.eType = descriptor.eType;
		switch (descriptor.eType) {
		case MMDB_TYPE_STRING:
			if (!CanRead(dwLocalOffset, descriptor.dwFieldSize))
				return MMDBValue();
			value.strValue.assign(reinterpret_cast<const char*>(&m_vecData[dwLocalOffset]), descriptor.dwFieldSize);
			dwLocalOffset += descriptor.dwFieldSize;
			break;
		case MMDB_TYPE_UINT16:
		case MMDB_TYPE_UINT32:
		case MMDB_TYPE_INT32:
		case MMDB_TYPE_UINT64:
		case MMDB_TYPE_UINT128:
			if (!ReadUnsignedInteger(dwLocalOffset, descriptor.dwFieldSize, value.ullValue))
				return MMDBValue();
			break;
		case MMDB_TYPE_MAP:
			if (!ReadMapValue(dwLocalOffset, descriptor.dwFieldSize, value))
				return MMDBValue();
			break;
		case MMDB_TYPE_ARRAY:
			if (!ReadArrayValue(dwLocalOffset, descriptor.dwFieldSize, value))
				return MMDBValue();
			break;
		case MMDB_TYPE_BOOLEAN:
			value.bValue = descriptor.dwFieldSize != 0;
			break;
		case MMDB_TYPE_BYTES:
		case MMDB_TYPE_DOUBLE:
		case MMDB_TYPE_FLOAT:
			if (!CanRead(dwLocalOffset, descriptor.dwFieldSize))
				return MMDBValue();
			dwLocalOffset += descriptor.dwFieldSize;
			break;
		default:
			return MMDBValue();
		}

		if (!bPointer)
			dwOffset = dwLocalOffset;
		return value;
	}

	bool ReadDataFieldDescriptor(uint32& dwOffset, MMDBFieldDescriptor& descriptor) const
	{
		if (!CanRead(dwOffset, 1))
			return false;

		const BYTE* pucData = &m_vecData[dwOffset];
		const uint32 dwAvailable = static_cast<uint32>(m_vecData.size() - dwOffset);

		descriptor.eType = static_cast<MMDBDataType>((pucData[0] & 0xE0) >> 5);
		if (descriptor.eType == MMDB_TYPE_POINTER) {
			const int iSize = (pucData[0] & 0x18) >> 3;
			if (dwAvailable < static_cast<uint32>(iSize + 2))
				return false;

			if (iSize == 0)
				descriptor.dwOffset = ((pucData[0] & 0x07) << 8) + pucData[1];
			else if (iSize == 1)
				descriptor.dwOffset = ((pucData[0] & 0x07) << 16) + (pucData[1] << 8) + pucData[2] + 2048;
			else if (iSize == 2)
				descriptor.dwOffset = ((pucData[0] & 0x07) << 24) + (pucData[1] << 16) + (pucData[2] << 8) + pucData[3] + 526336;
			else
				descriptor.dwOffset = (pucData[1] << 24) + (pucData[2] << 16) + (pucData[3] << 8) + pucData[4];

			dwOffset += iSize + 2;
			return true;
		}

		descriptor.dwFieldSize = pucData[0] & 0x1F;
		if (descriptor.dwFieldSize <= 28) {
			if (descriptor.eType == MMDB_TYPE_UNKNOWN) {
				if (dwAvailable < 2)
					return false;
				descriptor.eType = static_cast<MMDBDataType>(pucData[1] + 7);
				dwOffset += 2;
			} else {
				dwOffset += 1;
			}
		} else if (descriptor.dwFieldSize == 29) {
			if (dwAvailable < 2)
				return false;
			descriptor.dwFieldSize = pucData[1] + 29;
			dwOffset += 2;
		} else if (descriptor.dwFieldSize == 30) {
			if (dwAvailable < 3)
				return false;
			descriptor.dwFieldSize = (pucData[1] << 8) + pucData[2] + 285;
			dwOffset += 3;
		} else {
			if (dwAvailable < 4)
				return false;
			descriptor.dwFieldSize = (pucData[1] << 16) + (pucData[2] << 8) + pucData[3] + 65821;
			dwOffset += 4;
		}

		return true;
	}

	bool ReadMapValue(uint32& dwOffset, uint32 dwCount, MMDBValue& value) const
	{
		value.eType = MMDB_TYPE_MAP;
		for (uint32 i = 0; i < dwCount; ++i) {
			const MMDBValue key = ReadDataField(dwOffset);
			if (key.eType != MMDB_TYPE_STRING)
				return false;

			const MMDBValue fieldValue = ReadDataField(dwOffset);
			if (fieldValue.eType == MMDB_TYPE_UNKNOWN)
				return false;

			value.mapValue[key.strValue] = fieldValue;
		}
		return true;
	}

	bool ReadArrayValue(uint32& dwOffset, uint32 dwCount, MMDBValue& value) const
	{
		value.eType = MMDB_TYPE_ARRAY;
		for (uint32 i = 0; i < dwCount; ++i) {
			const MMDBValue fieldValue = ReadDataField(dwOffset);
			if (fieldValue.eType == MMDB_TYPE_UNKNOWN)
				return false;
			value.vecValue.push_back(fieldValue);
		}
		return true;
	}

	bool ReadUnsignedInteger(uint32& dwOffset, uint32 dwLength, ULONGLONG& ullValue) const
	{
		if (dwLength > 8 || !CanRead(dwOffset, dwLength))
			return false;

		ullValue = 0;
		for (uint32 i = 0; i < dwLength; ++i)
			ullValue = (ullValue << 8) | m_vecData[dwOffset + i];
		dwOffset += dwLength;
		return true;
	}

	bool CanRead(uint32 dwOffset, uint32 dwLength) const
	{
		return (dwOffset <= m_vecData.size()) && (dwLength <= (m_vecData.size() - dwOffset));
	}

	uint32 ReadNodeRecord(uint32 dwNode, int iBranch) const
	{
		const uint32 dwOffset = dwNode * m_dwNodeSize;
		if (!CanRead(dwOffset, m_dwNodeSize))
			return m_dwNodeCount;

		const BYTE* p = &m_vecData[dwOffset];
		if (m_uRecordSize == 24) {
			const int iShift = iBranch * 3;
			return (static_cast<uint32>(p[iShift]) << 16)
				| (static_cast<uint32>(p[iShift + 1]) << 8)
				| static_cast<uint32>(p[iShift + 2]);
		}

		if (m_uRecordSize == 28) {
			if (iBranch == 0) {
				// MaxMind stores the left 28-bit record as the high nibble of byte 3 followed by bytes 0-2.
				return ((static_cast<uint32>(p[3]) & 0xF0) << 20)
					| (static_cast<uint32>(p[0]) << 16)
					| (static_cast<uint32>(p[1]) << 8)
					| static_cast<uint32>(p[2]);
			}
			return ((static_cast<uint32>(p[3]) & 0x0F) << 24)
				| (static_cast<uint32>(p[4]) << 16)
				| (static_cast<uint32>(p[5]) << 8)
				| static_cast<uint32>(p[6]);
		}

		const int iShift = iBranch * 4;
		return (static_cast<uint32>(p[iShift]) << 24)
			| (static_cast<uint32>(p[iShift + 1]) << 16)
			| (static_cast<uint32>(p[iShift + 2]) << 8)
			| static_cast<uint32>(p[iShift + 3]);
	}

	bool LookupGeoRecordByNode(uint32 dwNodeID, SNodeGeoLocation& out) const
	{
		std::map<uint32, SNodeGeoLocation>::const_iterator itFound = m_mapNodeGeo.find(dwNodeID);
		if (itFound != m_mapNodeGeo.end()) {
			out = itFound->second;
			return !out.strCountryCode.IsEmpty();
		}

		if (dwNodeID <= m_dwNodeCount)
			return false;

		uint32 dwOffset = m_dwIndexSize + sizeof(s_aucDataSectionSeparator) + (dwNodeID - m_dwNodeCount - sizeof(s_aucDataSectionSeparator));
		const MMDBValue value = ReadDataField(dwOffset);
		if (value.eType != MMDB_TYPE_MAP)
			return false;

		SNodeGeoLocation decoded;
		if (!ExtractCountryCode(value, decoded.strCountryCode))
			return false;
		(void)ExtractCountryName(value, decoded.strCountryName);
		(void)ExtractCityName(value, decoded.strCityName);

		m_mapNodeGeo[dwNodeID] = decoded;
		out = decoded;
		return true;
	}

	static bool GetMapUnsigned(const MMDBValue& value, const char* pszKey, ULONGLONG& ullValue)
	{
		std::map<std::string, MMDBValue>::const_iterator itFound = value.mapValue.find(pszKey);
		if (itFound == value.mapValue.end())
			return false;
		if (itFound->second.eType != MMDB_TYPE_UINT16 && itFound->second.eType != MMDB_TYPE_UINT32
			&& itFound->second.eType != MMDB_TYPE_UINT64 && itFound->second.eType != MMDB_TYPE_INT32
			&& itFound->second.eType != MMDB_TYPE_UINT128)
		{
			return false;
		}
		ullValue = itFound->second.ullValue;
		return true;
	}

	static bool GetMapString(const MMDBValue& value, const char* pszKey, std::string& strValue)
	{
		std::map<std::string, MMDBValue>::const_iterator itFound = value.mapValue.find(pszKey);
		if (itFound == value.mapValue.end() || itFound->second.eType != MMDB_TYPE_STRING)
			return false;
		strValue = itFound->second.strValue;
		return true;
	}

	static bool GetNestedMapString(const MMDBValue& value, const char* pszMapKey, const char* pszNestedKey, std::string& strValue)
	{
		std::map<std::string, MMDBValue>::const_iterator itMap = value.mapValue.find(pszMapKey);
		if (itMap == value.mapValue.end() || itMap->second.eType != MMDB_TYPE_MAP)
			return false;
		return GetMapString(itMap->second, pszNestedKey, strValue);
	}

	static bool GetLocalizedName(const MMDBValue& value, const char* pszKey, std::string& strValue)
	{
		std::map<std::string, MMDBValue>::const_iterator itMap = value.mapValue.find(pszKey);
		if (itMap == value.mapValue.end() || itMap->second.eType != MMDB_TYPE_MAP)
			return false;

		std::map<std::string, MMDBValue>::const_iterator itNames = itMap->second.mapValue.find("names");
		if (itNames == itMap->second.mapValue.end() || itNames->second.eType != MMDB_TYPE_MAP)
			return false;

		return GetMapString(itNames->second, "en", strValue);
	}

	static bool ExtractCountryCode(const MMDBValue& value, CString& strCountryCode)
	{
		std::string strCode;
		if (!GetNestedMapString(value, "country", "iso_code", strCode)
			&& !GetNestedMapString(value, "registered_country", "iso_code", strCode))
		{
			return false;
		}

		strCountryCode = Utf8StringToCString(strCode);
		strCountryCode.MakeUpper();
		return !strCountryCode.IsEmpty();
	}

	static bool ExtractCountryName(const MMDBValue& value, CString& strCountryName)
	{
		std::string strName;
		if (!GetLocalizedName(value, "country", strName)
			&& !GetLocalizedName(value, "registered_country", strName))
		{
			return false;
		}

		strCountryName = Utf8StringToCString(strName);
		return !strCountryName.IsEmpty();
	}

	static bool ExtractCityName(const MMDBValue& value, CString& strCityName)
	{
		std::string strName;
		if (!GetLocalizedName(value, "city", strName))
			return false;

		strCityName = Utf8StringToCString(strName);
		return !strCityName.IsEmpty();
	}

	mutable std::map<uint32, SNodeGeoLocation> m_mapNodeGeo;
	std::vector<BYTE> m_vecData;
	uint16 m_uIPVersion;
	uint16 m_uRecordSize;
	uint32 m_dwNodeCount;
	uint32 m_dwNodeSize;
	uint32 m_dwIndexSize;
	__time64_t m_tBuildEpoch;
	CString m_strDatabaseType;
};

SGeoLocationRecord::SGeoLocationRecord()
	: iFlagImageIndex(-2)
	, bResolved(false)
{
}

CGeoLocation::CGeoLocation()
	: m_pDatabase(NULL)
	, m_tBuildEpoch(0)
	, m_pFlagImageList(NULL)
	, m_bBackgroundRefreshQueued(false)
{
	m_defaultRecord.strCountryCode = _T("N/A");
	m_defaultRecord.strCountryName = _T("N/A");
	m_defaultRecord.iFlagImageIndex = -1;
}

CGeoLocation::~CGeoLocation()
{
	Unload();
	if (m_pFlagImageList != NULL) {
		m_pFlagImageList->DeleteImageList();
		delete m_pFlagImageList;
		m_pFlagImageList = NULL;
	}
}

void CGeoLocation::Load()
{
	Unload();
	if (!IsEnabled())
		return;

	CString strError;
	CMmdbCityDatabase* pDatabase = new CMmdbCityDatabase;
	if (!pDatabase->Load(GetDatabaseFilePath(), strError)) {
		delete pDatabase;
		if (thePrefs.GetVerbose())
			AddDebugLogLine(false, _T("%s"), (LPCTSTR)strError);
		return;
	}

	m_pDatabase = pDatabase;
	m_tBuildEpoch = m_pDatabase->GetBuildEpoch();
	AddLogLine(false, _T("GeoLocation: Loaded %s (%s)."), (LPCTSTR)m_pDatabase->GetType(), (LPCTSTR)FormatBuildDate(m_tBuildEpoch));
}

void CGeoLocation::Unload()
{
	delete m_pDatabase;
	m_pDatabase = NULL;
	m_tBuildEpoch = 0;
	m_bBackgroundRefreshQueued = false;
	ClearCache();
}

void CGeoLocation::QueueBackgroundRefresh()
{
	(void)QueueRefresh(false, false);
}

void CGeoLocation::QueueManualRefresh()
{
	(void)QueueRefresh(true, true);
}

bool CGeoLocation::QueueRefresh(bool bForce, bool bUserInitiated)
{
	if (!IsEnabled()) {
		if (bUserInitiated)
			AddLogLine(false, _T("GeoLocation: manual download ignored because IP geolocation is disabled."));
		return false;
	}
	if (m_bBackgroundRefreshQueued) {
		if (bUserInitiated)
			AddLogLine(false, _T("GeoLocation: refresh already in progress."));
		return false;
	}

	const HWND hNotifyWnd = theApp.emuledlg != NULL ? theApp.emuledlg->m_hWnd : NULL;
	if (hNotifyWnd == NULL)
		return false;

	const CString strDatabasePath = GetDatabaseFilePath();
	if (!bForce && !IsAutomaticRefreshDue())
		return false;

	CString strConfigDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	CString strArchiveTempPath;
	CString strDatabaseTempPath;
	CString strError;
	if (!DirectDownload::CreateTempPathInDirectory(strConfigDir, _T("geo"), strArchiveTempPath, strError)) {
		AddDebugLogLine(false, _T("%s"), (LPCTSTR)strError);
		return false;
	}
	if (!DirectDownload::CreateTempPathInDirectory(strConfigDir, _T("gdb"), strDatabaseTempPath, strError)) {
		(void)LongPathSeams::DeleteFileIfExists(strArchiveTempPath);
		AddDebugLogLine(false, _T("%s"), (LPCTSTR)strError);
		return false;
	}

	std::unique_ptr<SBackgroundRefreshContext> pContext(new SBackgroundRefreshContext);
	pContext->strDownloadUrl = ExpandConfiguredUpdateUrlTemplate();
	pContext->strArchiveTempPath = strArchiveTempPath + _T(".gz");
	pContext->strDatabaseTempPath = strDatabaseTempPath;
	pContext->strInstallPath = strDatabasePath;
	pContext->hNotifyWnd = hNotifyWnd;
	pContext->bProxyEnabled = thePrefs.GetProxySettings().bUseProxy;

	(void)LongPathSeams::DeleteFileIfExists(pContext->strArchiveTempPath);

	CWinThread* pThread = AfxBeginThread(BackgroundRefreshThread, pContext.get(), THREAD_PRIORITY_BELOW_NORMAL, 0, 0, NULL);
	if (pThread == NULL) {
		(void)LongPathSeams::DeleteFileIfExists(pContext->strArchiveTempPath);
		(void)LongPathSeams::DeleteFileIfExists(pContext->strDatabaseTempPath);
		AddDebugLogLine(false, _T("GeoLocation: failed to start background refresh thread."));
		return false;
	}

	pThread->m_bAutoDelete = TRUE;
	m_bBackgroundRefreshQueued = true;
	__time64_t tNow = 0;
	_time64(&tNow);
	thePrefs.SetGeoLocationLastCheckTime(tNow, true);
	(void)pContext.release();
	return true;
}

void CGeoLocation::HandleBackgroundRefreshResult(bool bUpdated)
{
	m_bBackgroundRefreshQueued = false;
	if (bUpdated) {
		Load();
		RefreshVisibleWindows();
	}
}

const SGeoLocationRecord& CGeoLocation::Lookup(uint32 dwIP) const
{
	if (!IsEnabled() || m_pDatabase == NULL || IsPrivateIPv4(dwIP))
		return m_defaultRecord;

	std::map<uint32, SGeoLocationRecord>::const_iterator itFound = m_cacheByIp.find(dwIP);
	if (itFound != m_cacheByIp.end())
		return itFound->second;

	SNodeGeoLocation decoded;
	if (!m_pDatabase->LookupGeoRecord(dwIP, decoded) || decoded.strCountryCode.IsEmpty())
		return m_defaultRecord;

	SGeoLocationRecord& cached = m_cacheByIp[dwIP];
	cached.strCountryCode = decoded.strCountryCode;
	cached.strCountryName = decoded.strCountryName.IsEmpty() ? decoded.strCountryCode : decoded.strCountryName;
	cached.strCityName = decoded.strCityName;
	cached.bResolved = true;
	return cached;
}

CString CGeoLocation::GetDisplayText(uint32 dwIP) const
{
	return FormatDisplayText(Lookup(dwIP));
}

CImageList* CGeoLocation::GetFlagImageList()
{
	if (m_pFlagImageList == NULL) {
		m_pFlagImageList = new CImageList;
		if (!m_pFlagImageList->Create(FLAG_ICON_WIDTH, FLAG_ICON_HEIGHT, ILC_COLOR32 | ILC_MASK, 0, 32)) {
			delete m_pFlagImageList;
			m_pFlagImageList = NULL;
		}
	}
	return m_pFlagImageList;
}

int CGeoLocation::GetFlagImageIndex(uint32 dwIP)
{
	const SGeoLocationRecord& record = Lookup(dwIP);
	if (!record.bResolved)
		return -1;

	SGeoLocationRecord& mutableRecord = m_cacheByIp[dwIP];
	if (mutableRecord.iFlagImageIndex == -2)
		mutableRecord.iFlagImageIndex = GetFlagImageIndexForCode(record.strCountryCode);
	return mutableRecord.iFlagImageIndex;
}

bool CGeoLocation::DrawFlag(CDC& dc, uint32 dwIP, const POINT& point)
{
	const int iFlagIndex = GetFlagImageIndex(dwIP);
	CImageList* pFlagImageList = GetFlagImageList();
	return iFlagIndex >= 0 && pFlagImageList != NULL && pFlagImageList->Draw(&dc, iFlagIndex, point, ILD_NORMAL) != FALSE;
}

void CGeoLocation::RefreshVisibleWindows() const
{
	if (theApp.emuledlg == NULL)
		return;

	if (theApp.emuledlg->serverwnd != NULL)
		theApp.emuledlg->serverwnd->serverlistctrl.Invalidate();
	if (theApp.emuledlg->transferwnd != NULL) {
		theApp.emuledlg->transferwnd->GetDownloadList()->Invalidate();
		theApp.emuledlg->transferwnd->GetClientList()->Invalidate();
		theApp.emuledlg->transferwnd->GetDownloadClientsList()->Invalidate();
		theApp.emuledlg->transferwnd->GetQueueList()->Invalidate();
		theApp.emuledlg->transferwnd->m_pwndTransfer->uploadlistctrl.Invalidate();
	}
	if (theApp.emuledlg->kademliawnd != NULL)
		theApp.emuledlg->kademliawnd->Invalidate();
}

bool CGeoLocation::IsEnabled() const
{
	return thePrefs.IsGeoLocationEnabled();
}

bool CGeoLocation::IsPrivateIPv4(uint32 dwIP) const
{
	const BYTE b1 = static_cast<BYTE>(dwIP & 0xFF);
	const BYTE b2 = static_cast<BYTE>((dwIP >> 8) & 0xFF);

	if (dwIP == 0)
		return true;
	if (b1 == 10 || b1 == 127)
		return true;
	if (b1 == 169 && b2 == 254)
		return true;
	if (b1 == 172 && b2 >= 16 && b2 <= 31)
		return true;
	if (b1 == 192 && b2 == 168)
		return true;
	if (b1 >= 224)
		return true;
	return false;
}

bool CGeoLocation::IsAutomaticRefreshDue() const
{
	const UINT uCheckDays = thePrefs.GetGeoLocationCheckDays();
	if (uCheckDays == 0)
		return false;

	const __time64_t tLastCheck = thePrefs.GetGeoLocationLastCheckTime();
	if (tLastCheck <= 0)
		return true;

	__time64_t tNow = 0;
	_time64(&tNow);
	return tNow >= (tLastCheck + static_cast<__time64_t>(uCheckDays) * 24 * 60 * 60);
}

CString CGeoLocation::ExpandConfiguredUpdateUrlTemplate() const
{
	CString strTemplate(thePrefs.GetGeoLocationUpdateUrlTemplate());
	if (strTemplate.IsEmpty())
		strTemplate = DFLT_GEOLOCATION_DB_URL_TEMPLATE;

	SYSTEMTIME stNow = {};
	::GetSystemTime(&stNow);

	CString strYear;
	strYear.Format(_T("%04u"), stNow.wYear);
	CString strMonth;
	strMonth.Format(_T("%02u"), stNow.wMonth);
	strTemplate.Replace(_T("%Y"), strYear);
	strTemplate.Replace(_T("%m"), strMonth);
	return strTemplate;
}

CString CGeoLocation::GetDatabaseFilePath() const
{
	return thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + DFLT_GEOLOCATION_DB_FILENAME;
}

CString CGeoLocation::FormatDisplayText(const SGeoLocationRecord& record) const
{
	if (!record.bResolved)
		return m_defaultRecord.strCountryName;
	if (!record.strCityName.IsEmpty())
		return record.strCountryName + _T(", ") + record.strCityName;
	return record.strCountryName;
}

int CGeoLocation::GetFlagImageIndexForCode(const CString& strCountryCode)
{
	CString strLookupCode = strCountryCode;
	strLookupCode.Trim();
	strLookupCode.MakeUpper();
	if (strLookupCode.IsEmpty() || strLookupCode == _T("N/A"))
		return -1;

	std::map<CString, int, CStringLess>::const_iterator itFound = m_flagIndexByCode.find(strLookupCode);
	if (itFound != m_flagIndexByCode.end())
		return itFound->second;

	CImageList* pFlagImageList = GetFlagImageList();
	if (pFlagImageList == NULL)
		return -1;

	const CString strResourceName = _T("FLAG_") + strLookupCode;
	HICON hIcon = reinterpret_cast<HICON>(::LoadImage(AfxGetResourceHandle(), strResourceName, IMAGE_ICON, FLAG_ICON_WIDTH, FLAG_ICON_HEIGHT, LR_DEFAULTCOLOR));
	int iImage = -1;
	if (hIcon != NULL) {
		iImage = pFlagImageList->Add(hIcon);
		::DestroyIcon(hIcon);
	}

	m_flagIndexByCode[strLookupCode] = iImage;
	return iImage;
}

void CGeoLocation::ClearCache()
{
	m_cacheByIp.clear();
	m_flagIndexByCode.clear();
	m_defaultRecord.iFlagImageIndex = -1;
	if (m_pFlagImageList != NULL) {
		m_pFlagImageList->DeleteImageList();
		delete m_pFlagImageList;
		m_pFlagImageList = NULL;
	}
}

UINT AFX_CDECL CGeoLocation::BackgroundRefreshThread(LPVOID pParam)
{
	std::unique_ptr<SBackgroundRefreshContext> pContext(reinterpret_cast<SBackgroundRefreshContext*>(pParam));
	if (pContext.get() == NULL)
		return 0;

	bool bUpdated = false;
	CGZIPFile gzipFile;
	CString strLoadError;
	CMmdbCityDatabase validator;
	if (pContext->bProxyEnabled) {
		theApp.QueueLogLine(false, _T("GeoLocation: proxy-backed geo DB refresh is deprecated and ignored; use a VPN for network privacy."));
	}

	CString strError;
	if (!DirectDownload::DownloadUrlToFile(pContext->strDownloadUrl, pContext->strArchiveTempPath, strError)) {
		AddDebugLogLine(false, _T("GeoLocation: download failed from %s (%s)"), (LPCTSTR)pContext->strDownloadUrl, (LPCTSTR)strError);
		goto cleanup;
	}

	if (!gzipFile.Open(pContext->strArchiveTempPath) || !gzipFile.Extract(pContext->strDatabaseTempPath)) {
		AddDebugLogLine(false, _T("GeoLocation: failed to unpack %s"), (LPCTSTR)pContext->strArchiveTempPath);
		gzipFile.Close();
		goto cleanup;
	}
	gzipFile.Close();

	if (!validator.Load(pContext->strDatabaseTempPath, strLoadError)) {
		AddDebugLogLine(false, _T("GeoLocation: downloaded database rejected (%s)"), (LPCTSTR)strLoadError);
		goto cleanup;
	}

	if (!LongPathSeams::MoveFileEx(pContext->strDatabaseTempPath, pContext->strInstallPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		AddDebugLogLine(false, _T("GeoLocation: failed to install %s (%u)"), (LPCTSTR)pContext->strInstallPath, ::GetLastError());
		goto cleanup;
	}

	theApp.QueueLogLine(false, _T("GeoLocation: updated %s (%s)."), (LPCTSTR)validator.GetType(), (LPCTSTR)FormatBuildDate(validator.GetBuildEpoch()));
	bUpdated = true;

cleanup:
	(void)LongPathSeams::DeleteFileIfExists(pContext->strArchiveTempPath);
	(void)LongPathSeams::DeleteFileIfExists(pContext->strDatabaseTempPath);
	if (pContext->hNotifyWnd != NULL)
		(void)::PostMessage(pContext->hNotifyWnd, UM_GEOLOCATION_UPDATED, bUpdated ? 1u : 0u, 0);
	return 0;
}
