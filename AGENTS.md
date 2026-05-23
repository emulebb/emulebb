# Rules

- this worktree is the seam-enabled parity and regression baseline; it is not a
  product release or public tag/package target
- the authoritative workspace policy lives in `EMULE_WORKSPACE_ROOT\repos\emulebb-tooling\docs\WORKSPACE-POLICY.md`; follow it over local habit or stale branch names
- baseline maintenance is limited to inert seams, deterministic probes,
  required test tracing, and buildability fixes
- the first post-community commit must remain the global source-encoding normalization commit
- place changes at the earliest layer where they are true
- keep commits isolated by behavior and avoid mixing unrelated baseline work
- do not reintroduce workspace orchestration or dependency policy into this repo root
- workspace-wide rules about branches, worktrees, setup ownership, and dependency pins belong in the central workspace policy document, not here
- do not start new work on `stale/*` branches
