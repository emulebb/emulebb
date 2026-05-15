#pragma once

#include "FakeFileDetectorSeams.h"

#include <cstdint>
#include <string>

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
 * @brief Stable categories for fake-file reason codes shown in user-facing
 * explanations.
 */
enum class ExplanationReason
{
	Unknown,
	MultipleNames,
	BadSignalName,
	BadSignalComment,
	HeaderExtensionMismatch,
	ExecutableMasquerade,
	ArchiveMasquerade,
	PendingHeaderCheck,
	ClaimedTypeMismatch,
	SpamScore,
	SpamStatus,
	BadRating,
	FakeRating,
	MultipleAich
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

/**
 * @brief Maps stable fake-file analyzer reason codes to explanation buckets.
 */
inline ExplanationReason ClassifyExplanationReason(const std::string &rReason)
{
	if (rReason == "multiple_names")
		return ExplanationReason::MultipleNames;
	if (rReason == "bad_signal_name")
		return ExplanationReason::BadSignalName;
	if (rReason == "bad_signal_comment")
		return ExplanationReason::BadSignalComment;
	if (rReason == "header_extension_mismatch")
		return ExplanationReason::HeaderExtensionMismatch;
	if (rReason == "executable_masquerade")
		return ExplanationReason::ExecutableMasquerade;
	if (rReason == "archive_masquerade")
		return ExplanationReason::ArchiveMasquerade;
	if (rReason == "pending_header_check")
		return ExplanationReason::PendingHeaderCheck;
	if (rReason == "claimed_type_mismatch")
		return ExplanationReason::ClaimedTypeMismatch;
	if (rReason == "spam_score")
		return ExplanationReason::SpamScore;
	if (rReason == "spam_status")
		return ExplanationReason::SpamStatus;
	if (rReason == "bad_rating")
		return ExplanationReason::BadRating;
	if (rReason == "fake_rating")
		return ExplanationReason::FakeRating;
	if (rReason == "multiple_aich")
		return ExplanationReason::MultipleAich;
	return ExplanationReason::Unknown;
}
}
