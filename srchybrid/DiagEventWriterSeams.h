#pragma once

#include <atlstr.h>
#include <cstdint>

namespace DiagEventWriterSeams
{
constexpr uint64_t kDefaultSegmentBytes = 64ull * 1024ull * 1024ull;
constexpr uint64_t kMinimumSegmentBytes = 1ull * 1024ull * 1024ull;
constexpr uint64_t kDefaultSegmentAgeMs = 60ull * 60ull * 1000ull;
constexpr size_t kDefaultRetainedSegments = 128u;
constexpr uint64_t kDefaultMaxRetainedSeconds = 7ull * 24ull * 60ull * 60ull;
constexpr uint64_t kDefaultMinimumFreeBytes = 0ull;

inline uint64_t MaximumTotalBytes(const uint64_t ullSegmentBytes)
{
	return ullSegmentBytes * kDefaultRetainedSegments;
}

inline uint64_t MaximumGrowthBytes(
	const uint64_t ullRetainedBytes,
	const uint64_t ullAvailableBytes,
	const uint64_t ullMaximumTotalBytes,
	const uint64_t ullMinimumFreeBytes)
{
	const uint64_t ullAggregateGrowth = ullRetainedBytes < ullMaximumTotalBytes
		? ullMaximumTotalBytes - ullRetainedBytes
		: 0;
	const uint64_t ullFreeSpaceGrowth = ullAvailableBytes > ullMinimumFreeBytes
		? ullAvailableBytes - ullMinimumFreeBytes
		: 0;
	return ullAggregateGrowth < ullFreeSpaceGrowth ? ullAggregateGrowth : ullFreeSpaceGrowth;
}

inline uint64_t NormalizeSegmentBytes(const uint64_t ullConfiguredBytes)
{
	if (ullConfiguredBytes == 0)
		return kDefaultSegmentBytes;
	return ullConfiguredBytes < kMinimumSegmentBytes ? kMinimumSegmentBytes : ullConfiguredBytes;
}

inline bool ShouldPruneRetainedSegment(
	const bool bActive,
	const bool bExpired,
	const size_t uRemainingSegments,
	const uint64_t ullRemainingBytes,
	const uint64_t ullMaximumTotalBytes)
{
	return !bActive
		&& (bExpired
			|| uRemainingSegments > kDefaultRetainedSegments
			|| ullRemainingBytes > ullMaximumTotalBytes);
}

inline bool ShouldRotate(
	const uint64_t ullCurrentBytes,
	const uint64_t ullNextRecordBytes,
	const uint64_t ullSegmentAgeMs,
	const uint64_t ullMaximumBytes,
	const uint64_t ullMaximumAgeMs)
{
	if (ullCurrentBytes == 0)
		return false;
	return ullCurrentBytes >= ullMaximumBytes
		|| ullNextRecordBytes > ullMaximumBytes - ullCurrentBytes
		|| ullSegmentAgeMs >= ullMaximumAgeMs;
}

inline CString BuildSegmentFileName(
	const DWORD dwProcessId,
	LPCTSTR pszRunId,
	const uint64_t ullSegmentId)
{
	CString strFileName;
	strFileName.Format(
		_T("emulebb-mfc-diag-%u-%s-%06I64u.jsonl"),
		static_cast<UINT>(dwProcessId),
		pszRunId != NULL ? pszRunId : _T(""),
		ullSegmentId);
	return strFileName;
}

inline LPCTSTR SegmentFilePattern()
{
	return _T("emulebb-mfc-diag-*.jsonl");
}

inline LPCTSTR HealthFileName()
{
	return _T("emulebb-mfc-diag-health.json");
}
}
