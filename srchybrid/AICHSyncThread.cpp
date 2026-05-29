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
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#include "StdAfx.h"
#include "aichsyncthread.h"
#include "AICHSyncThreadSeams.h"
#include "shahashset.h"
#include "safefile.h"
#include "knownfile.h"
#include "emule.h"
#include "emuledlg.h"
#include "sharedfilelist.h"
#include "knownfilelist.h"
#include "sharedfileswnd.h"
#include "DownloadQueue.h"
#include "PartFile.h"
#include "Log.h"
#include "OtherFunctions.h"
#include "UserMsgs.h"
#include "WorkerUiMessageSeams.h"

#include <limits>
#include <vector>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	void SnapshotSharedAICHFileHashes(CArray<CSKey, const CSKey&> &aSharedFileHashes)
	{
		aSharedFileHashes.RemoveAll();
		CSharedFileList *pSharedFiles = theApp.sharedfiles;
		if (pSharedFiles == NULL || theApp.IsClosing())
			return;

		CSingleLock sharelock(&pSharedFiles->m_mutWriteList, TRUE);
		for (POSITION pos = BEFORE_START_POSITION; pos != NULL;) {
			CKnownFile *pFile = pSharedFiles->GetFileNext(pos);
			if (pFile != NULL && !pFile->IsPartFile())
				aSharedFileHashes.Add(CSKey(pFile->GetFileHash()));
		}
	}

	bool HasPendingPartFileHashing()
	{
		if (theApp.downloadqueue == NULL)
			return false;

		POSITION pos = NULL;
		for (INT_PTR i = 0, iCount = theApp.downloadqueue->GetFileCount(); i < iCount; ++i) {
			const CPartFile *pPartFile = theApp.downloadqueue->GetFileNext(pos);
			if (pPartFile == NULL)
				break;
			if (pPartFile != NULL && (pPartFile->GetStatus() == PS_WAITINGFORHASH || pPartFile->GetStatus() == PS_HASHING))
				return true;
		}
		return false;
	}

	bool ResolveSharedAICHSyncFileNoLock(CSharedFileList *pSharedFiles, const uchar *pFileHash, CKnownFile *&rpResolvedFile)
	{
		rpResolvedFile = NULL;
		if (pSharedFiles == NULL || pFileHash == NULL)
			return false;

		rpResolvedFile = pSharedFiles->GetFileByID(pFileHash);
		return rpResolvedFile != NULL && !rpResolvedFile->IsPartFile();
	}

	UINT GetAICHHashsetPayloadByteCount(CFile &file, const uint32 nHashCount)
	{
		const ULONGLONG ullHashsetBytes = static_cast<ULONGLONG>(nHashCount) * CAICHHash::GetHashSize();
		if (ullHashsetBytes > static_cast<ULONGLONG>((std::numeric_limits<UINT>::max)()))
			AfxThrowFileException(CFileException::genericException, 0, file.GetFilePath());
		return static_cast<UINT>(ullHashsetBytes);
	}

	void ReadAICHHashsetPayloadExact(CFile &file, std::vector<BYTE> &rBuffer, const UINT uBytes)
	{
		rBuffer.resize(uBytes);
		if (uBytes == 0)
			return;
		const UINT uActualRead = file.Read(rBuffer.data(), uBytes);
		if (uActualRead != uBytes)
			AfxThrowFileException(CFileException::endOfFile, 0, file.GetFilePath());
	}

	HWND GetAICHSyncProgressWindow()
	{
		if (theApp.emuledlg == NULL || theApp.emuledlg->sharedfileswnd == NULL)
			return NULL;
		return theApp.emuledlg->sharedfileswnd->GetSafeHwnd();
	}

	void PostAICHSyncProgressCount(INT_PTR nRemainingHashes)
	{
		if (!HasValidAICHSyncProgressCount(nRemainingHashes))
			return;

		const HWND hSharedFilesWnd = GetAICHSyncProgressWindow();
		const SWorkerUiMessageDelivery delivery = PostWorkerUiMessage(
			hSharedFilesWnd,
			UM_AICH_HASHING_COUNT_CHANGED,
			static_cast<WPARAM>(nRemainingHashes),
			0);
		if (GetAICHSyncProgressDeliveryAction(nRemainingHashes, delivery.eDelivery) == EAICHSyncProgressDeliveryAction::Failed && thePrefs.GetVerbose())
			AddDebugLogLine(false, _T("AICH sync progress update could not be posted to the shared-files window (%u)"), delivery.dwLastError);
	}
}


/////////////////////////////////////////////////////////////////////////////////////////
///CAICHSyncThread
IMPLEMENT_DYNCREATE(CAICHSyncThread, CWinThread)

BOOL CAICHSyncThread::InitInstance()
{
	DbgSetThreadName("AICHSyncThread");
	InitThreadLocale();
	return TRUE;
}

int CAICHSyncThread::Run()
{
	if (theApp.IsClosing())
		return 0;

	m_aToHash.RemoveAll();

	// we collect all masterhashes which we find in the known2.met and store them in a list
	CArray<CAICHHash> aKnown2Hashes;
	CArray<ULONGLONG> aKnown2HashesFilePos;
	CMap<CAICHHash, const CAICHHash&, ULONGLONG, ULONGLONG> mapKnown2HashPositions;
	CMap<CAICHHash, const CAICHHash&, bool, bool> mapUsedKnown2Hashes;

	CSafeFile file;
	// we need to keep a lock on this file while the thread is running
	CSingleLock lockKnown2Met(&CAICHRecoveryHashSet::m_mutKnown2File, TRUE);
	bool bJustCreated = ConvertKnown2ToKnown264(file);
	if (!bJustCreated)
		if (!CFileOpen(file
			, thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + KNOWN2_MET_FILENAME
			, CFile::modeReadWrite | CFile::modeCreate | CFile::modeNoTruncate | CFile::osSequentialScan | CFile::typeBinary | CFile::shareDenyNone
			, _T("Failed to load ") KNOWN2_MET_FILENAME _T(" file")))
		{
			return 0;
		}

	ULONGLONG ullLastVerifiedPos = 0;
	try {
		if (file.GetLength() >= 1) {
			uint8 header = file.ReadUInt8();
			if (header != KNOWN2_MET_VERSION)
				AfxThrowFileException(CFileException::endOfFile, 0, file.GetFileName());

			//::setvbuf(file.m_pStream, NULL, _IOFBF, 16384);
			ULONGLONG nExistingSize = file.GetLength();
			const UINT nKnown2HashIndexBuckets = AICHMaintenanceSeams::EstimateAICHHashIndexBucketCount(nExistingSize, CAICHHash::GetHashSize());
			mapKnown2HashPositions.InitHashTable(nKnown2HashIndexBuckets);
			mapUsedKnown2Hashes.InitHashTable(nKnown2HashIndexBuckets);
			while (file.GetPosition() < nExistingSize) {
				const ULONGLONG nHashsetFilePosition = file.GetPosition();
				CAICHHash aichHash(file);
				aKnown2HashesFilePos.Add(nHashsetFilePosition);
				aKnown2Hashes.Add(aichHash);
				mapKnown2HashPositions[aichHash] = nHashsetFilePosition;
				uint32 nHashCount = file.ReadUInt32();
				if (file.GetPosition() + nHashCount * (ULONGLONG)CAICHHash::GetHashSize() > nExistingSize)
					AfxThrowFileException(CFileException::endOfFile, 0, file.GetFileName());

				// skip the rest of this hashset
				file.Seek(nHashCount * (LONGLONG)CAICHHash::GetHashSize(), CFile::current);
				ullLastVerifiedPos = file.GetPosition();
			}
		} else
			file.WriteUInt8(KNOWN2_MET_VERSION);
	} catch (CFileException *ex) {
		if (ex->m_cause == CFileException::endOfFile) {
			LogError(LOG_STATUSBAR, GetResString(IDS_ERR_MET_BAD), KNOWN2_MET_FILENAME);
			// truncate the file to the last verified valid position
			try {
				// WHY: known2_64.met can exceed 4 GiB on very large libraries.
				// Corruption repair must truncate to the last verified 64-bit
				// record boundary, not a wrapped 32-bit offset that discards
				// unrelated valid AICH metadata.
				file.SetLength(ullLastVerifiedPos);
				if (file.GetLength() == 0) {
					file.SeekToBegin();
					file.WriteUInt8(KNOWN2_MET_VERSION);
				}
			} catch (CFileException *ex2) {
				DebugLogError(_T("Failed to truncate corrupt %s to byte %I64u%s"), KNOWN2_MET_FILENAME, ullLastVerifiedPos, (LPCTSTR)CExceptionStrDash(*ex2));
				ex2->Delete();
			}
		} else
			LogError(LOG_STATUSBAR, GetResString(IDS_ERR_SERVERMET_UNKNOWN), (LPCTSTR)CExceptionStr(*ex));

		ex->Delete();
		return 0;
	}

	// now we check that all files which are in the shared file list have a corresponding hash in the out list
	// those who don't are added to the hashing list
	CArray<CSKey, const CSKey&> aSharedFileHashes;
#ifdef _DEBUG
	CArray<CSKey, const CSKey&> aUsedAICHFileHashes;
#endif
	bool bDbgMsgCreatingPartHashes = true;
	INT_PTR nUsedKnown2HashCount = 0;

	SnapshotSharedAICHFileHashes(aSharedFileHashes);
	for (INT_PTR iFile = 0; iFile < aSharedFileHashes.GetCount(); ++iFile) {
		if (theApp.IsClosing()) // in case of shutdown while still hashing
			return 0;

		CSharedFileList *pSharedFiles = theApp.sharedfiles;
		if (pSharedFiles == NULL)
			return 0;

		CSingleLock sharelock(&pSharedFiles->m_mutWriteList, TRUE);
		CKnownFile *pFile = NULL;
		if (!ResolveSharedAICHSyncFileNoLock(pSharedFiles, aSharedFileHashes[iFile].m_key, pFile))
			continue;

		CFileIdentifier &fileid = pFile->GetFileIdentifier();
		if (fileid.HasAICHHash()) {
			ULONGLONG nKnown2HashPosition = 0;
			if (mapKnown2HashPositions.Lookup(fileid.GetAICHHash(), nKnown2HashPosition)) {
				bool bWasAlreadyUsed = false;
				if (!mapUsedKnown2Hashes.Lookup(fileid.GetAICHHash(), bWasAlreadyUsed)) {
					mapUsedKnown2Hashes[fileid.GetAICHHash()] = true;
					++nUsedKnown2HashCount;
				}
#ifdef _DEBUG
				aUsedAICHFileHashes.Add(aSharedFileHashes[iFile]);
#endif
				pFile->SetAICHRecoverHashSetAvailable(true);
				// Has the file the proper AICH Part hashset? If not, probably upgrading, create it
				if (!fileid.HasExpectedAICHHashCount()) {
					if (bDbgMsgCreatingPartHashes) {
						bDbgMsgCreatingPartHashes = false;
						DebugLogWarning(_T("Missing AICH Part Hashsets for known files - maybe upgrading from earlier version. Creating them out of full AICH recovery Hashsets, shouldn't take too long"));
					}
					CAICHRecoveryHashSet tempHashSet(pFile, pFile->GetFileSize());
					tempHashSet.SetMasterHash(fileid.GetAICHHash(), AICH_HASHSETCOMPLETE);
					if (!tempHashSet.LoadHashSet()) {
						ASSERT(0);
						DebugLogError(_T("Failed to load full AICH recovery Hashset - known2.met might be corrupt. Unable to create AICH Part Hashset - %s"), (LPCTSTR)pFile->GetFileName());
					} else {
						if (!fileid.SetAICHHashSet(tempHashSet)) {
							DebugLogError(_T("Failed to create AICH Part Hashset out of full AICH recovery Hashset - %s"), (LPCTSTR)pFile->GetFileName());
							ASSERT(0);
						}
						ASSERT(fileid.HasExpectedAICHHashCount());
					}
				}
				//theApp.QueueDebugLogLine(false, _T("%s - %s"), current_hash.GetString(), pFile->GetFileName());
				continue;
			}
		}
		pFile->SetAICHRecoverHashSetAvailable(false);
		m_aToHash.Add(aSharedFileHashes[iFile]);
	}

	// remove all unused AICH hashsets from known2.met
	if (nUsedKnown2HashCount != aKnown2Hashes.GetCount()
		&& (!thePrefs.IsRememberingDownloadedFiles() || thePrefs.DoPartiallyPurgeOldKnownFiles()))
	{
		file.SeekToBegin();
		try {
			uint8 header = file.ReadUInt8();
			if (header != KNOWN2_MET_VERSION)
				AfxThrowFileException(CFileException::endOfFile, 0, file.GetFileName());

			ULONGLONG nExistingSize = file.GetLength();
			ULONGLONG posWritePos = file.GetPosition();
			ULONGLONG posReadPos = posWritePos;
			uint32 nPurgeCount = 0;
			uint32 nPurgeBecauseOld = 0;
			uint32 nPurgeDups = 0;
			static const CAICHHash empty; //zero AICH hash
			ULONGLONG nCurrentHashsetPos;
			while ((nCurrentHashsetPos = file.GetPosition()) < nExistingSize) {
				ULONGLONG posTmp = 0; //position of an old duplicate hash
				CAICHHash aichHash(file);
				uint32 nHashCount = file.ReadUInt32();
				if (file.GetPosition() + nHashCount * (ULONGLONG)CAICHHash::GetHashSize() > nExistingSize)
					AfxThrowFileException(CFileException::endOfFile, 0, file.GetFileName());

				bool bKnown2HashUsed = false;
				if (aichHash == empty || (!thePrefs.IsRememberingDownloadedFiles() && !mapUsedKnown2Hashes.Lookup(aichHash, bKnown2HashUsed))) {
					// unused hashset skip the rest of this hashset
					file.Seek(nHashCount * (LONGLONG)CAICHHash::GetHashSize(), CFile::current);
					++nPurgeCount;
				} else if (thePrefs.IsRememberingDownloadedFiles() && theApp.knownfiles != NULL && theApp.knownfiles->ShouldPurgeAICHHashset(aichHash)) {
					ASSERT(thePrefs.DoPartiallyPurgeOldKnownFiles());
					// also unused (purged) hashset skip the rest of this hashset
					file.Seek(nHashCount * (LONGLONG)CAICHHash::GetHashSize(), CFile::current);
					++nPurgeCount;
					++nPurgeBecauseOld;
				} else if (nPurgeCount == 0) {
					// used Hashset, but it does not need to be moved as nothing changed yet
					file.Seek(nHashCount * (LONGLONG)CAICHHash::GetHashSize(), CFile::current);
					posReadPos = posWritePos = file.GetPosition();
					posTmp = CAICHRecoveryHashSet::AddStoredAICHHash(aichHash, nCurrentHashsetPos);
				} else {
					// used Hashset, move position in file
					const UINT uHashsetBytes = GetAICHHashsetPayloadByteCount(file, nHashCount);
					std::vector<BYTE> buffer;
					ReadAICHHashsetPayloadExact(file, buffer, uHashsetBytes);
					posReadPos = file.GetPosition();
					file.Seek(posWritePos, CFile::begin);
					file.Write(aichHash.GetRawHashC(), CAICHHash::GetHashSize());
					file.WriteUInt32(nHashCount);
					if (!buffer.empty())
						file.Write(buffer.data(), uHashsetBytes);
					posTmp = CAICHRecoveryHashSet::AddStoredAICHHash(aichHash, posWritePos);

					posWritePos = file.GetPosition();
					file.Seek(posReadPos, CFile::begin);
				}
				if (posTmp) {
					file.Seek(posTmp, CFile::begin);
					file.Write(empty.GetRawHashC(), CAICHHash::GetHashSize()); //mark this for purging
					file.Seek(posReadPos, CFile::begin);
					++nPurgeDups;
				}
			}
			posReadPos = file.GetPosition();
			file.SetLength(posWritePos);
			file.Flush();
			file.Close();
			theApp.QueueDebugLogLine(false, _T("Cleaned up known2.met, removed %u hashsets and purged %u hashsets of old known files (%s)")
				, nPurgeCount - nPurgeBecauseOld, nPurgeBecauseOld, (LPCTSTR)CastItoXBytes(posReadPos - posWritePos));
			if (nPurgeDups)
				theApp.QueueDebugLogLine(false, _T("Marked %u duplicate hashsets for purging"), nPurgeDups);
		} catch (CFileException *ex) {
			if (ex->m_cause == CFileException::endOfFile) {
				// we just parsed this file some ms ago, should never happen here
				ASSERT(0);
			} else
				LogError(LOG_STATUSBAR, GetResString(IDS_ERR_SERVERMET_UNKNOWN), (LPCTSTR)CExceptionStr(*ex));

			ex->Delete();
			return 0;
		}
	} else {
		// remember (/index) all hashes which are stored in the file for faster checking later on
		for (INT_PTR i = 0; i < aKnown2Hashes.GetCount() && !theApp.IsClosing(); ++i)
			CAICHRecoveryHashSet::AddStoredAICHHash(aKnown2Hashes[i], aKnown2HashesFilePos[i]);
	}

#ifdef _DEBUG
	for (INT_PTR i = 0; i < aUsedAICHFileHashes.GetCount() && !theApp.IsClosing(); ++i) {
		CSharedFileList *pSharedFiles = theApp.sharedfiles;
		if (pSharedFiles == NULL)
			return 0;

		CSingleLock sharelock(&pSharedFiles->m_mutWriteList, TRUE);
		CKnownFile *pFile = NULL;
		if (!ResolveSharedAICHSyncFileNoLock(pSharedFiles, aUsedAICHFileHashes[i].m_key, pFile)) {
			ASSERT(0);
			continue;
		}
		CAICHRecoveryHashSet *pTempHashSet = new CAICHRecoveryHashSet(pFile);
		pTempHashSet->SetFileSize(pFile->GetFileSize());
		pTempHashSet->SetMasterHash(pFile->GetFileIdentifier().GetAICHHash(), AICH_HASHSETCOMPLETE);
		ASSERT(pTempHashSet->LoadHashSet());
		delete pTempHashSet;
	}
#endif

	lockKnown2Met.Unlock();
	// warn the user if he just upgraded
	if (thePrefs.IsFirstStart() && m_aToHash.GetCount() != 0 && !bJustCreated)
		LogWarning(GetResString(IDS_AICH_WARNUSER));

	if (m_aToHash.GetCount() != 0) {
		theApp.QueueLogLine(true, GetResString(IDS_AICH_SYNCTOTAL), m_aToHash.GetCount());
		PostAICHSyncProgressCount(m_aToHash.GetCount());
		for (INT_PTR iFile = 0; iFile < m_aToHash.GetCount(); ++iFile) {
			if (theApp.IsClosing()) // in case of shutdown while still hashing
				return 0;

			for (;;) {
				CSharedFileList *pSharedFiles = theApp.sharedfiles;
				if (theApp.IsClosing() || pSharedFiles == NULL)
					return 0;
				const AICHSyncForegroundHashState state = {
					false,
					pSharedFiles->GetHashingCount(),
					HasPendingPartFileHashing()
				};
				const AICHSyncForegroundWaitAction action = AICHMaintenanceSeams::GetForegroundHashWaitAction(state);
				if (action.bShouldExit)
					return 0;
				if (action.dwSleepMilliseconds == 0u)
					break;
				::Sleep(action.dwSleepMilliseconds);
			}

			CSingleLock hashingLock(&theApp.hashing_mut, TRUE); // only one file hash at a time
			if (theApp.IsClosing())
				return 0;

			PostAICHSyncProgressCount(m_aToHash.GetCount() - iFile);
			{
				CSharedFileList *pSharedFiles = theApp.sharedfiles;
				if (pSharedFiles == NULL)
					return 0;

				CSingleLock sharelock(&pSharedFiles->m_mutWriteList, TRUE);
				CKnownFile *pCurFile = NULL;
				const bool bIsStillShared = ResolveSharedAICHSyncFileNoLock(pSharedFiles, m_aToHash[iFile].m_key, pCurFile);
				if (!ShouldCreateAICHSyncHash(theApp.IsClosing(), bIsStillShared))
					continue;
				theApp.QueueLogLine(false, GetResString(IDS_AICH_CALCFILE), (LPCTSTR)pCurFile->GetFileName());
				if (!pCurFile->CreateAICHHashSetOnly())
					theApp.QueueDebugLogLine(false, _T("Failed to create AICH Hashset while sync. for file %s"), (LPCTSTR)pCurFile->GetFileName());
			}
		}

		PostAICHSyncProgressCount(0);
	}

	theApp.QueueDebugLogLine(false, _T("AICHSyncThread finished"));
	return 0;
}

bool CAICHSyncThread::ConvertKnown2ToKnown264(CSafeFile &TargetFile)
{
	// converting known2.met to known2_64.met to support large files
	// changing hashcount from uint16 to uint32

	// there still exists a lock on known2_64.met and it should be not opened at this point
	const CString &sConfDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	const CString &oldfullpath(sConfDir + OLD_KNOWN2_MET_FILENAME);
	const CString &newfullpath(sConfDir + KNOWN2_MET_FILENAME);

	// continue only if the old file does exist, and the new file does not
	if (LongPathSeams::PathExists(newfullpath) || !LongPathSeams::PathExists(oldfullpath))
		return false;

	CSafeFile oldfile;
	if (!CFileOpen(oldfile, oldfullpath
		, CFile::modeRead | CFile::osSequentialScan | CFile::typeBinary | CFile::shareDenyNone
		, _T("Failed to load ") OLD_KNOWN2_MET_FILENAME _T(" file")))
	{
		// known2.met also doesn't exist, so nothing to convert
		return false;
	}
	if (!CFileOpen(TargetFile, newfullpath
		, CFile::modeReadWrite | CFile::modeCreate | CFile::osSequentialScan | CFile::typeBinary | CFile::shareDenyNone
		, _T("Failed to load ") KNOWN2_MET_FILENAME _T(" file")))
	{
		return false;
	}

	theApp.QueueLogLine(false, GetResString(IDS_CONVERTINGKNOWN2MET), OLD_KNOWN2_MET_FILENAME, KNOWN2_MET_FILENAME);

	try {
		TargetFile.WriteUInt8(KNOWN2_MET_VERSION);
		while (oldfile.GetPosition() < oldfile.GetLength()) {
			CAICHHash aichHash(oldfile);
			uint32 nHashCount = oldfile.ReadUInt16();
			if (oldfile.GetPosition() + nHashCount * (ULONGLONG)CAICHHash::GetHashSize() > oldfile.GetLength())
				AfxThrowFileException(CFileException::endOfFile, 0, oldfile.GetFileName());

			const UINT uHashsetBytes = GetAICHHashsetPayloadByteCount(oldfile, nHashCount);
			std::vector<BYTE> buffer;
			ReadAICHHashsetPayloadExact(oldfile, buffer, uHashsetBytes);
			TargetFile.Write(aichHash.GetRawHash(), CAICHHash::GetHashSize());
			TargetFile.WriteUInt32(nHashCount);
			if (!buffer.empty())
				TargetFile.Write(buffer.data(), uHashsetBytes);
		}
		TargetFile.Flush();
		oldfile.Close();
	} catch (CFileException *ex) {
		if (ex->m_cause == CFileException::endOfFile) {
			LogError(LOG_STATUSBAR, GetResString(IDS_ERR_MET_BAD), OLD_KNOWN2_MET_FILENAME);
			ASSERT(0);
		} else
			LogError(LOG_STATUSBAR, GetResString(IDS_ERR_SERVERMET_UNKNOWN), (LPCTSTR)CExceptionStr(*ex));
		ex->Delete();
		theApp.QueueLogLine(false, GetResString(IDS_CONVERTINGKNOWN2FAILED));
		TargetFile.Close();
		return false;
	}
	theApp.QueueLogLine(false, GetResString(IDS_CONVERTINGKNOWN2DONE));

	// Preserve the legacy source file after conversion; cleanup can be handled separately
	// once we decide whether rollback/manual inspection of the old file is still useful.
	//::DeleteFile(oldfullpath);
	TargetFile.SeekToBegin();
	return true;
}
