/*
	You may NOT modify this copyright message. You may add your name, if you
	changed or improved this code, but you mot not delete any part of this message or
	make it invisible etc.
*/
#include "stdafx.h"
#include "emule.h"
#include "CreditsDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CCreditsDlg dialog


CCreditsDlg::CCreditsDlg(CWnd *pParent /*=NULL*/)
	: CDialog(CCreditsDlg::IDD, pParent)
{
}

CCreditsDlg::~CCreditsDlg()
{
	m_imgSplash.DeleteObject();
}

BEGIN_MESSAGE_MAP(CCreditsDlg, CDialog)
	ON_WM_LBUTTONDOWN()
	ON_WM_PAINT()
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CCreditsDlg message handlers

/// <summary>
///
/// </summary>
/// <returns></returns>
BOOL CCreditsDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	VERIFY(m_imgSplash.Attach(theApp.LoadImage(_T("ABOUT"), _T("JPG"))));

	return TRUE;
}

void CCreditsDlg::OnPaint()
{
	if (m_imgSplash.GetSafeHandle()) {
		CDC dcMem;
		CPaintDC dc(this); // device context for painting

		if (dcMem.CreateCompatibleDC(&dc)) {
			CBitmap *pOldBM = dcMem.SelectObject(&m_imgSplash);
			BITMAP BM;
			m_imgSplash.GetBitmap(&BM);

			WINDOWPLACEMENT wp;
			GetWindowPlacement(&wp);
			wp.rcNormalPosition.right = wp.rcNormalPosition.left + BM.bmWidth;
			wp.rcNormalPosition.bottom = wp.rcNormalPosition.top + BM.bmHeight;
			SetWindowPlacement(&wp);

			dc.BitBlt(0, 0, BM.bmWidth, BM.bmHeight, &dcMem, 0, 0, SRCCOPY);
			dcMem.SelectObject(pOldBM);
		}
	}
}

BOOL CCreditsDlg::PreTranslateMessage(MSG *pMsg)
{
	switch (pMsg->message) {
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_NCLBUTTONDOWN:
	case WM_NCRBUTTONDOWN:
	case WM_NCMBUTTONDOWN:
		EndDialog(IDOK);
	}
	return CDialog::PreTranslateMessage(pMsg);
}
