#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#include <atlstr.h>

namespace IPFilterSeams
{
constexpr uint32_t kDefaultFilterLevel = 100u;

enum PathHintType
{
	PathHintUnknown = 0,
	PathHintFilterDat = 1,
	PathHintPeerGuardian = 2
};

/**
 * @brief Captures one IP filter range in host-order integer form.
 */
struct IPRange
{
	uint32_t Start = 0;
	uint32_t End = 0;
	uint32_t Level = 0;
	CStringA Description;
};

/**
 * @brief Reports how a loaded IP filter table changed during overlap normalization.
 */
struct NormalizationStats
{
	size_t DuplicateCount = 0;
	size_t MergedCount = 0;
};

inline CString ExtractFileName(const CString &rstrFilePath)
{
	int iSeparator = rstrFilePath.ReverseFind(_T('\\'));
	const int iAltSeparator = rstrFilePath.ReverseFind(_T('/'));
	if (iAltSeparator > iSeparator)
		iSeparator = iAltSeparator;
	return rstrFilePath.Mid(iSeparator + 1);
}

inline CString ExtractFileExtension(const CString &rstrFilePath)
{
	int iSeparator = rstrFilePath.ReverseFind(_T('\\'));
	const int iAltSeparator = rstrFilePath.ReverseFind(_T('/'));
	if (iAltSeparator > iSeparator)
		iSeparator = iAltSeparator;

	const int iDot = rstrFilePath.ReverseFind(_T('.'));
	if (iDot <= iSeparator)
		return CString();
	return rstrFilePath.Mid(iDot);
}

inline PathHintType DetectFileTypeFromPath(const CString &rstrFilePath)
{
	const CString strFileName = ExtractFileName(rstrFilePath);
	const CString strExtension = ExtractFileExtension(strFileName);
	if (strExtension.CompareNoCase(_T(".p2p")) == 0 || strFileName.CompareNoCase(_T("guarding.p2p.txt")) == 0)
		return PathHintPeerGuardian;
	if (strExtension.CompareNoCase(_T(".prefix")) == 0)
		return PathHintFilterDat;
	return PathHintUnknown;
}

inline bool ShouldEvaluateFilter(bool bEnabled, size_t uFilterCount, uint32_t uIP)
{
	return bEnabled && uFilterCount > 0u && uIP != 0u;
}

/**
 * @brief Determines whether startup should populate the in-memory IP filter table.
 */
inline bool ShouldLoadAtStartup(bool bEnabled)
{
	return bEnabled;
}

/**
 * @brief Determines whether the background updater may refresh the local IP filter file.
 */
inline bool ShouldQueueAutomaticRefresh(bool bFilterEnabled, bool bAutoUpdateEnabled)
{
	return bFilterEnabled && bAutoUpdateEnabled;
}

inline void SkipAsciiSpaces(const CStringA &rstrText, int &riPos)
{
	while (riPos < rstrText.GetLength() && (rstrText[riPos] == ' ' || rstrText[riPos] == '\t'))
		++riPos;
}

inline bool TryParseUnsignedAscii(const CStringA &rstrText, int &riPos, uint32_t &ruValue, const uint32_t uMaxValue)
{
	SkipAsciiSpaces(rstrText, riPos);
	if (riPos >= rstrText.GetLength() || rstrText[riPos] < '0' || rstrText[riPos] > '9')
		return false;

	uint32_t uValue = 0;
	while (riPos < rstrText.GetLength() && rstrText[riPos] >= '0' && rstrText[riPos] <= '9') {
		const uint32_t uDigit = static_cast<uint32_t>(rstrText[riPos] - '0');
		if (uValue > (uMaxValue - uDigit) / 10u)
			return false;
		uValue = uValue * 10u + uDigit;
		++riPos;
	}

	ruValue = uValue;
	return true;
}

/**
 * @brief Parses a dotted IPv4 literal into the host-order integer used by IP filter ranges.
 */
inline bool TryParseIPv4HostOrder(const CStringA &rstrText, int &riPos, uint32_t &ruAddress)
{
	uint32_t uParts[4] = {};
	for (int iPart = 0; iPart < 4; ++iPart) {
		if (!TryParseUnsignedAscii(rstrText, riPos, uParts[iPart], 255u))
			return false;
		if (iPart < 3) {
			if (riPos >= rstrText.GetLength() || rstrText[riPos] != '.')
				return false;
			++riPos;
		}
	}

	ruAddress = (uParts[0] << 24u) | (uParts[1] << 16u) | (uParts[2] << 8u) | uParts[3];
	return true;
}

/**
 * @brief Parses an ipfilter.dat style line: start - end [, level [, description]].
 */
inline bool TryParseFilterDatLine(const CStringA &rstrLine, IPRange &rRange)
{
	int iPos = 0;
	uint32_t uStart = 0;
	uint32_t uEnd = 0;
	if (!TryParseIPv4HostOrder(rstrLine, iPos, uStart))
		return false;

	SkipAsciiSpaces(rstrLine, iPos);
	if (iPos >= rstrLine.GetLength() || rstrLine[iPos] != '-')
		return false;
	++iPos;

	if (!TryParseIPv4HostOrder(rstrLine, iPos, uEnd))
		return false;

	uint32_t uLevel = kDefaultFilterLevel;
	CStringA strDescription;
	SkipAsciiSpaces(rstrLine, iPos);
	if (iPos < rstrLine.GetLength()) {
		if (rstrLine[iPos] != ',')
			return false;
		++iPos;
		if (!TryParseUnsignedAscii(rstrLine, iPos, uLevel, (std::numeric_limits<uint32_t>::max)()))
			return false;

		SkipAsciiSpaces(rstrLine, iPos);
		if (iPos < rstrLine.GetLength()) {
			if (rstrLine[iPos] != ',')
				return false;
			++iPos;
			SkipAsciiSpaces(rstrLine, iPos);
			strDescription = rstrLine.Mid(iPos);
			while (!strDescription.IsEmpty() && static_cast<unsigned char>(strDescription[strDescription.GetLength() - 1]) < ' ')
				strDescription.Truncate(strDescription.GetLength() - 1);
		}
	}

	rRange.Start = uStart;
	rRange.End = uEnd;
	rRange.Level = uLevel;
	rRange.Description = strDescription;
	return true;
}

/**
 * @brief Parses a PeerGuardian text line: description : start - end.
 */
inline bool TryParsePeerGuardianLine(const CStringA &rstrLine, IPRange &rRange)
{
	const int iColon = rstrLine.ReverseFind(':');
	if (iColon < 0)
		return false;

	CStringA strDescription = rstrLine.Left(iColon);
	strDescription.Replace("PGIPDB", "");
	strDescription.Trim();

	int iPos = iColon + 1;
	uint32_t uStart = 0;
	uint32_t uEnd = 0;
	if (!TryParseIPv4HostOrder(rstrLine, iPos, uStart))
		return false;

	SkipAsciiSpaces(rstrLine, iPos);
	if (iPos >= rstrLine.GetLength() || rstrLine[iPos] != '-')
		return false;
	++iPos;

	if (!TryParseIPv4HostOrder(rstrLine, iPos, uEnd))
		return false;

	SkipAsciiSpaces(rstrLine, iPos);
	if (iPos != rstrLine.GetLength())
		return false;

	rRange.Start = uStart;
	rRange.End = uEnd;
	rRange.Level = kDefaultFilterLevel;
	rRange.Description = strDescription;
	return true;
}

/**
 * @brief Normalizes overlapping IP ranges into sorted, non-overlapping segments.
 */
inline std::vector<IPRange> NormalizeIPRanges(const std::vector<IPRange> &rranges, NormalizationStats *pStats = nullptr)
{
	if (pStats != nullptr)
		*pStats = NormalizationStats{};

	std::vector<IPRange> ranges;
	ranges.reserve(rranges.size());
	for (const IPRange &range : rranges)
		if (range.Start <= range.End)
			ranges.push_back(range);
	if (ranges.empty())
		return ranges;

	std::stable_sort(ranges.begin(), ranges.end(), [](const IPRange &rLeft, const IPRange &rRight) {
		if (rLeft.Start != rRight.Start)
			return rLeft.Start < rRight.Start;
		if (rLeft.End != rRight.End)
			return rLeft.End < rRight.End;
		return rLeft.Level < rRight.Level;
	});

	std::vector<uint64_t> boundaries;
	boundaries.reserve(ranges.size() * 2u);
	for (const IPRange &range : ranges) {
		boundaries.push_back(range.Start);
		boundaries.push_back(static_cast<uint64_t>(range.End) + 1ull);
	}
	std::sort(boundaries.begin(), boundaries.end());
	boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());

	std::vector<IPRange> normalized;
	for (size_t iBoundary = 0; iBoundary + 1u < boundaries.size(); ++iBoundary) {
		const uint64_t uStart64 = boundaries[iBoundary];
		const uint64_t uEnd64 = boundaries[iBoundary + 1u] - 1ull;
		if (uStart64 > (std::numeric_limits<uint32_t>::max)() || uEnd64 > (std::numeric_limits<uint32_t>::max)())
			continue;

		const uint32_t uStart = static_cast<uint32_t>(uStart64);
		const uint32_t uEnd = static_cast<uint32_t>(uEnd64);
		const IPRange *pWinner = nullptr;
		size_t uCoveringRanges = 0;
		for (const IPRange &range : ranges) {
			if (range.Start > uStart)
				break;
			if (range.End < uStart)
				continue;
			++uCoveringRanges;
			if (pWinner == nullptr || range.Level < pWinner->Level)
				pWinner = &range;
		}

		if (pWinner == nullptr)
			continue;
		if (pStats != nullptr && uCoveringRanges > 1u)
			++pStats->MergedCount;

		if (!normalized.empty() && static_cast<uint64_t>(normalized.back().End) + 1ull == uStart && normalized.back().Level == pWinner->Level) {
			normalized.back().End = uEnd;
			continue;
		}

		IPRange segment;
		segment.Start = uStart;
		segment.End = uEnd;
		segment.Level = pWinner->Level;
		segment.Description = pWinner->Description;
		normalized.push_back(segment);
	}

	if (pStats != nullptr && ranges.size() > normalized.size())
		pStats->DuplicateCount = ranges.size() - normalized.size();
	return normalized;
}
}
