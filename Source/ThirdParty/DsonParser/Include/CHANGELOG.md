# Changelog — DsonParser C ABI

Changes to the flat C ABI (`DsonParserAPI.h`). Ships beside the header/DLL so
consumers see what changed without this repo's source. Newest first; stop at the
version you already integrate against. Policy: docs/versioning.md.

SemVer with C-ABI semantics: MAJOR = breaking ABI · MINOR = additive (binary-compatible) · PATCH = internal fix (`DsonParserAPI.h` byte-identical).
Entry sigils: `+` added · `~` changed · `-` removed/deprecated · `!` fixed.

## Unreleased

Nothing yet — new C-ABI changes land here, then move under a version heading on release.

## 1.1.0 — 2026-06-08 · MINOR (additive)

First typed modeling of `scene.extra`: the DAZ "Character Addon Loader"
`PostLoadAddons` manifest. Lets an importer discover companion conforming figures
(Genesis 9 eyes/mouth/eyelashes/tear/eyebrows) a `character` preset instances but
does not list in `scene.nodes`. Paths only — resolving against content roots and
loading the referenced files stay importer responsibilities.
+ DsonDocument_GetScenePostLoadAddonCount → slot count, flattened across every scene.extra PostLoadAddons map in document order (0 = none)
+ DsonDocument_GetScenePostLoadAddonSlot → DAZ slot key (e.g. Follower/Attachment/Head/Face/Eyes)
+ DsonDocument_GetScenePostLoadAddonAssetName → addon asset name
+ DsonDocument_GetScenePostLoadAddonAssetFile → content-relative loader .duf path
+ DsonDocument_GetScenePostLoadAddonMatPreset → content-relative MAT preset .duf path ("" = no preset)

## 1.0.0 — 2026-06-07 · baseline

First versioned release — labels the entire current C ABI (~180 functions). Full
inventory: DsonParser_Roadmap.md (this repo only); pre-versioning history is not
retro-numbered. Baseline covers geometry, skeleton/nodes, skin binding (per-vertex
influence cache + capped/renormalized weights), UV sets, source-order materials,
and morph targets. Most recent additive surfaces an importer may not yet bind:
+ Image pixel dimensions: DsonDocument_GetImageId, DsonDocument_GetImageMapWidth, DsonDocument_GetImageMapHeight
+ Per scene-material-channel LIE layers: DsonDocument_GetSceneMaterialChannelLayerCount (0 = plain, N≥2 = layered), DsonDocument_GetSceneMaterialChannelLayerTexturePath, DsonDocument_GetSceneMaterialChannelLayerLabel
+ Formula (RPN) storage for modifier_library + scene.modifiers: DsonDocument_GetModifierFormulaCount / DsonDocument_GetSceneModifierFormulaCount, each with matching FormulaOutput / FormulaStage / FormulaOperation* accessors — stored, not evaluated (importer-side)
