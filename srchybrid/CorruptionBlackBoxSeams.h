#pragma once

#include <Windows.h>

#include "types.h"

#define EMULE_TEST_HAVE_CORRUPTION_BLACK_BOX_SEAMS 1

namespace CorruptionBlackBoxSeams
{
/**
 * @brief Marks the overlapping record span and appends any untouched head/tail records after mutation.
 *
 * MFC CArray::Add may reallocate the backing store. Callers must therefore finish all writes to the
 * indexed record before appending split remainders and must not keep references across Add().
 */
template <typename TRecordArray, typename TRecord, typename TStatus>
inline uint64 MarkRecordOverlapAndAppendRemainders(TRecordArray &rRecords, INT_PTR nIndex, uint64 nRelStartPos, uint64 nRelEndPos, TStatus eMarkedStatus)
{
	TRecord &rRecord = rRecords[nIndex];
	const uint64 nOldStartPos = rRecord.m_nStartPos;
	const uint64 nOldEndPos = rRecord.m_nEndPos;
	if (nOldStartPos > nRelEndPos || nOldEndPos < nRelStartPos)
		return 0u;

	const uint64 nMarkedStartPos = nOldStartPos > nRelStartPos ? nOldStartPos : nRelStartPos;
	const uint64 nMarkedEndPos = nOldEndPos < nRelEndPos ? nOldEndPos : nRelEndPos;
	const uint32 dwOldIP = rRecord.m_dwIP;
	const TStatus eOldStatus = rRecord.m_BBRStatus;

	rRecord.m_nStartPos = nMarkedStartPos;
	rRecord.m_nEndPos = nMarkedEndPos;
	rRecord.m_BBRStatus = eMarkedStatus;

	if (nMarkedEndPos < nOldEndPos)
		rRecords.Add(TRecord(nMarkedEndPos + 1u, nOldEndPos, dwOldIP, eOldStatus));
	if (nOldStartPos < nMarkedStartPos)
		rRecords.Add(TRecord(nOldStartPos, nMarkedStartPos - 1u, dwOldIP, eOldStatus));

	return nMarkedEndPos - nMarkedStartPos + 1u;
}
}
