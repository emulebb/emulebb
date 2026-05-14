//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
#pragma once

#include <atlstr.h>

/**
 * @brief Small privacy helpers for diagnostic snapshot copy actions.
 */
namespace DiagnosticSnapshotSeams
{
	enum class ESnapshotPrivacyMode
	{
		Raw,
		Redacted
	};

	/** Returns true when a snapshot should suppress locally identifying data. */
	inline bool IsRedacted(ESnapshotPrivacyMode eMode)
	{
		return eMode == ESnapshotPrivacyMode::Redacted;
	}

	/** Replaces the last octet of an IPv4 address and hides non-IPv4 hosts. */
	inline CString RedactNetworkAddress(const CString& address)
	{
		CString trimmed(address);
		trimmed.Trim();
		if (trimmed.IsEmpty())
			return trimmed;

		int dots = 0;
		int lastDot = -1;
		for (int index = 0; index < trimmed.GetLength(); ++index) {
			const TCHAR ch = trimmed[index];
			if (ch == _T('.')) {
				++dots;
				lastDot = index;
				continue;
			}
			if (ch < _T('0') || ch > _T('9'))
				return _T("[redacted]");
		}

		if (dots != 3 || lastDot <= 0)
			return _T("[redacted]");

		return trimmed.Left(lastDot + 1) + _T("x");
	}

	/** Masks the user segment in common Windows profile paths. */
	inline CString RedactPath(const CString& path)
	{
		CString redacted(path);
		if (redacted.IsEmpty())
			return redacted;

		CString lower(redacted);
		lower.MakeLower();
		int usersIndex = lower.Find(_T("\\users\\"));
		const int prefixLength = 7;
		if (usersIndex < 0) {
			if (lower.Left(7) == _T("users\\"))
				usersIndex = -1;
			else
				return redacted;
		}

		const int nameStart = usersIndex + prefixLength;
		const int nextSlash = redacted.Find(_T('\\'), nameStart);
		if (nextSlash <= nameStart)
			return redacted;

		return redacted.Left(nameStart) + _T("<user>") + redacted.Mid(nextSlash);
	}
}
