#pragma once

#include "types.h"
#include "UploadQueueSeams.h"

/**
 * @brief Reports whether the UDP throttler worker should keep draining the control queue.
 */
inline bool ShouldContinueUdpControlSend(bool bQueueEmpty, bool bWouldBlock, uint32 nSentBytes, uint32 nMaxBytesToSend)
{
	return !bQueueEmpty && !bWouldBlock && nSentBytes < nMaxBytesToSend;
}

/**
 * @brief Reports whether a UDP control packet exceeded the queue age budget.
 *
 * Tick arithmetic intentionally uses wrap-safe unsigned subtraction.
 */
inline bool HasUdpControlPacketExpired(uint32 nCurrentTick, uint32 nQueuedTick, uint32 nMaxQueueMilliseconds)
{
	return nCurrentTick - nQueuedTick >= nMaxQueueMilliseconds;
}

/**
 * @brief Reports whether a failed UDP send should be retried from the head of the queue.
 */
inline bool ShouldRequeueUdpControlPacket(int nSendResult)
{
	return nSendResult < 0;
}

/**
 * @brief Reports whether the UDP throttler should stop the current drain after requeueing a would-block packet.
 */
inline bool ShouldYieldAfterUdpControlRequeue(int nSendResult)
{
	return ShouldRequeueUdpControlPacket(nSendResult);
}

/**
 * @brief Reports whether the throttler should be re-woken for queued UDP control data.
 */
inline bool ShouldSignalUdpControlQueue(bool bWouldBlock, bool bQueueEmpty)
{
	return !bWouldBlock && !bQueueEmpty;
}

enum UdpControlQueueSignalAction
{
	udpControlQueueNoSignal,
	udpControlQueueSignalAfterUnlock
};

/**
 * @brief Classifies whether a queued UDP control send should wake the throttler after the socket lock is released.
 */
inline UdpControlQueueSignalAction ClassifyUdpControlQueueSignal(bool bWouldBlock, bool bQueueEmpty)
{
	return ShouldSignalUdpControlQueue(bWouldBlock, bQueueEmpty)
		? udpControlQueueSignalAfterUnlock
		: udpControlQueueNoSignal;
}

enum UploadDiskReadCompletionAction
{
	uploadDiskReadCompletionSendPackets,
	uploadDiskReadCompletionMarkIoError,
	uploadDiskReadCompletionDiscard
};

/**
 * @brief Classifies a finished upload-disk read after the upload entry access state is known.
 */
inline UploadDiskReadCompletionAction ClassifyUploadDiskReadCompletion(UploadQueueEntryAccessState entryAccessState, bool bReadError)
{
	if (entryAccessState != uploadQueueEntryLive)
		return uploadDiskReadCompletionDiscard;

	return bReadError ? uploadDiskReadCompletionMarkIoError : uploadDiskReadCompletionSendPackets;
}
