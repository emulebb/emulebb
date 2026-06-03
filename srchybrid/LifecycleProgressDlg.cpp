//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.

#include "stdafx.h"
#include "LifecycleProgressDlg.h"
#include "OtherFunctions.h"
#include "resource.h"
#include "UserMsgs.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

CLifecycleProgressDlg::CLifecycleProgressDlg(UINT uTitleStringId, UINT uHeaderStringId, CWnd *pParent)
	: CDialog(IDD_SHUTDOWNPROGRESS, pParent)
	, m_uTitleStringId(uTitleStringId)
	, m_uHeaderStringId(uHeaderStringId)
	, m_ctrlProgress()
	, m_bMarqueeEnabled(false)
{
}

void CLifecycleProgressDlg::SetPhase(UINT uPercent, const CString &strStep, const CString &strDetail, bool bMarquee)
{
	if (GetSafeHwnd() == NULL)
		return;

	if (CWnd *pHeader = GetDlgItem(IDC_PROGRESS_HEADER))
		pHeader->SetWindowText(GetResString(m_uHeaderStringId));
	if (CWnd *pStep = GetDlgItem(IDC_SHUTDOWN_STEP))
		pStep->SetWindowText(strStep);
	if (CWnd *pDetail = GetDlgItem(IDC_SHUTDOWN_DETAIL))
		pDetail->SetWindowText(strDetail);
	if (bMarquee != IsMarqueeEnabled()) {
		m_ctrlProgress.SetMarquee(bMarquee ? TRUE : FALSE, 40);
		m_bMarqueeEnabled = bMarquee;
	}
	if (!bMarquee)
		m_ctrlProgress.SetPos(static_cast<int>(uPercent > 100u ? 100u : uPercent));
	UpdateWindow();
}

BOOL CLifecycleProgressDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	m_ctrlProgress.SubclassDlgItem(IDC_PROGRESS1, this);
	m_ctrlProgress.SetRange32(0, 100);
	m_ctrlProgress.SetPos(0);
	SetWindowText(GetResString(m_uTitleStringId));
	return TRUE;
}

bool CLifecycleProgressDlg::IsMarqueeEnabled() const
{
	return m_bMarqueeEnabled;
}

void PumpLifecycleProgressMessages(CDialog *pDialog)
{
	MSG msg = {};
	while (::PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE)) {
		if (msg.message == UM_STARTUP_NEXT_STAGE)
			break;
		if (!::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
			break;
		if (pDialog != NULL && pDialog->GetSafeHwnd() != NULL && pDialog->IsDialogMessage(&msg))
			continue;
		::TranslateMessage(&msg);
		::DispatchMessage(&msg);
	}
}
