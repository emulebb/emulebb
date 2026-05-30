#pragma once

#include <atlstr.h>
#include <cstddef>

namespace PublicIpProbeSeams
{
struct SPublicIpv4ProbeProvider
{
	const char *pszUrl;
	const char *pszHost;
	const char *pszPath;
};

inline const SPublicIpv4ProbeProvider* GetPublicIpv4ProbeProviders(size_t& rnCount)
{
	static const SPublicIpv4ProbeProvider kProviders[] = {
		{"http://api.ipify.org/", "api.ipify.org", "/"},
		{"http://ipv4.icanhazip.com/", "ipv4.icanhazip.com", "/"},
		{"http://checkip.amazonaws.com/", "checkip.amazonaws.com", "/"},
		{"http://v4.ident.me/", "v4.ident.me", "/"},
		{"http://ipecho.net/plain", "ipecho.net", "/plain"},
	};
	rnCount = sizeof kProviders / sizeof kProviders[0];
	return kProviders;
}

inline CStringA TrimAsciiWhitespace(CStringA strText)
{
	strText.Trim(" \t\r\n");
	return strText;
}

inline bool TryParsePublicIpv4Literal(CStringA strText, CStringA& rstrAddress)
{
	strText = TrimAsciiWhitespace(strText);
	if (strText.IsEmpty())
		return false;

	unsigned uParts[4] = {};
	int iStart = 0;
	for (int iPart = 0; iPart < 4; ++iPart) {
		const int iDot = strText.Find('.', iStart);
		if (iPart < 3) {
			if (iDot < 0)
				return false;
		} else if (iDot >= 0)
			return false;

		const int iEnd = iDot >= 0 ? iDot : strText.GetLength();
		if (iEnd <= iStart)
			return false;

		unsigned uPart = 0;
		for (int i = iStart; i < iEnd; ++i) {
			const char ch = strText[i];
			if (ch < '0' || ch > '9')
				return false;
			uPart = uPart * 10u + static_cast<unsigned>(ch - '0');
			if (uPart > 255u)
				return false;
		}
		uParts[iPart] = uPart;
		iStart = iEnd + 1;
	}

	rstrAddress.Format("%u.%u.%u.%u", uParts[0], uParts[1], uParts[2], uParts[3]);
	return true;
}

inline bool TryParsePublicIpv4HttpResponse(CStringA strResponse, CStringA& rstrAddress)
{
	rstrAddress.Empty();
	const int iHeaderEnd = strResponse.Find("\r\n\r\n");
	if (iHeaderEnd >= 0) {
		const CStringA strStatusLine = strResponse.Left(strResponse.Find("\r\n"));
		if (strStatusLine.Find(" 200 ") < 0 && strStatusLine.Right(4) != " 200")
			return false;
		strResponse = strResponse.Mid(iHeaderEnd + 4);
	}
	return TryParsePublicIpv4Literal(strResponse, rstrAddress);
}
}
