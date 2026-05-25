#pragma once

#include "emule.h"
#include "DownloadQueue.h"
#include "KnownFileList.h"
#include "PartFile.h"
#include "SharedFileList.h"

/**
 * @brief Checks whether a part-file pointer is still owned by the active download queue.
 */
inline bool IsLivePartFilePointer(const CPartFile *file)
{
	return file != NULL && theApp.downloadqueue != NULL && theApp.downloadqueue->IsPartFile(file);
}

/**
 * @brief Checks whether a known-file pointer is still owned by an active file collection.
 */
inline bool IsLiveKnownFilePointer(const CKnownFile *file)
{
	if (file == NULL)
		return false;
	if (theApp.downloadqueue != NULL && theApp.downloadqueue->IsPartFile(file))
		return true;
	if (theApp.sharedfiles != NULL && theApp.sharedfiles->ContainsFilePointer(file))
		return true;
	return theApp.knownfiles != NULL && theApp.knownfiles->ContainsFilePointer(file);
}
