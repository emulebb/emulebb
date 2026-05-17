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
#include "stdafx.h"
#include "emule.h"
#include "otherfunctions.h"
#include "Preferences.h"
#include "AddFriend.h"
#include "AddFriendInputSeams.h"
#include "FriendList.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


// CAddFriend dialog

IMPLEMENT_DYNAMIC(CAddFriend, CDialog)

BEGIN_MESSAGE_MAP(CAddFriend, CDialog)
	ON_BN_CLICKED(IDC_ADD, OnAddBtn)
END_MESSAGE_MAP()

CAddFriend::CAddFriend()
	: CDialog(CAddFriend::IDD)
	, m_pShowFriend()
	, m_icoWnd()
{
}

CAddFriend::~CAddFriend()
{
	if (m_icoWnd)
		VERIFY(::DestroyIcon(m_icoWnd));
}

void CAddFriend::DoDataExchange(CDataExchange *pDX)
{
	CDialog::DoDataExchange(pDX);
}

BOOL CAddFriend::OnInitDialog()
{
	CDialog::OnInitDialog();
	InitWindowStyles(this);
	Localize();
	if (m_pShowFriend) {
		SetIcon(m_icoWnd = theApp.LoadIcon(_T("ClientDetails")), FALSE);
		SendDlgItemMessage(IDC_IP, EM_SETREADONLY, TRUE);
		SendDlgItemMessage(IDC_PORT, EM_SETREADONLY, TRUE);
		SendDlgItemMessage(IDC_USERNAME, EM_SETREADONLY, TRUE);

		SetDlgItemInt(IDC_IP, m_pShowFriend->m_dwLastUsedIP, FALSE);
		SetDlgItemInt(IDC_PORT, m_pShowFriend->m_nLastUsedPort, FALSE);
		SetDlgItemText(IDC_USERNAME, m_pShowFriend->m_strName);
		SetDlgItemText(IDC_USERHASH, (m_pShowFriend->HasUserhash() ? md4str(m_pShowFriend->m_abyUserhash) : _T("")));

		if (m_pShowFriend->m_tLastSeen) {
			CTime t(m_pShowFriend->m_tLastSeen);
			SetDlgItemText(IDC_EDIT2, t.Format(thePrefs.GetDateTimeFormat()));
		}
		SetDlgItemText(IDC_AFKADID, GetResString(m_pShowFriend->HasKadID() ? IDS_KNOWN : IDS_UNKNOWN));
		/*if (m_pShowFriend->m_tLastChatted) {
			CTime t(m_pShowFriend->m_tLastChatted);
			SetDlgItemText(IDC_LAST_CHATTED, t.Format(thePrefs.GetDateTimeFormat()));
		}*/

		if (CWnd *pAddWnd = GetDlgItem(IDC_ADD))
			pAddWnd->ShowWindow(SW_HIDE);
	} else {
		SetIcon(m_icoWnd = theApp.LoadIcon(_T("AddFriend")), FALSE);
		/** Guard the editable nickname control so template drift degrades without crashing. */
		if (CEdit *pUserNameEdit = static_cast<CEdit*>(GetDlgItem(IDC_USERNAME)))
			pUserNameEdit->SetLimitText(thePrefs.GetMaxUserNickLength());
		SetDlgItemText(IDC_USERHASH, _T(""));
	}
	return TRUE;
}

void CAddFriend::Localize()
{
	SetWindowText(GetResString(m_pShowFriend ? IDS_DETAILS : IDS_ADDAFRIEND));
	SetDlgItemText(IDC_INFO1, GetResString(IDS_PAF_REQINFO));
	SetDlgItemText(IDC_INFO2, GetResString(IDS_PAF_MOREINFO));

	SetDlgItemText(IDC_ADD, GetResString(IDS_ADD));
	SetDlgItemText(IDCANCEL, GetResString(m_pShowFriend ? IDS_FD_CLOSE : IDS_CANCEL));

	SetDlgItemText(IDC_STATIC31, GetResString(IDS_CD_UNAME));
	SetDlgItemText(IDC_STATIC32, GetResString(IDS_CD_UHASH));
	SetDlgItemText(IDC_STATIC34, (m_pShowFriend ? GetResString(IDS_USERID) + _T(':') : GetResString(IDS_CD_UIP)));
	SetDlgItemText(IDC_STATIC35, GetResString(IDS_PORT) + _T(':'));
	SetDlgItemText(IDC_LAST_SEEN_LABEL, GetResString(IDS_LASTSEEN) + _T(':'));
	SetDlgItemText(IDC_AFKADIDLABEL, GetResString(IDS_KADID) + _T(':'));
	//SetDlgItemText(IDC_LAST_CHATTED_LABEL, GetResString(IDS_LASTCHATTED)+_T(':'));
}

void CAddFriend::OnAddBtn()
{
	if (!m_pShowFriend) {
		CString strAddress;
		CString strPort;
		CString strUserName;
		GetDlgItemText(IDC_IP, strAddress);
		GetDlgItemText(IDC_PORT, strPort);
		GetDlgItemText(IDC_USERNAME, strUserName);

		const AddFriendInputSeams::FriendInput input = AddFriendInputSeams::ParseFriendInput(strAddress, strPort, strUserName, thePrefs.GetMaxUserNickLength());
		if (input.Status != AddFriendInputSeams::FriendInputStatus::Valid) {
			LocMessageBox(IDS_ERR_NOVALIDFRIENDINFO, MB_OK, 0);
			GetDlgItem(input.Status == AddFriendInputSeams::FriendInputStatus::InvalidPort || input.Status == AddFriendInputSeams::FriendInputStatus::MissingPort ? IDC_PORT : IDC_IP)->SetFocus();
			return;
		}

		// why did we offer an edit control for entering the userhash but did not store it?
		;

		if (!theApp.friendlist->AddFriend(NULL, 0, input.NetworkOrderAddress, input.Port, 0, input.UserName, 0)) {
			LocMessageBox(IDS_WRN_FRIENDDUPLIPPORT, MB_OK, 0);
			GetDlgItem(IDC_IP)->SetFocus();
			return;
		}
	} else {
		// No "update" friend's data for now -- too much work to synchronize/update all
		// possible available related data in the client list...
	}

	OnCancel();
}
