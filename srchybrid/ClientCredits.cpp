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
#include "ClientCredits.h"
#include "ClientCreditsSeams.h"
#include "OtherFunctions.h"
#include "Preferences.h"
#include "SafeFile.h"
#include "Opcodes.h"
#include "ServerConnect.h"
#include "Log.h"
#include "LongPathSeams.h"
#include <base64.h>
#include <osrng.h>
#include <files.h>
#include <string>
#include <vector>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
/**
 * @brief Loads a Base64-encoded Crypto++ key file through the shared long-path byte reader.
 */
bool ReadBase64TextFileLongPath(LPCTSTR pszPath, std::string &rText)
{
	std::vector<unsigned char> fileBytes;
	if (!LongPathSeams::ReadAllBytes(pszPath, fileBytes))
		return false;

	rText.assign(reinterpret_cast<const char*>(fileBytes.data()), fileBytes.size());
	return true;
}

/**
 * @brief Stores a Base64-encoded Crypto++ key file through the shared long-path byte writer.
 */
bool WriteBase64TextFileLongPath(LPCTSTR pszPath, const std::string &rText)
{
	return LongPathSeams::WriteAllBytes(pszPath, reinterpret_cast<const unsigned char*>(rText.data()), rText.size());
}
}

#define CLIENTS_MET_FILENAME	_T("clients.met")

CClientCredits::CClientCredits(const CreditStruct &in_credits)
	: m_Credits(in_credits)
{
	InitalizeIdent();
	ClearWaitStartTime();
	m_dwWaitTimeIP = 0;
}

CClientCredits::CClientCredits(const uchar *key)
	: m_Credits()
{
	md4cpy(&m_Credits.abyKey, key);
	InitalizeIdent();
	m_dwSecureWaitTime = m_dwUnSecureWaitTime = ::GetTickCount64();
	m_dwWaitTimeIP = 0;
}

void CClientCredits::AddDownloaded(uint32 bytes, uint32 dwForIP)
{
	switch (GetCurrentIdentState(dwForIP)) {
	case IS_IDFAILED:
	case IS_IDBADGUY:
	case IS_IDNEEDED:
		if (theApp.clientcredits->CryptoAvailable())
			return;
	}

	uint64 current = GetDownloadedTotal() + bytes;
	//recode
	m_Credits.nDownloadedLo = LODWORD(current);
	m_Credits.nDownloadedHi = HIDWORD(current);
}

void CClientCredits::AddUploaded(uint32 bytes, uint32 dwForIP)
{
	switch (GetCurrentIdentState(dwForIP)) {
	case IS_IDFAILED:
	case IS_IDBADGUY:
	case IS_IDNEEDED:
		if (theApp.clientcredits->CryptoAvailable())
			return;
	}

	uint64 current = GetUploadedTotal() + bytes;
	//recode
	m_Credits.nUploadedLo = LODWORD(current);
	m_Credits.nUploadedHi = HIDWORD(current);
}

uint64 CClientCredits::GetUploadedTotal() const
{
	return ((uint64)m_Credits.nUploadedHi << 32) | m_Credits.nUploadedLo;
}

uint64 CClientCredits::GetDownloadedTotal() const
{
	return ((uint64)m_Credits.nDownloadedHi << 32) | m_Credits.nDownloadedLo;
}

float CClientCredits::GetScoreRatio(uint32 dwForIP) const
{
	// check the client ident status
	switch (GetCurrentIdentState(dwForIP)) {
	case IS_IDFAILED:
	case IS_IDBADGUY:
	case IS_IDNEEDED:
		if (theApp.clientcredits->CryptoAvailable())
			// bad guy - no credits for you
			return 1.0f;
	}

	const uint64 downloadedTotal = GetDownloadedTotal();
	if (downloadedTotal < 1048576)
		return 1.0f;
	float result;
	const uint64 uploadedTotal = GetUploadedTotal();
	if (uploadedTotal)
		result = static_cast<float>(downloadedTotal * 2) / static_cast<float>(uploadedTotal);
	else
		result = 10.0f;

	// exponential calculation of the max multiplicator based on uploaded data (9.2MB = 3.34, 100MB = 10.0)
	float result2 = static_cast<float>(sqrt(static_cast<float>(downloadedTotal) / 1048576.0f + 2.0f));

	// linear calculation of the max multiplicator based on uploaded data for the first chunk (1MB = 1.01, 9.2MB = 3.34)
	float result3;
	if (downloadedTotal < 9646899)
		result3 = static_cast<float>(downloadedTotal - 1048576) / 8598323.0f * 2.34f + 1.0f;
	else
		result3 = 10.0f;

	// take the smallest result
	result = min(result, min(result2, result3));

	if (result < 1.0f)
		return 1.0f;
	return min(result, 10.0f);
}


CClientCreditsList::CClientCreditsList()
{
	m_nLastSaved = ::GetTickCount64();
	LoadList();

	InitalizeCrypting();
}

CClientCreditsList::~CClientCreditsList()
{
	SaveList();
	CCKey tmpkey;
	for (POSITION pos = m_mapClients.GetStartPosition(); pos != NULL;) {
		CClientCredits *cur_credit;
		m_mapClients.GetNextAssoc(pos, tmpkey, cur_credit);
		delete cur_credit;
	}
	delete m_pSignkey;
}

void CClientCreditsList::LoadList()
{
	const CString &sConfDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	const CString &strFileName(sConfDir + CLIENTS_MET_FILENAME);
	const int iOpenFlags = CFile::modeRead | CFile::osSequentialScan | CFile::typeBinary | CFile::shareDenyWrite;
	CSafeBufferedFile file;
	if (!CFileOpen(file, strFileName, iOpenFlags, GetResString(IDS_ERR_LOADCREDITFILE)))
		return;

	::setvbuf(file.m_pStream, NULL, _IOFBF, 16384);
	try {
		uint8 version = file.ReadUInt8();
		if (version != CREDITFILE_VERSION && version != CREDITFILE_VERSION_29) {
			LogWarning(GetResString(IDS_ERR_CREDITFILEOLD));
			file.Close();
			return;
		}

		// everything is OK, lets see if the backup exist...
		const CString &strBakFileName(sConfDir + CLIENTS_MET_FILENAME _T(".bak"));

		BOOL bCreateBackup = TRUE;

		HANDLE hBakFile = LongPathSeams::CreateFile(strBakFileName, GENERIC_READ, FILE_SHARE_READ, NULL
									, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hBakFile != INVALID_HANDLE_VALUE) {
			// OK, the backup exist, get the size
			DWORD dwBakFileSize = ::GetFileSize(hBakFile, NULL); //debug
			if (dwBakFileSize > (DWORD)file.GetLength()) {
				// the size of the backup was larger then the orig. file, something is wrong here, don't overwrite old backup.
				bCreateBackup = FALSE;
			}
			//else: backup is smaller or the same size as orig. file, proceed with copying of file
			::CloseHandle(hBakFile);
		}
		//else: the backup doesn't exist, create it

		if (bCreateBackup) {
			file.Close(); // close the file before copying

			if (!LongPathSeams::CopyFile(strFileName, strBakFileName, FALSE))
				LogError(GetResString(IDS_ERR_MAKEBAKCREDITFILE));

			// reopen file
			if (!CFileOpen(file, strFileName, iOpenFlags, GetResString(IDS_ERR_LOADCREDITFILE)))
				return;

			::setvbuf(file.m_pStream, NULL, _IOFBF, 16384);
			file.Seek(1, CFile::begin); //set file pointer behind file version byte
		}

		uint32 count = file.ReadUInt32();
		m_mapClients.InitHashTable(count + 5000); // TODO: should be prime number... and 20% larger

		const time_t tExpired = time(NULL) - DAY2S(150); // today - 150 days
		uint32 cDeleted = 0;
		for (uint32 i = 0; i < count; ++i) {
			CreditStruct newcstruct{};
			file.Read(&newcstruct, (version == CREDITFILE_VERSION_29) ? sizeof(CreditStruct_29a) : sizeof(CreditStruct));

			if (newcstruct.tLastSeen < (uint32)tExpired)
				++cDeleted;
			else {
				CClientCredits *newcredits = new CClientCredits(newcstruct);
				m_mapClients[CCKey(newcredits->GetKey())] = newcredits;
			}
		}
		file.Close();

		if (cDeleted > 0)
			AddLogLine(false, GetResString(IDS_CREDITFILELOADED) + GetResString(IDS_CREDITSEXPIRED), count - cDeleted, cDeleted);
		else
			AddLogLine(false, GetResString(IDS_CREDITFILELOADED), count);
	} catch (CFileException *ex) {
		if (ex->m_cause == CFileException::endOfFile)
			LogError(LOG_STATUSBAR, GetResString(IDS_CREDITFILECORRUPT));
		else
			LogError(LOG_STATUSBAR, GetResString(IDS_ERR_CREDITFILEREAD), (LPCTSTR)CExceptionStr(*ex));
		ex->Delete();
	}
}

void CClientCreditsList::SaveList()
{
	if (thePrefs.GetLogFileSaving())
		AddDebugLogLine(false, _T("Saving clients credit list file \"%s\""), CLIENTS_MET_FILENAME);
	m_nLastSaved = ::GetTickCount64();

	CSafeFile file;// no buffering needed here since we swap out the entire array
	if (!CFileOpen(file
		, thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + CLIENTS_MET_FILENAME
		, CFile::modeWrite | CFile::modeCreate | CFile::typeBinary | CFile::shareDenyWrite
		, GetResString(IDS_ERR_FAILED_CREDITSAVE)))
	{
		return;
	}
	byte *pBuffer = new byte[m_mapClients.GetCount() * sizeof(CreditStruct)]; //not CreditStruct[] because of alignment
	uint32 count = 0;
	for (const CClientCreditsMap::CPair *pair = m_mapClients.PGetFirstAssoc(); pair != NULL; pair = m_mapClients.PGetNextAssoc(pair)) {
		const CClientCredits *cur_credit = pair->value;
		if (cur_credit->GetUploadedTotal() || cur_credit->GetDownloadedTotal())
			*reinterpret_cast<CreditStruct*>(&pBuffer[sizeof(CreditStruct) * count++]) = cur_credit->m_Credits;
	}

	try {
		uint8 version = CREDITFILE_VERSION;
		file.Write(&version, 1);
		file.Write(&count, 4);
		file.Write(pBuffer, (UINT)(count * sizeof(CreditStruct)));
		file.Close();
	} catch (CFileException *ex) {
		LogError(LOG_STATUSBAR, _T("%s%s"), (LPCTSTR)GetResString(IDS_ERR_FAILED_CREDITSAVE), (LPCTSTR)CExceptionStrDash(*ex));
		ex->Delete();
	}

	delete[] pBuffer;
}

CClientCredits* CClientCreditsList::GetCredit(const uchar *key)
{
	CCKey tkey(key);
	CClientCredits *result;
	if (!m_mapClients.Lookup(tkey, result)) {
		result = new CClientCredits(key);
		m_mapClients[CCKey(result->GetKey())] = result;
	}
	result->SetLastSeen();
	return result;
}

void CClientCreditsList::Process()
{
	if (::GetTickCount64() >= m_nLastSaved + MIN2MS(13))
		SaveList();
}

void CClientCredits::InitalizeIdent()
{
	if (m_Credits.nKeySize == 0) {
		memset(m_abyPublicKey, 0, sizeof m_abyPublicKey); // for debugging
		m_nPublicKeyLen = 0;
		IdentState = IS_NOTAVAILABLE;
	} else {
		m_nPublicKeyLen = m_Credits.nKeySize;
		memcpy(m_abyPublicKey, m_Credits.abySecureIdent, m_nPublicKeyLen);
		IdentState = IS_IDNEEDED;
	}
	m_dwCryptRndChallengeFor = 0;
	m_dwCryptRndChallengeFrom = 0;
	m_dwIdentIP = 0;
}

void CClientCredits::Verified(uint32 dwForIP)
{
	m_dwIdentIP = dwForIP;
	// client was verified, copy the key to store him if not done already
	if (m_Credits.nKeySize == 0) {
		m_Credits.nKeySize = m_nPublicKeyLen;
		memcpy(m_Credits.abySecureIdent, m_abyPublicKey, m_nPublicKeyLen);
		if (GetDownloadedTotal() > 0) {
			// for security reason, we have to delete all prior credits here
			m_Credits.nDownloadedHi = 0;
			m_Credits.nDownloadedLo = 1;
			m_Credits.nUploadedHi = 0;
			m_Credits.nUploadedLo = 1; // in order to save this client, set 1 byte
			if (thePrefs.GetVerbose())
				DEBUG_ONLY(AddDebugLogLine(false, _T("Credits deleted due to new SecureIdent")));
		}
	}
	IdentState = IS_IDENTIFIED;
}

bool CClientCredits::SetSecureIdent(const uchar *pachIdent, uint8 nIdentLen)  // verified Public key cannot change, use only if there is no public key yet
{
	if (MAXPUBKEYSIZE < nIdentLen || m_Credits.nKeySize != 0)
		return false;
	memcpy(m_abyPublicKey, pachIdent, nIdentLen);
	m_nPublicKeyLen = nIdentLen;
	IdentState = IS_IDNEEDED;
	return true;
}

EIdentState	CClientCredits::GetCurrentIdentState(uint32 dwForIP) const
{
	if (IdentState != IS_IDENTIFIED)
		return IdentState;
	if (dwForIP == m_dwIdentIP)
		return IS_IDENTIFIED;
	return IS_IDBADGUY;
	// mod note: clients which just reconnected after an IP change and have to ident yet will also have this state for 1-2 seconds
	//		 so don't try to spam such clients with "bad guy" messages (besides: spam messages are always bad)
}

using namespace CryptoPP;

void CClientCreditsList::InitalizeCrypting()
{
	m_nMyPublicKeyLen = 0;
	memset(m_abyMyPublicKey, 0, sizeof m_abyMyPublicKey); // not really needed; better for debugging tho
	m_pSignkey = NULL;
	if (!thePrefs.IsSecureIdentEnabled())
		return;
	// check if keyfile is there
	bool bCreateNewKey = false;
	const CString &cryptkeypath(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + _T("cryptkey.dat"));
	std::string keyFileText;
	if (!ReadBase64TextFileLongPath(cryptkeypath, keyFileText) || keyFileText.empty())
		bCreateNewKey = true;
	if (bCreateNewKey)
		CreateKeyPair();

	// load key
	try {
		if (!ReadBase64TextFileLongPath(cryptkeypath, keyFileText) || keyFileText.empty())
			throw 0;

		// load private key
		StringSource filesource(reinterpret_cast<const byte*>(keyFileText.data()), keyFileText.size(), true, new Base64Decoder);
		m_pSignkey = new RSASSA_PKCS1v15_SHA_Signer(filesource);
		// calculate and store public key
		RSASSA_PKCS1v15_SHA_Verifier pubkey(*m_pSignkey);
		ArraySink asink(m_abyMyPublicKey, sizeof m_abyMyPublicKey);
		pubkey.GetMaterial().Save(asink);
		m_nMyPublicKeyLen = (uint8)asink.TotalPutLength();
		asink.MessageEnd();
	} catch (...) {
		delete m_pSignkey;
		ResetClientCreditsCryptState(m_pSignkey, m_abyMyPublicKey, m_nMyPublicKeyLen);
		LogError(LOG_STATUSBAR, GetResString(IDS_CRYPT_INITFAILED));
		ASSERT(0);
	}
	ASSERT(Debug_CheckCrypting());
}

bool CClientCreditsList::CreateKeyPair()
{
	try {
		AutoSeededRandomPool rng;
		InvertibleRSAFunction privkey;
		privkey.Initialize(rng, RSAKEYSIZE);

		std::string encodedKey;
		Base64Encoder privkeysink(new StringSink(encodedKey));
		privkey.DEREncode(privkeysink);
		privkeysink.MessageEnd();
		if (!WriteBase64TextFileLongPath(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + _T("cryptkey.dat"), encodedKey))
			return false;

		if (thePrefs.GetLogSecureIdent())
			AddDebugLogLine(false, _T("Created new RSA keypair"));
		return true;
	} catch (...) {
		if (ShouldLogClientCreditsFailureDetail(thePrefs.GetVerbose()))
			AddDebugLogLine(false, _T("Failed to create new RSA keypair"));
		ASSERT(0);
	}
	return false;
}

uint8 CClientCreditsList::CreateSignature(CClientCredits *pTarget, uchar *pachOutput, uint8 nMaxSize
	, uint32 ChallengeIP, uint8 byChaIPKind, CryptoPP::RSASSA_PKCS1v15_SHA_Signer *sigkey) const
{
	ASSERT(pTarget != NULL && pachOutput != NULL);
	if (pTarget == NULL || pachOutput == NULL)
		return GetClientCreditsSignatureFailureResult();

	// sigkey param is used for debug only
	if (sigkey == NULL)
		sigkey = m_pSignkey;

	// create a signature of the public key from pTarget
	if (!CryptoAvailable())
		return GetClientCreditsSignatureFailureResult();
	try {
		SecByteBlock sbbSignature(sigkey->SignatureLength());
		if (!CanStoreClientCreditsSignature(sbbSignature.size(), nMaxSize)) {
			if (ShouldLogClientCreditsFailureDetail(thePrefs.GetVerbose()))
				AddDebugLogLine(false, _T("Failed to create secure-ident signature: output buffer too small (%u < %Iu)"), nMaxSize, sbbSignature.size());
			return GetClientCreditsSignatureFailureResult();
		}

		AutoSeededRandomPool rng;
		byte abyBuffer[MAXPUBKEYSIZE + 9];
		size_t keylen = pTarget->GetSecIDKeyLen();
		ClientCreditsChallengeLayout layout = {};
		if (!TryBuildClientCreditsChallengeLayout(keylen, byChaIPKind != 0, layout)) {
			if (ShouldLogClientCreditsFailureDetail(thePrefs.GetVerbose()))
				AddDebugLogLine(false, _T("Failed to create secure-ident signature: invalid public key length %Iu"), keylen);
			return GetClientCreditsSignatureFailureResult();
		}

		memcpy(abyBuffer, pTarget->GetSecureIdent(), keylen);
		// 4 additional bytes of random data sent from this client
		uint32 challenge = pTarget->m_dwCryptRndChallengeFrom;
		ASSERT(challenge);
		PokeUInt32(&abyBuffer[keylen], challenge);
		if (byChaIPKind != 0) {
			PokeUInt32(&abyBuffer[keylen + 4], ChallengeIP);
			abyBuffer[keylen + 4 + 4] = byChaIPKind;
		}
		sigkey->SignMessage(rng, abyBuffer, layout.nMessageLength, sbbSignature.begin());
		ArraySink asink(pachOutput, nMaxSize);
		asink.Put(sbbSignature.begin(), sbbSignature.size());
		return (uint8)asink.TotalPutLength();
	} catch (...) {
		if (ShouldLogClientCreditsFailureDetail(thePrefs.GetVerbose()))
			AddDebugLogLine(false, _T("Failed to create secure-ident signature due to an unexpected exception"));
		ASSERT(0);
	}
	return GetClientCreditsSignatureFailureResult();
}

bool CClientCreditsList::VerifyIdent(CClientCredits *pTarget, const uchar *pachSignature, uint8 nInputSize,
	uint32 dwForIP, uint8 byChaIPKind)
{
	ASSERT(pTarget);
	ASSERT(pachSignature);
	if (pTarget == NULL || pachSignature == NULL)
		return false;

	if (!CryptoAvailable()) {
		pTarget->IdentState = IS_NOTAVAILABLE;
		return false;
	}
	bool bResult;
	try {
		StringSource ss_Pubkey((byte*)pTarget->GetSecureIdent(), pTarget->GetSecIDKeyLen(), true, 0);
		RSASSA_PKCS1v15_SHA_Verifier pubkey(ss_Pubkey);
		// 4 additional bytes random data send from this client +5 bytes v2
		byte abyBuffer[MAXPUBKEYSIZE + 9];
		ClientCreditsChallengeLayout layout = {};
		bool bCanBuildChallenge = TryBuildClientCreditsChallengeLayout(m_nMyPublicKeyLen, byChaIPKind != 0, layout);
		if (!bCanBuildChallenge) {
			if (ShouldLogClientCreditsFailureDetail(thePrefs.GetVerbose()))
				AddDebugLogLine(false, _T("Error: Invalid secure-ident public key length in %hs"), __FUNCTION__);
		}

		if (bCanBuildChallenge) {
			memcpy(abyBuffer, m_abyMyPublicKey, m_nMyPublicKeyLen);
			uint32 challenge = pTarget->m_dwCryptRndChallengeFor;
			ASSERT(challenge);
			PokeUInt32(&abyBuffer[m_nMyPublicKeyLen], challenge);

			// v2 security improvements (not supported by 29b, not used as default by 29c)
			if (byChaIPKind != 0) {
				uint32 ChallengeIP = 0;
				switch (byChaIPKind) {
				case CRYPT_CIP_LOCALCLIENT:
					ChallengeIP = dwForIP;
					break;
				case CRYPT_CIP_REMOTECLIENT:
					if (theApp.serverconnect->GetClientID() == 0 || theApp.serverconnect->IsLowID()) {
						if (thePrefs.GetLogSecureIdent())
							AddDebugLogLine(false, _T("Warning: Maybe SecureHash Ident fails because LocalIP is unknown"));
						ChallengeIP = theApp.serverconnect->GetLocalIP();
					} else
						ChallengeIP = theApp.serverconnect->GetClientID();
					break;
				case CRYPT_CIP_NONECLIENT: // maybe not supported in future versions
					ChallengeIP = 0;
				}
				PokeUInt32(&abyBuffer[m_nMyPublicKeyLen + 4], ChallengeIP);
				abyBuffer[m_nMyPublicKeyLen + 4 + 4] = byChaIPKind;
			}
			//v2 end

			bResult = pubkey.VerifyMessage(abyBuffer, layout.nMessageLength, pachSignature, nInputSize);
		} else
			bResult = false;
	} catch (...) {
		if (ShouldLogClientCreditsFailureDetail(thePrefs.GetVerbose()))
			AddDebugLogLine(false, _T("Error: Unknown exception in %hs"), __FUNCTION__);
		//ASSERT(0);
		bResult = false;
	}
	if (!bResult) {
		if (pTarget->IdentState == IS_IDNEEDED)
			pTarget->IdentState = IS_IDFAILED;
	} else
		pTarget->Verified(dwForIP);

	return bResult;
}

bool CClientCreditsList::CryptoAvailable() const
{
	return m_nMyPublicKeyLen > 0 && m_pSignkey != NULL && thePrefs.IsSecureIdentEnabled();
}

#ifdef _DEBUG
bool CClientCreditsList::Debug_CheckCrypting()
{
	// create random key
	AutoSeededRandomPool rng;

	RSASSA_PKCS1v15_SHA_Signer priv(rng, 384);
	RSASSA_PKCS1v15_SHA_Verifier pub(priv);

	byte abyPublicKey[80];
	ArraySink asink(abyPublicKey, sizeof abyPublicKey);
	pub.GetMaterial().Save(asink);
	uint8 PublicKeyLen = (uint8)asink.TotalPutLength();
	asink.MessageEnd();
	uint32 challenge = GetRandomUInt32();
	// create fake client which pretends to be this emule
	CreditStruct emptystruct{};
	CClientCredits newcredits(emptystruct);
	newcredits.SetSecureIdent(m_abyMyPublicKey, m_nMyPublicKeyLen);
	newcredits.m_dwCryptRndChallengeFrom = challenge;
	// create signature with fake priv key
	uchar pachSignature[200] = {};
	uint8 sigsize = CreateSignature(&newcredits, pachSignature, sizeof pachSignature, 0, 0, &priv);

	// next fake client uses the random created public key
	CClientCredits newcredits2(emptystruct);
	newcredits2.m_dwCryptRndChallengeFor = challenge;

	// if you uncomment one of the following lines the check has to fail
	//abyPublicKey[5] = 34;
	//m_abyMyPublicKey[5] = 22;
	//pachSignature[5] = 232;

	newcredits2.SetSecureIdent(abyPublicKey, PublicKeyLen);

	//now verify this signature - if it's true everything is fine
	bool bResult = VerifyIdent(&newcredits2, pachSignature, sigsize, 0, 0);

	return bResult;
}
#endif

ULONGLONG CClientCredits::GetSecureWaitStartTime(uint32 dwForIP)
{
	if (m_dwUnSecureWaitTime == 0 || m_dwSecureWaitTime == 0)
		SetSecWaitStartTime(dwForIP);

	if (m_Credits.nKeySize != 0) {	// this client is a SecureHash Client
		if (GetCurrentIdentState(dwForIP) == IS_IDENTIFIED) // good boy
			return m_dwSecureWaitTime;

		// not so good boy
		if (dwForIP == m_dwWaitTimeIP)
			return m_dwUnSecureWaitTime;

		// bad boy
		// this can also happen if the client has not identified himself yet, but will do later - so maybe he is not a bad boy :) .
		/*CString buffer2, buffer;
		for (int i = 0; i < 16; ++i) {
			buffer2.Format("%02X", m_Credits.abyKey[i]);
			buffer += buffer2;
		}
		if (thePrefs.GetLogSecureIdent())
			AddDebugLogLine(false, "Warning: WaitTime reset due to Invalid Ident for Userhash %s", buffer);*/

		m_dwUnSecureWaitTime = ::GetTickCount64();
		m_dwWaitTimeIP = dwForIP;
	}
	// not a SecureHash Client - handle it like before for now (no security checks)
	return m_dwUnSecureWaitTime;
}

void CClientCredits::SetSecWaitStartTime(uint32 dwForIP)
{
	m_dwUnSecureWaitTime = ::GetTickCount64() - 1;
	m_dwSecureWaitTime = m_dwUnSecureWaitTime;
	m_dwWaitTimeIP = dwForIP;
}

void CClientCredits::ClearWaitStartTime()
{
	m_dwUnSecureWaitTime = 0;
	m_dwSecureWaitTime = 0;
}
