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
#include "resource.h"
#include "Log.h"
#include "OtherFunctions.h"
#include "MediaInfo.h"
#include "MediaInfoDllSeams.h"
#include "PathHelpers.h"
#include "Preferences.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{

typedef enum MediaInfo_stream_t
{
	MediaInfo_Stream_General,
	MediaInfo_Stream_Video,
	MediaInfo_Stream_Audio,
	MediaInfo_Stream_Text,
	MediaInfo_Stream_Other,
	MediaInfo_Stream_Image,
	MediaInfo_Stream_Menu,
	MediaInfo_Stream_Max
} MediaInfo_stream_C;

typedef enum MediaInfo_info_t
{
	MediaInfo_Info_Name,
	MediaInfo_Info_Text,
	MediaInfo_Info_Measure,
	MediaInfo_Info_Options,
	MediaInfo_Info_Name_Text,
	MediaInfo_Info_Measure_Text,
	MediaInfo_Info_Info,
	MediaInfo_Info_HowTo,
	MediaInfo_Info_Max
} MediaInfo_info_C;

/** @brief Runtime loader for the optional MediaInfo.dll dependency. */
/**
 * @brief Runtime loader for the optional MediaInfo DLL used by the metadata pipeline.
 */
class CMediaInfoDLL
{
public:
	CMediaInfoDLL()
		: m_ullVersion()
		, m_hLib()
		, m_bInitialized()
		, m_initLock()
		, m_eStatus(MediaInfoDllSeams::MediaInfoDll_NotInitialized)
		, m_strStatusReason()
		, m_strLoadedPath()
		, m_pfnMediaInfo_New()
		, m_pfnMediaInfo_Open()
		, m_pfnMediaInfo_Close()
		, m_pfnMediaInfo_Delete()
		, m_pfnMediaInfo_Get()
		, m_pfnMediaInfo_GetI()
	{
	}

	~CMediaInfoDLL()
	{
		if (m_hLib != NULL)
			::FreeLibrary(m_hLib);
	}

	bool Initialize()
	{
		CSingleLock initLock(&m_initLock, TRUE);
		if (!m_bInitialized) {
			m_bInitialized = true;
			ResetLoadedLibrary();
			CString strSummaryReason(_T("no compatible version 26.01 or newer was found"));
			MediaInfoDllSeams::EMediaInfoDllStatus eSummaryStatus = MediaInfoDllSeams::MediaInfoDll_Missing;

			const CString strConfiguredPath(theApp.GetProfileString(_T("eMule"), _T("MediaInfo_MediaInfoDllPath"), _T("MEDIAINFO.DLL")));
			if (MediaInfoDllSeams::IsLoadingDisabled(strConfiguredPath)) {
				strSummaryReason = _T("loading disabled by configuration");
				m_eStatus = MediaInfoDllSeams::MediaInfoDll_Disabled;
				m_strStatusReason = strSummaryReason;
				AddLogLine(false, _T("MediaInfo.dll not loaded: %s"), (LPCTSTR)strSummaryReason);
				return false;
			}

			if (!strConfiguredPath.IsEmpty() && !::PathIsRelative(strConfiguredPath)) {
				CString strResolvedPath(strConfiguredPath);
				canonical(strResolvedPath);
				CString strReason;
				MediaInfoDllSeams::EMediaInfoDllStatus eStatus = MediaInfoDllSeams::MediaInfoDll_NotInitialized;
				ULONGLONG ullVersion = 0;
				HMODULE hConfiguredLib = LoadCompatibleLibrary(strResolvedPath, ullVersion, strReason, eStatus);
				if (hConfiguredLib != NULL) {
					BindLoadedLibrary(hConfiguredLib, ullVersion, strResolvedPath);
					LogCandidate(strResolvedPath, _T("selected configured path"), ullVersion);
				} else {
					RecordCandidateFailure(eSummaryStatus, strSummaryReason, eStatus, strReason);
					LogCandidate(strResolvedPath, strReason, ullVersion);
				}
			}

			if (m_hLib == NULL) {
				CStringArray aCandidatePaths;
				CollectCandidatePaths(strConfiguredPath, aCandidatePaths);
				for (INT_PTR i = 0; i < aCandidatePaths.GetCount(); ++i) {
					const CString &strCandidatePath = aCandidatePaths[i];
					CString strReason;
					MediaInfoDllSeams::EMediaInfoDllStatus eStatus = MediaInfoDllSeams::MediaInfoDll_NotInitialized;
					ULONGLONG ullVersion = 0;
					HMODULE hCandidateLib = LoadCompatibleLibrary(strCandidatePath, ullVersion, strReason, eStatus);
					if (hCandidateLib == NULL) {
						RecordCandidateFailure(eSummaryStatus, strSummaryReason, eStatus, strReason);
						LogCandidate(strCandidatePath, strReason, ullVersion);
						continue;
					}
					BindLoadedLibrary(hCandidateLib, ullVersion, strCandidatePath);
					LogCandidate(strCandidatePath, _T("selected compatible candidate"), ullVersion);
					break;
				}
			}

			if (m_hLib != NULL) {
				m_eStatus = MediaInfoDllSeams::MediaInfoDll_Loaded;
				m_strStatusReason.Empty();
				AddLogLine(false, _T("MediaInfo.dll loaded: %s (version %s)"), (LPCTSTR)m_strLoadedPath, (LPCTSTR)FormatVersionString(m_ullVersion));
			} else {
				m_eStatus = eSummaryStatus;
				m_strStatusReason = strSummaryReason;
				AddLogLine(false, _T("MediaInfo.dll not loaded: %s"), (LPCTSTR)strSummaryReason);
			}
		}
		return m_hLib != NULL;
	}

	MediaInfoDllSeams::EMediaInfoDllStatus GetStatus() const
	{
		return m_eStatus;
	}

	void* Open(LPCTSTR pszFilePath)
	{
		if (!m_pfnMediaInfo_New)
			return NULL;
		void *Handle = (*m_pfnMediaInfo_New)();
		if (Handle != NULL && !MediaInfoDllSeams::IsOpenSucceeded((*m_pfnMediaInfo_Open)(Handle, pszFilePath))) {
			Close(Handle);
			Handle = NULL;
		}
		return Handle;
	}

	void Close(void *Handle)
	{
		if (Handle == NULL)
			return;
		if (m_pfnMediaInfo_Delete)
			(*m_pfnMediaInfo_Delete)(Handle);
		else if (m_pfnMediaInfo_Close)
			(*m_pfnMediaInfo_Close)(Handle);
	}

	CString Get(void *Handle, MediaInfo_stream_C StreamKind, int StreamNumber, LPCTSTR pszParameter, MediaInfo_info_C KindOfInfo, MediaInfo_info_C KindOfSearch)
	{
		if (!m_pfnMediaInfo_Get)
			return CString();
		const wchar_t *pszValue = (*m_pfnMediaInfo_Get)(Handle, StreamKind, StreamNumber, pszParameter, KindOfInfo, KindOfSearch);
		return pszValue != NULL ? CString(pszValue) : CString();
	}

	CString GetI(void *Handle, MediaInfo_stream_C StreamKind, size_t StreamNumber, size_t iParameter, MediaInfo_info_C KindOfInfo)
	{
		if (!m_pfnMediaInfo_GetI)
			return CString();
		const wchar_t *pszValue = (*m_pfnMediaInfo_GetI)(Handle, StreamKind, StreamNumber, iParameter, KindOfInfo);
		return pszValue != NULL ? CString(pszValue) : CString();
	}

protected:
	/**
	 * Formats a module version value for diagnostic logging.
	 */
	static CString FormatVersionString(ULONGLONG ullVersion)
	{
		CString strVersion;
		strVersion.Format(_T("%u.%u.%u.%u")
			, (UINT)HIWORD(HIDWORD(ullVersion)), (UINT)LOWORD(HIDWORD(ullVersion))
			, (UINT)HIWORD(LODWORD(ullVersion)), (UINT)LOWORD(LODWORD(ullVersion)));
		return strVersion;
	}

	static CString CombinePath(LPCTSTR pszBasePath, LPCTSTR pszChildPath)
	{
		return PathHelpers::AppendPathComponent(CString(pszBasePath != NULL ? pszBasePath : _T("")), pszChildPath);
	}

	static CString GetAppFolder()
	{
		return PathHelpers::GetDirectoryPath(PathHelpers::GetModuleFilePath(theApp.m_hInstance));
	}

	static bool IsCompatibleVersion(ULONGLONG ullVersion)
	{
		return MediaInfoDllSeams::IsCompatibleVersion(ullVersion);
	}

	static int GetFailureRank(MediaInfoDllSeams::EMediaInfoDllStatus eStatus)
	{
		switch (eStatus) {
		case MediaInfoDllSeams::MediaInfoDll_LoadFailed:
			return 4;
		case MediaInfoDllSeams::MediaInfoDll_BadExports:
			return 3;
		case MediaInfoDllSeams::MediaInfoDll_Incompatible:
			return 2;
		case MediaInfoDllSeams::MediaInfoDll_Missing:
			return 1;
		default:
			return 0;
		}
	}

	static void RecordCandidateFailure(MediaInfoDllSeams::EMediaInfoDllStatus &reSummaryStatus, CString &rstrSummaryReason, MediaInfoDllSeams::EMediaInfoDllStatus eCandidateStatus, const CString &rstrCandidateReason)
	{
		if (GetFailureRank(eCandidateStatus) >= GetFailureRank(reSummaryStatus)) {
			reSummaryStatus = eCandidateStatus;
			rstrSummaryReason = rstrCandidateReason;
		}
	}

	void AddCandidatePath(CStringArray &raCandidatePaths, const CString &strCandidatePath)
	{
		if (strCandidatePath.IsEmpty())
			return;
		CString strNormalizedPath(strCandidatePath);
		canonical(strNormalizedPath);
		MediaInfoDllSeams::AddAbsoluteCandidatePath(raCandidatePaths, strNormalizedPath);
	}

	void AddRegistryInstallCandidate(HKEY hRootKey, CStringArray &raCandidatePaths)
	{
		CRegKey key;
		if (key.Open(hRootKey, _T("Software\\MediaInfo"), KEY_READ) != ERROR_SUCCESS)
			return;
		std::vector<TCHAR> aInstallPath(PathHelpers::kMaxDynamicPathChars, _T('\0'));
		ULONG ulChars = static_cast<ULONG>(aInstallPath.size());
		if (key.QueryStringValue(_T("Path"), aInstallPath.data(), &ulChars) != ERROR_SUCCESS)
			return;
		aInstallPath[aInstallPath.size() - 1] = _T('\0');
		AddCandidatePath(raCandidatePaths, CombinePath(aInstallPath.data(), _T("MEDIAINFO.DLL")));
	}

	void CollectCandidatePaths(const CString &strConfiguredPath, CStringArray &raCandidatePaths)
	{
		if (!strConfiguredPath.IsEmpty() && ::PathIsRelative(strConfiguredPath)) {
			const CString strAppFolder(GetAppFolder());
			if (!strAppFolder.IsEmpty())
				AddCandidatePath(raCandidatePaths, CombinePath(strAppFolder, strConfiguredPath));
		}
		AddRegistryInstallCandidate(HKEY_CURRENT_USER, raCandidatePaths);
		AddRegistryInstallCandidate(HKEY_LOCAL_MACHINE, raCandidatePaths);
		const CString strProgramFiles(ShellGetFolderPath(CSIDL_PROGRAM_FILES));
		if (!strProgramFiles.IsEmpty())
			AddCandidatePath(raCandidatePaths, CombinePath(strProgramFiles, _T("MediaInfo\\MEDIAINFO.DLL")));
	}

	HMODULE LoadCompatibleLibrary(const CString &strPath, ULONGLONG &rullVersion, CString &rstrReason, MediaInfoDllSeams::EMediaInfoDllStatus &reStatus)
	{
		rullVersion = 0;
		reStatus = MediaInfoDllSeams::MediaInfoDll_Missing;
		if (!LongPathSeams::PathExists(strPath)) {
			rstrReason = _T("candidate path missing");
			return NULL;
		}

		rullVersion = GetModuleVersion((LPCTSTR)strPath);
		if (!IsCompatibleVersion(rullVersion)) {
			if (rullVersion != 0)
				rstrReason.Format(_T("candidate version %s is below required 26.01"), (LPCTSTR)FormatVersionString(rullVersion));
			else
				rstrReason = _T("candidate version could not be determined");
			reStatus = MediaInfoDllSeams::MediaInfoDll_Incompatible;
			return NULL;
		}

		const CString strLoadPath(PreparePathForLongPath(strPath));
		HMODULE hCandidateLib = ::LoadLibraryEx(strLoadPath, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
		if (hCandidateLib == NULL) {
			rstrReason.Format(_T("LoadLibraryEx failed: %s"), (LPCTSTR)GetErrorMessage(::GetLastError()));
			reStatus = MediaInfoDllSeams::MediaInfoDll_LoadFailed;
			return NULL;
		}

		if (::GetProcAddress(hCandidateLib, "MediaInfo_New") == NULL
			|| ::GetProcAddress(hCandidateLib, "MediaInfo_Delete") == NULL
			|| ::GetProcAddress(hCandidateLib, "MediaInfo_Open") == NULL
			|| ::GetProcAddress(hCandidateLib, "MediaInfo_Close") == NULL
			|| ::GetProcAddress(hCandidateLib, "MediaInfo_Get") == NULL)
		{
			rstrReason = _T("required MediaInfo exports are missing");
			::FreeLibrary(hCandidateLib);
			reStatus = MediaInfoDllSeams::MediaInfoDll_BadExports;
			return NULL;
		}

		rstrReason = _T("candidate accepted");
		reStatus = MediaInfoDllSeams::MediaInfoDll_Loaded;
		return hCandidateLib;
	}

	void ResetLoadedLibrary()
	{
		m_ullVersion = 0;
		m_hLib = NULL;
		m_eStatus = MediaInfoDllSeams::MediaInfoDll_NotInitialized;
		m_strStatusReason.Empty();
		m_strLoadedPath.Empty();
		m_pfnMediaInfo_New = NULL;
		m_pfnMediaInfo_Open = NULL;
		m_pfnMediaInfo_Close = NULL;
		m_pfnMediaInfo_Delete = NULL;
		m_pfnMediaInfo_Get = NULL;
		m_pfnMediaInfo_GetI = NULL;
	}

	void BindLoadedLibrary(HMODULE hLib, ULONGLONG ullVersion, const CString &strPath)
	{
		m_hLib = hLib;
		m_ullVersion = ullVersion;
		m_strLoadedPath = strPath;
		(FARPROC &)m_pfnMediaInfo_New = ::GetProcAddress(m_hLib, "MediaInfo_New");
		(FARPROC &)m_pfnMediaInfo_Delete = ::GetProcAddress(m_hLib, "MediaInfo_Delete");
		(FARPROC &)m_pfnMediaInfo_Open = ::GetProcAddress(m_hLib, "MediaInfo_Open");
		(FARPROC &)m_pfnMediaInfo_Close = ::GetProcAddress(m_hLib, "MediaInfo_Close");
		(FARPROC &)m_pfnMediaInfo_Get = ::GetProcAddress(m_hLib, "MediaInfo_Get");
		(FARPROC &)m_pfnMediaInfo_GetI = ::GetProcAddress(m_hLib, "MediaInfo_GetI");
	}

	void LogCandidate(const CString &strPath, LPCTSTR pszStatus, ULONGLONG ullVersion) const
	{
		if (!thePrefs.GetVerbose())
			return;
		if (ullVersion != 0) {
			AddDebugLogLine(false, _T("MediaInfoDLL: %s [%s] version=%s"), (LPCTSTR)strPath, pszStatus
				, (LPCTSTR)FormatVersionString(ullVersion));
		} else {
			AddDebugLogLine(false, _T("MediaInfoDLL: %s [%s]"), (LPCTSTR)strPath, pszStatus);
		}
	}

	ULONGLONG m_ullVersion;
	HINSTANCE m_hLib;
	bool m_bInitialized;
	CCriticalSection m_initLock;
	MediaInfoDllSeams::EMediaInfoDllStatus m_eStatus;
	CString m_strStatusReason;
	CString m_strLoadedPath;

	void* (__stdcall *m_pfnMediaInfo_New)();
	int(__stdcall *m_pfnMediaInfo_Open)(void *Handle, const wchar_t *File);
	void(__stdcall *m_pfnMediaInfo_Close)(void *Handle);
	void(__stdcall *m_pfnMediaInfo_Delete)(void *Handle);
	const wchar_t* (__stdcall *m_pfnMediaInfo_Get)(void *Handle, MediaInfo_stream_C StreamKind, size_t StreamNumber, const wchar_t *Parameter, MediaInfo_info_C KindOfInfo, MediaInfo_info_C KindOfSearch);
	const wchar_t* (__stdcall *m_pfnMediaInfo_GetI)(void *Handle, MediaInfo_stream_C StreamKind, size_t StreamNumber, size_t Parameter, MediaInfo_info_C KindOfInfo);
};

CMediaInfoDLL theMediaInfoDLL;

/**
 * @brief RAII wrapper for one MediaInfo DLL parser handle.
 */
class CScopedMediaInfoHandle
{
public:
	explicit CScopedMediaInfoHandle(void *pHandle)
		: m_pHandle(pHandle)
	{
	}

	~CScopedMediaInfoHandle()
	{
		theMediaInfoDLL.Close(m_pHandle);
	}

	void* Get() const
	{
		return m_pHandle;
	}

private:
	CScopedMediaInfoHandle(const CScopedMediaInfoHandle &);
	CScopedMediaInfoHandle& operator=(const CScopedMediaInfoHandle &);

	void *m_pHandle;
};

static void WarnAboutWrongFileExtension(SMediaInfo *mi, LPCTSTR pszFileName, LPCTSTR pszExtensions)
{
	if (!mi->strInfo.IsEmpty())
		mi->strInfo << _T("\r\n");
	mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfRed);
	mi->strInfo.AppendFormat(GetResString(IDS_WARNING_WRONGFILEEXTENTION), pszFileName, pszExtensions);
	mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfDef);
}

static CString InfoGet(void *Handle, MediaInfo_stream_C StreamKind, int StreamNumber, LPCTSTR pszParameter)
{
	return theMediaInfoDLL.Get(Handle, StreamKind, StreamNumber, pszParameter, MediaInfo_Info_Text, MediaInfo_Info_Name);
}

static CString InfoGetI(void *Handle, MediaInfo_stream_C StreamKind, int StreamNumber, size_t iParameter, MediaInfo_info_C KindOfInfo)
{
	return theMediaInfoDLL.GetI(Handle, StreamKind, StreamNumber, iParameter, KindOfInfo);
}

static int InfoGetBoundedCount(void *Handle, MediaInfo_stream_C StreamKind, int StreamNumber, LPCTSTR pszParameter, int iMaxValue)
{
	return MediaInfoDllSeams::NormalizeReportedCount(_tstoi(InfoGet(Handle, StreamKind, StreamNumber, pszParameter)), iMaxValue);
}

}

/** @brief Extracts audio/video metadata through the optional MediaInfo.dll runtime. */
/**
 * @brief Populates `SMediaInfo` from the optional MediaInfo DLL and reports whether the DLL was usable.
 */
bool GetMediaInfoDllInfo(LPCTSTR pszFilePath, EMFileSize ullFileSize, SMediaInfo *mi, bool bFullInfo, bool bSingleFile, bool *pbLibraryAvailable, MediaInfoDllSeams::EMediaInfoDllStatus *peLibraryStatus)
{
	(void)bSingleFile;

	if (pbLibraryAvailable != NULL)
		*pbLibraryAvailable = false;
	if (peLibraryStatus != NULL)
		*peLibraryStatus = MediaInfoDllSeams::MediaInfoDll_NotInitialized;
	if (mi == NULL || pszFilePath == NULL || *pszFilePath == _T('\0'))
		return false;

	if (!theMediaInfoDLL.Initialize()) {
		if (peLibraryStatus != NULL)
			*peLibraryStatus = theMediaInfoDLL.GetStatus();
		return false;
	}
	if (pbLibraryAvailable != NULL)
		*pbLibraryAvailable = true;
	if (peLibraryStatus != NULL)
		*peLibraryStatus = MediaInfoDllSeams::MediaInfoDll_Loaded;

	const CString strPreparedPath(PreparePathForLongPath(CString(pszFilePath)));
	CScopedMediaInfoHandle mediaInfoHandle(theMediaInfoDLL.Open(strPreparedPath));
	void *Handle = mediaInfoHandle.Get();
	if (Handle == NULL)
		return false;

	bool bFoundHeader = false;
	{
		const LPCTSTR pCodec = _T("Format");
		const LPCTSTR pCodecInfo = _T("Format/Info");
		const LPCTSTR pCodecString = _T("Format/String");
		const LPCTSTR pLanguageInfo = _T("Language_More");
		CString strDisplayName(mi->strFileName);
		if (strDisplayName.IsEmpty())
			strDisplayName = ::PathFindFileName(pszFilePath);
		LPCTSTR pszExtension = ::PathFindExtension(strDisplayName);
		CString strExtension(&pszExtension[static_cast<int>(*pszExtension != _T('\0'))]);
		strExtension.MakeLower();

		mi->strFileFormat = InfoGet(Handle, MediaInfo_Stream_General, 0, _T("Format"));
		CString str(InfoGet(Handle, MediaInfo_Stream_General, 0, _T("Format/String")));
		if (!str.IsEmpty() && str != mi->strFileFormat)
			mi->strFileFormat.AppendFormat(_T(" (%s)"), (LPCTSTR)str);

		if (bFullInfo && !strExtension.IsEmpty()) {
			str = InfoGet(Handle, MediaInfo_Stream_General, 0, _T("Format/Extensions"));
			if (!str.IsEmpty()) {
				str.Remove(_T(')'));
				str.Remove(_T('('));
				str.MakeLower();

				bool bMatchedExtension = false;
				for (int iPos = 0; iPos >= 0;) {
					const CString &strFormatExtension(str.Tokenize(_T(" "), iPos));
					if (!strFormatExtension.IsEmpty() && strFormatExtension == strExtension) {
						bMatchedExtension = true;
						break;
					}
				}
				if (!bMatchedExtension)
					WarnAboutWrongFileExtension(mi, strDisplayName, str);
			}
		}

		mi->strTitle = InfoGet(Handle, MediaInfo_Stream_General, 0, _T("Title"));
		const CString &strTitleMore(InfoGet(Handle, MediaInfo_Stream_General, 0, _T("Title_More")));
		if (!strTitleMore.IsEmpty() && !mi->strTitle.IsEmpty() && strTitleMore != mi->strTitle)
			mi->strTitle.AppendFormat(_T("; %s"), (LPCTSTR)strTitleMore);
		mi->strAuthor = InfoGet(Handle, MediaInfo_Stream_General, 0, _T("Performer"));
		if (mi->strAuthor.IsEmpty())
			mi->strAuthor = InfoGet(Handle, MediaInfo_Stream_General, 0, _T("Author"));
		mi->strAlbum = InfoGet(Handle, MediaInfo_Stream_General, 0, _T("Album"));
		const CString &strCopyright(InfoGet(Handle, MediaInfo_Stream_General, 0, _T("Copyright")));
		CString strComments(InfoGet(Handle, MediaInfo_Stream_General, 0, _T("Comments")));
		if (strComments.IsEmpty())
			strComments = InfoGet(Handle, MediaInfo_Stream_General, 0, _T("Comment"));
		CString strDate(InfoGet(Handle, MediaInfo_Stream_General, 0, _T("Date")));
		if (strDate.IsEmpty())
			strDate = InfoGet(Handle, MediaInfo_Stream_General, 0, _T("Encoded_Date"));
		if (bFullInfo && (!mi->strTitle.IsEmpty() || !mi->strAuthor.IsEmpty() || !strCopyright.IsEmpty() || !strComments.IsEmpty() || !strDate.IsEmpty())) {
			if (!mi->strInfo.IsEmpty())
				mi->strInfo << _T("\n");
			mi->OutputFileName();
			mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfBold);
			mi->strInfo << GetResString(IDS_FD_GENERAL) << _T("\n");
			if (!mi->strTitle.IsEmpty())
				mi->strInfo << _T("   ") << GetResString(IDS_TITLE) << _T(":\t") << mi->strTitle << _T("\n");
			if (!mi->strAuthor.IsEmpty())
				mi->strInfo << _T("   ") << GetResString(IDS_AUTHOR) << _T(":\t") << mi->strAuthor << _T("\n");
			if (!strCopyright.IsEmpty())
				mi->strInfo << _T("   Copyright:\t") << strCopyright << _T("\n");
			if (!strComments.IsEmpty())
				mi->strInfo << _T("   ") << GetResString(IDS_COMMENT) << _T(":\t") << strComments << _T("\n");
			if (!strDate.IsEmpty())
				mi->strInfo << _T("   ") << GetResString(IDS_DATE) << _T(":\t") << strDate << _T("\n");
		}

		str = InfoGet(Handle, MediaInfo_Stream_General, 0, _T("Duration"));
		if (str.IsEmpty())
			str = InfoGet(Handle, MediaInfo_Stream_General, 0, _T("PlayTime"));
		double fFileLengthSec = _tstoi(str) / SEC2MS(1.0);
		UINT uAllBitrates = 0;

		int iVideoStreams = InfoGetBoundedCount(Handle, MediaInfo_Stream_General, 0, _T("VideoCount"), 64);
		if (iVideoStreams > 0) {
			mi->iVideoStreams = iVideoStreams;
			mi->fVideoLengthSec = fFileLengthSec;

			str = InfoGet(Handle, MediaInfo_Stream_Video, 0, pCodec);
			mi->strVideoFormat = str;
			if (!str.IsEmpty()) {
				CStringA strCodecA(str);
				if (MediaInfoDllSeams::CanReadFourCc(strCodecA))
					mi->video.bmiHeader.biCompression = *(LPDWORD)(LPCSTR)strCodecA;
			}
			str = InfoGet(Handle, MediaInfo_Stream_Video, 0, pCodecString);
			if (!str.IsEmpty() && str != mi->strVideoFormat)
				mi->strVideoFormat.AppendFormat(_T(" (%s)"), (LPCTSTR)str);

			str = InfoGet(Handle, MediaInfo_Stream_Video, 0, _T("Width"));
			mi->video.bmiHeader.biWidth = _tstoi(str);
			str = InfoGet(Handle, MediaInfo_Stream_Video, 0, _T("Height"));
			mi->video.bmiHeader.biHeight = _tstoi(str);
			str = InfoGet(Handle, MediaInfo_Stream_Video, 0, _T("FrameRate"));
			mi->fVideoFrameRate = _tstof(str);

			str = InfoGet(Handle, MediaInfo_Stream_Video, 0, _T("BitRate_Mode"));
			if (str.CompareNoCase(_T("VBR")) == 0) {
				mi->video.dwBitRate = _UI32_MAX;
				uAllBitrates = _UI32_MAX;
			} else {
				str = InfoGet(Handle, MediaInfo_Stream_Video, 0, _T("BitRate"));
				int iBitrate = _tstoi(str);
				mi->video.dwBitRate = iBitrate == -1 ? _UI32_MAX : iBitrate;
				if (iBitrate == -1)
					uAllBitrates = _UI32_MAX;
				else
					uAllBitrates += iBitrate;
			}

			str = InfoGet(Handle, MediaInfo_Stream_Video, 0, _T("AspectRatio"));
			mi->fVideoAspectRatio = _tstof(str);
			bFoundHeader = true;

			if (bFullInfo) {
				for (int iStream = 1; iStream < iVideoStreams; ++iStream) {
					if (!mi->strInfo.IsEmpty())
						mi->strInfo << _T("\n");
					mi->OutputFileName();
					mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfBold);
					mi->strInfo << GetResString(IDS_VIDEO) << _T(" #") << iStream + 1 << _T("\n");

					CString strVideoFormat(InfoGet(Handle, MediaInfo_Stream_Video, iStream, pCodec));
					str = InfoGet(Handle, MediaInfo_Stream_Video, iStream, pCodecString);
					if (!str.IsEmpty() && str != strVideoFormat)
						strVideoFormat.AppendFormat(_T(" (%s)"), (LPCTSTR)str);
					if (!strVideoFormat.IsEmpty())
						mi->strInfo << _T("   ") << GetResString(IDS_CODEC) << _T(":\t") << strVideoFormat << _T("\n");

					CString strBitrate;
					str = InfoGet(Handle, MediaInfo_Stream_Video, iStream, _T("BitRate_Mode"));
					if (str.CompareNoCase(_T("VBR")) == 0) {
						strBitrate = _T("Variable");
						uAllBitrates = _UI32_MAX;
					} else {
						str = InfoGet(Handle, MediaInfo_Stream_Video, iStream, _T("BitRate"));
						int iBitrate = _tstoi(str);
						if (iBitrate != 0) {
							if (iBitrate == -1) {
								strBitrate = _T("Variable");
								uAllBitrates = _UI32_MAX;
							} else {
								strBitrate.Format(_T("%u %s"), (iBitrate + SEC2MS(1u) / 2) / SEC2MS(1), (LPCTSTR)GetResString(IDS_KBITSSEC));
								if (uAllBitrates != _UI32_MAX)
									uAllBitrates += iBitrate;
							}
						}
					}
					if (!strBitrate.IsEmpty())
						mi->strInfo << _T("   ") << GetResString(IDS_BITRATE) << _T(":\t") << strBitrate << _T("\n");

					const CString &strWidth(InfoGet(Handle, MediaInfo_Stream_Video, iStream, _T("Width")));
					const CString &strHeight(InfoGet(Handle, MediaInfo_Stream_Video, iStream, _T("Height")));
					if (!strWidth.IsEmpty() && !strHeight.IsEmpty())
						mi->strInfo << _T("   ") << GetResString(IDS_WIDTH) << _T(" x ") << GetResString(IDS_HEIGHT) << _T(":\t") << strWidth << _T(" x ") << strHeight << _T("\n");

					str = InfoGet(Handle, MediaInfo_Stream_Video, iStream, _T("AspectRatio"));
					if (!str.IsEmpty())
						mi->strInfo << _T("   ") << GetResString(IDS_ASPECTRATIO) << _T(":\t") << str << _T("  (") << GetKnownAspectRatioDisplayString((float)_tstof(str)) << _T(")\n");

					str = InfoGet(Handle, MediaInfo_Stream_Video, iStream, _T("FrameRate"));
					if (!str.IsEmpty())
						mi->strInfo << _T("   ") << GetResString(IDS_FPS) << _T(":\t") << str << _T("\n");
				}
			}
		}

		int iAudioStreams = InfoGetBoundedCount(Handle, MediaInfo_Stream_General, 0, _T("AudioCount"), 64);
		if (iAudioStreams > 0) {
			mi->iAudioStreams = iAudioStreams;
			mi->fAudioLengthSec = fFileLengthSec;

			str = InfoGet(Handle, MediaInfo_Stream_Audio, 0, pCodec);
			if (_stscanf(str, _T("%hx"), &mi->audio.wFormatTag) != 1) {
				mi->strAudioFormat = str;
				str = InfoGet(Handle, MediaInfo_Stream_Audio, 0, pCodecString);
			} else {
				mi->strAudioFormat = InfoGet(Handle, MediaInfo_Stream_Audio, 0, pCodecString);
				str = InfoGet(Handle, MediaInfo_Stream_Audio, 0, pCodecInfo);
			}
			if (!str.IsEmpty() && str != mi->strAudioFormat)
				mi->strAudioFormat.AppendFormat(_T(" (%s)"), (LPCTSTR)str);

			str = InfoGet(Handle, MediaInfo_Stream_Audio, 0, _T("Channel(s)"));
			mi->audio.nChannels = (WORD)_tstoi(str);
			str = InfoGet(Handle, MediaInfo_Stream_Audio, 0, _T("SamplingRate"));
			mi->audio.nSamplesPerSec = _tstoi(str);

			str = InfoGet(Handle, MediaInfo_Stream_Audio, 0, _T("BitRate_Mode"));
			if (str.CompareNoCase(_T("VBR")) == 0) {
				mi->audio.nAvgBytesPerSec = _UI32_MAX;
				uAllBitrates = _UI32_MAX;
			} else {
				str = InfoGet(Handle, MediaInfo_Stream_Audio, 0, _T("BitRate"));
				int iBitrate = _tstoi(str);
				mi->audio.nAvgBytesPerSec = iBitrate == -1 ? _UI32_MAX : iBitrate / 8;
				if (iBitrate == -1)
					uAllBitrates = _UI32_MAX;
				else if (uAllBitrates != _UI32_MAX)
					uAllBitrates += iBitrate;
			}

			mi->strAudioLanguage = InfoGet(Handle, MediaInfo_Stream_Audio, 0, _T("Language/String"));
			if (mi->strAudioLanguage.IsEmpty())
				mi->strAudioLanguage = InfoGet(Handle, MediaInfo_Stream_Audio, 0, _T("Language"));
			bFoundHeader = true;

			if (bFullInfo) {
				for (int iStream = 1; iStream < iAudioStreams; ++iStream) {
					if (!mi->strInfo.IsEmpty())
						mi->strInfo << _T("\n");
					mi->OutputFileName();
					mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfBold);
					mi->strInfo << GetResString(IDS_AUDIO) << _T(" #") << iStream + 1 << _T("\n");

					CString strAudioFormat(InfoGet(Handle, MediaInfo_Stream_Audio, iStream, pCodec));
					str = InfoGet(Handle, MediaInfo_Stream_Audio, iStream, pCodecString);
					WORD wFormatTag = 0;
					if (_stscanf(str, _T("%hx"), &wFormatTag) == 1) {
						strAudioFormat = str;
						str = InfoGet(Handle, MediaInfo_Stream_Audio, iStream, pCodecInfo);
					}
					if (!str.IsEmpty() && str != strAudioFormat)
						strAudioFormat.AppendFormat(_T(" (%s)"), (LPCTSTR)str);
					if (!strAudioFormat.IsEmpty())
						mi->strInfo << _T("   ") << GetResString(IDS_CODEC) << _T(":\t") << strAudioFormat << _T("\n");

					CString strBitrate;
					str = InfoGet(Handle, MediaInfo_Stream_Audio, iStream, _T("BitRate_Mode"));
					if (str.CompareNoCase(_T("VBR")) == 0) {
						strBitrate = _T("Variable");
						uAllBitrates = _UI32_MAX;
					} else {
						str = InfoGet(Handle, MediaInfo_Stream_Audio, iStream, _T("BitRate"));
						int iBitrate = _tstoi(str);
						if (iBitrate != 0) {
							if (iBitrate == -1) {
								strBitrate = _T("Variable");
								uAllBitrates = _UI32_MAX;
							} else {
								strBitrate.Format(_T("%u %s"), (iBitrate + SEC2MS(1u) / 2) / SEC2MS(1), (LPCTSTR)GetResString(IDS_KBITSSEC));
								if (uAllBitrates != _UI32_MAX)
									uAllBitrates += iBitrate;
							}
						}
					}
					if (!strBitrate.IsEmpty())
						mi->strInfo << _T("   ") << GetResString(IDS_BITRATE) << _T(":\t") << strBitrate << _T("\n");

					str = InfoGet(Handle, MediaInfo_Stream_Audio, iStream, _T("Channel(s)"));
					if (!str.IsEmpty()) {
						int iChannels = _tstoi(str);
						mi->strInfo << _T("   ") << GetResString(IDS_CHANNELS) << _T(":\t");
						if (iChannels == 1)
							mi->strInfo << _T("1 (Mono)");
						else if (iChannels == 2)
							mi->strInfo << _T("2 (Stereo)");
						else if (iChannels == 5)
							mi->strInfo << _T("5.1 (Surround)");
						else
							mi->strInfo << iChannels;
						mi->strInfo << _T("\n");
					}

					str = InfoGet(Handle, MediaInfo_Stream_Audio, iStream, _T("SamplingRate"));
					if (!str.IsEmpty())
						mi->strInfo << _T("   ") << GetResString(IDS_SAMPLERATE) << _T(":\t") << _tstoi(str) / 1000.0 << _T(" kHz\n");

					str = InfoGet(Handle, MediaInfo_Stream_Audio, iStream, _T("Language/String"));
					if (str.IsEmpty())
						str = InfoGet(Handle, MediaInfo_Stream_Audio, iStream, _T("Language"));
					if (!str.IsEmpty())
						mi->strInfo << _T("   ") << GetResString(IDS_PW_LANG) << _T(":\t") << str << _T("\n");

					str = InfoGet(Handle, MediaInfo_Stream_Audio, iStream, pLanguageInfo);
					if (!str.IsEmpty())
						mi->strInfo << _T("   ") << GetResString(IDS_PW_LANG) << _T(":\t") << str << _T("\n");
				}
			}
		}

		if (bFullInfo) {
			int iTextStreams = InfoGetBoundedCount(Handle, MediaInfo_Stream_General, 0, _T("TextCount"), 64);
			for (int iStream = 0; iStream < iTextStreams; ++iStream) {
				if (!mi->strInfo.IsEmpty())
					mi->strInfo << _T("\n");
				mi->OutputFileName();
				mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfBold);
				mi->strInfo << _T("Subtitle") << _T(" #") << iStream + 1 << _T("\n");

				str = InfoGet(Handle, MediaInfo_Stream_Text, iStream, pCodec);
				if (!str.IsEmpty())
					mi->strInfo << _T("   ") << GetResString(IDS_CODEC) << _T(":\t") << str << _T("\n");

				str = InfoGet(Handle, MediaInfo_Stream_Text, iStream, _T("Language/String"));
				if (str.IsEmpty())
					str = InfoGet(Handle, MediaInfo_Stream_Text, iStream, _T("Language"));
				if (!str.IsEmpty())
					mi->strInfo << _T("   ") << GetResString(IDS_PW_LANG) << _T(":\t") << str << _T("\n");

				str = InfoGet(Handle, MediaInfo_Stream_Text, iStream, pLanguageInfo);
				if (!str.IsEmpty())
					mi->strInfo << _T("   ") << GetResString(IDS_PW_LANG) << _T(":\t") << str << _T("\n");
			}

			int iMenuStreams = InfoGetBoundedCount(Handle, MediaInfo_Stream_General, 0, _T("MenuCount"), 32);
			for (int iMenu = 0; iMenu < iMenuStreams; ++iMenu) {
				if (!mi->strInfo.IsEmpty())
					mi->strInfo << _T("\n");
				mi->OutputFileName();
				mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfBold);
				mi->strInfo << _T("Menu") << _T(" #") << iMenu + 1 << _T("\n");

				str = InfoGet(Handle, MediaInfo_Stream_Menu, iMenu, _T("Chapters_Pos_Begin"));
				int iBegin = _tstoi(str);
				str = InfoGet(Handle, MediaInfo_Stream_Menu, iMenu, _T("Chapters_Pos_End"));
				int iEnd = MediaInfoDllSeams::NormalizeChapterEnd(iBegin, _tstoi(str), 512);
				for (int iChapter = iBegin; iChapter < iEnd; ++iChapter) {
					if (!mi->strInfo.IsEmpty())
						mi->strInfo << _T("\n");
					mi->OutputFileName();
					mi->strInfo.SetSelectionCharFormat(mi->strInfo.m_cfBold);
					mi->strInfo << _T("  Chapter") << _T(" #") << iChapter - iBegin + 1 << _T("\n");
					str = InfoGetI(Handle, MediaInfo_Stream_Menu, iMenu, iChapter, MediaInfo_Info_Name);
					mi->strInfo << _T("   ") << str << _T("\t");
					str = InfoGetI(Handle, MediaInfo_Stream_Menu, iMenu, iChapter, MediaInfo_Info_Text);
					mi->strInfo << str << _T("\n");
				}
			}
		}

		if (mi->strFileFormat.Find(_T("MPEG")) == 0) {
			if (uAllBitrates != 0 && uAllBitrates != _UI32_MAX) {
				const uint64 uFileSize = static_cast<uint64>(ullFileSize);
				fFileLengthSec = static_cast<double>(uFileSize) * 8.0 / uAllBitrates;
				if (mi->iVideoStreams > 0) {
					if (mi->fVideoFrameRate > 0) {
						ULONGLONG uFrames = (ULONGLONG)(fFileLengthSec * mi->fVideoFrameRate);
						const uint64 uFrameOverhead = uFrames <= uFileSize / 24 ? static_cast<uint64>(uFrames * 24) : uFileSize;
						const uint64 uPayloadBytes = uFileSize - uFrameOverhead;
						fFileLengthSec = static_cast<double>(uPayloadBytes) * 8.0 / uAllBitrates;
					}
					mi->fVideoLengthSec = fFileLengthSec;
				}
				if (mi->iAudioStreams > 0)
					mi->fAudioLengthSec = fFileLengthSec;
			}
			mi->bVideoLengthEstimated |= (mi->iVideoStreams > 0);
			mi->bAudioLengthEstimated |= (mi->iAudioStreams > 0);
		}
	}

	mi->InitFileLength();
	return bFoundHeader
		|| mi->fFileLengthSec > 0
		|| !mi->strTitle.IsEmpty()
		|| !mi->strAuthor.IsEmpty()
		|| !mi->strAlbum.IsEmpty();
}
