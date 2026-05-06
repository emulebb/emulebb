#pragma once

#include <string>

#include "WebServer.h"

#pragma warning(push, 0)
#include <nlohmann/json.hpp>
#pragma warning(pop)

namespace WebServerJson
{
/**
 * @brief Converts a narrow CString into a std::string without re-encoding.
 */
std::string ToStdString(const CStringA &rText);

/**
 * @brief Converts CString text to UTF-8 for REST and compatibility payloads.
 */
std::string ToStdUtf8(const CString &rText);

/**
 * @brief Converts UTF-8 REST or compatibility text into a Unicode CString.
 */
CString FromStdUtf8(const std::string &rText);

/**
 * @brief Serializes a JSON payload as UTF-8 using the native REST error policy.
 */
CStringA SerializeJsonUtf8(const nlohmann::json &rPayload);

/**
 * @brief Reports whether the current request target belongs to the REST surface.
 */
bool IsApiRequest(const ThreadData &rData);

/**
 * @brief Handles one authenticated `/api/v1/...` request and writes the JSON response.
 */
void ProcessRequest(const ThreadData &rData);

/**
 * @brief Executes one already-authenticated in-process REST command for a
 * compatibility surface that needs to reuse the native UI command pipeline.
 */
bool ExecuteInternalCommand(const nlohmann::json &rRequest, nlohmann::json &rResult, CStringA &rErrorCode, CString &rErrorMessage);

/**
 * @brief Executes one synchronous REST command dispatch context on the UI
 * thread.
 */
void RunDispatchedCommand(void *pContext);
}
