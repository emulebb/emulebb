# eMuleBB

This repo is the canonical app source for eMule broadband edition, compactly
branded as eMuleBB. Public product releases use the independent eMuleBB
version line while the workspace keeps explicit community `0.72a` baselines for
parity testing.

## Refs

- `main`: maintained eMuleBB integration line and beta `0.7.3` release
  source after reviewed release proof passes
- `baseline/community-0.72a`: seam-enabled parity and regression baseline,
  test-only
- `tracing-harness/community-0.72a`: variant-client parity harness
- accepted release refs are cut from reviewed commits on `main`

## Working Model

- the first post-community commit is full source encoding normalization
- active work lands on `main` unless the workspace policy names an exception
- release stabilization is tracked through the workspace release policy

## Build And Test

Builds are driven from the canonical workspace materialized under
`EMULE_WORKSPACE_ROOT`, using the paired `emulebb-setup` and `emulebb-build`
repos rather than a machine-local cleanroom path.
