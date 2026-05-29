#pragma once

#include <atlstr.h>
#include <cstdint>
#include <cstring>
#include <Shlwapi.h>
#include <tchar.h>

#ifndef EMULE_EFILETYPE_DEFINED
#define EMULE_EFILETYPE_DEFINED
enum EFileType : unsigned char
{
	FILETYPE_UNKNOWN,
	FILETYPE_EXECUTABLE,
	ARCHIVE_ZIP,
	ARCHIVE_RAR,
	ARCHIVE_ACE,
	ARCHIVE_7Z,
	IMAGE_ISO,
	AUDIO_MPEG,
	VIDEO_AVI,
	VIDEO_MPG,
	VIDEO_MP4,
	VIDEO_MKV,
	VIDEO_OGG,
	WM,
	PIC_JPG,
	PIC_PNG,
	PIC_GIF,
	DOCUMENT_PDF,
	DOCUMENT_EPUB,
	DOCUMENT_MOBI,
	ARCHIVE_GZ,
	AUDIO_FLAC,
	AUDIO_WAV,
	AUDIO_AAC
};
#endif

namespace FileTypeClassifierSeams
{
/**
 * @brief Number of leading bytes needed by the primary file signature table.
 */
constexpr size_t kHeaderCheckSize = 16;
constexpr size_t kDeepHeaderCheckSize = 68;
constexpr size_t kIsoHeaderCheckSize = 6;
constexpr uint64_t kIsoHeaderOffset = 0x8000;

inline uint64_t GetHeaderRangeEnd(const uint64_t uOffset, const size_t uSize = kHeaderCheckSize)
{
	return uOffset + uSize - 1;
}

enum class HeaderProbeStatus
{
	Pending,
	CheckedUnknown,
	Detected
};

struct HeaderProbeSummary
{
	HeaderProbeStatus status = HeaderProbeStatus::Pending;
	EFileType type = FILETYPE_UNKNOWN;
};

/**
 * @brief Maps one detected file type to its display label and accepted
 * filename extensions.
 */
struct SFileTypeExtension
{
	EFileType eType;
	LPCTSTR pszLabel;
	LPCTSTR pszExtensions;
};

/**
 * @brief Returns the shared extension table used by header checks, extension
 * checks, and fake-file analysis.
 */
inline const SFileTypeExtension* GetFileTypeExtensions()
{
	static const SFileTypeExtension s_fileExts[] =
	{
		{ ARCHIVE_ZIP,			_T("ZIP"),			_T("|ZIP|JAR|CBZ|") },
		{ ARCHIVE_RAR,			_T("RAR"),			_T("|RAR|CBR|") },
		{ ARCHIVE_ACE,			_T("ACE"),			_T("|ACE|") },
		{ ARCHIVE_7Z,			_T("7Z"),			_T("|7Z|") },
		{ ARCHIVE_GZ,			_T("GZip"),			_T("|GZ|TGZ|") },
		{ AUDIO_MPEG,			_T("MPEG Audio"),	_T("|MP2|MP3|") },
		{ AUDIO_FLAC,			_T("FLAC"),			_T("|FLAC|") },
		{ AUDIO_WAV,			_T("WAV"),			_T("|WAV|") },
		{ AUDIO_AAC,			_T("AAC"),			_T("|AAC|") },
		{ IMAGE_ISO,			_T("ISO/NRG"),		_T("|ISO|NRG|") },
		{ VIDEO_MPG,			_T("MPEG Video"),	_T("|MPG|MPEG|") },
		{ VIDEO_AVI,			_T("AVI"),			_T("|AVI|DIVX|") },
		{ VIDEO_MP4,			_T("MP4"),			_T("|MP4|MOV|QT|") },
		{ VIDEO_MKV,			_T("MKV"),			_T("|MKV|") },
		{ VIDEO_OGG,			_T("OGG"),			_T("|OGG|OGM|") },
		{ WM,					_T("Microsoft Media Audio/Video"), _T("|ASF|WMV|WMA|") },
		{ PIC_JPG,				_T("JPEG"),			_T("|JPG|JPEG|") },
		{ PIC_PNG,				_T("PNG"),			_T("|PNG|") },
		{ PIC_GIF,				_T("GIF"),			_T("|GIF|") },
		{ DOCUMENT_PDF,			_T("PDF"),			_T("|PDF|") },
		{ DOCUMENT_EPUB,		_T("EPUB"),			_T("|EPUB|") },
		{ DOCUMENT_MOBI,		_T("MOBI"),			_T("|MOBI|AZW|AZW3|") },
		{ FILETYPE_EXECUTABLE,	_T("WIN/DOS EXE"),	_T("|EXE|COM|DLL|SYS|CPL|FON|OCX|SCR|VBX|") },
		{ FILETYPE_UNKNOWN,		_T(""),				_T("") }
	};
	return s_fileExts;
}

/**
 * @brief Returns the stable display label for one internal file type.
 */
inline CString GetFileTypeLabel(const EFileType eType)
{
	for (const SFileTypeExtension *pExt = GetFileTypeExtensions(); pExt->eType != FILETYPE_UNKNOWN; ++pExt) {
		if (pExt->eType == eType)
			return pExt->pszLabel;
	}
	return CString(_T('?'));
}

/**
 * @brief Classifies a filename by extension using the shared extension table.
 */
inline EFileType GetFileTypeFromExtension(LPCTSTR pszFileName)
{
	LPCTSTR const pDot = ::PathFindExtension(pszFileName);
	CString strExt(&pDot[static_cast<int>(*pDot != _T('\0'))]);
	strExt.MakeUpper();

	for (const SFileTypeExtension *pExt = GetFileTypeExtensions(); pExt->eType != FILETYPE_UNKNOWN; ++pExt) {
		const CString strNeedle(_T('|') + strExt + _T('|'));
		if (CString(pExt->pszExtensions).Find(strNeedle) >= 0)
			return pExt->eType;
	}

	if (strExt.GetLength() == 3 && strExt[0] == _T('R') && _istdigit(strExt[1]) && _istdigit(strExt[2]))
		return ARCHIVE_RAR;
	return FILETYPE_UNKNOWN;
}

/**
 * @brief Reports whether an extension token is known and whether it belongs to
 * the supplied file type.
 */
inline int IsExtensionTypeOf(const EFileType eType, LPCTSTR const pszExt)
{
	CString strExt;
	strExt.Format(_T("|%s|"), pszExt);
	for (const SFileTypeExtension *pExt = GetFileTypeExtensions(); pExt->eType != FILETYPE_UNKNOWN; ++pExt) {
		if (CString(pExt->pszExtensions).Find(strExt) >= 0)
			return eType == pExt->eType ? 1 : -1;
	}
	return 0;
}

/**
 * @brief Classifies a leading file header buffer using the shared signature
 * table. ISO-at-offset detection remains owned by the caller.
 */
inline EFileType DetectFileTypeFromHeader(const BYTE *pHeader, const size_t uHeaderSize, LPCTSTR pszFileName)
{
	constexpr size_t kMobiMagicOffset = 60;
	constexpr size_t kZipCompressionMethodOffset = 8;
	constexpr size_t kZipFileNameLengthOffset = 26;
	constexpr size_t kZipExtraLengthOffset = 28;
	constexpr size_t kZipFileNameOffset = 30;
	static const BYTE FILEHEADER_7Z_ID[] =	{ 0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C };
	static const BYTE FILEHEADER_ACE_ID[] =	{ 0x2A, 0x2A, 0x41, 0x43, 0x45, 0x2A, 0x2A };
	static const BYTE FILEHEADER_EPUB_MEDIA_TYPE[] = { 0x61, 0x70, 0x70, 0x6C, 0x69, 0x63, 0x61, 0x74, 0x69, 0x6F, 0x6E, 0x2F, 0x65, 0x70, 0x75, 0x62, 0x2B, 0x7A, 0x69, 0x70 };
	static const BYTE FILEHEADER_EPUB_MIMETYPE[] = { 0x6D, 0x69, 0x6D, 0x65, 0x74, 0x79, 0x70, 0x65 };
	static const BYTE FILEHEADER_EXE_ID[] =	{ 0x4D, 0x5A };
	static const BYTE FILEHEADER_FLAC_ID[] =	{ 0x66, 0x4C, 0x61, 0x43 };
	static const BYTE FILEHEADER_GIF_ID[] =	{ 0x47, 0x49, 0x46, 0x38 };
	static const BYTE FILEHEADER_GZ_ID[] =	{ 0x1F, 0x8B, 0x08 };
	static const BYTE FILEHEADER_JPG_ID[] =	{ 0xFF, 0xD8, 0xFF };
	static const BYTE FILEHEADER_MKV_ID[] =	{ 0x1A, 0x45, 0xDF, 0xA3 };
	static const BYTE FILEHEADER_MOBI_ID[] =	{ 0x42, 0x4F, 0x4F, 0x4B, 0x4D, 0x4F, 0x42, 0x49 };
	static const BYTE FILEHEADER_MP3_ID[] =	{ 0x49, 0x44, 0x33, 0x03 };
	static const BYTE FILEHEADER_MP3_ID2[] =	{ 0xFE, 0xFB };
	static const BYTE FILEHEADER_MP4_ID[] =	{ 0x66, 0x74, 0x79, 0x70 };
	static const BYTE FILEHEADER_MPG_ID[] =	{ 0x00, 0x00, 0x01, 0xBA };
	static const BYTE FILEHEADER_OGG_ID[] =	{ 0x4F, 0x67, 0x67, 0x53 };
	static const BYTE FILEHEADER_PDF_ID[] =	{ 0x25, 0x50, 0x44, 0x46 };
	static const BYTE FILEHEADER_PNG_ID[] =	{ 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
	static const BYTE FILEHEADER_RAR_ID[] =	{ 0x52, 0x61, 0x72, 0x21 };
	static const BYTE FILEHEADER_RIFF_ID[] =	{ 0x52, 0x49, 0x46, 0x46 };
	static const BYTE FILEHEADER_WM_ID[] =	{ 0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11, 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C };
	static const BYTE FILEHEADER_ZIP_ID[] =	{ 0x50, 0x4B, 0x03, 0x04 };

	if (pHeader == NULL)
		return FILETYPE_UNKNOWN;
	if (uHeaderSize >= kZipFileNameOffset + sizeof FILEHEADER_EPUB_MIMETYPE + sizeof FILEHEADER_EPUB_MEDIA_TYPE
		&& memcmp(pHeader, FILEHEADER_ZIP_ID, sizeof FILEHEADER_ZIP_ID) == 0
		&& pHeader[kZipCompressionMethodOffset] == 0x00 && pHeader[kZipCompressionMethodOffset + 1] == 0x00
		&& pHeader[kZipFileNameLengthOffset] == sizeof FILEHEADER_EPUB_MIMETYPE && pHeader[kZipFileNameLengthOffset + 1] == 0x00
		&& pHeader[kZipExtraLengthOffset] == 0x00 && pHeader[kZipExtraLengthOffset + 1] == 0x00
		&& memcmp(pHeader + kZipFileNameOffset, FILEHEADER_EPUB_MIMETYPE, sizeof FILEHEADER_EPUB_MIMETYPE) == 0
		&& memcmp(pHeader + kZipFileNameOffset + sizeof FILEHEADER_EPUB_MIMETYPE, FILEHEADER_EPUB_MEDIA_TYPE, sizeof FILEHEADER_EPUB_MEDIA_TYPE) == 0)
	{
		return DOCUMENT_EPUB;
	}
	if (uHeaderSize >= sizeof FILEHEADER_ZIP_ID && memcmp(pHeader, FILEHEADER_ZIP_ID, sizeof FILEHEADER_ZIP_ID) == 0)
		return ARCHIVE_ZIP;
	if (uHeaderSize >= sizeof FILEHEADER_RAR_ID && memcmp(pHeader, FILEHEADER_RAR_ID, sizeof FILEHEADER_RAR_ID) == 0)
		return ARCHIVE_RAR;
	if (uHeaderSize >= 7 + sizeof FILEHEADER_ACE_ID && memcmp(pHeader + 7, FILEHEADER_ACE_ID, sizeof FILEHEADER_ACE_ID) == 0)
		return ARCHIVE_ACE;
	if (uHeaderSize >= sizeof FILEHEADER_7Z_ID && memcmp(pHeader, FILEHEADER_7Z_ID, sizeof FILEHEADER_7Z_ID) == 0)
		return ARCHIVE_7Z;
	if (uHeaderSize >= sizeof FILEHEADER_GZ_ID && memcmp(pHeader, FILEHEADER_GZ_ID, sizeof FILEHEADER_GZ_ID) == 0)
		return ARCHIVE_GZ;
	if (uHeaderSize >= sizeof FILEHEADER_WM_ID && memcmp(pHeader, FILEHEADER_WM_ID, sizeof FILEHEADER_WM_ID) == 0)
		return WM;
	if (uHeaderSize >= 11 && memcmp(pHeader, FILEHEADER_RIFF_ID, sizeof FILEHEADER_RIFF_ID) == 0 && strncmp(reinterpret_cast<const char*>(pHeader) + 8, "AVI", 3) == 0)
		return VIDEO_AVI;
	if (uHeaderSize >= 12 && memcmp(pHeader, FILEHEADER_RIFF_ID, sizeof FILEHEADER_RIFF_ID) == 0 && strncmp(reinterpret_cast<const char*>(pHeader) + 8, "WAVE", 4) == 0)
		return AUDIO_WAV;
	if ((uHeaderSize >= sizeof FILEHEADER_MP3_ID && memcmp(pHeader, FILEHEADER_MP3_ID, sizeof FILEHEADER_MP3_ID) == 0)
		|| (uHeaderSize >= sizeof FILEHEADER_MP3_ID2 && memcmp(pHeader, FILEHEADER_MP3_ID2, sizeof FILEHEADER_MP3_ID2) == 0))
	{
		return AUDIO_MPEG;
	}
	if (uHeaderSize >= sizeof FILEHEADER_FLAC_ID && memcmp(pHeader, FILEHEADER_FLAC_ID, sizeof FILEHEADER_FLAC_ID) == 0)
		return AUDIO_FLAC;
	if (uHeaderSize >= sizeof FILEHEADER_MPG_ID && memcmp(pHeader, FILEHEADER_MPG_ID, sizeof FILEHEADER_MPG_ID) == 0)
		return VIDEO_MPG;
	if (uHeaderSize >= 4 + sizeof FILEHEADER_MP4_ID && memcmp(pHeader + 4, FILEHEADER_MP4_ID, sizeof FILEHEADER_MP4_ID) == 0)
		return VIDEO_MP4;
	if (uHeaderSize >= sizeof FILEHEADER_MKV_ID && memcmp(pHeader, FILEHEADER_MKV_ID, sizeof FILEHEADER_MKV_ID) == 0)
		return VIDEO_MKV;
	if (uHeaderSize >= sizeof FILEHEADER_OGG_ID && memcmp(pHeader, FILEHEADER_OGG_ID, sizeof FILEHEADER_OGG_ID) == 0)
		return VIDEO_OGG;
	if (uHeaderSize >= sizeof FILEHEADER_PDF_ID && memcmp(pHeader, FILEHEADER_PDF_ID, sizeof FILEHEADER_PDF_ID) == 0)
		return DOCUMENT_PDF;
	if (uHeaderSize >= kMobiMagicOffset + sizeof FILEHEADER_MOBI_ID && memcmp(pHeader + kMobiMagicOffset, FILEHEADER_MOBI_ID, sizeof FILEHEADER_MOBI_ID) == 0)
		return DOCUMENT_MOBI;
	if (uHeaderSize >= sizeof FILEHEADER_PNG_ID && memcmp(pHeader, FILEHEADER_PNG_ID, sizeof FILEHEADER_PNG_ID) == 0)
		return PIC_PNG;
	if (uHeaderSize >= 4 && memcmp(pHeader, FILEHEADER_JPG_ID, sizeof FILEHEADER_JPG_ID) == 0 && (pHeader[3] == 0xE1 || pHeader[3] == 0xE0))
		return PIC_JPG;
	if (uHeaderSize >= 6 && memcmp(pHeader, FILEHEADER_GIF_ID, sizeof FILEHEADER_GIF_ID) == 0 && pHeader[5] == 0x61 && (pHeader[4] == 0x37 || pHeader[4] == 0x39))
		return PIC_GIF;
	const LPCTSTR pszExtension = ::PathFindExtension(pszFileName != NULL ? pszFileName : _T(""));
	if (uHeaderSize >= sizeof FILEHEADER_EXE_ID && memcmp(pHeader, FILEHEADER_EXE_ID, sizeof FILEHEADER_EXE_ID) == 0)
		return _tcsicmp(pszExtension, _T(".rar")) == 0 ? FILETYPE_UNKNOWN : FILETYPE_EXECUTABLE;
	if (uHeaderSize >= 2 && pHeader[0] == 0xFF && (pHeader[1] & 0xF0) == 0xF0 && (pHeader[1] & 0x06) == 0
		&& _tcsicmp(pszExtension, _T(".aac")) == 0)
	{
		return AUDIO_AAC;
	}
	if (uHeaderSize >= 2 && (pHeader[0] & 0xFF) == 0xFF && (pHeader[1] & 0xE0) == 0xE0)
		return AUDIO_MPEG;
	return FILETYPE_UNKNOWN;
}

/**
 * @brief Classifies the ISO signature buffer read at the legacy 0x8000 offset.
 */
inline EFileType DetectIsoTypeFromOffsetHeader(const BYTE *pHeader, const size_t uHeaderSize)
{
	static const BYTE FILEHEADER_ISO_ID[] = { 0x01, 0x43, 0x44, 0x30, 0x30, 0x31 };
	if (pHeader != NULL && uHeaderSize >= sizeof FILEHEADER_ISO_ID && memcmp(pHeader, FILEHEADER_ISO_ID, sizeof FILEHEADER_ISO_ID) == 0)
		return IMAGE_ISO;
	return FILETYPE_UNKNOWN;
}

inline bool NeedsIsoOffsetProbe(const EFileType eExtensionType)
{
	return eExtensionType == IMAGE_ISO;
}

inline HeaderProbeSummary SummarizeHeaderProbe(const EFileType eDetectedType, const EFileType eExtensionType, const bool bHeaderRangeAvailable, const bool bIsoRangeAvailable)
{
	HeaderProbeSummary summary;
	summary.type = eDetectedType;
	if (eDetectedType != FILETYPE_UNKNOWN) {
		summary.status = HeaderProbeStatus::Detected;
		return summary;
	}
	if (!bHeaderRangeAvailable || (NeedsIsoOffsetProbe(eExtensionType) && !bIsoRangeAvailable)) {
		summary.status = HeaderProbeStatus::Pending;
		return summary;
	}
	summary.status = HeaderProbeStatus::CheckedUnknown;
	return summary;
}
}

#define EMULEBB_TEST_HAVE_FILE_TYPE_CLASSIFIER_SEAMS 1
