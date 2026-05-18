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
	inline DWORD GetInternetOpenTypeForSystemProxyMode(const bool bUseWindowsSystemProxy) noexcept
	{
		return bUseWindowsSystemProxy ? INTERNET_OPEN_TYPE_PRECONFIG : INTERNET_OPEN_TYPE_DIRECT;
	}

	inline bool ShouldRegisterInternetHandleForCancellationState(bool bCancelled) noexcept
	{
		return !bCancelled;
	}
}
