//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( devs@emule-project.net / https://www.emule-project.net )
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
#ifndef ASSERT
#include <cassert>
#endif
#include "opcodes.h"
#include "types.h"

namespace FileSizeSeams
{
#ifdef ASSERT
	#define FILE_SIZE_SEAMS_ASSERT(expr) ASSERT(expr)
#else
	#define FILE_SIZE_SEAMS_ASSERT(expr) assert(expr)
#endif

	/**
	 * @brief Reports whether a signed platform file length can be stored in eMule file-size state.
	 */
	inline bool IsSupportedNetworkFileSize(sint64 llFileSize)
	{
		return llFileSize >= 0 && static_cast<uint64>(llFileSize) <= MAX_EMULE_FILE_SIZE;
	}

	/**
	 * @brief Reports whether an unsigned persisted/network file-size value is representable by eMuleBB.
	 */
	inline bool IsSupportedNetworkFileSize(uint64 ullFileSize)
	{
		return ullFileSize <= MAX_EMULE_FILE_SIZE;
	}

	/**
	 * @brief Creates an EMFileSize from a known unsigned network file-size value.
	 */
	inline EMFileSize FromUInt64(uint64 ullFileSize)
	{
		FILE_SIZE_SEAMS_ASSERT(ullFileSize <= MAX_EMULE_FILE_SIZE);
		return EMFileSize(ullFileSize);
	}

	/**
	 * @brief Creates an EMFileSize from a signed platform file length after validation.
	 */
	inline EMFileSize FromSignedFileLength(sint64 llFileSize)
	{
		FILE_SIZE_SEAMS_ASSERT(IsSupportedNetworkFileSize(llFileSize));
		return FromUInt64(static_cast<uint64>(llFileSize));
	}

	/**
	 * @brief Converts EMFileSize back to the stable unsigned storage/formatting type.
	 */
	inline uint64 ToUInt64(EMFileSize nFileSize)
	{
		return static_cast<uint64>(nFileSize);
	}

	#undef FILE_SIZE_SEAMS_ASSERT
}
