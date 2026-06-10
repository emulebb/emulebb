#pragma once

#include <cstdint>

class CWebServer;

void StartSockets(CWebServer *pThis);
bool StopSockets();

class CWebSocket
{
public:
	void SetParent(CWebServer *pParent)		{ m_pParent = pParent; }
	CWebServer *m_pParent;

	class CChunk
	{
	public:
		CChunk()
			: m_pData(NULL)
			, m_pToSend(NULL)
			, m_pNext(NULL)
			, m_dwSize(0)
		{
		}

		char *m_pData;
		char *m_pToSend;
		CChunk *m_pNext;
		DWORD m_dwSize;

		~CChunk()			{ delete[] m_pData; }
	};

	CChunk *m_pHead; // tails of what has to be sent
	CChunk *m_pTail;
	uint64_t m_uQueuedSendBytes;

	char *m_pBuf;
	void *m_ssl; //mbedtls_ssl_context *
	SOCKET m_hSocket;
	DWORD m_dwRecv;
	DWORD m_dwBufSize;
	DWORD m_dwHttpHeaderLen;
	DWORD m_dwHttpContentLen;

	bool m_bCanRecv;
	bool m_bCanSend;
	bool m_bValid;

	void OnReceived(const void *pData, DWORD dwSize, const in_addr inad); // must be implemented
	void SendData(const void *pData, DWORD dwDataSize);
	void SendData(LPCSTR szText)			{ SendData(szText, (DWORD)strlen(szText)); }
	void SendContent(LPCSTR szStdResponse, const void *pContent, DWORD dwContentSize);
	void SendContent(LPCSTR szStdResponse, const CString &rstr);
	void SendReply(LPCSTR szReply);
	void Disconnect();

	void OnRequestReceived(const char *pHeader, DWORD dwHeaderLen, const char *pData, DWORD dwDataLen, const in_addr inad);
};
