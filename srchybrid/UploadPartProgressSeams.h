#pragma once

#include "KnownFile.h"
#include "UpDownClient.h"

namespace UploadPartProgressSeams
{
	inline uint64 GetPartBytesForPart(const CKnownFile *file, UINT uPart, uint64 uFileSize)
	{
		const uint64 uPartStart = static_cast<uint64>(uPart) * PARTSIZE;
		if (file == NULL || uPartStart >= uFileSize)
			return 0;
		return min(static_cast<uint64>(PARTSIZE), uFileSize - uPartStart);
	}

	inline uint64 GetReportedProgressBytes(const CUpDownClient *client, const CKnownFile *file, uint64 uFileSize)
	{
		if (client == NULL || file == NULL || !client->HasUpPartStatusReported())
			return 0;

		uint64 uReportedBytes = 0;
		const UINT uPartCount = client->GetUpPartCount();
		for (UINT uPart = 0; uPart < uPartCount; ++uPart) {
			if (client->IsUpPartAvailable(uPart))
				uReportedBytes += GetPartBytesForPart(file, uPart, uFileSize);
		}
		return uReportedBytes;
	}

	inline uint64 GetEstimatedProgressBytes(const CUpDownClient *client, const CKnownFile *file)
	{
		const uint64 uFileSize = file != NULL ? static_cast<uint64>(file->GetFileSize()) : 0;
		const UINT uPartCount = client != NULL ? client->GetUpPartCount() : 0;
		if (uFileSize == 0 || uPartCount == 0 || client == NULL)
			return 0;

		uint64 uEstimatedBytes = GetReportedProgressBytes(client, file, uFileSize);
		const uint64 uSessionBytes = client->GetSessionUp();
		if (client->HasUpPartStatusReported()) {
			const uint64 uBaseline = min(client->GetUpPartStatusSessionUpBaseline(), uSessionBytes);
			uEstimatedBytes += uSessionBytes - uBaseline;
		} else
			uEstimatedBytes = uSessionBytes;

		return min(uEstimatedBytes, uFileSize);
	}

	inline double GetProgressPercent(const CUpDownClient *client, const CKnownFile *file)
	{
		const uint64 uFileSize = file != NULL ? static_cast<uint64>(file->GetFileSize()) : 0;
		if (uFileSize == 0 || client == NULL || client->GetUpPartCount() == 0)
			return -1.0;

		const uint64 uEstimatedBytes = GetEstimatedProgressBytes(client, file);
		if (uEstimatedBytes == 0)
			return 0.0;
		return static_cast<double>(uEstimatedBytes) * 100.0 / static_cast<double>(uFileSize);
	}

	inline CString FormatProgressPercentText(const CUpDownClient *client, const CKnownFile *file)
	{
		CString strText;
		const double fPercent = GetProgressPercent(client, file);
		if (fPercent > 0.0)
			strText.Format(_T("%.1f%%"), fPercent);
		return strText;
	}

	inline int GetProgressPercentSortValue(const CUpDownClient *client, const CKnownFile *file)
	{
		const double fPercent = GetProgressPercent(client, file);
		if (fPercent < 0.0)
			return -1;
		return static_cast<int>(fPercent * 10.0 + 0.5);
	}

	inline int CompareProgressPercent(const CUpDownClient *client1, const CKnownFile *file1, const CUpDownClient *client2, const CKnownFile *file2)
	{
		const int iPct1 = GetProgressPercentSortValue(client1, file1);
		const int iPct2 = GetProgressPercentSortValue(client2, file2);
		if (iPct1 < iPct2)
			return -1;
		if (iPct1 > iPct2)
			return 1;
		return 0;
	}

	inline uint64 GetMissingBytes(const CUpDownClient *client, const CKnownFile *file)
	{
		const uint64 uFileSize = file != NULL ? static_cast<uint64>(file->GetFileSize()) : 0;
		if (uFileSize == 0 || client == NULL || client->GetUpPartCount() == 0)
			return 0;

		const uint64 uEstimatedBytes = GetEstimatedProgressBytes(client, file);
		return uEstimatedBytes < uFileSize ? uFileSize - uEstimatedBytes : 0;
	}
}
