#pragma once

#include <atlstr.h>
#include <climits>
#include <cstdint>
#include <vector>
#include "PreferenceValidationSeams.h"

namespace PreferenceUiSeams
{
constexpr UINT kMaxLogFileSizeKiB = 1024u * 1024u;
constexpr UINT kDefaultLogFileSizeBytes = 16u * 1024u * 1024u;
constexpr UINT kMinLogBufferKiB = 16u;
constexpr UINT kMaxLogBufferKiB = 1024u * 1024u;
constexpr UINT kDefaultLogBufferKiB = 256u;
constexpr UINT kMaxChatHistoryLines = 10000u;
constexpr UINT kMaxMessageSessions = 10000u;
constexpr UINT kMaxPerfLogIntervalMinutes = 1440u;
constexpr int kSessionTransferModeDisabled = 0;
constexpr int kSessionTransferModePercentOfFile = 1;
constexpr int kSessionTransferModeAbsoluteMiB = 2;
constexpr UINT kDefaultSessionTransferPercent = 55u;
constexpr UINT kMinSessionTransferPercent = 1u;
constexpr UINT kMaxSessionTransferPercent = 100u;
constexpr UINT kMinSessionTransferMiB = 1u;
constexpr UINT kMaxSessionTransferMiB = 4096u;

inline bool IsLogFileSizeKiBAllowed(UINT uValue)
{
	return uValue <= kMaxLogFileSizeKiB;
}

inline bool IsLogBufferKiBAllowed(UINT uValue)
{
	return uValue >= kMinLogBufferKiB && uValue <= kMaxLogBufferKiB;
}

inline UINT LogFileSizeKiBToBytes(UINT uValue)
{
	return uValue > (UINT_MAX / 1024u) ? UINT_MAX : uValue * 1024u;
}

inline UINT LogFileSizeBytesToKiB(UINT uValue)
{
	return (uValue / 1024u) + ((uValue % 1024u) ? 1u : 0u);
}

inline UINT NormalizeLogFileSizeBytes(int iValue)
{
	if (iValue < 0)
		return kDefaultLogFileSizeBytes;

	const UINT uValue = static_cast<UINT>(iValue);
	return uValue <= LogFileSizeKiBToBytes(kMaxLogFileSizeKiB) ? uValue : kDefaultLogFileSizeBytes;
}

inline UINT NormalizeLogBufferKiB(int iValue)
{
	if (iValue < 0)
		return kDefaultLogBufferKiB;

	const UINT uValue = static_cast<UINT>(iValue);
	return IsLogBufferKiBAllowed(uValue) ? uValue : kDefaultLogBufferKiB;
}

inline UINT NormalizePositiveBounded(int iValue, UINT uDefault, UINT uMax)
{
	return PreferenceValidationSeams::NormalizePositiveBoundedIntOrDefault(iValue, uDefault, uMax);
}

inline int NormalizeCrashDumpMode(int iValue)
{
	return (iValue >= 0 && iValue <= 2) ? iValue : 0;
}

inline int NormalizePreviewSmallBlocks(int iValue)
{
	return (iValue >= 0 && iValue <= 2) ? iValue : 0;
}

inline int NormalizeLogFileFormat(int iValue)
{
	return (iValue == 0 || iValue == 1) ? iValue : 0;
}

inline int NormalizePerfLogFileFormat(int iValue)
{
	return (iValue == 0 || iValue == 1) ? iValue : 0;
}

inline UINT NormalizePerfLogIntervalMinutes(UINT uValue)
{
	return (uValue >= 1u && uValue <= kMaxPerfLogIntervalMinutes) ? uValue : 5u;
}

inline int NormalizeSessionTransferLimitMode(int iValue)
{
	switch (iValue) {
	case kSessionTransferModeDisabled:
	case kSessionTransferModePercentOfFile:
	case kSessionTransferModeAbsoluteMiB:
		return iValue;
	default:
		return kSessionTransferModePercentOfFile;
	}
}

inline UINT NormalizeSessionTransferLimitValue(int iMode, UINT uValue)
{
	const int iNormalizedMode = NormalizeSessionTransferLimitMode(iMode);
	if (iNormalizedMode == kSessionTransferModePercentOfFile) {
		if (uValue < kMinSessionTransferPercent)
			return kMinSessionTransferPercent;
		if (uValue > kMaxSessionTransferPercent)
			return kMaxSessionTransferPercent;
		return uValue;
	}
	if (iNormalizedMode == kSessionTransferModeAbsoluteMiB) {
		if (uValue < kMinSessionTransferMiB)
			return kMinSessionTransferMiB;
		if (uValue > kMaxSessionTransferMiB)
			return kMaxSessionTransferMiB;
		return uValue;
	}
	return uValue > kMaxSessionTransferMiB ? kMaxSessionTransferMiB : uValue;
}

inline bool IsPositiveBounded(UINT uValue, UINT uMax)
{
	return PreferenceValidationSeams::IsPositiveBounded(uValue, uMax);
}

inline bool TryParseIPv4Address(const CString &strInput, uint32_t &uAddress)
{
	CString strValue(strInput);
	strValue.Trim();
	if (strValue.IsEmpty())
		return false;

	uint32_t parts[4] = {};
	int iPart = 0;
	int iValue = 0;
	int iDigits = 0;
	for (int i = 0; i <= strValue.GetLength(); ++i) {
		const TCHAR ch = (i < strValue.GetLength()) ? strValue[i] : _T('.');
		if (ch >= _T('0') && ch <= _T('9')) {
			iValue = (iValue * 10) + (ch - _T('0'));
			if (++iDigits > 3 || iValue > 255)
				return false;
			continue;
		}

		if (ch != _T('.') || iDigits == 0 || iPart >= 4)
			return false;

		parts[iPart++] = static_cast<uint32_t>(iValue);
		iValue = 0;
		iDigits = 0;
	}

	if (iPart != 4)
		return false;

	uAddress = parts[0] | (parts[1] << 8) | (parts[2] << 16) | (parts[3] << 24);
	return true;
}

inline CString FormatIPv4Address(uint32_t uAddress)
{
	CString strAddress;
	strAddress.Format(_T("%u.%u.%u.%u"),
		static_cast<unsigned>(uAddress & 0xffu),
		static_cast<unsigned>((uAddress >> 8) & 0xffu),
		static_cast<unsigned>((uAddress >> 16) & 0xffu),
		static_cast<unsigned>((uAddress >> 24) & 0xffu));
	return strAddress;
}

inline bool TryParseAllowedRemoteIpList(const CString &strInput, std::vector<uint32_t> &ruAddresses, CString &rstrInvalidToken)
{
	ruAddresses.clear();
	rstrInvalidToken.Empty();

	CString strRemaining(strInput);
	int iPos = 0;
	while (iPos >= 0) {
		CString strToken(strRemaining.Tokenize(_T(";"), iPos));
		strToken.Trim();
		if (strToken.IsEmpty())
			continue;

		uint32_t uAddress = 0;
		if (!TryParseIPv4Address(strToken, uAddress) || uAddress == 0u || uAddress == 0xffffffffu) {
			rstrInvalidToken = strToken;
			return false;
		}
		ruAddresses.push_back(uAddress);
	}

	return true;
}

inline CString FormatAllowedRemoteIpList(const std::vector<uint32_t> &rAddresses)
{
	CString strResult;
	for (size_t i = 0; i < rAddresses.size(); ++i) {
		if (i > 0)
			strResult += _T(';');
		strResult += FormatIPv4Address(rAddresses[i]);
	}
	return strResult;
}
}
