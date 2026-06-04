#include <stdafx.h>
#include "OtherFunctions.h"
#include "WebSocket.h"
#include "WebServer.h"
#include "WebSocketHttpSeams.h"
#include "WebSocketTlsSeams.h"
#include "Preferences.h"
#include "StringConversion.h"
#include "Log.h"
#include "IPv4AddressSeams.h"

#include <vector>
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl_cache.h"
#include "mbedtls/ssl_ticket.h"
#include "TLSthreading.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

static HANDLE s_hTerminate = NULL;
static CWinThread *s_pSocketThread = NULL;
static CCriticalSection s_acceptedThreadLock;

struct AcceptedThreadState
{
	CWinThread *pThread;
};

static std::vector<AcceptedThreadState> s_acceptedThreads;

mbedtls_ssl_config conf;
mbedtls_x509_crt srvcert;
mbedtls_pk_context pkey;
mbedtls_ssl_cache_context cache;
mbedtls_ssl_ticket_context ticket_ctx;

void StopSSL();

typedef struct
{
	void	*pThis;
	SOCKET	hSocket;
	in_addr incomingaddr;
} SocketData;

namespace
{
	const DWORD kWebSocketThreadShutdownTimeoutMs = 10000;

	bool IsTlsWant(const int nResult)
	{
		return nResult == MBEDTLS_ERR_SSL_WANT_READ || nResult == MBEDTLS_ERR_SSL_WANT_WRITE;
	}

	struct TlsSocketContext
	{
		SOCKET hSocket;
	};

	int GetMbedTlsSocketTransferSize(const size_t uSize)
	{
		return uSize > static_cast<size_t>(INT_MAX) ? INT_MAX : static_cast<int>(uSize);
	}

	int SendMbedTlsSocket(void *pContext, const unsigned char *pBuffer, size_t uSize) noexcept
	{
		TlsSocketContext *const pSocketContext = reinterpret_cast<TlsSocketContext*>(pContext);
		if (pSocketContext == NULL || pSocketContext->hSocket == INVALID_SOCKET)
			return MBEDTLS_ERR_NET_INVALID_CONTEXT;

		const int nResult = send(pSocketContext->hSocket, reinterpret_cast<const char*>(pBuffer), GetMbedTlsSocketTransferSize(uSize), 0);
		if (nResult != SOCKET_ERROR)
			return nResult;

		const int nError = WSAGetLastError();
		if (nError == WSAEWOULDBLOCK || nError == WSAEINTR)
			return MBEDTLS_ERR_SSL_WANT_WRITE;
		if (nError == WSAECONNRESET)
			return MBEDTLS_ERR_NET_CONN_RESET;
		return MBEDTLS_ERR_NET_SEND_FAILED;
	}

	int RecvMbedTlsSocket(void *pContext, unsigned char *pBuffer, size_t uSize) noexcept
	{
		TlsSocketContext *const pSocketContext = reinterpret_cast<TlsSocketContext*>(pContext);
		if (pSocketContext == NULL || pSocketContext->hSocket == INVALID_SOCKET)
			return MBEDTLS_ERR_NET_INVALID_CONTEXT;

		const int nResult = recv(pSocketContext->hSocket, reinterpret_cast<char*>(pBuffer), GetMbedTlsSocketTransferSize(uSize), 0);
		if (nResult != SOCKET_ERROR)
			return nResult;

		const int nError = WSAGetLastError();
		if (nError == WSAEWOULDBLOCK || nError == WSAEINTR)
			return MBEDTLS_ERR_SSL_WANT_READ;
		if (nError == WSAECONNRESET)
			return MBEDTLS_ERR_NET_CONN_RESET;
		return MBEDTLS_ERR_NET_RECV_FAILED;
	}

	bool DrainWebSocketSendQueue(CWebSocket &rWebSocket, const SOCKET hSocket, const bool bUseHttps)
	{
		while (rWebSocket.m_pHead) {
			if (rWebSocket.m_pHead->m_pToSend) {
				if (bUseHttps) {
					bool bWouldBlock = false;
					for (;;) {
						int nRes = mbedtls_ssl_write(
							reinterpret_cast<mbedtls_ssl_context*>(rWebSocket.m_ssl),
							reinterpret_cast<unsigned char*>(rWebSocket.m_pHead->m_pToSend),
							rWebSocket.m_pHead->m_dwSize);
						if (nRes > 0) {
							rWebSocket.m_pHead->m_pToSend += nRes;
							rWebSocket.m_pHead->m_dwSize -= nRes;
							rWebSocket.m_uQueuedSendBytes = WebSocketHttpSeams::ConsumeQueuedResponseBytes(rWebSocket.m_uQueuedSendBytes, static_cast<size_t>(nRes));
							if (rWebSocket.m_pHead->m_dwSize)
								continue;
						}
						if (!rWebSocket.m_pHead->m_dwSize)
							break;
						if (IsTlsWant(nRes)) {
							bWouldBlock = true;
							break;
						}
						return false;
					}
					if (bWouldBlock)
						break;
				} else {
					int nRes = send(hSocket, rWebSocket.m_pHead->m_pToSend, rWebSocket.m_pHead->m_dwSize, 0);
					if (nRes != static_cast<int>(rWebSocket.m_pHead->m_dwSize)) {
						if (nRes) {
							if ((nRes > 0) && (nRes < static_cast<int>(rWebSocket.m_pHead->m_dwSize))) {
								rWebSocket.m_pHead->m_pToSend += nRes;
								rWebSocket.m_pHead->m_dwSize -= nRes;
								rWebSocket.m_uQueuedSendBytes = WebSocketHttpSeams::ConsumeQueuedResponseBytes(rWebSocket.m_uQueuedSendBytes, static_cast<size_t>(nRes));
							} else if (WSAEWOULDBLOCK != WSAGetLastError())
								rWebSocket.m_bValid = false;
						}
						break;
					}
					rWebSocket.m_uQueuedSendBytes = WebSocketHttpSeams::ConsumeQueuedResponseBytes(rWebSocket.m_uQueuedSendBytes, rWebSocket.m_pHead->m_dwSize);
				}
			} else if (shutdown(hSocket, SD_SEND)) {
				rWebSocket.m_bValid = false;
				break;
			}

			CWebSocket::CChunk *pNext = rWebSocket.m_pHead->m_pNext;
			delete rWebSocket.m_pHead;
			rWebSocket.m_pHead = pNext;
			if (rWebSocket.m_pHead == NULL)
				rWebSocket.m_pTail = NULL;
		}
		return true;
	}

	void CloseTrackedThread(std::vector<AcceptedThreadState>::iterator &rIt)
	{
		delete rIt->pThread;
		rIt = s_acceptedThreads.erase(rIt);
	}

	void ReapAcceptedThreadHandles()
	{
		CSingleLock lock(&s_acceptedThreadLock, TRUE);
		for (std::vector<AcceptedThreadState>::iterator it = s_acceptedThreads.begin(); it != s_acceptedThreads.end();) {
			const DWORD dwWaitResult = ::WaitForSingleObject(it->pThread->m_hThread, 0);
			if (dwWaitResult == WAIT_OBJECT_0 || dwWaitResult == WAIT_FAILED) {
				if (dwWaitResult == WAIT_FAILED) {
					const DWORD dwWaitError = ::GetLastError();
					DebugLogWarning(_T("Web Interface accepted-client thread wait failed while reaping finished threads: %s"), (LPCTSTR)GetErrorMessage(dwWaitError, 1));
				}
				CloseTrackedThread(it);
			} else
				++it;
		}
	}

	size_t GetTrackedAcceptedThreadCount()
	{
		CSingleLock lock(&s_acceptedThreadLock, TRUE);
		return s_acceptedThreads.size();
	}

	/**
	 * @brief Tracks an accepted-client worker before starting it so it cannot run untracked.
	 */
	bool StartTrackedAcceptedThread(CWinThread *pThread)
	{
		ASSERT(pThread != NULL);
		if (pThread == NULL)
			return false;

		CSingleLock lock(&s_acceptedThreadLock, TRUE);
		AcceptedThreadState state = {pThread};
		s_acceptedThreads.push_back(state);
		try {
			if (pThread->CreateThread())
				return true;
			s_acceptedThreads.pop_back();
		} catch (...) {
			s_acceptedThreads.pop_back();
			throw;
		}
		return false;
	}

	bool WaitForAcceptedThreadHandles(const DWORD dwTimeoutMs)
	{
		const ULONGLONG ullStart = ::GetTickCount64();

		for (;;) {
			HANDLE hThread = NULL;
			{
				CSingleLock lock(&s_acceptedThreadLock, TRUE);
				if (s_acceptedThreads.empty())
					return true;
				hThread = s_acceptedThreads.front().pThread->m_hThread;
			}

			DWORD dwWaitMs = INFINITE;
			if (dwTimeoutMs != INFINITE) {
				const ULONGLONG ullElapsed = ::GetTickCount64() - ullStart;
				if (ullElapsed >= dwTimeoutMs)
					dwWaitMs = 0;
				else
					dwWaitMs = static_cast<DWORD>(dwTimeoutMs - ullElapsed);
			}

			const DWORD dwWaitResult = ::WaitForSingleObject(hThread, dwWaitMs);
			if (dwWaitResult == WAIT_TIMEOUT) {
				CSingleLock lock(&s_acceptedThreadLock, TRUE);
				DebugLogError(_T("Web Interface shutdown timed out with %u accepted-client thread(s) still running"), static_cast<unsigned int>(s_acceptedThreads.size()));
				return false;
			}
			if (dwWaitResult == WAIT_FAILED) {
				const DWORD dwWaitError = ::GetLastError();
				DebugLogWarning(_T("Web Interface accepted-client thread wait failed during shutdown: %s"), (LPCTSTR)GetErrorMessage(dwWaitError, 1));
			}

			CSingleLock lock(&s_acceptedThreadLock, TRUE);
			for (std::vector<AcceptedThreadState>::iterator it = s_acceptedThreads.begin(); it != s_acceptedThreads.end(); ++it) {
				if (it->pThread->m_hThread == hThread) {
					CloseTrackedThread(it);
					break;
				}
			}
		}
	}

	bool TryCompleteDeferredWebSocketShutdown()
	{
		bool bCanCloseTerminate = true;
		if (s_pSocketThread != NULL) {
			if (s_pSocketThread->m_hThread != NULL) {
				const DWORD dwWaitRes = ::WaitForSingleObject(s_pSocketThread->m_hThread, 0);
				if (dwWaitRes == WAIT_OBJECT_0) {
					delete s_pSocketThread;
					s_pSocketThread = NULL;
				} else {
					if (dwWaitRes == WAIT_FAILED) {
						const DWORD dwWaitError = ::GetLastError();
						DebugLogWarning(_T("Web Interface listener thread wait failed while completing deferred shutdown: %s"), (LPCTSTR)GetErrorMessage(dwWaitError, 1));
					}
					bCanCloseTerminate = false;
				}
			} else {
				delete s_pSocketThread;
				s_pSocketThread = NULL;
			}
		}

		ReapAcceptedThreadHandles();
		const size_t uAcceptedThreads = GetTrackedAcceptedThreadCount();
		if (uAcceptedThreads != 0)
			bCanCloseTerminate = false;

		if (bCanCloseTerminate && s_hTerminate != NULL) {
			VERIFY(::CloseHandle(s_hTerminate));
			s_hTerminate = NULL;
			StopSSL();
			DebugLogWarning(_T("Web Interface completed deferred socket shutdown before restart"));
		}

		return s_hTerminate == NULL && s_pSocketThread == NULL && uAcceptedThreads == 0;
	}

	void CleanupFailedListenerStartup(const bool bSslStarted)
	{
		if (s_pSocketThread != NULL) {
			::StopSockets();
			return;
		}
		if (bSslStarted)
			StopSSL();
		if (s_hTerminate != NULL) {
			VERIFY(::CloseHandle(s_hTerminate));
			s_hTerminate = NULL;
		}
	}

	bool WaitForSocketEventOrTerminate(const SOCKET hSocket, HANDLE hEvent, const DWORD dwTimeoutMs)
	{
		HANDLE pWait[] = {hEvent, s_hTerminate};
		const DWORD dwWaitResult = ::WaitForMultipleObjects(DWORD(_countof(pWait)), pWait, FALSE, dwTimeoutMs);
		if (dwWaitResult == WAIT_TIMEOUT) {
			DebugLogWarning(_T("Web Interface accepted-client socket timed out after %lu ms"), dwTimeoutMs);
			return false;
		}
		if (dwWaitResult != WAIT_OBJECT_0)
			return false;

		WSANETWORKEVENTS stEvents;
		return !WSAEnumNetworkEvents(hSocket, hEvent, &stEvents)
			&& !(stEvents.lNetworkEvents & FD_CLOSE);
	}

	CStringA GetHttpHeaderValue(const CStringA &strHeader, LPCSTR pszHeaderName)
	{
		ASSERT(pszHeaderName != NULL);
		if (pszHeaderName == NULL)
			return CStringA();

		std::string strValue;
		return WebSocketHttpSeams::GetSingleHeaderValue(
			std::string(strHeader, strHeader.GetLength()),
			pszHeaderName,
			strValue) == WebSocketHttpSeams::EHeaderValueResult::Found
				? CStringA(strValue.c_str(), static_cast<int>(strValue.size()))
				: CStringA();
	}

	bool TryResolveWebBindAddr(in_addr *pAddr)
	{
		ASSERT(pAddr != NULL);
		if (pAddr == NULL)
			return false;
		pAddr->s_addr = INADDR_ANY;
		const CString &strWebBindAddr = thePrefs.GetWebBindAddr();
		if (strWebBindAddr.IsEmpty())
			return true;

		uint32_t uAddress = 0;
		if (!IPv4AddressSeams::TryParseIPv4Address(strWebBindAddr, uAddress)) {
			DebugLogError(_T("Web Interface start failed: invalid WebBindAddr '%s'"), (LPCTSTR)strWebBindAddr);
			return false;
		}

		pAddr->s_addr = uAddress;
		return true;
	}
}

void CWebSocket::OnRequestReceived(const char *pHeader, DWORD dwHeaderLen, const char *pData, DWORD dwDataLen, const in_addr inad)
{
	CStringA sHeader(pHeader, dwHeaderLen);
	CStringA sMethod;
	CStringA sRequestTarget;
	CStringA sURL;

	std::string strMethod;
	std::string strRequestTarget;
	if (WebSocketHttpSeams::TryParseRequestLine(std::string(pHeader, dwHeaderLen), strMethod, strRequestTarget)) {
		sMethod = CStringA(strMethod.c_str(), static_cast<int>(strMethod.size()));
		sRequestTarget = CStringA(strRequestTarget.c_str(), static_cast<int>(strRequestTarget.size()));
	}

	if (sMethod == "GET") {
		sMethod = "GET";
		sURL = sHeader.Trim();
	} else if (sMethod == "POST") {
		sMethod = "POST";
		CStringA sData(pData, dwDataLen);
		sURL = '?' + sData.Trim();	// '?' to imitate GET syntax for ParseURL
	} else if (sMethod == "PATCH") {
		sMethod = "PATCH";
		sURL = sHeader.Trim();
	} else if (sMethod == "DELETE") {
		sMethod = "DELETE";
		sURL = sHeader.Trim();
	}

	sURL.Delete(0, sURL.Find(' ') + 1);
	int i = sURL.Find(' ');
	if (i >= 0)
		sURL.Truncate(i);
	bool filereq = sURL.GetLength() >= 3 && sURL.Find("..") < 0; // prevent file access in the eMule's webserver folder
	if (filereq) {
		CStringA ext(sURL.Right(5).MakeLower());
		i = ext.ReverseFind('.') + 1;
		ext.Delete(0, i);
		filereq = (i > 0) && ext.GetLength() > 1 && (ext == "gif" || ext == "jpg" || ext == "png"
			|| ext == "ico" || ext == "css" || ext == "bmp" || ext == "js" || ext == "jpeg");
	}
	ThreadData Data;
	Data.sURL = sURL;
	Data.strMethod = sMethod;
	Data.strRequestTarget = sRequestTarget;
	Data.strRequestBody = CStringA(pData, dwDataLen);
	Data.strContentType = GetHttpHeaderValue(sHeader, "Content-Type");
	Data.strApiKey = GetHttpHeaderValue(sHeader, "X-API-Key");
	Data.strCookie = GetHttpHeaderValue(sHeader, "Cookie");
	Data.pThis = (void*)m_pParent;
	Data.inadr = inad;
	Data.pSocket = this;

	if (!filereq)
		m_pParent->_ProcessURL(Data);
	else
		m_pParent->_ProcessFileReq(Data);

	Disconnect();
}

void CWebSocket::OnReceived(const void *pData, DWORD dwSize, const in_addr inad)
{
	static const DWORD SIZE_PRESERVE = 0x1000u;

	uint32_t uRequiredBufSize = 0;
	if (!WebSocketHttpSeams::TryCalculateReceiveBufferSize(m_dwRecv, dwSize, 0u, uRequiredBufSize)) {
		m_bValid = false;
		return;
	}

	if (m_dwBufSize < uRequiredBufSize) {
		// reallocate
		char *pNewBuf;
		try {
			uint32_t uNewBufSize = 0;
			if (!WebSocketHttpSeams::TryCalculateReceiveBufferSize(m_dwRecv, dwSize, SIZE_PRESERVE, uNewBufSize)) {
				m_bValid = false;
				return;
			}
			m_dwBufSize = static_cast<DWORD>(uNewBufSize);
			pNewBuf = new char[m_dwBufSize];
		} catch (...) {
			m_bValid = false; // internal problem
			return;
		}

		if (m_pBuf) {
			memcpy(pNewBuf, m_pBuf, m_dwRecv);
			delete[] m_pBuf;
		}

		m_pBuf = pNewBuf;
	}
	if (pData != NULL) {
		memcpy(&m_pBuf[m_dwRecv], pData, dwSize);
		m_dwRecv += dwSize;
	}
	// check if we have all that we want
	if (!m_dwHttpHeaderLen) {
		uint32_t uHttpHeaderLen = 0;
		const WebSocketHttpSeams::EHttpHeaderScanResult eHeaderScan = WebSocketHttpSeams::ScanHttpHeaderLength(m_pBuf, m_dwRecv, uHttpHeaderLen);
		if (eHeaderScan == WebSocketHttpSeams::EHttpHeaderScanResult::TooLarge) {
			m_bValid = false;
			return;
		}
		if (eHeaderScan == WebSocketHttpSeams::EHttpHeaderScanResult::Complete) {
			m_dwHttpHeaderLen = static_cast<DWORD>(uHttpHeaderLen);

			bool bHasContentLength = false;
			uint32_t uContentLength = 0;
			if (!WebSocketHttpSeams::TryParseContentLengthHeaders(
					std::string(m_pBuf, m_dwHttpHeaderLen),
					bHasContentLength,
					uContentLength)) {
				m_bValid = false;
				return;
			}
			if (bHasContentLength)
				m_dwHttpContentLen = static_cast<DWORD>(uContentLength);
		}

	}
	if (m_dwHttpHeaderLen && !m_bCanRecv && !m_dwHttpContentLen)
		m_dwHttpContentLen = m_dwRecv - m_dwHttpHeaderLen; // of course

	if (WebSocketHttpSeams::IsCompleteHttpRequestBuffered(m_dwRecv, m_dwHttpHeaderLen, m_dwHttpContentLen)) {
		OnRequestReceived(m_pBuf, m_dwHttpHeaderLen, m_pBuf + m_dwHttpHeaderLen, m_dwHttpContentLen, inad);

		if (m_bCanRecv && (m_dwRecv > m_dwHttpHeaderLen + m_dwHttpContentLen)) {
			// move our data
			m_dwRecv -= m_dwHttpHeaderLen + m_dwHttpContentLen;
			memmove(m_pBuf, m_pBuf + m_dwHttpHeaderLen + m_dwHttpContentLen, m_dwRecv);
		} else
			m_dwRecv = 0;

		m_dwHttpHeaderLen = 0;
		m_dwHttpContentLen = 0;
	}
}

void CWebSocket::SendData(const void *pData, DWORD dwDataSize)
{
	ASSERT(pData);
	if (m_bValid && m_bCanSend) {
		if (!m_pHead) {
			if (thePrefs.GetWebUseHttps()) {
				for (;;) {
					int nRes = mbedtls_ssl_write((mbedtls_ssl_context*)m_ssl, (unsigned char*)pData, dwDataSize);
					if (nRes > 0) {
						reinterpret_cast<const char*&>(pData) += nRes;
						dwDataSize -= nRes;
						if (dwDataSize)
							continue;
					}
					if (!dwDataSize)
						break;
					if (IsTlsWant(nRes))
						break;
					m_bValid = false;
					break;
				}
			} else {
				// try to send it directly
				//-- remember: "nRes" could be "-1" after "send" call
				int nRes = send(m_hSocket, (char*)pData, dwDataSize, 0);

				if (nRes > 0) {
					reinterpret_cast<const char*&>(pData) += nRes;
					dwDataSize -= nRes;
				} else if (nRes == SOCKET_ERROR && WSAEWOULDBLOCK != WSAGetLastError()) {
					m_bValid = false;
				}
			}
		}

		if (dwDataSize && m_bValid) {
			// WHY: a single accepted web client can still apply backpressure by
			// requesting data and then not reading it. Request-size limits do not
			// bound response memory, so cap queued CChunk bytes before allocating.
			if (!WebSocketHttpSeams::CanQueueResponseBytes(m_uQueuedSendBytes, dwDataSize)) {
				m_bValid = false;
				return;
			}

			// push it to our tails
			CChunk *pChunk = NULL;
			try {
				pChunk = new CChunk;
			} catch (...) {
				return;
			}
			pChunk->m_pNext = NULL;
			pChunk->m_dwSize = dwDataSize;
			try {
				pChunk->m_pData = new char[dwDataSize];
			} catch (...) {
				delete pChunk; // oops, no memory (???)
				return;
			}
			//-- data should be copied into "pChunk->m_pData" anyhow
			//-- possible solution is simple:

			memcpy(pChunk->m_pData, pData, dwDataSize);

			// push it to the end of our queue
			pChunk->m_pToSend = pChunk->m_pData;
			if (m_pTail)
				m_pTail->m_pNext = pChunk;
			else
				m_pHead = pChunk;
			m_pTail = pChunk;
			m_uQueuedSendBytes += dwDataSize;
		}
	}
}

void CWebSocket::SendReply(LPCSTR szReply)
{
	CStringA sBuf;
	sBuf.Format("%s\r\n", szReply);
	if (!sBuf.IsEmpty())
		SendData(sBuf, sBuf.GetLength());
}

void CWebSocket::SendContent(LPCSTR szStdResponse, const void *pContent, DWORD dwContentSize)
{
	CStringA sBuf;
	sBuf.Format("HTTP/1.1 200 OK\r\n%sContent-Length: %lu\r\n\r\n", szStdResponse, dwContentSize);
	if (!sBuf.IsEmpty()) {
		SendData(sBuf, sBuf.GetLength());
		SendData(pContent, dwContentSize);
	}
}

void CWebSocket::SendContent(LPCSTR szStdResponse, const CString &rstr)
{
	CStringA strA(wc2utf8(rstr));
	SendContent(szStdResponse, strA, strA.GetLength());
}

void CWebSocket::Disconnect()
{
	if (m_bValid && m_bCanSend) {
		// WHY: HTTP responses advertise Connection: close and the listener is
		// intentionally capped to one accepted worker. After a complete request
		// has been answered, waiting for the peer FIN keeps the worker counted
		// active and the listener resets the next client instead of serving it.
		m_bCanRecv = false;
		m_bCanSend = false;
		if (m_pTail)
			try {
				// push an empty chunk as the tail
				m_pTail->m_pNext = new CChunk();
			} catch (...) {
			}
		else if (shutdown(m_hSocket, SD_SEND))
			m_bValid = false;
	}
}

UINT AFX_CDECL WebSocketAcceptedFunc(LPVOID pD)
{
	DbgSetThreadName("WebSocketAccepted");
	InitThreadLocale();

	srand((unsigned)time(NULL));

	const SocketData *pData = static_cast<SocketData*>(pD);
	CWebServer *pThis = static_cast<CWebServer*>(pData->pThis);
	SOCKET hSocket = pData->hSocket;
	const in_addr ad = pData->incomingaddr;
	pThis->SetIP(ad.s_addr);
	delete pData;

	ASSERT(INVALID_SOCKET != hSocket);

	HANDLE hEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
	if (hEvent) {
		if (!WSAEventSelect(hSocket, hEvent, FD_READ | FD_CLOSE | FD_WRITE)) {
			mbedtls_ssl_context ssl;
			TlsSocketContext tlsSocketContext = {hSocket};
			CWebSocket stWebSocket;
			stWebSocket.SetParent(pThis);
			stWebSocket.m_pHead = NULL;
			stWebSocket.m_pTail = NULL;
			stWebSocket.m_uQueuedSendBytes = 0;
			stWebSocket.m_bValid = true;
			stWebSocket.m_bCanRecv = true;
			stWebSocket.m_bCanSend = true;
			stWebSocket.m_hSocket = hSocket;
			stWebSocket.m_pBuf = NULL;
			stWebSocket.m_dwRecv = 0;
			stWebSocket.m_dwBufSize = 0;
			stWebSocket.m_dwHttpHeaderLen = 0;
			stWebSocket.m_dwHttpContentLen = 0;
			stWebSocket.m_ssl = &ssl;

			if (thePrefs.GetWebUseHttps()) {
				mbedtls_ssl_init(&ssl);
				int ret = mbedtls_ssl_setup(&ssl, &conf);
				if (ret)
					goto thread_exit;
				mbedtls_ssl_set_bio(&ssl, &tlsSocketContext, SendMbedTlsSocket, RecvMbedTlsSocket, NULL);
				while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
					if (!IsTlsWant(ret)) {
						DebugLogWarning(_T("Web Interface handshake failed: %s"), (LPCTSTR)SSLerror(ret));
						goto thread_exit;
					}
					if (!WaitForSocketEventOrTerminate(hSocket, hEvent, WebSocketHttpSeams::kAcceptedClientIoTimeoutMs))
						goto thread_exit;
				}
			}
			HANDLE pWait[] = {hEvent, s_hTerminate};

			for (;;) {
				const DWORD dwWaitResult = ::WaitForMultipleObjects(DWORD(_countof(pWait)), pWait, FALSE, static_cast<DWORD>(WebSocketHttpSeams::kAcceptedClientIoTimeoutMs));
				if (dwWaitResult == WAIT_TIMEOUT) {
					DebugLogWarning(_T("Web Interface accepted-client socket timed out after %lu ms"), static_cast<DWORD>(WebSocketHttpSeams::kAcceptedClientIoTimeoutMs));
					break;
				}
				if (dwWaitResult != WAIT_OBJECT_0)
					break;
				while (stWebSocket.m_bValid) {
					WSANETWORKEVENTS stEvents;
					if (WSAEnumNetworkEvents(hSocket, hEvent, &stEvents))
						stWebSocket.m_bValid = false;
					else {
						if (!stEvents.lNetworkEvents)
							break; //no more events till now

						if (FD_READ & stEvents.lNetworkEvents) {
							for (;;) {
								char pBuf[WebSocketHttpSeams::kAcceptedClientReadBufferBytes];
								int nRes;
								if (thePrefs.GetWebUseHttps())
									nRes = mbedtls_ssl_read((mbedtls_ssl_context*)stWebSocket.m_ssl, (unsigned char*)pBuf, sizeof pBuf);
								else
									nRes = recv(hSocket, pBuf, sizeof pBuf, 0);
								if (thePrefs.GetWebUseHttps() && IsTlsWant(nRes))
									break;
								if (nRes <= 0) {
									if (!nRes) {
										stWebSocket.m_bCanRecv = false;
										stWebSocket.OnReceived(NULL, 0, ad);
									} else if (thePrefs.GetWebUseHttps() || WSAEWOULDBLOCK != WSAGetLastError())
										stWebSocket.m_bValid = false;
									break;
								}
								stWebSocket.OnReceived(pBuf, nRes, ad);
							}
							if (thePrefs.GetWebUseHttps() && stWebSocket.m_bValid && stWebSocket.m_pHead && !DrainWebSocketSendQueue(stWebSocket, hSocket, true))
								goto thread_exit;
						}

						if (FD_CLOSE & stEvents.lNetworkEvents)
							stWebSocket.m_bCanRecv = false;

						if (FD_WRITE & stEvents.lNetworkEvents)
							if (!DrainWebSocketSendQueue(stWebSocket, hSocket, thePrefs.GetWebUseHttps()))
								goto thread_exit;
					}
				}

				if (!stWebSocket.m_bValid || (!stWebSocket.m_bCanRecv && !stWebSocket.m_pHead))
					break;
			}
thread_exit:
			stWebSocket.m_bValid = false;
			while (stWebSocket.m_pHead) {
				CWebSocket::CChunk *pNext = stWebSocket.m_pHead->m_pNext;
				delete stWebSocket.m_pHead;
				stWebSocket.m_pHead = pNext;
			}
			stWebSocket.m_uQueuedSendBytes = 0;
			delete[] stWebSocket.m_pBuf;
			if (thePrefs.GetWebUseHttps()) {
				int ret;
				while ((ret = mbedtls_ssl_close_notify((mbedtls_ssl_context*)stWebSocket.m_ssl)) < 0)
					break;
				mbedtls_ssl_free((mbedtls_ssl_context*)stWebSocket.m_ssl);
			}
		}
		VERIFY(::CloseHandle(hEvent));
	}
	VERIFY(!closesocket(hSocket));
	return 0;
}

UINT AFX_CDECL WebSocketListeningFunc(LPVOID pThis)
{
	DbgSetThreadName("WebSocketListening");
	InitThreadLocale();

	srand((unsigned)time(NULL));

	SOCKET hSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);
	if (INVALID_SOCKET != hSocket) {
		SOCKADDR_IN stAddr;
		stAddr.sin_family = AF_INET;
		stAddr.sin_port = htons(thePrefs.GetWSPort());
		if (!TryResolveWebBindAddr(&stAddr.sin_addr)) {
			VERIFY(!closesocket(hSocket));
			return 0;
		}

		if (bind(hSocket, (LPSOCKADDR)&stAddr, sizeof stAddr)) {
			const int nBindError = WSAGetLastError();
			DebugLogError(_T("Web Interface start failed: bind %s:%u failed: %s")
				, (LPCTSTR)ipstr(stAddr.sin_addr.s_addr)
				, ntohs(stAddr.sin_port)
				, (LPCTSTR)GetErrorMessage(nBindError, 1));
		} else if (listen(hSocket, SOMAXCONN)) {
			const int nListenError = WSAGetLastError();
			DebugLogError(_T("Web Interface start failed: listen %s:%u failed: %s")
				, (LPCTSTR)ipstr(stAddr.sin_addr.s_addr)
				, ntohs(stAddr.sin_port)
				, (LPCTSTR)GetErrorMessage(nListenError, 1));
		} else {
			DebugLog(_T("Web Interface listening on %s:%u")
				, (LPCTSTR)ipstr(stAddr.sin_addr.s_addr)
				, ntohs(stAddr.sin_port));
			HANDLE hEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
			if (hEvent) {
				if (!WSAEventSelect(hSocket, hEvent, FD_ACCEPT)) {
					HANDLE pWait[] = {hEvent, s_hTerminate};
					while (WAIT_OBJECT_0 == ::WaitForMultipleObjects(2, pWait, FALSE, INFINITE)) {
						ReapAcceptedThreadHandles();
						for (;;) {
							SOCKADDR_IN their_addr;
							int sin_size = (int)sizeof(SOCKADDR_IN);

							SOCKET hAccepted = accept(hSocket, (LPSOCKADDR)&their_addr, &sin_size);
							if (INVALID_SOCKET == hAccepted)
								break;

							bool bAllowedIP = thePrefs.GetAllowedRemoteAccessIPs().IsEmpty();
							if (!bAllowedIP) {
								for (INT_PTR i = thePrefs.GetAllowedRemoteAccessIPs().GetCount(); --i >= 0;)
									if (their_addr.sin_addr.s_addr == thePrefs.GetAllowedRemoteAccessIPs()[i]) {
										bAllowedIP = true;
										break;
									}

								if (!bAllowedIP) {
									LogWarning(_T("Web Interface: Rejected connection attempt from %s"), (LPCTSTR)ipstr(their_addr.sin_addr.s_addr));
									VERIFY(!closesocket(hAccepted));
									if (WebSocketHttpSeams::GetRejectedRemoteAccessIpAction() == WebSocketHttpSeams::ERejectedRemoteAccessIpAction::ContinueAcceptDrain)
										continue;
									break;
								}
							}

							if (thePrefs.GetWSIsEnabled()) {
								const size_t uAcceptedThreads = GetTrackedAcceptedThreadCount();
								if (!WebSocketHttpSeams::CanStartAcceptedClientThread(uAcceptedThreads)) {
									DebugLogWarning(_T("Web Interface rejected connection from %s because %u accepted-client thread(s) are already active"), (LPCTSTR)ipstr(their_addr.sin_addr.s_addr), static_cast<unsigned int>(uAcceptedThreads));
									VERIFY(!closesocket(hAccepted));
									continue;
								}

								SocketData *pData = NULL;
								try {
									pData = new SocketData;
								} catch (...) {
									VERIFY(!closesocket(hAccepted));
									break;
								}
								pData->pThis = pThis;
								pData->hSocket = hAccepted;
								pData->incomingaddr = their_addr.sin_addr;
								// - do NOT use Windows API 'CreateThread' to create a thread which uses MFC/CRT -> lot of mem leaks!
								// - 'AfxBeginThread' is excessive for our needs.
								CWinThread *pAcceptThread = NULL;
								try {
									pAcceptThread = new CWinThread(WebSocketAcceptedFunc, (LPVOID)pData);
								} catch (...) {
									delete pData;
									VERIFY(!closesocket(hAccepted));
									break;
								}
								pAcceptThread->m_bAutoDelete = FALSE;
								bool bAcceptedThreadStarted = false;
								try {
									bAcceptedThreadStarted = StartTrackedAcceptedThread(pAcceptThread);
								} catch (...) {
									delete pAcceptThread;
									delete pData;
									VERIFY(!closesocket(hAccepted));
									break;
								}
								if (!bAcceptedThreadStarted) {
									delete pAcceptThread;
									delete pData;
									VERIFY(!closesocket(hAccepted));
								}
							} else
								VERIFY(!closesocket(hAccepted));
						}
					}
				}
				VERIFY(::CloseHandle(hEvent));
			}
		}
		VERIFY(!closesocket(hSocket));
	}

	return 0;
}

int StartSSL()
{
	if (!thePrefs.GetWebUseHttps())
		return 0; //success
	mbedtls_threading_set_alt(threading_mutex_init_alt, threading_mutex_destroy_alt, threading_mutex_lock_alt, threading_mutex_unlock_alt
							, cond_init_alt, cond_destroy_alt, cond_signal_alt, cond_broadcast_alt, cond_wait_alt);
	mbedtls_ssl_config_init(&conf);
	mbedtls_x509_crt_init(&srvcert);
	mbedtls_pk_init(&pkey);
	mbedtls_ssl_cache_init(&cache);
	mbedtls_ssl_ticket_init(&ticket_ctx);
	int ret = (int)psa_crypto_init();
	if (!ret) { // PSA_SUCCESS is 0
		std::vector<unsigned char> buf;
		if (!WebSocketTlsSeams::TryLoadPemFileForMbedTls(thePrefs.GetWebCertPath(), buf))
			ret = MBEDTLS_ERR_X509_FILE_IO_ERROR;
		else
			ret = mbedtls_x509_crt_parse(&srvcert, buf.data(), buf.size());
		if (!ret) {
			buf.clear();
			if (!WebSocketTlsSeams::TryLoadPemFileForMbedTls(thePrefs.GetWebKeyPath(), buf))
				ret = MBEDTLS_ERR_PK_FILE_IO_ERROR;
			else
				ret = mbedtls_pk_parse_key(&pkey, buf.data(), buf.size(), NULL, 0);
			if (!ret) {
				ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
				if (!ret) {
					mbedtls_ssl_conf_session_cache(&conf, &cache, mbedtls_ssl_cache_get, mbedtls_ssl_cache_set);
					ret = mbedtls_ssl_ticket_setup(&ticket_ctx, PSA_ALG_GCM, PSA_KEY_TYPE_AES, 256, 86400);
					if (!ret) {
						mbedtls_ssl_conf_session_tickets_cb(&conf, mbedtls_ssl_ticket_write, mbedtls_ssl_ticket_parse, &ticket_ctx);
						mbedtls_ssl_conf_new_session_tickets(&conf, 1);
						mbedtls_ssl_conf_tls13_key_exchange_modes(&conf, MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_ALL);
						ret = mbedtls_ssl_conf_own_cert(&conf, &srvcert, &pkey);
					}
				}
			}
		}
	}
	if (ret)
		DebugLogError(_T("Web Interface start failed: %s"), (LPCTSTR)SSLerror(ret));
	else {
		unsigned char fingerprint[20];
		mbedtls_sha1(srvcert.raw.p, srvcert.raw.len, fingerprint);
		DebugLog(_T("Loaded certificate: %s"), (LPCTSTR)GetCertHash(fingerprint, (int)(sizeof fingerprint)));
	}
	return ret;
}

void StopSSL()
{
	if (thePrefs.GetWebUseHttps()) {
		mbedtls_ssl_config_free(&conf);
		mbedtls_ssl_cache_free(&cache);
		mbedtls_ssl_ticket_free(&ticket_ctx);
		mbedtls_x509_crt_free(&srvcert);
		mbedtls_pk_free(&pkey);
		mbedtls_psa_crypto_free();
		mbedtls_threading_free_alt();
	}
}

void StartSockets(CWebServer *pThis)
{
	ASSERT(s_hTerminate == NULL);
	ASSERT(s_pSocketThread == NULL);
	if (!TryCompleteDeferredWebSocketShutdown()) {
		DebugLogError(_T("Web Interface cannot start because previous socket shutdown is still active (listener=%u, accepted=%u, terminate=%u)"),
			s_pSocketThread != NULL ? 1u : 0u,
			static_cast<unsigned>(GetTrackedAcceptedThreadCount()),
			s_hTerminate != NULL ? 1u : 0u);
		return;
	}

	s_hTerminate = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (s_hTerminate != NULL) {
		bool bSslStarted = false;
		CWinThread *pNewSocketThread = NULL;
		try {
			if (StartSSL()) {
				StopSSL();
				VERIFY(::CloseHandle(s_hTerminate));
				s_hTerminate = NULL;
				return;
			}
			bSslStarted = true;

			// - do NOT use Windows API 'CreateThread' to create a thread which uses MFC/CRT -> lot of mem leaks!
			// - because we want to wait on the thread handle,
			//   we have to disable 'CWinThread::m_AutoDelete' -> can't use 'AfxBeginThread'
			pNewSocketThread = new CWinThread(WebSocketListeningFunc, (LPVOID)pThis);
			pNewSocketThread->m_bAutoDelete = FALSE;
			s_pSocketThread = pNewSocketThread;
			pNewSocketThread = NULL;
			if (!s_pSocketThread->CreateThread())
				StopSockets();
		} catch (CException *ex) {
			if (pNewSocketThread != NULL)
				delete pNewSocketThread;
			CleanupFailedListenerStartup(bSslStarted);
			DebugLogError(_T("Web Interface listener startup failed with %s"), (LPCTSTR)CExceptionStr(*ex));
			ex->Delete();
		} catch (...) {
			if (pNewSocketThread != NULL)
				delete pNewSocketThread;
			CleanupFailedListenerStartup(bSslStarted);
			DebugLogError(_T("Web Interface listener startup failed with an unexpected exception"));
		}
	}
}

void StopSockets()
{
	if (s_hTerminate)
		VERIFY(::SetEvent(s_hTerminate));

	if (s_pSocketThread) {
		bool bListenerWaitSucceeded = true;
		if (s_pSocketThread->m_hThread) {
			// because we want to wait on the thread handle we must not use 'CWinThread::m_AutoDelete'.
			// otherwise we may run into the situation that the CWinThread was already auto-deleted and
			// the CWinThread::m_hThread is invalid.
			ASSERT(!s_pSocketThread->m_bAutoDelete);

			DWORD dwWaitRes = ::WaitForSingleObject(s_pSocketThread->m_hThread, kWebSocketThreadShutdownTimeoutMs);
			if (dwWaitRes == WAIT_TIMEOUT) {
				TRACE("*** Failed to wait for websocket thread termination - Timeout\n");
				DebugLogError(_T("Web Interface listener thread did not exit within %lu ms"), kWebSocketThreadShutdownTimeoutMs);
				bListenerWaitSucceeded = false;
			} else if (dwWaitRes == WAIT_FAILED) {
				const DWORD dwWaitError = ::GetLastError();
				TRACE("*** Failed to wait for websocket thread termination - Error %lu\n", dwWaitError);
				DebugLogError(_T("Web Interface listener thread wait failed: %s"), (LPCTSTR)GetErrorMessage(dwWaitError, 1));
				bListenerWaitSucceeded = false;
			}
		}
		if (WebSocketHttpSeams::GetSocketThreadShutdownFollowUp(bListenerWaitSucceeded) == WebSocketHttpSeams::ESocketThreadShutdownFollowUp::WaitWithoutTimeout) {
			DebugLogError(_T("Web Interface listener thread is still using WebServer state; waiting without a timeout before teardown."));
			if (s_pSocketThread->m_hThread != NULL)
				(void)::WaitForSingleObject(s_pSocketThread->m_hThread, INFINITE);
		}
		delete s_pSocketThread;
		s_pSocketThread = NULL;
	}

	if (!WaitForAcceptedThreadHandles(kWebSocketThreadShutdownTimeoutMs)) {
		if (WebSocketHttpSeams::GetSocketThreadShutdownFollowUp(false) == WebSocketHttpSeams::ESocketThreadShutdownFollowUp::WaitWithoutTimeout) {
			DebugLogError(_T("Web Interface accepted-client thread(s) are still using WebServer state; waiting without a timeout before teardown."));
			(void)WaitForAcceptedThreadHandles(INFINITE);
		}
	}

	if (s_hTerminate) {
		VERIFY(::CloseHandle(s_hTerminate));
		s_hTerminate = NULL;
		StopSSL();
	} else
		StopSSL();
}
