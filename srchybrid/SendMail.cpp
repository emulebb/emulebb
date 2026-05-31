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
#include "stdafx.h"
#include "emule.h"
#include "emuleDlg.h"
#include "TaskbarNotifier.h"
#include "OtherFunctions.h"
#include "StringConversion.h"
#include "Log.h"
#include "Preferences.h"
#include <atlenc.h>

#include "mbedtls/base64.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl_cache.h"
#include "mbedtls/ssl_ticket.h"
#include "TLSthreading.h"

// FROZEN SURFACE (2026-05-17): SMTP/email notifications receive no support, no
// new tests, and no hardening/refactor investment. This feature may be removed;
// only touch it to delete it or to protect supported shared infrastructure.

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

bool encoded_word(const CString &src, CStringA &dst)
{
	if (!NeedUTF8String(src)) {
		dst = src;
		return true;
	}
	CStringA srcA(wc2utf8(src));
	int iLength = Base64EncodeGetRequiredLength(srcA.GetLength(), ATL_BASE64_FLAG_NOCRLF);

	if (!Base64Encode(reinterpret_cast<const BYTE*>((LPCSTR)srcA), srcA.GetLength()
		, dst.GetBuffer(iLength), &iLength, ATL_BASE64_FLAG_NOCRLF))
	{
		dst.ReleaseBuffer(0);
		DebugLogWarning(LOG_DONTNOTIFY, _T("'%s' to base64 failed"), (LPCTSTR)src);
		return false;
	}
	dst.ReleaseBuffer(iLength);
	dst.Insert(0, "=?utf-8?b?");
	dst += "?=";
	return true;
}

static int do_handshake(mbedtls_ssl_context *ssl, LPCTSTR *pmsg)
{
	int ret;
	while ((ret = mbedtls_ssl_handshake(ssl)) != 0)
		if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
			*pmsg = _T("mbedtls_ssl_handshake");
			return ret;
		}

	uint32_t flags = mbedtls_ssl_get_verify_result(ssl);
	if (flags != 0)
		DebugLogWarning(LOG_DONTNOTIFY, _T("Mail server certificate has issues: %u"), flags);
	return 0;
}

static int write_ssl(mbedtls_ssl_context *ssl, const char *buf, size_t len)
{
	if (len > 0)
		for (int ret; (ret = mbedtls_ssl_write(ssl, (unsigned char*)buf, len)) <= 0;)
			if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
				DebugLog(LOG_DONTNOTIFY, _T("mbedtls_ssl_write returned %d"), ret);
				return -1;
			}

	for (;;) {
		unsigned char data[256];

		int ret = mbedtls_ssl_read(ssl, data, sizeof(data) - 1);
		if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
			if (ret <= 0) {
				if (ret != MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
					DebugLog(LOG_DONTNOTIFY, _T("mbedtls_ssl_read returned %d"), ret);
				return -1;
			}
			data[min(3, ret)] = '\0';
			return atoi((char*)data);
		}
	}
}

static int write_txt(mbedtls_net_context *sock_fd, const char *buf, size_t len)
{
	if (len > 0) {
		int ret = mbedtls_net_send(sock_fd, (unsigned char*)buf, len);
		if (ret <= 0) {
			DebugLog(LOG_DONTNOTIFY, _T("mbedtls_net_send returned %d"), ret);
			return -1;
		}
	}

	unsigned char data[256];
	int ret = mbedtls_net_recv(sock_fd, data, sizeof data - 1);
	if (ret <= 0) {
		DebugLogWarning(LOG_DONTNOTIFY, _T("mbedtls_net_recv returned %d"), ret);
		return -1;
	}

	data[min(3, ret)] = '\0';
	return atoi((char*)data);
}


///////////////////////////////////////////////////////////////////////////////
// CNotifierMailThread

class CNotifierMailThread : public CWinThread
{
	DECLARE_DYNCREATE(CNotifierMailThread)

	void sendmail();
	int write_data(mbedtls_ssl_context *ssl, const char *buf);
	int write_data(mbedtls_ssl_context *ssl, const char *buf, size_t len);

	EmailSettings m_mail;

	CString m_strSubject;
	CString m_strBody;

protected:
	CNotifierMailThread() = default;	// protected constructor used by dynamic creation
	static CCriticalSection sm_critSect;

public:
	virtual	BOOL InitInstance();
};

CCriticalSection CNotifierMailThread::sm_critSect;

IMPLEMENT_DYNCREATE(CNotifierMailThread, CWinThread)

BOOL CNotifierMailThread::InitInstance()
{
	DbgSetThreadName("NotifierMailThread");
	if (!theApp.IsClosing() && sm_critSect.Lock()) {
		InitThreadLocale();
		sendmail();

		sm_critSect.Unlock();
	}
	return FALSE;
}

void CNotifierMailThread::sendmail()
{
	CStringA sBodyA, sReceiverA, sSenderA, sServerA, sTmpA, sBufA;

	mbedtls_net_context server_fd;
	mbedtls_pk_context pkey;
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;
	mbedtls_ssl_cache_context cache;
	mbedtls_ssl_ticket_context ticket_ctx;
	LPCTSTR pmsg = NULL;
	int ret;

	mbedtls_threading_set_alt(threading_mutex_init_alt, threading_mutex_destroy_alt, threading_mutex_lock_alt, threading_mutex_unlock_alt
							, cond_init_alt, cond_destroy_alt, cond_signal_alt, cond_broadcast_alt, cond_wait_alt);
	mbedtls_net_init(&server_fd);
	mbedtls_ssl_init(&ssl);
	mbedtls_ssl_config_init(&conf);
	mbedtls_pk_init(&pkey);
	mbedtls_ssl_cache_init(&cache);
	mbedtls_ssl_ticket_init(&ticket_ctx);
	ret = (int)psa_crypto_init();
	if (ret) { //PSA_SUCCESS is 0
		pmsg = _T("psa_crypto_init");
		goto exit;
	}

	sServerA = (CStringA)m_mail.sServer;
	sTmpA.Format("%d", m_mail.uPort);
	ret = mbedtls_net_connect(&server_fd, sServerA, sTmpA, MBEDTLS_NET_PROTO_TCP);
	if (ret != 0) {
		DebugLogWarning(LOG_DONTNOTIFY, _T("Connect to %s:%hu failed: %d"), (LPCTSTR)m_mail.sServer, m_mail.uPort, ret);
		goto exit;
	}

	ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
	if (ret != 0) {
		pmsg = _T("mbedtls_ssl_config_defaults");
		goto exit;
	}

	mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
	ret = mbedtls_ssl_setup(&ssl, &conf);
	if (ret != 0) {
		pmsg = _T("mbedtls_ssl_setup");
		goto exit;
	}

	mbedtls_ssl_conf_session_cache(&conf, &cache, mbedtls_ssl_cache_get, mbedtls_ssl_cache_set);
	ret = mbedtls_ssl_ticket_setup(&ticket_ctx, PSA_ALG_GCM, PSA_KEY_TYPE_AES, 256, 86400);
	if (ret != 0) {
		pmsg = _T("mbedtls_ssl_ticket_setup");
		goto exit;
	}
	mbedtls_ssl_conf_session_tickets_cb(&conf, mbedtls_ssl_ticket_write, mbedtls_ssl_ticket_parse, &ticket_ctx);
	mbedtls_ssl_conf_new_session_tickets(&conf, 1);
	mbedtls_ssl_conf_tls13_key_exchange_modes(&conf, MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_ALL);

	ret = mbedtls_ssl_set_hostname(&ssl, sServerA);
	if (ret != 0) {
		pmsg = _T("mbedtls_ssl_set_hostname");
		goto exit;
	}

	mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

	char hostname[256];
	gethostname(hostname, sizeof hostname);

	switch (m_mail.uTLS) {
	case MODE_SSL_TLS:
		ret = do_handshake(&ssl, &pmsg);
		if (ret)
			goto exit;

		ret = write_ssl(&ssl, NULL, 0);
		if (ret > 0 && ret < 200 || ret > 299) {
			pmsg = _T("write_ssl");
			goto failed;
		}

		sBufA.Format("EHLO %s\r\n", hostname);
		ret = write_ssl(&ssl, sBufA, sBufA.GetLength()); //250
		if (ret > 0 && ret < 200 || ret > 299) {
			pmsg = _T("write_txt (EHLO hostname)");
			goto failed;
		}
		break;
	case MODE_STARTTLS:
	default: //MODE_NONE
		ret = write_txt(&server_fd, NULL, 0); //220
		if (ret > 0 && ret < 200 || ret > 299) {
			pmsg = _T("write_txt (greetings)");
			goto exit;
		}

		sBufA.Format("EHLO %s\r\n", hostname);
		ret = write_txt(&server_fd, sBufA, sBufA.GetLength()); //250
		if (ret > 0 && ret < 200 || ret > 299) {
			pmsg = _T("write_txt (EHLO)");
			goto exit;
		}
		if (m_mail.uTLS == MODE_NONE)
			break;

		ret = write_txt(&server_fd, "STARTTLS\r\n", sizeof("STARTTLS\r\n")); //220
		if (ret > 0 && ret < 200 || ret > 299) {
			pmsg = _T("write_txt (STARTTLS)");
			goto exit;
		}
		ret = do_handshake(&ssl, &pmsg);
		if (ret)
			goto exit;
	}

	size_t n;
	unsigned char base[1024];

	switch (m_mail.uAuth) {
	case AUTH_PLAIN:
		sTmpA.Format("%c%s%c%s", '\0', (LPCSTR)((CStringA)m_mail.sUser), '\0', (LPCSTR)(CStringA(m_mail.sPass)));
		ret = mbedtls_base64_encode(base, sizeof base, &n, (unsigned char*)(LPCSTR)sTmpA, sTmpA.GetLength());
		if (ret != 0) {
			pmsg = _T("mbedtls_base64_encode (plain ID)");
			goto failed;
		}

		sTmpA.Format("AUTH PLAIN %s\r\n", base);
		ret = write_data(&ssl, sTmpA); //235
		if (ret > 0 && ret < 200 || ret > 299) {
			pmsg = _T("write_data (AUTH PLAIN)");
			goto failed;
		}
		break;
	case AUTH_LOGIN:
		ret = write_data(&ssl, "AUTH LOGIN\r\n"); //334
		if (ret > 0 && ret < 200 || ret > 399) {
			pmsg = _T("write_data (AUTH LOGIN)");
			goto failed;
		}

		sTmpA = (CStringA)m_mail.sUser;
		ret = mbedtls_base64_encode(base, sizeof base, &n, (unsigned char*)(LPCSTR)sTmpA, sTmpA.GetLength());
		if (ret != 0) {
			pmsg = _T("mbedtls_base64_encode (login)");
			goto failed;
		}
		sBufA.Format("%s\r\n", base);
		ret = write_data(&ssl, sBufA); //334
		if (ret > 0 && ret < 300 || ret > 399) {
			pmsg = _T("write_data (login)");
			goto failed;
		}

		sTmpA = (CStringA)m_mail.sPass;
		ret = mbedtls_base64_encode(base, sizeof base, &n, (unsigned char*)(LPCSTR)sTmpA, sTmpA.GetLength());
		if (ret != 0) {
			pmsg = _T("mbedtls_base64_encode (password)");
			goto failed;
		}
		sBufA.Format("%s\r\n", base);
		ret = write_data(&ssl, sBufA); //235
		if (ret > 0 && ret < 200 || ret > 299) {
			pmsg = _T("write_data (password)");
			goto failed;
		}
	}

	sSenderA = CStringA(m_mail.sFrom);
	sBufA.Format("MAIL FROM:<%s>\r\n", (LPCSTR)sSenderA);
	ret = write_data(&ssl, sBufA); //250
	if (ret > 0 && ret < 200 || ret > 299) {
		pmsg = _T("write_data (MAIL FROM)");
		goto failed;
	}

	sReceiverA = (CStringA)m_mail.sTo;
	sBufA.Format("RCPT TO:<%s>\r\n", (LPCSTR)sReceiverA);
	ret = write_data(&ssl, sBufA); //250 251
	if (ret > 0 && ret < 200 || ret > 299) {
		pmsg = _T("write_data (RCPT TO)");
		goto failed;
	}

	ret = write_data(&ssl, "DATA\r\n"); //354 250
	if (ret > 0 && ret < 200 || ret > 399) {
		pmsg = _T("write_data (DATA)");
		goto failed;
	}

	sBodyA.Format("Content-Type: text/plain;\r\n"
			"\tformat=flowed;\r\n"
			"\tcharset=\"utf-8\"\r\n"
			"Content-Transfer-Encoding: 8bit\r\n\r\n%s"
			, (LPCSTR)(NeedUTF8String(m_strBody) ? wc2utf8(m_strBody) : (CStringA)m_strBody));

	if (!encoded_word(m_strSubject, sTmpA)) //subject
		goto failed;

	sBufA.Format(
		"From: eMuleBB <%s>\r\n"
		"Subject: %s\r\n"
		"To: <%s>\r\n"
		"MIME-Version: 1.0\r\n"
		"%s\r\n"
		"\r\n.\r\n"
		, (LPCSTR)sSenderA, (LPCSTR)sTmpA, (LPCSTR)sReceiverA, (LPCSTR)sBodyA);

	ret = write_data(&ssl, sBufA);
	if (ret > 0 && ret < 200 || ret > 299) {
		pmsg = _T("write_data");
		goto failed;
	}

	ret = write_data(&ssl, "QUIT\r\n"); //221
	if (ret > 0 && ret < 200 || ret > 299)
		pmsg = _T("write_data (QUIT)");

failed:
	mbedtls_ssl_close_notify(&ssl);

exit:
	mbedtls_net_free(&server_fd);
	mbedtls_ssl_free(&ssl);
	mbedtls_ssl_config_free(&conf);
	mbedtls_ssl_cache_free(&cache);
	mbedtls_ssl_ticket_free(&ticket_ctx);
	mbedtls_pk_free(&pkey);
	mbedtls_psa_crypto_free();
	mbedtls_threading_free_alt();

	if (pmsg)
		DebugLogError(_T("Error: %s returned -0x%04x - %s"), pmsg, -ret, (LPCTSTR)SSLerror(ret));
}

int CNotifierMailThread::write_data(mbedtls_ssl_context *ssl, const char *buf)
{
	return write_data(ssl, buf, buf == NULL ? 0 : strlen(buf));
}

int CNotifierMailThread::write_data(mbedtls_ssl_context *ssl, const char *buf, size_t len)
{
	return m_mail.uTLS == MODE_NONE
		? write_txt((mbedtls_net_context*)ssl->MBEDTLS_PRIVATE(p_bio), buf, len)
		: write_ssl(ssl, buf, len);
}

void CemuleDlg::SendNotificationMail(TbnMsg nMsgType, LPCTSTR pszText)
{
	// FROZEN: legacy notifier mail worker is retained for compatibility only.
	// Do not include this path in RC lifetime hardening unless product unfreezes it.
	if (!thePrefs.IsNotifierSendMailEnabled())
		return;

	EmailSettings mail(thePrefs.GetEmailSettings());
	if (mail.sServer.Trim().IsEmpty() || mail.sTo.Trim().IsEmpty() || mail.sFrom.Trim().IsEmpty())
		return;

	CNotifierMailThread *pThread = static_cast<CNotifierMailThread*>(AfxBeginThread(RUNTIME_CLASS(CNotifierMailThread), THREAD_PRIORITY_LOWEST, 0, CREATE_SUSPENDED));
	if (pThread) {
		pThread->m_mail = mail;
		pThread->m_strSubject = GetResString(IDS_EMULENOTIFICATION);
		UINT uid;
		switch (nMsgType) {
		case TBN_CHAT:
			uid = IDS_PW_TBN_POP_ALWAYS;
			break;
		case TBN_DOWNLOADFINISHED:
			uid = IDS_PW_TBN_ONDOWNLOAD;
			break;
		case TBN_DOWNLOADADDED:
			uid = IDS_TBN_ONNEWDOWNLOAD;
			break;
		case TBN_LOG:
			uid = IDS_PW_TBN_ONLOG;
			break;
		case TBN_IMPORTANTEVENT:
			uid = IDS_ERROR;
			break;
		case TBN_NEWVERSION:
			uid = IDS_CB_TBN_ONNEWVERSION;
			break;
		default:
			uid = 0;
			ASSERT(0);
		}
		if (uid)
			pThread->m_strSubject.AppendFormat(_T(": %s"), (LPCTSTR)GetResString(uid));
		pThread->m_strBody = pszText;
		pThread->ResumeThread();
	}
}
