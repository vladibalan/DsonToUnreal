# Changelog — DsonParser C ABI

Changes to the flat C ABI (`DsonParserAPI.h`). Ships beside the header/DLL so
consumers see what changed without this repo's source. Newest first; stop at the
version you already integrate against. Policy: docs/versioning.md.

SemVer with C-ABI semantics: MAJOR = breaking ABI · MINOR = additive (binary-compatible) · PATCH = internal fix (`DsonParserAPI.h` byte-identical).
Entry sigils: `+` added · `~` changed · `-` removed/deprecated · `!` fixed.

## Unreleased

Nothing yet — new C-ABI changes land here, then move under a version heading on release.

## 1.4.0 — 2026-06-10 · MINOR (additive)

Per-layer LIE compositing metadata. DAZ Layered Image (LIE) `map` elements carry the
per-layer instructions a faithful re-composite needs — blend `operation`, `transparency`
(opacity), `active`/`invert` flags, `color` tint, and a 2D transform (`rotation`,
`xscale`/`yscale`, `xoffset`/`yoffset`, `xmirror`/`ymirror`). Through 1.3.0 only each
layer's `url`/`label` was retained (1.3.0: "per-layer blend op/transform stay
unmodeled"); these are now parsed faithfully onto `Image::layers` (raw values, DAZ-
semantic defaults) and exposed on **both** existing layer surfaces — the per-image
index family (1.3.0) and the per-scene-material-channel family (1.0.0) — at parity with
the path+label accessors. Parser stays faithful (R6.4): raw passthrough, no compositing
performed, no cross-section merge; the consumer re-composites from the raw layers. A
color-only base layer with no `url` stays excluded from `Image::layers` (so its fields
are unreachable), unchanged from 1.3.0. Verified against TestFiles/HID_Nancy_9.duf (G9
HID Nancy head diffuse + SSS Color, 4-layer stacks) and a crafted inline-`#id` snippet.

28 new accessors = 14 shared suffixes × 2 prefixes (per-image
`DsonDocument_GetImageLayer…`, args `(handle, imageIndex, layerIdx)`; per-channel
`DsonDocument_GetSceneMaterialChannelLayer…`, args `(handle, sceneMatIndex, channelIdx, layerIdx)`):
+ …BlendMode → raw "operation" blend string, e.g. "blend_source_over"/"blend_multiply" ("" = invalid/absent)
+ …Opacity → raw "transparency" (1.0 = opaque). NB sentinel 0.0 collides with a legitimately-transparent layer — bound-check Count first
+ …Active → "active" flag (false = invalid)
+ …Invert → "invert" flag (false = invalid)
+ …ColorR / …ColorG / …ColorB → "color" RGB tint components (0.0 = invalid)
+ …Rotation → "rotation" in degrees (0.0 = invalid)
+ …ScaleX / …ScaleY → "xscale" / "yscale" (1.0 = invalid; scale exception per the R1 contract)
+ …OffsetX / …OffsetY → "xoffset" / "yoffset" (0.0 = invalid)
+ …MirrorX / …MirrorY → "xmirror" / "ymirror" mirror flags (false = invalid)

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
