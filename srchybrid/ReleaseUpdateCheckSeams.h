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

#include <cctype>
#include <exception>
#include <limits>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace ReleaseUpdateCheckSeams
{
	/**
	 * @brief Public eMule BB release version used by GitHub tag comparison.
	 */
	struct SModReleaseVersion
	{
		unsigned uMajor;
		unsigned uMinor;
		unsigned uPatch;
	};

	/**
	 * @brief Result of evaluating one GitHub release JSON payload.
	 */
	enum class EReleaseEvaluationStatus
	{
		ParseFailed,
		IgnoredRelease,
		MissingAsset,
		NotNewer,
		Newer
	};

	/**
	 * @brief Normalized release-check decision from a GitHub latest-release response.
	 */
	struct SReleaseEvaluation
	{
		EReleaseEvaluationStatus eStatus = EReleaseEvaluationStatus::ParseFailed;
		SModReleaseVersion version = {};
		std::string strReleaseUrl;
		std::string strRequiredAssetName;
		std::string strError;
	};

	inline constexpr const char *kModReleaseTagPrefix = "emule-bb-v";
	inline constexpr const char *kModReleaseAssetPrefix = "eMule-broadband-";

	/**
	 * @brief Returns the asset platform token for the current binary.
	 */
	inline std::string GetCurrentPlatformAssetToken()
	{
#if defined(_M_ARM64)
		return "arm64";
#else
		return "x64";
#endif
	}

	inline bool TryReadUnsignedComponent(std::string_view text, size_t &uPos, unsigned &uValue)
	{
		if (uPos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[uPos])))
			return false;

		unsigned uParsed = 0;
		do {
			const unsigned uDigit = static_cast<unsigned>(text[uPos] - '0');
			if (uParsed > ((std::numeric_limits<unsigned>::max)() - uDigit) / 10u)
				return false;
			uParsed = uParsed * 10u + uDigit;
			++uPos;
		} while (uPos < text.size() && std::isdigit(static_cast<unsigned char>(text[uPos])));

		uValue = uParsed;
		return true;
	}

	/**
	 * @brief Parses a strict MAJOR.MINOR.PATCH release version.
	 */
	inline bool TryParseReleaseVersion(std::string_view text, SModReleaseVersion &version)
	{
		size_t uPos = 0;
		SModReleaseVersion parsed = {};
		if (!TryReadUnsignedComponent(text, uPos, parsed.uMajor))
			return false;
		if (uPos >= text.size() || text[uPos++] != '.')
			return false;
		if (!TryReadUnsignedComponent(text, uPos, parsed.uMinor))
			return false;
		if (uPos >= text.size() || text[uPos++] != '.')
			return false;
		if (!TryReadUnsignedComponent(text, uPos, parsed.uPatch))
			return false;
		if (uPos != text.size())
			return false;

		version = parsed;
		return true;
	}

	/**
	 * @brief Parses a strict emule-bb-vMAJOR.MINOR.PATCH GitHub release tag.
	 */
	inline bool TryParseReleaseTag(std::string_view strTagName, SModReleaseVersion &version)
	{
		const std::string_view strPrefix(kModReleaseTagPrefix);
		if (strTagName.size() <= strPrefix.size() || strTagName.substr(0, strPrefix.size()) != strPrefix)
			return false;
		return TryParseReleaseVersion(strTagName.substr(strPrefix.size()), version);
	}

	/**
	 * @brief Compares two public release versions.
	 */
	inline int CompareReleaseVersions(const SModReleaseVersion &lhs, const SModReleaseVersion &rhs)
	{
		if (lhs.uMajor != rhs.uMajor)
			return lhs.uMajor < rhs.uMajor ? -1 : 1;
		if (lhs.uMinor != rhs.uMinor)
			return lhs.uMinor < rhs.uMinor ? -1 : 1;
		if (lhs.uPatch != rhs.uPatch)
			return lhs.uPatch < rhs.uPatch ? -1 : 1;
		return 0;
	}

	/**
	 * @brief Formats MAJOR.MINOR.PATCH for release tags and package names.
	 */
	inline std::string FormatReleaseVersion(const SModReleaseVersion &version)
	{
		return std::to_string(version.uMajor) + "." + std::to_string(version.uMinor) + "." + std::to_string(version.uPatch);
	}

	/**
	 * @brief Builds the required platform-specific release package name.
	 */
	inline std::string BuildRequiredAssetName(const SModReleaseVersion &version, const std::string &strPlatformToken)
	{
		return std::string(kModReleaseAssetPrefix) + FormatReleaseVersion(version) + "-" + strPlatformToken + ".zip";
	}

	/**
	 * @brief Returns whether the GitHub release JSON contains the required platform package.
	 */
	inline bool HasRequiredAsset(const nlohmann::json &releaseJson, const std::string &strRequiredAssetName)
	{
		const nlohmann::json::const_iterator itAssets = releaseJson.find("assets");
		if (itAssets == releaseJson.end() || !itAssets->is_array())
			return false;

		for (const nlohmann::json &asset : *itAssets) {
			const nlohmann::json::const_iterator itName = asset.find("name");
			if (itName != asset.end() && itName->is_string() && itName->get<std::string>() == strRequiredAssetName)
				return true;
		}
		return false;
	}

	/**
	 * @brief Evaluates GitHub latest-release JSON against the compiled local release and required asset.
	 */
	inline SReleaseEvaluation EvaluateLatestReleaseJson(
		const std::string &strJson,
		const SModReleaseVersion &localVersion,
		const std::string &strPlatformToken)
	{
		SReleaseEvaluation evaluation;
		try {
			const nlohmann::json releaseJson = nlohmann::json::parse(strJson);

			if (releaseJson.value("draft", false) || releaseJson.value("prerelease", false)) {
				evaluation.eStatus = EReleaseEvaluationStatus::IgnoredRelease;
				evaluation.strError = "latest release is draft or prerelease";
				return evaluation;
			}

			const nlohmann::json::const_iterator itTagName = releaseJson.find("tag_name");
			if (itTagName == releaseJson.end() || !itTagName->is_string() || !TryParseReleaseTag(itTagName->get<std::string>(), evaluation.version)) {
				evaluation.eStatus = EReleaseEvaluationStatus::IgnoredRelease;
				evaluation.strError = "latest release tag does not match emule-bb-vMAJOR.MINOR.PATCH";
				return evaluation;
			}

			evaluation.strRequiredAssetName = BuildRequiredAssetName(evaluation.version, strPlatformToken);
			if (!HasRequiredAsset(releaseJson, evaluation.strRequiredAssetName)) {
				evaluation.eStatus = EReleaseEvaluationStatus::MissingAsset;
				evaluation.strError = "latest release is missing the required platform package";
				return evaluation;
			}

			evaluation.strReleaseUrl = releaseJson.value("html_url", std::string());
			if (CompareReleaseVersions(evaluation.version, localVersion) <= 0) {
				evaluation.eStatus = EReleaseEvaluationStatus::NotNewer;
				return evaluation;
			}

			evaluation.eStatus = EReleaseEvaluationStatus::Newer;
			return evaluation;
		} catch (const std::exception &ex) {
			evaluation.eStatus = EReleaseEvaluationStatus::ParseFailed;
			evaluation.strError = ex.what();
			return evaluation;
		}
	}
}
