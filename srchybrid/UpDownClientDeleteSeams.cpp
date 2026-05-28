//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.

#include "stdafx.h"
#include "UpDownClientDeleteSeams.h"

#include "ClientList.h"
#include "DownloadQueue.h"
#include "emule.h"
#include "ListenSocket.h"
#include "PartFile.h"
#include "UploadQueue.h"
#include "UpdownClient.h"

void UpDownClientDeleteSeams::AssertReadyToDelete(const CUpDownClient *pClient, const TCHAR *pszContext)
{
#ifdef _DEBUG
	UNREFERENCED_PARAMETER(pszContext);
	ASSERT(pClient != NULL);
	if (pClient == NULL || theApp.IsClosing())
		return;

	ASSERT(pClient->socket == NULL);
	ASSERT(pClient->GetConnectingState() == CCS_NONE);
	ASSERT(pClient->GetRequestFile() == NULL);
	ASSERT(pClient->m_OtherRequests_list.IsEmpty());
	ASSERT(pClient->m_OtherNoNeeded_list.IsEmpty());

	if (theApp.clientlist != NULL)
		ASSERT(!theApp.clientlist->IsConnectingClient(pClient));
	if (theApp.downloadqueue != NULL)
		ASSERT(!theApp.downloadqueue->IsInList(pClient));
	if (theApp.uploadqueue != NULL) {
		ASSERT(!theApp.uploadqueue->IsDownloading(pClient));
		ASSERT(!theApp.uploadqueue->IsOnUploadQueue(const_cast<CUpDownClient*>(pClient)));
	}
#else
	UNREFERENCED_PARAMETER(pClient);
	UNREFERENCED_PARAMETER(pszContext);
#endif
}

bool UpDownClientDeleteSeams::TryToConnectOrDelete(CUpDownClient *pClient, const TCHAR *pszContext, const bool bIgnoreMaxCon, const bool bNoCallbacks)
{
	ASSERT(pClient != NULL);
	if (pClient == NULL)
		return false;
	if (pClient->TryToConnect(bIgnoreMaxCon, bNoCallbacks))
		return true;

	AssertReadyToDelete(pClient, pszContext);
	delete pClient;
	return false;
}

void UpDownClientDeleteSeams::AssertTemporarySourceReadyToDelete(const CUpDownClient *pClient, const CPartFile *pInitialRequestFile, const TCHAR *pszContext)
{
#ifdef _DEBUG
	UNREFERENCED_PARAMETER(pszContext);
	ASSERT(pClient != NULL);
	if (pClient == NULL || theApp.IsClosing())
		return;

	ASSERT(pClient->socket == NULL);
	ASSERT(pClient->GetConnectingState() == CCS_NONE);
	// Temporary source probes are born with a request file; the key lifetime
	// check is that no external owner has inserted the client into that file.
	ASSERT(pClient->GetRequestFile() == NULL || pClient->GetRequestFile() == pInitialRequestFile);
	ASSERT(pClient->m_OtherRequests_list.IsEmpty());
	ASSERT(pClient->m_OtherNoNeeded_list.IsEmpty());
	ASSERT(pInitialRequestFile == NULL || pInitialRequestFile->srclist.Find(const_cast<CUpDownClient*>(pClient)) == NULL);
	ASSERT(pInitialRequestFile == NULL || pInitialRequestFile->A4AFsrclist.Find(const_cast<CUpDownClient*>(pClient)) == NULL);

	if (theApp.clientlist != NULL) {
		ASSERT(!theApp.clientlist->ContainsClientPointer(pClient));
		ASSERT(!theApp.clientlist->IsConnectingClient(pClient));
	}
	if (theApp.downloadqueue != NULL)
		ASSERT(!theApp.downloadqueue->IsInList(pClient));
	if (theApp.uploadqueue != NULL) {
		ASSERT(!theApp.uploadqueue->IsDownloading(pClient));
		ASSERT(!theApp.uploadqueue->IsOnUploadQueue(const_cast<CUpDownClient*>(pClient)));
	}
#else
	UNREFERENCED_PARAMETER(pClient);
	UNREFERENCED_PARAMETER(pInitialRequestFile);
	UNREFERENCED_PARAMETER(pszContext);
#endif
}
