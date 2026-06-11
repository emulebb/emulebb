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
 * @brief User-facing Kad publisher confidence bucket for search results.
 */
enum class KadTrustKind
{
	Unknown,
	Low,
	Normal,
	High
};

/**
 * @brief Unified file-confidence band shown in the search/download "Confidence" column.
 *        Higher rank = more confident. Composes fake-file severity, user ratings, and
 *        Kad publisher confidence into a single symmetric scale.
 */
enum class ConfidenceLevel
{
	Spam,
	LikelyFake,
	Suspect,
	Caution,
	LooksGood,
	Genuine
};

/**
 * @brief Stable categories for fake-file reason codes shown in user-facing
 * explanations.
 */
enum class ExplanationReason
{
	Unknown,
	MultipleNames,
	NameMediaTagMismatch,
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
	MultipleAich,
	ImplausibleMediaLength,
	ImplausibleMediaBitrate
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
 * @brief Deterministic Kad confidence hint decoded from TAG_PUBLISHINFO.
 */
struct KadTrustHint
{
	KadTrustKind kind = KadTrustKind::Unknown;
	uint32_t trustValueCent = 0;
	uint32_t publishers = 0;
	uint32_t differentNames = 0;
};

/**
 * @brief Composite confidence hint for the unified "Confidence" column / REST field.
 *        score is 0-100 (higher = more confident) for stable sorting.
 */
struct ConfidenceHint
{
	ConfidenceLevel level = ConfidenceLevel::LooksGood;
	unsigned int rank = 4;
	uint32_t score = 70;       // composite confidence 0-100 (higher = more confident)
	uint32_t fakeScore = 0;    // underlying fake-risk % for the cautionary/fake bands
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
 * @brief Converts a user-visible risk display kind to the stable REST token.
 */
inline const char* DisplayKindToken(const DisplayKind eKind)
{
	switch (eKind) {
	case DisplayKind::Caution:
		return "caution";
	case DisplayKind::Warning:
		return "warning";
	case DisplayKind::HighRisk:
		return "high_risk";
	case DisplayKind::Spam:
		return "spam";
	case DisplayKind::Ok:
	default:
		return "ok";
	}
}

inline unsigned int KadTrustRank(const KadTrustKind eKind)
{
	switch (eKind) {
	case KadTrustKind::High:
		return 3;
	case KadTrustKind::Normal:
		return 2;
	case KadTrustKind::Low:
		return 1;
	case KadTrustKind::Unknown:
	default:
		return 0;
	}
}

/**
 * @brief Decodes Kad publish info into simple Low/Normal/High buckets.
 */
inline KadTrustHint BuildKadTrustHint(const uint32_t uKadPublishInfo)
{
	KadTrustHint hint;
	hint.differentNames = (uKadPublishInfo >> 24) & 0xffu;
	hint.publishers = (uKadPublishInfo >> 16) & 0xffu;
	hint.trustValueCent = uKadPublishInfo & 0xffffu;
	if (uKadPublishInfo == 0 || hint.publishers == 0) {
		hint.kind = KadTrustKind::Unknown;
		return hint;
	}
	if (hint.trustValueCent < 100)
		hint.kind = KadTrustKind::Low;
	else if (hint.trustValueCent < 300)
		hint.kind = KadTrustKind::Normal;
	else
		hint.kind = KadTrustKind::High;
	return hint;
}

/**
 * @brief Compares Kad confidence hints in ascending confidence order.
 */
inline int CompareKadTrustHints(const KadTrustHint &rLeft, const KadTrustHint &rRight)
{
	const unsigned int uLeftRank = KadTrustRank(rLeft.kind);
	const unsigned int uRightRank = KadTrustRank(rRight.kind);
	if (uLeftRank != uRightRank)
		return uLeftRank < uRightRank ? -1 : 1;
	if (rLeft.trustValueCent != rRight.trustValueCent)
		return rLeft.trustValueCent < rRight.trustValueCent ? -1 : 1;
	if (rLeft.publishers != rRight.publishers)
		return rLeft.publishers < rRight.publishers ? -1 : 1;
	if (rLeft.differentNames != rRight.differentNames)
		return rLeft.differentNames > rRight.differentNames ? -1 : 1;
	return 0;
}

/**
 * @brief Converts a Kad publisher confidence bucket to the stable REST token.
 */
inline const char* KadTrustKindToken(const KadTrustKind eKind)
{
	switch (eKind) {
	case KadTrustKind::Low:
		return "low";
	case KadTrustKind::Normal:
		return "normal";
	case KadTrustKind::High:
		return "high";
	case KadTrustKind::Unknown:
	default:
		return "unknown";
	}
}

/**
 * @brief Builds the unified Confidence hint from fake-file analysis, user ratings, and
 *        Kad publisher confidence. Deterministic; higher score/rank = more confident.
 */
inline ConfidenceHint BuildConfidenceHint(const bool bSpam,
	const FakeFileDetectorSeams::Severity eSeverity, const uint32_t uFakeScore,
	const KadTrustHint &rKad, const uint32_t uUserRating0to5, const bool bHasComment)
{
	ConfidenceHint hint;
	hint.fakeScore = uFakeScore;
	if (bSpam) {
		hint.level = ConfidenceLevel::Spam;
		hint.rank = 0;
		hint.score = 0;
		return hint;
	}

	// Bad end: lower fake score keeps slightly more within-tier confidence (0..8).
	const uint32_t uWithinBad = uFakeScore >= 100 ? 0u : (100u - uFakeScore) / 12u;
	switch (eSeverity) {
	case FakeFileDetectorSeams::Severity::Critical:
	case FakeFileDetectorSeams::Severity::High:
		hint.level = ConfidenceLevel::LikelyFake; hint.rank = 1; hint.score = 10u + uWithinBad; return hint;
	case FakeFileDetectorSeams::Severity::Medium:
		hint.level = ConfidenceLevel::Suspect; hint.rank = 2; hint.score = 30u + uWithinBad; return hint;
	case FakeFileDetectorSeams::Severity::Low:
		hint.level = ConfidenceLevel::Caution; hint.rank = 3; hint.score = 50u + uWithinBad; return hint;
	case FakeFileDetectorSeams::Severity::None:
	default:
		break;
	}

	// Positive end: strong Kad publisher confidence and/or good user ratings.
	const unsigned int uKadRank = KadTrustRank(rKad.kind); // 0..3
	if (uKadRank >= 3 || uUserRating0to5 >= 4) {
		hint.level = ConfidenceLevel::Genuine;
		hint.rank = 5;
		hint.score = 90u + std::min<uint32_t>(9u, uKadRank * 2u + uUserRating0to5 + (bHasComment ? 1u : 0u));
		return hint;
	}
	if (uKadRank >= 2 || uUserRating0to5 >= 3) {
		hint.level = ConfidenceLevel::LooksGood; hint.rank = 4; hint.score = 78u; return hint;
	}
	// Neutral: no red flags and no positive signal yet.
	hint.level = ConfidenceLevel::LooksGood; hint.rank = 4; hint.score = 70u;
	return hint;
}

/**
 * @brief Compares confidence hints in ascending confidence order (callers invert for desc).
 */
inline int CompareConfidenceHints(const ConfidenceHint &rLeft, const ConfidenceHint &rRight)
{
	if (rLeft.rank != rRight.rank)
		return rLeft.rank < rRight.rank ? -1 : 1;
	if (rLeft.score != rRight.score)
		return rLeft.score < rRight.score ? -1 : 1;
	return 0;
}

/**
 * @brief Converts a confidence band to the stable REST token.
 */
inline const char* ConfidenceToken(const ConfidenceLevel eLevel)
{
	switch (eLevel) {
	case ConfidenceLevel::Spam:
		return "spam";
	case ConfidenceLevel::LikelyFake:
		return "likely_fake";
	case ConfidenceLevel::Suspect:
		return "suspect";
	case ConfidenceLevel::Caution:
		return "caution";
	case ConfidenceLevel::Genuine:
		return "genuine";
	case ConfidenceLevel::LooksGood:
	default:
		return "looks_good";
	}
}

/**
 * @brief Maps stable fake-file analyzer reason codes to explanation buckets.
 */
inline ExplanationReason ClassifyExplanationReason(const std::string &rReason)
{
	if (rReason == "multiple_names")
		return ExplanationReason::MultipleNames;
	if (rReason == "name_media_tag_mismatch")
		return ExplanationReason::NameMediaTagMismatch;
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
	if (rReason == "implausible_media_length")
		return ExplanationReason::ImplausibleMediaLength;
	if (rReason == "implausible_media_bitrate")
		return ExplanationReason::ImplausibleMediaBitrate;
	return ExplanationReason::Unknown;
}
}
