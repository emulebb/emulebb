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
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//GNU General Public License for more details.

#pragma once

#include <windows.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define EMULE_TEST_HAVE_WORKER_UI_MESSAGE_DELIVERY 1

namespace WorkerUiMessageSeams
{
	struct SPostedPayloadEntry
	{
		ULONG_PTR nOwnerKey = 0;
		void *pPayload = NULL;
		void (*pfnDestroy)(void *) = NULL;
	};

	inline std::atomic<ULONG_PTR>& GetNextPostedPayloadToken()
	{
		static std::atomic<ULONG_PTR> s_nNextPostedPayloadToken(1);
		return s_nNextPostedPayloadToken;
	}

	inline std::mutex& GetPostedPayloadMutex()
	{
		static std::mutex s_postedPayloadMutex;
		return s_postedPayloadMutex;
	}

	inline std::unordered_map<ULONG_PTR, SPostedPayloadEntry>& GetPostedPayloads()
	{
		static std::unordered_map<ULONG_PTR, SPostedPayloadEntry> s_postedPayloads;
		return s_postedPayloads;
	}

	inline std::unordered_multimap<ULONG_PTR, ULONG_PTR>& GetPostedPayloadOwnerIndex()
	{
		static std::unordered_multimap<ULONG_PTR, ULONG_PTR> s_postedPayloadOwnerIndex;
		return s_postedPayloadOwnerIndex;
	}

	inline void RemovePostedPayloadOwnerIndexEntryUnlocked(ULONG_PTR nOwnerKey, ULONG_PTR nToken)
	{
		std::unordered_multimap<ULONG_PTR, ULONG_PTR> &ownerIndex = GetPostedPayloadOwnerIndex();
		const auto range = ownerIndex.equal_range(nOwnerKey);
		for (auto it = range.first; it != range.second; ++it) {
			if (it->second == nToken) {
				ownerIndex.erase(it);
				break;
			}
		}
	}
}

/**
 * @brief Result category for handing an owned worker payload to a UI window.
 */
enum class EWorkerUiMessageDelivery
{
	Delivered,
	InvalidWindow,
	Failed
};

/**
 * @brief Delivery result plus the Win32 error captured on failed dispatch.
 */
struct SWorkerUiMessageDelivery
{
	EWorkerUiMessageDelivery eDelivery = EWorkerUiMessageDelivery::Failed;
	DWORD dwLastError = ERROR_SUCCESS;
};

/**
 * @brief Converts an owner object's stable address into the queue key used for deferred worker payload cleanup.
 */
inline ULONG_PTR GetWorkerUiPayloadOwnerKey(const void *pOwner)
{
	return reinterpret_cast<ULONG_PTR>(pOwner);
}

/**
 * @brief Reports whether the target UI owner has already entered teardown.
 */
inline bool IsWorkerUiTargetClosing(const std::atomic_bool *pbTargetClosing)
{
	return pbTargetClosing != NULL && pbTargetClosing->load(std::memory_order_acquire);
}

/**
 * @brief Validates that a worker thread may still queue a message for the target window.
 */
inline bool CanPostWorkerUiMessage(HWND hTargetWnd, const std::atomic_bool *pbTargetClosing = NULL)
{
	return !IsWorkerUiTargetClosing(pbTargetClosing) && hTargetWnd != NULL && ::IsWindow(hTargetWnd) != FALSE;
}

/**
 * @brief Posts a worker-owned notification only while the target window is still alive.
 */
inline bool TryPostWorkerUiMessage(HWND hTargetWnd, UINT uMessage, WPARAM wParam = 0, LPARAM lParam = 0, const std::atomic_bool *pbTargetClosing = NULL)
{
	return CanPostWorkerUiMessage(hTargetWnd, pbTargetClosing) && ::PostMessage(hTargetWnd, uMessage, wParam, lParam) != FALSE;
}

/**
 * @brief Classifies whether a worker result was accepted by a UI window.
 */
inline EWorkerUiMessageDelivery ClassifyWorkerUiMessageDelivery(const bool bWindowAvailable, const bool bDelivered) noexcept
{
	if (!bWindowAvailable)
		return EWorkerUiMessageDelivery::InvalidWindow;
	return bDelivered ? EWorkerUiMessageDelivery::Delivered : EWorkerUiMessageDelivery::Failed;
}

/**
 * @brief Posts an owned worker payload to the UI thread without blocking the worker.
 */
inline SWorkerUiMessageDelivery PostWorkerUiMessage(HWND hTargetWnd, UINT uMessage, WPARAM wParam = 0, LPARAM lParam = 0, const std::atomic_bool *pbTargetClosing = NULL)
{
	SWorkerUiMessageDelivery result;
	const bool bWindowAvailable = CanPostWorkerUiMessage(hTargetWnd, pbTargetClosing);
	if (!bWindowAvailable) {
		result.eDelivery = EWorkerUiMessageDelivery::InvalidWindow;
		result.dwLastError = ERROR_INVALID_WINDOW_HANDLE;
		return result;
	}

	::SetLastError(ERROR_SUCCESS);
	const bool bDelivered = ::PostMessage(hTargetWnd, uMessage, wParam, lParam) != FALSE;
	result.eDelivery = ClassifyWorkerUiMessageDelivery(true, bDelivered);
	result.dwLastError = bDelivered ? ERROR_SUCCESS : ::GetLastError();
	return result;
}

/**
 * @brief Sends an owned worker payload synchronously when the receiver must take ownership before the worker exits.
 */
inline SWorkerUiMessageDelivery SendWorkerUiMessage(HWND hTargetWnd, UINT uMessage, WPARAM wParam = 0, LPARAM lParam = 0)
{
	SWorkerUiMessageDelivery result;
	const bool bWindowAvailable = hTargetWnd != NULL && ::IsWindow(hTargetWnd) != FALSE;
	if (!bWindowAvailable) {
		result.eDelivery = EWorkerUiMessageDelivery::InvalidWindow;
		result.dwLastError = ERROR_INVALID_WINDOW_HANDLE;
		return result;
	}

	::SetLastError(ERROR_SUCCESS);
	const bool bDelivered = ::SendMessage(hTargetWnd, uMessage, wParam, lParam) != 0;
	result.eDelivery = ClassifyWorkerUiMessageDelivery(true, bDelivered);
	result.dwLastError = bDelivered ? ERROR_SUCCESS : ::GetLastError();
	return result;
}

/**
 * @brief Releases a queued worker payload with its original concrete type.
 */
template <typename TPayload>
inline void DestroyPostedWorkerUiPayload(void *pPayload)
{
	delete static_cast<TPayload*>(pPayload);
}

/**
 * @brief Removes and returns a queued worker payload after the UI thread consumes its wakeup message.
 */
template <typename TPayload>
inline std::unique_ptr<TPayload> TakePostedWorkerUiPayload(WPARAM wParam)
{
	const ULONG_PTR nToken = static_cast<ULONG_PTR>(wParam);

	void *pPayload = NULL;
	ULONG_PTR nOwnerKey = 0;
	{
		std::lock_guard<std::mutex> lock(WorkerUiMessageSeams::GetPostedPayloadMutex());
		std::unordered_map<ULONG_PTR, WorkerUiMessageSeams::SPostedPayloadEntry> &postedPayloads = WorkerUiMessageSeams::GetPostedPayloads();
		const auto it = postedPayloads.find(nToken);
		if (it == postedPayloads.end())
			return std::unique_ptr<TPayload>();

		pPayload = it->second.pPayload;
		nOwnerKey = it->second.nOwnerKey;
		postedPayloads.erase(it);
		WorkerUiMessageSeams::RemovePostedPayloadOwnerIndexEntryUnlocked(nOwnerKey, nToken);
	}

	return std::unique_ptr<TPayload>(static_cast<TPayload*>(pPayload));
}

/**
 * @brief Queues heap-owned worker payload data until the UI thread explicitly consumes it.
 */
template <typename TPayload>
inline bool TryPostWorkerUiPayloadMessage(HWND hTargetWnd, const std::atomic_bool *pbTargetClosing, ULONG_PTR nOwnerKey, UINT uMessage, std::unique_ptr<TPayload> pPayload)
{
	if (!pPayload)
		return false;
	if (!CanPostWorkerUiMessage(hTargetWnd, pbTargetClosing))
		return false;

	const ULONG_PTR nToken = WorkerUiMessageSeams::GetNextPostedPayloadToken().fetch_add(1, std::memory_order_relaxed);
	{
		std::lock_guard<std::mutex> lock(WorkerUiMessageSeams::GetPostedPayloadMutex());
		WorkerUiMessageSeams::GetPostedPayloads().emplace(nToken, WorkerUiMessageSeams::SPostedPayloadEntry{nOwnerKey, pPayload.release(), &DestroyPostedWorkerUiPayload<TPayload>});
		WorkerUiMessageSeams::GetPostedPayloadOwnerIndex().emplace(nOwnerKey, nToken);
	}

	if (!TryPostWorkerUiMessage(hTargetWnd, uMessage, static_cast<WPARAM>(nToken), 0, pbTargetClosing)) {
		std::unique_ptr<TPayload> pDroppedPayload = TakePostedWorkerUiPayload<TPayload>(static_cast<WPARAM>(nToken));
		(void)pDroppedPayload;
		return false;
	}

	return true;
}

/**
 * @brief Drops every queued payload still owned by a UI object that is being destroyed.
 */
inline void DiscardPostedWorkerUiPayloadsForOwner(ULONG_PTR nOwnerKey)
{
	std::vector<WorkerUiMessageSeams::SPostedPayloadEntry> queuedPayloads;
	{
		std::lock_guard<std::mutex> lock(WorkerUiMessageSeams::GetPostedPayloadMutex());
		std::unordered_map<ULONG_PTR, WorkerUiMessageSeams::SPostedPayloadEntry> &postedPayloads = WorkerUiMessageSeams::GetPostedPayloads();
		std::unordered_multimap<ULONG_PTR, ULONG_PTR> &ownerIndex = WorkerUiMessageSeams::GetPostedPayloadOwnerIndex();
		const auto range = ownerIndex.equal_range(nOwnerKey);
		for (auto it = range.first; it != range.second; ++it) {
			const auto payloadIt = postedPayloads.find(it->second);
			if (payloadIt != postedPayloads.end()) {
				queuedPayloads.push_back(payloadIt->second);
				postedPayloads.erase(payloadIt);
			}
		}
		ownerIndex.erase(range.first, range.second);
	}

	for (size_t i = 0; i < queuedPayloads.size(); ++i) {
		if (queuedPayloads[i].pfnDestroy != NULL)
			queuedPayloads[i].pfnDestroy(queuedPayloads[i].pPayload);
	}
}
