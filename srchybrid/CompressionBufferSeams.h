#pragma once

#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#define EMULEBB_TEST_HAVE_OWNED_BYTE_BUFFER_GROWTH 1

/**
 * @brief Derives an initial zlib work-buffer size and caps it to the caller's maximum.
 */
inline bool TryDeriveZlibBufferSize(size_t nSourceSize, size_t nMultiplier, size_t nBias, size_t nMaximumSize, size_t *pnBufferSize)
{
	if (pnBufferSize == nullptr || nMaximumSize == 0)
		return false;

	size_t nDerivedSize = 0;
	if (nMultiplier != 0 && nSourceSize > ((std::numeric_limits<size_t>::max)() - nBias) / nMultiplier)
		nDerivedSize = nMaximumSize;
	else
		nDerivedSize = nSourceSize * nMultiplier + nBias;

	if (nDerivedSize == 0 || nDerivedSize > nMaximumSize)
		nDerivedSize = nMaximumSize;

	*pnBufferSize = nDerivedSize;
	return true;
}

/**
 * @brief Calculates the next bounded zlib retry-buffer size for `Z_BUF_ERROR` growth loops.
 */
inline bool TryGrowZlibBufferSize(size_t nCurrentSize, size_t nMaximumSize, size_t *pnNextSize)
{
	if (pnNextSize == nullptr || nCurrentSize == 0 || nCurrentSize >= nMaximumSize)
		return false;

	*pnNextSize = (nCurrentSize > nMaximumSize / 2u) ? nMaximumSize : nCurrentSize * 2u;
	return *pnNextSize > nCurrentSize;
}

/**
 * @brief Copies vector-backed temporary data into a legacy `new[]` buffer for ownership handoff.
 */
inline std::unique_ptr<unsigned char[]> MakeOwnedByteBufferCopy(const std::vector<unsigned char> &rSource, size_t nCopiedSize)
{
	if (nCopiedSize == 0 || nCopiedSize > rSource.size())
		return std::unique_ptr<unsigned char[]>();

	std::unique_ptr<unsigned char[]> pCopy(new unsigned char[nCopiedSize]);
	std::memcpy(pCopy.get(), rSource.data(), nCopiedSize);
	return pCopy;
}

/**
 * @brief Grows a legacy-owned byte buffer while preserving already-inflated bytes.
 *
 * Download decompression ultimately queues a `new[]` buffer into CPartFile,
 * whose buffered-data cleanup still uses `delete[]`. Growing this ownership
 * type directly avoids the previous vector-to-new[] copy at the hot handoff
 * while keeping the old deletion contract unchanged.
 */
inline bool TryGrowOwnedByteBuffer(std::unique_ptr<unsigned char[]> &rpBuffer, const size_t nCurrentBytes, const size_t nNextCapacity)
{
	if (!rpBuffer || nNextCapacity < nCurrentBytes)
		return false;

	std::unique_ptr<unsigned char[]> pNext(new unsigned char[nNextCapacity]);
	if (nCurrentBytes > 0)
		std::memcpy(pNext.get(), rpBuffer.get(), nCurrentBytes);
	rpBuffer.swap(pNext);
	return true;
}
