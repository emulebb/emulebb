#pragma once

#include "SearchParamsWnd.h"
class CSearchFile;
class CClosableTabCtrl;


///////////////////////////////////////////////////////////////////////////////
// CSearchDlg frame

class CSearchDlg : public CFrameWnd
{
	DECLARE_DYNCREATE(CSearchDlg)

public:
	CSearchDlg();           // constructor used by dynamic creation
	CSearchResultsWnd *m_pwndResults;

	BOOL CreateWnd(CWnd *pParent);

	void Localize();
	void CreateMenus();

	void RemoveResult(const CSearchFile *pFile);

	bool DoNewEd2kSearch(SSearchParams *pParams);
	bool DoNewKadSearch(SSearchParams *pParams);
	void CancelEd2kSearch();
	void CancelKadSearch(UINT uSearchID);
	void SetNextSearchID(uint32 uNextID);
	void ProcessEd2kSearchLinkRequest(const CString &strSearchTerm)	{ m_wndParams.ProcessEd2kSearchLinkRequest(strSearchTerm); }

	bool CanSearchRelatedFiles() const;
	void SearchRelatedFiles(CPtrList &listFiles);

	void DownloadSelected();
	void DownloadSelected(bool bPaused);

	bool CanDeleteSearches() const;
	void DeleteSearch(uint32 nSearchID);
	void DeleteAllSearches();

	void LocalEd2kSearchEnd(UINT nCount, bool bMoreResultsAvailable);
	void AddEd2kSearchResults(UINT nCount);

	bool CreateNewTab(SSearchParams *pParams, bool bActiveIcon = true);
	SSearchParams* GetSearchParamsBySearchID(uint32 nSearchID);
	CClosableTabCtrl& GetSearchSelector() const;

	int GetSelectedCat();
	void UpdateCatTabs();
	void SaveAllSettings()					{ m_wndParams.SaveSettings(); }
	void ResetHistory()						{ m_wndParams.ResetHistory(); }

	void SetToolTipsDelay(UINT uDelay);
	void DeleteAllSearchListCtrlItems();

	BOOL IsSearchParamsWndVisible() const	{ return m_wndParams.IsWindowVisible(); }
	void OpenParametersWnd()				{ ShowControlBar(&m_wndParams, TRUE, TRUE); }
	void DockParametersWnd();

	void UpdateSearch(CSearchFile *pSearchFile);
	/** Toggles Search keyboard focus between the Name textbox and the results list. */
	bool ToggleSearchFocus();

protected:
	CSearchParamsWnd	m_wndParams;

	virtual BOOL PreTranslateMessage(MSG *pMsg);

	DECLARE_MESSAGE_MAP()
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg void OnSetFocus(CWnd *pOldWnd);
	afx_msg void OnClose();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg BOOL OnHelpInfo(HELPINFO*);
};
