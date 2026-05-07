#pragma once

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <climits>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "WebServerJsonSeams.h"

namespace WebApiCommandSeams
{
using json = nlohmann::json;

enum class ESearchMethod : uint8_t
{
	Invalid,
	Automatic,
	Server,
	Global,
	Kad
};

enum class ESearchFileType : uint8_t
{
	Invalid,
	Any,
	Archive,
	Audio,
	CdImage,
	Image,
	Program,
	Video,
	Document,
	EmuleCollection
};

/**
 * @brief Carries the validated public search/start payload before the UI layer
 * maps it onto the legacy search model.
 */
struct SSearchStartRequest
{
	std::string strQuery;
	ESearchMethod eMethod;
	ESearchFileType eFileType;
	std::string strExtension;
	uint64_t ullMinSize;
	uint64_t ullMaxSize;
	uint64_t ullMinAvailability;
	bool bHasMinSize;
	bool bHasMaxSize;
	bool bHasMinAvailability;
	bool bClearExisting;

	SSearchStartRequest()
		: eMethod(ESearchMethod::Automatic)
		, eFileType(ESearchFileType::Any)
		, ullMinSize(0)
		, ullMaxSize(0)
		, ullMinAvailability(0)
		, bHasMinSize(false)
		, bHasMaxSize(false)
		, bHasMinAvailability(false)
		, bClearExisting(false)
	{
	}
};

/**
 * @brief Carries the validated transfer list selector so the UI command path
 * can test argument policy without needing the live download queue.
 */
struct STransfersListRequest
{
	std::string strFilterLower;
	unsigned uCategory;
	bool bHasCategory;

	STransfersListRequest()
		: uCategory(0)
		, bHasCategory(false)
	{
	}
};

/**
 * @brief Carries the bulk mutation selector shared by pause/resume/stop/delete.
 */
struct STransferBulkMutationRequest
{
	std::vector<json> hashes;
	bool bDeleteFiles;

	STransferBulkMutationRequest()
		: bDeleteFiles(false)
	{
	}
};

/**
 * @brief Carries the validated transfer rename payload.
 */
struct STransferRenameRequest
{
	std::string strName;
};

/**
 * @brief Carries the validated shared-file rating/comment mutation payload.
 */
struct SSharedFileRatingCommentRequest
{
	std::string strComment;
	int iRating;

	SSharedFileRatingCommentRequest()
		: iRating(0)
	{
	}
};

inline std::string ToLowerAscii(const std::string &rValue)
{
	return WebServerJsonSeams::ToLowerAscii(rValue);
}

inline std::string TrimAsciiWhitespace(const std::string &rValue)
{
	return WebServerJsonSeams::TrimAsciiWhitespace(rValue);
}

/**
 * @brief Reports whether a string is made only of ASCII decimal digits.
 */
inline bool IsUnsignedDecimalString(const std::string &rValue)
{
	return WebServerJsonSeams::IsValidUnsignedDecimal(rValue);
}

/**
 * @brief Parses a strict unsigned decimal string without accepting signs or
 * whitespace.
 */
inline bool TryParseUnsignedDecimalString(const std::string &rValue, uint64_t &ruValue)
{
	return WebServerJsonSeams::TryParseUnsignedDecimalValue(rValue, ruValue);
}

/**
 * @brief Reports whether a public MD4 hash token uses the required lowercase
 * 32-character hexadecimal form.
 */
inline bool IsLowercaseMd4HexString(const std::string &rValue)
{
	return WebServerJsonSeams::IsLowercaseMd4HexString(rValue);
}

/**
 * @brief Parses a non-negative JSON integer regardless of whether the parser
 * stored it as signed or unsigned.
 */
inline bool TryParseNonNegativeUInt64(const json &rValue, uint64_t &ruValue)
{
	return WebServerJsonSeams::TryParseJsonUInt64(rValue, ruValue);
}

/**
 * @brief Returns the native search method token adapters should use when the
 * caller has not requested a transport-specific search.
 */
inline const char *GetDefaultSearchMethodName()
{
	return "automatic";
}

/**
 * @brief Parses the public search method vocabulary used by search/start.
 */
inline ESearchMethod ParseSearchMethodName(const char *pszMethod)
{
	if (pszMethod == nullptr || pszMethod[0] == '\0')
		return ESearchMethod::Invalid;

	const std::string strMethod(ToLowerAscii(pszMethod));
	if (strMethod == GetDefaultSearchMethodName())
		return ESearchMethod::Automatic;
	if (strMethod == "server")
		return ESearchMethod::Server;
	if (strMethod == "global")
		return ESearchMethod::Global;
	if (strMethod == "kad")
		return ESearchMethod::Kad;
	return ESearchMethod::Invalid;
}

/**
 * @brief Parses the public search type vocabulary used by search/start.
 */
inline ESearchFileType ParseSearchFileTypeName(const char *pszType)
{
	if (pszType == nullptr)
		return ESearchFileType::Invalid;

	const std::string strType(ToLowerAscii(pszType));
	if (strType.empty() || strType == "any")
		return ESearchFileType::Any;
	if (strType == "archive")
		return ESearchFileType::Archive;
	if (strType == "audio")
		return ESearchFileType::Audio;
	if (strType == "cdimage" || strType == "iso")
		return ESearchFileType::CdImage;
	if (strType == "image")
		return ESearchFileType::Image;
	if (strType == "program")
		return ESearchFileType::Program;
	if (strType == "video")
		return ESearchFileType::Video;
	if (strType == "document")
		return ESearchFileType::Document;
	if (strType == "emulecollection")
		return ESearchFileType::EmuleCollection;
	return ESearchFileType::Invalid;
}

/**
 * @brief Validates the search/start payload independently from the live search
 * window so command-contract tests stay deterministic.
 */
inline bool TryParseSearchStartRequest(const json &rParams, SSearchStartRequest &rRequest, std::string &rError)
{
	if (!rParams.contains("query") || !rParams["query"].is_string()) {
		rError = "query must be a string";
		return false;
	}

	if (!WebServerJsonSeams::TryNormalizeSearchText(rParams["query"].get<std::string>(), "query", false, rRequest.strQuery, rError))
		return false;

	rRequest.eMethod = ESearchMethod::Automatic;
	if (rParams.contains("method")) {
		if (!rParams["method"].is_string()) {
			rError = "method must be a string";
			return false;
		}

		rRequest.eMethod = ParseSearchMethodName(rParams["method"].get_ref<const std::string&>().c_str());
		if (rRequest.eMethod == ESearchMethod::Invalid) {
			rError = "method must be one of automatic, server, global, kad";
			return false;
		}
	}

	rRequest.eFileType = ESearchFileType::Any;
	if (rParams.contains("type")) {
		if (!rParams["type"].is_string()) {
			rError = "type must be a string";
			return false;
		}

		rRequest.eFileType = ParseSearchFileTypeName(rParams["type"].get_ref<const std::string&>().c_str());
		if (rRequest.eFileType == ESearchFileType::Invalid) {
			rError = "type is not supported";
			return false;
		}
	}

	rRequest.strExtension.clear();
	if (rParams.contains("extension")) {
		if (!rParams["extension"].is_string()) {
			rError = "extension must be a string";
			return false;
		}
		rRequest.strExtension = rParams["extension"].get<std::string>();
	}

	rRequest.ullMinSize = 0;
	rRequest.bHasMinSize = false;
	if (rParams.contains("minSizeBytes")) {
		if (!TryParseNonNegativeUInt64(rParams["minSizeBytes"], rRequest.ullMinSize)) {
			rError = "minSizeBytes must be an unsigned number";
			return false;
		}
		rRequest.bHasMinSize = true;
	}

	rRequest.ullMaxSize = 0;
	rRequest.bHasMaxSize = false;
	if (rParams.contains("maxSizeBytes")) {
		if (!TryParseNonNegativeUInt64(rParams["maxSizeBytes"], rRequest.ullMaxSize)) {
			rError = "maxSizeBytes must be an unsigned number";
			return false;
		}
		rRequest.bHasMaxSize = true;
	}

	if (rRequest.bHasMinSize && rRequest.bHasMaxSize && rRequest.ullMaxSize < rRequest.ullMinSize) {
		rError = "maxSizeBytes must be greater than or equal to minSizeBytes";
		return false;
	}

	rRequest.ullMinAvailability = 0;
	rRequest.bHasMinAvailability = false;
	if (rParams.contains("minAvailability")) {
		if (!TryParseNonNegativeUInt64(rParams["minAvailability"], rRequest.ullMinAvailability) || rRequest.ullMinAvailability > 1000000ui64) {
			rError = "minAvailability must be an unsigned number in the range 0..1000000";
			return false;
		}
		rRequest.bHasMinAvailability = true;
	}

	rRequest.bClearExisting = false;
	if (rParams.contains("clearExisting")) {
		if (!rParams["clearExisting"].is_boolean()) {
			rError = "clearExisting must be a boolean";
			return false;
		}
		rRequest.bClearExisting = rParams["clearExisting"].get<bool>();
	}

	return true;
}

/**
 * @brief Validates the transfers/list filter payload without touching the live
 * transfer list.
 */
inline bool TryParseTransfersListRequest(const json &rParams, STransfersListRequest &rRequest, std::string &rError)
{
	rRequest.strFilterLower.clear();
	rRequest.uCategory = 0;
	rRequest.bHasCategory = false;

	if (rParams.contains("filter")) {
		if (!rParams["filter"].is_string()) {
			rError = "filter must be a string when provided";
			return false;
		}
		rRequest.strFilterLower = ToLowerAscii(rParams["filter"].get<std::string>());
	}

	if (rParams.contains("categoryId")) {
		uint64_t uCategory = 0;
		if (!TryParseNonNegativeUInt64(rParams["categoryId"], uCategory) || uCategory > UINT_MAX) {
			rError = "categoryId must be an unsigned number";
			return false;
		}
		rRequest.uCategory = static_cast<unsigned>(uCategory);
		rRequest.bHasCategory = true;
	}

	return true;
}

/**
 * @brief Validates the transfers/add payload and trims the public ed2k link
 * before the UI thread starts parsing legacy link objects.
 */
inline bool TryParseTransferAddLink(const json &rParams, std::string &rLink, std::string &rError)
{
	return WebServerJsonSeams::TryParseTransferAddLink(rParams, rLink, rError);
}

/**
 * @brief Validates the shared bulk-mutation payload used by pause/resume/stop
 * and delete before the UI layer resolves hashes to part files.
 */
inline bool TryParseTransferBulkMutationRequest(const json &rParams, STransferBulkMutationRequest &rRequest, std::string &rError)
{
	if (!rParams.contains("hashes") || !rParams["hashes"].is_array()) {
		rError = "hashes must be a string array";
		return false;
	}
	if (rParams.contains("deleteFiles") && !rParams["deleteFiles"].is_boolean()) {
		rError = "deleteFiles must be a boolean";
		return false;
	}

	rRequest.hashes.clear();
	for (const json &hashValue : rParams["hashes"]) {
		if (!hashValue.is_string() || !IsLowercaseMd4HexString(hashValue.get<std::string>())) {
			rError = "hashes must be a string array of 32-character lowercase hex strings";
			return false;
		}
		rRequest.hashes.push_back(hashValue);
	}
	rRequest.bDeleteFiles = rParams.value("deleteFiles", false);
	return true;
}

/**
 * @brief Validates a transfer rename payload before the UI layer resolves the
 * transfer hash.
 */
inline bool TryParseTransferRenameRequest(const json &rParams, STransferRenameRequest &rRequest, std::string &rError)
{
	std::string strName;
	if (!WebServerJsonSeams::TryParseTransferRenameText(rParams, strName, rError))
		return false;

	rRequest.strName = strName;
	return true;
}

/**
 * @brief Validates a shared-file rating/comment mutation payload before the UI
 * layer resolves the shared-file hash.
 */
inline bool TryParseSharedFileRatingCommentRequest(const json &rParams, SSharedFileRatingCommentRequest &rRequest, std::string &rError)
{
	if (!rParams.contains("comment") || !rParams["comment"].is_string()) {
		rError = "comment must be a string";
		return false;
	}
	if (!rParams.contains("rating") || !rParams["rating"].is_number_integer()) {
		rError = "rating must be an integer between 0 and 5";
		return false;
	}

	const int iRating = rParams["rating"].get<int>();
	if (iRating < 0 || iRating > 5) {
		rError = "rating must be an integer between 0 and 5";
		return false;
	}

	rRequest.strComment = rParams["comment"].get<std::string>();
	rRequest.iRating = iRating;
	return true;
}

/**
 * @brief Parses the public decimal search identifier without requiring the live
 * search window.
 */
inline bool TryParseSearchId(const json &rValue, uint32_t &ruSearchID, std::string &rError)
{
	if (!rValue.is_string()) {
		rError = "searchId must be a decimal string";
		return false;
	}

	const std::string strValue = rValue.get<std::string>();
	if (strValue.empty()) {
		rError = "searchId must not be empty";
		return false;
	}

	uint64_t uValue = 0;
	if (!TryParseUnsignedDecimalString(strValue, uValue) || uValue > UINT32_MAX) {
		rError = "searchId must be a valid uint32 decimal string";
		return false;
	}

	ruSearchID = static_cast<uint32_t>(uValue);
	return true;
}
}
