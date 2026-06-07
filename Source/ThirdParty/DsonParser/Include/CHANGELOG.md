# Changelog

All notable changes to **DsonParser** (the flat C ABI in `DsonParserAPI.h`) are
recorded here. This file ships beside the header/DLL so upstream consumers can
learn what changed without this repo's source tree. See
[`docs/versioning.md`](docs/versioning.md) for the policy.

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: SemVer with C-ABI semantics — **MAJOR** = breaking ABI change,
**MINOR** = additive (binary-compatible), **PATCH** = internal fix only.

## [Unreleased]

_Nothing yet. Add entries here as the C ABI changes; on release they move under a
new version heading that equals `DSONPARSER_VERSION_STRING`._

### Added
### Changed
### Deprecated
### Removed
### Fixed

## [1.0.0] - 2026-06-07

First **versioned** release — labels the entire current C ABI (~180 exported
functions). Full capability inventory: [`DsonParser_Roadmap.md`](DsonParser_Roadmap.md).
Pre-versioning history is not retro-numbered.

### Added
- Baseline C ABI: geometry, skeleton/nodes, skin binding (per-vertex influence
  cache + capped/renormalized weights), UV sets, source-order materials, and
  morph targets. See the roadmap for the per-family breakdown.
- Notable **additive** surfaces included in the baseline — the most recent
  capability an upstream importer may not yet bind:
  - **Image pixel dimensions** — `DsonDocument_GetImageId`,
    `DsonDocument_GetImageMapWidth`, `DsonDocument_GetImageMapHeight`.
  - **Per scene-material-channel LIE layers** —
    `DsonDocument_GetSceneMaterialChannelLayerCount` (`0` for a plain channel,
    `N ≥ 2` for a layered one), `…LayerTexturePath`, `…LayerLabel`.
  - **Formula (RPN) storage over the C ABI** for both `modifier_library` and
    `scene.modifiers` indexes —
    `DsonDocument_GetModifierFormulaCount` / `DsonDocument_GetSceneModifierFormulaCount`
    plus the matching `…FormulaOutput` / `…FormulaStage` / `…FormulaOperation*`
    accessors. Stored only, **not evaluated** (evaluation is importer-side).

[Unreleased]: #unreleased
[1.0.0]: #100---2026-06-07
