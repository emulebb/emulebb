//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.

#include "stdafx.h"
#include "PartFileSourceOwnershipSeams.h"

#include "OtherFunctions.h"
#include "DownloadQueue.h"
#include "emule.h"

void PartFileSourceOwnershipSeams::DetachFailedReaskSourceFromDownloadOwners(CUpDownClient *pClient)
{
	if (pClient != NULL)
		theApp.downloadqueue->RemoveSource(pClient);
}
