#pragma once

#include <cstddef>
#include <cstring>
#include <limits>

#include "types.h"

#define EMULEBB_TEST_HAVE_CLIENT_CREDITS_BUFFER_SEAMS 1
#define EMULEBB_TEST_HAVE_CLIENT_CREDITS_FAILURE_POLICY_SEAMS 1

struct ClientCreditsChallengeLayout
{
	size_t nMessageLength;
	size_t nChallengeIpLength;
};

constexpr size_t kClientCreditsMaxPublicKeySize = 80u;

/**
 * @brief Builds the bounded secure-ident challenge layout from the caller-provided key length.
 */
inline bool TryBuildClientCreditsChallengeLayout(const size_t nKeyLength, const bool bIncludeChallengeIp, ClientCreditsChallengeLayout &layout)
{
	if (nKeyLength > kClientCreditsMaxPublicKeySize)
		return false;

	layout.nChallengeIpLength = bIncludeChallengeIp ? 5u : 0u;
	layout.nMessageLength = nKeyLength + 4u + layout.nChallengeIpLength;
	return layout.nMessageLength <= kClientCreditsMaxPublicKeySize + 9u;
}

/**
 * @brief Reports whether the caller-provided signature buffer can hold the encoded signature bytes.
 */
inline bool CanStoreClientCreditsSignature(const size_t nSignatureLength, const size_t nMaxSize)
{
	return nSignatureLength <= nMaxSize;
}

/**
 * @brief Keeps failed secure-ident signature creation on the historical zero-length failure result.
 */
inline uint8 GetClientCreditsSignatureFailureResult()
{
	return 0u;
}

/**
 * @brief Mirrors the existing verbose-only debug logging policy for non-fatal secure-ident failures.
 */
inline bool ShouldLogClientCreditsFailureDetail(const bool bVerboseLogging)
{
	return bVerboseLogging;
}

/**
 * @brief Derives the transient clients.met save-buffer size while rejecting overflow.
 */
inline bool TryBuildClientCreditsSaveBufferSize(const size_t nRecordCount, const size_t nRecordSize, size_t *pnBufferSize)
{
	if (pnBufferSize == NULL)
		return false;
	if (nRecordSize != 0u && nRecordCount > (std::numeric_limits<size_t>::max)() / nRecordSize)
		return false;

	*pnBufferSize = nRecordCount * nRecordSize;
	return true;
}

/**
 * @brief Clears the secure-ident runtime state so startup and reinitialization share one safe reset path.
 */
template <typename TSigner, typename TByte, size_t N, typename TLen>
inline void ResetClientCreditsCryptState(TSigner *&pSignKey, TByte (&abyPublicKey)[N], TLen &nPublicKeyLen)
{
	pSignKey = NULL;
	nPublicKeyLen = 0;
	memset(abyPublicKey, 0, sizeof(abyPublicKey));
}

/**
 * @brief Applies the existing secure-ident failure transition without re-deriving it at each callsite.
 */
template <typename TState>
inline TState GetClientCreditsStateAfterVerifyFailure(const TState currentState, const TState neededState, const TState failedState)
{
	return currentState == neededState ? failedState : currentState;
}
