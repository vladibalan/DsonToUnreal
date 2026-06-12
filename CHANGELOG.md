# DsonToUnreal — Changelog

Consumer-facing changes only — the published surface a downstream consumer pins to:
the public `DsonImporter` module API (`Source/DsonImporter/Public/DsonImporter.h` +
`DsonImportRequest.h`) and the shape of emitted output it binds to (the
`/Game/DazImports/...` asset layout). Newest first; one sigil-prefixed line per
change (`+` added · `~` changed · `-` removed/deprecated · `!` fixed). Scheme,
baseline, and the per-change gate: [`Docs/Versioning.md`](Docs/Versioning.md) and
[`Docs/CodeReviewRules.md`](Docs/CodeReviewRules.md) R12.

## 1.6.2 — 2026-06-12 · PATCH

! **Eyelash cutout opacity** — G9 eyelash surfaces now import their DAZ `Cutout Opacity` map and
  render as transparent lash strands instead of solid dark cards. Cutout surfaces route to a new
  `M_DazCutout` master (Masked, two-sided); the grayscale cutout map imports linear. Internal
  builder + master-asset fix; public API and `/Game/DazImports/...` layout unchanged.

## 1.6.1 — 2026-06-12 · PATCH

! **Companion parent-surface material wiring** — a companion MAT preset that binds to a
  legacy parent-surface name (e.g. `Eyelashes`) absent from the geometry's real leaf surfaces
  (`Eyelashes Lower`/`Eyelashes Upper`) now resolves to those child surfaces instead of falling
  to `M_DazDefault` — fixes the stock G9 eyelash companion importing untextured on characters
  without a bespoke leaf-named eyelash preset (e.g. HID Nancy 9). Internal builder fix; public
  API and `/Game/DazImports/...` layout unchanged.

## 1.6.0 — 2026-06-12 · MINOR

+ **Library catalog API** — new `FDsonImporterModule` entry points for a downstream
  browse-and-pick consumer over the installed DAZ library, read from declared data only
  (no folder inference): `BeginCatalogEnumerate(roots, progress)` →
  `TFuture<FDsonCatalogResult>` (async supplied-roots walk, faithful per-asset
  classification, per-root tolerance, incremental on-disk cache), `GetCatalogThumbnail(root, id)`
  (companion preview bytes, lazy LRU), and `InvalidateCatalog()`.
+ **`Public/DsonCatalog.h`** — new public types: `FDsonCatalogEntry`
  (id/rootId/relativePath/label/type/generation/dependsOn/browsable; `id` is the
  root-relative path == `relativePath`, combined with a root it round-trips to
  `ImportDazAsset`), `EDsonCatalogAssetType`, `FDsonCatalogRoot`, `FDsonCatalogResult` +
  per-root `EDsonCatalogRootStatus` (Ok/Missing/Error/Offline). `EGenesisGeneration`
  relocated here (re-exported from the import path; no consumer change).
+ Built against **DsonParser 1.6.0** (per-library-item `presentation.type`/`label` + geograft
  signal for classification; thread-safe distinct-handle contract for the background walk).

## 1.5.0 — 2026-06-11 · MINOR

+ **`UDsonAssetRecipe::Formulas`** — new `TArray<FDsonFormula>` carrying raw, uncomposed DAZ formula
  records from three sources: (1) `scene.modifiers` inline formulas (bFromSceneModifier=true —
  control/ERC dials with formulas defined inline in the DUF), (2) external modifier DSFs reached
  via the scene.modifiers reachability walk (bFromSceneModifier=true — e.g. `HID Nancy 9.dsf`,
  FACS control DSFs applied to the figure), and (3) the body figure modifier_library
  (bFromSceneModifier=false — JCM corrective morphs, intrinsic rig formulas). Each record carries
  the full RPN op list (`FDsonFormulaOp[]`: Op token, Val, Url), the output URL with its
  `EDsonFormulaTarget` tag (MorphValue / BoneCenterPoint / BoneEndPoint / Other), the carrier
  modifier id/name, the dial value, and the bound `UMorphTarget` name where the carrier is a morph.
  No formula evaluation, no composition — faithful raw DAZ data for downstream authoring. Formulas
  are deduped across all three passes by `ModifierId|OutputUrl|Stage`.
+ **`UDsonAssetRecipe::RigPoints`** — new `TArray<FDsonNodeRigPoint>` with one entry per unique bone
  referenced by an ERC-follow formula (OutputTarget BoneCenterPoint or BoneEndPoint). Stores raw
  DAZ center_point + end_point XYZ so a consumer can compute followed-position = base ± evaluated
  delta. Raw DAZ coordinates (not UE-flipped) to keep the whole formula block in one coordinate
  space.
+ **13 new parser exports bound** (all optional, DsonParser ABI-checked): 5 scene-modifier formula
  op accessors, 5 modifier-library formula op accessors, 3 node end_point accessors.
~ **`[recipe-shape]` summary line** extended with formula counts by tag, bound count, and rigpoint count.

## 1.4.0 — 2026-06-11 · MINOR

+ **Anim-bound LIE surfaces** — `AppendLieSurfaces` now scans `scene.animations` key-0
  `Leaf=="image"` entries per scene material (e.g. the eye LIE on G9 Eyes companion).
  These were previously invisible to the recipe walk (only `scene.materials` channel
  `#fragment` URLs were read). The pre-baked marker (`bImporterPreBaked` / `BakedComposite`)
  now fires for anim-bound composites. Dedup: if the same channel is reachable via both
  paths, the channel-walk result is kept and a warning is logged.
~ **`ParseAnimationUrl` / `StripUniquifyingSuffix`** moved to `DsonImportUtils.h` (shared
  inline helpers, R4); `DsonMaterialBuilder` call sites updated — no behavior change.

## 1.3.0 — 2026-06-11 · MINOR

+ **`FDsonLieSurface.SourceCompanionSlot`** — new field on every LIE surface. Empty for body
  surfaces; set to the companion's `PostLoadAddons` slot path for surfaces originating from a
  companion figure (e.g. `.../Face/Eyes`). Makes the recipe self-describing about which mesh
  each surface belongs to without requiring a separate lookup.
+ **Companion LIE surfaces** — the recipe LIE walk now covers every companion figure's
  MAT-preset DUF in addition to the body DUF. Eyes companion baked composites (`Eye Color`,
  `Eye Translucency`) are now emitted and the `bImporterPreBaked` / `BakedComposite` marker
  fires correctly for them. A companion DUF that fails to load warns and is skipped (R7).
~ **Dial-weight join broadened to external morph DSFs** — the fragment id is now URL-decoded
  before lookup (`HID%20Nancy%209` → `HID Nancy 9`), and each modifier URL's referenced DSF
  is resolved and opened directly (cached by path) instead of searching only the figure DSF.
  Results are validated against the actually-imported `UMorphTarget` set so HD morphs and
  control morphs with no mesh target produce no dangling bindings. Expected on Nancy: ≥1
  correlated (character morph ± FACS); `*_HD3` and `SkinBinding` stay uncorrelated by nature.

## 1.2.0 — 2026-06-11 · MINOR

+ **`FDsonDialWeight`** — new array `UDsonAssetRecipe::DialWeights`. One entry per
  imported morph target whose corresponding `scene.modifiers` entry exists in the DUF:
  raw channel value + min/max/clamped range, the DAZ modifier URL, and the sanitized UE
  morph-target name (`ObjectTools::SanitizeObjectName`, same logic the morph builder
  uses). Uncorrelated modifiers (external morph DSF, no matching figure-DSF morph id)
  are silently omitted. Downstream consumers use `BoundMorphTargetName` to look up the
  already-imported `UMorphTarget` and re-apply the dialed weight.
+ **Pre-baked LIE marker** — `FDsonLieSurface` gains `bImporterPreBaked` + `BakedComposite`.
  Set for every `#fragment` image channel where the importer alpha-composited ≥2 layers
  into a single `UTexture2D` at import time. Downstream consumers must NOT re-composite
  those surfaces from the raw `Layers` — the `BakedComposite` texture is the realized
  result. N==1 (single-layer) channels are never marked.
~ **`[recipe-shape]` diagnostic** — emit counts at import: modifiers seen / non-default /
  correlated / uncorrelated; LIE surfaces baked / raw. Per-entry detail gated on
  `bDumpMaterialDiagnostics`.
~ **`UDsonAssetRecipe` comment** updated to reflect Slice 2 contents.

## 1.1.0 — 2026-06-10 · MINOR

+ **`UDsonAssetRecipe`** — new persisted asset emitted under
  `…/Characters/<Name>/<Name>_Recipe` after each import. Carries raw, uncomposed
  DAZ authoring metadata (Slice 1): manifest (source id, skeleton/mesh soft refs),
  companion slot tags (DAZ `PostLoadAddons` slot path paired with each companion
  mesh), and per-surface LIE recipe (ordered layer stack per channel that has LIE
  layers — blend mode, opacity, active, invert, color, rotation, scale, offset,
  mirror; verbatim parser values, never composed). Emission is additive and
  permissive — a failure skips the recipe without aborting the import.

## 1.0.0 — 2026-06-10 · baseline

+ First versioned release. Labels the entire current tree as the consumer baseline:
  - **API** — `FDsonImporterModule::IsAvailable()` / `::Get()` /
    `::ImportDazAsset(const FDsonImportRequest&) → FDsonImportReport`; the
    `FDsonImportRequest`, `FDsonImportReport`, and `EDsonImportStatus` types.
  - **Emitted output** — assets under `/Game/DazImports/Characters/<Name>/` (body +
    companion meshes, `_Skeleton`, `Materials/`, `Textures/Composites/`) and shared
    source textures under `/Game/DazImports/Library/Textures/` (`FDsonAssetUtils`).
  - Pre-versioning history is **not** retro-numbered — including the 2026-06-10
    asset-folder-structure change, which was breaking for path-reconstructing
    consumers (see [`Docs/Roadmap.md`](Docs/Roadmap.md)). A consumer pins to
    `v1.0.0` = the current tree, new folder layout included.
