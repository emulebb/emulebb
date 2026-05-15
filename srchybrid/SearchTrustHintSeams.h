#pragma once

#include "FakeFileDetectorSeams.h"

#include <cstdint>

namespace SearchTrustHintSeams
{
/**
 * @brief User-visible trust state for a search-result row.
 */
enum class DisplayKind
{
	Ok,
	Caution,
	Warning,
	HighRisk,
	Spam
};

/**
 * @brief Deterministic warning-only trust hint for search-result rows.
 */
struct TrustHint
{
	DisplayKind displayKind = DisplayKind::Ok;
	unsigned int riskBucket = 0;
	uint32_t fakeScore = 0;
};

/**
 * @brief Maps spam and fake-file analysis into a sortable search-result trust hint.
 */
inline TrustHint BuildTrustHint(const bool bSpam, const uint32_t uFakeScore, const FakeFileDetectorSeams::Severity eSeverity)
{
	if (bSpam)
		return {DisplayKind::Spam, 4, uFakeScore};

	switch (eSeverity) {
	case FakeFileDetectorSeams::Severity::Critical:
	case FakeFileDetectorSeams::Severity::High:
		return {DisplayKind::HighRisk, 3, uFakeScore};
	case FakeFileDetectorSeams::Severity::Medium:
		return {DisplayKind::Warning, 2, uFakeScore};
	case FakeFileDetectorSeams::Severity::Low:
		return {DisplayKind::Caution, 1, uFakeScore};
	case FakeFileDetectorSeams::Severity::None:
	default:
		break;
	}

	return {DisplayKind::Ok, 0, 0};
}

/**
 * @brief Compares trust hints in ascending risk order; callers can invert for descending sorts.
 */
inline int CompareTrustHints(const TrustHint &rLeft, const TrustHint &rRight)
{
	if (rLeft.riskBucket != rRight.riskBucket)
		return rLeft.riskBucket < rRight.riskBucket ? -1 : 1;
	if (rLeft.fakeScore != rRight.fakeScore)
		return rLeft.fakeScore < rRight.fakeScore ? -1 : 1;
	return 0;
}
}
