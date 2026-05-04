# eMule BB

This repo is the canonical app source for eMule broadband edition, compactly
branded as eMule BB. The workspace still carries `v0.72a` path names for
historical lineage and comparison baselines, but public product releases use
the independent eMule BB version line.

## Refs

- `community/v0.72a`: imported community baseline
- `main`: maintained eMule BB integration line
- accepted release refs are cut from reviewed commits on `main`

## Working Model

- the first post-community commit is full source encoding normalization
- active work lands on `main` unless the workspace policy names an exception
- release stabilization is tracked through the workspace release policy

## Build And Test

Builds are driven from the canonical workspace materialized under
`EMULE_WORKSPACE_ROOT`, using the paired `eMulebb-setup` and `eMule-build`
repos rather than a machine-local cleanroom path.
