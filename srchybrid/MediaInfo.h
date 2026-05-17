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
#include "MediaInfoDllSeams.h"
#include "MediaMetadataFallbackSeams.h"
#include "RichEditStream.h"

/////////////////////////////////////////////////////////////////////////////
// CStringStream

class CStringStream
{
public:
	CStringStream() = default;

	CStringStream& operator<<(LPCTSTR psz);
	CStringStream& operator<<(char *psz);
	CStringStream& operator<<(UINT uVal);
	CStringStream& operator<<(int iVal);
	CStringStream& operator<<(double fVal);

	bool IsEmpty() const
	{
		return str.IsEmpty();
	}
	void AppendFormat(LPCTSTR pszFmt, ...)
	{
		va_list argp;
		va_start(argp, pszFmt);
		str.AppendFormatV(pszFmt, argp);
		va_end(argp);
	}
	const CString& GetText() const
	{
		return str;
	}

protected:
	CString str;
};

// Core Windows multimedia structs used by the legacy `SMediaInfo` data model.
//#define MMNODRV		// mmsystem: Installable driver support
#define MMNOSOUND		// mmsystem: Sound support
//#define MMNOWAVE		// mmsystem: Waveform support
#define MMNOMIDI		// mmsystem: MIDI support
#define MMNOAUX			// mmsystem: Auxiliary audio support
#define MMNOMIXER		// mmsystem: Mixer support
#define MMNOTIMER		// mmsystem: Timer support
#define MMNOJOY			// mmsystem: Joystick support
#define MMNOMCI			// mmsystem: MCI support
//#define MMNOMMIO		// mmsystem: Multimedia file I/O support
#include <mmsystem.h>
#include <mmreg.h>

// Those defines are for 'mmreg.h' and 'vfw.h'
#define NOMMIDS		 // Multimedia IDs are not defined
//#define NONEWWAVE	   // No new waveform types are defined except WAVEFORMATEX
#define NONEWRIFF	 // No new RIFF forms are defined
#define NOJPEGDIB	 // No JPEG DIB definitions
#define NONEWIC		 // No new Image Compressor types are defined
#define NOBITMAP	 // No extended bitmap info header definition
// Those defines are for 'vfw.h'
//#define NOCOMPMAN
//#define NODRAWDIB
#define NOVIDEO
//#define NOAVIFMT
//#define NOMMREG
//#define NOAVIFILE
#define NOMCIWND
#define NOAVICAP
#define NOMSACM
#include <amvideo.h>
#include <strmif.h>
#include <vfw.h>
#ifndef FOURCC_RIFF
#define FOURCC_RIFF mmioFOURCC('R', 'I', 'F', 'F')
#endif
#ifndef FOURCC_LIST
#define FOURCC_LIST mmioFOURCC('L', 'I', 'S', 'T')
#endif


/////////////////////////////////////////////////////////////////////////////
// SMediaInfo

struct SMediaInfo
{
	SMediaInfo()
		: ulFileSize()
		, fFileLengthSec()
		, bFileLengthEstimated()
		, iVideoStreams()
		, video()
		, fVideoLengthSec()
		, bVideoLengthEstimated()
		, fVideoFrameRate()
		, fVideoAspectRatio()
		, iAudioStreams()
		, audio()
		, fAudioLengthSec()
		, bAudioLengthEstimated()
		, bOutputFileName(true)
	{
	}

	SMediaInfo& operator=(const SMediaInfo &strm)
	{
		strFileFormat = strm.strFileFormat;
		strMimeType = strm.strMimeType;
		ulFileSize = strm.ulFileSize;
		fFileLengthSec = strm.fFileLengthSec;
		bFileLengthEstimated = strm.bFileLengthEstimated;
		strTitle = strm.strTitle;
		strAuthor = strm.strAuthor;
		strAlbum = strm.strAlbum;

		iVideoStreams = strm.iVideoStreams;
		strVideoFormat = strm.strVideoFormat;
		video = strm.video;
		fVideoLengthSec = strm.fVideoLengthSec;
		bVideoLengthEstimated = strm.bVideoLengthEstimated;
		fVideoFrameRate = strm.fVideoFrameRate;
		fVideoAspectRatio = strm.fVideoAspectRatio;

		iAudioStreams = strm.iAudioStreams;
		strAudioFormat = strm.strAudioFormat;
		audio = strm.audio;
		fAudioLengthSec = strm.fAudioLengthSec;
		bAudioLengthEstimated = strm.bAudioLengthEstimated;
		strAudioLanguage = strm.strAudioLanguage;
		strFileName = strm.strFileName;
		bOutputFileName = strm.bOutputFileName;
		return *this;
	}

	SMediaInfo(const SMediaInfo &strm)
	{
		*this = strm;
	}

	void OutputFileName()
	{
		if (bOutputFileName) {
			bOutputFileName = false;
			if (!strInfo.IsEmpty())
				strInfo << _T("\n");
			strInfo.SetSelectionCharFormat(strInfo.m_cfBold);
			strInfo << GetResString(IDS_FILE) << _T(": ") << strFileName << _T("\n");
		}
	}

	void InitFileLength()
	{
		if (fFileLengthSec == 0) {
			if (fVideoLengthSec > 0.0) {
				fFileLengthSec = fVideoLengthSec;
				bFileLengthEstimated = bVideoLengthEstimated;
			} else if (fAudioLengthSec > 0.0) {
				fFileLengthSec = fAudioLengthSec;
				bFileLengthEstimated = bAudioLengthEstimated;
			}
		}
	}

	CString			strFileName;
	CString			strFileFormat;
	CString			strMimeType;
	EMFileSize		ulFileSize;
	double			fFileLengthSec;
	bool			bFileLengthEstimated;
	CString			strTitle;
	CString			strAuthor;
	CString			strAlbum;

	int				iVideoStreams;
	CString			strVideoFormat;
	VIDEOINFOHEADER	video;
	double			fVideoLengthSec;
	bool			bVideoLengthEstimated;
	double			fVideoFrameRate;
	double			fVideoAspectRatio;

	int				iAudioStreams;
	CString			strAudioFormat;
	WAVEFORMAT		audio;
	double			fAudioLengthSec;
	bool			bAudioLengthEstimated;
	CString			strAudioLanguage;

	bool			bOutputFileName;
	CRichEditStream	strInfo;
};


bool GetMimeType(LPCTSTR pszFilePath, CString &rstrMimeType);
bool GetDRM(LPCTSTR pszFilePath);
/**
 * @brief Extracts audio/video metadata through the optional MediaInfo DLL when it is available.
 */
bool GetMediaInfoDllInfo(LPCTSTR pszFilePath, EMFileSize ullFileSize, SMediaInfo *mi, bool bFullInfo = false, bool bSingleFile = true, bool *pbLibraryAvailable = NULL, MediaInfoDllSeams::EMediaInfoDllStatus *peLibraryStatus = NULL);
/**
 * @brief Extracts basic media metadata through maintained fallback providers when MediaInfo.dll is unavailable.
 */
bool GetFallbackMediaInfo(LPCTSTR pszFilePath, EMFileSize ullFileSize, SMediaInfo *mi, bool bFullInfo = false, bool bSingleFile = true);
/**
 * @brief Extracts basic metadata through Windows Media Foundation without decoding samples.
 */
bool GetMediaFoundationMediaInfo(LPCTSTR pszFilePath, EMFileSize ullFileSize, SMediaInfo *mi);
/**
 * @brief Extracts basic metadata through eMule BB-owned MP4 and EBML parsers.
 */
bool GetOwnedContainerMediaInfo(LPCTSTR pszFilePath, EMFileSize ullFileSize, SMediaInfo *mi);
/**
 * @brief Supplements descriptive metadata from Windows Shell properties without replacing technical parser facts.
 */
bool SupplementShellMediaProperties(LPCTSTR pszFilePath, SMediaInfo *mi);
/**
 * @brief Runs each maintained metadata extractor independently for redacted diagnostics and live corpus comparison.
 */
std::vector<MediaMetadataFallbackSeams::SVariantSummary> ProbeMediaMetadataVariants(LPCTSTR pszFilePath, EMFileSize ullFileSize);
/**
 * @brief Returns the preferred visible MediaInfo DLL preference path for the current machine.
 */
CString ResolveMediaInfoDllPreferencePath(const CString &strConfiguredPath);
bool GetRIFFHeaders(LPCTSTR pszFileName, SMediaInfo *mi, bool &rbIsAVI, bool bFullInfo = false);
bool GetRMHeaders(LPCTSTR pszFileName, SMediaInfo *mi, bool &rbIsRM, bool bFullInfo = false);
#ifdef HAVE_WMSDK_H
bool GetWMHeaders(LPCTSTR pszFileName, SMediaInfo *mi, bool &rbIsWM, bool bFullInfo = false);
#endif//HAVE_WMSDK_H
CString GetAudioFormatName(WORD wFormatTag, CString &rstrComment);
CString GetAudioFormatName(WORD wFormatTag);
CString GetAudioFormatCodecId(WORD wFormatTag);
bool IsEqualFOURCC(FOURCC fccA, FOURCC fccB);
CString GetVideoFormatName(DWORD biCompression);
CString GetKnownAspectRatioDisplayString(float fAspectRatio);
CString GetCodecDisplayName(const CString &strCodecId);
