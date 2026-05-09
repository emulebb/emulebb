#pragma once

#define EMULE_DIRECT_DOWNLOAD_SEAMS_HAS_CANCELLED_REGISTER_POLICY 1

namespace DirectDownloadSeams
{
/**
 * @brief Reports whether a newly created WinInet handle may enter the owner
 * cancellation registry for the current cancellation state.
 */
inline bool ShouldRegisterInternetHandleForCancellationState(bool bOwnerCancelled) noexcept
{
	return !bOwnerCancelled;
}
}
