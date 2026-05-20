#pragma once

#include <atlstr.h>

#include "types.h"
#include "ClientStateDefs.h"
#include "Version.h"

enum FriendLinkTransitionAction
{
	friendLinkTransitionNone,
	friendLinkTransitionUserhashFailed,
	friendLinkTransitionUnlink
};

struct FriendLinkSnapshot
{
	bool bHasFriend;
	bool bHasUserhash;
	bool bTryingToConnect;
	bool bHashMatchesClient;
	bool bEndpointMatchesClient;
};

/**
 * @brief Classifies how the current linked friend should react to the latest user-hash snapshot.
 */
inline FriendLinkTransitionAction ClassifyFriendLinkTransition(const FriendLinkSnapshot &rSnapshot)
{
	if (!rSnapshot.bHasFriend || !rSnapshot.bHasUserhash || rSnapshot.bHashMatchesClient)
		return friendLinkTransitionNone;

	return rSnapshot.bTryingToConnect ? friendLinkTransitionUserhashFailed : friendLinkTransitionUnlink;
}

/**
 * @brief Reports whether the current friend snapshot requires a fresh search in the friend list.
 */
inline bool ShouldSearchReplacementFriend(const FriendLinkSnapshot &rSnapshot)
{
	return !rSnapshot.bHasFriend || rSnapshot.bHasUserhash || !rSnapshot.bEndpointMatchesClient;
}

struct BuddyHelloSnapshot
{
	bool bShouldAdvertise;
	uint32 dwBuddyIP;
	uint16 nBuddyPort;
};

/**
 * @brief Returns the peer-visible mod identity advertised in eMule hello tags.
 */
inline CString GetAdvertisedClientModIdentity()
{
	return MOD_CLIENT_MOD_VERSION_TEXT;
}

/**
 * @brief Formats a client software label with its optional mod identity.
 */
inline CString BuildFullClientSoftVersionDisplay(const CString &strClientSoftware, const CString &strClientModVersion)
{
	if (strClientModVersion.IsEmpty())
		return strClientSoftware;

	CString strDisplay;
	strDisplay.Format(_T("%s [%s]"), (LPCTSTR)strClientSoftware, (LPCTSTR)strClientModVersion);
	return strDisplay;
}

/**
 * @brief Formats a client software label without a version suffix.
 */
inline CString BuildClientSoftwareNameDisplay(LPCTSTR pszSoftware)
{
	return CString(pszSoftware);
}

/**
 * @brief Formats legacy eMule-compatible version bytes as a v0.minor client label.
 */
inline CString BuildClientSoftwareMinorVersionDisplay(LPCTSTR pszSoftware, UINT nClientMinVersion)
{
	CString strDisplay;
	strDisplay.Format(_T("%s v0.%u"), pszSoftware, nClientMinVersion);
	return strDisplay;
}

/**
 * @brief Formats packed eMule-protocol client version fields using stock display rules.
 */
inline CString BuildClientSoftwareStructuredVersionDisplay(EClientSoftware clientSoft, LPCTSTR pszSoftware, UINT nClientMajVersion, UINT nClientMinVersion, UINT nClientUpVersion)
{
	CString strDisplay;
	if (clientSoft == SO_EMULE)
		strDisplay.Format(_T("%s v%u.%u%c"), pszSoftware, nClientMajVersion, nClientMinVersion, _T('a') + nClientUpVersion);
	else if (clientSoft == SO_AMULE || nClientUpVersion != 0)
		strDisplay.Format(_T("%s v%u.%u.%u"), pszSoftware, nClientMajVersion, nClientMinVersion, nClientUpVersion);
	else if (clientSoft == SO_LPHANT)
		strDisplay.Format(_T("%s v%u.%02u"), pszSoftware, nClientMajVersion - 1, nClientMinVersion);
	else
		strDisplay.Format(_T("%s v%u.%u"), pszSoftware, nClientMajVersion, nClientMinVersion);
	return strDisplay;
}

/**
 * @brief Formats a decoded legacy eDonkeyHybrid version tuple.
 */
inline CString BuildDonkeyHybridClientSoftwareVersionDisplay(UINT nClientMajVersion, UINT nClientMinVersion, UINT nClientUpVersion)
{
	CString strDisplay;
	if (nClientUpVersion != 0)
		strDisplay.Format(_T("eDonkeyHybrid v%u.%u.%u"), nClientMajVersion, nClientMinVersion, nClientUpVersion);
	else
		strDisplay.Format(_T("eDonkeyHybrid v%u.%u"), nClientMajVersion, nClientMinVersion);
	return strDisplay;
}

/**
 * @brief Builds the one-shot buddy payload snapshot used while serializing hello tags.
 */
inline BuddyHelloSnapshot BuildBuddyHelloSnapshot(bool bIsFirewalled, bool bHasBuddy, uint32 dwBuddyIP, uint16 nBuddyPort)
{
	BuddyHelloSnapshot snapshot = {};
	snapshot.bShouldAdvertise = bIsFirewalled && bHasBuddy;
	snapshot.dwBuddyIP = dwBuddyIP;
	snapshot.nBuddyPort = nBuddyPort;
	return snapshot;
}

/**
 * @brief Computes the hello tag count once the buddy advertisement decision is known.
 */
inline uint32 GetHelloTagCount(const BuddyHelloSnapshot &rBuddySnapshot)
{
	return 7u + (rBuddySnapshot.bShouldAdvertise ? 2u : 0u);
}
