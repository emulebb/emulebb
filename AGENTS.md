# Rules

- this worktree is the variant-client parity harness derived from the community
  parity/regression baseline
- the authoritative workspace policy lives in `EMULE_WORKSPACE_ROOT\repos\eMule-tooling\docs\WORKSPACE_POLICY.md`; follow it over local habit or stale branch names
- harness behavior may intentionally change runtime decisions only when needed
  for explicit parity testing
- the first post-community commit must remain the global source-encoding normalization commit
- place changes at the earliest layer where they are true
- keep commits isolated by behavior and avoid mixing unrelated harness work
- do not reintroduce workspace orchestration or dependency policy into this repo root
- workspace-wide rules about branches, worktrees, setup ownership, and dependency pins belong in the central workspace policy document, not here
- do not start new work on `stale/*` branches
