#pragma once

#include <atlstr.h>
#include <algorithm>
#include <windows.h>

namespace FilenameTextRepairSeams
{
constexpr int kMaxEntityNameLength = 32;
constexpr int kMaxMojibakeSegmentLength = 24;

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
		|| ch == static_cast<TCHAR>(0x00E2u)
		|| ch == static_cast<TCHAR>(0x00F0u)
		|| ch == static_cast<TCHAR>(0x00EFu);
}

inline int CountMojibakeMarkers(const CString &rstrText)
{
	int nScore = 0;
	for (int i = 0; i < rstrText.GetLength(); ++i) {
		const TCHAR ch = rstrText[i];
		if (IsMojibakeStartMarker(ch))
			++nScore;
		else if (ch == static_cast<TCHAR>(0xFFFDu))
			nScore += 2;
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

inline bool TryDecodeWindows1252MojibakeSegment(const CString &rstrSegment, CString &rstrDecoded)
{
	CStringA strBytes;
	LPSTR pszBytes = strBytes.GetBuffer(rstrSegment.GetLength());
	for (int i = 0; i < rstrSegment.GetLength(); ++i) {
		BYTE byValue = 0;
		if (!TryMapWindows1252WideCharToByte(static_cast<WCHAR>(rstrSegment[i]), byValue)) {
			strBytes.ReleaseBuffer(0);
			return false;
		}
		pszBytes[i] = static_cast<CHAR>(byValue);
	}
	strBytes.ReleaseBuffer(rstrSegment.GetLength());

	CStringW wstrDecoded;
	LPWSTR pszDecoded = wstrDecoded.GetBuffer(rstrSegment.GetLength());
	const int nDecoded = ::MultiByteToWideChar(
		CP_UTF8,
		MB_ERR_INVALID_CHARS,
		strBytes,
		strBytes.GetLength(),
		pszDecoded,
		rstrSegment.GetLength());
	if (nDecoded <= 0) {
		wstrDecoded.ReleaseBuffer(0);
		return false;
	}
	wstrDecoded.ReleaseBuffer(nDecoded);

	if (ContainsUnsafeDecodedText(wstrDecoded))
		return false;

	rstrDecoded = wstrDecoded;
	return CountMojibakeMarkers(rstrDecoded) < CountMojibakeMarkers(rstrSegment);
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

/**
 * @brief Repairs conservative filename-only entity escaping and mojibake from remote intake text.
 */
inline CString RepairIncomingFilenameText(const CString &rstrInput)
{
	CString strRepaired(DecodeHtmlEntitiesBounded(rstrInput));
	strRepaired = RepairWindows1252Mojibake(strRepaired);
	return strRepaired;
}

inline CString RepairIncomingSearchFilename(const CString &rstrInput)
{
	return RepairIncomingFilenameText(rstrInput);
}

inline CString RepairIncomingEd2kLinkFilename(const CString &rstrInput)
{
	return RepairIncomingFilenameText(rstrInput);
}
}
