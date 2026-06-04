#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace UploadBlockRequestSeams
{
inline constexpr std::size_t kUploadBlockFileIdBytes = 16u;

/**
 * @brief Stable lookup key for one upload block request range.
 */
struct SUploadBlockRequestKey
{
	std::uint64_t ullStartOffset = 0;
	std::uint64_t ullEndOffset = 0;
	std::array<unsigned char, kUploadBlockFileIdBytes> abyFileId = {};
};

inline bool operator<(const SUploadBlockRequestKey &rLeft, const SUploadBlockRequestKey &rRight)
{
	if (rLeft.ullStartOffset != rRight.ullStartOffset)
		return rLeft.ullStartOffset < rRight.ullStartOffset;
	if (rLeft.ullEndOffset != rRight.ullEndOffset)
		return rLeft.ullEndOffset < rRight.ullEndOffset;
	return rLeft.abyFileId < rRight.abyFileId;
}

inline SUploadBlockRequestKey BuildUploadBlockRequestKey(
	const std::uint64_t ullStartOffset,
	const std::uint64_t ullEndOffset,
	const unsigned char *pabyFileId)
{
	SUploadBlockRequestKey key = {};
	key.ullStartOffset = ullStartOffset;
	key.ullEndOffset = ullEndOffset;
	if (pabyFileId != nullptr)
		std::memcpy(key.abyFileId.data(), pabyFileId, key.abyFileId.size());
	return key;
}
}
