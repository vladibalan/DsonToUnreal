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
  (module lifecycle + the programmatic `ImportDazAsset` entry point)
- Public programmatic-import types: `Source/DsonImporter/Public/DsonImportRequest.h`
- Public persisted recipe asset type: `Source/DsonImporter/Public/DsonAssetRecipe.h`
- Public library-catalog types: `Source/DsonImporter/Public/DsonCatalog.h`
- Implementation code: `Source/DsonImporter/Private/`
- Bundled parser API: `Source/ThirdParty/DsonParser/`
- Master material assets: `Content/Materials/`

## Runtime Flow

1. `FDsonImporterModule::StartupModule` calls `EnsureDsonParserLoaded` (idempotent; also triggered at the `ImportDazAsset` entry point so a second module image hosted via `AdditionalPluginDirectories` binds its own `GDsonParser`), which loads `DsonParser.dll`, resolves parser exports into `GDsonParser`, then registers the File menu entry.
2. The menu entry opens `SDsonImportWindow`.
3. `SDsonImportWindow` detects DAZ content roots, lets the user choose a `.duf` or `.dsf`, and calls `FDsonValidator`.
4. `FDsonValidator` parses basic metadata and resolves figure/material/geometry dependencies.
5. Import confirmation produces `FDsonImportSettings`.
6. `FDsonImportPipeline::Run(Settings, ContentRoots)` orchestrates asset creation and returns `FDsonImportResult` (skeleton, mesh, abort flag), in this order:
   - `FDsonMaterialBuilder` builds material instances from scene material channels, driving image import through `FDsonTextureImporter`. It also creates one per-character `USubsurfaceProfile` for skin MICs (Subsurface Profile masters) and imports makeup base textures and non-base LIE layers as standalone `UTexture2D` assets without MIC binding; **variant**-LIE composition is deferred to authoring tools — except the fixed-factory eye-albedo LIE (animation-bound `#fragment`), which is composited and baked at import (`DecisionLog.md` owns the fixed-vs-variant scope rule).
   - Optional `FDsonMaterialDiagnostic` channel dump, then the `M_DazDefault` master loads — a missing master aborts before any asset is built.
   - `FDsonSkeletonBuilder` creates a `USkeleton` from figure DSF nodes.
   - `FDsonMeshBuilder` creates the skeletal mesh, UVs, polygon groups, and material slots, invoking `FDsonSkinWeightsBuilder` (DSF skin influences) and `FDsonMorphBuilder` (MeshDescription morph attributes) before mesh commit; `BuildSkeletalMesh` generates the `UMorphTarget`s.

Two entry points funnel into step 6: the Slate window above, and the programmatic
`FDsonImporterModule::ImportDazAsset(FDsonImportRequest) → FDsonImportReport`, which
runs steps 3–6 headlessly (root detect, validate, gate on resolved deps, `Run`) and
shares the path→settings assembly (`FDsonValidator::ToImportSettings`) with the window.

## Component Responsibilities

`DsonImporter.cpp`

- Owns module startup/shutdown.
- Loads `DsonParser.dll` and populates `GDsonParser` via `EnsureDsonParserLoaded` (idempotent; also triggered at `ImportDazAsset` for multi-image hosting via `AdditionalPluginDirectories`).
- Verifies the loaded DLL's reported version against the built-against header (`DsonParser_GetVersion` vs `DSONPARSER_VERSION_*`); refuses to register on a MAJOR mismatch.
- Adds the File menu entry that launches the import dialog.
- Exposes the public programmatic entry point `ImportDazAsset` (`FDsonImportRequest` → `FDsonImportReport`): detects roots, validates, gates on resolved dependencies, runs `FDsonImportPipeline::Run`, maps the result to the public report. Headless counterpart to the Slate window; both share `FDsonValidator::ToImportSettings`.

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
- `ToImportSettings`: assembles `FDsonImportSettings` from a validated result + the dump-diagnostics option — the single source used by both the Slate window and `ImportDazAsset`.

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
- Resolves the animation-bound `#fragment` eye-albedo LIE to an `image_library`
  entry and composites it through `FDsonTextureImporter` (fixed-factory bake only).
- For IrayUber, bakes bump maps into the normal input and leaves the master's
  `BumpStrength`/`BumpMap`/`UseBumpMap` parameters unset.
- Imports textures through `FDsonTextureImporter`.
- Outputs material instances keyed by material group name.

`DsonTextureImporter.*`

- Resolves image URLs to disk.
- Imports or reuses `UTexture2D` assets.
- Bakes IrayUber bump height maps into tangent-space normal textures and combines
  them with the surface normal map when present.
- Composites a fixed-factory LIE layer stack into one baked `UTexture2D`
  (`CompositeImageLayers`) at native size on the `map_size` canvas; saved
  per-character under `CharacterRoot/Textures/Composites`; cached by image id.
- Source textures saved under `SharedTexturesRoot` (`…/Library/Textures/…`),
  deduped across characters; LIE composites are per-character.
- Sets sRGB according to material channel needs.
- Caches ordinary imports by resolved absolute path, sRGB mode, and optional
  asset-name suffix so color and linear variants can coexist.

`DsonMaterialDiagnostic.*`

- Debug-only material channel dump used for planning and verification.

`DsonParserFunctions.h`

- Function pointer typedefs and `FDsonParserAPI`; includes the optional `DsonParser_GetVersion` accessor used by `DsonImporter.cpp`'s startup ABI-compatibility check.
- Keep synchronized with the bundled DLL's exports: morph-target + scene-modifier URLs discover external morph files (formula-output accessors extend that discovery only — never evaluated).
- Optional scene material channel layer accessors expose LIE layer count, texture
  path, and label; the importer uses them only for standalone non-base layer
  texture import.
- Optional PostLoadAddon accessors (`GetScenePostLoadAddon{Count,Slot,AssetName,AssetFile,MatPreset}`) expose G9 companion-figure declarations from scene.extra; used by `DsonValidator.*` companion discovery.
- Optional `image_library` accessors — `GetImageCount`/`GetImageId`, the per-image LIE layer stack `GetImageLayer{Count,TexturePath,Label}`, and canvas dims `GetImageMapWidth`/`GetImageMapHeight` (`map_size`) — let `DsonMaterialBuilder.*`/`DsonTextureImporter.*` resolve an animation-bound `#fragment` eye-albedo LIE and composite its layers at native size on the `map_size` canvas.
- Optional catalog accessors `Get{Node,Modifier}Presentation{Type,Label}` + `GetGeometryIsGraft` (DsonParser >= 1.5.0) back `DsonCatalog.*` classification (declared content type, label, geograft signal).

`DsonParserAbiCheck.cpp`

- Build-time tripwire only: one `static_assert` per `DSON_PARSER_API_LIST` row asserts ABI-compat
  of the bound signature vs the vendored `DsonParserAPI.h` prototype; no runtime code/linkage, compiles to nothing without the header (`__has_include`).

`DsonImportPipeline.*`

- Top-level import orchestrator: `FDsonImportPipeline::Run` sequences material/texture
  build, the diagnostic dump and `M_DazDefault` gate, then skeleton, body mesh, and companion
  meshes (via `FDsonMeshBuilder::BuildCompanion`; permissive — failures skip, don't abort).

`DsonImportTypes.h`

- Inter-stage types: `FDsonCompanionSource` (resolved companion-figure record, Slice A+), `FDsonImportSettings`, `FDsonImportResult` (carries `Skeleton`, `Mesh`, `CompanionMeshes` from Slice B+). `EGenesisGeneration` now lives in `Public/DsonCatalog.h`, re-exported here via `#include`.

`DsonAssetRecipe.h` (Public)
- `UDsonAssetRecipe` UCLASS + companion/LIE USTRUCTs; consumer-facing recipe asset (R12); passive data container; module's first UHT-reflected type.
`DsonRecipeBuilder.*`
- Emits `<Name>_Recipe` under `CharacterRoot` after each import: manifest, companion slot tags, per-surface LIE recipe. Permissive (R7) — never aborts import.
`DsonCatalog.*` (Public `DsonCatalog.h` + Private `DsonCatalogImpl.h`/`DsonCatalog.cpp`)
- `FDsonCatalog`: read-only library survey (P3) — async supplied-roots enumerate, one parser open/asset, faithful `presentation.type`+graft classification, per-root tolerance, incremental MTime/Size disk cache + lazy thumbnail LRU. Entry on `FDsonImporterModule`: `BeginCatalogEnumerate`/`GetCatalogThumbnail`/`InvalidateCatalog`.

`DsonImportRequest.h`

- Public, pipeline-decoupled programmatic-import surface: `FDsonImportRequest` (source path + options), `EDsonImportStatus`, `FDsonImportReport` (produced assets + success/diagnostics). References no Private type by design.

`DsonLoadedDocument.*`

- `FDsonLoadedDocument`: RAII owner of one parser document handle and the only place
  parser `Create`/`Load`/`Destroy` runs (R3); optional `OutError` surfaces failure text.

`DsonAssetUtils.*`

- `FDsonAssetUtils`: package creation, asset saving, and import-path construction.
  `ImportRootPath()` → `/Game/DazImports`; `CharacterRoot(name)` → `…/Characters/<name>` (per-character assets); `FigureRoot(id)` → `…/Figures/<id>` (parent figure assets); `FigureImportComplete(id)` → true when `<id>_Recipe` exists (completeness-marker check; no-overwrite/skip primitive for the layered-import workstream); `SharedTexturesRoot()` → `…/Library/Textures` (shared, deduped source textures).

`DsonImportUtils.h`

- Header-only shared leaf helpers: URL scheme/fragment stripping, UTF-8→`FString`, DAZ id
  normalization, unit-scale, and the load-bearing coordinate flip; list/rules: `CodeReviewRules.md` R3/R4.

## Discovery Rule

Subsystem→file routing is owned by `AGENTS.md` (Task Routing) — this doc does not
restate it (R10). When asked to change behavior, identify the owning component from
that table, then read its entry in Component Responsibilities above plus the relevant
header and top-of-file `.cpp` comment, and inspect only the function involved.

For audits and diagnostics, identify the symptom first, collect the relevant log or asset evidence, then inspect the routed source component.
