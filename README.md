# eMuleBB

eMuleBB is eMule broadband edition: a native Windows eMule client for long
sessions, large libraries, broadband-era transfer behavior, and trusted local
automation.

The 0.7.3 release line preserves stock-compatible eD2K/Kad behavior and keeps
selected legacy eMule features frozen for compatibility. After 0.7.3 establishes
the first stable eMuleBB baseline, future releases can evolve more aggressively
while keeping protocol compatibility explicit.

## Download And Install

For most users, use the ZIP package:

1. Open <https://github.com/emulebb/emulebb/releases>.
2. Download the intended eMuleBB ZIP. For RC1, use
   `emulebb-0.7.3-rc.1-x64.zip` once it is published.
3. Extract the ZIP into a new folder, for example
   `C:\Apps\eMuleBB\0.7.3-rc.1`.
4. Run `emulebb.exe`.

Keep each version in its own application folder. For release candidates,
nightlies, or support testing, use a backed-up profile or launch with an
explicit disposable profile:

```powershell
emulebb.exe -c "$env:TEMP\eMuleBB-TestProfile"
```

Use the x64 package for ordinary Windows desktop installs. Use ARM64 only for
ARM64 Windows testing.

The full suite PowerShell bootstrapper is the second install path. Use it when
you want the eMuleBB suite installer flow instead of only unpacking and running
the desktop app.

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
