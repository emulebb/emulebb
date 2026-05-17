#pragma once

#include <atlstr.h>
#include <cstddef>
#include <time.h>

namespace IPFilterUpdateSeams
{
constexpr unsigned DefaultUpdatePeriodDays = 7u;
constexpr unsigned MinUpdatePeriodDays = 1u;
constexpr unsigned MaxUpdatePeriodDays = 365u;

/**
 * @brief Clamps the IP-filter automatic update interval to the supported day range.
 */
inline unsigned NormalizeUpdatePeriodDays(const unsigned uDays)
{
	if (uDays < MinUpdatePeriodDays)
		return MinUpdatePeriodDays;
	if (uDays > MaxUpdatePeriodDays)
		return MaxUpdatePeriodDays;
	return uDays;
}

/**
 * @brief Returns whether the configured automatic IP-filter interval is due.
 */
inline bool IsAutomaticRefreshDue(const __time64_t tNow, const __time64_t tLastCheck, const unsigned uPeriodDays)
{
	if (tNow <= 0)
		return false;
	if (tLastCheck <= 0)
		return true;

	const unsigned uNormalizedPeriodDays = NormalizeUpdatePeriodDays(uPeriodDays);
	return tNow >= (tLastCheck + static_cast<__time64_t>(uNormalizedPeriodDays) * 24 * 60 * 60);
}

/**
 * @brief Detects common server error payloads before they can replace the live filter file.
 */
inline bool LooksLikeMarkupPayload(const char* pcData, const std::size_t uLength)
{
	if (pcData == nullptr || uLength == 0)
		return false;

	std::size_t uOffset = 0;
	while (uOffset < uLength && static_cast<unsigned char>(pcData[uOffset]) <= ' ')
		++uOffset;
	if (uOffset >= uLength)
		return false;

	const std::size_t uRemaining = uLength - uOffset;
	const char* pc = pcData + uOffset;
	return (uRemaining >= 5 && (pc[0] == '<') && (pc[1] == 'h' || pc[1] == 'H') && (pc[2] == 't' || pc[2] == 'T') && (pc[3] == 'm' || pc[3] == 'M') && (pc[4] == 'l' || pc[4] == 'L'))
		|| (uRemaining >= 4 && (pc[0] == '<') && (pc[1] == 'x' || pc[1] == 'X') && (pc[2] == 'm' || pc[2] == 'M') && (pc[3] == 'l' || pc[3] == 'L'))
		|| (uRemaining >= 5 && (pc[0] == '<') && pc[1] == '?' && (pc[2] == 'x' || pc[2] == 'X') && (pc[3] == 'm' || pc[3] == 'M') && (pc[4] == 'l' || pc[4] == 'L'))
		|| (uRemaining >= 5 && (pc[0] == '<') && pc[1] == '!' && (pc[2] == 'd' || pc[2] == 'D') && (pc[3] == 'o' || pc[3] == 'O') && (pc[4] == 'c' || pc[4] == 'C'));
}

/**
 * @brief Returns whether an archive member name is an accepted IP-filter payload.
 */
inline bool IsSupportedArchiveMemberName(CString strFileName)
{
	strFileName.Trim();
	return strFileName.CompareNoCase(_T("ipfilter.dat")) == 0
		|| strFileName.CompareNoCase(_T("guarding.p2p")) == 0
		|| strFileName.CompareNoCase(_T("guardian.p2p")) == 0;
}
}
