#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#include <atlstr.h>

namespace IPFilterSeams
{
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
