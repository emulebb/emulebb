#pragma once

#include "WebServer.h"

/**
 * @brief Runtime web-server entry points for the eMuleBB *arr compatibility bridge.
 */
namespace WebServerArrCompat
{
/**
 * @brief Reports whether the request belongs to the eMuleBB *arr compatibility surface.
 */
bool IsCompatRequest(const ThreadData &rData);

/**
 * @brief Handles one authenticated Torznab-compatible Prowlarr request.
 */
void ProcessRequest(const ThreadData &rData);
}
