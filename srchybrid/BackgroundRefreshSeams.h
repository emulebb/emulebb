#pragma once

namespace BackgroundRefreshSeams
{
/**
 * @brief Records a refresh attempt only after the background worker actually starts.
 */
inline bool ShouldRecordRefreshAttempt(bool bThreadCreated, bool bThreadResumed)
{
	return bThreadCreated && bThreadResumed;
}
}
