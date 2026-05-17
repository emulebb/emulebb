#pragma once

#include "DialogInputParsingSeams.h"

namespace AddFriendInputSeams
{
enum class FriendInputStatus
{
	Valid,
	InvalidAddress,
	MissingPort,
	InvalidEmbeddedPort,
	InvalidPort
};

struct FriendInput
{
	FriendInputStatus Status = FriendInputStatus::InvalidAddress;
	uint32_t NetworkOrderAddress = 0;
	uint16_t Port = 0;
	CString UserName;
	bool AddressContainedPort = false;
};

/**
 * @brief Normalizes the optional friend nickname using the same length limit as the edit control.
 */
inline CString NormalizeFriendName(CString strUserName, int iMaxUserNickLength)
{
	strUserName.Trim();
	if (iMaxUserNickLength < 0)
		iMaxUserNickLength = 0;
	if (strUserName.GetLength() > iMaxUserNickLength)
		strUserName.Truncate(iMaxUserNickLength);
	return strUserName;
}

/**
 * @brief Builds a validated friend-add request from dialog text fields.
 */
inline FriendInput ParseFriendInput(const CString& strAddressText, const CString& strPortText, const CString& strUserName, int iMaxUserNickLength)
{
	FriendInput input;
	input.UserName = NormalizeFriendName(strUserName, iMaxUserNickLength);

	uint16_t uEmbeddedPort = 0;
	if (!DialogInputParsingSeams::TryParseIPv4AddressAndOptionalPort(strAddressText, input.NetworkOrderAddress, uEmbeddedPort, input.AddressContainedPort)) {
		input.Status = strAddressText.Find(_T(':')) >= 0 ? FriendInputStatus::InvalidEmbeddedPort : FriendInputStatus::InvalidAddress;
		return input;
	}

	if (input.AddressContainedPort) {
		input.Port = uEmbeddedPort;
		input.Status = FriendInputStatus::Valid;
		return input;
	}

	if (strPortText.IsEmpty()) {
		input.Status = FriendInputStatus::MissingPort;
		return input;
	}

	if (!DialogInputParsingSeams::TryParseTcpPort(strPortText, input.Port)) {
		input.Status = FriendInputStatus::InvalidPort;
		return input;
	}

	input.Status = FriendInputStatus::Valid;
	return input;
}
}
