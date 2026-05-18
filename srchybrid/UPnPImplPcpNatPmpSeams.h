#pragma once

#include <windows.h>

#define EMULE_TEST_HAVE_UPNP_PCP_NATPMP_SEAMS 1

/**
 * @brief Thread-wrapper action selected after probing the PCP/NAT-PMP discovery worker handle.
 */
enum class EPcpNatPmpDiscoveryThreadWaitAction
{
	KeepWaiting,
	ReleaseFinished,
	ReleaseAfterWaitFailure
};

/**
 * @brief Classifies a nonblocking WaitForSingleObject result for the owned PCP/NAT-PMP discovery worker.
 */
inline EPcpNatPmpDiscoveryThreadWaitAction ClassifyPcpNatPmpDiscoveryThreadWait(DWORD dwWait)
{
	if (dwWait == WAIT_OBJECT_0)
		return EPcpNatPmpDiscoveryThreadWaitAction::ReleaseFinished;
	if (dwWait == WAIT_FAILED)
		return EPcpNatPmpDiscoveryThreadWaitAction::ReleaseAfterWaitFailure;
	return EPcpNatPmpDiscoveryThreadWaitAction::KeepWaiting;
}
