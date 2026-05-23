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
#include <io.h>
#include <share.h>
#include "PerfLog.h"
#include "ini2.h"
#include "Opcodes.h"
#include "Preferences.h"
#include "Statistics.h"
#include "Log.h"
#include "LogArtifactNames.h"
#include "PerfLogSeams.h"
#include "PreferenceUiSeams.h"
#include "otherfunctions.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


CPerfLog thePerfLog;

namespace
{
CString GetDefaultPerfLogFilePath(int iFileFormat)
{
	CString strDefFilePath(thePrefs.GetMuleDirectory(EMULE_CONFIGBASEDIR));
	strDefFilePath += (PreferenceUiSeams::NormalizePerfLogFileFormat(iFileFormat) == 0)
		? LogArtifactNames::PerformanceCsvFileName()
		: LogArtifactNames::PerformanceMrtgFileName();
	return strDefFilePath;
}

int NormalizePerfLogMode(int iMode)
{
	return (iMode == CPerfLog::OneSample || iMode == CPerfLog::AllSamples) ? iMode : CPerfLog::None;
}
}

CPerfLog::CPerfLog()
	: m_dwInterval(MIN2MS(5))
	, m_dwLastSampled()
	, m_nLastSessionSentBytes()
	, m_nLastSessionRecvBytes()
	, m_nLastDnOH()
	, m_nLastUpOH()
	, m_eMode(None)
	, m_eFileFormat(CSV)
	, m_bInitialized()
{
}

bool CPerfLog::IsEnabled() const
{
	return m_eMode != None;
}

void CPerfLog::Startup()
{
	if (m_bInitialized)
		return;

	ReloadSettings();
}

void CPerfLog::LoadSettings()
{
	CIni ini(thePrefs.GetConfigFile(), _T("PerfLog"));

	m_eMode = static_cast<ELogMode>(NormalizePerfLogMode(ini.GetInt(_T("Mode"), None)));
	m_eFileFormat = CSV;
	m_dwInterval = MIN2MS(5);
	m_strFilePath.Empty();
	m_strMRTGDataFilePath.Empty();
	m_strMRTGOverheadFilePath.Empty();
	if (m_eMode != None) {
		m_eFileFormat = static_cast<ELogFileFormat>(PreferenceUiSeams::NormalizePerfLogFileFormat(ini.GetInt(_T("FileFormat"), CSV)));

		const CString strDefFilePath(GetDefaultPerfLogFilePath(m_eFileFormat));
		m_strFilePath = ini.GetString(_T("File"), strDefFilePath);
		if (m_strFilePath.IsEmpty())
			m_strFilePath = strDefFilePath;

		if (m_eFileFormat == MRTG) {
			m_strMRTGDataFilePath = PerfLogSeams::BuildMrtgSidecarPath(m_strFilePath, LogArtifactNames::PerformanceMrtgDataSuffix());
			m_strMRTGOverheadFilePath = PerfLogSeams::BuildMrtgSidecarPath(m_strFilePath, LogArtifactNames::PerformanceMrtgOverheadSuffix());
			m_strFilePath.Empty();
		}

		m_dwInterval = MIN2MS(PreferenceUiSeams::NormalizePerfLogIntervalMinutes(ini.GetInt(_T("Interval"), 5)));
	}
}

void CPerfLog::ResetSampleBaseline()
{
	m_nLastSessionRecvBytes = theStats.sessionReceivedBytes;
	m_nLastSessionSentBytes = theStats.sessionSentBytes;
	m_nLastDnOH = theStats.GetDownDataOverheadFileRequest()
				+ theStats.GetDownDataOverheadSourceExchange()
				+ theStats.GetDownDataOverheadServer()
				+ theStats.GetDownDataOverheadKad()
				+ theStats.GetDownDataOverheadOther();
	m_nLastUpOH = theStats.GetUpDataOverheadFileRequest()
				+ theStats.GetUpDataOverheadSourceExchange()
				+ theStats.GetUpDataOverheadServer()
				+ theStats.GetUpDataOverheadKad()
				+ theStats.GetUpDataOverheadOther();
	m_dwLastSampled = ::GetTickCount64();
}

void CPerfLog::ReloadSettings()
{
	const bool bWasInitialized = m_bInitialized;
	const ELogMode ePreviousMode = m_eMode;

	LoadSettings();
	m_bInitialized = true;

	if (m_eMode == None)
		return;

	if (!bWasInitialized || ePreviousMode == None) {
		if (m_eMode == OneSample) {
			m_nLastSessionRecvBytes = 0;
			m_nLastSessionSentBytes = 0;
			m_nLastDnOH = 0;
			m_nLastUpOH = 0;
			m_dwLastSampled = 0;
		} else {
			ResetSampleBaseline();
		}
	}

	if ((m_eMode == OneSample) && (!bWasInitialized || ePreviousMode == None))
		LogSamples();
}

void CPerfLog::SetEnabled(bool bEnable)
{
	SetSettings(bEnable, GetConfiguredFileFormat(), GetConfiguredFilePath(), GetConfiguredIntervalMinutes());
}

void CPerfLog::SetSettings(bool bEnable, int iFileFormat, const CString &strFilePath, UINT uIntervalMinutes)
{
	CIni ini(thePrefs.GetConfigFile(), _T("PerfLog"));
	const int iCurrentMode = NormalizePerfLogMode(ini.GetInt(_T("Mode"), None));

	const int iNewMode = bEnable ? ((iCurrentMode != None) ? iCurrentMode : static_cast<int>(AllSamples)) : static_cast<int>(None);
	if (iCurrentMode != iNewMode)
		ini.WriteInt(_T("Mode"), iNewMode);

	CString strNormalizedPath(strFilePath);
	strNormalizedPath.Trim();
	ini.WriteInt(_T("FileFormat"), PreferenceUiSeams::NormalizePerfLogFileFormat(iFileFormat));
	ini.WriteString(_T("File"), strNormalizedPath);
	ini.WriteInt(_T("Interval"), static_cast<int>(PreferenceUiSeams::NormalizePerfLogIntervalMinutes(uIntervalMinutes)));

	ReloadSettings();
}

int CPerfLog::GetConfiguredFileFormat()
{
	CIni ini(thePrefs.GetConfigFile(), _T("PerfLog"));
	return PreferenceUiSeams::NormalizePerfLogFileFormat(ini.GetInt(_T("FileFormat"), CSV));
}

CString CPerfLog::GetConfiguredFilePath()
{
	const int iFileFormat = GetConfiguredFileFormat();
	CIni ini(thePrefs.GetConfigFile(), _T("PerfLog"));
	CString strFilePath(ini.GetString(_T("File"), GetDefaultPerfLogFilePath(iFileFormat)));
	strFilePath.Trim();
	return strFilePath.IsEmpty() ? GetDefaultPerfLogFilePath(iFileFormat) : strFilePath;
}

UINT CPerfLog::GetConfiguredIntervalMinutes()
{
	CIni ini(thePrefs.GetConfigFile(), _T("PerfLog"));
	return PreferenceUiSeams::NormalizePerfLogIntervalMinutes(ini.GetInt(_T("Interval"), 5));
}

void CPerfLog::WriteSamples(UINT nCurDn, UINT nCurUp, UINT nCurDnOH, UINT nCurUpOH)
{
	ASSERT(m_bInitialized);

	if (m_eFileFormat == CSV) {
		time_t tNow = time(NULL);
		char szTime[40];
		tm tmNow = {};
		// do not localize this date/time string!
		if (localtime_s(&tmNow, &tNow) == 0)
			strftime(szTime, _countof(szTime), "%m/%d/%Y %H:%M:%S", &tmNow);
		else
			szTime[0] = '\0';

		FILE *fp = LongPathSeams::OpenFileStreamDenyWriteLongPath(m_strFilePath, (m_eMode == OneSample) ? _T("wt") : _T("at"));
		if (fp == NULL) {
			LogError(LOG_DEFAULT, _T("Failed to open performance log file \"%s\" - %s"), (LPCTSTR)m_strFilePath, (LPCTSTR)GetCrtErrorString(errno));
			return;
		}
		::setvbuf(fp, NULL, _IOFBF, 16384); // ensure that all lines are written to file with one call
		if (m_eMode == OneSample || _filelength(_fileno(fp)) == 0)
			fprintf(fp, "\"(PDH-CSV 4.0)\",\"DatDown\",\"DatUp\",\"OvrDown\",\"OvrUp\"\n");
		fprintf(fp, "\"%s\",\"%u\",\"%u\",\"%u\",\"%u\"\n", szTime, nCurDn, nCurUp, nCurDnOH, nCurUpOH);
		fclose(fp);
	} else {
		ASSERT(m_eFileFormat == MRTG);

		FILE *fp = LongPathSeams::OpenFileStreamDenyWriteLongPath(m_strMRTGDataFilePath, (m_eMode == OneSample) ? _T("wt") : _T("at"));
		if (fp != NULL) {
			fprintf(fp, "%u\n%u\n\n\n", nCurDn, nCurUp);
			fclose(fp);
		} else
			LogError(LOG_DEFAULT, _T("Failed to open performance log file \"%s\" - %s"), (LPCTSTR)m_strMRTGDataFilePath, (LPCTSTR)GetCrtErrorString(errno));

		fp = LongPathSeams::OpenFileStreamDenyWriteLongPath(m_strMRTGOverheadFilePath, (m_eMode == OneSample) ? _T("wt") : _T("at"));
		if (fp != NULL) {
			fprintf(fp, "%u\n%u\n\n\n", nCurDnOH, nCurUpOH);
			fclose(fp);
		} else
			LogError(LOG_DEFAULT, _T("Failed to open performance log file \"%s\" - %s"), (LPCTSTR)m_strMRTGOverheadFilePath, (LPCTSTR)GetCrtErrorString(errno));
	}
}

void CPerfLog::LogSamples()
{
	if (m_eMode == None)
		return;

	const ULONGLONG curTick = ::GetTickCount64();
	if (curTick < m_dwLastSampled + static_cast<ULONGLONG>(m_dwInterval))
		return;

	// 'data counters' amount of transferred file data
	UINT nCurDn = (UINT)(theStats.sessionReceivedBytes - m_nLastSessionRecvBytes);
	UINT nCurUp = (UINT)(theStats.sessionSentBytes - m_nLastSessionSentBytes);

	// 'overhead counters' amount of total overhead
	uint64 nDnOHTotal = theStats.GetDownDataOverheadFileRequest()
					+ theStats.GetDownDataOverheadSourceExchange()
					+ theStats.GetDownDataOverheadServer()
					+ theStats.GetDownDataOverheadKad()
					+ theStats.GetDownDataOverheadOther();
	uint64 nUpOHTotal = theStats.GetUpDataOverheadFileRequest()
					+ theStats.GetUpDataOverheadSourceExchange()
					+ theStats.GetUpDataOverheadServer()
					+ theStats.GetUpDataOverheadKad()
					+ theStats.GetUpDataOverheadOther();
	UINT nCurDnOH = (UINT)(nDnOHTotal - m_nLastDnOH);
	UINT nCurUpOH = (UINT)(nUpOHTotal - m_nLastUpOH);

	WriteSamples(nCurDn, nCurUp, nCurDnOH, nCurUpOH);

	m_nLastSessionRecvBytes = theStats.sessionReceivedBytes;
	m_nLastSessionSentBytes = theStats.sessionSentBytes;
	m_nLastDnOH = nDnOHTotal;
	m_nLastUpOH = nUpOHTotal;
	m_dwLastSampled = curTick;
}

void CPerfLog::Shutdown()
{
	if (m_eMode == OneSample)
		WriteSamples(0, 0, 0, 0);
}
