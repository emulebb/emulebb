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
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#include "stdafx.h"
#include "eMule.h"
#include "FileInfoDialog.h"
#include "OtherFunctions.h"
#include "KnownFileMetadataSeams.h"
#include "PathHelpers.h"
#include "MediaInfo.h"
#include "PartFile.h"
#include "Preferences.h"
#include "UserMsgs.h"
#include "WorkerUiMessageSeams.h"
#include "SplitterControl.h"

#include "id3/tag.h"
#include "id3/misc_support.h"


#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


/////////////////////////////////////////////////////////////////////////////
// SMediaInfoThreadResult

struct SMediaInfoThreadResult
{
	~SMediaInfoThreadResult()
	{
		delete paMediaInfo;
	}
	CArray<SMediaInfo> *paMediaInfo = NULL;
	CStringA strInfo;
};

static CString BuildMediaInfoDllHint(MediaInfoDllSeams::EMediaInfoDllStatus eStatus)
{
	CString strInstFolder(PathHelpers::GetDirectoryPath(PathHelpers::GetModuleFilePath(theApp.m_hInstance)));
	CString strHint;
	strHint.Format(GetResString(IDS_MEDIAINFO_DLLMISSING), (LPCTSTR)strInstFolder);

	switch (eStatus) {
	case MediaInfoDllSeams::MediaInfoDll_Incompatible:
		strHint += _T("\r\nMediaInfo.dll was found, but eMule BB requires version 26.01 or newer.");
		break;
	case MediaInfoDllSeams::MediaInfoDll_BadExports:
		strHint += _T("\r\nA MediaInfo.dll candidate was found, but it does not expose the required MediaInfo API exports.");
		break;
	case MediaInfoDllSeams::MediaInfoDll_LoadFailed:
		strHint += _T("\r\nA MediaInfo.dll candidate was found, but Windows could not load it or one of its dependencies.");
		break;
	case MediaInfoDllSeams::MediaInfoDll_Missing:
		strHint += _T("\r\nLookup also checks the MediaInfo.dll path from Preferences > Tweaks, MediaInfo registry install paths, and %ProgramFiles%\\MediaInfo\\MEDIAINFO.DLL.");
		break;
	default:
		break;
	}
	return strHint;
}

/////////////////////////////////////////////////////////////////////////////
// CGetMediaInfoThread

class CGetMediaInfoThread : public CWinThread
{
	DECLARE_DYNCREATE(CGetMediaInfoThread)

protected:
	CGetMediaInfoThread()
		: m_hWndOwner()
		, m_hFont()
	{
	}

public:
	virtual BOOL InitInstance();
	virtual int	Run();
	void SetValues(HWND hWnd, const CSimpleArray<CObject*> *paFiles, HFONT hFont)
	{
		m_hWndOwner = hWnd;
		for (int i = 0; i < paFiles->GetSize(); ++i)
			m_aFiles.Add(static_cast<CShareableFile*>((*paFiles)[i]));
		m_hFont = hFont;
	}

private:
	bool GetMediaInfo(HWND hWndOwner, const CShareableFile *pFile, SMediaInfo *mi, bool bSingleFile);
	CSimpleArray<const CShareableFile*> m_aFiles;
	HWND m_hWndOwner;
	HFONT m_hFont;
};


/////////////////////////////////////////////////////////////////////////////
// CFileInfoDialog dialog

IMPLEMENT_DYNAMIC(CFileInfoDialog, CResizablePage)

BEGIN_MESSAGE_MAP(CFileInfoDialog, CResizablePage)
	ON_MESSAGE(UM_MEDIA_INFO_RESULT, OnMediaInfoResult)
	ON_MESSAGE(UM_DATA_CHANGED, OnDataChanged)
	ON_WM_DESTROY()
END_MESSAGE_MAP()

CFileInfoDialog::CFileInfoDialog()
	: CResizablePage(CFileInfoDialog::IDD)
	, m_paFiles()
	, m_bDataChanged()
	, m_bReducedDlg()
{
	m_strCaption = GetResString(IDS_CONTENT_INFO);
	m_psp.pszTitle = m_strCaption;
	m_psp.dwFlags |= PSP_USETITLE;
}

BOOL CFileInfoDialog::OnInitDialog()
{
	CWaitCursor curWait; // we may get quite busy here.
	CResizablePage::OnInitDialog();
	InitWindowStyles(this);

	if (!m_bReducedDlg) {
		AddAnchor(IDC_FILESIZE, TOP_LEFT, TOP_RIGHT);
		AddAnchor(IDC_FULL_FILE_INFO, TOP_LEFT, BOTTOM_RIGHT);

		m_fi.LimitText(_I32_MAX);
		m_fi.SendMessage(EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(3, 3));
		m_fi.SetAutoURLDetect();
		m_fi.SetEventMask(m_fi.GetEventMask() | ENM_LINK);
	} else {
		GetDlgItem(IDC_FILESIZE)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_FULL_FILE_INFO)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_FD_XI1)->ShowWindow(SW_HIDE);

		CRect rc;
		GetDlgItem(IDC_FILESIZE)->GetWindowRect(rc);
		int nDelta = rc.Height();

		CSplitterControl::ChangeHeight(GetDlgItem(IDC_GENERAL), -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_LENGTH), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FORMAT), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FD_XI3), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_VCODEC), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_VBITRATE), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_VWIDTH), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_VASPECT), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_VFPS), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FD_XI6), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FD_XI8), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FD_XI10), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FD_XI12), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_STATIC_LANGUAGE), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FD_XI4), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_ACODEC), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_ABITRATE), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_ACHANNEL), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_ASAMPLERATE), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_ALANGUAGE), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FD_XI5), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FD_XI9), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FD_XI7), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FD_XI14), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FD_XI13), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_FD_XI2), 0, -nDelta);
		CSplitterControl::ChangePos(GetDlgItem(IDC_STATICFI), 0, -nDelta);
	}

	// General Group
	AddAnchor(IDC_GENERAL, TOP_LEFT, TOP_RIGHT);

	// Video Group
	AddAnchor(IDC_FD_XI3, TOP_LEFT, TOP_CENTER);

	// Audio Group - Labels
	AddAnchor(IDC_FD_XI4, TOP_CENTER, TOP_RIGHT);
	AddAnchor(IDC_FD_XI6, TOP_CENTER, TOP_CENTER);
	AddAnchor(IDC_FD_XI8, TOP_CENTER, TOP_CENTER);
	AddAnchor(IDC_FD_XI10, TOP_CENTER, TOP_CENTER);
	AddAnchor(IDC_FD_XI12, TOP_CENTER, TOP_CENTER);
	AddAnchor(IDC_STATIC_LANGUAGE, TOP_CENTER, TOP_CENTER);

	// Audio Group - Values
	AddAnchor(IDC_ACODEC, TOP_CENTER, TOP_RIGHT);
	AddAnchor(IDC_ABITRATE, TOP_CENTER, TOP_RIGHT);
	AddAnchor(IDC_ACHANNEL, TOP_CENTER, TOP_RIGHT);
	AddAnchor(IDC_ASAMPLERATE, TOP_CENTER, TOP_RIGHT);
	AddAnchor(IDC_ALANGUAGE, TOP_CENTER, TOP_RIGHT);

	AddAllOtherAnchors();

	CResizablePage::UpdateData(FALSE);
	Localize();
	return TRUE;
}

void CFileInfoDialog::OnDestroy()
{
	// This property sheet's window may get destroyed and re-created several times although
	// the corresponding C++ class is kept -> explicitly reset ResizableLib state
	RemoveAllAnchors();

	CResizablePage::OnDestroy();
}

BOOL CFileInfoDialog::OnSetActive()
{
	if (!CResizablePage::OnSetActive())
		return FALSE;
	if (m_bDataChanged) {
		InitDisplay(GetResString(IDS_FSTAT_WAITING));

		CGetMediaInfoThread *pThread = (CGetMediaInfoThread*)AfxBeginThread(RUNTIME_CLASS(CGetMediaInfoThread), THREAD_PRIORITY_LOWEST, 0, CREATE_SUSPENDED);
		if (pThread) {
			pThread->SetValues(m_hWnd, m_paFiles, (HFONT)GetDlgItem(IDC_FD_XI1)->GetFont()->m_hObject);
			pThread->ResumeThread();
		}
		m_pFiles.RemoveAll();
		for (int i = m_paFiles->GetSize(); --i >= 0;)
			m_pFiles.Add((*m_paFiles)[i]);
		m_bDataChanged = false;
	}
	return TRUE;
}

LRESULT CFileInfoDialog::OnDataChanged(WPARAM, LPARAM)
{
	m_bDataChanged = true;
	return 1;
}

IMPLEMENT_DYNCREATE(CGetMediaInfoThread, CWinThread)

BOOL CGetMediaInfoThread::InitInstance()
{
	DbgSetThreadName("GetMediaInfo");
	InitThreadLocale();
	return TRUE;
}

int CGetMediaInfoThread::Run()
{
	(void)::CoInitialize(NULL);

	HWND hwndRE = ::CreateWindow(MSFTEDIT_CLASS, NULL, ES_MULTILINE | ES_READONLY | WS_DISABLED, 0, 0, 200, 200, NULL, NULL, NULL, NULL);
	ASSERT(hwndRE);
	if (hwndRE && m_hFont)
		::SendMessage(hwndRE, WM_SETFONT, (WPARAM)m_hFont, 0);

	CArray<SMediaInfo> *paMediaInfo = NULL;
	try {
		CRichEditStream re;
		re.Attach(hwndRE);
		re.LimitText(_I32_MAX);
		PARAFORMAT pf;
		pf.cbSize = (UINT)sizeof pf;
		if (re.GetParaFormat(pf)) {
			pf.dwMask |= PFM_TABSTOPS;
			pf.cTabCount = 1;
			pf.rgxTabs[0] = 3000;
			re.SetParaFormat(pf);
		}
		re.Detach();

		const int arcnt = m_aFiles.GetSize();
		paMediaInfo = new CArray<SMediaInfo>;
		for (int i = 0; i < arcnt; ++i) {
			SMediaInfo mi;
			mi.bOutputFileName = arcnt > 1;
			mi.strFileName = m_aFiles[i]->GetFileName();
			mi.strInfo.Attach(hwndRE);
			mi.strInfo.InitColors();
			if (!::IsWindow(m_hWndOwner) || !GetMediaInfo(m_hWndOwner, m_aFiles[i], &mi, (arcnt == 1))) {
				mi.strInfo.Detach();
				delete paMediaInfo;
				paMediaInfo = NULL;
				break;
			}
			mi.strInfo.Detach();
			paMediaInfo->Add(mi);
		}
	} catch (...) {
		ASSERT(0);
		delete paMediaInfo;
		paMediaInfo = NULL;
	}

	SMediaInfoThreadResult *pThreadRes = new SMediaInfoThreadResult;
	pThreadRes->paMediaInfo = paMediaInfo;
	CRichEditStream re;
	re.Attach(hwndRE);
	re.GetRTFText(pThreadRes->strInfo);
	re.Detach();
	VERIFY(DestroyWindow(hwndRE));

	// Keep media-info delivery synchronous: the result owns heap data that the dialog must
	// accept immediately, otherwise the worker remains responsible for deleting it.
	const SWorkerUiMessageDelivery delivery = SendWorkerUiMessage(
		m_hWndOwner, UM_MEDIA_INFO_RESULT, 0, (LPARAM)pThreadRes);
	if (delivery.eDelivery != EWorkerUiMessageDelivery::Delivered)
		delete pThreadRes;

	::CoUninitialize();
	return 0;
}

void CFileInfoDialog::InitDisplay(LPCTSTR pStr)
{
	SetDlgItemText(IDC_FORMAT, pStr);
	SetDlgItemText(IDC_FILESIZE, pStr);
	SetDlgItemText(IDC_LENGTH, pStr);
	SetDlgItemText(IDC_VCODEC, pStr);
	SetDlgItemText(IDC_VBITRATE, pStr);
	SetDlgItemText(IDC_VWIDTH, pStr);
	SetDlgItemText(IDC_VASPECT, pStr);
	SetDlgItemText(IDC_VFPS, pStr);
	SetDlgItemText(IDC_ACODEC, pStr);
	SetDlgItemText(IDC_ACHANNEL, pStr);
	SetDlgItemText(IDC_ASAMPLERATE, pStr);
	SetDlgItemText(IDC_ABITRATE, pStr);
	SetDlgItemText(IDC_ALANGUAGE, pStr);
	SetDlgItemText(IDC_FULL_FILE_INFO, _T(""));

}

LRESULT CFileInfoDialog::OnMediaInfoResult(WPARAM, LPARAM lParam)
{
	SetDlgItemText(IDC_FD_XI3, GetResString(IDS_VIDEO));
	SetDlgItemText(IDC_FD_XI4, GetResString(IDS_AUDIO));
	InitDisplay(_T("-"));

	SMediaInfoThreadResult *pThreadRes = (SMediaInfoThreadResult*)lParam;
	if (pThreadRes == NULL)
		return 1;
	CArray<SMediaInfo> *paMediaInfo = pThreadRes->paMediaInfo;
	if (paMediaInfo == NULL) {
		delete pThreadRes;
		return 1;
	}

	if (paMediaInfo->GetSize() != m_paFiles->GetSize()) {
		InitDisplay(_T(""));
		delete pThreadRes;
		return 1;
	}

	uint64 uTotalFileSize = 0;
	SMediaInfo ami;
	bool bDiffVideoStreamCount = false;
	bool bDiffVideoCompression = false;
	bool bDiffVideoWidth = false;
	bool bDiffVideoHeight = false;
	bool bDiffVideoFrameRate = false;
	bool bDiffVideoBitRate = false;
	bool bDiffVideoAspectRatio = false;
	bool bDiffAudioStreamCount = false;
	bool bDiffAudioCompression = false;
	bool bDiffAudioChannels = false;
	bool bDiffAudioSamplesPerSec = false;
	bool bDiffAudioAvgBytesPerSec = false;
	bool bDiffAudioLanguage = false;
	for (int i = 0; i < paMediaInfo->GetSize(); ++i) {
		SMediaInfo &mi = (*paMediaInfo)[i];

		mi.InitFileLength();
		uTotalFileSize += (uint64)mi.ulFileSize;
		if (i == 0)
			ami = mi;
		else {
			if (ami.strFileFormat != mi.strFileFormat)
				ami.strFileFormat.Empty();

			if (ami.strMimeType != mi.strMimeType)
				ami.strMimeType.Empty();

			ami.fFileLengthSec += mi.fFileLengthSec;
			ami.bFileLengthEstimated |= mi.bFileLengthEstimated;

			ami.fVideoLengthSec += mi.fVideoLengthSec;
			ami.bVideoLengthEstimated |= mi.bVideoLengthEstimated;
			if (ami.iVideoStreams == 0 && mi.iVideoStreams > 0 || ami.iVideoStreams > 0 && mi.iVideoStreams == 0) {
				if (ami.iVideoStreams == 0)
					ami.iVideoStreams = mi.iVideoStreams;
				bDiffVideoStreamCount = true;
				bDiffVideoCompression = true;
				bDiffVideoWidth = true;
				bDiffVideoHeight = true;
				bDiffVideoFrameRate = true;
				bDiffVideoBitRate = true;
				bDiffVideoAspectRatio = true;
			} else {
				bDiffVideoStreamCount |= (ami.iVideoStreams != mi.iVideoStreams);
				bDiffVideoCompression |= (ami.strVideoFormat != mi.strVideoFormat);
				bDiffVideoWidth |= (ami.video.bmiHeader.biWidth != mi.video.bmiHeader.biWidth);
				bDiffVideoHeight |= (ami.video.bmiHeader.biHeight != mi.video.bmiHeader.biHeight);
				bDiffVideoFrameRate |= (ami.fVideoFrameRate != mi.fVideoFrameRate);
				bDiffVideoBitRate |= (ami.video.dwBitRate != mi.video.dwBitRate);
				bDiffVideoAspectRatio |= (ami.fVideoAspectRatio != mi.fVideoAspectRatio);
			}

			ami.fAudioLengthSec += mi.fAudioLengthSec;
			ami.bAudioLengthEstimated |= mi.bAudioLengthEstimated;
			if (ami.iAudioStreams == 0 && mi.iAudioStreams > 0 || ami.iAudioStreams > 0 && mi.iAudioStreams == 0) {
				if (ami.iAudioStreams == 0)
					ami.iAudioStreams = mi.iAudioStreams;
				bDiffAudioStreamCount = true;
				bDiffAudioCompression = true;
				bDiffAudioChannels = true;
				bDiffAudioSamplesPerSec = true;
				bDiffAudioAvgBytesPerSec = true;
				bDiffAudioLanguage = true;
			} else {
				bDiffAudioStreamCount |= (ami.iAudioStreams != mi.iAudioStreams);
				bDiffAudioCompression |= (ami.strAudioFormat != mi.strAudioFormat);
				bDiffAudioChannels |= (ami.audio.nChannels != mi.audio.nChannels);
				bDiffAudioSamplesPerSec |= (ami.audio.nSamplesPerSec != mi.audio.nSamplesPerSec);
				bDiffAudioAvgBytesPerSec |= (ami.audio.nAvgBytesPerSec != mi.audio.nAvgBytesPerSec);
				bDiffAudioLanguage |= (ami.strAudioLanguage.CompareNoCase(mi.strAudioLanguage) != 0);
			}
		}
	}

	CString buffer(ami.strFileFormat);
	if (!ami.strMimeType.IsEmpty()) {
		if (!buffer.IsEmpty())
			buffer += _T("; MIME type=");
		buffer += ami.strMimeType;
	}
	SetDlgItemText(IDC_FORMAT, buffer);

	if (uTotalFileSize)
		SetDlgItemText(IDC_FILESIZE, CastItoXBytes(uTotalFileSize));

	if (ami.fFileLengthSec) {
		buffer = CastSecondsToHM((time_t)ami.fFileLengthSec);
		if (ami.bFileLengthEstimated)
			buffer.AppendFormat(_T(" (%s)"), (LPCTSTR)GetResString(IDS_ESTIMATED));
		SetDlgItemText(IDC_LENGTH, buffer);
	}

	if (ami.iVideoStreams) {
		if (!bDiffVideoStreamCount && ami.iVideoStreams > 1)
			SetDlgItemText(IDC_FD_XI3, GetResString(IDS_VIDEO) + _T(" #1"));

		SetDlgItemText(IDC_VCODEC, bDiffVideoCompression ? _T("") : ami.strVideoFormat);

		if (!bDiffVideoBitRate && ami.video.dwBitRate) {
			if (ami.video.dwBitRate == _UI32_MAX)
				buffer = _T("Variable");
			else
				buffer.Format(_T("%lu %s"), (ami.video.dwBitRate + SEC2MS(1) / 2) / SEC2MS(1), (LPCTSTR)GetResString(IDS_KBITSSEC));
			SetDlgItemText(IDC_VBITRATE, buffer);
		} else
			SetDlgItemText(IDC_VBITRATE, _T(""));

		if (!bDiffVideoWidth && ami.video.bmiHeader.biWidth && !bDiffVideoHeight && ami.video.bmiHeader.biHeight) {
			buffer.Format(_T("%i x %i"), abs(ami.video.bmiHeader.biWidth), abs(ami.video.bmiHeader.biHeight));
			SetDlgItemText(IDC_VWIDTH, buffer);
		} else
			SetDlgItemText(IDC_VWIDTH, _T(""));

		if (!bDiffVideoAspectRatio && ami.fVideoAspectRatio) {
			buffer.Format(_T("%.3f"), ami.fVideoAspectRatio);
			const CString &strAR(GetKnownAspectRatioDisplayString((float)ami.fVideoAspectRatio));
			if (!strAR.IsEmpty())
				buffer.AppendFormat(_T("  (%s)"), (LPCTSTR)strAR);
			SetDlgItemText(IDC_VASPECT, buffer);
		} else
			SetDlgItemText(IDC_VASPECT, _T(""));

		if (!bDiffVideoFrameRate && ami.fVideoFrameRate) {
			buffer.Format(_T("%.2f"), ami.fVideoFrameRate);
			SetDlgItemText(IDC_VFPS, buffer);
		} else
			SetDlgItemText(IDC_VFPS, _T(""));
	}

	if (ami.iAudioStreams) {
		if (!bDiffAudioStreamCount && ami.iAudioStreams > 1)
			SetDlgItemText(IDC_FD_XI4, GetResString(IDS_AUDIO) + _T(" #1"));

		SetDlgItemText(IDC_ACODEC, bDiffAudioCompression ? _T("") : ami.strAudioFormat);

		LPCTSTR pChan;
		if (!bDiffAudioChannels && ami.audio.nChannels) {
			switch (ami.audio.nChannels) {
			case 1:
				pChan = _T("1 (Mono)");
				break;
			case 2:
				pChan = _T("2 (Stereo)");
				break;
			case 5:
				pChan = _T("5.1 (Surround)");
				break;
			default:
				pChan = NULL;
				SetDlgItemInt(IDC_ACHANNEL, ami.audio.nChannels, FALSE);
			}
		} else
			pChan = _T("");
		if (pChan)
			SetDlgItemText(IDC_ACHANNEL, pChan);

		if (!bDiffAudioSamplesPerSec && ami.audio.nSamplesPerSec) {
			buffer.Format(_T("%.3f kHz"), ami.audio.nSamplesPerSec / 1000.0);
			SetDlgItemText(IDC_ASAMPLERATE, buffer);
		} else
			SetDlgItemText(IDC_ASAMPLERATE, _T(""));

		if (!bDiffAudioAvgBytesPerSec && ami.audio.nAvgBytesPerSec) {
			if (ami.audio.nAvgBytesPerSec == _UI32_MAX)
				buffer = _T("Variable");
			else
				buffer.Format(_T("%u %s"), (UINT)((ami.audio.nAvgBytesPerSec * 16ull + SEC2MS(1)) / SEC2MS(2)), (LPCTSTR)GetResString(IDS_KBITSSEC));
			SetDlgItemText(IDC_ABITRATE, buffer);
		} else
			SetDlgItemText(IDC_ABITRATE, _T(""));

		SetDlgItemText(IDC_ALANGUAGE, bDiffAudioLanguage ? _T("") : ami.strAudioLanguage);
	}

	if (!m_bReducedDlg) {
		m_fi.SetRTFText(pThreadRes->strInfo);
		DisableAutoSelect(m_fi);
	}

	delete pThreadRes;
	return 1;
}

//TODO Verify if next two functions are still broken in the latest id3lib codes

wchar_t* ID3_GetStringW(const ID3_Frame *frame, ID3_FieldID fldName)
{
	// ID3LIB BUG: Use the Unicode support of id3lib only if the tag is already
	// in Unicode format, thus avoiding couple of bugs with character conversion in
	// id3lib to get triggered.
	if (frame) {
		ID3_Field *fld = frame->GetField(fldName);
		if (fld	&& fld->GetEncoding() == ID3TE_UTF16) {
			unicode_t *text = NULL;
			size_t nText = fld->Size();
			text = new unicode_t[nText / sizeof(unicode_t) + 1];
			fld->Get(text, nText / sizeof(unicode_t));
			text[nText / sizeof(unicode_t)] = L'\0';
			for (unsigned int i = 0; i < nText / sizeof(unicode_t); ++i)
				text[i] = _byteswap_ushort(text[i]);
			return (wchar_t*)text;
		}
	}

	char *text = ID3_GetString(frame, fldName);
	CStringW wstr(text);
	delete[] text;
	wchar_t *pwsz = new wchar_t[wstr.GetLength() + 1];
	wcscpy(pwsz, wstr);
	return pwsz;
}

wchar_t* ID3_GetStringW(const ID3_Frame *frame, ID3_FieldID fldName, size_t nIndex)
{
	// Do not use 'ID3_FieldImpl::Get(unicode_t *buffer, size_t maxLength, size_t itemNum)'.
	// That function is broken in id3lib (the bug is in 'GetRawUnicodeTextItem')
	return (nIndex == 0) ? ID3_GetStringW(frame, fldName) : NULL;
}

bool CGetMediaInfoThread::GetMediaInfo(HWND hWndOwner, const CShareableFile *pFile, SMediaInfo *mi, bool bSingleFile)
{
	if (!pFile)
		return false;
	ASSERT(!pFile->GetFilePath().IsEmpty());

	bool bHasDRM = false;
	if (!pFile->IsPartFile() || static_cast<const CPartFile*>(pFile)->IsCompleteBDSafe(0, 1024)) {
		GetMimeType(pFile->GetFilePath(), mi->strMimeType);
		bHasDRM = GetDRM(pFile->GetFilePath());
		if (bHasDRM) {
			if (!mi->strInfo.IsEmpty())
				mi->strInfo << _T("\r\n");
			mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfRed);
			mi->strInfo.AppendFormat(GetResString(IDS_MEDIAINFO_DRMWARNING), (LPCTSTR)pFile->GetFileName());
			mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfDef);
		}
	}

	mi->ulFileSize = pFile->GetFileSize();

	bool bFoundHeader = false;
	if (pFile->IsPartFile()) {
		// Do *not* pass a part file which does not have the beginning of file to the following code.
		//	- The MP3 reading code will skip all 0-bytes from the beginning of the file and may block
		//	  the main thread for a long time.
		//
		//	- The RIFF reading code will not work without the file header.
		//
		//	- Most (if not all) other code also will not work without the beginning of the file available.
		if (!static_cast<const CPartFile*>(pFile)->IsCompleteSafe(0, 16 * 1024))
			return bFoundHeader || !mi->strMimeType.IsEmpty();
	}

	const EED2KFileType eFileType = GetED2KFileTypeID(pFile->GetFileName());
	const bool bShouldInspectWithMediaInfo = thePrefs.GetInspectAllFileTypes()
		|| eFileType == ED2KFT_AUDIO
		|| eFileType == ED2KFT_VIDEO;
	MediaInfoDllSeams::EMediaInfoDllStatus eMediaInfoStatus = MediaInfoDllSeams::MediaInfoDll_NotInitialized;
	bool bGiveMediaInfoLibHint = false;

	if (bShouldInspectWithMediaInfo) {
		try {
			bFoundHeader = GetMediaInfoDllInfo(pFile->GetFilePath(), pFile->GetFileSize(), mi, true, bSingleFile, NULL, &eMediaInfoStatus);
		} catch (...) {
			ASSERT(0);
		}

		if (!::IsWindow(hWndOwner))
			return false;

		if (bFoundHeader)
			return true;

		bGiveMediaInfoLibHint = MediaInfoDllSeams::ShouldShowInstallHint(eMediaInfoStatus);
	}

	////////////////////////////////////////////////////////////////////////////
	// Check for AVI file
	//
	try {
		bool bIsAVI = false;
		if (GetRIFFHeaders(pFile->GetFilePath(), mi, bIsAVI, true))
			return true;
	} catch (...) {
		ASSERT(0);
	}

	if (!::IsWindow(hWndOwner))
		return false;

	////////////////////////////////////////////////////////////////////////////
	// Check for RM file
	//
	try {
		bool bIsRM = false;
		if (GetRMHeaders(pFile->GetFilePath(), mi, bIsRM, true))
			return true;
	} catch (...) {
		ASSERT(0);
	}

	if (!::IsWindow(hWndOwner))
		return false;

	////////////////////////////////////////////////////////////////////////////
	// Check for WM file
	//
#ifdef HAVE_WMSDK_H
	try {
		bool bIsWM = false;
		if (GetWMHeaders(pFile->GetFilePath(), mi, bIsWM, true))
			return true;
	} catch (...) {
		ASSERT(0);
	}

	if (!::IsWindow(hWndOwner))
		return false;
#endif//HAVE_WMSDK_H

	////////////////////////////////////////////////////////////////////////////
	// Check for MPEG Audio file
	//
	if (KnownFileMetadataSeams::IsMpegAudioMetadataExtension(pFile->GetFileName())) {
		try {
			// ID3LIB BUG: If there are ID3v2 _and_ ID3v1 tags available, id3lib
			// destroys (actually corrupts) the Unicode strings from ID3v2 tags due to
			// converting Unicode to ASCII and then conversion back from ASCII to Unicode.
			// To prevent this, we force the reading of ID3v2 tags only, in case there are
			// also ID3v1 tags available.
			// Won't do: id3lib still takes an ANSI path here, so Unicode/long-path coverage remains limited.
			ID3_Tag myTag;
			CStringA strFilePathA(pFile->GetFilePath());
			size_t id3Size = myTag.Link(strFilePathA, ID3TT_ID3V2);
			if (id3Size == 0) {
				myTag.Clear();
				myTag.Link(strFilePathA, ID3TT_ID3V1);
			}

			const Mp3_Headerinfo *mp3info;
			mp3info = myTag.GetMp3HeaderInfo();
			if (mp3info) {
				mi->strFileFormat = _T("MPEG audio");

				mi->OutputFileName();
				if (!bSingleFile) {
					mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfBold);
					mi->strInfo << _T("MP3 Header Info\n");
				}

				switch (mp3info->version) {
				case MPEGVERSION_2_5:
					mi->strAudioFormat = _T("MPEG-2.5,");
					mi->audio.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
					break;
				case MPEGVERSION_2:
					mi->strAudioFormat = _T("MPEG-2,");
					mi->audio.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
					break;
				case MPEGVERSION_1:
					mi->strAudioFormat = _T("MPEG-1,");
					mi->audio.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
					break;
				default:
					break;
				}
				mi->strAudioFormat += _T(' ');

				switch (mp3info->layer) {
				case MPEGLAYER_III:
					mi->strAudioFormat += _T("Layer 3");
					break;
				case MPEGLAYER_II:
					mi->strAudioFormat += _T("Layer 2");
					break;
				case MPEGLAYER_I:
					mi->strAudioFormat += _T("Layer 1");
					break;
				default:
					break;
				}
				if (!bSingleFile) {
					mi->strInfo << _T("   ") << GetResString(IDS_CODEC) << _T(":\t") << mi->strAudioFormat << _T("\n");
					//no vbr bit rate
					//mi->strInfo << _T("   ") << GetResString(IDS_BITRATE) << _T(":\t") << ((mp3info->vbr_bitrate ? mp3info->vbr_bitrate : mp3info->bitrate) + SEC2MS(1) / 2) / SEC2MS(1) << _T(" ") << GetResString(IDS_KBITSSEC) << _T("\n");
					mi->strInfo << _T("   ") << GetResString(IDS_BITRATE) << _T(":\t") << (mp3info->bitrate + SEC2MS(1) / 2) / SEC2MS(1) << _T(" ") << GetResString(IDS_KBITSSEC) << _T("\n");
					mi->strInfo << _T("   ") << GetResString(IDS_SAMPLERATE) << _T(":\t") << mp3info->frequency / SEC2MS(1.0) << _T(" kHz\n");
				}

				++mi->iAudioStreams;
				//no vbr bit rate
				//mi->audio.nAvgBytesPerSec = mp3info->vbr_bitrate ? mp3info->vbr_bitrate/8 : mp3info->bitrate/8;
				mi->audio.nAvgBytesPerSec = mp3info->bitrate / 8;
				mi->audio.nSamplesPerSec = mp3info->frequency;

				if (!bSingleFile)
					mi->strInfo << _T("   Mode:\t");
				switch (mp3info->channelmode) {
				case MP3CHANNELMODE_STEREO:
					if (!bSingleFile)
						mi->strInfo << _T("Stereo");
					mi->audio.nChannels = 2;
					break;
				case MP3CHANNELMODE_JOINT_STEREO:
					if (!bSingleFile)
						mi->strInfo << _T("Joint Stereo");
					mi->audio.nChannels = 2;
					break;
				case MP3CHANNELMODE_DUAL_CHANNEL:
					if (!bSingleFile)
						mi->strInfo << _T("Dual Channel");
					mi->audio.nChannels = 2;
					break;
				case MP3CHANNELMODE_SINGLE_CHANNEL:
					if (!bSingleFile)
						mi->strInfo << _T("Mono");
					mi->audio.nChannels = 1;
				}
				if (!bSingleFile)
					mi->strInfo << _T("\n");

				// length
				if (mp3info->time) {
					if (!bSingleFile) {
						mi->strInfo << _T("   ") << GetResString(IDS_LENGTH) << _T(":\t") << SecToTimeLength(mp3info->time);
						if (pFile->IsPartFile()) {
							mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfRed);
							mi->strInfo << _T(" (") << GetResString(IDS_ESTIMATED) << _T(")");
							mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfDef);
						}
						mi->strInfo << _T("\n");
					}
					mi->fAudioLengthSec = mp3info->time;
					mi->bAudioLengthEstimated |= pFile->IsPartFile();
				}

				bFoundHeader = true;
				if (mi->strMimeType.IsEmpty())
					mi->strMimeType = _T("audio/mpeg");
			}

			int iTag = 0;
			ID3_Tag::Iterator *iter = myTag.CreateIterator();
			for (const ID3_Frame *frame; (frame = iter->GetNext()) != NULL;) {
				if (iTag == 0) {
					if (mp3info && !bSingleFile)
						mi->strInfo << _T("\n");
					mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfBold);
					mi->strInfo << _T("MP3 Tags\n");
				}
				++iTag;

				LPCSTR desc = frame->GetDescription();
				if (!desc)
					desc = frame->GetTextID();

				CStringStream strFidInfo;
				ID3_FrameID eFrameID = frame->GetID();
				switch (eFrameID) {
				case ID3FID_ALBUM:
				case ID3FID_COMPOSER:
				case ID3FID_CONTENTTYPE:
				case ID3FID_COPYRIGHT:
				case ID3FID_DATE:
				case ID3FID_PLAYLISTDELAY:
				case ID3FID_ENCODEDBY:
				case ID3FID_LYRICIST:
				case ID3FID_FILETYPE:
				case ID3FID_TIME:
				case ID3FID_CONTENTGROUP:
				case ID3FID_TITLE:
				case ID3FID_SUBTITLE:
				case ID3FID_INITIALKEY:
				case ID3FID_LANGUAGE:
				case ID3FID_MEDIATYPE:
				case ID3FID_ORIGALBUM:
				case ID3FID_ORIGFILENAME:
				case ID3FID_ORIGLYRICIST:
				case ID3FID_ORIGARTIST:
				case ID3FID_ORIGYEAR:
				case ID3FID_FILEOWNER:
				case ID3FID_LEADARTIST:
				case ID3FID_BAND:
				case ID3FID_CONDUCTOR:
				case ID3FID_MIXARTIST:
				case ID3FID_PARTINSET:
				case ID3FID_PUBLISHER:
				case ID3FID_TRACKNUM:
				case ID3FID_RECORDINGDATES:
				case ID3FID_NETRADIOSTATION:
				case ID3FID_NETRADIOOWNER:
				case ID3FID_SIZE:
				case ID3FID_ISRC:
				case ID3FID_ENCODERSETTINGS:
				case ID3FID_YEAR:
					{
						wchar_t *sText = ID3_GetStringW(frame, ID3FN_TEXT);
						strFidInfo << CString(sText).Trim();
						delete[] sText;
					}
					break;
				case ID3FID_BPM:
					{
						wchar_t *sText = ID3_GetStringW(frame, ID3FN_TEXT);
						long lLength = _wtol(sText);
						if (lLength > 0) // check for != "0"
							strFidInfo << sText;
						delete[] sText;
					}
					break;
				case ID3FID_SONGLEN:
					{
						wchar_t *sText = ID3_GetStringW(frame, ID3FN_TEXT);
						long lLength = _wtol(sText) / 1000;
						if (lLength > 0)
							strFidInfo << SecToTimeLength(lLength);
						delete[] sText;
					}
					break;
				case ID3FID_USERTEXT:
					{
						wchar_t *sText = ID3_GetStringW(frame, ID3FN_TEXT);
						CString strText(sText);
						delete[] sText;
						if (!strText.Trim().IsEmpty()) {
							wchar_t *sDesc = ID3_GetStringW(frame, ID3FN_DESCRIPTION);
							CString strDesc(sDesc);
							delete[] sDesc;
							if (!strDesc.Trim().IsEmpty())
								strFidInfo << _T("(") << strDesc << _T("): ");

							strFidInfo << strText;
						}
					}
					break;
				case ID3FID_COMMENT:
				case ID3FID_UNSYNCEDLYRICS:
					{
						wchar_t *sText = ID3_GetStringW(frame, ID3FN_TEXT);
						CString strText(sText);
						delete[] sText;
						if (!strText.Trim().IsEmpty()) {
							wchar_t *sDesc = ID3_GetStringW(frame, ID3FN_DESCRIPTION);
							CString strDesc(sDesc);
							delete[] sDesc;
							if (strDesc.Trim() == _T("ID3v1 Comment"))
								strDesc.Empty();
							else if (!strDesc.IsEmpty())
								strFidInfo << _T("(") << strDesc << _T(")");

							wchar_t *sLang = ID3_GetStringW(frame, ID3FN_LANGUAGE);
							CString strLang(sLang);
							delete[] sLang;
							if (!strLang.Trim().IsEmpty())
								strFidInfo << _T("[") << strLang << _T("]");

							if (!strDesc.IsEmpty() || !strLang.IsEmpty())
								strFidInfo << _T(": ");
							strFidInfo << strText;
						}
					}
					break;
				case ID3FID_WWWAUDIOFILE:
				case ID3FID_WWWARTIST:
				case ID3FID_WWWAUDIOSOURCE:
				case ID3FID_WWWCOMMERCIALINFO:
				case ID3FID_WWWCOPYRIGHT:
				case ID3FID_WWWPUBLISHER:
				case ID3FID_WWWPAYMENT:
				case ID3FID_WWWRADIOPAGE:
					{
						wchar_t *sURL = ID3_GetStringW(frame, ID3FN_URL);
						strFidInfo << CString(sURL).Trim();
						delete[] sURL;
					}
					break;
				case ID3FID_WWWUSER:
					{
						wchar_t *sURL = ID3_GetStringW(frame, ID3FN_URL);
						CString strURL(sURL);
						delete[] sURL;
						if (!strURL.Trim().IsEmpty()) {
							wchar_t *sDesc = ID3_GetStringW(frame, ID3FN_DESCRIPTION);
							CString strDesc(sDesc);
							delete[] sDesc;
							if (!strDesc.Trim().IsEmpty())
								strFidInfo << _T("(") << strDesc << _T("): ");

							strFidInfo << strURL;
						}
					}
					break;
				case ID3FID_INVOLVEDPEOPLE:
					{
						size_t nItems = frame->GetField(ID3FN_TEXT)->GetNumTextItems();
						for (size_t nIndex = 0; nIndex < nItems; ++nIndex) {
							wchar_t *sPeople = ID3_GetStringW(frame, ID3FN_TEXT, nIndex);
							strFidInfo << sPeople;
							delete[] sPeople;
							if (nIndex + 1 < nItems)
								strFidInfo << _T(", ");
						}
					}
					break;
				case ID3FID_PICTURE:
					{
						wchar_t *sMimeType = ID3_GetStringW(frame, ID3FN_MIMETYPE);
						wchar_t *sDesc = ID3_GetStringW(frame, ID3FN_DESCRIPTION);
						wchar_t *sFormat = ID3_GetStringW(frame, ID3FN_IMAGEFORMAT);
						size_t nPicType = frame->GetField(ID3FN_PICTURETYPE)->Get();
						size_t nDataSize = frame->GetField(ID3FN_DATA)->Size();
						strFidInfo << _T("(") << sDesc << _T(")[") << sFormat << _T(", ")
							<< static_cast<UINT>(nPicType) << _T("]: ") << sMimeType << _T(", ")
							<< static_cast<UINT>(nDataSize) << _T(" bytes");
						delete[] sMimeType;
						delete[] sDesc;
						delete[] sFormat;
					}
					break;
				case ID3FID_GENERALOBJECT:
					{
						wchar_t *sMimeType = ID3_GetStringW(frame, ID3FN_MIMETYPE);
						wchar_t *sDesc = ID3_GetStringW(frame, ID3FN_DESCRIPTION);
						wchar_t *sFileName = ID3_GetStringW(frame, ID3FN_FILENAME);
						size_t
							nDataSize = frame->GetField(ID3FN_DATA)->Size();
						strFidInfo << _T("(") << sDesc << _T(")[")
							<< sFileName << _T("]: ") << sMimeType << _T(", ") << static_cast<UINT>(nDataSize) << _T(" bytes");
						delete[] sMimeType;
						delete[] sDesc;
						delete[] sFileName;
						break;
					}
				case ID3FID_UNIQUEFILEID:
					{
						wchar_t *sOwner = ID3_GetStringW(frame, ID3FN_OWNER);
						size_t nDataSize = frame->GetField(ID3FN_DATA)->Size();
						strFidInfo << sOwner << _T(", ") << static_cast<UINT>(nDataSize) << _T(" bytes");
						delete[] sOwner;
					}
					break;
				case ID3FID_PLAYCOUNTER:
					{
						size_t nCounter = frame->GetField(ID3FN_COUNTER)->Get();
						strFidInfo << static_cast<UINT>(nCounter);
					}
					break;
				case ID3FID_POPULARIMETER:
					{
						wchar_t *sEmail = ID3_GetStringW(frame, ID3FN_EMAIL);
						size_t nCounter = frame->GetField(ID3FN_COUNTER)->Get();
						size_t nRating = frame->GetField(ID3FN_RATING)->Get();
						strFidInfo << sEmail << _T(", counter=") << static_cast<UINT>(nCounter) << _T(" rating=") << static_cast<UINT>(nRating);
						delete[] sEmail;
					}
					break;
				case ID3FID_CRYPTOREG:
				case ID3FID_GROUPINGREG:
					{
						wchar_t *sOwner = ID3_GetStringW(frame, ID3FN_OWNER);
						size_t nSymbol = frame->GetField(ID3FN_ID)->Get();
						size_t nDataSize = frame->GetField(ID3FN_DATA)->Size();
						strFidInfo << _T("(") << static_cast<UINT>(nSymbol) << _T("): ") << sOwner << _T(", ") << static_cast<UINT>(nDataSize) << _T(" bytes");
					}
					break;
				case ID3FID_SYNCEDLYRICS:
					{
						wchar_t *sDesc = ID3_GetStringW(frame, ID3FN_DESCRIPTION);
						wchar_t *sLang = ID3_GetStringW(frame, ID3FN_LANGUAGE);
						//size_t nTimestamp = frame->GetField(ID3FN_TIMESTAMPFORMAT)->Get();
						size_t nRating = frame->GetField(ID3FN_CONTENTTYPE)->Get();
						//const char *format = (2 == nTimestamp) ? "ms" : "frames";
						strFidInfo << _T("(") << sDesc << _T(")[") << sLang << _T("]: ");
						switch (nRating) {
						case ID3CT_OTHER:
							strFidInfo << _T("Other");
							break;
						case ID3CT_LYRICS:
							strFidInfo << _T("Lyrics");
							break;
						case ID3CT_TEXTTRANSCRIPTION:
							strFidInfo << _T("Text transcription");
							break;
						case ID3CT_MOVEMENT:
							strFidInfo << _T("Movement/part name");
							break;
						case ID3CT_EVENTS:
							strFidInfo << _T("Events");
							break;
						case ID3CT_CHORD:
							strFidInfo << _T("Chord");
							break;
						case ID3CT_TRIVIA:
							strFidInfo << _T("Trivia/'pop up' information");
						}
						/*ID3_Field *fld = frame->GetField(ID3FN_DATA);
						if (fld) {
							ID3_MemoryReader mr(fld->GetRawBinary(), fld->BinSize());
							while (!mr.atEnd()) {
								strFidInfo << io::readString(mr).c_str();
								strFidInfo << _T(" [") << io::readBENumber(mr, sizeof(uint32)) << _T(" ")
									<< format << _T("] ");
							}
						}*/
						delete[] sDesc;
						delete[] sLang;
					}
					break;
				case ID3FID_AUDIOCRYPTO:
				case ID3FID_EQUALIZATION:
				case ID3FID_EVENTTIMING:
				case ID3FID_CDID:
				case ID3FID_MPEGLOOKUP:
				case ID3FID_OWNERSHIP:
				case ID3FID_PRIVATE:
				case ID3FID_POSITIONSYNC:
				case ID3FID_BUFFERSIZE:
				case ID3FID_VOLUMEADJ:
				case ID3FID_REVERB:
				case ID3FID_SYNCEDTEMPO:
				case ID3FID_METACRYPTO:
					//strFidInfo << _T(" (not implemented)");
					break;
				default:
					//strFidInfo << _T(" frame");
					break;
				}

				if (!strFidInfo.IsEmpty()) {
					mi->strInfo << _T("   ") << CString(desc) << _T(":");
					for (int iPos = 0; iPos >= 0;) {
						const CString &strFidInfoLine(strFidInfo.GetText().Tokenize(_T("\r\n"), iPos));
						if (!strFidInfoLine.IsEmpty())
							mi->strInfo << _T("\t") << strFidInfoLine << _T("\r\n");
					}
				}
			}
			delete iter;
		} catch (...) {
			ASSERT(0);
		}

		if (bFoundHeader) {
			mi->InitFileLength();
			return true;
		}
	}

	if (!::IsWindow(hWndOwner))
		return false;

	if (bShouldInspectWithMediaInfo) {
		try {
			bFoundHeader = GetFallbackMediaInfo(pFile->GetFilePath(), pFile->GetFileSize(), mi, true, bSingleFile);
		} catch (...) {
			ASSERT(0);
		}
	}

	if (!bFoundHeader && bGiveMediaInfoLibHint) {
		const CString strHint(BuildMediaInfoDllHint(eMediaInfoStatus));
		if (!mi->strInfo.IsEmpty())
			mi->strInfo << _T("\r\n");
		mi->strInfo << strHint;
	}

	return bFoundHeader || !mi->strMimeType.IsEmpty() || bHasDRM;
}

void CFileInfoDialog::DoDataExchange(CDataExchange *pDX)
{
	CResizablePage::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_FULL_FILE_INFO, m_fi);
}

void CFileInfoDialog::Localize()
{
	if (!m_hWnd)
		return;
	SetTabTitle(IDS_CONTENT_INFO, this);

	SetDlgItemText(IDC_GENERAL, GetResString(IDS_FD_GENERAL));
	SetDlgItemText(IDC_FD_XI2, GetResString(IDS_LENGTH) + _T(':'));
	SetDlgItemText(IDC_FD_XI3, GetResString(IDS_VIDEO));
	SetDlgItemText(IDC_FD_XI4, GetResString(IDS_AUDIO));
	SetDlgItemText(IDC_FD_XI5, GetResString(IDS_CODEC) + _T(':'));
	SetDlgItemText(IDC_FD_XI6, GetResString(IDS_CODEC) + _T(':'));
	SetDlgItemText(IDC_FD_XI7, GetResString(IDS_BITRATE) + _T(':'));
	SetDlgItemText(IDC_FD_XI8, GetResString(IDS_BITRATE) + _T(':'));
	CString sWH;
	sWH.Format(_T("%s x %s:"), (LPCTSTR)GetResString(IDS_WIDTH), (LPCTSTR)GetResString(IDS_HEIGHT));
	SetDlgItemText(IDC_FD_XI9, sWH);
	SetDlgItemText(IDC_FD_XI13, GetResString(IDS_FPS) + _T(':'));
	SetDlgItemText(IDC_FD_XI10, GetResString(IDS_CHANNELS) + _T(':'));
	SetDlgItemText(IDC_FD_XI12, GetResString(IDS_SAMPLERATE) + _T(':'));
	SetDlgItemText(IDC_STATICFI, GetResString(IDS_FILEFORMAT) + _T(':'));
	SetDlgItemText(IDC_FD_XI14, GetResString(IDS_ASPECTRATIO) + _T(':'));
	SetDlgItemText(IDC_STATIC_LANGUAGE, GetResString(IDS_PW_LANG) + _T(':'));
	if (!m_bReducedDlg)
		SetDlgItemText(IDC_FD_XI1, GetResString(IDS_FD_SIZE));
}
