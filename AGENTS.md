# DsonToUnreal Agent Guide

`DsonToUnreal` imports DAZ Studio DSON/DSF/DUF Genesis assets into Unreal Engine 5.4.4. This plugin is an editor module named `DsonImporter`.

## Read Order

For discovery, prefer this order:

1. `Docs/ImporterArchitecture.md`
2. For audit, review, debugging, or diagnostic requests: `Docs/AuditGuide.md`
3. The relevant `.h` file for the component you need.
4. The top comment and section headings in the matching `.cpp`.
5. The full `.cpp` only after identifying the task area.

## Task Routing

- Module startup, menu registration, parser DLL loading: `Source/DsonImporter/Private/DsonImporter.cpp`
- Import dialog, validation UI, selected file settings: `Source/DsonImporter/Private/SDsonImportWindow.*`
- DAZ content root discovery and DSON URL resolution: `Source/DsonImporter/Private/DsonContentRoots.*`
- File validation, Genesis generation detection, dependency resolution: `Source/DsonImporter/Private/DsonValidator.*`
- Skeleton creation from figure DSF nodes: `Source/DsonImporter/Private/DsonSkeletonBuilder.*`
- Skeletal mesh geometry, UVs, material slots: `Source/DsonImporter/Private/DsonMeshBuilder.*`
- Skinning and bone influences: `Source/DsonImporter/Private/DsonSkinWeightsBuilder.*`
- Scene material instance creation and shader/channel mapping: `Source/DsonImporter/Private/DsonMaterialBuilder.*`
- Texture import, package naming, texture cache: `Source/DsonImporter/Private/DsonTextureImporter.*`
- Verbose material channel dumps: `Source/DsonImporter/Private/DsonMaterialDiagnostic.*`
- Parser C API function pointers: `Source/DsonImporter/Private/DsonParserFunctions.h`
- Third-party parser import library and DLL packaging: `Source/ThirdParty/DsonParser/`
- Master material parameter contract: `MaterialMastersV1.md`

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
