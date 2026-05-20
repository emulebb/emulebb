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
#include "HttpDownloadLog.h"
#include "HttpTransfer.h"
#include "Log.h"
#include "OtherFunctions.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	const ULONGLONG PROGRESS_LOG_INTERVAL_MS = 5000;
	const DWORD PROGRESS_LOG_PERCENT_STEP = 10;

	CString FormatDownloadBytes(ULONGLONG ullBytes)
	{
		return CastItoXBytes(static_cast<uint64>(ullBytes));
	}
}

namespace HttpDownloadLog
{
	bool DownloadToFile(const CString& strUrl, const CString& strTargetPath, const CString& strLabel, HttpTransferSeams::ERequestProfile eProfile, CString& strError)
	{
		HttpTransfer::SRequest request = HttpTransfer::MakeRequest(eProfile, strUrl);
		request.strHeaders = _T("Accept-Encoding: identity\r\n");

		Log(_T("Downloading %s from %s"), (LPCTSTR)strLabel, (LPCTSTR)strUrl);

		ULONGLONG ullLastLogTick = ::GetTickCount64();
		DWORD dwLastPercent = 101;
		const HttpTransfer::ProgressCallback progressCallback = [strLabel, &ullLastLogTick, &dwLastPercent](ULONGLONG ullBytesRead, ULONGLONG ullContentLength) {
			const ULONGLONG ullNow = ::GetTickCount64();
			const bool bIntervalElapsed = ullNow - ullLastLogTick >= PROGRESS_LOG_INTERVAL_MS;

			if (ullContentLength != 0) {
				const ULONGLONG ullPercent = ullBytesRead * 100ull / ullContentLength;
				const DWORD dwPercent = static_cast<DWORD>(ullPercent > 100ull ? 100ull : ullPercent);
				if (dwLastPercent == 101 || dwPercent >= dwLastPercent + PROGRESS_LOG_PERCENT_STEP || dwPercent == 100 || bIntervalElapsed) {
					AddDebugLogLine(false, _T("%s download %u%% (%s of %s)"),
						(LPCTSTR)strLabel,
						dwPercent,
						(LPCTSTR)FormatDownloadBytes(ullBytesRead),
						(LPCTSTR)FormatDownloadBytes(ullContentLength));
					ullLastLogTick = ullNow;
					dwLastPercent = dwPercent;
				}
			} else if (bIntervalElapsed) {
				AddDebugLogLine(false, _T("%s download received %s"),
					(LPCTSTR)strLabel,
					(LPCTSTR)FormatDownloadBytes(ullBytesRead));
				ullLastLogTick = ullNow;
			}
		};

		if (!HttpTransfer::DownloadToFile(request, strTargetPath, strError, progressCallback)) {
			AddDebugLogLine(false, _T("%s download failed from %s: %s"),
				(LPCTSTR)strLabel,
				(LPCTSTR)strUrl,
				(LPCTSTR)strError);
			return false;
		}

		Log(_T("Downloaded %s from %s"), (LPCTSTR)strLabel, (LPCTSTR)strUrl);
		return true;
	}
}
