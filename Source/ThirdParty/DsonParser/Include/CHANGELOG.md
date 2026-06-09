# Changelog — DsonParser C ABI

Changes to the flat C ABI (`DsonParserAPI.h`). Ships beside the header/DLL so
consumers see what changed without this repo's source. Newest first; stop at the
version you already integrate against. Policy: docs/versioning.md.

SemVer with C-ABI semantics: MAJOR = breaking ABI · MINOR = additive (binary-compatible) · PATCH = internal fix (`DsonParserAPI.h` byte-identical).
Entry sigils: `+` added · `~` changed · `-` removed/deprecated · `!` fixed.

## Unreleased

Nothing yet — new C-ABI changes land here, then move under a version heading on release.

## 1.3.0 — 2026-06-09 · MINOR (additive)

Per-layer LIE map stack of an `image_library` entry, reachable **by image index**.
The layers were already parsed onto `Image::layers` but exposed only incidentally —
copied onto a material channel that inline-references the image. An image referenced
from elsewhere (e.g. a `scene.animations` `diffuse/image` binding to a base-figure LIE
such as the Genesis 9 eyes) had its layer stack unreachable; `GetSceneAnimationString`
returned only the raw `"#fragment"`. These accessors read the same parsed
`Image::layers` over the `GetImageId` index space, at parity with the per-channel
`…ChannelLayer*` surface. Path + label + count only — per-layer blend op/transform stay
unmodeled (the eye case is all blend_source_over with identity transforms). Parser
unchanged: faithful exposure of already-parsed data, no merge onto `scene.materials` (R6.4).
+ DsonDocument_GetImageLayerCount → textured-layer count of the entry's map stack (1 = plain single texture, N = LIE, 0 = no array-form map / invalid; a color-only no-url base layer is not counted). NB unlike GetSceneMaterialChannelLayerCount, which is 0 for a plain channel.
+ DsonDocument_GetImageLayerTexturePath → layer texture path by (imageIndex, layerIdx); layer 0 = first textured map element ("" = invalid)
+ DsonDocument_GetImageLayerLabel → LIE layer label by (imageIndex, layerIdx) ("" = invalid)

## 1.2.0 — 2026-06-08 · MINOR (additive)

Faithful exposure of `scene.animations` keyframe channels. DAZ
`preset_hierarchical_material` presets park real channel values and `image_file`
paths under `scene.animations` (`{url, keys}`, key 0 = initialization data) while
leaving `scene.materials` channels as bare placeholders. The parser now stores each
entry verbatim — the raw `url` pointer plus the first key's typed value — and exposes
it; it does **not** apply them onto `scene.materials`, resolve the pointer, or resolve
the `image_file` string against `image_library` (parser stays faithful — the consumer
reads both sections and decides the override). First key only; `image_modification`/
tiling and multi-key are recognized in the data but not modeled.
+ DsonDocument_GetSceneAnimationCount → entry count (0 = none/invalid handle)
+ DsonDocument_GetSceneAnimationUrl → raw DSON property pointer, verbatim ("" = invalid)
+ DsonDocument_GetSceneAnimationValueKind → first-key value kind: 0 null · 1 number · 2 bool · 3 string · 4 color (-1 = invalid)
+ DsonDocument_GetSceneAnimationFloat → number value (0.0 if kind ≠ number/invalid)
+ DsonDocument_GetSceneAnimationBool → bool value (false if kind ≠ bool/invalid)
+ DsonDocument_GetSceneAnimationString → string value, e.g. an image_file path ("" if kind ≠ string/invalid)
+ DsonDocument_GetSceneAnimationColorR / …ColorG / …ColorB → RGB from a ≥3-number array (0.0 if kind ≠ color/invalid)

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
