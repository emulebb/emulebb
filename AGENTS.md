# Rules

- this repo is the canonical app source for eMule BB, the compact app/mod name
  for eMule broadband edition
- the authoritative workspace policy lives in `EMULE_WORKSPACE_ROOT\repos\eMule-tooling\docs\WORKSPACE_POLICY.md`; follow it over local habit or stale branch names
- `community/v0.72a` is the imported baseline and `main` is the maintained eMule BB integration branch
- the first post-community commit must remain the global source-encoding normalization commit
- always honor repo `.editorconfig` and `.gitattributes` when editing tracked
  files; do not restate line-ending rules here
- place changes at the earliest layer where they are true, then let later milestones inherit them
- keep commits isolated by behavior and avoid mixing baseline, seam, and bugfix work
- for app-source changes, rebuild both `Debug|x64` and `Release|x64` before handoff unless the user explicitly narrows validation
- do not reintroduce workspace orchestration or dependency policy into this repo root
- workspace-wide rules about branches, worktrees, setup ownership, and dependency pins belong in the central workspace policy document, not here
- do not start new work on `stale/*` branches
