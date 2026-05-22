#pragma once

#include <afxcmn.h>

class CLifecycleProgressDlg : public CDialog
{
public:
	CLifecycleProgressDlg(UINT uTitleStringId, UINT uHeaderStringId, CWnd *pParent = NULL);

	void SetPhase(UINT uPercent, const CString &strStep, const CString &strDetail, bool bMarquee);

protected:
	virtual BOOL OnInitDialog();

private:
	bool IsMarqueeEnabled() const;

	UINT m_uTitleStringId;
	UINT m_uHeaderStringId;
	CProgressCtrl m_ctrlProgress;
	bool m_bMarqueeEnabled;
};

void PumpLifecycleProgressMessages(CDialog *pDialog);
