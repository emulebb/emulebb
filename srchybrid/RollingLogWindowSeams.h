#pragma once

#include <atlstr.h>
#include <climits>
#include <windows.h>

namespace RollingLogWindowSeams
{
constexpr size_t kNormalLogVisibleEntries = 2000u;
constexpr size_t kVerboseLogVisibleEntries = 4000u;
constexpr int kMaxVisibleLineChars = 1000;
constexpr ULONGLONG kRichEditTextLimitSlackChars = 256u * 1024u;

struct SRollingLogEntry
{
	CString strText;
	UINT uMsgType = 0;
	bool bTyped = false;
};

struct STrimPlan
{
	size_t uVisibleEntriesToTrim = 0;
	size_t uPendingEntriesToDrop = 0;
};

inline size_t MinSizeT(const size_t uLeft, const size_t uRight)
{
	return uLeft < uRight ? uLeft : uRight;
}

inline int MaxInt(const int iLeft, const int iRight)
{
	return iLeft > iRight ? iLeft : iRight;
}

/**
 * @brief Plans how many already-materialized and not-yet-displayed rolling log entries to drop.
 */
inline STrimPlan BuildTrimPlan(const size_t uTotalEntryCount, const size_t uPendingEntryCount, const size_t uMaxEntryCount)
{
	STrimPlan plan;
	if (uMaxEntryCount == 0 || uTotalEntryCount <= uMaxEntryCount)
		return plan;

	const size_t uOverflow = uTotalEntryCount - uMaxEntryCount;
	const size_t uVisibleEntryCount = uTotalEntryCount > uPendingEntryCount ? uTotalEntryCount - uPendingEntryCount : 0;
	plan.uVisibleEntriesToTrim = MinSizeT(uOverflow, uVisibleEntryCount);
	plan.uPendingEntriesToDrop = uOverflow - plan.uVisibleEntriesToTrim;
	return plan;
}

inline int SaturatingAddChars(const int iCurrent, const int iAdditional)
{
	if (iAdditional <= 0)
		return iCurrent;
	if (iCurrent > INT_MAX - iAdditional)
		return INT_MAX;
	return iCurrent + iAdditional;
}

/**
 * @brief Applies a trim plan to the retained entries and pending display entries.
 */
template <typename TEntryDeque, typename TLengthFn>
inline int ApplyTrimPlan(TEntryDeque &rEntries, TEntryDeque &rPendingEntries, const size_t uMaxEntryCount, TLengthFn getEntryLength)
{
	int iCharsToRemoveFromVisibleText = 0;
	const STrimPlan trimPlan = BuildTrimPlan(rEntries.size(), rPendingEntries.size(), uMaxEntryCount);
	for (size_t uIndex = 0; uIndex < trimPlan.uVisibleEntriesToTrim && !rEntries.empty(); ++uIndex) {
		iCharsToRemoveFromVisibleText = SaturatingAddChars(iCharsToRemoveFromVisibleText, getEntryLength(rEntries.front()));
		rEntries.pop_front();
	}
	for (size_t uIndex = 0; uIndex < trimPlan.uPendingEntriesToDrop; ++uIndex) {
		if (!rEntries.empty())
			rEntries.pop_front();
		if (!rPendingEntries.empty())
			rPendingEntries.pop_front();
	}
	return iCharsToRemoveFromVisibleText;
}

inline DWORD BuildRichEditTextLimit(const size_t uMaxEntries, const int iMaxLineChars)
{
	const ULONGLONG ullDesiredLimit =
		static_cast<ULONGLONG>(uMaxEntries) *
		static_cast<ULONGLONG>(MaxInt(iMaxLineChars, 128));
	const ULONGLONG ullLimit = ullDesiredLimit + kRichEditTextLimitSlackChars;
	return static_cast<DWORD>(ullLimit > 0x7fffffffu ? 0x7fffffffu : ullLimit);
}
}
