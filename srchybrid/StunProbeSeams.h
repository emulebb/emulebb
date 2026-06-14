#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// RFC 5389 STUN Binding codec and public server list for the UDP egress probe.
// This is the UDP counterpart to PublicIpProbeSeams.h (which probes the egress
// public IPv4 over TCP/HTTP): a STUN Binding Request reports the reflexive
// (server-observed) address, letting us verify the *UDP* egress path that the
// HTTP probe cannot reach. Header-only and free of socket/MFC state so the codec
// can be exercised in isolation.
namespace StunProbeSeams
{
	struct SStunIpv4ProbeServer
	{
		const char *pszLabel; // host:port for logging/diagnostics
		const char *pszHost;
		const char *pszPort; // UDP service/port
	};

	inline const SStunIpv4ProbeServer* GetStunIpv4ProbeServers(size_t& rnCount)
	{
		// Long-running public STUN servers. Google is UDP-only on 19302; the
		// others answer on the IANA STUN port 3478. Multiple entries so a single
		// flaky/rate-limited server does not sink the probe.
		static const SStunIpv4ProbeServer kServers[] = {
			{"stun.l.google.com:19302", "stun.l.google.com", "19302"},
			{"stun1.l.google.com:19302", "stun1.l.google.com", "19302"},
			{"stun.cloudflare.com:3478", "stun.cloudflare.com", "3478"},
			{"stun.nextcloud.com:3478", "stun.nextcloud.com", "3478"},
		};
		rnCount = sizeof kServers / sizeof kServers[0];
		return kServers;
	}

	constexpr uint16_t kStunBindingRequest = 0x0001;
	constexpr uint16_t kStunBindingSuccess = 0x0101;
	constexpr uint16_t kStunAttrMappedAddress = 0x0001;
	constexpr uint16_t kStunAttrXorMappedAddress = 0x0020;
	constexpr uint32_t kStunMagicCookie = 0x2112A442;
	constexpr uint8_t kStunFamilyIpv4 = 0x01;
	constexpr size_t kStunHeaderLen = 20;
	constexpr size_t kStunTransactionIdLen = 12;

	// Builds the 20-byte STUN Binding Request: type, zero message length, magic
	// cookie, then the caller-supplied 96-bit transaction id (no attributes).
	// pTxid must point at kStunTransactionIdLen bytes; pBuffer at kStunHeaderLen.
	inline void BuildStunBindingRequest(const uint8_t *pTxid, uint8_t *pBuffer)
	{
		pBuffer[0] = static_cast<uint8_t>((kStunBindingRequest >> 8) & 0xff);
		pBuffer[1] = static_cast<uint8_t>(kStunBindingRequest & 0xff);
		pBuffer[2] = 0; // message length high byte (no attributes)
		pBuffer[3] = 0; // message length low byte
		pBuffer[4] = static_cast<uint8_t>((kStunMagicCookie >> 24) & 0xff);
		pBuffer[5] = static_cast<uint8_t>((kStunMagicCookie >> 16) & 0xff);
		pBuffer[6] = static_cast<uint8_t>((kStunMagicCookie >> 8) & 0xff);
		pBuffer[7] = static_cast<uint8_t>(kStunMagicCookie & 0xff);
		memcpy(pBuffer + 8, pTxid, kStunTransactionIdLen);
	}

	// Parses a STUN Binding Success response and writes the reflexive IPv4 as a
	// network-order scalar into ruNetworkOrderAddress, using the exact byte order
	// of IPv4AddressSeams::TryParseIPv4Address (octet a.b.c.d -> a | b<<8 | c<<16 |
	// d<<24) so the value drops straight into the VPN-guard CIDR check with no
	// string round-trip. Returns false on any malformed/mismatched response or
	// non-IPv4 address.
	inline bool TryParseStunIpv4Response(const uint8_t *pData, size_t nLen, const uint8_t *pTxid, uint32_t& ruNetworkOrderAddress)
	{
		ruNetworkOrderAddress = 0;
		if (pData == NULL || nLen < kStunHeaderLen)
			return false;
		const uint16_t uType = static_cast<uint16_t>((pData[0] << 8) | pData[1]);
		if (uType != kStunBindingSuccess)
			return false;
		const uint32_t uCookie = (static_cast<uint32_t>(pData[4]) << 24)
			| (static_cast<uint32_t>(pData[5]) << 16)
			| (static_cast<uint32_t>(pData[6]) << 8)
			| static_cast<uint32_t>(pData[7]);
		if (uCookie != kStunMagicCookie)
			return false;
		if (memcmp(pData + 8, pTxid, kStunTransactionIdLen) != 0)
			return false;

		const size_t nMsgLen = static_cast<size_t>((pData[2] << 8) | pData[3]);
		const size_t nDeclared = kStunHeaderLen + nMsgLen;
		const size_t nTotal = nLen < nDeclared ? nLen : nDeclared;
		size_t nPos = kStunHeaderLen;
		while (nPos + 4 <= nTotal) {
			const uint16_t uAttrType = static_cast<uint16_t>((pData[nPos] << 8) | pData[nPos + 1]);
			const size_t nAttrLen = static_cast<size_t>((pData[nPos + 2] << 8) | pData[nPos + 3]);
			const size_t nValuePos = nPos + 4;
			if (nValuePos + nAttrLen > nTotal)
				break;

			if ((uAttrType == kStunAttrXorMappedAddress || uAttrType == kStunAttrMappedAddress) && nAttrLen >= 8) {
				// value layout: reserved(1) family(1) port(2) address(4 for IPv4)
				const uint8_t uFamily = pData[nValuePos + 1];
				if (uFamily == kStunFamilyIpv4) {
					uint8_t bytes[4] = {
						pData[nValuePos + 4], pData[nValuePos + 5],
						pData[nValuePos + 6], pData[nValuePos + 7]};
					if (uAttrType == kStunAttrXorMappedAddress) {
						bytes[0] ^= static_cast<uint8_t>((kStunMagicCookie >> 24) & 0xff);
						bytes[1] ^= static_cast<uint8_t>((kStunMagicCookie >> 16) & 0xff);
						bytes[2] ^= static_cast<uint8_t>((kStunMagicCookie >> 8) & 0xff);
						bytes[3] ^= static_cast<uint8_t>(kStunMagicCookie & 0xff);
					}
					// network-order scalar: octet a.b.c.d -> a | b<<8 | c<<16 | d<<24
					ruNetworkOrderAddress = static_cast<uint32_t>(bytes[0])
						| (static_cast<uint32_t>(bytes[1]) << 8)
						| (static_cast<uint32_t>(bytes[2]) << 16)
						| (static_cast<uint32_t>(bytes[3]) << 24);
					return true;
				}
				// family 0x02 (IPv6) is intentionally skipped: IPv4-only client.
			}
			// attributes are padded to a 4-byte boundary
			nPos = nValuePos + ((nAttrLen + 3) & ~static_cast<size_t>(3));
		}
		return false;
	}
}
