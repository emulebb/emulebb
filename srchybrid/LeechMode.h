#pragma once

// Leech mode is a monitoring-only build flavor (the feature/leech-mode branch).
// Its purpose is to observe eD2K/Kad network behavior without contributing
// anything back to the network. When active the client:
//   * advertises a stock, non-mod identity (no CT_MOD_VERSION mod string),
//   * forces the stock nickname (the project homepage URL) and ignores any
//     user-configured nick,
//   * refuses every form of sharing: it grants no upload slots and sends no
//     file data, it never offers its files to the server (OP_OFFERFILES), it
//     never publishes keywords/sources/notes to Kad, and it answers every
//     browse-shared / shared-directory request as denied/empty.
//
// WHY constexpr (and not a runtime preference): leech behavior must be
// unconditional and impossible to switch off at runtime, so the compiler folds
// every identity/sharing gate against this constant. Flip the return value to
// false (or guard it behind a build macro) to turn the branch back into a
// normal, contributing client.
inline constexpr bool IsLeechModeActive()
{
	return true;
}
