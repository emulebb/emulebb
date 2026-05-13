//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( devs@emule-project.net / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
#include "stdafx.h"
#include "FakeFileDetector.h"
#include "emule.h"
#include "FileTypeClassifierSeams.h"
#include "Kademlia/Kademlia/Entry.h"
#include "Log.h"
#include "OtherFunctions.h"
#include "PartFile.h"
#include "Preferences.h"
#include "SafeFile.h"
#include "SearchFile.h"
#include "StringConversion.h"

#include <map>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
constexpr LPCTSTR kRuleFileName = _T("FakeFileFilter.dat");
constexpr LPCTSTR kCacheFileName = _T("FakeFile.met");
const BYTE kCacheHeader[] = { 'B', 'B', 'F', 'F', 'M', 'E', 'T', '2' };
constexpr uint32 kRulesFingerprintSeed = 2166136261U;
constexpr uint32 kRulesFingerprintPrime = 16777619U;
constexpr uint32 kDetectorRulesVersion = 1;
constexpr uint32 kMaxCachedReasons = 32;
constexpr ULONGLONG kMaxRuleFileBytes = 1024 * 1024;

struct SCacheRecord
{
	uint32 uRulesFingerprint = 0;
	SFakeFileReport report;
};

struct SPartHeaderProbeState
{
	uint8 uProbeMask = 0;
	EFileType eExtensionType = FILETYPE_UNKNOWN;
};

FakeFileDetectorSeams::RuleSet g_rules;
uint32 g_uRulesFingerprint = 0;
bool g_bRulesLoaded = false;
bool g_bCacheLoaded = false;
bool g_bCacheDirty = false;
std::map<CString, SCacheRecord> g_cache;
std::map<CString, SPartHeaderProbeState> g_partHeaderProbeState;

constexpr uint8 kHeaderProbeStartMask = 0x01;
constexpr uint8 kHeaderProbeIsoMask = 0x02;

std::wstring ToWide(const CString &rstr)
{
	return std::wstring(rstr.GetString(), rstr.GetLength());
}

CString FromUtf8Reason(const std::string &rReason)
{
	return CString(CA2T(rReason.c_str(), CP_UTF8));
}

void AddCStringUnique(std::vector<CString> &rValues, const CString &rValue)
{
	CString strValue(rValue);
	strValue.Trim();
	if (strValue.IsEmpty())
		return;
	for (const CString &rExisting : rValues) {
		if (rExisting.CompareNoCase(strValue) == 0)
			return;
	}
	rValues.push_back(strValue);
}

SFakeFileReport ToAppReport(const FakeFileDetectorSeams::Report &rReport)
{
	SFakeFileReport report;
	report.nScore = rReport.score;
	report.eSeverity = rReport.severity;
	report.eExtensionType = rReport.extensionType;
	report.eHeaderType = rReport.headerType;
	report.strClaimedType = CString(rReport.claimedType.c_str());
	report.bPendingHeaderCheck = rReport.pendingHeaderCheck;
	report.bCached = rReport.cached;
	for (const std::string &rReason : rReport.reasons)
		AddCStringUnique(report.astrReasons, FromUtf8Reason(rReason));
	for (const std::wstring &rName : rReport.observedNames)
		AddCStringUnique(report.astrObservedNames, CString(rName.c_str()));
	for (const std::wstring &rExtension : rReport.observedExtensions)
		AddCStringUnique(report.astrObservedExtensions, CString(rExtension.c_str()));
	return report;
}

CString GetRuleFilePath()
{
	return thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + kRuleFileName;
}

CString GetCacheFilePath()
{
	return thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + kCacheFileName;
}

void HashByte(uint32 &ruHash, const BYTE uValue)
{
	ruHash ^= uValue;
	ruHash *= kRulesFingerprintPrime;
}

void HashWideString(uint32 &ruHash, const std::wstring &rValue)
{
	for (const wchar_t ch : rValue) {
		HashByte(ruHash, static_cast<BYTE>(ch & 0xFF));
		HashByte(ruHash, static_cast<BYTE>((ch >> 8) & 0xFF));
	}
	HashByte(ruHash, 0);
}

uint32 BuildRulesFingerprint(const FakeFileDetectorSeams::RuleSet &rRules)
{
	uint32 uHash = kRulesFingerprintSeed;
	HashByte(uHash, static_cast<BYTE>(kDetectorRulesVersion & 0xFF));
	HashByte(uHash, static_cast<BYTE>((kDetectorRulesVersion >> 8) & 0xFF));
	for (const std::wstring &rToken : rRules.tokens) {
		HashByte(uHash, 'T');
		HashWideString(uHash, rToken);
	}
	for (const std::wstring &rRegex : rRules.regexes) {
		HashByte(uHash, 'R');
		HashWideString(uHash, rRegex);
	}
	return uHash;
}

void WriteDefaultRuleFile(const CString &rstrPath)
{
	CFile file;
	CFileException ex;
	if (!file.Open(rstrPath, CFile::modeCreate | CFile::modeWrite | CFile::typeBinary | CFile::shareDenyWrite, &ex)) {
		DebugLogError(_T("Failed to create FakeFileFilter.dat%s"), (LPCTSTR)CExceptionStrDash(ex));
		return;
	}
	const CString strDefaultRules(
		_T("# eMule BB fake-file bad-signal rules\n")
		_T("# UTF-8 text. One rule per line. Lines starting with # or ; are ignored.\n\n")
		_T("[tokens]\n")
		_T("fake\n")
		_T("corrupt\n")
		_T("wrong file\n")
		_T("password\n")
		_T("virus\n")
		_T("trojan\n")
		_T("malware\n\n")
		_T("[regex]\n")
		_T("\\.mp4\\.exe$\n")
		_T("\\.avi\\.scr$\n"));
	const CUnicodeToBOMUTF8 utf8(strDefaultRules);
	file.Write(static_cast<LPCSTR>(utf8), utf8.GetLength());
	file.Close();
}

bool ReadRuleFileText(const CString &rstrPath, CString &rstrText)
{
	rstrText.Empty();
	CFile file;
	CFileException ex;
	if (!file.Open(rstrPath, CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, &ex)) {
		DebugLogError(_T("Failed to load FakeFileFilter.dat%s"), (LPCTSTR)CExceptionStrDash(ex));
		return false;
	}
	const ULONGLONG ullLength = file.GetLength();
	if (ullLength > kMaxRuleFileBytes) {
		DebugLogError(_T("Failed to load FakeFileFilter.dat - file is too large"));
		return false;
	}
	CStringA strBytes;
	const UINT uLength = static_cast<UINT>(ullLength);
	LPSTR pszBuffer = strBytes.GetBuffer(static_cast<int>(uLength));
	const UINT uRead = file.Read(pszBuffer, uLength);
	strBytes.ReleaseBuffer(static_cast<int>(uRead));
	file.Close();
	rstrText = OptUtf8ToStr(strBytes);
	if (!rstrText.IsEmpty() && rstrText[0] == 0xFEFF)
		rstrText.Delete(0);
	return true;
}

void EnsureRulesLoaded()
{
	if (g_bRulesLoaded)
		return;
	FakeFileDetector::ReloadRules();
}

bool IsValidRegex(const CString &rstrPattern)
{
	return RegexMatchSeams::IsValidPattern(ToWide(rstrPattern), std::regex_constants::icase | std::regex_constants::ECMAScript);
}

uint64 GetIsoHeaderRangeEnd()
{
	return FileTypeClassifierSeams::kIsoHeaderOffset + FileTypeClassifierSeams::kHeaderCheckSize - 1;
}

bool IsIsoHeaderRangeAvailable(CPartFile &rPartFile)
{
	const uint64 uIsoRangeEnd = GetIsoHeaderRangeEnd();
	return !rPartFile.IsPartFile()
		|| (static_cast<uint64>(rPartFile.GetCompletedSize()) >= uIsoRangeEnd + 1
			&& rPartFile.IsCompleteBD(FileTypeClassifierSeams::kIsoHeaderOffset, uIsoRangeEnd));
}

bool IsStartHeaderRangeAvailable(CPartFile &rPartFile)
{
	return !rPartFile.IsPartFile() || rPartFile.IsCompleteBDSafe(0, FileTypeClassifierSeams::kHeaderCheckSize);
}

uint8 GetAvailableHeaderProbeMask(CPartFile &rPartFile)
{
	uint8 uMask = 0;
	if (IsStartHeaderRangeAvailable(rPartFile))
		uMask |= kHeaderProbeStartMask;
	if (IsIsoHeaderRangeAvailable(rPartFile))
		uMask |= kHeaderProbeIsoMask;
	return uMask;
}

void AddRuleLine(const CString &rstrSection, const CString &rstrLine)
{
	if (rstrSection.CompareNoCase(_T("tokens")) == 0) {
		g_rules.tokens.push_back(ToWide(rstrLine));
		return;
	}
	if (rstrSection.CompareNoCase(_T("regex")) == 0) {
		if (rstrLine.GetLength() > 256) {
			DebugLogWarning(_T("Ignoring overlong fake-file regex rule in %s"), kRuleFileName);
			return;
		}
		if (!IsValidRegex(rstrLine)) {
			DebugLogWarning(_T("Ignoring invalid fake-file regex rule \"%s\""), (LPCTSTR)rstrLine);
			return;
		}
		g_rules.regexes.push_back(ToWide(rstrLine));
	}
}

void LoadCache()
{
	if (g_bCacheLoaded)
		return;
	g_bCacheLoaded = true;
	g_cache.clear();

	CSafeBufferedFile file;
	if (!CFileOpen(file, GetCacheFilePath(), CFile::modeRead | CFile::osSequentialScan | CFile::typeBinary | CFile::shareDenyWrite, _T("Failed to load FakeFile.met")))
		return;
	try {
		BYTE aucHeader[sizeof kCacheHeader] = {};
		if (file.Read(aucHeader, sizeof aucHeader) != sizeof aucHeader || memcmp(aucHeader, kCacheHeader, sizeof kCacheHeader) != 0)
			return;
		const uint32 uCount = file.ReadUInt32();
		for (uint32 i = 0; i < uCount; ++i) {
			uchar hash[16];
			file.ReadHash16(hash);
			SCacheRecord record;
			record.report.nScore = file.ReadUInt32();
			record.report.eSeverity = static_cast<FakeFileDetectorSeams::Severity>(file.ReadUInt8());
			record.report.eExtensionType = static_cast<EFileType>(file.ReadUInt8());
			record.report.eHeaderType = static_cast<EFileType>(file.ReadUInt8());
			record.report.bPendingHeaderCheck = file.ReadUInt8() != 0;
			record.uRulesFingerprint = file.ReadUInt32();
			const uint32 uReasonCount = file.ReadUInt32();
			for (uint32 j = 0; j < uReasonCount; ++j) {
				const CString strReason(static_cast<CFileDataIO&>(file).ReadString(true));
				if (j < kMaxCachedReasons)
					record.report.astrReasons.push_back(strReason);
			}
			if (record.uRulesFingerprint == g_uRulesFingerprint)
				g_cache[md4str(hash)] = record;
		}
		file.Close();
	} catch (CFileException *ex) {
		DebugLogError(_T("Failed to load FakeFile.met%s"), (LPCTSTR)CExceptionStrDash(*ex));
		ex->Delete();
		g_cache.clear();
	}
}

bool HasCurrentCacheRecord(const uchar *pHash)
{
	LoadCache();
	const auto it = g_cache.find(md4str(pHash));
	if (it == g_cache.end())
		return false;
	return it->second.uRulesFingerprint == g_uRulesFingerprint;
}

void MergeCurrentCacheForPendingHeader(const uchar *pHash, SFakeFileReport &rReport)
{
	if (!rReport.bPendingHeaderCheck)
		return;
	if (!g_bCacheLoaded)
		return;
	LoadCache();
	const auto it = g_cache.find(md4str(pHash));
	if (it == g_cache.end() || it->second.uRulesFingerprint != g_uRulesFingerprint)
		return;
	const SFakeFileReport &rCached = it->second.report;
	if (rCached.eExtensionType != rReport.eExtensionType || rCached.eHeaderType == FILETYPE_UNKNOWN || rCached.nScore <= rReport.nScore)
		return;
	const bool bPendingHeaderCheck = rReport.bPendingHeaderCheck;
	rReport = rCached;
	rReport.bCached = true;
	rReport.bPendingHeaderCheck = bPendingHeaderCheck;
}

uint32 GetCurrentCacheRecordCount()
{
	uint32 uCount = 0;
	for (const auto &rPair : g_cache) {
		if (rPair.second.uRulesFingerprint == g_uRulesFingerprint)
			++uCount;
	}
	return uCount;
}

void UpdateCache(const uchar *pHash, const SFakeFileReport &rReport)
{
	LoadCache();
	const CString strHash(md4str(pHash));
	const auto it = g_cache.find(strHash);
	if (rReport.nScore == 0) {
		if (it != g_cache.end()) {
			g_cache.erase(it);
			g_bCacheDirty = true;
		}
		return;
	}

	SCacheRecord record;
	record.uRulesFingerprint = g_uRulesFingerprint;
	record.report = rReport;
	record.report.bCached = false;
	if (it == g_cache.end() || it->second.uRulesFingerprint != record.uRulesFingerprint
		|| it->second.report.nScore != record.report.nScore
		|| it->second.report.eSeverity != record.report.eSeverity
		|| it->second.report.eExtensionType != record.report.eExtensionType
		|| it->second.report.eHeaderType != record.report.eHeaderType
		|| it->second.report.bPendingHeaderCheck != record.report.bPendingHeaderCheck
		|| it->second.report.astrReasons != record.report.astrReasons)
	{
		g_cache[strHash] = record;
		g_bCacheDirty = true;
	}
}

void AddKadComments(const CAbstractFile &rFile, FakeFileDetectorSeams::Evidence &rEvidence)
{
	const CTypedPtrList<CPtrList, Kademlia::CEntry*> &rNotes = rFile.getNotes();
	for (POSITION pos = rNotes.GetHeadPosition(); pos != NULL;) {
		const Kademlia::CEntry *const pEntry = rNotes.GetNext(pos);
		if (pEntry == NULL)
			continue;
		const CString strComment(pEntry->GetStrTagValue(Kademlia::CKadTagNameString(TAG_DESCRIPTION)));
		if (!strComment.IsEmpty())
			rEvidence.comments.push_back(ToWide(strComment));
		const UINT uRating = static_cast<UINT>(pEntry->GetIntTagValue(Kademlia::CKadTagNameString(TAG_FILERATING)));
		rEvidence.badRating = rEvidence.badRating || (uRating > 0 && uRating < 2);
		rEvidence.fakeRating = rEvidence.fakeRating || (uRating == 1);
	}
}

void FillCommonEvidence(const CAbstractFile &rFile, FakeFileDetectorSeams::Evidence &rEvidence)
{
	rEvidence.claimedType = ToWide(rFile.GetFileType());
	rEvidence.names.push_back(ToWide(rFile.GetFileName()));
	rEvidence.extensionType = FileTypeClassifierSeams::GetFileTypeFromExtension(rFile.GetFileName());
	if (const_cast<CAbstractFile&>(rFile).HasComment())
		rEvidence.comments.push_back(ToWide(const_cast<CAbstractFile&>(rFile).GetFileComment()));
	rEvidence.badRating = rEvidence.badRating || rFile.HasBadRating();
	rEvidence.fakeRating = rEvidence.fakeRating || (rFile.HasRating() && rFile.UserRating() == 1);
	AddKadComments(rFile, rEvidence);
}

SFakeFileReport BuildSearchFileReport(const CSearchFile &rSearchFile, const bool bUpdateCache)
{
	EnsureRulesLoaded();
	FakeFileDetectorSeams::Evidence evidence;
	FillCommonEvidence(rSearchFile, evidence);
	for (INT_PTR i = 0; i < rSearchFile.GetObservedNameCount(); ++i)
		evidence.names.push_back(ToWide(rSearchFile.GetObservedNameAt(i)));
	evidence.spamRating = rSearchFile.GetSpamRating();
	evidence.consideredSpam = rSearchFile.IsConsideredSpam();
	evidence.multipleAich = rSearchFile.HasFoundMultipleAICH();
	SFakeFileReport report(ToAppReport(FakeFileDetectorSeams::Analyze(evidence, g_rules)));
	report.bCached = bUpdateCache && HasCurrentCacheRecord(rSearchFile.GetFileHash());
	if (bUpdateCache)
		UpdateCache(rSearchFile.GetFileHash(), report);
	return report;
}

SFakeFileReport BuildPartFileReport(CPartFile &rPartFile, const bool bProbeHeader, const bool bUpdateCache)
{
	EnsureRulesLoaded();
	FakeFileDetectorSeams::Evidence evidence;
	FillCommonEvidence(rPartFile, evidence);
	evidence.headerPending = true;
	evidence.headerAvailable = false;
	const bool bHeaderRangeAvailable = IsStartHeaderRangeAvailable(rPartFile);
	const bool bIsoRangeAvailable = IsIsoHeaderRangeAvailable(rPartFile);
	if (bProbeHeader) {
		if (bHeaderRangeAvailable || bIsoRangeAvailable)
			evidence.headerType = GetFileTypeEx(&rPartFile, false, true);
	} else if (rPartFile.GetVerifiedFileType() != FILETYPE_UNKNOWN) {
		evidence.headerType = rPartFile.GetVerifiedFileType();
	}
	const bool bHeaderProbeCovered = bProbeHeader ? bHeaderRangeAvailable : evidence.headerType != FILETYPE_UNKNOWN;
	const bool bIsoProbeCovered = bProbeHeader ? bIsoRangeAvailable : evidence.headerType != FILETYPE_UNKNOWN;
	const FileTypeClassifierSeams::HeaderProbeSummary headerSummary = FileTypeClassifierSeams::SummarizeHeaderProbe(
		evidence.headerType,
		evidence.extensionType,
		bHeaderProbeCovered,
		bIsoProbeCovered);
	evidence.headerAvailable = headerSummary.status == FileTypeClassifierSeams::HeaderProbeStatus::Detected
		|| headerSummary.status == FileTypeClassifierSeams::HeaderProbeStatus::CheckedUnknown;
	evidence.headerPending = headerSummary.status == FileTypeClassifierSeams::HeaderProbeStatus::Pending;
	SFakeFileReport report(ToAppReport(FakeFileDetectorSeams::Analyze(evidence, g_rules)));
	if (bProbeHeader)
		report.bCached = HasCurrentCacheRecord(rPartFile.GetFileHash());
	else
		MergeCurrentCacheForPendingHeader(rPartFile.GetFileHash(), report);
	if (bUpdateCache)
		UpdateCache(rPartFile.GetFileHash(), report);
	return report;
}
}

bool FakeFileDetector::ReloadRules()
{
	g_rules = FakeFileDetectorSeams::RuleSet();
	g_uRulesFingerprint = 0;
	g_bRulesLoaded = true;

	const CString strPath(GetRuleFilePath());
	if (!PathFileExists(strPath))
		WriteDefaultRuleFile(strPath);

	CString strRulesText;
	if (!ReadRuleFileText(strPath, strRulesText)) {
		g_uRulesFingerprint = BuildRulesFingerprint(g_rules);
		return false;
	}

	CString strSection;
	int iPos = 0;
	while (iPos >= 0) {
		CString strLine(strRulesText.Tokenize(_T("\n"), iPos));
		strLine.TrimRight(_T("\r"));
		strLine.Trim();
		if (strLine.IsEmpty() || strLine[0] == _T('#') || strLine[0] == _T(';'))
			continue;
		if (strLine[0] == _T('[') && strLine[strLine.GetLength() - 1] == _T(']')) {
			strSection = strLine.Mid(1, strLine.GetLength() - 2);
			strSection.Trim();
			continue;
		}
		AddRuleLine(strSection, strLine);
	}
	g_uRulesFingerprint = BuildRulesFingerprint(g_rules);
	DebugLog(_T("Loaded fake-file filter rules. Tokens: %u, regexes: %u"), static_cast<unsigned>(g_rules.tokens.size()), static_cast<unsigned>(g_rules.regexes.size()));
	return true;
}

void FakeFileDetector::SaveCache()
{
	if (!g_bCacheLoaded || !g_bCacheDirty)
		return;

	CSafeBufferedFile file;
	if (!CFileOpen(file, GetCacheFilePath(), CFile::modeWrite | CFile::modeCreate | CFile::typeBinary | CFile::shareDenyWrite, _T("Failed to save FakeFile.met")))
		return;
	try {
		file.Write(kCacheHeader, sizeof kCacheHeader);
		const uint32 uCurrentRecordCount = GetCurrentCacheRecordCount();
		file.WriteUInt32(uCurrentRecordCount);
		for (const auto &rPair : g_cache) {
			if (rPair.second.uRulesFingerprint != g_uRulesFingerprint)
				continue;
			uchar hash[16] = {};
			if (!DecodeBase16(rPair.first, 32, hash, sizeof hash))
				continue;
			file.WriteHash16(hash);
			file.WriteUInt32(rPair.second.report.nScore);
			file.WriteUInt8(static_cast<uint8>(rPair.second.report.eSeverity));
			file.WriteUInt8(static_cast<uint8>(rPair.second.report.eExtensionType));
			file.WriteUInt8(static_cast<uint8>(rPair.second.report.eHeaderType));
			file.WriteUInt8(static_cast<uint8>(rPair.second.report.bPendingHeaderCheck));
			file.WriteUInt32(rPair.second.uRulesFingerprint);
			file.WriteUInt32(static_cast<uint32>(rPair.second.report.astrReasons.size()));
			for (const CString &rReason : rPair.second.report.astrReasons)
				static_cast<CFileDataIO&>(file).WriteString(rReason, UTF8strRaw);
		}
		file.Close();
		g_bCacheDirty = false;
		DebugLog(_T("Stored FakeFile.met, wrote %u records"), static_cast<unsigned>(uCurrentRecordCount));
	} catch (CFileException *ex) {
		DebugLogError(_T("Failed to save FakeFile.met%s"), (LPCTSTR)CExceptionStrDash(*ex));
		ex->Delete();
	}
}

SFakeFileReport FakeFileDetector::AnalyzeSearchFile(const CSearchFile &rSearchFile)
{
	return BuildSearchFileReport(rSearchFile, true);
}

SFakeFileReport FakeFileDetector::AnalyzePartFile(CPartFile &rPartFile)
{
	return BuildPartFileReport(rPartFile, true, true);
}

SFakeFileReport FakeFileDetector::GetSearchFileReportSnapshot(const CSearchFile &rSearchFile)
{
	return BuildSearchFileReport(rSearchFile, false);
}

SFakeFileReport FakeFileDetector::GetPartFileReportSnapshot(CPartFile &rPartFile)
{
	return BuildPartFileReport(rPartFile, false, false);
}

bool FakeFileDetector::RefreshPartFileHeaderIfAvailable(CPartFile &rPartFile)
{
	const EFileType eExtensionType = FileTypeClassifierSeams::GetFileTypeFromExtension(rPartFile.GetFileName());
	const uint8 uAvailableProbeMask = GetAvailableHeaderProbeMask(rPartFile);
	if (uAvailableProbeMask == 0)
		return false;

	const CString strHash(md4str(rPartFile.GetFileHash()));
	const auto it = g_partHeaderProbeState.find(strHash);
	if (it != g_partHeaderProbeState.end() && it->second.eExtensionType == eExtensionType
		&& (it->second.uProbeMask & uAvailableProbeMask) == uAvailableProbeMask)
	{
		return false;
	}

	(void)AnalyzePartFile(rPartFile);
	SPartHeaderProbeState &rState = g_partHeaderProbeState[strHash];
	rState.eExtensionType = eExtensionType;
	rState.uProbeMask |= uAvailableProbeMask;
	return true;
}

CString FakeFileDetector::FormatReportSummary(const SFakeFileReport &rReport)
{
	CString strSummary;
	strSummary.Format(_T("Fake-file score: %u (%S)"), rReport.nScore, SeverityToToken(rReport.eSeverity));
	if (!rReport.astrReasons.empty()) {
		strSummary += _T(" - ");
		for (size_t i = 0; i < rReport.astrReasons.size(); ++i) {
			if (i > 0)
				strSummary += _T(", ");
			strSummary += rReport.astrReasons[i];
		}
	}
	return strSummary;
}

const char* FakeFileDetector::SeverityToToken(FakeFileDetectorSeams::Severity eSeverity)
{
	switch (eSeverity) {
	case FakeFileDetectorSeams::Severity::Low:
		return "low";
	case FakeFileDetectorSeams::Severity::Medium:
		return "medium";
	case FakeFileDetectorSeams::Severity::High:
		return "high";
	case FakeFileDetectorSeams::Severity::Critical:
		return "critical";
	case FakeFileDetectorSeams::Severity::None:
	default:
		return "none";
	}
}
