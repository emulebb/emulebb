#pragma once
#include "PreferenceToolTipHelper.h"

class CCustomAutoComplete;

class CPPgSecurity : public CPropertyPage
{
	DECLARE_DYNAMIC(CPPgSecurity)

	enum
	{
		IDD = IDD_PPG_SECURITY
	};

public:
	CPPgSecurity();

	void Localize();
	void DeleteDDB();

protected:
	CCustomAutoComplete *m_pacIPFilterURL;
	CPreferenceToolTipHelper m_toolTip;
	CString m_strUpdateUrlText;
	bool m_bAutoUpdate;
	UINT m_uPeriodDays;

	void LoadSettings();
	void UpdateToolTips();
	void UpdateIPFilterControls();
	void UpdateAutoUpdateControls();

	virtual void DoDataExchange(CDataExchange *pDX);    // DDX/DDV support
	virtual BOOL OnInitDialog();
	virtual BOOL OnApply();
	virtual BOOL PreTranslateMessage(MSG *pMsg);
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnSettingsChange()					{ SetModified(); }
	afx_msg void OnBnClickedIPFilterEnabled();
	afx_msg void OnReloadIPFilter();
	afx_msg void OnEditIPFilter();
	afx_msg void OnLoadIPFFromURL();
	afx_msg void OnEnChangeUpdateUrl();
	afx_msg LRESULT OnRestoreUpdateUrl(WPARAM, LPARAM);
	afx_msg void OnBnClickedAutoupdateIpfilter();
	afx_msg void OnEnChangeIpfilterperiod();
	afx_msg void OnDDClicked();
	afx_msg void OnHelp();
	afx_msg BOOL OnHelpInfo(HELPINFO*);
	afx_msg void OnDestroy();
	afx_msg void OnObfuscatedDisabledChange();
	afx_msg void OnObfuscatedRequestedChange();
};
