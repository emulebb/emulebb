#pragma once

#include <atlstr.h>
#include <shlwapi.h>
#include "LongPathSeams.h"

namespace FileCompletionCommandSeams
{
struct CompletionCommandContext
{
	bool enabled = false;
	bool appClosing = false;
	bool completionSucceeded = false;
	bool knownFileAdded = false;
	CString programPath;
	CString argumentTemplate;
	CString filePath;
	CString directory;
	CString fileName;
	CString fileHash;
	CString categoryName;
	unsigned __int64 fileSize = 0;
};

struct CompletionCommandLaunchRequest
{
	CString applicationName;
	CString commandLine;
	CString workingDirectory;
};

/** Returns whether the configured program type is intentionally executable-only. */
inline bool HasSupportedProgramExtension(const CString &programPath)
{
	const LPCTSTR pszExt = ::PathFindExtension(programPath);
	return pszExt != NULL && (_tcsicmp(pszExt, _T(".exe")) == 0 || _tcsicmp(pszExt, _T(".com")) == 0);
}

/** Returns whether the Files preference page can accept this configured program. */
inline bool IsValidConfiguredProgramPath(const CString &programPath)
{
	CString trimmed(programPath);
	trimmed.Trim();
	return !trimmed.IsEmpty() && HasSupportedProgramExtension(trimmed) && LongPathSeams::PathExists(trimmed);
}

/** Quotes an argument using Windows command-line quoting rules. */
inline CString QuoteCommandLineArgument(const CString &value)
{
	CString quoted(_T("\""));
	int backslashes = 0;

	for (int i = 0; i < value.GetLength(); ++i) {
		const TCHAR ch = value[i];
		if (ch == _T('\\')) {
			++backslashes;
			continue;
		}
		if (ch == _T('"')) {
			for (int slash = 0; slash < backslashes * 2 + 1; ++slash)
				quoted += _T('\\');
			quoted += ch;
			backslashes = 0;
			continue;
		}
		while (backslashes-- > 0)
			quoted += _T('\\');
		backslashes = 0;
		quoted += ch;
	}

	for (int slash = 0; slash < backslashes * 2; ++slash)
		quoted += _T('\\');
	quoted += _T('"');
	return quoted;
}

/** Expands the supported completion tokens without environment-variable expansion. */
inline CString ExpandArguments(const CompletionCommandContext &context)
{
	CString result(context.argumentTemplate);
	CString fileSize;
	fileSize.Format(_T("%I64u"), static_cast<unsigned __int64>(context.fileSize));

	result.Replace(_T("%F"), QuoteCommandLineArgument(context.filePath));
	result.Replace(_T("%D"), QuoteCommandLineArgument(context.directory));
	result.Replace(_T("%N"), context.fileName);
	result.Replace(_T("%H"), context.fileHash);
	result.Replace(_T("%S"), fileSize);
	result.Replace(_T("%C"), context.categoryName);
	return result;
}

/** Builds the CreateProcess input for a retained, successfully completed file. */
inline bool TryBuildLaunchRequest(const CompletionCommandContext &context, CompletionCommandLaunchRequest &request)
{
	CString programPath(context.programPath);
	programPath.Trim();
	if (!context.enabled || context.appClosing || !context.completionSucceeded || !context.knownFileAdded
		|| programPath.IsEmpty() || !HasSupportedProgramExtension(programPath)) {
		return false;
	}

	request.applicationName = programPath;
	request.commandLine = QuoteCommandLineArgument(programPath);
	const CString expandedArguments(ExpandArguments(context));
	if (!expandedArguments.IsEmpty()) {
		request.commandLine += _T(' ');
		request.commandLine += expandedArguments;
	}
	request.workingDirectory = context.directory;
	return true;
}
}
