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
#include "DirectDownload.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace DirectDownload
{
bool CreateTempPathInDirectory(const CString& strDirectory, LPCTSTR pszPrefix, CString& strTempPath, CString& strError)
{
	return HttpTransfer::CreateTempPathInDirectory(strDirectory, pszPrefix, strTempPath, strError);
}

bool DownloadUrlToFile(const CString& strUrl, const CString& strTargetPath, CString& strError)
{
	return DownloadUrlToFile(strUrl, strTargetPath, strError, std::shared_ptr<CDownloadCancellation>());
}

bool DownloadUrlToFile(const CString& strUrl, const CString& strTargetPath, CString& strError, const std::shared_ptr<CDownloadCancellation>& pCancellation)
{
	HttpTransfer::SRequest request;
	request.strUrl = strUrl;
	return HttpTransfer::DownloadToFile(request, strTargetPath, strError, pCancellation);
}
}
