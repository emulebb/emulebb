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
#include "PartFilePreviewSeams.h"

class CPartFile;
class CKnownFile;
class CUpDownClient;

/**
 * Owns the result payload passed from the thumbnail worker back to the UI thread.
 */
struct VideoThumbnailResult_Struct
{
	CPartFile *pPartFile = NULL;
	HBITMAP hBitmap = NULL;
	CString strFileHash;
	CString strCachePath;
	uint64 ullCompletedSize = 0;
	PartFilePreviewSeams::EVideoThumbnailAttemptResult eResult = PartFilePreviewSeams::VTAR_NONE;
	DWORD dwErrorCode = ERROR_SUCCESS;
	DWORD dwProcessExitCode = 0;

	~VideoThumbnailResult_Struct()
	{
		if (hBitmap)
			::DeleteObject(hBitmap);
	}
};

/**
 * Loads a cached PNG thumbnail into a caller-owned bitmap handle.
 */
bool ReadVideoThumbnailBitmapFile(const CString &rstrPath, HBITMAP &rhBitmap);

/**
 * Owns FFmpeg-generated peer preview frames until the UI thread sends them.
 */
struct PeerPreviewResult_Struct
{
	CKnownFile *pFile = NULL;
	CUpDownClient *pSender = NULL;
	HBITMAP *imgResults = NULL;
	uint8 nImagesGrabbed = 0;

	~PeerPreviewResult_Struct()
	{
		for (int i = nImagesGrabbed; --i >= 0;)
			if (imgResults[i])
				::DeleteObject(imgResults[i]);
		delete[] imgResults;
	}

	void ReleaseFrames()
	{
		imgResults = NULL;
		nImagesGrabbed = 0;
	}
};

///////////////////////////////////////////////////////////////////////////////
// CVideoThumbnailThread

/**
 * Asks FFmpeg to render one preview thumbnail from the current part file.
 */
class CVideoThumbnailThread : public CWinThread
{
	DECLARE_DYNCREATE(CVideoThumbnailThread)

public:
	virtual	BOOL	InitInstance();
	virtual int		Run();
	void	SetValues(CPartFile *pPartFile, LPCTSTR pszFfmpegPath, HWND hNotifyWnd, LPCTSTR pszCachePath);

protected:
	CVideoThumbnailThread();

	CPartFile *m_pPartfile;
	CString m_strFfmpegPath;
	CString m_strTitle;
	CString m_strInputPath;
	CString m_strWorkingDirectory;
	CString m_strFileHash;
	CString m_strCachePath;
	HWND m_hNotifyWnd;
	uint64 m_ullFileSize;
	uint64 m_ullCompletedSize;
};

///////////////////////////////////////////////////////////////////////////////
// CPeerPreviewThread

/**
 * Uses FFmpeg to produce wire-compatible peer preview PNG frames.
 */
class CPeerPreviewThread : public CWinThread
{
	DECLARE_DYNCREATE(CPeerPreviewThread)

public:
	virtual	BOOL	InitInstance();
	virtual int		Run();
	void	SetValues(CKnownFile *pFile, CUpDownClient *pSender, LPCTSTR pszFfmpegPath, HWND hNotifyWnd);

protected:
	CPeerPreviewThread();

	CKnownFile *m_pFile;
	CUpDownClient *m_pSender;
	CString m_strFfmpegPath;
	CString m_strInputPath;
	CString m_strWorkingDirectory;
	CString m_strFileHash;
	HWND m_hNotifyWnd;
};


///////////////////////////////////////////////////////////////////////////////
// CPreviewApps

class CPreviewApps
{
public:
	CPreviewApps();

	static CString GetDefaultAppsFile();
	INT_PTR ReadAllApps();
	void RemoveAllApps();

	int GetAllMenuEntries(CMenu &rMenu, const CPartFile *file);
	void RunApp(CPartFile *file, UINT uMenuID) const;

	enum ECanPreviewRes
	{
		NotHandled,
		No,
		Yes
	};
	ECanPreviewRes CanPreview(const CPartFile *file);
	int GetPreviewApp(const CPartFile *file);
	bool Preview(CPartFile *file);

protected:
	struct SPreviewApp
	{
		SPreviewApp() = default;
		SPreviewApp(const SPreviewApp &rCopy)
		{
			*this = rCopy;
		}

		SPreviewApp& operator=(const SPreviewApp &rCopy)
		{
			strTitle = rCopy.strTitle;
			strCommand = rCopy.strCommand;
			strCommandArgs = rCopy.strCommandArgs;
			astrExtensions.Copy(rCopy.astrExtensions);
			ullMinStartOfFile = rCopy.ullMinStartOfFile;
			ullMinCompletedSize = rCopy.ullMinCompletedSize;
			return *this;
		}

		uint64 ullMinStartOfFile;
		uint64 ullMinCompletedSize;
		CString strTitle;
		CString strCommand;
		CString strCommandArgs;
		CStringArray astrExtensions;
	};
	CArray<SPreviewApp> m_aApps;
	time_t m_tDefAppsFileLastModified;
	CPartFile *m_pLastCheckedPartFile;
	SPreviewApp *m_pLastPartFileApp;

	void UpdateApps();

};

extern CPreviewApps thePreviewApps;

void ExecutePartFile(CPartFile *file, LPCTSTR pszCommand, LPCTSTR pszCommandArgs);
