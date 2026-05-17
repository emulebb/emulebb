#pragma once

#include <atlstr.h>
#include <cstdint>
#include <cstring>
#include <vector>

namespace MediaMetadataFallbackSeams
{
/**
 * @brief Basic media facts used by owned metadata parsers and variant reports.
 */
struct SBasicMediaInfo
{
	CString strContainer;
	CString strVideoCodec;
	CString strAudioCodec;
	double fDurationSec = 0.0;
	std::uint32_t uWidth = 0;
	std::uint32_t uHeight = 0;
	std::uint32_t uVideoBitrate = 0;
	std::uint32_t uAudioBitrate = 0;
	std::uint32_t uAudioChannels = 0;
	std::uint32_t uAudioSampleRate = 0;
	int iVideoStreams = 0;
	int iAudioStreams = 0;
};

/**
 * @brief Redacted comparison state for one metadata extraction variant.
 */
struct SVariantSummary
{
	CString strName;
	bool bSucceeded = false;
	CString strStatus;
	SBasicMediaInfo info;
};

/**
 * @brief Converts a big-endian 32-bit field into host byte order.
 */
inline std::uint32_t ReadBe32(const std::uint8_t *p)
{
	return (static_cast<std::uint32_t>(p[0]) << 24)
		| (static_cast<std::uint32_t>(p[1]) << 16)
		| (static_cast<std::uint32_t>(p[2]) << 8)
		| static_cast<std::uint32_t>(p[3]);
}

/**
 * @brief Converts a big-endian 64-bit field into host byte order.
 */
inline std::uint64_t ReadBe64(const std::uint8_t *p)
{
	return (static_cast<std::uint64_t>(ReadBe32(p)) << 32) | ReadBe32(p + 4);
}

/**
 * @brief Returns true when two optional numeric facts materially diverge.
 */
inline bool ValuesDiverge(const double fLeft, const double fRight, const double fTolerance)
{
	if (fLeft <= 0.0 || fRight <= 0.0)
		return false;
	const double fDelta = fLeft > fRight ? fLeft - fRight : fRight - fLeft;
	return fDelta > fTolerance;
}

/**
 * @brief Appends one stable text finding when comparable metadata fields disagree.
 */
inline void AppendDivergenceFindings(const SVariantSummary &rExpected, const SVariantSummary &rActual, std::vector<CString> &raFindings)
{
	if (!rExpected.bSucceeded || !rActual.bSucceeded)
		return;
	if (ValuesDiverge(rExpected.info.fDurationSec, rActual.info.fDurationSec, 1.0)) {
		CString strFinding;
		strFinding.Format(_T("%s duration differs from %s"), (LPCTSTR)rActual.strName, (LPCTSTR)rExpected.strName);
		raFindings.push_back(strFinding);
	}
	if (rExpected.info.uWidth != 0 && rActual.info.uWidth != 0 && rExpected.info.uWidth != rActual.info.uWidth) {
		CString strFinding;
		strFinding.Format(_T("%s width differs from %s"), (LPCTSTR)rActual.strName, (LPCTSTR)rExpected.strName);
		raFindings.push_back(strFinding);
	}
	if (rExpected.info.uHeight != 0 && rActual.info.uHeight != 0 && rExpected.info.uHeight != rActual.info.uHeight) {
		CString strFinding;
		strFinding.Format(_T("%s height differs from %s"), (LPCTSTR)rActual.strName, (LPCTSTR)rExpected.strName);
		raFindings.push_back(strFinding);
	}
}

/**
 * @brief Reads one EBML variable-length integer from a bounded byte span.
 */
inline bool TryReadEbmlVint(const std::uint8_t *pData, const std::size_t uSize, std::size_t &ruOffset, std::uint64_t &ruValue, std::uint8_t &ruWidth, const bool bMaskLengthBit)
{
	if (ruOffset >= uSize)
		return false;
	const std::uint8_t uFirst = pData[ruOffset];
	std::uint8_t uMask = 0x80;
	ruWidth = 1;
	while (ruWidth <= 8 && (uFirst & uMask) == 0) {
		uMask >>= 1;
		++ruWidth;
	}
	if (ruWidth > 8 || ruOffset + ruWidth > uSize)
		return false;
	ruValue = bMaskLengthBit ? static_cast<std::uint64_t>(uFirst & ~uMask) : uFirst;
	for (std::uint8_t i = 1; i < ruWidth; ++i)
		ruValue = (ruValue << 8) | pData[ruOffset + i];
	ruOffset += ruWidth;
	return true;
}

/**
 * @brief Parses core MP4/MOV/M4A metadata from an in-memory atom span.
 */
inline bool TryReadMp4Basics(const std::uint8_t *pData, const std::size_t uSize, SBasicMediaInfo &rInfo)
{
	if (pData == nullptr || uSize < 16)
		return false;
	bool bSawFileType = false;
	bool bSawMetadata = false;
	for (std::size_t uOffset = 0; uOffset + 8 <= uSize;) {
		std::uint64_t uBoxSize = ReadBe32(pData + uOffset);
		const std::uint32_t uType = ReadBe32(pData + uOffset + 4);
		std::size_t uHeader = 8;
		if (uBoxSize == 1 && uOffset + 16 <= uSize) {
			uBoxSize = ReadBe64(pData + uOffset + 8);
			uHeader = 16;
		} else if (uBoxSize == 0) {
			uBoxSize = uSize - uOffset;
		}
		if (uBoxSize < uHeader || uOffset + uBoxSize > uSize)
			break;
		const std::size_t uPayload = uOffset + uHeader;
		const std::size_t uPayloadSize = static_cast<std::size_t>(uBoxSize - uHeader);
		if (uType == 0x66747970u) {
			bSawFileType = true;
			rInfo.strContainer = _T("MP4");
		} else if (uType == 0x6d766864u && uPayloadSize >= 20) {
			const std::uint8_t uVersion = pData[uPayload];
			std::uint32_t uTimescale = 0;
			std::uint64_t uDuration = 0;
			if (uVersion == 1 && uPayloadSize >= 32) {
				uTimescale = ReadBe32(pData + uPayload + 20);
				uDuration = ReadBe64(pData + uPayload + 24);
			} else {
				uTimescale = ReadBe32(pData + uPayload + 12);
				uDuration = ReadBe32(pData + uPayload + 16);
			}
			if (uTimescale != 0 && uDuration != 0) {
				rInfo.fDurationSec = static_cast<double>(uDuration) / static_cast<double>(uTimescale);
				bSawMetadata = true;
			}
		} else if (uType == 0x746b6864u && uPayloadSize >= 84) {
			const std::uint8_t uVersion = pData[uPayload];
			const std::size_t uDimensionOffset = uPayload + (uVersion == 1 ? 88 : 76);
			if (uDimensionOffset + 8 <= uOffset + uBoxSize) {
				const std::uint32_t uWidthFixed = ReadBe32(pData + uDimensionOffset);
				const std::uint32_t uHeightFixed = ReadBe32(pData + uDimensionOffset + 4);
				if ((uWidthFixed >> 16) != 0 && (uHeightFixed >> 16) != 0) {
					rInfo.uWidth = uWidthFixed >> 16;
					rInfo.uHeight = uHeightFixed >> 16;
					rInfo.iVideoStreams = rInfo.iVideoStreams == 0 ? 1 : rInfo.iVideoStreams;
					bSawMetadata = true;
				}
			}
		} else if ((uType == 0x6d6f6f76u || uType == 0x7472616bu || uType == 0x6d646961u || uType == 0x6d696e66u || uType == 0x7374626cu) && uPayloadSize >= 8) {
			SBasicMediaInfo nested;
			if (TryReadMp4Basics(pData + uPayload, uPayloadSize, nested)) {
				if (rInfo.fDurationSec <= 0.0)
					rInfo.fDurationSec = nested.fDurationSec;
				if (rInfo.uWidth == 0)
					rInfo.uWidth = nested.uWidth;
				if (rInfo.uHeight == 0)
					rInfo.uHeight = nested.uHeight;
				rInfo.iVideoStreams += nested.iVideoStreams;
				rInfo.iAudioStreams += nested.iAudioStreams;
				bSawMetadata = true;
			}
		}
		uOffset += static_cast<std::size_t>(uBoxSize);
	}
	return bSawFileType || bSawMetadata;
}

/**
 * @brief Parses core Matroska/WebM metadata from an in-memory EBML span.
 */
inline bool TryReadEbmlBasics(const std::uint8_t *pData, const std::size_t uSize, SBasicMediaInfo &rInfo)
{
	if (pData == nullptr || uSize < 16)
		return false;
	bool bSawEbml = false;
	bool bSawMetadata = false;
	double fTimecodeScale = 1000000.0;
	for (std::size_t uOffset = 0; uOffset + 2 < uSize;) {
		std::size_t uElementOffset = uOffset;
		std::uint64_t uId = 0;
		std::uint64_t uDataSize = 0;
		std::uint8_t uIdWidth = 0;
		std::uint8_t uSizeWidth = 0;
		if (!TryReadEbmlVint(pData, uSize, uElementOffset, uId, uIdWidth, false)
			|| !TryReadEbmlVint(pData, uSize, uElementOffset, uDataSize, uSizeWidth, true)
			|| uElementOffset + uDataSize > uSize) {
			++uOffset;
			continue;
		}
		const std::uint8_t *pPayload = pData + uElementOffset;
		if (uId == 0x1A45DFA3u) {
			bSawEbml = true;
			rInfo.strContainer = _T("Matroska/WebM");
		} else if (uId == 0x2AD7B1u && uDataSize > 0 && uDataSize <= 8) {
			std::uint64_t uValue = 0;
			for (std::uint64_t i = 0; i < uDataSize; ++i)
				uValue = (uValue << 8) | pPayload[i];
			if (uValue != 0)
				fTimecodeScale = static_cast<double>(uValue);
		} else if (uId == 0x4489u && (uDataSize == 4 || uDataSize == 8)) {
			if (uDataSize == 4) {
				std::uint32_t uRaw = ReadBe32(pPayload);
				float fDuration = 0.0f;
				memcpy(&fDuration, &uRaw, sizeof fDuration);
				rInfo.fDurationSec = static_cast<double>(fDuration) * fTimecodeScale / 1000000000.0;
			} else {
				std::uint64_t uRaw = ReadBe64(pPayload);
				double fDuration = 0.0;
				memcpy(&fDuration, &uRaw, sizeof fDuration);
				rInfo.fDurationSec = fDuration * fTimecodeScale / 1000000000.0;
			}
			bSawMetadata = rInfo.fDurationSec > 0.0;
		} else if (uId == 0xB0u && uDataSize > 0 && uDataSize <= 8) {
			std::uint32_t uWidth = 0;
			for (std::uint64_t i = 0; i < uDataSize; ++i)
				uWidth = (uWidth << 8) | pPayload[i];
			if (uWidth > 0 && uWidth <= 16384) {
				rInfo.uWidth = uWidth;
				rInfo.iVideoStreams = rInfo.iVideoStreams == 0 ? 1 : rInfo.iVideoStreams;
				bSawMetadata = true;
			}
		} else if (uId == 0xBAu && uDataSize > 0 && uDataSize <= 8) {
			std::uint32_t uHeight = 0;
			for (std::uint64_t i = 0; i < uDataSize; ++i)
				uHeight = (uHeight << 8) | pPayload[i];
			if (uHeight > 0 && uHeight <= 16384) {
				rInfo.uHeight = uHeight;
				rInfo.iVideoStreams = rInfo.iVideoStreams == 0 ? 1 : rInfo.iVideoStreams;
				bSawMetadata = true;
			}
		}
		uOffset = uElementOffset + static_cast<std::size_t>(uDataSize);
	}
	return bSawEbml || bSawMetadata;
}
}
