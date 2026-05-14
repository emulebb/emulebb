//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( devs@emule-project.net / https://www.emule-project.net )
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
#include <atomic>

#include "ResizableLib\ResizableDialog.h"
#include "SharedFilesCtrl.h"
#include "SharedDirsTreeCtrl.h"
#include "SplitterControl.h"
#include "EditDelayed.h"
#include "ListViewWalkerPropertySheet.h"
#include "filedetaildlgstatistics.h"
#include "ED2kLinkDlg.h"
#include "ArchivePreviewDlg.h"
#include "FileInfoDialog.h"
#include "MetaDataDlg.h"

#ifndef EMULE_COMPILED_STARTUP_PROFILING
	#if defined(_DEBUG) || defined(EMULE_ENABLE_STARTUP_PROFILING)
	#define EMULE_COMPILED_STARTUP_PROFILING 1
	#else
	#define EMULE_COMPILED_STARTUP_PROFILING 0
	#endif
#endif

/////////////////////////////////////////////////////////////////////////////////////////////
// CSharedFileDetailsModelessSheet
class CSharedFileDetailsModelessSheet : public CListViewPropertySheet
{
	DECLARE_DYNAMIC(CSharedFileDetailsModelessSheet)

public:
	CSharedFileDetailsModelessSheet();
	CSharedFileDetailsModelessSheet(const CSharedFileDetailsModelessSheet&) = delete;
	CSharedFileDetailsModelessSheet& operator=(const CSharedFileDetailsModelessSheet&) = delete;

	virtual BOOL OnInitDialog();

	void SetFiles(CTypedPtrList<CPtrList, CShareableFile*> &aFiles);
	void Localize();
protected:
	CArchivePreviewDlg			m_wndArchiveInfo;
	CED2kLinkDlg				m_wndFileLink;
	CFileDetailDlgStatistics	m_wndStatistics;
	CFileInfoDialog				m_wndMediaInfo;
	CMetaDataDlg				m_wndMetaData;

	DECLARE_MESSAGE_MAP()
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg LRESULT OnDataChanged(WPARAM, LPARAM);
};

class CSharedFilesWnd : public CResizableDialog
{
	DECLARE_DYNAMIC(CSharedFilesWnd)

	enum
	{
		IDD = IDD_FILES
	};

public:
	explicit CSharedFilesWnd(CWnd *pParent = NULL);   // standard constructor
	virtual	~CSharedFilesWnd();

	virtual BOOL OnInitDialog();
	void Localize();
	void SetToolTipsDelay(DWORD dwDelay);
	/**
	 * @brief Reloads shared files unless active shared hashing requires deferring the reload.
	 */
	bool Reload(bool bForceTreeReload = false);
	uint32	GetFilterColumn() const				{ return m_nFilterColumn; }
	void OnVolumesChanged();
	void OnSingleFileShareStatusChanged();
	void ShowSelectedFilesDetails(bool bForce = false);
	void ShowDetailsPanel(bool bShow);
#if EMULE_COMPILED_STARTUP_PROFILING
	void OnStartupSharedFilesModelChanged();
	void OnStartupProfileStartupComplete();
#endif
	/**
	 * @brief Runs any full reload deferred while the shared-file hash worker was active.
	 */
	void OnSharedHashingDrained();
	/**
	 * @brief Returns the teardown flag used to reject worker-thread UI posts while this window is closing.
	 */
	const std::atomic_bool *GetWorkerUiClosingFlag() const	{ return &m_bWorkerUiClosing; }
	/**
	 * @brief Returns the owner key for queued worker-thread payload cleanup.
	 */
	ULONG_PTR GetWorkerUiPayloadOwnerKey() const			{ return reinterpret_cast<ULONG_PTR>(this); }
	/**
	 * @brief Refreshes reload affordance text/tooltips from the current hash state.
	 */
	void UpdateReloadButtonState();

	CSharedFilesCtrl sharedfilesctrl;
	CStringArray m_astrFilter;
	CSharedDirsTreeCtrl m_ctlSharedDirTree;

private:

	HICON icon_files;
	CSplitterControl m_wndSplitter;
	CEditDelayed	m_ctlFilter;
	CHeaderCtrl		m_ctlSharedListHeader;
	CToolTipCtrl	m_reloadToolTip;
	uint32			m_nFilterColumn;
	std::atomic_bool m_bWorkerUiClosing;
	bool			m_bDetailsVisible;
	bool			m_bSharedTreeInitialized;
#if EMULE_COMPILED_STARTUP_PROFILING
	bool			m_bStartupSharedTreePopulatedReported;
	bool			m_bStartupSharedModelPopulatedReported;
	bool			m_bStartupSharedFilesReadyReported;
	bool			m_bStartupSharedFilesHashingDoneReported;
#endif
	bool			m_bReloadToolTipCreated;
	bool			m_bDeferredFullReloadAfterHash;
	bool			m_bDeferredSharedFilesReloadAfterHash;
	CSharedFileDetailsModelessSheet	m_dlgDetails;

protected:
	void SetAllIcons();
	void DoResize(int iDelta);
	void EnsureSharedTreeInitialized();
#if EMULE_COMPILED_STARTUP_PROFILING
	void ReportStartupSharedFilesReadinessIfReady();
#endif
	/**
	 * @brief Reports whether a full shared-files reload would conflict with active shared hashing.
	 */
	bool IsSharedHashingReloadBlocked() const;
	/**
	 * @brief Records a pending reload to run once shared hashing drains.
	 */
	void DeferReloadUntilHashingDone(bool bForceTreeReload, bool bNotifyUser);
	/**
	 * @brief Applies and clears a reload deferred by active shared hashing.
	 */
	void RunDeferredReloadAfterHash();
	/**
	 * @brief Updates the reload button tooltip for idle or hash-busy state.
	 */
	void UpdateReloadToolTipText();

	virtual void DoDataExchange(CDataExchange *pDX);    // DDX/DDV support
	virtual BOOL PreTranslateMessage(MSG *pMsg);
	virtual LRESULT DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()
	afx_msg BOOL OnHelpInfo(HELPINFO*);
	afx_msg HBRUSH OnCtlColor(CDC *pDC, CWnd *pWnd, UINT nCtlColor);
	afx_msg LRESULT OnChangeFilter(WPARAM wParam, LPARAM lParam);
	afx_msg void OnBnClickedReloadSharedFiles();
	afx_msg void OnLvnItemActivateSharedFiles(LPNMHDR, LRESULT*);
	afx_msg void OnNmClickSharedFiles(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnStnDblClickFilesIco();
	afx_msg void OnSysColorChange();
	afx_msg void OnTvnSelChangedSharedDirsTree(LPNMHDR, LRESULT *pResult);
	afx_msg void OnShowWindow(BOOL bShow, UINT);
	afx_msg void OnDestroy();
	afx_msg void OnBnClickedSfHideshowdetails();
	afx_msg void OnLvnItemchangedSflist(LPNMHDR, LRESULT *pResult);
	afx_msg LRESULT OnAICHHashingCountChanged(WPARAM, LPARAM);
	afx_msg LRESULT OnMonitoredSharedDirectoryUpdate(WPARAM, LPARAM);
};
