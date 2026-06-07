#pragma once

#include <atlstr.h>
#include <algorithm>
#include <windows.h>

namespace FilenameTextRepairSeams
{
constexpr int kMaxEntityNameLength = 32;
constexpr int kMaxMojibakeSegmentLength = 24;
constexpr int kMaxMojibakeRepairPasses = 3;

inline bool IsSurrogateCodePoint(const DWORD dwCodePoint)
{
	return dwCodePoint >= 0xD800u && dwCodePoint <= 0xDFFFu;
}

inline bool IsUnsafeDecodedEntityCodePoint(const DWORD dwCodePoint)
{
	return dwCodePoint == 0 || IsSurrogateCodePoint(dwCodePoint) || dwCodePoint > 0x10FFFFu || dwCodePoint < 0x20u;
}

inline bool AppendCodePoint(CString &rstrOutput, const DWORD dwCodePoint)
{
	if (IsUnsafeDecodedEntityCodePoint(dwCodePoint))
		return false;

	if (dwCodePoint <= 0xFFFFu) {
		rstrOutput += static_cast<TCHAR>(dwCodePoint);
		return true;
	}

	const DWORD dwScalar = dwCodePoint - 0x10000u;
	rstrOutput += static_cast<TCHAR>(0xD800u + (dwScalar >> 10));
	rstrOutput += static_cast<TCHAR>(0xDC00u + (dwScalar & 0x3FFu));
	return true;
}

inline bool TryParseEntityNumber(const CString &rstrEntity, DWORD &rdwCodePoint)
{
	if (rstrEntity.GetLength() < 2 || rstrEntity[0] != _T('#'))
		return false;

	const bool bHex = rstrEntity.GetLength() >= 3 && (rstrEntity[1] == _T('x') || rstrEntity[1] == _T('X'));
	const int iStart = bHex ? 2 : 1;
	if (iStart >= rstrEntity.GetLength())
		return false;

	DWORD dwValue = 0;
	for (int i = iStart; i < rstrEntity.GetLength(); ++i) {
		const TCHAR ch = rstrEntity[i];
		DWORD dwDigit = 0;
		if (ch >= _T('0') && ch <= _T('9'))
			dwDigit = static_cast<DWORD>(ch - _T('0'));
		else if (bHex && ch >= _T('a') && ch <= _T('f'))
			dwDigit = 10u + static_cast<DWORD>(ch - _T('a'));
		else if (bHex && ch >= _T('A') && ch <= _T('F'))
			dwDigit = 10u + static_cast<DWORD>(ch - _T('A'));
		else
			return false;

		const DWORD dwBase = bHex ? 16u : 10u;
		if (dwValue > (0x10FFFFu - dwDigit) / dwBase)
			return false;
		dwValue = dwValue * dwBase + dwDigit;
	}

	rdwCodePoint = dwValue;
	return true;
}

inline bool TryDecodeNamedEntity(const CString &rstrEntity, DWORD &rdwCodePoint)
{
	if (rstrEntity.CompareNoCase(_T("amp")) == 0)
		rdwCodePoint = _T('&');
	else if (rstrEntity.CompareNoCase(_T("lt")) == 0)
		rdwCodePoint = _T('<');
	else if (rstrEntity.CompareNoCase(_T("gt")) == 0)
		rdwCodePoint = _T('>');
	else if (rstrEntity.CompareNoCase(_T("quot")) == 0)
		rdwCodePoint = _T('"');
	else if (rstrEntity.CompareNoCase(_T("apos")) == 0)
		rdwCodePoint = _T('\'');
	else if (rstrEntity.CompareNoCase(_T("nbsp")) == 0)
		rdwCodePoint = 0x00A0u;
	else if (rstrEntity.CompareNoCase(_T("agrave")) == 0)
		rdwCodePoint = 0x00E0u;
	else if (rstrEntity.CompareNoCase(_T("egrave")) == 0)
		rdwCodePoint = 0x00E8u;
	else if (rstrEntity.CompareNoCase(_T("eacute")) == 0)
		rdwCodePoint = 0x00E9u;
	else if (rstrEntity.CompareNoCase(_T("igrave")) == 0)
		rdwCodePoint = 0x00ECu;
	else if (rstrEntity.CompareNoCase(_T("ograve")) == 0)
		rdwCodePoint = 0x00F2u;
	else if (rstrEntity.CompareNoCase(_T("ugrave")) == 0)
		rdwCodePoint = 0x00F9u;
	else if (rstrEntity.CompareNoCase(_T("ntilde")) == 0)
		rdwCodePoint = 0x00F1u;
	else if (rstrEntity.CompareNoCase(_T("lsquo")) == 0)
		rdwCodePoint = 0x2018u;
	else if (rstrEntity.CompareNoCase(_T("rsquo")) == 0)
		rdwCodePoint = 0x2019u;
	else if (rstrEntity.CompareNoCase(_T("ldquo")) == 0)
		rdwCodePoint = 0x201Cu;
	else if (rstrEntity.CompareNoCase(_T("rdquo")) == 0)
		rdwCodePoint = 0x201Du;
	else if (rstrEntity.CompareNoCase(_T("ndash")) == 0)
		rdwCodePoint = 0x2013u;
	else if (rstrEntity.CompareNoCase(_T("mdash")) == 0)
		rdwCodePoint = 0x2014u;
	else if (rstrEntity.CompareNoCase(_T("hellip")) == 0)
		rdwCodePoint = 0x2026u;
	else
		return false;
	return true;
}

inline bool DecodeHtmlEntityPass(const CString &rstrInput, CString &rstrOutput)
{
	rstrOutput.Empty();
	bool bChanged = false;

	for (int i = 0; i < rstrInput.GetLength(); ++i) {
		if (rstrInput[i] != _T('&')) {
			rstrOutput += rstrInput[i];
			continue;
		}

		const int iSemi = rstrInput.Find(_T(';'), i + 1);
		if (iSemi < 0 || iSemi - i - 1 <= 0 || iSemi - i - 1 > kMaxEntityNameLength) {
			rstrOutput += rstrInput[i];
			continue;
		}

		const CString strEntity(rstrInput.Mid(i + 1, iSemi - i - 1));
		DWORD dwCodePoint = 0;
		if ((!TryDecodeNamedEntity(strEntity, dwCodePoint) && !TryParseEntityNumber(strEntity, dwCodePoint))
			|| IsUnsafeDecodedEntityCodePoint(dwCodePoint))
		{
			rstrOutput += rstrInput.Mid(i, iSemi - i + 1);
			i = iSemi;
			continue;
		}

		if (!AppendCodePoint(rstrOutput, dwCodePoint)) {
			rstrOutput += rstrInput.Mid(i, iSemi - i + 1);
			i = iSemi;
			continue;
		}
		bChanged = true;
		i = iSemi;
	}

	return bChanged;
}

inline CString DecodeHtmlEntitiesBounded(const CString &rstrInput)
{
	CString strCurrent(rstrInput);
	for (int i = 0; i < 2; ++i) {
		CString strDecoded;
		if (!DecodeHtmlEntityPass(strCurrent, strDecoded))
			break;
		strCurrent = strDecoded;
	}
	return strCurrent;
}

inline bool IsMojibakeStartMarker(const TCHAR ch)
{
	return ch == static_cast<TCHAR>(0x00C3u)
		|| ch == static_cast<TCHAR>(0x00C2u)
		|| ch == static_cast<TCHAR>(0x00CCu)
		|| ch == static_cast<TCHAR>(0x00E2u)
		|| ch == static_cast<TCHAR>(0x00E3u)
		|| ch == static_cast<TCHAR>(0x00E4u)
		|| ch == static_cast<TCHAR>(0x00E5u)
		|| ch == static_cast<TCHAR>(0x00E6u)
		|| ch == static_cast<TCHAR>(0x00E7u)
		|| ch == static_cast<TCHAR>(0x00E8u)
		|| ch == static_cast<TCHAR>(0x00E9u)
		|| ch == static_cast<TCHAR>(0x00F0u)
		|| ch == static_cast<TCHAR>(0x00EFu);
}

inline bool IsLikelyMojibakeContinuationMarker(const TCHAR ch)
{
	if (ch >= static_cast<TCHAR>(0x0080u) && ch <= static_cast<TCHAR>(0x00BFu))
		return true;

	switch (ch) {
	case static_cast<TCHAR>(0x20ACu):
	case static_cast<TCHAR>(0x201Au):
	case static_cast<TCHAR>(0x0192u):
	case static_cast<TCHAR>(0x201Eu):
	case static_cast<TCHAR>(0x2026u):
	case static_cast<TCHAR>(0x2020u):
	case static_cast<TCHAR>(0x2021u):
	case static_cast<TCHAR>(0x02C6u):
	case static_cast<TCHAR>(0x2030u):
	case static_cast<TCHAR>(0x0160u):
	case static_cast<TCHAR>(0x2039u):
	case static_cast<TCHAR>(0x0152u):
	case static_cast<TCHAR>(0x017Du):
	case static_cast<TCHAR>(0x2018u):
	case static_cast<TCHAR>(0x2019u):
	case static_cast<TCHAR>(0x201Cu):
	case static_cast<TCHAR>(0x201Du):
	case static_cast<TCHAR>(0x2022u):
	case static_cast<TCHAR>(0x2013u):
	case static_cast<TCHAR>(0x2014u):
	case static_cast<TCHAR>(0x02DCu):
	case static_cast<TCHAR>(0x2122u):
	case static_cast<TCHAR>(0x0161u):
	case static_cast<TCHAR>(0x203Au):
	case static_cast<TCHAR>(0x0153u):
	case static_cast<TCHAR>(0x017Eu):
	case static_cast<TCHAR>(0x0178u):
		return true;
	default:
		return false;
	}
}

inline int CountMojibakeMarkers(const CString &rstrText)
{
	int nScore = 0;
	for (int i = 0; i < rstrText.GetLength(); ++i) {
		const TCHAR ch = rstrText[i];
		if (ch == static_cast<TCHAR>(0xFFFDu))
			nScore += 2;
		else if (IsMojibakeStartMarker(ch)
			&& i + 1 < rstrText.GetLength()
			&& IsLikelyMojibakeContinuationMarker(rstrText[i + 1]))
		{
			++nScore;
		}
	}
	return nScore;
}

inline bool TryMapWindows1252WideCharToByte(const WCHAR ch, BYTE &rbyValue)
{
	if (ch <= 0x00FFu) {
		rbyValue = static_cast<BYTE>(ch);
		return true;
	}

	switch (ch) {
	case 0x20ACu: rbyValue = 0x80u; return true;
	case 0x201Au: rbyValue = 0x82u; return true;
	case 0x0192u: rbyValue = 0x83u; return true;
	case 0x201Eu: rbyValue = 0x84u; return true;
	case 0x2026u: rbyValue = 0x85u; return true;
	case 0x2020u: rbyValue = 0x86u; return true;
	case 0x2021u: rbyValue = 0x87u; return true;
	case 0x02C6u: rbyValue = 0x88u; return true;
	case 0x2030u: rbyValue = 0x89u; return true;
	case 0x0160u: rbyValue = 0x8Au; return true;
	case 0x2039u: rbyValue = 0x8Bu; return true;
	case 0x0152u: rbyValue = 0x8Cu; return true;
	case 0x017Du: rbyValue = 0x8Eu; return true;
	case 0x2018u: rbyValue = 0x91u; return true;
	case 0x2019u: rbyValue = 0x92u; return true;
	case 0x201Cu: rbyValue = 0x93u; return true;
	case 0x201Du: rbyValue = 0x94u; return true;
	case 0x2022u: rbyValue = 0x95u; return true;
	case 0x2013u: rbyValue = 0x96u; return true;
	case 0x2014u: rbyValue = 0x97u; return true;
	case 0x02DCu: rbyValue = 0x98u; return true;
	case 0x2122u: rbyValue = 0x99u; return true;
	case 0x0161u: rbyValue = 0x9Au; return true;
	case 0x203Au: rbyValue = 0x9Bu; return true;
	case 0x0153u: rbyValue = 0x9Cu; return true;
	case 0x017Eu: rbyValue = 0x9Eu; return true;
	case 0x0178u: rbyValue = 0x9Fu; return true;
	default:
		return false;
	}
}

inline bool ContainsUnsafeDecodedText(const CStringW &rwstrText)
{
	if (rwstrText.IsEmpty())
		return true;
	for (int i = 0; i < rwstrText.GetLength(); ++i) {
		const WCHAR ch = rwstrText[i];
		if (ch < L' ' || ch == 0xFFFDu)
			return true;
	}
	return false;
}

inline bool TryBuildWindows1252MojibakeBytes(const CString &rstrSegment, const bool bTreatLowerAtildeAsUpperUtf8Lead, CStringA &rstrBytes)
{
	LPSTR pszBytes = rstrBytes.GetBuffer(rstrSegment.GetLength());
	for (int i = 0; i < rstrSegment.GetLength(); ++i) {
		BYTE byValue = 0;
		if (!TryMapWindows1252WideCharToByte(static_cast<WCHAR>(rstrSegment[i]), byValue)) {
			rstrBytes.ReleaseBuffer(0);
			return false;
		}
		if (bTreatLowerAtildeAsUpperUtf8Lead && rstrSegment[i] == static_cast<TCHAR>(0x00E3u)) {
			BYTE byNext = 0;
			if (i + 1 < rstrSegment.GetLength()
				&& TryMapWindows1252WideCharToByte(static_cast<WCHAR>(rstrSegment[i + 1]), byNext)
				&& byNext >= 0x80u
				&& byNext <= 0xBFu)
			{
				byValue = 0xC3u;
			}
		}
		pszBytes[i] = static_cast<CHAR>(byValue);
	}
	rstrBytes.ReleaseBuffer(rstrSegment.GetLength());
	return true;
}

inline bool TryDecodeUtf8Bytes(const CStringA &rstrBytes, CString &rstrDecoded)
{
	CStringW wstrDecoded;
	LPWSTR pszDecoded = wstrDecoded.GetBuffer(rstrBytes.GetLength());
	const int nDecoded = ::MultiByteToWideChar(
		CP_UTF8,
		MB_ERR_INVALID_CHARS,
		rstrBytes,
		rstrBytes.GetLength(),
		pszDecoded,
		rstrBytes.GetLength());
	if (nDecoded <= 0) {
		wstrDecoded.ReleaseBuffer(0);
		return false;
	}
	wstrDecoded.ReleaseBuffer(nDecoded);

	if (ContainsUnsafeDecodedText(wstrDecoded))
		return false;

	rstrDecoded = wstrDecoded;
	return true;
}

inline bool TryDecodeWindows1252MojibakeSegmentWithMode(const CString &rstrSegment, const bool bTreatLowerAtildeAsUpperUtf8Lead, CString &rstrDecoded)
{
	CStringA strBytes;
	if (!TryBuildWindows1252MojibakeBytes(rstrSegment, bTreatLowerAtildeAsUpperUtf8Lead, strBytes))
		return false;
	if (!TryDecodeUtf8Bytes(strBytes, rstrDecoded))
		return false;
	return CountMojibakeMarkers(rstrDecoded) < CountMojibakeMarkers(rstrSegment);
}

inline bool TryDecodeWindows1252MojibakeSegment(const CString &rstrSegment, CString &rstrDecoded)
{
	if (TryDecodeWindows1252MojibakeSegmentWithMode(rstrSegment, false, rstrDecoded))
		return true;
	return TryDecodeWindows1252MojibakeSegmentWithMode(rstrSegment, true, rstrDecoded);
}

inline CString RepairWindows1252Mojibake(const CString &rstrInput)
{
	if (CountMojibakeMarkers(rstrInput) == 0)
		return rstrInput;

	CString strOutput;
	for (int i = 0; i < rstrInput.GetLength();) {
		if (!IsMojibakeStartMarker(rstrInput[i])) {
			strOutput += rstrInput[i++];
			continue;
		}

		int nBestLength = 0;
		CString strBestDecoded;
		const int nMaxLength = (std::min)(kMaxMojibakeSegmentLength, rstrInput.GetLength() - i);
		for (int nLength = 2; nLength <= nMaxLength; ++nLength) {
			const CString strSegment(rstrInput.Mid(i, nLength));
			CString strDecoded;
			if (TryDecodeWindows1252MojibakeSegment(strSegment, strDecoded)
				&& (nBestLength == 0 || CountMojibakeMarkers(strSegment) > CountMojibakeMarkers(rstrInput.Mid(i, nBestLength)) || nLength > nBestLength))
			{
				nBestLength = nLength;
				strBestDecoded = strDecoded;
			}
		}

		if (nBestLength == 0) {
			strOutput += rstrInput[i++];
			continue;
		}

		strOutput += strBestDecoded;
		i += nBestLength;
	}

	return CountMojibakeMarkers(strOutput) < CountMojibakeMarkers(rstrInput) ? strOutput : rstrInput;
}

inline CString RepairWindows1252MojibakeBounded(const CString &rstrInput)
{
	CString strCurrent(rstrInput);
	for (int i = 0; i < kMaxMojibakeRepairPasses; ++i) {
		const CString strRepaired(RepairWindows1252Mojibake(strCurrent));
		if (strRepaired == strCurrent)
			break;
		if (CountMojibakeMarkers(strRepaired) >= CountMojibakeMarkers(strCurrent))
			break;
		strCurrent = strRepaired;
	}
	return strCurrent;
}

inline bool TryParseHexNibble(const TCHAR ch, BYTE &rbyValue)
{
	if (ch >= _T('0') && ch <= _T('9')) {
		rbyValue = static_cast<BYTE>(ch - _T('0'));
		return true;
	}
	if (ch >= _T('a') && ch <= _T('f')) {
		rbyValue = static_cast<BYTE>(10 + ch - _T('a'));
		return true;
	}
	if (ch >= _T('A') && ch <= _T('F')) {
		rbyValue = static_cast<BYTE>(10 + ch - _T('A'));
		return true;
	}
	return false;
}

inline bool TryParsePercentByteAt(const CString &rstrInput, const int iOffset, BYTE &rbyValue)
{
	if (iOffset + 2 >= rstrInput.GetLength() || rstrInput[iOffset] != _T('%'))
		return false;

	BYTE byHigh = 0;
	BYTE byLow = 0;
	if (!TryParseHexNibble(rstrInput[iOffset + 1], byHigh) || !TryParseHexNibble(rstrInput[iOffset + 2], byLow))
		return false;
	rbyValue = static_cast<BYTE>((byHigh << 4) | byLow);
	return true;
}

inline bool TryDecodePercentUtf8Bytes(const CStringA &rstrBytes, CString &rstrDecoded)
{
	bool bHasHighByte = false;
	for (int i = 0; i < rstrBytes.GetLength(); ++i) {
		if (static_cast<BYTE>(rstrBytes[i]) >= 0x80u) {
			bHasHighByte = true;
			break;
		}
	}
	if (!bHasHighByte)
		return false;
	return TryDecodeUtf8Bytes(rstrBytes, rstrDecoded);
}

inline CString DecodePercentEncodedUtf8Bounded(const CString &rstrInput)
{
	CString strOutput;
	for (int i = 0; i < rstrInput.GetLength();) {
		BYTE byValue = 0;
		if (!TryParsePercentByteAt(rstrInput, i, byValue)) {
			strOutput += rstrInput[i++];
			continue;
		}

		CStringA strBytes;
		int iEnd = i;
		while (TryParsePercentByteAt(rstrInput, iEnd, byValue)) {
			strBytes += static_cast<CHAR>(byValue);
			iEnd += 3;
		}

		CString strDecoded;
		if (TryDecodePercentUtf8Bytes(strBytes, strDecoded)) {
			strOutput += strDecoded;
			i = iEnd;
			continue;
		}

		strOutput += rstrInput[i++];
	}
	return strOutput;
}

inline CString PrecomposeUnicodeText(const CString &rstrInput)
{
	if (rstrInput.IsEmpty())
		return rstrInput;

	const CStringW wstrInput(rstrInput);
	const int nRequired = ::FoldStringW(MAP_PRECOMPOSED, wstrInput, wstrInput.GetLength(), NULL, 0);
	if (nRequired <= 0)
		return rstrInput;

	CStringW wstrOutput;
	LPWSTR pszOutput = wstrOutput.GetBuffer(nRequired);
	const int nWritten = ::FoldStringW(MAP_PRECOMPOSED, wstrInput, wstrInput.GetLength(), pszOutput, nRequired);
	if (nWritten <= 0) {
		wstrOutput.ReleaseBuffer(0);
		return rstrInput;
	}
	wstrOutput.ReleaseBuffer(nWritten);
	return CString(wstrOutput);
}

/**
 * @brief Repairs conservative filename-only entity escaping and mojibake from remote intake text.
 */
inline CString RepairIncomingFilenameText(const CString &rstrInput)
{
	CString strRepaired(DecodeHtmlEntitiesBounded(rstrInput));
	strRepaired = DecodePercentEncodedUtf8Bounded(strRepaired);
	strRepaired = RepairWindows1252MojibakeBounded(strRepaired);
	return PrecomposeUnicodeText(strRepaired);
}

inline CString RepairIncomingSearchFilename(const CString &rstrInput)
{
	return RepairIncomingFilenameText(rstrInput);
}

inline CString RepairIncomingEd2kLinkFilename(const CString &rstrInput)
{
	return RepairIncomingFilenameText(rstrInput);
}

inline CString RepairIncomingCollectionFilename(const CString &rstrInput)
{
	return RepairIncomingFilenameText(rstrInput);
}
}
