#pragma once

#define EMULEBB_TEST_HAVE_KAD_INDEXED_LOAD_THREAD_SEAMS 1

namespace KadIndexedLoadThreadSeams
{
/**
 * @brief Action selected after attempting to create the asynchronous Kad index loader.
 */
enum class ELoadThreadLaunchAction
{
	StartWorker,
	DiscardWithoutStore
};

/**
 * @brief Classifies the MFC worker pointer returned by AfxBeginThread.
 */
inline ELoadThreadLaunchAction ClassifyLoadThreadLaunch(const void *pThread) noexcept
{
	return pThread != nullptr ? ELoadThreadLaunchAction::StartWorker : ELoadThreadLaunchAction::DiscardWithoutStore;
}

/**
 * @brief Reports whether shutdown must wait for an active asynchronous index loader.
 */
inline bool ShouldWaitForLoadThreadShutdown(const bool bLoadThreadStarted, const bool bDataLoaded) noexcept
{
	return bLoadThreadStarted && !bDataLoaded;
}
}
