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
#include "ComInitializationSeams.h"
#include "KnownFileMetadataSeams.h"
#include "LongPathSeams.h"
#include "OtherFunctions.h"
#include "resource.h"
#include "MediaInfo.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <io.h>
#include <propkey.h>
#include <propsys.h>
#include <propvarutil.h>
#include <shobjidl.h>
#include <vector>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
class CScopedMfStartup
{
public:
	CScopedMfStartup()
		: m_hr(::MFStartup(MF_VERSION, MFSTARTUP_LITE))
	{
	}

	~CScopedMfStartup()
	{
		if (SUCCEEDED(m_hr))
			::MFShutdown();
	}

	bool IsUsable() const
	{
		return SUCCEEDED(m_hr);
	}

private:
	HRESULT m_hr;
};

CString GuidToCodecName(const GUID &rGuid)
{
	if (rGuid.Data2 == 0x0000 && rGuid.Data3 == 0x0010 && rGuid.Data4[0] == 0x80 && rGuid.Data4[1] == 0x00) {
		CStringA strFourCc(reinterpret_cast<LPCSTR>(&rGuid.Data1), 4);
		strFourCc.Trim();
		if (!strFourCc.IsEmpty())
			return CString(strFourCc);
	}
	WCHAR wszGuid[64] = {};
	if (::StringFromGUID2(rGuid, wszGuid, _countof(wszGuid)) > 0)
		return CString(wszGuid);
	return CString();
}

void ApplyBasicInfoToMediaInfo(const MediaMetadataFallbackSeams::SBasicMediaInfo &rBasic, SMediaInfo *mi)
{
	if (mi == NULL)
		return;
	if (!rBasic.strContainer.IsEmpty() && mi->strFileFormat.IsEmpty())
		mi->strFileFormat = rBasic.strContainer;
	if (rBasic.fDurationSec > 0.0) {
		if (mi->fFileLengthSec <= 0.0)
			mi->fFileLengthSec = rBasic.fDurationSec;
		if (mi->fVideoLengthSec <= 0.0 && rBasic.iVideoStreams > 0)
			mi->fVideoLengthSec = rBasic.fDurationSec;
		if (mi->fAudioLengthSec <= 0.0 && rBasic.iAudioStreams > 0)
			mi->fAudioLengthSec = rBasic.fDurationSec;
	}
	if (rBasic.iVideoStreams > 0) {
		mi->iVideoStreams = max(mi->iVideoStreams, rBasic.iVideoStreams);
		if (!rBasic.strVideoCodec.IsEmpty() && mi->strVideoFormat.IsEmpty())
			mi->strVideoFormat = rBasic.strVideoCodec;
		if (rBasic.uWidth != 0)
			mi->video.bmiHeader.biWidth = static_cast<LONG>(rBasic.uWidth);
		if (rBasic.uHeight != 0)
			mi->video.bmiHeader.biHeight = static_cast<LONG>(rBasic.uHeight);
		if (rBasic.uVideoBitrate != 0)
			mi->video.dwBitRate = rBasic.uVideoBitrate;
		if (rBasic.uWidth != 0 && rBasic.uHeight != 0)
			mi->fVideoAspectRatio = static_cast<double>(rBasic.uWidth) / static_cast<double>(rBasic.uHeight);
	}
	if (rBasic.iAudioStreams > 0) {
		mi->iAudioStreams = max(mi->iAudioStreams, rBasic.iAudioStreams);
		if (!rBasic.strAudioCodec.IsEmpty() && mi->strAudioFormat.IsEmpty())
			mi->strAudioFormat = rBasic.strAudioCodec;
		if (rBasic.uAudioBitrate != 0)
			mi->audio.nAvgBytesPerSec = rBasic.uAudioBitrate / 8;
		if (rBasic.uAudioChannels != 0)
			mi->audio.nChannels = static_cast<WORD>(rBasic.uAudioChannels);
		if (rBasic.uAudioSampleRate != 0)
			mi->audio.nSamplesPerSec = rBasic.uAudioSampleRate;
	}
	mi->InitFileLength();
}

MediaMetadataFallbackSeams::SBasicMediaInfo SnapshotFromMediaInfo(const SMediaInfo &rMediaInfo)
{
	MediaMetadataFallbackSeams::SBasicMediaInfo info;
	info.strContainer = rMediaInfo.strFileFormat;
	info.strVideoCodec = rMediaInfo.strVideoFormat;
	info.strAudioCodec = rMediaInfo.strAudioFormat;
	info.fDurationSec = rMediaInfo.fFileLengthSec > 0.0 ? rMediaInfo.fFileLengthSec : max(rMediaInfo.fVideoLengthSec, rMediaInfo.fAudioLengthSec);
	info.uWidth = rMediaInfo.video.bmiHeader.biWidth > 0 ? static_cast<std::uint32_t>(rMediaInfo.video.bmiHeader.biWidth) : 0;
	info.uHeight = rMediaInfo.video.bmiHeader.biHeight > 0 ? static_cast<std::uint32_t>(rMediaInfo.video.bmiHeader.biHeight) : 0;
	info.uVideoBitrate = rMediaInfo.video.dwBitRate != _UI32_MAX ? rMediaInfo.video.dwBitRate : 0;
	info.uAudioBitrate = rMediaInfo.audio.nAvgBytesPerSec != _UI32_MAX ? rMediaInfo.audio.nAvgBytesPerSec * 8 : 0;
	info.uAudioChannels = rMediaInfo.audio.nChannels;
	info.uAudioSampleRate = rMediaInfo.audio.nSamplesPerSec;
	info.iVideoStreams = rMediaInfo.iVideoStreams;
	info.iAudioStreams = rMediaInfo.iAudioStreams;
	return info;
}

bool HasUsableMediaFacts(const SMediaInfo &rMediaInfo)
{
	return rMediaInfo.fFileLengthSec > 0.0
		|| rMediaInfo.fVideoLengthSec > 0.0
		|| rMediaInfo.fAudioLengthSec > 0.0
		|| rMediaInfo.iVideoStreams > 0
		|| rMediaInfo.iAudioStreams > 0
		|| !rMediaInfo.strTitle.IsEmpty()
		|| !rMediaInfo.strAuthor.IsEmpty()
		|| !rMediaInfo.strAlbum.IsEmpty();
}

void SetPropVariantString(PROPVARIANT &rValue, CString &rTarget)
{
	WCHAR wszValue[512] = {};
	if (rTarget.IsEmpty() && SUCCEEDED(::PropVariantToString(rValue, wszValue, _countof(wszValue))))
		rTarget = wszValue;
}

bool LoadProbeWindow(LPCTSTR pszFilePath, std::vector<std::uint8_t> &raBytes)
{
	raBytes.clear();
	const int hFile = KnownFileMetadataSeams::OpenMetadataReadOnlyDescriptor(pszFilePath);
	if (hFile < 0)
		return false;
	__int64 iFileSize = _filelengthi64(hFile);
	if (iFileSize <= 0) {
		_close(hFile);
		return false;
	}
	constexpr std::size_t uWindowBytes = 8u * 1024u * 1024u;
	const std::size_t uHeadBytes = static_cast<std::size_t>(iFileSize < static_cast<__int64>(uWindowBytes) ? iFileSize : static_cast<__int64>(uWindowBytes));
	raBytes.resize(uHeadBytes);
	int iRead = _read(hFile, raBytes.data(), static_cast<unsigned int>(uHeadBytes));
	if (iRead < 0) {
		_close(hFile);
		raBytes.clear();
		return false;
	}
	raBytes.resize(static_cast<std::size_t>(iRead));
	if (iFileSize > static_cast<__int64>(uWindowBytes * 2u) && _lseeki64(hFile, iFileSize - uWindowBytes, SEEK_SET) >= 0) {
		const std::size_t uOldSize = raBytes.size();
		raBytes.resize(uOldSize + uWindowBytes);
		iRead = _read(hFile, raBytes.data() + uOldSize, static_cast<unsigned int>(uWindowBytes));
		if (iRead > 0)
			raBytes.resize(uOldSize + static_cast<std::size_t>(iRead));
		else
			raBytes.resize(uOldSize);
	}
	_close(hFile);
	return !raBytes.empty();
}

void AppendFullInfoSummary(SMediaInfo *mi, LPCTSTR pszProviderName)
{
	if (mi == NULL || mi->strInfo.m_hWnd == NULL)
		return;
	if (!mi->strInfo.IsEmpty())
		mi->strInfo << _T("\r\n");
	mi->OutputFileName();
	mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfBold);
	mi->strInfo << pszProviderName << _T("\r\n");
	mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfDef);
	if (!mi->strFileFormat.IsEmpty())
		mi->strInfo << _T("   Format:\t") << mi->strFileFormat << _T("\r\n");
	if (mi->fFileLengthSec > 0.0)
		mi->strInfo << _T("   ") << GetResString(IDS_LENGTH) << _T(":\t") << SecToTimeLength(static_cast<UINT>(mi->fFileLengthSec)) << _T("\r\n");
	if (mi->iVideoStreams > 0) {
		mi->strInfo << _T("   ") << GetResString(IDS_VIDEO) << _T(":\t");
		if (!mi->strVideoFormat.IsEmpty())
			mi->strInfo << mi->strVideoFormat << _T(" ");
		if (mi->video.bmiHeader.biWidth != 0 && mi->video.bmiHeader.biHeight != 0)
			mi->strInfo << abs(mi->video.bmiHeader.biWidth) << _T(" x ") << abs(mi->video.bmiHeader.biHeight);
		mi->strInfo << _T("\r\n");
	}
	if (mi->iAudioStreams > 0) {
		mi->strInfo << _T("   ") << GetResString(IDS_AUDIO) << _T(":\t");
		if (!mi->strAudioFormat.IsEmpty())
			mi->strInfo << mi->strAudioFormat << _T(" ");
		if (mi->audio.nChannels != 0)
			mi->strInfo << mi->audio.nChannels << _T(" ch ");
		if (mi->audio.nSamplesPerSec != 0)
			mi->strInfo << mi->audio.nSamplesPerSec / 1000.0 << _T(" kHz");
		mi->strInfo << _T("\r\n");
	}
}

MediaMetadataFallbackSeams::SVariantSummary BuildVariantSummary(LPCTSTR pszName, bool (*pfnProbe)(LPCTSTR, EMFileSize, SMediaInfo*), LPCTSTR pszFilePath, EMFileSize ullFileSize)
{
	MediaMetadataFallbackSeams::SVariantSummary summary;
	summary.strName = pszName;
	SMediaInfo mediaInfo;
	try {
		summary.bSucceeded = pfnProbe(pszFilePath, ullFileSize, &mediaInfo);
		summary.strStatus = summary.bSucceeded ? _T("ok") : _T("no_metadata");
		summary.info = SnapshotFromMediaInfo(mediaInfo);
	} catch (...) {
		summary.bSucceeded = false;
		summary.strStatus = _T("exception");
	}
	return summary;
}

bool ProbeMediaInfoDllOnly(LPCTSTR pszFilePath, EMFileSize ullFileSize, SMediaInfo *mi)
{
	return GetMediaInfoDllInfo(pszFilePath, ullFileSize, mi, false, true);
}

bool ProbeOwnedOnly(LPCTSTR pszFilePath, EMFileSize ullFileSize, SMediaInfo *mi)
{
	return GetOwnedContainerMediaInfo(pszFilePath, ullFileSize, mi);
}

bool ProbeShellOnly(LPCTSTR pszFilePath, EMFileSize, SMediaInfo *mi)
{
	return SupplementShellMediaProperties(pszFilePath, mi) && HasUsableMediaFacts(*mi);
}
}

bool GetMediaFoundationMediaInfo(LPCTSTR pszFilePath, EMFileSize, SMediaInfo *mi)
{
	if (pszFilePath == NULL || *pszFilePath == _T('\0') || mi == NULL)
		return false;

	ComInitializationSeams::CScopedComInitialize coInit(COINIT_MULTITHREADED);
	if (!coInit.IsUsable())
		return false;
	CScopedMfStartup mfStartup;
	if (!mfStartup.IsUsable())
		return false;

	CComPtr<IMFSourceReader> pReader;
	const CString strPreparedPath(PreparePathForLongPath(CString(pszFilePath)));
	if (FAILED(::MFCreateSourceReaderFromURL(strPreparedPath, NULL, &pReader)) || pReader == NULL)
		return false;

	MediaMetadataFallbackSeams::SBasicMediaInfo basic;
	basic.strContainer = _T("Media Foundation");

	PROPVARIANT var;
	::PropVariantInit(&var);
	if (SUCCEEDED(pReader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var)) && var.vt == VT_UI8 && var.uhVal.QuadPart != 0)
		basic.fDurationSec = static_cast<double>(var.uhVal.QuadPart) / 10000000.0;
	::PropVariantClear(&var);

	for (DWORD uStream = 0; uStream < 128; ++uStream) {
		CComPtr<IMFMediaType> pType;
		const HRESULT hrType = pReader->GetNativeMediaType(uStream, 0, &pType);
		if (hrType == MF_E_INVALIDSTREAMNUMBER)
			break;
		if (FAILED(hrType) || pType == NULL)
			continue;
		GUID majorType = GUID_NULL;
		if (FAILED(pType->GetGUID(MF_MT_MAJOR_TYPE, &majorType)))
			continue;
		GUID subtype = GUID_NULL;
		(void)pType->GetGUID(MF_MT_SUBTYPE, &subtype);
		if (majorType == MFMediaType_Video) {
			++basic.iVideoStreams;
			if (basic.strVideoCodec.IsEmpty())
				basic.strVideoCodec = GuidToCodecName(subtype);
			UINT32 uWidth = 0;
			UINT32 uHeight = 0;
			if (SUCCEEDED(::MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &uWidth, &uHeight))) {
				basic.uWidth = basic.uWidth == 0 ? uWidth : basic.uWidth;
				basic.uHeight = basic.uHeight == 0 ? uHeight : basic.uHeight;
			}
			UINT32 uNumerator = 0;
			UINT32 uDenominator = 0;
			if (SUCCEEDED(::MFGetAttributeRatio(pType, MF_MT_FRAME_RATE, &uNumerator, &uDenominator)) && uDenominator != 0)
				mi->fVideoFrameRate = static_cast<double>(uNumerator) / static_cast<double>(uDenominator);
			UINT32 uBitrate = 0;
			if (SUCCEEDED(pType->GetUINT32(MF_MT_AVG_BITRATE, &uBitrate)))
				basic.uVideoBitrate = uBitrate;
		} else if (majorType == MFMediaType_Audio) {
			++basic.iAudioStreams;
			if (basic.strAudioCodec.IsEmpty())
				basic.strAudioCodec = GuidToCodecName(subtype);
			UINT32 uValue = 0;
			if (SUCCEEDED(pType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &uValue)))
				basic.uAudioChannels = uValue;
			if (SUCCEEDED(pType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &uValue)))
				basic.uAudioSampleRate = uValue;
			if (SUCCEEDED(pType->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &uValue)))
				basic.uAudioBitrate = uValue * 8;
		}
	}

	ApplyBasicInfoToMediaInfo(basic, mi);
	return HasUsableMediaFacts(*mi);
}

bool GetOwnedContainerMediaInfo(LPCTSTR pszFilePath, EMFileSize, SMediaInfo *mi)
{
	if (pszFilePath == NULL || *pszFilePath == _T('\0') || mi == NULL)
		return false;
	std::vector<std::uint8_t> aBytes;
	if (!LoadProbeWindow(pszFilePath, aBytes))
		return false;
	MediaMetadataFallbackSeams::SBasicMediaInfo basic;
	LPCTSTR pszExtension = ::PathFindExtension(pszFilePath);
	CString strExtension(&pszExtension[static_cast<int>(*pszExtension != _T('\0'))]);
	strExtension.MakeLower();
	bool bParsed = false;
	if (strExtension == _T("mp4") || strExtension == _T("m4v") || strExtension == _T("mov") || strExtension == _T("m4a"))
		bParsed = MediaMetadataFallbackSeams::TryReadMp4Basics(aBytes.data(), aBytes.size(), basic);
	else if (strExtension == _T("mkv") || strExtension == _T("webm"))
		bParsed = MediaMetadataFallbackSeams::TryReadEbmlBasics(aBytes.data(), aBytes.size(), basic);
	if (!bParsed)
		return false;
	ApplyBasicInfoToMediaInfo(basic, mi);
	return HasUsableMediaFacts(*mi);
}

bool SupplementShellMediaProperties(LPCTSTR pszFilePath, SMediaInfo *mi)
{
	if (pszFilePath == NULL || *pszFilePath == _T('\0') || mi == NULL)
		return false;
	ComInitializationSeams::CScopedComInitialize coInit(COINIT_MULTITHREADED);
	if (!coInit.IsUsable())
		return false;

	CComPtr<IPropertyStore> pStore;
	const CString strPreparedPath(PreparePathForLongPath(CString(pszFilePath)));
	if (FAILED(::SHGetPropertyStoreFromParsingName(strPreparedPath, NULL, GPS_BESTEFFORT, IID_PPV_ARGS(&pStore))) || pStore == NULL)
		return false;

	bool bChanged = false;
	PROPVARIANT var;
	::PropVariantInit(&var);
	if (SUCCEEDED(pStore->GetValue(PKEY_Title, &var))) {
		const bool bWasEmpty = mi->strTitle.IsEmpty();
		SetPropVariantString(var, mi->strTitle);
		bChanged |= bWasEmpty && !mi->strTitle.IsEmpty();
	}
	::PropVariantClear(&var);
	::PropVariantInit(&var);
	if (SUCCEEDED(pStore->GetValue(PKEY_Music_Artist, &var))) {
		const bool bWasEmpty = mi->strAuthor.IsEmpty();
		SetPropVariantString(var, mi->strAuthor);
		bChanged |= bWasEmpty && !mi->strAuthor.IsEmpty();
	}
	::PropVariantClear(&var);
	::PropVariantInit(&var);
	if (SUCCEEDED(pStore->GetValue(PKEY_Music_AlbumTitle, &var))) {
		const bool bWasEmpty = mi->strAlbum.IsEmpty();
		SetPropVariantString(var, mi->strAlbum);
		bChanged |= bWasEmpty && !mi->strAlbum.IsEmpty();
	}
	::PropVariantClear(&var);
	::PropVariantInit(&var);
	if (mi->fFileLengthSec <= 0.0 && SUCCEEDED(pStore->GetValue(PKEY_Media_Duration, &var)) && var.vt == VT_UI8 && var.uhVal.QuadPart != 0) {
		mi->fFileLengthSec = static_cast<double>(var.uhVal.QuadPart) / 10000000.0;
		bChanged = true;
	}
	::PropVariantClear(&var);
	mi->InitFileLength();
	return bChanged;
}

bool GetFallbackMediaInfo(LPCTSTR pszFilePath, EMFileSize ullFileSize, SMediaInfo *mi, bool bFullInfo, bool)
{
	if (pszFilePath == NULL || *pszFilePath == _T('\0') || mi == NULL)
		return false;

	bool bFoundHeader = false;
	try {
		bFoundHeader = GetMediaFoundationMediaInfo(pszFilePath, ullFileSize, mi);
	} catch (...) {
		ASSERT(0);
	}
	if (!bFoundHeader) {
		try {
			bFoundHeader = GetOwnedContainerMediaInfo(pszFilePath, ullFileSize, mi);
		} catch (...) {
			ASSERT(0);
		}
	}
	try {
		(void)SupplementShellMediaProperties(pszFilePath, mi);
	} catch (...) {
		ASSERT(0);
	}
	if (bFullInfo && HasUsableMediaFacts(*mi))
		AppendFullInfoSummary(mi, _T("eMule BB fallback metadata"));
	return bFoundHeader || HasUsableMediaFacts(*mi);
}

std::vector<MediaMetadataFallbackSeams::SVariantSummary> ProbeMediaMetadataVariants(LPCTSTR pszFilePath, EMFileSize ullFileSize)
{
	std::vector<MediaMetadataFallbackSeams::SVariantSummary> variants;
	variants.push_back(BuildVariantSummary(_T("MediaInfo.dll"), ProbeMediaInfoDllOnly, pszFilePath, ullFileSize));
	variants.push_back(BuildVariantSummary(_T("Media Foundation"), GetMediaFoundationMediaInfo, pszFilePath, ullFileSize));
	variants.push_back(BuildVariantSummary(_T("Owned container parser"), ProbeOwnedOnly, pszFilePath, ullFileSize));
	variants.push_back(BuildVariantSummary(_T("Shell properties"), ProbeShellOnly, pszFilePath, ullFileSize));
	return variants;
}
