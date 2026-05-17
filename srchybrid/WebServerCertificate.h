//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
#pragma once

#include <vector>

namespace WebServerCertificate
{
/**
 * @brief Describes one self-signed WebServer certificate generation request.
 */
struct SGenerationRequest
{
	CString strKeyFile;
	CString strCertFile;
	CStringA strSubjectName;
	CStringA strIssuerName;
	CStringA strNotBefore;
	CStringA strNotAfter;
	uint16 uSerial;
	std::vector<CStringA> astrDnsNames;
	std::vector<CStringA> astrIpAddresses;
};

/**
 * @brief Builds the default one-year certificate validity window.
 */
void BuildDefaultValidityWindow(CStringA &rstrNotBefore, CStringA &rstrNotAfter);

/**
 * @brief Builds a default local WebServer certificate generation request.
 */
SGenerationRequest BuildDefaultLocalRequest(const CString &strKeyFile, const CString &strCertFile);

/**
 * @brief Creates a self-signed WebServer certificate and private key PEM pair.
 */
int CreateSelfSignedCertificate(const SGenerationRequest &request);
}
