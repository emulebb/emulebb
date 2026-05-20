#pragma once

namespace SharedDirsTreeCtrlSeams
{
	/**
	 * @brief Returns true only after every MFC image-list drag-start step has succeeded.
	 */
	inline bool ShouldCommitDragStartState(const bool bHasDragImage, const bool bBeginDragSucceeded, const bool bDragEnterSucceeded)
	{
		return bHasDragImage && bBeginDragSucceeded && bDragEnterSucceeded;
	}
}
