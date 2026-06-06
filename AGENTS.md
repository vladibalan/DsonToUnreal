# DsonToUnreal Agent Guide

`DsonToUnreal` imports DAZ Studio DSON/DSF/DUF Genesis assets into Unreal Engine 5.4.4. This plugin is an editor module named `DsonImporter`.

## Operating model (two-agent workflow)

This plugin is worked through two roles: a **Director** (takes your instructions,
reads files, writes docs/instruction/config files, and authors prompts for the
Implementer) and an **Implementer** (executes those prompts and edits source per
the code-review rules). The user passes prompts between the two by hand. At
session start the user states which role this session plays; if unstated, ask.

**Both roles:** the user handles binary builds and git commits/pushes — never
assume a build ran and never commit. If a needed file is missing from the
project folder, ask the user to upload it rather than guessing its contents.

> This plugin sits inside the `DsonHost` project next to the separate `DsonParser`
> repo, which runs the same workflow. Roles and boundaries do **not** carry across
> repos — when a session opens from the host root, confirm the role applies to
> *this* plugin before doing role-specific work.

**Read [`Docs/AgentWorkflow.md`](Docs/AgentWorkflow.md)** for the full role
definitions, handoff sequence, and the Director's prompt template.

## Version Control

**This plugin folder is its own git repository.** The surrounding host project
(`D:/Unreal Projects/DsonHost`) is **not** under git — only this `Plugins/DsonToUnreal`
directory is. For any `git status`/`diff`/`log`/`commit` of plugin changes, run git
from here (`Plugins/DsonToUnreal`), not from the host root.

- Repo root: `Plugins/DsonToUnreal` (run `git rev-parse --show-toplevel` here to confirm).
- Remote: `origin` → `https://github.com/vladibalan/DsonToUnreal`.
- Default branch: `main`.

All source edits in this guide's scope live inside this repo, so commits/PRs for them
belong here.

## Read Order

For discovery, prefer this order:

1. `Docs/ImporterArchitecture.md` (code layout). For **project status** — what
   phase the importer is at, what shipped, what is deferred, and the bug/cleanup
   backlog — read `Docs/Roadmap.md` (the single source of truth that replaces the
   old external handoffs).
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
   session, read it now.** It is the standing checklist (R1–R9) covering the
   hazards that are easy to introduce and expensive to catch later: UE 5.4.4 API
   compatibility (R1), the single-source-of-truth parser ABI in
   `DsonParserFunctions.h` (R2), RAII handles + immediate parser-string copy (R3),
   the shared DRY helpers and the correctness-critical coordinate flip (R4),
   compactness without losing functionality (R5), the C++20 idiom (R6), the
   permissive-parser / return-value / breaking-change contracts (R7), keeping
   the agent-orientation docs in sync with the change (R8), and keeping
   `Docs/Roadmap.md` current as phases complete, bugs are fixed/found, or work is
   deferred (R9). **Write code that already complies with R1–R9 rather than
   fixing it on review.**
2. **After each edit, self-audit the diff against that doc's Quick Checklist and
   state the result.** Name the rules you checked and confirm the diff satisfies
   them, or flag what doesn't — do not say "looks fine".

This applies even to small or comment-only edits; the parser-ABI (R2),
RAII/string-lifetime (R3), and breaking-change (R7) rules are most often violated
by "minor" tweaks. (A `PreToolUse` hook in `.claude/settings.json` also surfaces
this checklist on each plugin-source edit, but treat that as a backstop — the
self-audit is your responsibility, and only an end-of-change review over the whole
diff catches cross-file issues like a duplicated helper or an export drifting from
the X-macro list.)

## Task Routing

- Module startup, menu registration, parser DLL loading: `Source/DsonImporter/Private/DsonImporter.cpp`
- Import dialog, validation UI, selected file settings: `Source/DsonImporter/Private/SDsonImportWindow.*`
- DAZ content root discovery and DSON URL resolution: `Source/DsonImporter/Private/DsonContentRoots.*`
- File validation, Genesis generation detection, dependency resolution: `Source/DsonImporter/Private/DsonValidator.*`
- Skeleton creation from figure DSF nodes: `Source/DsonImporter/Private/DsonSkeletonBuilder.*`
- Skeletal mesh geometry, UVs, material slots: `Source/DsonImporter/Private/DsonMeshBuilder.*`
- Skinning and bone influences: `Source/DsonImporter/Private/DsonSkinWeightsBuilder.*`
- Morph target import: `Source/DsonImporter/Private/DsonMorphBuilder.*`
- Scene material instance creation and shader/channel mapping: `Source/DsonImporter/Private/DsonMaterialBuilder.*`
- Texture import, package naming, texture cache: `Source/DsonImporter/Private/DsonTextureImporter.*`
- Verbose material channel dumps: `Source/DsonImporter/Private/DsonMaterialDiagnostic.*`
- Parser C API function pointers: `Source/DsonImporter/Private/DsonParserFunctions.h`
- Compile-time parser ABI drift check (no runtime code): `Source/DsonImporter/Private/DsonParserAbiCheck.cpp`
- Third-party parser import library and DLL packaging: `Source/ThirdParty/DsonParser/`
- Master material parameter contract: `MaterialMastersV1.md`
- Project status, phase tracking, deferred features, known issues, cleanup backlog: `Docs/Roadmap.md`

## Generated Folders

Do not inspect these during normal discovery:

- `Binaries/`
- `Intermediate/`
- `.git/`

For audits and diagnostics, inspect generated folders only when they are evidence:

- `Saved/Logs/` for current and backup editor logs.
- `Saved/Crashes/` for crash logs and crash context.
- `Intermediate/` only for generated code or build-output questions.
- `Binaries/` only for packaging, stale binary, or DLL-load questions.

## Editor Tooling — clangd index (read before regenerating it)

A clangd compile database already exists for this plugin. If you need to
(re)generate it, **do not reach for UBT's `Build.bat -mode=GenerateClangDatabase`** —
that mode aborts immediately with "Clang x64 must be installed" because this
machine has **no LLVM/Clang toolchain installed** (only `clang-format`/`clang-tidy`
ship with VS; there is no `clang-cl.exe`/`clang.exe`). clangd itself needs no such
install — it has its own frontend — so the toolchain is not worth installing.

The working, no-install approach (already set up):

- Generator: `tools/gen-compile-commands.ps1` at the **host project root**
  (`<project>/`, one level **above** this plugin repo — it is not under this git
  repo, same as the `.clangd` and `compile_commands.json` it produces).
- It **harvests** the per-TU response files UBT wrote on the last build
  (`Plugins/DsonToUnreal/Intermediate/Build/Win64/x64/*/Development/DsonImporter/*.cpp.obj.rsp`),
  inlines the shared `/I` list, and strips the MSVC-only flags clangd can't use
  (`/Yu /Fp /Fo /sourceDependencies`). No compiler runs.
- Prerequisite: the **editor target must have been built once** so those `.rsp`
  files exist. Regenerate after adding/removing `.cpp` files, changing module
  dependencies, or rebuilding with new flags:
  `pwsh -File tools/gen-compile-commands.ps1` (from the host project root).
- If clangd warns on a stray MSVC flag, add it to the script's drop-list — do
  not switch to installing LLVM.

## Code Notes

- `GDsonParser` is populated at module startup by loading exports from `DsonParser.dll`.
- Builder classes are mostly static orchestration helpers except `FDsonMaterialBuilder` and `FDsonTextureImporter`, which carry caches/counters.
- DSON URLs are usually relative to DAZ content roots and may include URL encoding and fragments.
- The import flow starts from the File menu, opens `SDsonImportWindow`, validates the chosen `.duf`/`.dsf`, then builds skeleton, materials/textures, mesh, and skin weights.

## Audit Notes

- Start audit/diagnostic requests with `Docs/AuditGuide.md`.
- Route by symptom before opening source files.
- Collect log evidence first when the user reports a crash, import failure, missing asset, or wrong output.
- For code-review audits, report findings first with file/line references.
- If no issue is found, state the evidence checked and remaining risk.
