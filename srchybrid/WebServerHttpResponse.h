#pragma once

#include "WebSocket.h"

namespace WebServerHttpResponse
{
/**
 * @brief Sends a complete HTTP response with a byte body through the embedded web socket.
 */
inline void SendBody(CWebSocket *pSocket, const int iStatusCode, LPCSTR pszReason, LPCSTR pszDefaultReason, LPCSTR pszContentTypeHeader, const void *pBody, const size_t uBodySize, LPCSTR pszExtraHeaders = NULL, const bool bCloseConnection = true)
{
	if (pSocket == NULL || uBodySize > MAXDWORD)
		return;

	CStringA strHeader;
	strHeader.Format(
		"HTTP/1.1 %d %s\r\n"
		"%s"
		"Cache-Control: no-store\r\n"
		"%s"
		"%s"
		"Content-Length: %Iu\r\n\r\n",
		iStatusCode,
		pszReason != NULL ? pszReason : pszDefaultReason,
		pszContentTypeHeader != NULL ? pszContentTypeHeader : "",
		bCloseConnection ? "Connection: close\r\n" : "",
		pszExtraHeaders != NULL ? pszExtraHeaders : "",
		uBodySize);
	pSocket->SendData(strHeader, static_cast<DWORD>(strHeader.GetLength()));
	if (pBody != NULL && uBodySize > 0)
		pSocket->SendData(pBody, static_cast<DWORD>(uBodySize));
}

/**
 * @brief Sends a complete HTTP response with a CStringA body.
 */
inline void SendCStringA(CWebSocket *pSocket, const int iStatusCode, LPCSTR pszReason, LPCSTR pszDefaultReason, LPCSTR pszContentTypeHeader, const CStringA &strBody, LPCSTR pszExtraHeaders = NULL, const bool bCloseConnection = true)
{
	SendBody(pSocket, iStatusCode, pszReason, pszDefaultReason, pszContentTypeHeader, static_cast<LPCSTR>(strBody), static_cast<size_t>(strBody.GetLength()), pszExtraHeaders, bCloseConnection);
}
}
