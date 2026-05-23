#pragma once

#include <atlstr.h>
#include "LogArtifactNames.h"

namespace LogFileSeams
{
inline CString BuildRotatedLogFilePath(const CString &rstrFilePath, const CString &rstrTimestamp)
{
	return LogArtifactNames::BuildPathWithTimestampSuffix(rstrFilePath, rstrTimestamp);
}
}
