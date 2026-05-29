#pragma once

#include <limits>
#include <vector>

#include "types.h"

#define EMULEBB_TEST_HAVE_COLLECTION_OWNERSHIP_SEAMS 1
#define EMULEBB_TEST_HAVE_COLLECTION_REJECTED_IMPORT_SEAM 1
#define EMULEBB_TEST_HAVE_COLLECTION_FILE_IMPORT_SEAMS 1

constexpr uint32 COLLECTION_FILE_MAX_ENTRY_TAGS = 256u;

struct CollectionSignatureLayout
{
	UINT nMessageLength;
	UINT nSignatureLength;
};

/**
 * @brief Builds the signed-message and trailing-signature spans from a serialized collection payload.
 */
inline bool TryBuildCollectionSignatureLayout(const ULONGLONG nSerializedLength, const ULONGLONG nSignedLength, CollectionSignatureLayout &layout)
{
	if (nSerializedLength <= nSignedLength)
		return false;
	if (nSerializedLength > static_cast<ULONGLONG>((std::numeric_limits<UINT>::max)()))
		return false;
	if (nSignedLength > static_cast<ULONGLONG>((std::numeric_limits<UINT>::max)()))
		return false;

	layout.nMessageLength = static_cast<UINT>(nSignedLength);
	layout.nSignatureLength = static_cast<UINT>(nSerializedLength - nSignedLength);
	return layout.nSignatureLength > 0;
}

/**
 * @brief Converts a serialized collection length into the on-disk 32-bit format when representable.
 */
inline bool TryConvertCollectionSerializedLength(const ULONGLONG nSerializedLength, uint32 &dwSerializedLength)
{
	if (nSerializedLength > static_cast<ULONGLONG>((std::numeric_limits<uint32>::max)()))
		return false;

	dwSerializedLength = static_cast<uint32>(nSerializedLength);
	return true;
}

/**
 * @brief Keeps collection import tolerant of malformed individual entries while continuing the outer file load.
 */
inline bool ShouldContinueAfterCollectionEntryFailure()
{
	return true;
}

/**
 * @brief Rejects impossible or hostile per-file tag counts before parsing collection file tags.
 */
inline bool HasSaneCollectionFileTagCount(const ULONGLONG nPosition, const ULONGLONG nLength, const uint32 nTagCount, const uint32 nMaxTagCount = COLLECTION_FILE_MAX_ENTRY_TAGS)
{
	return nPosition <= nLength
		&& nTagCount <= nMaxTagCount
		&& static_cast<ULONGLONG>(nTagCount) <= nLength - nPosition;
}

/**
 * @brief Keeps malformed optional collection file tags skippable within one serialized entry.
 */
inline bool ShouldSkipMalformedCollectionFileTag()
{
	return true;
}

/**
 * @brief Rejects collection file entries which cannot identify an eD2K file hash.
 */
inline bool ShouldRejectCollectionFileWithoutHash()
{
	return true;
}

/**
 * @brief Ignores malformed optional AICH metadata while keeping the collection entry importable.
 */
inline bool ShouldIgnoreInvalidCollectionAICHHash()
{
	return true;
}

/**
 * @brief Treats rejected imported collection entries as still owned by the importer.
 */
inline bool ShouldDisposeRejectedCollectionImportEntry()
{
	return true;
}

/**
 * @brief Returns the raw author-key view while preserving the historical NULL-for-empty contract.
 */
inline const BYTE *GetCollectionAuthorKeyData(const std::vector<BYTE> &keyData)
{
	return keyData.empty() ? NULL : keyData.data();
}

/**
 * @brief Replaces the stored collection author key and keeps the serialized size field in sync.
 */
inline void AssignCollectionAuthorKey(const BYTE *pKeyData, const uint32 nKeySize, std::vector<BYTE> &keyData, uint32 &rnStoredKeySize)
{
	if (pKeyData != NULL && nKeySize != 0u) {
		keyData.assign(pKeyData, pKeyData + nKeySize);
		rnStoredKeySize = nKeySize;
	} else {
		keyData.clear();
		rnStoredKeySize = 0u;
	}
}

/**
 * @brief Clears the stored collection author key and resets the serialized size field.
 */
inline void ClearCollectionAuthorKey(std::vector<BYTE> &keyData, uint32 &rnStoredKeySize)
{
	keyData.clear();
	rnStoredKeySize = 0u;
}
