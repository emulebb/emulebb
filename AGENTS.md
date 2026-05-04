# Rules

- Read `EMULE_WORKSPACE_ROOT\repos\eMule-tooling\docs\WORKSPACE_POLICY.md`
  before app-source work; it is authoritative for workspace-wide rules.
- This file contains app-source local deltas only. Do not duplicate branch,
  worktree, setup, dependency, or build/test policy here.
- this repo is the canonical app source for eMule BB, the compact app/mod name
  for eMule broadband edition
- the first post-community commit must remain the global source-encoding normalization commit
- always honor repo `.editorconfig` and `.gitattributes` when editing tracked
  files; do not restate line-ending rules here
- place changes at the earliest layer where they are true, then let later milestones inherit them
- keep commits isolated by behavior and avoid mixing baseline, seam, and bugfix work
- for app-source changes, rebuild both `Debug|x64` and `Release|x64` before handoff unless the user explicitly narrows validation
- do not reintroduce workspace orchestration or dependency policy into this repo root
