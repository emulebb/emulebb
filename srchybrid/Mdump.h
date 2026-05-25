#pragma once

struct _EXCEPTION_POINTERS;

class CMiniDumper
{
public:
	/**
	 * @brief Reports the result of an operator-requested diagnostic dump write.
	 */
	struct SManualDumpResult
	{
		bool bSuccess;
		DWORD dwError;
		CString strDumpPath;
	};

	static void Enable(LPCTSTR pszAppName, bool bShowErrors, LPCTSTR pszDumpDir);
	/**
	 * @brief Writes an operator-requested diagnostic dump for the current process.
	 */
	static SManualDumpResult CreateManualDump(LPCTSTR pszAppName, LPCTSTR pszDumpDir, bool bFullMemoryDump);
	unsigned uCreateCrashDump; //0 - no dump; 1 - create dump if user agrees; 2 - create without asking
	bool bCaptureFullCrashDump;
private:
	static CString m_strAppName;
	static CString m_strDumpDir;

	static LONG WINAPI TopLevelFilter(struct _EXCEPTION_POINTERS *pExceptionInfo) noexcept;
};

extern CMiniDumper theCrashDumper;
