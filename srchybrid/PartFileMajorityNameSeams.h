#pragma once

#include "FilenameNormalizationPolicy.h"
#include "FilenameTextRepairSeams.h"
#include "FilenameTokenizationSeams.h"

#include <atlstr.h>
#include <cstdint>
#include <vector>

namespace PartFileMajorityNameSeams
{
struct MajorityNameSelection
{
	bool HasCandidate = false;
	CString Name;
	CString CanonicalName;
	UINT CandidateVotes = 0;
	UINT TotalVotes = 0;
	UINT RequiredPercent = 51;
	UINT MinimumVotes = 0;
};

/**
 * @brief Normalizes the required source-name agreement percentage for majority filename decisions.
 */
inline UINT NormalizeRequiredPercent(UINT percent)
{
	if (percent < 1)
		return 1;
	if (percent > 100)
		return 100;
	return percent;
}

/**
 * @brief Returns whether a candidate has enough source-name agreement to be applied.
 */
inline bool HasRequiredAgreement(UINT candidateVotes, UINT totalVotes, UINT requiredPercent)
{
	if (candidateVotes == 0 || totalVotes == 0 || candidateVotes > totalVotes)
		return false;
	return static_cast<uint64_t>(candidateVotes) * 100u >= static_cast<uint64_t>(NormalizeRequiredPercent(requiredPercent)) * totalVotes;
}

/**
 * @brief Repairs and normalizes one source-provided filename before majority voting.
 */
inline bool TryPrepareMajoritySourceFilename(const CString &rstrSourceName, CString &rstrPreparedName)
{
	const CString strRepaired(FilenameTextRepairSeams::RepairIncomingFilenameText(rstrSourceName));
	return FilenameNormalizationPolicy::TryNormalizeDownloadFilenameCandidate(strRepaired, rstrPreparedName);
}

/**
 * @brief Selects the unique majority filename candidate from normalized source-provided names.
 */
inline MajorityNameSelection SelectMajorityName(const std::vector<CString> &sourceNames, UINT minimumVotes, UINT requiredPercent)
{
	struct Bucket
	{
		CString CanonicalName;
		CString Name;
		UINT Votes = 0;
		UINT BestExactVotes = 0;
	};

	std::vector<Bucket> buckets;
	UINT totalVotes = 0;
	for (const CString &sourceName : sourceNames) {
		CString name(sourceName);
		name.Trim();
		if (name.IsEmpty())
			continue;
		const FilenameTokenizationSeams::CanonicalName canonical = FilenameTokenizationSeams::BuildCanonicalName(std::wstring(name.GetString()));
		if (!canonical.hasUsableBaseName || canonical.canonical.empty())
			continue;
		if (canonical.canonical == L"download")
			continue;
		CString canonicalName(canonical.canonical.c_str());

		++totalVotes;
		bool found = false;
		for (Bucket &bucket : buckets) {
			if (bucket.CanonicalName.CompareNoCase(canonicalName) == 0) {
				++bucket.Votes;
				UINT exactVotes = 0;
				for (const CString &candidateName : sourceNames) {
					CString normalizedCandidate(candidateName);
					normalizedCandidate.Trim();
					if (normalizedCandidate.Compare(name) == 0)
						++exactVotes;
				}
				if (exactVotes > bucket.BestExactVotes || (exactVotes == bucket.BestExactVotes && bucket.Name.CompareNoCase(name) > 0)) {
					bucket.Name = name;
					bucket.BestExactVotes = exactVotes;
				}
				found = true;
				break;
			}
		}
		if (!found) {
			Bucket bucket;
			bucket.CanonicalName = canonicalName;
			bucket.Name = name;
			bucket.Votes = 1;
			bucket.BestExactVotes = 1;
			buckets.push_back(bucket);
		}
	}

	MajorityNameSelection selection;
	selection.TotalVotes = totalVotes;
	selection.RequiredPercent = NormalizeRequiredPercent(requiredPercent);
	selection.MinimumVotes = minimumVotes;

	bool tiedForFirst = false;
	for (const Bucket &bucket : buckets) {
		if (bucket.Votes > selection.CandidateVotes) {
			selection.Name = bucket.Name;
			selection.CanonicalName = bucket.CanonicalName;
			selection.CandidateVotes = bucket.Votes;
			tiedForFirst = false;
		} else if (bucket.Votes == selection.CandidateVotes) {
			tiedForFirst = true;
		}
	}

	selection.HasCandidate = !tiedForFirst
		&& selection.CandidateVotes >= minimumVotes
		&& HasRequiredAgreement(selection.CandidateVotes, selection.TotalVotes, selection.RequiredPercent);
	if (!selection.HasCandidate)
		selection.Name.Empty();
	if (!selection.HasCandidate)
		selection.CanonicalName.Empty();
	return selection;
}
}
