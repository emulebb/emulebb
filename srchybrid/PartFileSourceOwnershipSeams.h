//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.

#pragma once

class CUpDownClient;

struct PartFileSourceOwnershipState
{
	bool bKnownClient;
	bool bInPartFileSourceList;
	bool bInDownloadingSourceList;
	bool bInA4AFLists;
	bool bInUploadQueue;
	bool bInWaitingQueue;
	bool bInConnectingList;
	bool bSocketAttached;
	bool bDeadSourceTracked;
	bool bCallerOwnsDelete;
	bool bDeleted;
	unsigned nDeleteCount;
};

enum class PartFileSourceAskOutcome
{
	SourceKeptAlive,
	CallerOwnsFailedSource
};

namespace PartFileSourceOwnershipSeams
{
/**
 * @brief Builds the normal owned-source shape before CPartFile::Process reasks it.
 */
inline PartFileSourceOwnershipState CreateLiveDownloadSourceState()
{
	PartFileSourceOwnershipState state = {};
	state.bKnownClient = true;
	state.bInPartFileSourceList = true;
	state.bInDownloadingSourceList = true;
	state.bInA4AFLists = true;
	state.bInUploadQueue = false;
	state.bInWaitingQueue = false;
	state.bInConnectingList = false;
	state.bSocketAttached = false;
	return state;
}

/**
 * @brief Reports whether any download-side owner still references the source.
 */
inline bool HasDownloadOwnerReferences(const PartFileSourceOwnershipState &state)
{
	return state.bInPartFileSourceList || state.bInDownloadingSourceList || state.bInA4AFLists;
}

/**
 * @brief Models CDownloadQueue::RemoveSource for the ownership references relevant to source deletion.
 */
inline void DetachSourceFromDownloadOwners(PartFileSourceOwnershipState &state)
{
	state.bInPartFileSourceList = false;
	state.bInDownloadingSourceList = false;
	state.bInA4AFLists = false;
}

void DetachFailedReaskSourceFromDownloadOwners(CUpDownClient *pClient);

/**
 * @brief Models a failed connection precheck inside TryToConnect/Disconnected.
 *
 * A false AskForDownload result means the caller owns the final delete. Before
 * that delete happens, Disconnected must already have removed all download-side
 * owner references so list/UI refresh paths cannot keep using the raw source.
 */
inline PartFileSourceAskOutcome ApplyImmediateTryToConnectFailure(PartFileSourceOwnershipState &state)
{
	DetachSourceFromDownloadOwners(state);
	state.bDeadSourceTracked = true;
	state.bInConnectingList = false;
	state.bSocketAttached = false;
	state.bCallerOwnsDelete = true;
	return PartFileSourceAskOutcome::CallerOwnsFailedSource;
}

/**
 * @brief Models the explicit delete performed by CPartFile::Process after AskForDownload returns false.
 */
inline void DeleteCallerOwnedFailedSource(PartFileSourceOwnershipState &state)
{
	ASSERT(state.bCallerOwnsDelete);
	ASSERT(!state.bDeleted);
	ASSERT(!HasDownloadOwnerReferences(state));

	state.bKnownClient = false;
	state.bInUploadQueue = false;
	state.bInWaitingQueue = false;
	state.bDeleted = true;
	++state.nDeleteCount;
}

/**
 * @brief Models the benign AskForDownload path where the source stays alive for a later retry.
 */
inline PartFileSourceAskOutcome KeepSourceForLaterRetry(PartFileSourceOwnershipState &state)
{
	state.bCallerOwnsDelete = false;
	return PartFileSourceAskOutcome::SourceKeptAlive;
}
}
