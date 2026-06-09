# DsonToUnreal Agent Guide

`DsonToUnreal` imports DAZ Studio DSON/DSF/DUF Genesis assets into Unreal Engine 5.4.4. This plugin is an editor module named `DsonImporter`. Its purpose and scope are governed by [`Docs/Principles.md`](Docs/Principles.md) — the Roadmap is subordinate to it.

## Operating model (two-agent workflow)

This plugin is worked through two human-mediated roles — a **Director** (coordination
+ docs/config; no source edits) and an **Implementer** (source edits; any LLM agent the
user launches) — with a file-based handoff through `.handoff/`. At session start the
user declares the role; **if unstated, ask** before role-specific work, and confirm it
applies to *this* plugin — this repo sits beside the separate `DsonParser` repo and
roles do **not** carry across repos.

The full role definitions, the shared hard boundaries (build/git ownership; never guess
a missing file — ask the user to upload it), the handoff sequence, and the task-file /
feedback-file templates are owned by **[`Docs/AgentWorkflow.md`](Docs/AgentWorkflow.md)**
— read it before role-specific work.

## Version Control

**This plugin folder is its own git repository.** The surrounding host project
(`D:/Unreal Projects/DsonHost`) is **not** under git — only this `Plugins/DsonToUnreal`
directory is. For any `git status`/`diff`/`log`/`commit` of plugin changes, run git
from here (`Plugins/DsonToUnreal`), not from the host root.

- Repo root: `Plugins/DsonToUnreal` (run `git rev-parse --show-toplevel` here to confirm).
- Remote: `origin` → `https://github.com/vladibalan/DsonToUnreal`.
- Default branch: `main`.

All source edits in this guide's scope live inside this repo, so commits/PRs for them
belong here; the Director commits per the branch-per-task workflow (`Docs/AgentWorkflow.md`).

## Read Order

For discovery, prefer this order:

1. `Docs/ImporterArchitecture.md` (code layout). For **project status** — what
   phase the importer is at, what shipped, what is deferred, and the bug/cleanup
   backlog — read `Docs/Roadmap.md` (the single source of truth that replaces the
   old external handoffs). For the *why* behind a shipped decision (postmortems,
   slice handoff history) read `Docs/DecisionLog.md`; for durable engineering
   facts, hard-won lessons, and recurring gotchas (the coordinate flip, verified
   vert counts, the LIE recipe / `#fragment` diagnostic) read `Docs/Reference.md`.
   Consult those two for the *why* or a hard-won fact — not for current status.
2. For audit, review, debugging, or diagnostic requests: `Docs/AuditGuide.md`
   - For code-review runs (DRY / modern-C++ / compactness) **and before editing
     source**, see "Before editing source" below — it governs authoring, not just review.
   - For the Director/Implementer role definitions, handoff sequence, and the
     Director's prompt template: `Docs/AgentWorkflow.md`.
3. The relevant `.h` file for the component you need.
4. The component's entry in `Docs/ImporterArchitecture.md` ("Component
   Responsibilities") and the purpose-comment at the top of the matching `.h` —
   the `.cpp` files carry no top orientation comment, so don't look for one there.
5. The full `.cpp` only after identifying the task area.

## Before editing source

These rules govern *authoring*, not just review. Before you edit any file under
`Source/DsonImporter/`:

1. **If you have not read [`Docs/CodeReviewRules.md`](Docs/CodeReviewRules.md) this
   session, read it now.** It owns the standing checklist (R1–R11) — the recurring
   hazards that are easy to introduce and expensive to catch on review. **Write
   code that already complies with R1–R11 rather than fixing it after the fact.**
2. **After each edit, self-audit the diff against that doc's Quick Checklist and
   state the result.** Name the rules you checked and confirm the diff satisfies
   them, or flag what doesn't — do not say "looks fine".

This applies even to small or comment-only edits — the parser-ABI (R2),
RAII/string-lifetime (R3), and breaking-change (R7) rules are most often broken by
"minor" tweaks. A `PreToolUse` hook surfaces this checklist on save, but that's a
backstop: the self-audit, and an end-of-change review over the whole diff (catching
cross-file issues like a duplicated helper or an X-macro-list drift), are your job.

## Before editing docs

Editing an orientation doc (`AGENTS.md`, `Docs/*.md`, `MaterialMastersV1.md`) — even
doc-only as the Director — follow **[`Docs/CodeReviewRules.md`](Docs/CodeReviewRules.md)
R10**: keep docs tight and tiered (put content in the tier that owns it), **point
instead of duplicating**, and don't push a hot-path doc past its line budget —
relocate or split instead.

## Task Routing

- Module startup, menu registration, parser DLL loading: `Source/DsonImporter/Private/DsonImporter.cpp`
- Import dialog, validation UI, selected file settings: `Source/DsonImporter/Private/SDsonImportWindow.*`
- DAZ content root discovery and DSON URL resolution: `Source/DsonImporter/Private/DsonContentRoots.*`
- File validation, Genesis generation detection, dependency resolution: `Source/DsonImporter/Private/DsonValidator.*`
- Top-level import orchestration and build sequencing (`Run`), abort-before-build gate: `Source/DsonImporter/Private/DsonImportPipeline.*`
- Skeleton creation from figure DSF nodes: `Source/DsonImporter/Private/DsonSkeletonBuilder.*`
- Skeletal mesh geometry, UVs, material slots: `Source/DsonImporter/Private/DsonMeshBuilder.*`
- Skinning and bone influences: `Source/DsonImporter/Private/DsonSkinWeightsBuilder.*`
- Morph target import: `Source/DsonImporter/Private/DsonMorphBuilder.*`
- Scene material instance creation and shader/channel mapping: `Source/DsonImporter/Private/DsonMaterialBuilder.*`
- Texture import, bump-to-normal baking, package naming, texture cache: `Source/DsonImporter/Private/DsonTextureImporter.*`
- Verbose material channel dumps: `Source/DsonImporter/Private/DsonMaterialDiagnostic.*`
- Parser C API function pointers: `Source/DsonImporter/Private/DsonParserFunctions.h`
- Compile-time parser ABI drift check (no runtime code): `Source/DsonImporter/Private/DsonParserAbiCheck.cpp`
- Requesting a parser feature (new DSON data the parser doesn't expose yet): `Docs/AgentWorkflow.md` ("Requesting parser features")
- Settings/result structs passed between import stages: `Source/DsonImporter/Private/DsonImportTypes.h`
- Parser document-handle RAII (only place `Create`/`Load`/`Destroy` runs; R3): `Source/DsonImporter/Private/DsonLoadedDocument.*`
- Package creation, asset saving, import-folder paths: `Source/DsonImporter/Private/DsonAssetUtils.*`
- Shared URL/id/UTF-8/coordinate-flip leaf helpers (see `Docs/CodeReviewRules.md` R4): `Source/DsonImporter/Private/DsonImportUtils.h`
- Third-party parser import library and DLL packaging: `Source/ThirdParty/DsonParser/`
- Updating the vendored parser bundle (header/version/CHANGELOG/DLL) from a local DsonParser repo: `Tools/Sync-Parser.ps1` (see `Docs/Tooling.md`)
- Master material parameter contract: `MaterialMastersV1.md`
- Project status, phase tracking, deferred features, known issues, cleanup backlog: `Docs/Roadmap.md`
- Why a shipped decision was made; postmortems; slice handoff history: `Docs/DecisionLog.md`
- Durable engineering facts, hard-won lessons, recurring gotchas (coordinate flip, verified counts, LIE recipe / `#fragment`): `Docs/Reference.md`

## Generated Folders

Do not inspect these during normal discovery: `Binaries/`, `Intermediate/`, `.git/`.
For audits/diagnostics they can be evidence (editor logs, crashes, build output) —
see `Docs/AuditGuide.md` (Evidence Sources) for when and what. Never browse
`.handoff/` either (Director↔Implementer task/feedback scratch, gitignored): read
only the one `task-<id>.md` you are explicitly handed — see
[`Docs/AgentWorkflow.md`](Docs/AgentWorkflow.md). Never read `Docs/_OutOfScope/` either
(out-of-scope intent, agnostic to the Importer — open it only when the user directs you there).

## Build & tooling

**Build & verify** the plugin (compile the host `DsonHostEditor` target — close the
UE Editor first) and clangd compile-database regeneration both live in
**[`Docs/Tooling.md`](Docs/Tooling.md)**; read it before building or regenerating.

## Code Notes

- `GDsonParser` is populated at module startup by loading exports from `DsonParser.dll`.
- Builder classes are mostly static orchestration helpers except `FDsonMaterialBuilder` and `FDsonTextureImporter`, which carry caches/counters.
- The end-to-end import flow, DSON-URL handling, and per-component responsibilities live in `Docs/ImporterArchitecture.md` (Runtime Flow + Component Responsibilities) — not restated here. Audit/diagnostic routing: `Docs/AuditGuide.md` (Read Order step 2).
