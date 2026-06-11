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
#include "ResizableLib\ResizableFormView.h"
#include "SearchListCtrl.h"
#include "ButtonsTabCtrl.h"
#include "ClosableTabCtrl.h"
#include "DropDownButton.h"
#include "IconStatic.h"
#include "EditX.h"
#include "EditDelayed.h"
#include "ComboBoxEx2.h"
#include "ListCtrlEditable.h"

class CCustomAutoComplete;
class Packet;
class CSafeMemFile;
class CSearchParamsWnd;
struct SSearchParams;


///////////////////////////////////////////////////////////////////////////////
// CSearchResultsSelector

class CSearchResultsSelector : public CClosableTabCtrl
{
public:
	CSearchResultsSelector() = default;

protected:
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnContextMenu(CWnd*, CPoint point);
};

///////////////////////////////////////////////////////////////////////////////
// CSearchResultsWnd dialog

class CSearchResultsWnd : public CResizableFormView
{
	DECLARE_DYNCREATE(CSearchResultsWnd)

	enum
	{
		IDD = IDD_SEARCH
	};
	/** Detaches the currently visible results list before its backing search files are freed. */
	void	DetachActiveResultView(uint32 uSearchID);
	void	NoTabItems();

public:
	explicit CSearchResultsWnd(CWnd *pParent = NULL);   // standard constructor
	virtual	~CSearchResultsWnd();
	CSearchResultsWnd(const CSearchResultsWnd&) = delete;
	CSearchResultsWnd& operator=(const CSearchResultsWnd&) = delete;

	CSearchListCtrl searchlistctrl;
	CSearchResultsSelector searchselect;
	CStringArray m_astrFilter;
	CSearchParamsWnd *m_pwndParams;

	void	Localize();

	void	StartSearch(SSearchParams *pParams);
	/**
	 * @brief Starts a search without interactive message boxes so REST callers receive structured errors.
	 */
	bool	StartSearchFromApi(SSearchParams *pParams, CString &rError);
	bool	SearchMore();
	void	CancelSearch(uint32 uSearchID = 0);
	bool	IsSearchRunning(uint32 uSearchID) const;
	/** Returns true while a visible search is waiting for its serialized network launch slot. */
	bool	IsSearchQueued(uint32 uSearchID) const;

	bool	DoNewEd2kSearch(SSearchParams *pParams);
	void	CancelEd2kSearch();
	bool	IsLocalEd2kSearchRunning() const	{ return m_uTimerLocalServer != 0; }
	bool	IsGlobalEd2kSearchRunning() const	{ return global_search_timer != 0; }
	void	LocalEd2kSearchEnd(UINT count, bool bMoreResultsAvailable);
	void	AddEd2kSearchResults(UINT count);
	void	SetNextSearchID(uint32 uNextID)		{ m_nNextSearchID = uNextID; m_nEd2kSearchID = uNextID; }
	uint32	GetNextSearchID()					{ return ++m_nNextSearchID; }

	bool	DoNewKadSearch(SSearchParams *pParams);
	void	CancelKadSearch(uint32 uSearchID)	{ SearchCancelled(uSearchID); }

	bool	CanSearchRelatedFiles() const;
	void	SearchRelatedFiles(CPtrList &listFiles);

	void	DownloadSelected();
	void	DownloadSelected(bool bPaused);

	bool	CanDeleteSearches() const			{ return searchselect.GetItemCount() > 0; }
	void	DeleteSearch(uint32 uSearchID);
	void	DeleteAllSearches();
	void	DeleteSelectedSearch();

	bool	CreateNewTab(SSearchParams *pParams, bool bActiveIcon = true);
	bool	SelectAdjacentSearchResultTab(int iDirection);
	/** Moves focus to the visible search results list when it can accept keyboard input. */
	bool	FocusResultsList();
	/** Refreshes the visible queued/searching/empty status for the active result tab. */
	void	RefreshSearchActivity();
	/** Re-applies the resizable layout after hidden/tray search-result changes. */
	void	RefreshResultLayout();
	void	ShowSearchSelector(bool visible);
	int		GetSelectedCat() const				{ return m_cattabs.GetCurSel(); }
	void	UpdateCatTabs();

	SSearchParams* GetSearchResultsParams(uint32 uSearchID) const;

	uint32	GetFilterColumn() const				{ return m_nFilterColumn; }

protected:
	CProgressCtrl searchprogress;
	CStatic		m_ctlSearchStatus;
	CBrush		m_brSearchStatusBackground;
	CHeaderCtrl m_ctlSearchListHeader;
	CEditDelayed m_ctlFilter;
	CButton		m_ctlOpenParamsWnd;
	CImageList	m_imlSearchResults;
	CButtonsTabCtrl	m_cattabs;
	CDropDownButton	m_btnSearchListMenu;
	Packet		*m_searchpacket;
	UINT_PTR	global_search_timer;
	UINT_PTR	m_uTimerLocalServer;
	UINT_PTR	m_uTimerSearchQueue;
	CTypedPtrList<CPtrList, SSearchParams*> m_queuedSearches;
	uint32		m_nNextSearchID;
	uint32		m_nEd2kSearchID;
	uint32		m_nFilterColumn;
	COLORREF	m_crSearchStatusBackground;
	unsigned	m_servercount;
	int			m_iSentMoreReq;
	bool		m_b64BitSearchPacket;
	bool		m_globsearch;
	bool		m_cancelled;

	bool StartNewSearch(SSearchParams *pParams);
	/** Adds a search to the serialized launch queue and creates its visible result tab. */
	bool QueueSearch(SSearchParams *pParams, CString &rError);
	/** Starts the next queued search on the network without taking ownership from the result tab. */
	bool StartQueuedSearch(SSearchParams *pParams, CString &rError);
	/** Resolves automatic search selection against the currently connected networks. */
	bool ResolveAutomaticSearchType(SSearchParams *pParams, CString &rError) const;
	/** Returns true when the next queued request must wait for active eD2K state to drain. */
	bool IsQueuedSearchStartBlocked(const SSearchParams *pParams) const;
	/** Releases at most one queued search into the network layer. */
	void ProcessSearchQueue();
	void ArmSearchQueueTimer(UINT uDelayMS);
	void DisarmSearchQueueTimer();
	bool RemoveQueuedSearch(uint32 uSearchID);
	void ClearQueuedSearches();
	void SearchStarted();
	void SearchCancelled(uint32 uSearchID);
	CString GetSearchActivityText(const SSearchParams *pParams) const;
	void PositionSearchStatusOverlay();
	void SetSearchProgressIndeterminate(bool bEnable);
	bool IsBoundedSearchProgressVisible(const SSearchParams *pParams) const;
	void ShowResults(const SSearchParams *pParams);
	/** Re-applies the current text + hide-state filter to the active results tab. */
	void ReapplyActiveResultFilter();
	void SetAllIcons();
	void SetSearchResultsIcon(uint32 uSearchID, int iImage);
	void SetActiveSearchResultsIcon(uint32 uSearchID);
	void SetInactiveSearchResultsIcon(uint32 uSearchID);


	virtual void OnInitialUpdate();
	virtual BOOL PreTranslateMessage(MSG *pMsg);
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
	virtual void DoDataExchange(CDataExchange *pDX);    // DDX/DDV support

	DECLARE_MESSAGE_MAP()
	afx_msg void OnDblClkSearchList(LPNMHDR, LRESULT *pResult);
	afx_msg void OnSelChangeTab(LPNMHDR, LRESULT *pResult);
	afx_msg void OnSelChangingTab(LPNMHDR, LRESULT *pResult);
	afx_msg LRESULT OnCloseTab(WPARAM wParam, LPARAM);
	afx_msg LRESULT OnDblClickTab(WPARAM wParam, LPARAM);
	afx_msg void OnDestroy();
	afx_msg void OnSysColorChange();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnBnClickedDownloadSelected();
	afx_msg void OnBnClickedClearAll();
	afx_msg void OnClose();
	afx_msg BOOL OnHelpInfo(HELPINFO*);
	afx_msg LRESULT OnIdleUpdateCmdUI(WPARAM, LPARAM);
	afx_msg void OnBnClickedOpenParamsWnd();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg LRESULT OnChangeFilter(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnFilterMenuExtend(WPARAM wParam, LPARAM lParam);
	afx_msg void OnSearchListMenuBtnDropDown(LPNMHDR, LRESULT*);
	afx_msg HBRUSH OnCtlColor(CDC *pDC, CWnd *pWnd, UINT nCtlColor);
};
