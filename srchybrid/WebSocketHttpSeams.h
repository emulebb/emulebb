#pragma once

#include <cstdint>
#include <cstddef>
#include <limits>
#include <string>

#include "WebServerJsonSeams.h"

/**
 * @brief Pure HTTP request parsing helpers used before REST and compatibility
 * dispatch.
 */
namespace WebSocketHttpSeams
{
static const uint64_t kMaxHttpContentLength = 16ui64 * 1024ui64 * 1024ui64;
static const uint64_t kMaxHttpHeaderLength = 64ui64 * 1024ui64;
static const uint64_t kMaxQueuedResponseBytes = 16ui64 * 1024ui64 * 1024ui64;
static const uint32_t kAcceptedClientIoTimeoutMs = 30000u;
static const DWORD kSocketThreadShutdownTimeoutMs = 30000u;
static const size_t kAcceptedClientReadBufferBytes = 4u * 1024u;
// WHY: accepted web workers still share CWebServer request/session state, most
// visibly m_uCurIP and broad _ProcessURL access to main-thread eMule objects.
// Keep this at one until those fields are made per-connection or explicitly
// protected; raising only the thread budget would turn the resource cap into
// wrong-IP logging/session races and wider UI-state data races.
static const size_t kMaxAcceptedClientThreads = 1u;

#define EMULEBB_WEBSOCKET_HTTP_SEAMS_HAS_REJECTED_IP_ACTION 1

enum class EHttpHeaderScanResult
{
	Incomplete,
	Complete,
	TooLarge
};

enum class EContentLengthHeader
{
	NotContentLength,
	Valid,
	Invalid
};

enum class EHeaderValueResult
{
	Missing,
	Found,
	Duplicate
};

enum class ERejectedRemoteAccessIpAction
{
	ContinueAcceptDrain,
	StopAcceptDrain
};

enum class ESocketThreadShutdownFollowUp
{
	CompleteShutdown,
	DeferShutdownCleanup
};

/**
 * @brief Reports whether one HTTP method token is supported by the web
 * dispatcher.
 */
inline bool IsSupportedDispatchMethod(const std::string &rMethod)
{
	return rMethod == "GET" || rMethod == "POST" || rMethod == "PATCH" || rMethod == "DELETE";
}

/**
 * @brief Reports whether another accepted WebServer client thread may be
 * started without exceeding the R1 resource budget.
 */
inline bool CanStartAcceptedClientThread(const size_t uCurrentAcceptedThreads)
{
	return uCurrentAcceptedThreads < kMaxAcceptedClientThreads;
}

/**
 * @brief Reports whether a stalled accepted client may retain more response bytes.
 *
 * The listener accepts one web client at a time, but that client can still stop
 * reading after causing the server to generate a response. Bound queued response
 * bytes separately from request-body limits so backpressure cannot retain
 * unbounded CChunk buffers until the accepted-client timeout fires.
 */
inline bool CanQueueResponseBytes(const uint64_t uCurrentQueuedBytes, const uint32_t uAddingBytes)
{
	if (uAddingBytes > kMaxQueuedResponseBytes)
		return false;
	return uCurrentQueuedBytes <= kMaxQueuedResponseBytes - uAddingBytes;
}

/**
 * @brief Accounts for response bytes that have left the queued send chunks.
 */
inline uint64_t ConsumeQueuedResponseBytes(const uint64_t uCurrentQueuedBytes, const size_t uSentBytes)
{
	return uSentBytes >= uCurrentQueuedBytes ? 0u : uCurrentQueuedBytes - uSentBytes;
}

/**
 * @brief Keeps the listener draining already-pending accepts after rejecting
 * one disallowed remote IP address.
 */
inline ERejectedRemoteAccessIpAction GetRejectedRemoteAccessIpAction()
{
	return ERejectedRemoteAccessIpAction::ContinueAcceptDrain;
}

/**
 * @brief Defers socket cleanup after a bounded wait times out or fails.
 */
inline ESocketThreadShutdownFollowUp GetSocketThreadShutdownFollowUp(const bool bBoundedWaitSucceeded)
{
	return bBoundedWaitSucceeded
		? ESocketThreadShutdownFollowUp::CompleteShutdown
		: ESocketThreadShutdownFollowUp::DeferShutdownCleanup;
}

/**
 * @brief Parses the method and request target from one HTTP request header.
 */
inline bool TryParseRequestLine(const std::string &rHeader, std::string &rMethod, std::string &rRequestTarget)
{
	rMethod.clear();
	rRequestTarget.clear();

	const std::string::size_type uLineEnd = rHeader.find('\n');
	std::string strLine(rHeader.substr(0, uLineEnd == std::string::npos ? std::string::npos : uLineEnd));
	while (!strLine.empty() && strLine[strLine.size() - 1] == '\r')
		strLine.erase(strLine.size() - 1);

	const std::string::size_type uFirstSpace = strLine.find(' ');
	if (uFirstSpace == std::string::npos || uFirstSpace == 0)
		return false;
	const std::string::size_type uSecondSpace = strLine.find(' ', uFirstSpace + 1);
	if (uSecondSpace == std::string::npos || uSecondSpace <= uFirstSpace + 1)
		return false;

	rMethod = strLine.substr(0, uFirstSpace);
	rRequestTarget = strLine.substr(uFirstSpace + 1, uSecondSpace - uFirstSpace - 1);
	return !rMethod.empty() && !rRequestTarget.empty();
}

/**
 * @brief Scans the receive buffer for one complete HTTP header without
 * allowing unbounded pre-header growth.
 */
inline EHttpHeaderScanResult ScanHttpHeaderLength(const char *pBuffer, const size_t nBufferSize, uint32_t &ruHeaderLength)
{
	ruHeaderLength = 0;
	if (pBuffer == NULL)
		return nBufferSize == 0 ? EHttpHeaderScanResult::Incomplete : EHttpHeaderScanResult::TooLarge;

	bool bPrevEndl = false;
	for (size_t uPos = 0; uPos < nBufferSize; ++uPos) {
		if ('\n' == pBuffer[uPos]) {
			if (bPrevEndl) {
				ruHeaderLength = static_cast<uint32_t>(uPos + 1);
				return EHttpHeaderScanResult::Complete;
			}
			bPrevEndl = true;
		} else if ('\r' != pBuffer[uPos])
			bPrevEndl = false;
	}

	return nBufferSize > kMaxHttpHeaderLength
		? EHttpHeaderScanResult::TooLarge
		: EHttpHeaderScanResult::Incomplete;
}

/**
 * @brief Strictly parses one Content-Length field value.
 */
inline bool TryParseContentLengthValue(const std::string &rValue, uint32_t &ruContentLength)
{
	ruContentLength = 0;
	uint64_t ullContentLength = 0;
	if (!WebServerJsonSeams::TryParseUnsignedDecimalValue(WebServerJsonSeams::TrimAsciiWhitespace(rValue), ullContentLength))
		return false;
	if (ullContentLength > kMaxHttpContentLength)
		return false;
	ruContentLength = static_cast<uint32_t>(ullContentLength);
	return true;
}

/**
 * @brief Classifies and parses one raw HTTP header line.
 */
inline EContentLengthHeader ParseContentLengthHeaderLine(const std::string &rLine, uint32_t &ruContentLength)
{
	ruContentLength = 0;
	std::string strLine(rLine);
	while (!strLine.empty() && (strLine[strLine.size() - 1] == '\r' || strLine[strLine.size() - 1] == '\n'))
		strLine.erase(strLine.size() - 1);

	const std::string::size_type uColon = strLine.find(':');
	if (uColon == std::string::npos)
		return EContentLengthHeader::NotContentLength;

	const std::string strName(WebServerJsonSeams::ToLowerAscii(WebServerJsonSeams::TrimAsciiWhitespace(strLine.substr(0, uColon))));
	if (strName != "content-length")
		return EContentLengthHeader::NotContentLength;

	return TryParseContentLengthValue(strLine.substr(uColon + 1), ruContentLength)
		? EContentLengthHeader::Valid
		: EContentLengthHeader::Invalid;
}

/**
 * @brief Scans one complete HTTP header block for a single valid
 * Content-Length field.
 */
inline bool TryParseContentLengthHeaders(
	const std::string &rHeader,
	bool &rbHasContentLength,
	uint32_t &ruContentLength)
{
	rbHasContentLength = false;
	ruContentLength = 0;

	for (std::string::size_type uPos = 0; uPos < rHeader.size();) {
		const std::string::size_type uLineEnd = rHeader.find('\n', uPos);
		const std::string strLine(rHeader.substr(uPos, uLineEnd == std::string::npos ? std::string::npos : uLineEnd - uPos));

		uint32_t uParsedContentLength = 0;
		const EContentLengthHeader eContentLength = ParseContentLengthHeaderLine(strLine, uParsedContentLength);
		if (eContentLength == EContentLengthHeader::Invalid)
			return false;
		if (eContentLength == EContentLengthHeader::Valid) {
			if (rbHasContentLength)
				return false;
			rbHasContentLength = true;
			ruContentLength = uParsedContentLength;
		}

		if (uLineEnd == std::string::npos)
			break;
		uPos = uLineEnd + 1;
	}

	return true;
}

/**
 * @brief Reads one case-insensitive HTTP header value and rejects duplicate
 * sensitive header fields.
 */
inline EHeaderValueResult GetSingleHeaderValue(
	const std::string &rHeader,
	const std::string &rHeaderName,
	std::string &rValue)
{
	rValue.clear();
	const std::string strHeaderName(WebServerJsonSeams::ToLowerAscii(WebServerJsonSeams::TrimAsciiWhitespace(rHeaderName)));
	if (strHeaderName.empty())
		return EHeaderValueResult::Missing;

	bool bFound = false;
	for (std::string::size_type uPos = 0; uPos < rHeader.size();) {
		const std::string::size_type uLineEnd = rHeader.find('\n', uPos);
		std::string strLine(rHeader.substr(uPos, uLineEnd == std::string::npos ? std::string::npos : uLineEnd - uPos));
		while (!strLine.empty() && (strLine[strLine.size() - 1] == '\r' || strLine[strLine.size() - 1] == '\n'))
			strLine.erase(strLine.size() - 1);

		if (strLine.empty())
			break;

		const std::string::size_type uColon = strLine.find(':');
		if (uColon != std::string::npos && uColon > 0) {
			const std::string strName(WebServerJsonSeams::ToLowerAscii(WebServerJsonSeams::TrimAsciiWhitespace(strLine.substr(0, uColon))));
			if (strName == strHeaderName) {
				if (bFound) {
					rValue.clear();
					return EHeaderValueResult::Duplicate;
				}
				bFound = true;
				rValue = WebServerJsonSeams::TrimAsciiWhitespace(strLine.substr(uColon + 1));
			}
		}

		if (uLineEnd == std::string::npos)
			break;
		uPos = uLineEnd + 1;
	}

	return bFound ? EHeaderValueResult::Found : EHeaderValueResult::Missing;
}

/**
 * @brief Reports whether the buffered bytes contain the complete request body
 * promised by the already-parsed header.
 */
inline bool IsCompleteHttpRequestBuffered(
	const size_t nBufferedBytes,
	const uint32_t uHeaderLength,
	const uint32_t uContentLength)
{
	if (uHeaderLength == 0 || nBufferedBytes < uHeaderLength)
		return false;
	if (uContentLength == 0)
		return true;

	return static_cast<uint64_t>(uHeaderLength) + static_cast<uint64_t>(uContentLength) <= nBufferedBytes;
}

/**
 * @brief Calculates the next receive-buffer size without DWORD wraparound.
 *
 * The socket stores receive-buffer lengths in DWORDs because the surrounding
 * MFC socket API does. HTTP header and body parsing already apply protocol
 * limits, but this allocation boundary still needs its own checked arithmetic:
 * if a future caller changes read sizes or preserves pipelined bytes, the heap
 * size must not wrap before the later parser limits run.
 */
inline bool TryCalculateReceiveBufferSize(
	const uint32_t uBufferedBytes,
	const uint32_t uIncomingBytes,
	const uint32_t uPreserveBytes,
	uint32_t &ruBufferSize)
{
	const uint64_t uRequiredBytes = static_cast<uint64_t>(uBufferedBytes) + static_cast<uint64_t>(uIncomingBytes);
	const uint64_t uAllocatedBytes = uRequiredBytes + static_cast<uint64_t>(uPreserveBytes);
	if (uRequiredBytes > static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)())
		|| uAllocatedBytes > static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)()))
	{
		ruBufferSize = 0;
		return false;
	}

	ruBufferSize = static_cast<uint32_t>(uAllocatedBytes);
	return true;
}
}
