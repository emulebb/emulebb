//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.

#pragma once

#include <cstddef>
#include <list>

namespace UploadBandwidthThrottlerSeams
{
	/**
	 * @brief Returns the standard-list span the equal-share pass should rotate across.
	 */
	inline std::size_t CalculateEqualShareSlotLimit(std::size_t nStandardListSize, std::size_t nNominalShareSlots, std::size_t nQueuedSlotLimit)
	{
		const std::size_t nRequestedSlots = nNominalShareSlots > nQueuedSlotLimit
			? nNominalShareSlots
			: nQueuedSlotLimit;
		return nRequestedSlots < nStandardListSize ? nRequestedSlots : nStandardListSize;
	}

	/**
	 * @brief Removes every queued instance of a socket from a single control queue.
	 *
	 * @return true when the queue contained at least one matching entry.
	 */
	template <typename TSocket>
	inline bool RemoveSocketFromControlQueue(std::list<TSocket*> &queue, TSocket *socket)
	{
		const size_t nOriginalCount = queue.size();
		queue.remove(socket);
		return queue.size() != nOriginalCount;
	}

	/**
	 * @brief Removes a socket from every control-queue domain owned by the throttler.
	 *
	 * @return true when at least one queue entry was removed.
	 */
	template <typename TSocket>
	inline bool RemoveSocketFromAllControlQueues(std::list<TSocket*> &controlQueueFirst, std::list<TSocket*> &controlQueue,
		std::list<TSocket*> &tempControlQueueFirst, std::list<TSocket*> &tempControlQueue, TSocket *socket)
	{
		const bool bRemovedFromPriority = RemoveSocketFromControlQueue(controlQueueFirst, socket);
		const bool bRemovedFromNormal = RemoveSocketFromControlQueue(controlQueue, socket);
		const bool bRemovedFromTempPriority = RemoveSocketFromControlQueue(tempControlQueueFirst, socket);
		const bool bRemovedFromTempNormal = RemoveSocketFromControlQueue(tempControlQueue, socket);
		return bRemovedFromPriority || bRemovedFromNormal || bRemovedFromTempPriority || bRemovedFromTempNormal;
	}

	/**
	 * @brief Merges the pending producer queues into the live control queues while preserving priority order.
	 */
	template <typename TSocket>
	inline void MergePendingControlQueues(std::list<TSocket*> &controlQueueFirst, std::list<TSocket*> &controlQueue,
		std::list<TSocket*> &tempControlQueueFirst, std::list<TSocket*> &tempControlQueue)
	{
		controlQueueFirst.splice(controlQueueFirst.end(), tempControlQueueFirst);
		controlQueue.splice(controlQueue.end(), tempControlQueue);
	}

	/**
	 * @brief Pops the next control socket to send while preserving the priority queue order.
	 */
	template <typename TSocket>
	inline TSocket *PopNextControlSocket(std::list<TSocket*> &controlQueueFirst, std::list<TSocket*> &controlQueue)
	{
		if (!controlQueueFirst.empty()) {
			TSocket *pSocket = controlQueueFirst.front();
			controlQueueFirst.pop_front();
			return pSocket;
		}
		if (!controlQueue.empty()) {
			TSocket *pSocket = controlQueue.front();
			controlQueue.pop_front();
			return pSocket;
		}
		return nullptr;
	}

	/**
	 * @brief Clears every control-queue domain during throttler shutdown.
	 */
	template <typename TSocket>
	inline void ClearAllControlQueues(std::list<TSocket*> &controlQueueFirst, std::list<TSocket*> &controlQueue,
		std::list<TSocket*> &tempControlQueueFirst, std::list<TSocket*> &tempControlQueue)
	{
		controlQueueFirst.clear();
		controlQueue.clear();
		tempControlQueueFirst.clear();
		tempControlQueue.clear();
	}
}
