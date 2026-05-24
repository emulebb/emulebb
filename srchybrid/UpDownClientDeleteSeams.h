#pragma once

#include <tchar.h>

class CUpDownClient;

namespace UpDownClientDeleteSeams
{
/**
 * @brief Debug-checks that normal external client owners were detached before explicit deletion.
 *
 * The client list remains the canonical lifetime root and is intentionally not
 * checked here because CUpDownClient::~CUpDownClient removes the object from it.
 */
void AssertReadyToDelete(const CUpDownClient *pClient, const TCHAR *pszContext);
}
