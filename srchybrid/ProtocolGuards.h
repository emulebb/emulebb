//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#pragma once

#include <cstddef>
#include <limits>
#include "types.h"

/**
 * Returns the payload size encoded in an eD2k-style packet header while treating
 * zero-length packets as malformed instead of letting the subtraction underflow.
 */
inline uint32 GetPacketPayloadSize(uint32 packetlength)
{
	return packetlength > 0 ? packetlength - 1 : 0;
}

/**
 * Reports whether a TCP packet header can be materialized into the receive buffer
 * without overflowing the payload area after the opcode byte is removed.
 */
inline bool CanReadTcpPacketPayload(uint32 packetlength, size_t readBufferSize)
{
	return packetlength > 0 && static_cast<size_t>(packetlength - 1) <= readBufferSize;
}

/**
 * Reports whether a serialized byte span fits fully within a packet buffer.
 */
inline bool CanReadPacketSpan(size_t packetSize, size_t offset, size_t spanSize)
{
	return offset <= packetSize && spanSize <= packetSize - offset;
}

/**
 * Reports whether a saved partial TCP header can be restored into the shared
 * receive buffer without overrunning the header scratch area or the read buffer.
 */
inline bool CanRestoreTcpPendingHeader(size_t pendingHeaderSize, size_t packetHeaderSize, size_t readBufferSize)
{
	return pendingHeaderSize <= packetHeaderSize && pendingHeaderSize <= readBufferSize;
}

/**
 * Reports whether the trailing bytes left in the TCP read buffer are small
 * enough to cache as the next partial header fragment.
 */
inline bool CanStoreTcpPendingHeader(size_t trailingByteCount, size_t packetHeaderSize)
{
	return trailingByteCount < packetHeaderSize;
}

/**
 * Reports whether a partially assembled packet has not already overrun its
 * target payload buffer.
 */
inline bool CanContinuePacketAssembly(uint32 packetSize, uint32 bufferedByteCount)
{
	return bufferedByteCount <= packetSize;
}

/**
 * Safely adds a fixed overhead to a byte count without allowing wraparound.
 */
inline bool TryAddSize(size_t baseSize, size_t extraSize, size_t *pnCombinedSize)
{
	if (pnCombinedSize == NULL || baseSize > (std::numeric_limits<size_t>::max)() - extraSize)
		return false;

	*pnCombinedSize = baseSize + extraSize;
	return true;
}

/**
 * Safely multiplies a byte count and adds a fixed overhead without allowing wraparound.
 */
inline bool TryMultiplyAddSize(size_t baseSize, size_t multiplier, size_t addend, size_t *pnExpandedSize)
{
	if (pnExpandedSize == NULL)
		return false;

	if (multiplier != 0 && baseSize > ((std::numeric_limits<size_t>::max)() - addend) / multiplier)
		return false;

	*pnExpandedSize = baseSize * multiplier + addend;
	return true;
}

/**
 * Rejects impossible or hostile tag counts before entering the per-tag parsing loop.
 */
inline bool HasSaneTagCount(ULONGLONG position, ULONGLONG length, uint32 tagcount, uint32 maxTagCount)
{
	return position <= length
		&& tagcount <= maxTagCount
		&& static_cast<ULONGLONG>(tagcount) <= (length - position);
}

/**
 * Reports whether a UDP payload contains the protocol byte and opcode byte.
 */
inline bool HasUdpPayloadHeader(UINT payloadLength)
{
	return payloadLength >= 2;
}

/**
 * Reports whether a compressed UDP payload contains the protocol and opcode bytes
 * plus at least one compressed byte for the inflater input buffer.
 */
inline bool HasCompressedUdpPayload(UINT payloadLength)
{
	return payloadLength > 2;
}

/**
 * Reports whether the UDP callback packet is large enough to rewrite the
 * forwarded IP/port fields and still carry a non-empty forwarded payload.
 */
inline bool HasUdpCallbackPayload(size_t payloadLength)
{
	return payloadLength >= 17 && CanReadPacketSpan(payloadLength, 10u, 6u);
}

/**
 * Reports whether OP_REASKFILEPING contains the mandatory file hash.
 */
inline bool HasUdpReaskFilePingBase(size_t payloadLength)
{
	return payloadLength >= 16u;
}

/**
 * Reports whether legacy UDP reask extended-info contains the complete-source count.
 */
inline bool HasUdpLegacyReaskCompleteSourceCount(size_t payloadLength)
{
	return payloadLength >= 18u;
}

/**
 * Reports whether OP_REASKACK contains the mandatory queue-rank field.
 */
inline bool HasUdpReaskAckBase(size_t payloadLength)
{
	return payloadLength >= 2u;
}

/**
 * Reports whether OP_REASKACK still has the queue-rank field after optional
 * variable-length file-status parsing consumed its prefix.
 */
inline bool HasUdpReaskAckQueueRank(ULONGLONG position, ULONGLONG length)
{
	return position <= length && length - position >= 2u;
}

/**
 * Reports whether OP_DIRECTCALLBACKREQ contains tcp port, user hash, and options.
 */
inline bool HasUdpDirectCallbackRequest(size_t payloadLength)
{
	return payloadLength >= 19u;
}

/**
 * Reports whether OP_CALLBACK contains Kad check ID, file ID, IP, and TCP port.
 */
inline bool HasTcpKadCallbackPayload(size_t payloadLength)
{
	return payloadLength >= 38u;
}

/**
 * Reports whether OP_REASKCALLBACKTCP contains destination IP, TCP port, and file hash.
 */
inline bool HasTcpBuddyReaskCallbackPayload(size_t payloadLength)
{
	return payloadLength >= 22u;
}

/**
 * Returns the minimum serialized block-packet header length for the current
 * offset width and compression mode.
 */
inline uint32 GetDownloadBlockPacketHeaderSize(const bool bPacked, const bool bI64Offsets)
{
	const uint32 nFileHashSize = 16;
	const uint32 nOffsetFieldSize = bI64Offsets ? 8u : 4u;
	return nFileHashSize + nOffsetFieldSize + (bPacked ? 4u : nOffsetFieldSize);
}

/**
 * Reports whether a download block packet is long enough to read its fixed header fields.
 */
inline bool HasDownloadBlockPacketHeader(size_t packetSize, const bool bPacked, const bool bI64Offsets)
{
	return packetSize >= GetDownloadBlockPacketHeaderSize(bPacked, bI64Offsets);
}

/**
 * Reports whether a peer-supplied item count can fit when every remaining item
 * has at least a fixed record prefix. Variable tag payloads may still need
 * deeper parsing, but impossible counts are rejected before entering the loop.
 */
inline bool HasSaneCountedRecords(size_t remainingBytes, uint32 recordCount, size_t minimumRecordSize)
{
	return minimumRecordSize == 0u
		|| recordCount <= remainingBytes / minimumRecordSize;
}

constexpr uint32 kMaxPersistentMetadataTags = 4096u;
constexpr size_t kMinPersistentMetadataTagBytes = sizeof(uint8) + sizeof(uint8) + sizeof(uint8);

/**
 * Reports whether a local persisted tag count can fit before tag parsing starts.
 */
inline bool HasSanePersistentMetadataTagCount(ULONGLONG position, ULONGLONG length, uint32 tagCount)
{
	return position <= length
		&& tagCount <= kMaxPersistentMetadataTags
		&& HasSaneCountedRecords(static_cast<size_t>(length - position), tagCount, kMinPersistentMetadataTagBytes);
}

/**
 * Reports whether a blob payload can be read without underflowing the
 * remaining-byte calculation first.
 */
inline bool CanReadBlobPayload(ULONGLONG position, ULONGLONG length, uint32 blobSize)
{
	return position <= length
		&& static_cast<ULONGLONG>(blobSize) <= (length - position);
}

/**
 * Returns a bounded integer percentage and treats a zero denominator as 0%
 * instead of relying on ASSERT-only guards.
 */
inline uint32 CalculateProgressPercent(uint64 completed, uint64 total)
{
	if (total == 0)
		return 0;

	const uint64 boundedCompleted = completed <= total ? completed : total;
	return static_cast<uint32>((static_cast<double>(boundedCompleted) * 100.0) / static_cast<double>(total));
}

/**
 * Returns a bounded floating-point progress ratio while treating a zero or
 * negative denominator as an empty-progress state.
 */
inline float CalculateProgressRatio(float completed, float total)
{
	if (total <= 0.0f)
		return 0.0f;

	const float ratio = completed / total;
	if (ratio < 0.0f)
		return 0.0f;
	return ratio <= 1.0f ? ratio : 1.0f;
}

/**
 * Parses a dotted IPv4 literal without conflating the valid broadcast address
 * 255.255.255.255 with an invalid parse result.
 */
inline bool TryParseDottedIPv4Literal(const char *pszAddress, uint32 *pnAddress)
{
	if (pszAddress == NULL || pnAddress == NULL)
		return false;

	unsigned int octets[4] = {};
	const char *pszCursor = pszAddress;
	const size_t nOctetCount = sizeof(octets) / sizeof(octets[0]);
	for (size_t index = 0; index < nOctetCount; ++index) {
		if (*pszCursor < '0' || *pszCursor > '9')
			return false;

		unsigned int nOctet = 0;
		do {
			nOctet = nOctet * 10 + static_cast<unsigned int>(*pszCursor - '0');
			if (nOctet > 255)
				return false;
			++pszCursor;
		} while (*pszCursor >= '0' && *pszCursor <= '9');

		octets[index] = nOctet;
		if (index + 1 == nOctetCount)
			break;

		if (*pszCursor != '.')
			return false;
		++pszCursor;
	}

	if (*pszCursor != '\0')
		return false;

	uint8 *pBytes = reinterpret_cast<uint8*>(pnAddress);
	pBytes[0] = static_cast<uint8>(octets[0]);
	pBytes[1] = static_cast<uint8>(octets[1]);
	pBytes[2] = static_cast<uint8>(octets[2]);
	pBytes[3] = static_cast<uint8>(octets[3]);
	return true;
}
