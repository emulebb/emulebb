//this file is part of eMule
//Copyright (C)2003-2026 Merkur ( devs@emule-project.net / https://www.emule-project.net )
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
#include "stdafx.h"
#include "emule.h"
#include "PreviewDlg.h"
#include "OtherFunctions.h"
#include "SearchFile.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
HBITMAP ClonePreviewBitmap(HBITMAP hBitmap)
{
	if (hBitmap == NULL)
		return NULL;
	return static_cast<HBITMAP>(::CopyImage(hBitmap, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
}
}

IMPLEMENT_DYNAMIC(PreviewDlg, CDialog)

BEGIN_MESSAGE_MAP(PreviewDlg, CDialog)
	ON_BN_CLICKED(IDC_PV_EXIT, OnBnClickedPvExit)
	ON_BN_CLICKED(IDC_PV_NEXT, OnBnClickedPvNext)
	ON_BN_CLICKED(IDC_PV_PRIOR, OnBnClickedPvPrior)
END_MESSAGE_MAP()

PreviewDlg::PreviewDlg(CWnd *pParent /*=NULL*/)
	: CDialog(PreviewDlg::IDD, pParent)
	, m_localFrames()
	, m_strLocalTitle()
	, m_nCurrentImage()
	, m_icons()
{
}

PreviewDlg::~PreviewDlg()
{
	for (int i = m_localFrames.GetSize(); --i >= 0;)
		if (m_localFrames[i])
			::DeleteObject(m_localFrames[i]);
	DestroyIconsArr(m_icons, _countof(m_icons));
}

void PreviewDlg::DoDataExchange(CDataExchange *pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_PV_IMAGE, m_ImageStatic);
}

BOOL PreviewDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	if (m_localFrames.GetSize() == 0) {
		ASSERT(0);
		return FALSE;
	}
	InitWindowStyles(this);
	CString title(GetResNoAmp(IDS_DL_PREVIEW));
	title.AppendFormat(_T(": %s"), (LPCTSTR)m_strLocalTitle);
	SetWindowText(title);

	m_nCurrentImage = 0;
	ShowImage(0);

	m_icons[0] = theApp.LoadIcon(_T("Cancel"));
	static_cast<CButton*>(GetDlgItem(IDC_PV_EXIT))->SetIcon(m_icons[0]);
	m_icons[1] = theApp.LoadIcon(_T("Forward"));
	static_cast<CButton*>(GetDlgItem(IDC_PV_NEXT))->SetIcon(m_icons[1]);
	m_icons[2] = theApp.LoadIcon(_T("Back"));
	static_cast<CButton*>(GetDlgItem(IDC_PV_PRIOR))->SetIcon(m_icons[2]);
	return TRUE;
}

void PreviewDlg::SetFile(const CSearchFile *pFile)
{
	if (pFile == NULL) {
		ASSERT(0);
		return;
	}

	m_strLocalTitle = pFile->GetFileName();
	const CSimpleArray<HBITMAP> &previews = pFile->GetPreviews();
	for (int i = 0; i < previews.GetSize(); ++i) {
		HBITMAP hBitmap = ClonePreviewBitmap(previews[i]);
		if (hBitmap != NULL)
			m_localFrames.Add(hBitmap);
	}
	if (m_localFrames.GetSize() == 0)
		return;
	Show();
}

void PreviewDlg::SetLocalPreview(LPCTSTR pszTitle, HBITMAP hBitmap)
{
	m_strLocalTitle = pszTitle;
	m_localFrames.Add(hBitmap);
	Show();
}

int PreviewDlg::GetPreviewCount() const
{
	return m_localFrames.GetSize();
}

HBITMAP PreviewDlg::GetPreviewBitmap(int nNumber) const
{
	return m_localFrames[nNumber];
}

void PreviewDlg::ShowImage(int nNumber)
{
	int nImageCount = GetPreviewCount();
	if (nImageCount <= 0)
		return;
	if (nImageCount <= nNumber)
		nNumber = 0;
	else if (nNumber < 0)
		nNumber = nImageCount - 1;

	m_nCurrentImage = nNumber;
	m_ImageStatic.SetBitmap(GetPreviewBitmap(nNumber));

	CString strInfo;
	strInfo.Format(_T("Image %i of %i"), nNumber + 1, nImageCount);
	SetDlgItemText(IDC_PREVIEW_INFO, strInfo);
}

void PreviewDlg::Show()
{
	Create(IDD_PREVIEWDIALOG, NULL);
}

// PreviewDlg message handlers

void PreviewDlg::OnBnClickedPvExit()
{
	OnClose();
}

void PreviewDlg::OnBnClickedPvNext()
{
	ShowImage(m_nCurrentImage + 1);
}

void PreviewDlg::OnBnClickedPvPrior()
{
	ShowImage(m_nCurrentImage - 1);
}

void PreviewDlg::OnClose()
{
	m_ImageStatic.SetBitmap(NULL);
	CDialog::OnClose();
	delete this;
}
