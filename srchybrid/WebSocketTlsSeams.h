#pragma once

#include <vector>

#include "LongPathSeams.h"

#define EMULEBB_TEST_HAVE_WEB_SOCKET_TLS_SEAMS 1

/**
 * @brief Testable TLS file-loading helpers for the native WebServer socket.
 */
namespace WebSocketTlsSeams
{
/**
 * @brief Loads a PEM file from a Windows path and appends the NUL byte required
 * by the mbedTLS PEM parsers.
 */
inline bool TryLoadPemFileForMbedTls(LPCTSTR pszPath, std::vector<unsigned char> &rBytes)
{
	rBytes.clear();
	if (!LongPathSeams::ReadAllBytes(pszPath, rBytes))
		return false;

	rBytes.push_back(0);
	return true;
}
}
