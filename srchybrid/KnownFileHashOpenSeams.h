#pragma once

#include <windows.h>

namespace KnownFileHashOpenSeams
{
constexpr unsigned kHashOpenRetryAttempts = 5u;
constexpr DWORD kHashOpenRetryDelayMs = 100u;

/**
 * @brief Identifies transient sharing failures worth retrying during hashing intake.
 */
inline bool IsRetryableHashOpenError(const DWORD dwError)
{
	return dwError == ERROR_SHARING_VIOLATION || dwError == ERROR_LOCK_VIOLATION;
}

/**
 * @brief Returns true when a failed hashing open should try again.
 */
inline bool ShouldRetryHashOpen(const DWORD dwError, const unsigned uAttemptIndex, const unsigned uMaxAttempts = kHashOpenRetryAttempts)
{
	return IsRetryableHashOpenError(dwError) && uAttemptIndex + 1u < uMaxAttempts;
}
}
