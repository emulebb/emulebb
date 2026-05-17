//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
#include "stdafx.h"
#include "WebServerCertificate.h"

#include <array>
#include <cstring>

#include "LongPathSeams.h"
#include "Log.h"
#include "OtherFunctions.h"
#include "TLSthreading.h"

#include "mbedtls/asn1.h"
#include "mbedtls/oid.h"
#include "mbedtls/x509.h"
#include "mbedtls/x509_crt.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
struct SSANNode
{
	mbedtls_x509_san_list node;
	CStringA strValue;
	std::array<unsigned char, 16> abyIpAddress;
};

int WriteBuffer(LPCTSTR pszOutputFile, const unsigned char *pBuffer)
{
	const size_t uLength = strlen(reinterpret_cast<const char*>(pBuffer));
	if (!LongPathSeams::WriteAllBytes(pszOutputFile, pBuffer, uLength))
		return -1;
	return 0;
}

int WritePrivateKey(const mbedtls_pk_context *pKey, LPCTSTR pszOutputFile)
{
	unsigned char outputBuffer[16000];

	const int iResult = mbedtls_pk_write_key_pem(pKey, outputBuffer, sizeof(outputBuffer));
	return iResult != 0 ? iResult : WriteBuffer(pszOutputFile, outputBuffer);
}

int CreateRsaPrivateKey(mbedtls_pk_context *pKey, LPCTSTR pszOutputFile)
{
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_set_key_algorithm(&attributes, PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_ANY_HASH));
	psa_set_key_type(&attributes, PSA_KEY_TYPE_RSA_KEY_PAIR);
	psa_set_key_bits(&attributes, 2048);
	psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_VERIFY_HASH | PSA_KEY_USAGE_EXPORT);

	LPCTSTR pszMessage = NULL;
	mbedtls_svc_key_id_t keyId;
	int iResult = static_cast<int>(psa_generate_key(&attributes, &keyId));
	psa_reset_key_attributes(&attributes);

	if (iResult != 0) {
		pszMessage = _T("psa_generate_key");
	} else {
		iResult = mbedtls_pk_wrap_psa(pKey, keyId);
		if (iResult != 0) {
			pszMessage = _T("mbedtls_pk_wrap_psa");
		} else {
			iResult = WritePrivateKey(pKey, pszOutputFile);
			if (iResult != 0)
				pszMessage = _T("WritePrivateKey");
		}
	}
	if (pszMessage != NULL)
		DebugLogError(_T("Error: %s returned -0x%04x - %s"), pszMessage, -iResult, (LPCTSTR)SSLerror(iResult));
	return iResult;
}

int WriteCertificate(mbedtls_x509write_cert *pCertificate, LPCTSTR pszOutputFile)
{
	unsigned char outputBuffer[8192];

	const int iResult = mbedtls_x509write_crt_pem(pCertificate, outputBuffer, sizeof(outputBuffer));
	return iResult != 0 ? iResult : WriteBuffer(pszOutputFile, outputBuffer);
}

bool TryParseIpAddress(const CStringA &strValue, std::array<unsigned char, 16> &rabyAddress, size_t &ruLength)
{
	rabyAddress.fill(0);
	IN_ADDR ipv4Address = {};
	if (::InetPtonA(AF_INET, strValue, &ipv4Address) == 1) {
		memcpy(rabyAddress.data(), &ipv4Address, sizeof(ipv4Address));
		ruLength = sizeof(ipv4Address);
		return true;
	}
	IN6_ADDR ipv6Address = {};
	if (::InetPtonA(AF_INET6, strValue, &ipv6Address) == 1) {
		memcpy(rabyAddress.data(), &ipv6Address, sizeof(ipv6Address));
		ruLength = sizeof(ipv6Address);
		return true;
	}
	return false;
}

void AppendSanNode(std::vector<SSANNode> &raNodes, int iType, const CStringA &strValue)
{
	SSANNode node = {};
	node.strValue = strValue;
	node.node.node.type = iType;
	node.node.next = NULL;
	raNodes.push_back(node);
}

mbedtls_x509_san_list *BuildSubjectAlternativeNameList(std::vector<SSANNode> &raNodes, const WebServerCertificate::SGenerationRequest &request)
{
	for (const CStringA &strIpAddress : request.astrIpAddresses)
		AppendSanNode(raNodes, MBEDTLS_X509_SAN_IP_ADDRESS, strIpAddress);
	for (const CStringA &strDnsName : request.astrDnsNames)
		AppendSanNode(raNodes, MBEDTLS_X509_SAN_DNS_NAME, strDnsName);
	for (size_t uIndex = 0; uIndex < raNodes.size(); ++uIndex) {
		SSANNode &rNode = raNodes[uIndex];
		if (rNode.node.node.type == MBEDTLS_X509_SAN_IP_ADDRESS) {
			size_t uIpLength = 0;
			if (!TryParseIpAddress(rNode.strValue, rNode.abyIpAddress, uIpLength))
				continue;
			rNode.node.node.san.unstructured_name.p = rNode.abyIpAddress.data();
			rNode.node.node.san.unstructured_name.len = uIpLength;
		} else {
			rNode.node.node.san.unstructured_name.p = reinterpret_cast<unsigned char*>(const_cast<char*>(rNode.strValue.GetString()));
			rNode.node.node.san.unstructured_name.len = static_cast<size_t>(rNode.strValue.GetLength());
		}
		raNodes[uIndex].node.next = (uIndex + 1 < raNodes.size()) ? &raNodes[uIndex + 1].node : NULL;
	}
	return raNodes.empty() ? NULL : &raNodes.front().node;
}

void SetServerAuthOid(mbedtls_asn1_sequence &rSequence)
{
	rSequence = {};
	rSequence.buf.len = MBEDTLS_OID_SIZE(MBEDTLS_OID_SERVER_AUTH);
	rSequence.buf.p = reinterpret_cast<unsigned char*>(const_cast<char*>(MBEDTLS_OID_SERVER_AUTH));
	rSequence.buf.tag = MBEDTLS_ASN1_OID;
}
}

void WebServerCertificate::BuildDefaultValidityWindow(CStringA &rstrNotBefore, CStringA &rstrNotAfter)
{
	SYSTEMTIME st;
	GetSystemTime(&st);
	rstrNotBefore.Format("%4hu%02hu01000000", st.wYear, st.wMonth);
	rstrNotAfter.Format("%4hu%02hu01000000", st.wYear + 1, st.wMonth);
}

WebServerCertificate::SGenerationRequest WebServerCertificate::BuildDefaultLocalRequest(const CString &strKeyFile, const CString &strCertFile)
{
	SGenerationRequest request = {};
	request.strKeyFile = strKeyFile;
	request.strCertFile = strCertFile;
	request.strSubjectName = "CN=eMule BB WebServer,O=eMule BB,OU=REST";
	request.strIssuerName = "CN=eMule BB Local WebServer CA,O=eMule BB";
	BuildDefaultValidityWindow(request.strNotBefore, request.strNotAfter);
	request.uSerial = _byteswap_ushort(rand() & 0x0fff);
	if (request.uSerial == 0)
		++request.uSerial;
	request.astrIpAddresses.push_back("127.0.0.1");
	request.astrDnsNames.push_back("localhost");
	return request;
}

int WebServerCertificate::CreateSelfSignedCertificate(const SGenerationRequest &request)
{
	mbedtls_pk_context issuerKey;
	mbedtls_x509write_cert certificate;
	LPCTSTR pszMessage = NULL;

	mbedtls_threading_set_alt(threading_mutex_init_alt, threading_mutex_destroy_alt, threading_mutex_lock_alt, threading_mutex_unlock_alt,
		cond_init_alt, cond_destroy_alt, cond_signal_alt, cond_broadcast_alt, cond_wait_alt);
	psa_crypto_init();
	mbedtls_pk_init(&issuerKey);
	mbedtls_x509write_crt_init(&certificate);

	int iResult = CreateRsaPrivateKey(&issuerKey, request.strKeyFile);
	if (iResult != 0)
		goto exit;

	mbedtls_x509write_crt_set_subject_key(&certificate, &issuerKey);
	mbedtls_x509write_crt_set_issuer_key(&certificate, &issuerKey);

	iResult = mbedtls_x509write_crt_set_subject_name(&certificate, request.strSubjectName);
	if (iResult != 0) {
		pszMessage = _T("mbedtls_x509write_crt_set_subject_name");
		goto exit;
	}
	iResult = mbedtls_x509write_crt_set_issuer_name(&certificate, request.strIssuerName);
	if (iResult != 0) {
		pszMessage = _T("mbedtls_x509write_crt_set_issuer_name");
		goto exit;
	}

	mbedtls_x509write_crt_set_version(&certificate, MBEDTLS_X509_CRT_VERSION_3);
	mbedtls_x509write_crt_set_md_alg(&certificate, MBEDTLS_MD_SHA256);

	iResult = mbedtls_x509write_crt_set_serial_raw(&certificate, reinterpret_cast<const unsigned char*>(&request.uSerial), 1 + static_cast<size_t>(request.uSerial > 0xff));
	if (iResult != 0) {
		pszMessage = _T("mbedtls_x509write_crt_set_serial_raw");
		goto exit;
	}
	iResult = mbedtls_x509write_crt_set_validity(&certificate, request.strNotBefore, request.strNotAfter);
	if (iResult != 0) {
		pszMessage = _T("mbedtls_x509write_crt_set_validity");
		goto exit;
	}
	iResult = mbedtls_x509write_crt_set_basic_constraints(&certificate, 0, 0);
	if (iResult != 0) {
		pszMessage = _T("mbedtls_x509write_crt_set_basic_constraints");
		goto exit;
	}
	iResult = mbedtls_x509write_crt_set_key_usage(&certificate, MBEDTLS_X509_KU_DIGITAL_SIGNATURE | MBEDTLS_X509_KU_KEY_ENCIPHERMENT);
	if (iResult != 0) {
		pszMessage = _T("mbedtls_x509write_crt_set_key_usage");
		goto exit;
	}
	{
		mbedtls_asn1_sequence serverAuthExt;
		SetServerAuthOid(serverAuthExt);
		iResult = mbedtls_x509write_crt_set_ext_key_usage(&certificate, &serverAuthExt);
		if (iResult != 0) {
			pszMessage = _T("mbedtls_x509write_crt_set_ext_key_usage");
			goto exit;
		}
	}
	{
		std::vector<SSANNode> sanNodes;
		mbedtls_x509_san_list *pSanList = BuildSubjectAlternativeNameList(sanNodes, request);
		if (pSanList != NULL) {
			iResult = mbedtls_x509write_crt_set_subject_alternative_name(&certificate, pSanList);
			if (iResult != 0) {
				pszMessage = _T("mbedtls_x509write_crt_set_subject_alternative_name");
				goto exit;
			}
		}
	}
	iResult = mbedtls_x509write_crt_set_subject_key_identifier(&certificate);
	if (iResult != 0) {
		pszMessage = _T("mbedtls_x509write_crt_set_subject_key_identifier");
		goto exit;
	}
	iResult = mbedtls_x509write_crt_set_authority_key_identifier(&certificate);
	if (iResult != 0) {
		pszMessage = _T("mbedtls_x509write_crt_set_authority_key_identifier");
		goto exit;
	}

	iResult = WriteCertificate(&certificate, request.strCertFile);
	if (iResult != 0)
		pszMessage = _T("WriteCertificate");

exit:
	mbedtls_x509write_crt_free(&certificate);
	mbedtls_pk_free(&issuerKey);
	mbedtls_psa_crypto_free();
	mbedtls_threading_free_alt();

	if (pszMessage != NULL)
		DebugLogError(_T("Error: %s returned -0x%04x - %s"), pszMessage, -iResult, (LPCTSTR)SSLerror(iResult));
	return iResult;
}
