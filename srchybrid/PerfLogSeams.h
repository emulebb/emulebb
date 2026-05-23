#pragma once

#include <atlstr.h>
#include "LogArtifactNames.h"

namespace PerfLogSeams
{
inline CString BuildMrtgSidecarPath(const CString &rstrConfiguredPath, LPCTSTR pszSuffixWithExtension)
{
	return LogArtifactNames::BuildPathWithStemSuffix(rstrConfiguredPath, pszSuffixWithExtension);
}
}
