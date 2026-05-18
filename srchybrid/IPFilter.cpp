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
#include <share.h>
#include <fcntl.h>
#include <io.h>
#include "emule.h"
#include "IPFilter.h"
#include "IPFilterSeams.h"
#include "OtherFunctions.h"
#include "Preferences.h"
#include "Log.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


#define	DFLT_FILTER_LEVEL	IPFilterSeams::kDefaultFilterLevel // default filter level if non specified

CIPFilter::CIPFilter()
	: m_pLastHit()
	, m_bModified(false)
{
	if (IPFilterSeams::ShouldLoadAtStartup(thePrefs.IsIPFilterEnabled()))
		LoadFromDefaultFile(false);
}

CIPFilter::~CIPFilter()
{
	if (m_bModified) {
		try {
			SaveToDefaultFile();
		} catch (const CString&) {
		}
	}
	RemoveAllIPFilters();
}

CString CIPFilter::GetDefaultFilePath()
{
	return thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + DFLT_IPFILTER_FILENAME;
}

INT_PTR	CIPFilter::LoadFromDefaultFile(bool bShowResponse)
{
	RemoveAllIPFilters();
	m_bModified = false;
	return AddFromFile(GetDefaultFilePath(), bShowResponse);
}

#pragma warning(push)
#pragma warning(disable:4701) //local variables 'level' 'end' 'start'
INT_PTR CIPFilter::AddFromFile(LPCTSTR pszFilePath, bool bShowResponse)
{
	const ULONGLONG dwStart = ::GetTickCount64();
	FILE *readFile = LongPathSeams::OpenFileStreamDenyWriteLongPath(pszFilePath, _T("r"));
	if (readFile != NULL) {
		int iFoundRanges = 0;
		int iLine = 0;
		try {
			enum EIPFilterFileType
			{
				Unknown = 0,
				FilterDat = 1,		// ipfilter.dat/ip.prefix format
				PeerGuardian = 2,	// PeerGuardian text format
				PeerGuardian2 = 3	// PeerGuardian binary format
			} eFileType = Unknown;
			::setvbuf(readFile, NULL, _IOFBF, 32768);

			const IPFilterSeams::PathHintType ePathHintType = IPFilterSeams::DetectFileTypeFromPath(CString(pszFilePath));
			if (ePathHintType == IPFilterSeams::PathHintPeerGuardian)
				eFileType = PeerGuardian;
			else if (ePathHintType == IPFilterSeams::PathHintFilterDat)
				eFileType = FilterDat;
			else {
				VERIFY(_setmode(_fileno(readFile), _O_BINARY) != -1);
				static const BYTE _aucP2Bheader[] = "\xff\xff\xff\xff" "P2B";
				BYTE aucHeader[sizeof _aucP2Bheader - 1];
				if (fread(aucHeader, sizeof aucHeader, 1, readFile) == 1) {
					if (memcmp(aucHeader, _aucP2Bheader, sizeof _aucP2Bheader - 1) == 0)
						eFileType = PeerGuardian2;
					else {
						(void)fseek(readFile, 0, SEEK_SET);
						VERIFY(_setmode(_fileno(readFile), _O_TEXT) != -1); // ugly!
					}
				}
			}

			uint32 prevStart = 0; //set 'modified' flag if start IPs were not in ascending order
			if (eFileType == PeerGuardian2) {
				// Version 1: strings are ISO-8859-1 encoded
				// Version 2: strings are UTF-8 encoded
				uint8 nVersion;
				if (fread(&nVersion, sizeof nVersion, 1, readFile) == 1 && (nVersion == 1 || nVersion == 2)) {
					while (!feof(readFile)) {
						CHAR szName[256];
						unsigned iLen = 0;
						for (;;) { // read until NUL or EOF
							int iChar = getc(readFile);
							if (iChar == EOF)
								break;
							if (iLen < sizeof szName - 1)
								szName[iLen++] = (CHAR)iChar;
							if (iChar == '\0')
								break;
						}
						szName[iLen] = '\0';

						uint32 uStart;
						if (fread(&uStart, sizeof uStart, 1, readFile) != 1)
							break;
						uStart = ntohl(uStart);
						if (prevStart > uStart)
							m_bModified = true;
						prevStart = uStart;

						uint32 uEnd;
						if (fread(&uEnd, sizeof uEnd, 1, readFile) != 1)
							break;
						uEnd = ntohl(uEnd);

						++iLine;
						// (nVersion == 2) ? OptUtf8ToStr(szName, iLen) :
						AddIPRange(uStart, uEnd, DFLT_FILTER_LEVEL, CStringA(szName, iLen));
						++iFoundRanges;
					}
				}
			} else {
				CStringA sbuffer;
				CHAR szBuffer[1024];
				while (fgets(szBuffer, _countof(szBuffer), readFile) != NULL) {
					++iLine;
					sbuffer = szBuffer;
					sbuffer.Trim(" \t\r\n");

					// ignore comments & too short lines
					if (sbuffer[0] == '#' || sbuffer[0] == '/' || sbuffer.GetLength() < 15) {
						DEBUG_ONLY((!sbuffer.IsEmpty()) ? TRACE("IP filter: ignored line %u\n", iLine) : (void)0);
						continue;
					}

					if (eFileType == Unknown) {
						// looks like html
						if (sbuffer.Find('>') >= 0 && sbuffer.Find('<') >= 0)
							sbuffer.Delete(0, sbuffer.ReverseFind('>') + 1);

						IPFilterSeams::IPRange detectedRange;
						if (IPFilterSeams::TryParseFilterDatLine(sbuffer, detectedRange))
							eFileType = FilterDat;
						else {
							// check for <description> ':' <IP> '-' <IP>
							int iColon = sbuffer.Find(':');
							if (iColon >= 0) {
								if (IPFilterSeams::TryParsePeerGuardianLine(sbuffer, detectedRange))
									eFileType = PeerGuardian;
							}
						}
					}

					bool bValid;
					uint32 start = 0;
					uint32 end = 0;
					uint32 level = 0;
					CStringA desc;
					if (eFileType == FilterDat)
						bValid = ParseFilterLine1(sbuffer, start, end, level, desc);
					else if (eFileType == PeerGuardian)
						bValid = ParseFilterLine2(sbuffer, start, end, level, desc);
					else
						bValid = false;
					// add a filter
					if (bValid) {
						if (prevStart > start)
							m_bModified = true;
						prevStart = start;
						AddIPRange(start, end, level, desc);
						++iFoundRanges;
					} else {
						DEBUG_ONLY(sbuffer.IsEmpty() ? 0 : TRACE("IP filter: ignored line %u\n", iLine));
						m_bModified = true;
					}
				}
			}
		} catch (...) {
			AddDebugLogLine(false, _T("Exception when loading IP filters - %s"), _tcserror(errno));
			fclose(readFile);
			throw;
		}
		fclose(readFile);

		std::vector<IPFilterSeams::IPRange> loadedRanges;
		loadedRanges.reserve(static_cast<size_t>(m_iplist.GetCount()));
		for (INT_PTR iRange = 0; iRange < m_iplist.GetCount(); ++iRange) {
			const SIPFilter *pFilter = m_iplist[iRange];
			IPFilterSeams::IPRange range;
			range.Start = pFilter->start;
			range.End = pFilter->end;
			range.Level = pFilter->level;
			range.Description = pFilter->desc;
			loadedRanges.push_back(range);
		}

		IPFilterSeams::NormalizationStats normalizationStats;
		const std::vector<IPFilterSeams::IPRange> normalizedRanges = IPFilterSeams::NormalizeIPRanges(loadedRanges, &normalizationStats);
		bool bNormalizedChanged = normalizedRanges.size() != loadedRanges.size();
		if (!bNormalizedChanged) {
			for (size_t iRange = 0; iRange < normalizedRanges.size(); ++iRange) {
				if (normalizedRanges[iRange].Start != loadedRanges[iRange].Start
					|| normalizedRanges[iRange].End != loadedRanges[iRange].End
					|| normalizedRanges[iRange].Level != loadedRanges[iRange].Level
					|| normalizedRanges[iRange].Description != loadedRanges[iRange].Description)
				{
					bNormalizedChanged = true;
					break;
				}
			}
		}
		RemoveAllIPFilters();
		for (const IPFilterSeams::IPRange &range : normalizedRanges)
			AddIPRange(range.Start, range.End, range.Level, range.Description);
		if (bNormalizedChanged)
			m_bModified = true;

		if (thePrefs.GetVerbose()) {
			AddDebugLogLine(false, _T("Loaded IP filters from \"%s\""), pszFilePath);
			AddDebugLogLine(false, _T("Parsed lines/entries:%u  Found IP ranges:%u  Duplicate:%u  Merged:%u  Time:%s"), iLine, iFoundRanges, normalizationStats.DuplicateCount, normalizationStats.MergedCount, (LPCTSTR)CastSecondsToHM((::GetTickCount64() - dwStart + 500) / 1000));
		}
		AddLogLine(bShowResponse, GetResString(IDS_IPFILTERLOADED), m_iplist.GetCount());
	}
	return m_iplist.GetCount();
}
#pragma warning(pop)


void CIPFilter::SaveToDefaultFile()
{
	const CString &strFilePath(GetDefaultFilePath());
	FILE *fp = LongPathSeams::OpenFileStreamDenyWriteLongPath(strFilePath, _T("wt"));
	if (fp != NULL) {
		for (INT_PTR i = 0; i < m_iplist.GetCount(); ++i) {
			const SIPFilter *flt = m_iplist[i];

			CHAR szStart[16];
			ipstrA(szStart, _countof(szStart), htonl(flt->start));

			CHAR szEnd[16];
			ipstrA(szEnd, _countof(szEnd), htonl(flt->end));

			if (fprintf(fp, "%-15s - %-15s , %3u , %s\n", szStart, szEnd, flt->level, (LPCSTR)flt->desc) == 0 || ferror(fp)) {
				CString strError;
				strError.Format(_T("Failed to save IP filter to file \"%s\" - %s"), (LPCTSTR)strFilePath, _tcserror(errno));
				fclose(fp);
				throw strError;
			}
		}
		fclose(fp);
		m_bModified = false;
	} else {
		CString strError;
		strError.Format(_T("Failed to save IP filter to file \"%s\" - %s"), (LPCTSTR)strFilePath, _tcserror(errno));
		throw strError;
	}
}

bool CIPFilter::ParseFilterLine1(const CStringA &sbuffer, uint32 &ip1, uint32 &ip2, uint32 &level, CStringA &desc)
{
	IPFilterSeams::IPRange range;
	if (!IPFilterSeams::TryParseFilterDatLine(sbuffer, range))
		return false;

	ip1 = range.Start;
	ip2 = range.End;
	level = range.Level;
	desc = range.Description;
	return true;
}

bool CIPFilter::ParseFilterLine2(const CStringA &sbuffer, uint32 &ip1, uint32 &ip2, uint32 &level, CStringA &desc)
{
	IPFilterSeams::IPRange range;
	if (!IPFilterSeams::TryParsePeerGuardianLine(sbuffer, range))
		return false;

	ip1 = range.Start;
	ip2 = range.End;
	level = range.Level;
	desc = range.Description;
	return true;
}

void CIPFilter::RemoveAllIPFilters()
{
	for (INT_PTR i = m_iplist.GetCount(); --i >= 0;)
		delete m_iplist[i];
	m_iplist.RemoveAll();
	m_pLastHit = NULL;
}

bool CIPFilter::IsFiltered(uint32 ip) /*const*/
{
	return IsFiltered(ip, thePrefs.GetIPFilterLevel());
}

static int __cdecl CmpSIPFilterByAddr(const void *pvKey, const void *pvElement) noexcept
{
	uint32 ip = *(uint32*)pvKey;
	const SIPFilter *pIPFilter = *(SIPFilter**)pvElement;

	if (ip < pIPFilter->start)
		return -1;
	if (ip > pIPFilter->end)
		return 1;
	return 0;
}

bool CIPFilter::IsFiltered(uint32 ip, uint32 level) /*const*/
{
	if (!IPFilterSeams::ShouldEvaluateFilter(thePrefs.IsIPFilterEnabled(), static_cast<size_t>(m_iplist.GetCount()), ip))
		return false;

	ip = ntohl(ip);

	// to speed things up we use a binary search
	//	*)	the IP filter list must be sorted by IP range start addresses
	//	*)	the IP filter list is not allowed to contain overlapping IP ranges (see also the IP range merging code when
	//		loading the list)
	//	*)	the filter 'level' is ignored during the binary search and is evaluated only for the found element
	//
	// TODO: this can still be improved even more:
	//	*)	use a pre-assembled list of IP ranges which contains only the IP ranges for the currently used filter level
	//	*)	use a dumb plain array for storing the IP range structures. this will give more cache hits when processing
	//		the list. but(!) this would require to also use a dumb SIPFilter structure (don't use data items with ctors).
	//		otherwise the creation of the array would be rather slow.
	SIPFilter **ppFound = (SIPFilter**)bsearch(&ip, m_iplist.GetData(), m_iplist.GetCount(), sizeof m_iplist[0], CmpSIPFilterByAddr);
	if (ppFound && (*ppFound)->level < level) {
		(*ppFound)->hits++;
		m_pLastHit = *ppFound;
		return true;
	}

	return false;
}

CString CIPFilter::GetLastHit() const
{
	return CString(m_pLastHit ? m_pLastHit->desc : "Not available");
}

bool CIPFilter::RemoveIPFilter(const SIPFilter *pFilter)
{
	for (INT_PTR i = m_iplist.GetCount(); --i >= 0;)
		if (m_iplist[i] == pFilter) {
			delete m_iplist[i];
			m_iplist.RemoveAt(i);
			return true;
		}

	return false;
}
