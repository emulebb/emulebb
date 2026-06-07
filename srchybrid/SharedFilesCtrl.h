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
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#pragma once
#include "MuleListCtrl.h"
#include "TitledMenu.h"
#include "ListCtrlItemWalk.h"
#include <vector>

class CSharedFileList;
class CKnownFile;
class CShareableFile;
class CDirectoryItem;
class CToolTipCtrlX;

class CSharedFilesCtrl : public CMuleListCtrl, public CListCtrlItemWalk
{
	DECLARE_DYNAMIC(CSharedFilesCtrl)
	friend class CSharedDirsTreeCtrl;

public:
	class CShareDropTarget: public COleDropTarget
	{
	public:
		CShareDropTarget();
		virtual	~CShareDropTarget();
		void	SetParent(CSharedFilesCtrl *pParent)	{ m_pParent = pParent; }

		DROPEFFECT	OnDragEnter(CWnd *pWnd, COleDataObject *pDataObject, DWORD dwKeyState, CPoint point);
		DROPEFFECT	OnDragOver(CWnd*, COleDataObject *pDataObject, DWORD, CPoint point);
		BOOL		OnDrop(CWnd*, COleDataObject *pDataObject, DROPEFFECT dropEffect, CPoint point);
		void		OnDragLeave(CWnd*);

	protected:
		CComPtr<IDropTargetHelper> m_piDropHelper;
		bool				m_bUseDnDHelper;
//		BOOL ReadHdropData (COleDataObject *pDataObject);
		CSharedFilesCtrl	*m_pParent;
	};

	CSharedFilesCtrl();
	virtual	~CSharedFilesCtrl();

	void	Init();
	void	SetToolTipsDelay(DWORD dwDelay);
	void	CreateMenus();
	/**
	 * @brief Binds the visible shared-files list to the current shared-file model on demand.
	 */
	void	EnsureModelBound();
	/**
	 * @brief Returns whether the control has already materialized the shared-file model into UI items.
	 */
	bool	IsModelBound() const							{ return m_bModelBound; }
	void	ReloadFileList();
	void	AddFile(const CShareableFile *file);
	void	RemoveFile(const CShareableFile *file, bool bDeletedFromDisk);
	void	UpdateFile(const CShareableFile *file, bool bUpdateFileSummary = true);
	void	UpdateFilesAfterPublishedED2KBatch();
	/**
	 * @brief Queues a routine Shared Files row refresh for the desktop UI refresh tick.
	 */
	void	QueueFileDisplayRefresh(const CShareableFile *file, bool bUpdateSelectedDetails = true);
	/**
	 * @brief Queues a coalesced Shared Files summary label refresh for the desktop UI refresh tick.
	 */
	void	QueueFilesCountRefresh();
	/**
	 * @brief Queues a coalesced published-state refresh for the desktop UI refresh tick.
	 */
	void	QueuePublishedFilesDisplayRefresh();
	/**
	 * @brief Flushes queued routine Shared Files UI refreshes.
	 */
	void	FlushDisplayRefreshes();
	/**
	 * @brief Queues one coalesced Shared Files list reload for startup-deferred hash intake.
	 */
	void	ScheduleStartupDeferredReload();
	/**
	 * @brief Runs one queued startup-deferred Shared Files list reload after shared hashing is idle.
	 */
	void	FlushStartupDeferredReload();
	virtual DWORD_PTR GetVirtualItemData(int iItem) const override;
	virtual int GetVirtualItemCount() const override;
	void	Localize();
	void	ShowFilesCount();
	void	ShowComments(CShareableFile *file);
	void	SetAICHHashing(INT_PTR nVal)				{ nAICHHashing = nVal; }
	void	ApplyAICHHashingCount(INT_PTR nVal);
	void	SetDirectoryFilter(CDirectoryItem *pNewFilter, bool bRefresh = true);
	bool	IsSelectionRestoreInProgress() const		{ return m_bSelectionRestoreInProgress; }
	void	SetSelectionRestoreInProgress(bool bInProgress)	{ m_bSelectionRestoreInProgress = bInProgress; }
	/**
	 * @brief Resolves one visible Shared Files row back to its backing file object.
	 */
	CShareableFile* GetFileByIndex(int iItem) const;
	/**
	 * @brief Resolves one visible row only when the backing object is still owned by a live file list.
	 */
	CShareableFile* GetLiveFileByIndex(int iItem) const;
	/**
	 * @brief Resolves one backing file object to its current visible row index, or `-1` when hidden.
	 */
	int FindFile(const CShareableFile *pFile);
	/**
	 * @brief Collects the currently selected Shared Files backing objects in visible-row order.
	 */
	void CollectSelectedFiles(CTypedPtrList<CPtrList, CShareableFile*> &rSelectedFiles) const;
	/**
	 * @brief Collects the current visible Shared Files backing objects and prunes stale rows first.
	 */
	void CollectVisibleFiles(CTypedPtrList<CPtrList, CShareableFile*> &rVisibleFiles);
	/**
	 * @brief Returns the single selected Shared Files object, or `NULL` when the selection is not singular.
	 */
	CShareableFile* GetSingleSelectedFile() const;
	virtual CObject* GetNextSelectableItem();
	virtual CObject* GetPrevSelectableItem();

protected:
	CTitledMenu		m_SharedFilesMenu;
	CTitledMenu		m_CollectionsMenu;
	CMenu			m_PrioMenu;
	bool			m_aSortBySecondValue[4];
	CImageList		m_ImageList;
	bool			m_bModelBound;
	CDirectoryItem	*m_pDirectoryFilter;
	volatile INT_PTR	nAICHHashing;
	CToolTipCtrlX	*m_pToolTip;
	CTypedPtrList<CPtrList, CShareableFile*>	liTempShareableFilesInDir;
	CShareableFile	*m_pHighlightedItem;
	CShareDropTarget m_ShareDropTarget;
	std::vector<CShareableFile*> m_aVisibleFiles;
	CMap<const CShareableFile*, const CShareableFile*, int, int> m_mapVisibleFileIndex;
	bool			m_bSelectionRestoreInProgress;
	bool			m_bStartupDeferredReloadPending;
	bool			m_bVisibleFilePrunePending;
	std::vector<const CShareableFile*> m_aPendingDisplayRefreshFiles;
	CMap<const CShareableFile*, const CShareableFile*, int, int> m_mapPendingDisplayRefreshFiles;
	bool			m_bPendingFullDisplayRefresh;
	bool			m_bPendingSelectedDetailsRefresh;
	bool			m_bPendingFilesCountRefresh;
	mutable bool	m_bKadSummaryCacheValid;
	mutable uint64	m_uKadSummaryModelGeneration;
	mutable uint64	m_uKadSummaryPublishStateGeneration;
	mutable ULONGLONG m_ullKadSummaryCacheTick;
	mutable time_t	m_tKadSummaryCacheBucket;
	mutable bool	m_bKadSummaryCacheConnected;
	mutable bool	m_bKadSummaryCacheFirewalled;
	mutable bool	m_bKadSummaryCacheRunning;
	mutable bool	m_bKadSummaryCacheUdpVerifiedOpen;
	mutable uint32	m_uKadSummaryCacheBuddyIP;
	mutable unsigned int m_uCachedPublishedKadCount;

	static int CALLBACK SortProc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort);
	void OpenFile(const CShareableFile *file);
	void ShowFileDialog(CTypedPtrList<CPtrList, CShareableFile*> &aFiles, UINT uInvokePage = 0);
	void SetAllIcons();
	void RebuildVisibleFileIndex();
	void UpdateVisibleFileIndexRange(int iStartIndex, int iEndIndex);
	void ClearVisibleFiles();
	void ApplyVisibleFileCount();
	void ClearPendingDisplayRefreshes();
	bool ApplyFileDisplayRefresh(const CShareableFile *file, bool bUpdateSelectedDetails);
	void ApplyPublishedFilesDisplayRefresh();
	bool IsLiveVisibleFilePointer(const CShareableFile *file) const;
	bool PruneStaleVisibleFiles();
	CObject* WalkToLiveVisibleFileItem(int iDirection);
	void ScheduleVisibleFilePrune();
	void FlushVisibleFilePrune();
	bool ShouldPreserveVirtualListState() const;
	bool ShouldDisplayFile(const CShareableFile *file) const;
	void SortVisibleFiles();
	void InsertVisibleFileByCurrentSort(CShareableFile *file);
	void AppendVisibleFile(CShareableFile *file, bool bUpdateIndex = true);
	bool HasActiveSortOrder() const;
	bool NeedsSortReposition(int iIndex) const;
	bool RepositionFileByCurrentSort(CShareableFile *file, int iIndex);
	int CompareVisibleFiles(const CShareableFile *item1, const CShareableFile *item2, LPARAM lParamSort) const;
	CString GetItemDisplayText(const CShareableFile *file, int iSubItem) const;
	CString FormatFilesCountText() const;
	bool HandleSortShortcut(UINT nChar);
	void SortByColumn(int iSubItem);
	bool IsFilteredOut(const CShareableFile *pKnownFile) const;
	bool IsSharedInKad(const CKnownFile *file) const;
	void AddShareableFiles(const CString &strFromDir);
	void CheckBoxClicked(int iItem);
	bool CheckBoxesEnabled() const;

	virtual BOOL OnCommand(WPARAM wParam, LPARAM);
	virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnContextMenu(CWnd*, CPoint point);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnGetDispInfo(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnGetInfoTip(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnNmDblClk(LPNMHDR, LRESULT *pResult);
	afx_msg void OnSysColorChange();
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg BOOL OnNMClick(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
};
