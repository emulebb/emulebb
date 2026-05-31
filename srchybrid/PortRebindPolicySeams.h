#pragma once

namespace PortRebindPolicySeams
{
/**
 * Runtime TCP/UDP listener rebinds are intentionally disabled. Port changes
 * are persisted by preferences and become active on the next eMule restart.
 */
inline bool CanApplyRuntimePortRebind()
{
	return false;
}
}
