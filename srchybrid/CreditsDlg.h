#pragma once

/////////////////////////////////////////////////////////////////////////////
// CCreditsDlg dialog

class CCreditsDlg : public CDialog
{
	enum
	{
		IDD = IDD_ABOUTBOX
	};

// Construction
public:
	explicit CCreditsDlg(CWnd *pParent = NULL);   // standard constructor
	~CCreditsDlg();

// Implementation
protected:
	virtual BOOL OnInitDialog();
	virtual void OnPaint();

	BOOL PreTranslateMessage(MSG *pMsg);
	// Generated message map functions
	//{{AFX_MSG(CCreditsDlg)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
private:
	CBitmap m_imgAbout;
};
