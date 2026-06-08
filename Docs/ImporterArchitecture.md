# DsonToUnreal Importer Architecture

This document is for fast agent discovery. It explains where to look before opening full implementation files.

This document describes *code layout*. For **project status** — phase completion,
deferred-to-v2 features, known issues, and the cleanup backlog — see
`Docs/Roadmap.md`.

For audit, review, debugging, or diagnostic requests, read `Docs/AuditGuide.md` after this overview.

## Shape

The plugin is an Unreal Editor module:

- Module: `DsonImporter`
- Plugin descriptor: `DsonToUnreal.uplugin`
- Public module entry point: `Source/DsonImporter/Public/DsonImporter.h`
- Implementation code: `Source/DsonImporter/Private/`
- Bundled parser API: `Source/ThirdParty/DsonParser/`
- Master material assets: `Content/Materials/`

## Runtime Flow

1. `FDsonImporterModule::StartupModule` loads `DsonParser.dll`, resolves parser exports into `GDsonParser`, and registers the File menu entry.
2. The menu entry opens `SDsonImportWindow`.
3. `SDsonImportWindow` detects DAZ content roots, lets the user choose a `.duf` or `.dsf`, and calls `FDsonValidator`.
4. `FDsonValidator` parses basic metadata and resolves figure/material/geometry dependencies.
5. Import confirmation produces `FDsonImportSettings`.
6. `FDsonImportPipeline::Run(Settings, ContentRoots)` orchestrates asset creation and returns `FDsonImportResult` (skeleton, mesh, abort flag), in this order:
   - `FDsonMaterialBuilder` builds material instances from scene material channels, driving image import through `FDsonTextureImporter`. It also creates one per-character `USubsurfaceProfile` for skin MICs (Subsurface Profile masters) and imports makeup base textures and non-base LIE layers as standalone `UTexture2D` assets without MIC binding; composition is deferred to authoring tools.
   - Optional `FDsonMaterialDiagnostic` channel dump, then the `M_DazDefault` master loads — a missing master aborts before any asset is built.
   - `FDsonSkeletonBuilder` creates a `USkeleton` from figure DSF nodes.
   - `FDsonMeshBuilder` creates the skeletal mesh, UVs, polygon groups, and material slots, invoking `FDsonSkinWeightsBuilder` (DSF skin influences) and `FDsonMorphBuilder` (MeshDescription morph attributes) before mesh commit; `BuildSkeletalMesh` generates the `UMorphTarget`s.

## Component Responsibilities

`DsonImporter.cpp`

- Owns module startup/shutdown.
- Loads the third-party parser DLL.
- Populates `GDsonParser`.
- Verifies the loaded DLL's reported version against the built-against header (`DsonParser_GetVersion` vs `DSONPARSER_VERSION_*`); refuses to register on a MAJOR mismatch.
- Adds the File menu entry that launches the import dialog.

`SDsonImportWindow.*`

- Slate modal dialog for import.
- Stores the selected file and pending settings.
- Runs validation and gates the Import action.

`DsonContentRoots.*`

- Reads DAZ Studio content directories from Windows Registry.
- Resolves DSON URL paths against content roots.
- URL-decodes DSON paths.

`DsonValidator.*`

- Loads the selected DSON document through the parser.
- Determines asset type and Genesis generation.
- Resolves dependencies needed by the import.
- Discovers G9 companion figures from scene.extra PostLoadAddons (`DiscoverCompanionFigures`): loads each addon's loader .duf (RAII), extracts geometry DSF URL + node id into `FDsonCompanionSource`, logs results.

`DsonSkeletonBuilder.*`

- Reads figure nodes from the resolved base figure DSF.
- Converts node transforms into UE reference skeleton bones.
- Saves the skeleton asset.
- `MergeCompanionBonesIntoSkeleton`: merges companion-exclusive bones into the body
  skeleton (e.g. tongue01–05 under `lowerteeth`) and re-saves; called by `BuildCompanion`.

`DsonMeshBuilder.*`

- Loads geometry DSF and optional UV-set DSF.
- Converts vertices, faces, UVs, polygon groups, and material slots into a `USkeletalMesh`.
- Calls `FDsonSkinWeightsBuilder` before committing mesh data.
- `BuildCompanion`: builds a companion geometry DSF as a separate `USkeletalMesh` bound to the body `USkeleton` by bone name; wires sections to `MaterialsByGroup` MICs with `DefaultMaterial` fallback (Slice B+C).

`DsonSkinWeightsBuilder.*`

- Finds the skin binding modifier.
- Maps DSF joint/node names to UE skeleton bone indices.
- Writes capped/normalized influences to mesh description skin-weight attributes.

`DsonMorphBuilder.*`

- Reads morph modifiers from the base figure DSF and external morph DSFs referenced
  directly by the scene or transitively through `?value` formula outputs.
- Registers morph targets into the `MeshDescription` before `CommitMeshDescription`.
- Converts DAZ position deltas through `DazPointToUe`; morph normals are recomputed by the engine.

`DsonMaterialBuilder.*`

- Detects DAZ shader kind from scene material metadata.
- Maps DAZ material channels onto Unreal material instance parameters.
- Creates one per-character `USubsurfaceProfile`, tints it from the character's
  skin color, and assigns it to skin MICs while gating known non-skin groups off.
- Imports unmapped makeup base images and non-base LIE layer images as standalone
  textures under the normal texture-import convention, without MIC parameter binding.
- For IrayUber, bakes bump maps into the normal input and leaves the master's
  `BumpStrength`/`BumpMap`/`UseBumpMap` parameters unset.
- Imports textures through `FDsonTextureImporter`.
- Outputs material instances keyed by material group name.

`DsonTextureImporter.*`

- Resolves image URLs to disk.
- Imports or reuses `UTexture2D` assets.
- Bakes IrayUber bump height maps into tangent-space normal textures and combines
  them with the surface normal map when present.
- Sets sRGB according to material channel needs.
- Caches ordinary imports by resolved absolute path, sRGB mode, and optional
  asset-name suffix so color and linear variants can coexist.

`DsonMaterialDiagnostic.*`

- Debug-only material channel dump used for planning and verification.

`DsonParserFunctions.h`

- Function pointer typedefs and `FDsonParserAPI`; includes the optional `DsonParser_GetVersion` accessor used by `DsonImporter.cpp`'s startup ABI-compatibility check.
- Keep this synchronized with exports provided by the bundled parser DLL, including morph-target accessors and scene-modifier URLs used to discover external morph files.
  Formula-output accessors are optional and extend morph file discovery only; they
  are not used to evaluate or compose formula-driven dial values.
- Optional scene material channel layer accessors expose LIE layer count, texture
  path, and label; the importer uses them only for standalone non-base layer
  texture import.
- Optional PostLoadAddon accessors (`GetScenePostLoadAddon{Count,Slot,AssetName,AssetFile,MatPreset}`) expose G9 companion-figure declarations from scene.extra; used by `DsonValidator.*` companion discovery.

`DsonParserAbiCheck.cpp`

- Build-time tripwire only: emits one `static_assert` per `DSON_PARSER_API_LIST`
  row asserting the bound signature is ABI-compatible with the vendored
  `DsonParserAPI.h` prototype. No runtime code, no linkage. Compiles to nothing
  if the vendored header is absent (`__has_include` guard).

`DsonImportPipeline.*`

- Top-level import orchestrator: `FDsonImportPipeline::Run` sequences material/texture
  build, the diagnostic dump and `M_DazDefault` gate, then skeleton, body mesh, and companion
  meshes (via `FDsonMeshBuilder::BuildCompanion`; permissive — failures skip, don't abort).

`DsonImportTypes.h`

- Inter-stage types: `EGenesisGeneration` (moved here from `DsonValidator.h` to break circular include), `FDsonCompanionSource` (resolved companion-figure record, Slice A+), `FDsonImportSettings`, `FDsonImportResult` (carries `Skeleton`, `Mesh`, and `CompanionMeshes` TArray from Slice B+).

`DsonLoadedDocument.*`

- `FDsonLoadedDocument`: RAII owner of one parser document handle and the only place
  parser `Create`/`Load`/`Destroy` runs (R3); optional `OutError` surfaces failure text.

`DsonAssetUtils.*`

- `FDsonAssetUtils`: package creation, asset saving, and import-folder/subfolder path
  construction under the `/Game/DazImports` root.

`DsonImportUtils.h`

- Header-only shared leaf helpers: URL scheme/fragment stripping, UTF-8→`FString`, DAZ id
  normalization, unit-scale, and the load-bearing coordinate flip; list/rules: `CodeReviewRules.md` R3/R4.

## Common Change Areas

- Parser export missing or new parser function: update `DsonParserFunctions.h` and `DsonImporter.cpp`.
- Import dialog behavior: edit `SDsonImportWindow.*`.
- Path/dependency failures: start in `DsonContentRoots.*`, then `DsonValidator.*`.
- G9 companion-figure discovery or resolution failures: `DsonValidator.*` (`DiscoverCompanionFigures`).
- G9 companion mesh or material failures: `DsonImportPipeline.*` (companion loop) → `DsonMeshBuilder.*` (`BuildCompanion`) or `DsonMaterialBuilder.*` (`BuildAllSceneMaterials` on MAT preset).
- Bad bone hierarchy or transforms: start in `DsonSkeletonBuilder.*`.
- Bad geometry, UVs, material slots, or mesh asset save: start in `DsonMeshBuilder.*`.
- Bad skin weights: start in `DsonSkinWeightsBuilder.*`.
- Missing or wrong morph targets: start in `DsonMorphBuilder.*`, then `DsonParserFunctions.h` for morph, scene-modifier, and formula-output exports.
- Bad shader detection or channel mapping: start in `DsonMaterialBuilder.*`, then `MaterialMastersV1.md`.
- Missing or wrong textures: start in `DsonTextureImporter.*`.
- Import sequencing, or the abort-before-build gate: `DsonImportPipeline.*`.
- A new field carried between import stages: `DsonImportTypes.h`.
- A parser-handle leak or hand-rolled `Create`/`Destroy`: `DsonLoadedDocument.*` (R3).
- A duplicated URL/id/coordinate helper: `DsonImportUtils.h` (see `CodeReviewRules.md` R4).
- Package/asset save or import-path naming: `DsonAssetUtils.*`.

## Discovery Rule

When asked to change behavior, identify the component using the routing table first. Read the relevant header and top-of-file `.cpp` comment, then inspect only the function involved.

For audits and diagnostics, identify the symptom first, collect the relevant log or asset evidence, then inspect the routed source component.
