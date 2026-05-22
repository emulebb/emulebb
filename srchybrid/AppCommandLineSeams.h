#pragma once

#include <atlstr.h>
#include <tchar.h>
#include <vector>

#include "StartupConfigOverride.h"

namespace AppCommandLineSeams
{
	/**
	 * @brief High-level startup mode selected by the normalized eMuleBB command line.
	 */
	enum class EMode
	{
		NormalStartup,
		Help,
		GenerateWebServerCertificate,
		DiagnoseMediaMetadata,
		Invalid
	};

	/**
	 * @brief Parsed, validated command-line state used by startup and native tests.
	 */
	struct SParseResult
	{
		EMode eMode = EMode::NormalStartup;
		bool bIgnoreInstances = false;
		bool bAutoStart = false;
		bool bAssertFile = false;
		bool bHasConfigBaseDir = false;
		CString strConfigBaseDir;
		CString strPositional;
		CString strCertFile;
		CString strKeyFile;
		CString strMetadataInputFile;
		CString strMetadataOutputFile;
		std::vector<CStringA> astrCertDnsNames;
		std::vector<CStringA> astrCertIpAddresses;
		CString strError;
		CString strUsage;
	};

	/**
	 * @brief Builds the supported eMuleBB command-line usage text.
	 */
	inline CString BuildUsageText()
	{
		return _T("Usage:\r\n")
			_T("  emulebb.exe [options] [ed2k-link|collection-file|command]\r\n")
			_T("\r\n")
			_T("Options:\r\n")
			_T("  -c <base-dir>                         Use an isolated eMule base directory.\r\n")
			_T("  -ignoreinstances                      Start without enforcing the running-instance guard.\r\n")
			_T("  -AutoStart                            Mark this startup as an automatic startup.\r\n")
			_T("  -assertfile                           Debug builds write CRT assertion output to a file.\r\n")
			_T("  --generate-webserver-cert             Generate a WebServer TLS certificate and exit.\r\n")
			_T("  --cert <path>                         Certificate output path for certificate generation.\r\n")
			_T("  --key <path>                          Private-key output path for certificate generation.\r\n")
			_T("  --host <dns-or-ip>                    Certificate subject alternative name; repeatable.\r\n")
			_T("  --diagnose-media-metadata             Probe maintained metadata extractors and exit.\r\n")
			_T("  --input <path>                        Media file path for metadata diagnostics.\r\n")
			_T("  --output <path>                       Optional JSON output path for metadata diagnostics.\r\n")
			_T("  --help, -h, /?                        Show this help text and exit.\r\n");
	}

	inline bool IsSwitchToken(const CString &strToken)
	{
		return !strToken.IsEmpty() && (strToken[0] == _T('-') || strToken[0] == _T('/'));
	}

	inline bool TrySplitSwitchToken(const CString &strToken, CString &rstrName, CString &rstrInlineValue, bool &rbHasInlineValue)
	{
		rstrName.Empty();
		rstrInlineValue.Empty();
		rbHasInlineValue = false;
		if (!IsSwitchToken(strToken))
			return false;

		CString strBody(strToken);
		if (strBody.Left(2) == _T("--"))
			strBody = strBody.Mid(2);
		else
			strBody = strBody.Mid(1);

		const int iEquals = strBody.Find(_T('='));
		if (iEquals >= 0) {
			rstrInlineValue = strBody.Mid(iEquals + 1);
			strBody = strBody.Left(iEquals);
			rbHasInlineValue = true;
		}

		strBody.MakeLower();
		rstrName = strBody;
		return !rstrName.IsEmpty();
	}

	inline bool TrySetSingleton(bool &rbSeen, LPCTSTR pszOptionDisplayName, CString &rstrError)
	{
		if (!rbSeen) {
			rbSeen = true;
			return true;
		}
		rstrError.Format(_T("The %s option may be specified only once."), pszOptionDisplayName);
		return false;
	}

	inline bool TryReadOptionValue(
		const std::vector<CString> &raTokens,
		size_t &riIndex,
		LPCTSTR pszOptionDisplayName,
		const bool bHasInlineValue,
		const CString &strInlineValue,
		CString &rstrValue,
		CString &rstrError)
	{
		if (bHasInlineValue) {
			rstrValue = strInlineValue;
		} else {
			if (riIndex + 1 >= raTokens.size() || IsSwitchToken(raTokens[riIndex + 1])) {
				rstrError.Format(_T("The %s option requires a value."), pszOptionDisplayName);
				return false;
			}
			rstrValue = raTokens[++riIndex];
		}
		rstrValue.Trim();
		if (rstrValue.IsEmpty()) {
			rstrError.Format(_T("The %s option requires a non-empty value."), pszOptionDisplayName);
			return false;
		}
		return true;
	}

	inline bool TryParseIpv4Octet(const CStringA &strOctet)
	{
		if (strOctet.IsEmpty() || strOctet.GetLength() > 3)
			return false;
		int iValue = 0;
		for (int i = 0; i < strOctet.GetLength(); ++i) {
			const char ch = strOctet[i];
			if (ch < '0' || ch > '9')
				return false;
			iValue = iValue * 10 + (ch - '0');
		}
		return iValue <= 255;
	}

	inline bool LooksLikeIpv4Address(const CStringA &strValue)
	{
		int iPartCount = 0;
		int iStart = 0;
		for (int i = 0; i <= strValue.GetLength(); ++i) {
			if (i != strValue.GetLength() && strValue[i] != '.')
				continue;
			if (!TryParseIpv4Octet(strValue.Mid(iStart, i - iStart)))
				return false;
			++iPartCount;
			iStart = i + 1;
		}
		return iPartCount == 4;
	}

	inline bool LooksLikeIpv6Address(const CStringA &strValue)
	{
		if (strValue.Find(':') < 0)
			return false;
		for (int i = 0; i < strValue.GetLength(); ++i) {
			const char ch = strValue[i];
			const bool bHex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
			if (!bHex && ch != ':' && ch != '.')
				return false;
		}
		return true;
	}

	inline bool LooksLikeIpAddress(const CStringA &strValue)
	{
		return LooksLikeIpv4Address(strValue) || LooksLikeIpv6Address(strValue);
	}

	inline bool ContainsWhitespace(const CString &strValue)
	{
		for (int i = 0; i < strValue.GetLength(); ++i) {
			if (_istspace(strValue[i]))
				return true;
		}
		return false;
	}

	inline bool AddCertificateHost(const CString &strHost, SParseResult &rResult, CString &rstrError)
	{
		CString strTrimmed(strHost);
		strTrimmed.Trim();
		if (strTrimmed.IsEmpty() || ContainsWhitespace(strTrimmed)) {
			rstrError = _T("The --host option requires a DNS name or IP address without whitespace.");
			return false;
		}

		const CStringA strHostA(strTrimmed);
		if (LooksLikeIpAddress(strHostA))
			rResult.astrCertIpAddresses.push_back(strHostA);
		else
			rResult.astrCertDnsNames.push_back(strHostA);
		return true;
	}

	inline bool IsRecognizedSwitchName(const CString &strName)
	{
		return strName == _T("?")
			|| strName == _T("h")
			|| strName == _T("help")
			|| strName == _T("c")
			|| strName == _T("ignoreinstances")
			|| strName == _T("autostart")
			|| strName == _T("assertfile")
			|| strName == _T("generate-webserver-cert")
			|| strName == _T("cert")
			|| strName == _T("key")
			|| strName == _T("host")
			|| strName == _T("diagnose-media-metadata")
			|| strName == _T("input")
			|| strName == _T("output");
	}

	/**
	 * @brief Parses the normalized Windows argv tokens into eMuleBB startup options.
	 */
	inline SParseResult ParseTokens(const std::vector<CString> &raTokens)
	{
		SParseResult result;
		result.strUsage = BuildUsageText();

		bool bSeenConfig = false;
		bool bSeenIgnoreInstances = false;
		bool bSeenAutoStart = false;
		bool bSeenAssertFile = false;
		bool bSeenGenerate = false;
		bool bSeenMetadataDiagnostic = false;
		bool bSeenCert = false;
		bool bSeenKey = false;
		bool bSeenInput = false;
		bool bSeenOutput = false;
		bool bSawCertificateHost = false;

		for (size_t i = 1; i < raTokens.size(); ++i) {
			const CString &strToken = raTokens[i];
			if (!IsSwitchToken(strToken)) {
				if (!result.strPositional.IsEmpty()) {
					result.eMode = EMode::Invalid;
					result.strError = _T("Only one positional command, link, or file argument is supported.");
					return result;
				}
				result.strPositional = strToken;
				continue;
			}

			CString strName;
			CString strInlineValue;
			bool bHasInlineValue = false;
			if (!TrySplitSwitchToken(strToken, strName, strInlineValue, bHasInlineValue) || !IsRecognizedSwitchName(strName)) {
				result.eMode = EMode::Invalid;
				result.strError.Format(_T("Unknown command-line switch: %s"), (LPCTSTR)strToken);
				return result;
			}

			if (strName == _T("?") || strName == _T("h") || strName == _T("help")) {
				if (bHasInlineValue) {
					result.eMode = EMode::Invalid;
					result.strError.Format(_T("The %s option does not accept a value."), (LPCTSTR)strToken);
					return result;
				}
				result.eMode = EMode::Help;
				continue;
			}

			if (strName == _T("c")) {
				CString strValue;
				if (!TrySetSingleton(bSeenConfig, _T("-c"), result.strError)
					|| !TryReadOptionValue(raTokens, i, _T("-c"), bHasInlineValue, strInlineValue, strValue, result.strError)) {
					result.eMode = EMode::Invalid;
					return result;
				}
				if (!StartupConfigOverride::IsAbsoluteBaseDirPath(strValue)) {
					result.eMode = EMode::Invalid;
					result.strError = _T("The -c option requires a canonical absolute eMule base directory like C:\\path.");
					return result;
				}
				result.bHasConfigBaseDir = true;
				result.strConfigBaseDir = StartupConfigOverride::NormalizeBaseDir(strValue);
				continue;
			}

			if (strName == _T("ignoreinstances")) {
				if (bHasInlineValue || !TrySetSingleton(bSeenIgnoreInstances, _T("-ignoreinstances"), result.strError)) {
					result.eMode = EMode::Invalid;
					if (result.strError.IsEmpty())
						result.strError = _T("The -ignoreinstances option does not accept a value.");
					return result;
				}
				result.bIgnoreInstances = true;
				continue;
			}

			if (strName == _T("autostart")) {
				if (bHasInlineValue || !TrySetSingleton(bSeenAutoStart, _T("-AutoStart"), result.strError)) {
					result.eMode = EMode::Invalid;
					if (result.strError.IsEmpty())
						result.strError = _T("The -AutoStart option does not accept a value.");
					return result;
				}
				result.bAutoStart = true;
				continue;
			}

			if (strName == _T("assertfile")) {
				if (bHasInlineValue || !TrySetSingleton(bSeenAssertFile, _T("-assertfile"), result.strError)) {
					result.eMode = EMode::Invalid;
					if (result.strError.IsEmpty())
						result.strError = _T("The -assertfile option does not accept a value.");
					return result;
				}
				result.bAssertFile = true;
				continue;
			}

			if (strName == _T("generate-webserver-cert")) {
				if (bHasInlineValue || !TrySetSingleton(bSeenGenerate, _T("--generate-webserver-cert"), result.strError)) {
					result.eMode = EMode::Invalid;
					if (result.strError.IsEmpty())
						result.strError = _T("The --generate-webserver-cert option does not accept a value.");
					return result;
				}
				continue;
			}

			if (strName == _T("diagnose-media-metadata")) {
				if (bHasInlineValue || !TrySetSingleton(bSeenMetadataDiagnostic, _T("--diagnose-media-metadata"), result.strError)) {
					result.eMode = EMode::Invalid;
					if (result.strError.IsEmpty())
						result.strError = _T("The --diagnose-media-metadata option does not accept a value.");
					return result;
				}
				continue;
			}

			if (strName == _T("cert")) {
				if (!TrySetSingleton(bSeenCert, _T("--cert"), result.strError)
					|| !TryReadOptionValue(raTokens, i, _T("--cert"), bHasInlineValue, strInlineValue, result.strCertFile, result.strError)) {
					result.eMode = EMode::Invalid;
					return result;
				}
				continue;
			}

			if (strName == _T("key")) {
				if (!TrySetSingleton(bSeenKey, _T("--key"), result.strError)
					|| !TryReadOptionValue(raTokens, i, _T("--key"), bHasInlineValue, strInlineValue, result.strKeyFile, result.strError)) {
					result.eMode = EMode::Invalid;
					return result;
				}
				continue;
			}

			if (strName == _T("host")) {
				CString strHost;
				if (!TryReadOptionValue(raTokens, i, _T("--host"), bHasInlineValue, strInlineValue, strHost, result.strError)
					|| !AddCertificateHost(strHost, result, result.strError)) {
					result.eMode = EMode::Invalid;
					return result;
				}
				bSawCertificateHost = true;
				continue;
			}

			if (strName == _T("input")) {
				if (!TrySetSingleton(bSeenInput, _T("--input"), result.strError)
					|| !TryReadOptionValue(raTokens, i, _T("--input"), bHasInlineValue, strInlineValue, result.strMetadataInputFile, result.strError)) {
					result.eMode = EMode::Invalid;
					return result;
				}
				continue;
			}

			if (strName == _T("output")) {
				if (!TrySetSingleton(bSeenOutput, _T("--output"), result.strError)
					|| !TryReadOptionValue(raTokens, i, _T("--output"), bHasInlineValue, strInlineValue, result.strMetadataOutputFile, result.strError)) {
					result.eMode = EMode::Invalid;
					return result;
				}
				continue;
			}
		}

		if (result.eMode == EMode::Help)
			return result;

		if (bSeenGenerate && bSeenMetadataDiagnostic) {
			result.eMode = EMode::Invalid;
			result.strError = _T("Only one headless command may be specified.");
			return result;
		}

		if (bSeenGenerate) {
			if (result.strCertFile.IsEmpty() || result.strKeyFile.IsEmpty()) {
				result.eMode = EMode::Invalid;
				result.strError = _T("The --generate-webserver-cert command requires --cert and --key.");
				return result;
			}
			if (!result.strPositional.IsEmpty()) {
				result.eMode = EMode::Invalid;
				result.strError = _T("The --generate-webserver-cert command does not accept a positional argument.");
				return result;
			}
			result.eMode = EMode::GenerateWebServerCertificate;
			return result;
		}

		if (bSeenMetadataDiagnostic) {
			if (result.strMetadataInputFile.IsEmpty()) {
				result.eMode = EMode::Invalid;
				result.strError = _T("The --diagnose-media-metadata command requires --input.");
				return result;
			}
			if (!result.strPositional.IsEmpty()) {
				result.eMode = EMode::Invalid;
				result.strError = _T("The --diagnose-media-metadata command does not accept a positional argument.");
				return result;
			}
			result.eMode = EMode::DiagnoseMediaMetadata;
			return result;
		}

		if (bSeenCert || bSeenKey || bSawCertificateHost) {
			result.eMode = EMode::Invalid;
			result.strError = _T("The --cert, --key, and --host options require --generate-webserver-cert.");
			return result;
		}

		if (bSeenInput || bSeenOutput) {
			result.eMode = EMode::Invalid;
			result.strError = _T("The --input and --output options require --diagnose-media-metadata.");
			return result;
		}

		result.eMode = EMode::NormalStartup;
		return result;
	}
}
