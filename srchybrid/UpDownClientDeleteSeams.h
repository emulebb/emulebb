#pragma once

#include <tchar.h>

class CUpDownClient;
class CPartFile;

namespace UpDownClientDeleteSeams
{
/**
 * @brief Debug-checks that normal external client owners were detached before explicit deletion.
 *
 * The client list remains the canonical lifetime root and is intentionally not
 * checked here because CUpDownClient::~CUpDownClient removes the object from it.
 */
void AssertReadyToDelete(const CUpDownClient *pClient, const TCHAR *pszContext);

/**
 * @brief Calls CUpDownClient::TryToConnect and owns the false/delete contract.
 *
 * TryToConnect returns false only when the client has already been detached
 * enough for explicit deletion. Fire-and-forget callers use this wrapper so a
 * rejected immediate connect cannot leave a dead client pointer in owner lists.
 */
bool TryToConnectOrDelete(CUpDownClient *pClient, const TCHAR *pszContext, bool bIgnoreMaxCon = false, bool bNoCallbacks = false);

/**
 * @brief Debug-checks temporary source reject paths before the source is published.
 *
 * Server and source-exchange probes are constructed with an initial request
 * file. Rejecting them before they enter queue ownership should not require
 * clearing that constructor-time pointer first.
 */
void AssertTemporarySourceReadyToDelete(const CUpDownClient *pClient, const CPartFile *pInitialRequestFile, const TCHAR *pszContext);
}
