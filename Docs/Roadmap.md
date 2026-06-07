# DsonToUnreal Roadmap & Status

This is the **single source of truth for project status**: what phase the
importer is at, what shipped in each, what was deliberately deferred, and the
open bug/cleanup backlog. It replaces the external "handoff" documents that went
stale between sessions — anything an agent or the maintainer needs to know about
*where the project stands* lives here, and is updated **in the same change that
moves it** (see `Docs/CodeReviewRules.md` R9).

This file tracks *status*. For *how the code is organized* see
`Docs/ImporterArchitecture.md`; for material-master parameter contracts see
`MaterialMastersV1.md`. Load-bearing technical invariants (the DAZ→UE coordinate
flip, winding, scale) are owned by `CodeReviewRules.md` R4 and the `DazPointToUe`
helper — referenced here, not restated, so they cannot drift.

_Last updated: 2026-06-06._

## Phase status

| Phase | Scope | Status |
|---|---|---|
| 1–2 | DLL load, content roots, validator, import window | ✅ Done |
| 3 | Skeleton builder (bone orientation, handedness, normals) | ✅ Done — verified G9 + G8.1 |
| 4 | Skeletal mesh geometry, UVs, polygon groups, material slots | ✅ Done |
| 5 | Skin weights (binds on bone name) | ✅ Done |
| 6 | Materials — per-section MIC wiring, 3 masters, texture import | ✅ Done (v1) |
| 6.x | UV-set import (seams) | ✅ Done — verified G8.1 + Laura, zero fallbacks |
| 6.y | Material polish (IrayUber washy fix; multi-UDIM resolved as not-needed) | ✅ Done |
| 6 v2 | Materials v2 — faithful makeup + LIE import, then SSS Profile (current), eye-moisture | 🔄 In progress — slice #1 implemented (Nancy-verified); acceptance regression pending, then slice #2 |
| 7 | Morph targets (`UMorphTarget` per morph) | ✅ Done — delta-bearing morphs, including formula-reachable `?value` leaf files, via MeshDescription morph attributes |
| 8 | Save to Content Browser (`/Game/DazImports/`) | ✅ Implemented per-phase, working |

## Phase 6 — what shipped (v1)

- Per scene-material `UMaterialInstanceConstant`, parented to one of three
  hand-authored masters in `Content/Materials/` (spec: `MaterialMastersV1.md`):
  `M_DazIrayUber` (G8/G8.1/G3), `M_DazPBRSkin` (G9/Laura), `M_DazDefault` (fallback).
- Shader detection: URL fragment first, then `shader_type`.
- Channel→parameter mapping tables in `DsonMaterialBuilder.cpp`
  (`GetIrayUberMapping()` / `GetPBRSkinMapping()`); textures imported via
  `DsonTextureImporter` with per-channel sRGB.
- IrayUber subsurface washy-skin fix: a `TranslucencyWeight` (default 0.1) gates
  the translucency tint + SSS map into Subsurface Color.
- Outputs: MICs `/Game/DazImports/Materials/<basename>/MI_<sceneMatId>`; textures
  `/Game/DazImports/Textures/<mirrored DAZ path>/T_<filename>`.

"v1" means materials look acceptable on the tested figures; the items under
**Deferred to v2** are knowingly out of scope for now.

## Figure / generation support

Figure support is gated by **shader**, not geometry: mesh + skeleton + skin
import is generation-agnostic, but a figure only renders correctly if its DAZ
shader has a matching master + channel mapping.

| Generation | Geometry / skeleton / skin | Materials | Status |
|---|---|---|---|
| Genesis 8 / 8.1 | ✅ | IrayUber → `M_DazIrayUber` | ✅ Supported, verified |
| Genesis 9 (Laura, Nancy) | ✅ | PBRSkin → `M_DazPBRSkin` | ✅ Supported, verified; makeup/LIE source textures import standalone |
| Genesis 3 (Victoria 7 HD) | ✅ | IrayUber → `M_DazIrayUber` | ✅ Supported, verified |

## Phase 6 v2 — Materials v2

**Closes Phase 6 (Materials).** Picks up the deferred-from-v1 work under a
two-part filter:

1. **Runtime perf > visual fidelity.** Game-runtime cost decides; the same
   filter used for the IrayUber bump→normal decision (2026-06-06). If a DAZ
   feature requires runtime shader work that can't be made free-or-near-free,
   v1's approximation stands as the runtime answer.
2. **Content options preserved.** The importer is a data pump — it never
   bakes away authoring choices. Source assets (`Makeup Base Color`, LIE
   layers) land as standalone `UTexture2D`s; whether/how they get combined
   into a runtime Diffuse is an authoring step, deferred to the **Designer**
   plugin (separate, future — see below).

**Acceptance set** (same as v1): G8 Jordina Full Character + G8.1
Genesis8_1Female base, G9 Laura + Nancy, G3 Victoria 7 HD. For the master
parameter-contract format, `MaterialMastersV1.md` remains the source of record;
its "Open follow-ups" subset is now superseded by the slice list below.

Slices are sized to ship independently and are taken in order; each one updates
this section as it lands. **Current: slice #2 (Subsurface Profile pipeline).**

### Planned slices

1. **Faithful makeup + LIE import** — ✅ Implemented 2026-06-06; verified on
   Nancy 9, acceptance regression pending (see "Slice #1 — handoff notes"). The
   importer imports `Makeup
   Base Color` textures and each non-base LIE layer as standalone
   `UTexture2D` assets under `/Game/DazImports/Textures/`. **No** `Makeup *`
   entries added to `GetPBRSkinMapping()`, **no** `Makeup *` parameters added
   to `M_DazPBRSkin` — per-surface makeup values (Enable/Weight/Roughness
   Mult) stay in the DAZ source for the future Designer plugin to consume
   directly via the parser. Folds in the sRGB-cache-conflict fix in
   `DsonTextureImporter` while we're inside it.
2. **Subsurface Profile pipeline** — generate per-character
   `USubsurfaceProfile` assets; rewire skin masters' subsurface input. Maps
   the SSS-family channels (`Sub Surface Enable`, `SSS Color`, `SSS
   Direction`, `Transmitted Color`, `Translucency *`, `Scattering Measurement
   Distance`) into the profile. Perf-neutral vs. inline subsurface.
3. **Eye-moisture / cornea master** (`M_DazEyeMoisture`) — new translucent
   master + eye-surface detection + mapping. Translucent shading cost
   absorbed by the small pixel footprint of eyes (~1% on close-ups, much less
   normally).

### Slice #1 — handoff notes

The parser-surface investigation the previous note called for is **done**
(2026-06-06). Slice #1 decomposes into three independent parts; only one
needs the parser, and that one is now the gating item.

- **(a) Makeup Base Color import — plugin-side done.** The parser already
  exposes every surface channel by id via the generic scene-material channel
  accessors (`GetSceneMaterialChannelId` plus
  `GetSceneMaterialChannelImageUrl` / `GetSceneMaterialChannelTexturePath`)
  and drops nothing, so `Makeup Base Color` is already reachable; it just
  stays out of `GetPBRSkinMapping()` by design. The importer uses a small
  **side path**: iterate channels, match a known image-bearing-but-unmapped
  list (`Makeup Base Color`), and call
  `FDsonTextureImporter::ImportOrFind` **without**
  `SetTextureParameterValueEditorOnly`. No parser change.
- **(c) sRGB cache-conflict fix — plugin-side done.** `ImportOrFind`
  (`DsonTextureImporter.cpp`) keys ordinary texture imports by resolved path,
  sRGB mode, and optional asset suffix. If an existing base package already
  belongs to the other sRGB mode, the second variant gets a disambiguating
  package suffix. No parser change.
- **(b) Non-base LIE layers — DsonParser change SHIPPED (2026-06-06).** This
  needed a **retain-then-expose** parser change, not accessors-only: the parser
  had been **discarding** overlay layers at parse time (`GetImageMapPath` read
  only `map[0]`; `struct Image` stored a single `map_file`). It now retains all
  layers on `Image::layers`, with `map_file` kept as the base for unchanged
  single-texture resolution (non-breaking). Plugin side imports layers 1..N-1
  as standalone textures.

**Sequencing (2026-06-06): parser-first — parser done; plugin side done.** The DsonParser-repo
change shipped; the rebuilt `DsonParser.dll`/`.lib`/header are in
`Source/ThirdParty/DsonParser/` (bundled header byte-identical to the parser
source). The plugin binds the layer exports, imports makeup and non-base LIE
textures as standalone assets, and keys ordinary texture imports by source path
plus sRGB mode with a collision-only package suffix for the second variant.

**Parser ABI — shipped 2026-06-06** (the plugin binds these via the
`DSON_PARSER_API_LIST` X-macro in `DsonParserFunctions.h`, guarded by
`DsonParserAbiCheck.cpp`). Three accessors landed — note the parser **did not**
add a per-layer image-url accessor, so the importer uses `LayerTexturePath`
directly (no raw-URL fallback):

- `int DsonDocument_GetSceneMaterialChannelLayerCount(handle, sceneMatIdx, channelIdx)`
  — LIE layer count; **0 for a plain channel, N ≥ 2 for a layered one** (never
  1). Layer 0 = base; its texture path equals the existing
  `GetSceneMaterialChannelTexturePath`.
- `const char* DsonDocument_GetSceneMaterialChannelLayerTexturePath(handle, sceneMatIdx, channelIdx, layerIdx)`
- `const char* DsonDocument_GetSceneMaterialChannelLayerLabel(handle, sceneMatIdx, channelIdx, layerIdx)`

Layers attach only on an identity (id/url) image match, never a shared
base-path match. Per-layer compositing metadata (operation/opacity/color/
transforms) stays in the parser model, **deferred to the Designer** — not
exposed and not this slice.
- **Verification asset.** HID Nancy 9 (`D:/Daz_content/People/Genesis 9/
  Characters/HID Nancy 9.duf`) carries both PBRSkin `Makeup *` channels
  (every surface) and a LIE-based head diffuse — confirmed during planning.
  Use as the slice's primary verification figure; retest the rest of the
  acceptance set to confirm no regression on figures without makeup/LIE.

**Session handoff (2026-06-06) — slice #1 status & open items.**

- **Implemented and verified on Nancy 9 only.** End-to-end on HID Nancy 9: base
  skin binds (head renders correctly), every non-base LIE layer imports as a
  standalone `T_…_lie_<idx>[_label]` asset (incl. `makeup_02` + `brows_base`,
  cross-checked against the on-disk `lie/` folder), and no sRGB conflicts.
- **Acceptance-set regression NOT run yet — the immediate next action.** Verify
  no new textures / unchanged materials on the no-makeup/no-LIE figures: **G8
  Jordina Full Character, G8.1 base female, G9 Laura, G3 Victoria 7 HD.** Only
  after that is slice #1 fully signed off — and only then start slice #2.
- **One open follow-up — the benign `#fragment` warning** (cosmetic; see "Known
  latent issues" + the LIE subsection under "Deferred to Designer"). A
  ready-to-use Implementer prompt was drafted this session: skip `#`-prefixed
  refs in `ImportOrFind` + a Verbose diagnostic to capture origin. The exact
  call passing the raw `#fragment` was never pinned statically (all importer
  paths prefer the resolved `texture_path` / real layer urls), but the fix is
  safe regardless — a `#`-ref is never a file. Backlog, or fold in before slice #2.

### Slice #3 — note for the master rework

While reworking `M_DazPBRSkin` to wire the Subsurface Profile, audit it for
the same gated-but-evaluated-nodes pattern that motivated the IrayUber bump
cleanup (parameters do not fold to zero at compile time; gated branches still
sample). Remove any cost-when-disabled paths in the same pass.

### Dropped from v2 — runtime cost > visual-fidelity gain

v1's approximation is the runtime answer; the full DAZ-faithful version is not
pursued for game runtime. Same framing as the IrayUber bump decision.

- **Full dual-lobe specular** — adds a second specular GGX evaluation per skin
  pixel per frame. v1's single-lobe approximation stands.
- **Clear-coat split** — UE Clear Coat shading model adds clear-coat GGX +
  transmission on top. v1's tinted-specular top-coat stands.
- **Metallic Flakes (skin)** — procedural noise + size/density is non-trivial
  runtime ALU. No current content needs it (Nancy ships flakes at weight 0).
  If a future asset requires strong flakes, address per-character outside the
  default skin pipeline.

### Deferred to Designer (separate future plugin)

The **Designer** plugin (not yet started) will be a separate UE plugin built on
top of the importer. Planned scope:

- In-editor diffuse composition: pick a target surface; mix base Diffuse with
  the imported `Makeup Base Color`, LIE layers, and any user-provided
  textures; per-source blend mode / opacity / weight; live preview.
- Bake-out: produce a new `UTexture2D` and rebind the MIC's `DiffuseMap` to
  it. Original imported assets stay untouched so variants remain possible.

The Importer stays agnostic to the Designer: imported assets land with
predictable paths and naming so the Designer (and any third tool) discovers
them by convention — no importer-side hooks, no Designer-specific sidecars.
The Designer re-uses the parser directly for per-surface metadata (Makeup
weights, etc.) rather than receiving Importer-emitted data. Rationale: keep
each plugin LLM-agent-friendly (smaller contexts per agent task).

#### LIE (layered-image) composition — the recipe behind makeup/overlays

**Read this before chasing any `#fragment` image reference or a
"could not resolve '#…'" log warning — re-deriving it has cost two sessions.**

A DAZ surface channel can point its `image` at a **Layered Image Editor (LIE)
entry** in the document's `image_library`, by `#fragment` id — e.g.
`"image": "#g09_Nancy_head_base 6_<guid> 2-1"`. That entry is **not a texture
file; it is a composition recipe.** Its `map` array is an ordered layer stack,
and each layer carries blend instructions — `operation` (`blend_source_over`,
`blend_multiply`, …), `transparency`, `color`, `invert`, rotation/mirror/scale/
offset, `active`. DAZ *runs* the recipe to bake one composited image, and that
baked result — not any single file — is what the surface actually shows.

Example — Nancy 9 head Diffuse, trimmed to the load-bearing fields (the real
entry also repeats `transparency`/`color`/`invert`/transforms per layer):

```
"id": "g09_Nancy_head_base 6_<guid> 2-1",
"map": [
  { "url": ".../g09_Nancy_head_base.jpg", "operation": "blend_source_over" },
  { "url": ".../lie/g09_Nancy_lie_brows_base.png", "operation": "blend_multiply" },
  { "url": ".../lie/g09_Nancy_lie_base_color_02.png", "operation": "blend_source_over" },
  { "url": ".../lie/g09_Nancy_lie_makeup_02.png", "operation": "blend_source_over" }
]
```

L0 = base skin, L1 = brows (multiplied), L2 = skin-tone, L3 = makeup. All four
`url`s are real files on disk; the intended face is L0→L3 composited per those
operations, not any single file.

**What the importer does today (v1, by design — not a bug):**

- Binds **layer 0 only** (the base, via the channel's resolved `texture_path`)
  as the Diffuse map → the surface renders *base skin*, not the composited look.
- Imports the **non-base layers as standalone ingredient `UTexture2D`s**
  (`T_…_lie_<idx>[_label]`) so they exist for later composition.
- **Drops the recipe**: the parser's per-layer model keeps only `url` + `label`;
  the blend instructions (operation/transparency/color/invert/transforms/active)
  are discarded at parse time.

**The Designer feature.** The "in-editor diffuse composition" bullet above *is*
the feature that executes this recipe — composite the ingredient layers per
their blend ops / transparency / order into a faithful Diffuse, then bake-out
and rebind the MIC. Prerequisite: the **parser must first expose the per-layer
compositing metadata it currently drops** (operation/transparency/color/invert/
transforms/active) — an additive ABI extension on the Designer's critical path,
not the importer's.

**Diagnostic shortcut (the time-saver).** A channel `image` beginning with `#`
is a LIE recipe id, *never* a file path. The importer gets the **base** from the
channel's `texture_path` and the **overlay ingredients** from the
`GetSceneMaterialChannelLayer*` accessors; the raw `#fragment` is neither, so if
it reaches `ImportOrFind` it logs a harmless "could not resolve '#…'". That is
cosmetic — every real texture is present and imported, and the surface renders.
Do not treat it as a missing asset.

### Parked — revisit if content needs it

- **True multi-tile UDIM** — one material/section whose UVs span multiple tiles
  needing *different* textures (VT/atlas territory). This is **not** the
  per-section integer `UVTileOffset` that was built and reverted: under UE Wrap
  addressing `frac(u−n) = frac(u)`, so an integer offset is a visual no-op.
  Current content does not need it (DAZ ships each skin zone as its own 0–1
  section).

## Deferred to v2 (morph follow-ups)

- **Scene dial current values are not baked into the imported character shape.**
  Phase 7 creates rest-state morph targets; applying a DUF's dialed character
  expression/body shape remains a later animation/control-rig or bake step.
- **Formula evaluation/composition is not implemented.** The importer now follows
  scene and external modifier formula outputs whose query property is exactly
  `?value`, resolves those referenced files transitively, and imports every
  delta-bearing morph in each reached file as its own rest-state morph target
  (weight 0). It deliberately does **not** evaluate formulas, seed dial values,
  or compose/bake `Σ(leaf_deltas × evaluated_value)` into the dialed character
  shape. That future evaluator still needs channel values/clamps and a fragment
  to leaf-morph identity bridge. Full analysis:
  **[`Docs/FormulaMorphsV2.md`](FormulaMorphsV2.md)**.

## IrayUber bump-map seam — root cause & fix decision (2026-06-06)

**Symptom.** G8 *character* figures on the IrayUber master (e.g. "Jordina Full
Character") showed hard, lit-only shading creases at every DAZ material-zone
boundary (face↔torso↔arms↔legs). The G8.1 *base* female (same `M_DazIrayUber`)
and all G9/PBRSkin figures did **not**.

**Root cause.** `M_DazIrayUber` reconstructs a normal from the grayscale **bump
(height)** map *in-shader*, using screen/texture-space UV derivatives. Those
derivatives are discontinuous across UV-island boundaries, and DAZ skin zones are
exactly those islands → a hard normal seam at every zone edge. Confirmed by A/B:
`UseBumpMap = 0` removes the seams. The seam *amplitude* scales with bump strength ×
map contrast/frequency, so it has a **visibility threshold** — presence of a bump
map is not the trigger, magnitude is. The G8.1 base female **does** ship a bump map,
but its mild, low-contrast bump stayed below that threshold (and looked nearly
identical before/after the offline bake), whereas detailed character bumps (Jordina)
cross it and seam. PBRSkin has no bump path at all — which is why only strong-bump
IrayUber surfaces seamed.

**Decision: bake bump→normal offline (Option A), not an in-shader graph fix
(Option B).** Selection criteria, in order: (1) game-runtime performance,
(2) fidelity to DAZ surface authoring intent.
- **Performance (decisive).** A turns the bump detail into an ordinary normal-map
  sample — one tap, no per-pixel ALU, correct offline mips. A *seamless* in-shader
  reconstruction (B) requires multi-tap (≥3) texture-space sampling per pixel,
  every frame, on large close-up skin, multiplied per character.
- **Fidelity (secondary).** A honors the authored `Bump Strength` at bake time and
  is actually more correct on mips and seams. B's only edge is keeping bump
  strength as a live runtime dial — an authoring convenience, not a runtime-quality
  gain.

**Approach.** At import, bake the bump height map to a tangent-space normal map
(texture-space Sobel, scaled by the DAZ `Bump Strength`), **combine** it with the
surface's existing normal map when present (detail preserved, not dropped), and
feed the result to the master's normal input. The builder stops populating the
master's in-shader bump parameters.

**Consequences / trade-offs.**
- Bump strength is **baked at import** — changing it means re-importing, not a live
  material dial.
- Adds baked normal textures (surfaces that already ship a normal map combine in
  place, adding none; bump-only surfaces add one).
- The master's `BumpStrength`/`BumpMap`/`UseBumpMap` parameters and the
  `NormalFromHeightmap` + `BlendAngleCorrectedNormals` nodes have been **removed
  from `M_DazIrayUber`** — leaving them in cost ~4 extra texture samples per skin
  pixel every frame even when gated off by a zero `UseBumpMap` (parameters do not
  fold at compile time). The `NormalMap`-group lerp now drives the material's
  `Normal` input directly. Visually neutral (the bump branch contributed flat-only
  when gated off), purely a perf win. `MaterialMastersV1.md` `M_DazIrayUber` table
  updated to match.
- The bake must use the green-channel/handedness convention that matches the mesh's
  MikkTSpace tangents (verify visually on a known figure).
- Height-range nuance (DAZ bump min/max mm) is approximated by the strength scalar
  in v1.5.

**Status:** ✅ done 2026-06-06 — build succeeded; multiple IrayUber figures (incl.
"Jordina Full Character") seam-free, bump detail preserved; G8.1 base female and a
G9/Laura figure import unchanged. `M_DazIrayUber` bump nodes/parameters removed by
user 2026-06-06 (perf cleanup; no visual change).

## Known latent issues (not blocking)

- `SavePackage` return value not checked (hardening).
- `IsValid()` does not include the UV function pointers — consistent with the
  permissive-parser convention (they are optional exports).
- **Benign `could not resolve '#…'` warning on LIE characters** (e.g. G9 Nancy):
  the channel `image` is a `#fragment` LIE composition-recipe id, not a file —
  see "Deferred to Designer → LIE (layered-image) composition". Every real
  texture still resolves and imports; cosmetic only. Cleanup: have the texture
  importer skip `#`-prefixed refs before resolving.

## Cleanup backlog

- **Trim UV diagnostic logging** in `DsonMeshBuilder.cpp`: keep the slim
  `[uv] expansion:` summary (applied/skipped counts); trim or `Verbose`-gate the
  per-triplet `[uv] override[..]` sample lines and per-reason skip samples (R5 —
  no debug scaffolding at `Log` in the import path). Fold in any dead remnants of
  the reverted `UVTileOffset` logging if stubs remain.
- **Remove dead `GetUVPolygonVertexIndex*` parser APIs** — dead since the
  sparse-format migration (return 0 for sparse DSFs). Parser-side change (parser
  repo + DLL rebuild/copy into `Source/ThirdParty/DsonParser/Libs/Win64/`), after
  confirming no references remain.
- **Audit source comments / log strings for stale G3 fallback phrasing.** The
  earlier Roadmap claim that G3 fell back to `M_DazDefault` was incorrect (G3
  uses IrayUber → `M_DazIrayUber`, verified Victoria 7 HD, 2026-06-06). If any
  file's comments or log messages still claim "Genesis 3 → default" / "G3
  fallback", remove or correct them. Search `Source/DsonImporter/` for
  `Genesis 3` / `G3` references.

## Carry-forward lessons (hard-won; don't relearn)

- **Morph targets must go through the MeshDescription, not post-build.** The
  post-build path (`UMorphTarget::PopulateDeltas` + `RegisterMorphTarget` +
  `InitMorphTargetsAndRebuildRenderData`) silently fails: that last call wraps
  itself in `FScopedSkeletalMeshPostEditChange`, which re-derives the asset from
  the MeshDescription on scope exit and discards the just-registered morphs
  (log shows `created>0` but the Morph Target Previewer is empty). The working
  path is `FSkeletalMeshAttributes::RegisterMorphTargetAttribute` +
  `GetVertexMorphPositionDelta` **before** `CommitMeshDescription`; the build
  generates the `UMorphTarget`s. Consequence: only **position** deltas survive
  (`CreateFromMeshDescription` ignores MeshDescription normal deltas); morph
  normals are engine-recomputed. That is why the parser's morph normal-delta
  exports are bound but unused.
- **A delta count of ~tens on a "character" morph means it's a control or
  corrective, not the complete dialed character.** DAZ character identity (e.g.
  Laura) is a *formula-driven control tree* with no deltas until leaf morphs in
  other files; the importer now discovers those `?value` leaf files, but does not
  evaluate their dialed contribution. A real composed shape affects many more
  verts. See `Docs/FormulaMorphsV2.md`.
- **DAZ formula and asset reference URLs can be scheme-qualified.** Formula
  outputs commonly arrive as `<AssetId>:/data/...#Id?value`; `ResolveUrl` strips
  the leading `Scheme:`, `#fragment`, and leading `/` to form the
  content-relative disk path.
- **Establish the visual symptom against the real asset before chasing
  mechanism.** The multi-UDIM thread burned many turns on a Laura mis-sampling
  bug that never existed — she rendered correctly throughout.
- **Integer UV offsets are invisible under Wrap** (`frac(u−n) = frac(u)`); to
  prove a UV-offset path is live, test with a *fractional* value.
- **The MIC preview sphere under-reports subsurface;** tune SSS/translucency
  weights against the figure in the Skeletal Mesh editor, not the sphere.
- `Use<Name>Map` toggles gate the texture via `lerp(Color, Map, UseFlag)`,
  default 0; the builder raises it to 1 when it sets a texture. If a channel
  ignores its texture, check the use-flag before suspecting the sampler.
- PBRSkin Base Color is **not** diffuse-sampler-direct — it is
  `Multiply(lerp(DiffuseColor, Diffuse, UseDiffuseMap), AO-branch)`.

## Verified data facts (sanity checks for future work)

- **G8.1:** `Genesis8_1Female.dsf` 16556 verts / 16368 polys; UV set
  `Base 8.1 Female.dsf` 18293 UVs, 3308 overrides; 17 sections, all IrayUber.
- **G9 / Laura:** `Genesis9.dsf` 25182 verts / 25156 polys; UV set
  `Base Multi UDIM.dsf` 27087 UVs, 3744 overrides; 7 sections, all PBRSkin
  (`studio/material/daz_brick`, `PBRSkin.dsf#PBRSkin`). Zone→tile: Head 1001,
  Body 1002, Legs 1003, Arms 1004, Nails 1005.
- Figure DSFs contain **no** UV data — only a `default_uv_set` URL to a separate
  UV-set DSF.
- Coordinate conversion (load-bearing — see `CodeReviewRules.md` R4 /
  `DazPointToUe`, do not revert): `UE_X=DAZ_Z, UE_Y=−DAZ_X, UE_Z=DAZ_Y` (det −1);
  natural winding `(0,1,2)/(0,2,3)`; scale `ToCm = UnitScale = 1.0`.
- **Laura 9 (`Base Characters 9`) is recipe-only — do not re-hunt for a "Laura"
  morph.** Verified by walking her formula tree (56 files): the only `Laura_*`
  modifiers (`Laura_figure_ctrl_Character`, `Laura_head_bs_Head`,
  `Laura_body_bs_body`) are **pure controls with zero deltas**. All 43
  delta-bearing leaves are **generic** Genesis 9 base morphs
  (`BaseFeminine_head_bs_Head`, `BaseFeminine_body_bs_Body`, `body_bs_Proportion*`,
  the `head_bs_Asymmetry*` family). Her identity is **formula weights, not a
  sculpt**: every edge is `push(url:parent?value) · push(<const>) · mult` (e.g.
  eyelashes ×0.5, head-size proportion ×0.0999999). Discovery-only import exposes
  these 43 leaves at weight 0 (the *ingredients*); reconstructing "Laura" needs the
  deferred evaluator (`effective_weight = root dial × Π push-constants`), which is
  exactly why its parked accessors — channel `current_value` + operation
  `Op/Val/Url` + the non-empty-`Url` push discriminator — are required. (General
  case still holds: a premium character that ships a unique sculpt would have a
  `<Name>_*` leaf *with* deltas, and the same walk would import it.)

## Next up

**Phase 6 v2 — Materials v2.** Active. **Immediate next action: finish slice #1
sign-off** — run the acceptance-set regression (G8 Jordina, G8.1 base, G9 Laura,
G3 Victoria 7 HD); slice #1 is implemented and Nancy-verified but not yet
regression-checked (see "Slice #1 — handoff notes"). **Then** start slice #2
(Subsurface Profile pipeline). See the Phase 6 v2 section above for the full
slice plan and acceptance set.

**Phase 7 v2 — formula evaluation/composed character shape** (queued behind
Phase 6 v2). The discovery-only portion is done: formula-reachable `?value`
files import their leaf morph targets at weight 0. Next is the evaluator/compose
feature in [`Docs/FormulaMorphsV2.md`](FormulaMorphsV2.md): channel
`current_value`, `min`/`max`/`clamped`, and a fragment-to-leaf bridge are still
needed before the importer can bake or emit a combined dialed character shape.
