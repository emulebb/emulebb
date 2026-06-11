# Rules

- Read `EMULEBB_WORKSPACE_ROOT\repos\emulebb-tooling\docs\WORKSPACE-POLICY.md`
  first; it is authoritative for workspace-wide rules.
- Start from
  `EMULEBB_WORKSPACE_ROOT\repos\emulebb-tooling\docs\reference\AGENT-CHECKLIST.md`
  for the repeatable operating path.

Everything below is this repo's local deltas only:

- This repo is the canonical app source for eMuleBB, the compact app/mod name
  for eMule broadband edition.
- The first post-community commit must remain the global source-encoding
  normalization commit.
- Place changes at the earliest layer where they are true.
- Honor repo `.editorconfig` and `.gitattributes` when editing tracked files.
- For app-source changes, rebuild both `Debug|x64` and `Release|x64` before
  handoff unless the user explicitly narrows validation.
- Do not reintroduce workspace orchestration or dependency policy into this repo
  root.
