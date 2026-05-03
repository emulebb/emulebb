#pragma once

#include "WebServer.h"

/**
 * @brief qBittorrent Web API compatibility surface for *arr download clients.
 */
namespace WebServerQBitCompat
{
/**
 * @brief Reports whether the current request target belongs to the qBittorrent
 * compatibility surface.
 */
bool IsCompatRequest(const ThreadData &rData);

/**
 * @brief Handles one qBittorrent-compatible request and writes the response.
 */
void ProcessRequest(const ThreadData &rData);
}
