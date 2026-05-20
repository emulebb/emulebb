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

#include <wininet.h>

namespace HttpTransferSeams
{
	enum class ERequestProfile : unsigned char
	{
		ReleaseUpdateJson = 0,
		ServerMet,
		NodesDat,
		IPFilter,
		GeoDatabase,
		GenericFileDownload
	};

	struct SRequestLimits
	{
		DWORD dwConnectTimeoutMs;
		DWORD dwSendTimeoutMs;
		DWORD dwReceiveTimeoutMs;
		ULONGLONG ullTotalTimeoutMs;
		ULONGLONG ullMaxResponseBytes;
	};

	inline constexpr ULONGLONG KiB(const ULONGLONG ullValue) noexcept
	{
		return ullValue * 1024ull;
	}

	inline constexpr ULONGLONG MiB(const ULONGLONG ullValue) noexcept
	{
		return KiB(ullValue) * 1024ull;
	}

	inline SRequestLimits GetRequestLimitsForProfile(const ERequestProfile eProfile) noexcept
	{
		switch (eProfile) {
		case ERequestProfile::ReleaseUpdateJson:
			return { 7000u, 7000u, 7000u, 7000ull, KiB(512) };
		case ERequestProfile::ServerMet:
		case ERequestProfile::NodesDat:
			return { 15000u, 15000u, 15000u, 30000ull, MiB(2) };
		case ERequestProfile::IPFilter:
			return { 30000u, 30000u, 30000u, 180000ull, MiB(64) };
		case ERequestProfile::GeoDatabase:
			return { 30000u, 30000u, 30000u, 600000ull, MiB(192) };
		case ERequestProfile::GenericFileDownload:
		default:
			return { 30000u, 30000u, 30000u, 300000ull, MiB(64) };
		}
	}

	inline DWORD GetInternetOpenTypeForSystemProxyMode(const bool bUseWindowsSystemProxy) noexcept
	{
		return bUseWindowsSystemProxy ? INTERNET_OPEN_TYPE_PRECONFIG : INTERNET_OPEN_TYPE_DIRECT;
	}

	inline bool IsKnownContentLengthAllowed(const ULONGLONG ullContentLength, const ULONGLONG ullMaxResponseBytes) noexcept
	{
		return ullMaxResponseBytes == 0 || ullContentLength == 0 || ullContentLength <= ullMaxResponseBytes;
	}

	inline bool WouldExceedResponseLimit(const ULONGLONG ullCurrentBytes, const DWORD dwNextBytes, const ULONGLONG ullMaxResponseBytes) noexcept
	{
		return ullMaxResponseBytes != 0 && (ullCurrentBytes > ullMaxResponseBytes || static_cast<ULONGLONG>(dwNextBytes) > ullMaxResponseBytes - ullCurrentBytes);
	}
}
