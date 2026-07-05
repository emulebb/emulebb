# eMuleBB

eMuleBB is eMule broadband edition: a native Windows eMule client for long
sessions, large libraries, broadband-era transfer behavior, and trusted local
automation.

The 0.7.3 release line preserves stock-compatible eD2K/Kad behavior and keeps
selected legacy eMule features frozen for compatibility. After 0.7.3 establishes
the first stable eMuleBB baseline, future releases can evolve more aggressively
while keeping protocol compatibility explicit.

## Download And Install

Stable `0.7.3` is published on GitHub Releases. Choose one install path:

### Option 1: Manual Standalone ZIP

Use this path when you only want the eMuleBB desktop app.

1. Open
   <https://github.com/emulebb/emulebb/releases/tag/emulebb-v0.7.3>.
2. Download `emulebb-0.7.3-x64.zip`.
3. Extract the ZIP into a new version-specific folder, for example
   `eMuleBB-0.7.3`.
4. Run `emulebb.exe`.

Keep each version in its own application folder. For first launch or support
testing, use a backed-up profile or launch with an explicit disposable profile:

```powershell
emulebb.exe -c "$env:TEMP\eMuleBB-TestProfile"
```

Use the x64 package for ordinary Windows desktop installs. Use ARM64 only for
ARM64 Windows testing.

### Option 2: Full Suite PowerShell One-Liner

Use this path when you want eMuleBB plus aMuTorrent, Prowlarr, Radarr, and
Sonarr integration out of the box.

```powershell
irm https://github.com/emulebb/emulebb/releases/download/emulebb-v0.7.3/Bootstrap-eMuleBBSuite.ps1 | iex
```

The bootstrapper downloads and verifies the matching eMuleBB release package,
resolves the aMuTorrent controller package from `emulebb/amutorrent/releases`,
extracts the suite installer, and starts the install flow. Full installs also
download pinned public Node, Prowlarr, Radarr, and Sonarr payloads by default.

### Security And Provenance

Stable release builds and packaging happen in GitHub Actions and are published
through GitHub Releases. The `0.7.3` release includes ZIPs, manifests, SHA-256
evidence, SPDX SBOMs, diagnostics packages, the suite bootstrapper, and the
bootstrapper SHA-256 asset. The bootstrapper verifies package hashes from the
release manifests before installing.

## Documentation

- Setup guide:
  <https://emulebb.github.io/emulebb-tooling/reference/GUIDE-SETUP/>
- Product guide:
  <https://emulebb.github.io/emulebb-tooling/reference/GUIDE-EMULEBB/>
- FAQ:
  <https://emulebb.github.io/faq/>
- Release notes:
  <https://emulebb.github.io/emulebb-tooling/active/RELEASE-0.7.3-NOTES/>
- Full documentation:
  <https://emulebb.github.io/emulebb-tooling/>

## Current Branches

- `main`: maintained eMuleBB integration line and 0.7.3 release source after
  reviewed release proof passes
- `baseline/community-0.72a`: seam-enabled parity and regression baseline,
  test-only
- `tracing-harness/community-0.72a`: deterministic parity harness derived from
  the community baseline

Accepted release tags are cut from reviewed commits on `main`.

## Contributors

App-source changes are made in the canonical eMuleBB workspace, not from an
ad hoc local checkout. Start with the workspace policy and developer docs:

- Workspace policy:
  <https://emulebb.github.io/emulebb-tooling/WORKSPACE-POLICY/>
- Development guide:
  <https://emulebb.github.io/emulebb-tooling/reference/DEVELOPMENT-GUIDE/>
- Agent checklist:
  <https://emulebb.github.io/emulebb-tooling/reference/AGENT-CHECKLIST/>

Build, validation, test, and packaging orchestration lives in the paired
`emulebb-build` repository.
