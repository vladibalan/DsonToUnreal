# DsonToUnreal — Changelog

Consumer-facing changes only — the published surface a downstream consumer pins to:
the public `DsonImporter` module API (`Source/DsonImporter/Public/DsonImporter.h` +
`DsonImportRequest.h`) and the shape of emitted output it binds to (the
`/Game/DazImports/...` asset layout). Newest first; one sigil-prefixed line per
change (`+` added · `~` changed · `-` removed/deprecated · `!` fixed). Scheme,
baseline, and the per-change gate: [`Docs/Versioning.md`](Docs/Versioning.md) and
[`Docs/CodeReviewRules.md`](Docs/CodeReviewRules.md) R12.

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
