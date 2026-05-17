#pragma once

#include <afx.h>
#include <atlstr.h>
#include <cstdint>
#include <limits>

#include "Scheduler.h"

namespace SchedulerPolicySeams
{
constexpr int kMaxScheduleActions = 16;
constexpr int kSundayDayOfWeek = 1;
constexpr int kSaturdayDayOfWeek = 7;

/**
 * @brief Returns whether a stored scheduler day selector is supported.
 */
inline bool IsValidScheduleDay(const UINT uDay)
{
	return uDay <= DAY_SA_SO;
}

/**
 * @brief Normalizes an unsupported stored scheduler day back to daily.
 */
inline UINT NormalizeScheduleDay(const UINT uDay)
{
	return IsValidScheduleDay(uDay) ? uDay : DAY_DAILY;
}

/**
 * @brief Returns whether a scheduler action id is an executable action.
 */
inline bool IsValidScheduleAction(const int iAction)
{
	return iAction >= ACTION_SETUPL && iAction <= ACTION_CATRESUME;
}

/**
 * @brief Returns whether the scheduler action list can accept another action row.
 */
inline bool CanAddScheduleAction(const int iActionCount)
{
	return iActionCount >= 0 && iActionCount < kMaxScheduleActions;
}

/**
 * @brief Returns whether the scheduler should evaluate schedules on this tick.
 */
inline bool ShouldCheckScheduler(const bool bSchedulerEnabled, const INT_PTR iScheduleCount, const bool bAppClosing, const bool bForceCheck, const int iCurrentMinute, const int iLastCheckedMinute)
{
	if (!bSchedulerEnabled || iScheduleCount <= 0 || bAppClosing)
		return false;
	if (!bForceCheck && iCurrentMinute == iLastCheckedMinute)
		return false;
	return true;
}

/**
 * @brief Converts an hour/minute pair into minutes since midnight.
 */
inline int MinutesSinceMidnight(const int iHour, const int iMinute)
{
	return iHour * 60 + iMinute;
}

/**
 * @brief Returns whether a scheduler day selector matches the current local day.
 */
inline bool MatchesScheduleDay(const UINT uScheduleDay, const UINT uCurrentDayOfWeek)
{
	const UINT uDay = NormalizeScheduleDay(uScheduleDay);
	if (uDay == DAY_DAILY)
		return true;

	switch (uDay) {
	case DAY_MO:
	case DAY_DI:
	case DAY_MI:
	case DAY_DO:
	case DAY_FR:
	case DAY_SA:
	case DAY_SO:
		return (uDay % 7) + 1 == uCurrentDayOfWeek;
	case DAY_MO_FR:
		return uCurrentDayOfWeek != kSundayDayOfWeek && uCurrentDayOfWeek != kSaturdayDayOfWeek;
	case DAY_MO_SA:
		return uCurrentDayOfWeek != kSundayDayOfWeek;
	case DAY_SA_SO:
		return uCurrentDayOfWeek == kSundayDayOfWeek || uCurrentDayOfWeek == kSaturdayDayOfWeek;
	default:
		return false;
	}
}

/**
 * @brief Returns whether the current minute lies inside the scheduler time span.
 */
inline bool MatchesScheduleTime(const int iStartMinute, const int iEndMinute, const int iCurrentMinute)
{
	if (iStartMinute <= iEndMinute)
		return iCurrentMinute >= iStartMinute && iCurrentMinute < iEndMinute;
	return iCurrentMinute >= iStartMinute || iCurrentMinute < iEndMinute;
}

/**
 * @brief Returns whether a schedule row is eligible to run on this evaluation.
 */
inline bool ShouldActivateSchedule(const bool bScheduleEnabled, const bool bHasFirstAction, const UINT uScheduleDay, const int iStartMinute, const int iEndMinute, const UINT uCurrentDayOfWeek, const int iCurrentMinute)
{
	return bScheduleEnabled
		&& bHasFirstAction
		&& MatchesScheduleDay(uScheduleDay, uCurrentDayOfWeek)
		&& MatchesScheduleTime(iStartMinute, iEndMinute, iCurrentMinute);
}

/**
 * @brief Parses a signed decimal scheduler value without accepting partial input.
 */
inline bool TryParseSignedDecimal(CString strText, int& iValue)
{
	strText.Trim();
	if (strText.IsEmpty())
		return false;

	bool bNegative = false;
	if (strText[0] == _T('-') || strText[0] == _T('+')) {
		bNegative = strText[0] == _T('-');
		strText.Delete(0);
		if (strText.IsEmpty())
			return false;
	}

	int64_t iParsed = 0;
	for (int i = 0; i < strText.GetLength(); ++i) {
		const TCHAR ch = strText[i];
		if (ch < _T('0') || ch > _T('9'))
			return false;
		iParsed = iParsed * 10 + static_cast<int64_t>(ch - _T('0'));
		if ((!bNegative && iParsed > (std::numeric_limits<int>::max)())
			|| (bNegative && -iParsed < (std::numeric_limits<int>::min)()))
		{
			return false;
		}
	}

	iValue = static_cast<int>(bNegative ? -iParsed : iParsed);
	return true;
}

/**
 * @brief Parses a scheduler action value according to the action's value domain.
 */
inline bool TryParseScheduleActionValue(const int iAction, const CString& strText, int& iValue)
{
	if (!IsValidScheduleAction(iAction))
		return false;
	if (!TryParseSignedDecimal(strText, iValue))
		return false;

	switch (iAction) {
	case ACTION_CATSTOP:
	case ACTION_CATRESUME:
		return iValue >= -2;
	case ACTION_SETUPL:
	case ACTION_SETDOWNL:
	case ACTION_SOURCESL:
	case ACTION_CON5SEC:
	case ACTION_CONS:
		return iValue >= 0;
	default:
		return false;
	}
}

/**
 * @brief Formats a validated scheduler action value for storage.
 */
inline bool TryNormalizeScheduleActionValueText(const int iAction, const CString& strText, CString& strNormalized)
{
	int iValue = 0;
	if (!TryParseScheduleActionValue(iAction, strText, iValue))
		return false;
	strNormalized.Format(_T("%d"), iValue);
	return true;
}
}
