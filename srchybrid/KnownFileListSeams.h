#pragma once

#define EMULE_TESTS_HAS_KNOWN_FILE_COLLISION_SEAMS 1
#define EMULE_TESTS_HAS_KNOWN_FILE_PUBLISH_SEAMS 1

/**
 * @brief Determines whether a stored AICH hashset should be purged from known2.met.
 *
 * A hashset should be purged when no current known-file entry owns that master hash any more,
 * or when the owning known file is old enough to be partially purged.
 */
inline bool ShouldPurgeKnownAICHHashset(bool bHasKnownFileEntry, bool bShouldPartiallyPurgeFile) noexcept
{
	return !bHasKnownFileEntry || bShouldPartiallyPurgeFile;
}

enum class KnownFileCollisionDecision
{
	KeepExisting,
	AdoptIncoming
};

/**
 * @brief Chooses the owner when two known files share the same MD4 hash.
 *
 * Live shared/download records are referenced by other subsystems and must not
 * be destructively replaced by a later known-file insert.
 */
inline KnownFileCollisionDecision ResolveKnownFileCollision(
	bool bExistingIsShared,
	bool bExistingIsDownloading,
	bool bIncomingIsShared,
	bool bIncomingIsDownloading) noexcept
{
	if (bExistingIsShared || bExistingIsDownloading)
		return KnownFileCollisionDecision::KeepExisting;

	if (bIncomingIsShared || bIncomingIsDownloading)
		return KnownFileCollisionDecision::AdoptIncoming;

	return KnownFileCollisionDecision::KeepExisting;
}

/**
 * @brief Reports whether changing the visible eD2K publish state should notify shared-files UI.
 */
inline bool ShouldNotifyPublishedED2KChange(bool bCurrentPublished, bool bRequestedPublished) noexcept
{
	return bCurrentPublished != bRequestedPublished;
}
