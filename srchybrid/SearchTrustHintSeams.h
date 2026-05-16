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
	if (rReason == "implausible_media_length")
		return ExplanationReason::ImplausibleMediaLength;
	if (rReason == "implausible_media_bitrate")
		return ExplanationReason::ImplausibleMediaBitrate;
	return ExplanationReason::Unknown;
}
}
