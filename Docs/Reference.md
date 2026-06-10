# DsonToUnreal Engineering Reference

Durable, slow-changing facts an agent or maintainer needs *while working* — not
project status (that is `Docs/Roadmap.md`) and not dated decision rationale (that
is `Docs/DecisionLog.md`). This is the home for hard-won lessons, verified data
points used as sanity checks, and recurring gotchas. Extracted from the Roadmap
so they stay findable without loading the status doc.

Contents:
- Carry-forward lessons (hard-won; don't relearn)
- Verified data facts (sanity checks for future work)
- LIE (layered-image) composition — the makeup/overlay recipe + `#fragment` diagnostic
- Genesis 9 companion figures & the `PostLoadAddons` discovery chain

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
- **A SubsurfaceProfile won't bind on a MIC whose parent was set by raw
  `MIC->Parent = Master`.** `UpdateMaterialRenderProxy` is the only place the
  profile registers and binds, gated on
  `UseSubsurfaceProfile(GetShadingModels())`; `PostEditChange` reaches that
  render-proxy propagation before refreshing the MIC's cached `ShadingModels`
  from the parent. Symptom: imported skin is default-lit until any later MIC edit
  re-fires `PostEditChange`. Fix: set the parent via
  `SetParentEditorOnly(Master, /*RecacheShader=*/true)`, which caches the
  shading model at parent-set time.
- **UE Subsurface Profile *redistributes* diffuse light (≈energy-conserving); it
  does not *add* luminance.** So it cannot replace an additive subsurface/translucency
  brightness term — toggling the profile shifts hue (the scatter tint), not overall
  brightness. PBRSkin's skin brightness came from its inline translucency (DAZ
  `Translucency Weight` ~0.85 — the *active* path; the `Sub Surface` section is inert).
  Since the profile model exposes no Subsurface Color pin, that term is restored
  **tuned → Base Color** (additive, so it stays lighting-aware); IrayUber's
  translucency was negligible (0.1) and stays removed. Per-character brightness is a
  **MIC**-level concern, not the master (e.g. Laura is faithfully darker than Nancy).
- `Use<Name>Map` toggles gate the texture via `lerp(Color, Map, UseFlag)`,
  default 0; the builder raises it to 1 when it sets a texture. If a channel
  ignores its texture, check the use-flag before suspecting the sampler.
- PBRSkin Base Color is **not** diffuse-sampler-direct — it is
  `Multiply(lerp(DiffuseColor, Diffuse, UseDiffuseMap), AO-branch)`.
- **UE refraction is gated by the Refraction *Method* (material Details), not the input pin.**
  Disconnecting the Refraction pin does **not** disable refraction — set **Method = None**. A copied-node
  dump shows the Method only in the root pin's *label* (`Refraction (Index Of Refraction)` = on;
  `Refraction (Disabled)` = off). A translucent shell that minifies the background **independent of opacity**
  is the refraction signature — toggle the Method, not the opacity, to test. This cost a full loop on the G9
  eye-moisture lensing (the cornea shell's IOR refraction minified the eyeball; the pin-disconnect did
  nothing). Full story → `Docs/DecisionLog.md` "eye-moisture cornea lensing".

## Verified data facts (sanity checks for future work)

- **G8.1:** `Genesis8_1Female.dsf` 16556 verts / 16368 polys; UV set
  `Base 8.1 Female.dsf` 18293 UVs, 3308 overrides; 17 sections, all IrayUber.
- **G9 / Laura:** `Genesis9.dsf` 25182 verts / 25156 polys; UV set
  `Base Multi UDIM.dsf` 27087 UVs, 3744 overrides; 7 sections, all PBRSkin
  (`studio/material/daz_brick`, `PBRSkin.dsf#PBRSkin`). Zone→tile: Head 1001,
  Body 1002, Legs 1003, Arms 1004, Nails 1005.
- **G9 companion figures** (separate conforming figures, each its own DSF + bones):
  Eyes `Genesis9Eyes-1` 2120 v / 2112 p (`EyeMoisture Left/Right`, `Eye Left/Right`),
  13 bones; Eyelashes `Genesis9Eyelashes-1` 2028 v / 858 p (`Eyelashes Lower/Upper`),
  38 bones; Mouth `Genesis9Mouth-1` 5079 v / 5000 p (`Mouth`, `Teeth`), 43 bones; Tear
  `Genesis9Tear-1` 280 v / 220 p (`Tear`), 38 bones. The G9 **body** (`Genesis9-1`,
  25182 v) carries none of these — its 7 surfaces are Fingernails, Toenails, Legs,
  Mouth Cavity, Arms, Head, Body.
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

## LIE (layered-image) composition — the recipe behind makeup/overlays

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

**Composition is out of importer scope.** Executing this recipe — compositing the
ingredient layers per their blend ops / transparency / order into a faithful Diffuse,
then baking out and rebinding the MIC — is *interpretation*, not translation, so the
importer does not do it (`Docs/Principles.md` P1). The recipe must still be brought
across **faithfully** (P2); closing the gap left by today's dropped recipe (above) is
an additive parser exposure of the per-layer compositing metadata
(operation/transparency/color/invert/transforms/active) — taken when a concrete need
lands (`Docs/Principles.md` P4), not yet.

**Diagnostic shortcut (the time-saver).** A channel `image` beginning with `#`
is a LIE recipe id, *never* a file path. The importer gets the **base** from the
channel's `texture_path` and the **overlay ingredients** from the
`GetSceneMaterialChannelLayer*` accessors; the raw `#fragment` is neither, so if
it reaches `ImportOrFind` it logs a harmless "could not resolve '#…'". That is
cosmetic — every real texture is present and imported, and the surface renders.
Do not treat it as a missing asset.

## Genesis 9 companion figures & the `PostLoadAddons` discovery chain

**Read before concluding a G9 file "doesn't reference" eyes/mouth/etc. — they are
declared in `scene.extra`, not `scene.nodes`** (re-deriving this cost a parsing pass
that wrongly concluded the companions were absent). A G9 character preset
(`asset_info.type` = `character`) instances only the **body** (`Genesis9.dsf`) in
`scene.nodes`; the eyes, mouth, eyelashes, and tear are pulled in by a post-load script
recorded in `scene.extra`:

```
scene.extra[ type="scene_post_load_script", name="Genesis9PostLoad…" ]
  .settings.PostLoadAddons.value[ "<slot>" ].value:
     AssetName   e.g. "Genesis9Eyes"
     AssetFile   -> a loader .duf (type=wearable) that instances the companion
     Presets.value.Mat.value.PresetFile -> a MAT preset .duf (preset_hierarchical_material)
```

Resolution chain to import one companion: `character preset → PostLoadAddons[slot].AssetFile
(loader .duf) → its scene node → geometry DSF` (e.g. `Genesis9Eyes.dsf#Genesis9Eyes-1`),
plus `PostLoadAddons[slot].MatPreset` for its materials (matched by geometry-id + group;
the preset targets the figure via `name://@selection`). The loader conforms the companion
to the selected figure (`conform_target = name://@selection:`).

Slots seen: `…/Face/Eyes`, `…/Face/Mouth`, `…/Face/Eyelashes`, `…/Face/Tears`, and
(character-dependent) `…/Forehead/Eyebrows`. The eyebrow addon is **fibermesh**
(`G9EyebrowFibers`, strand-based hair), not a polygon companion. `PostLoadAddons` is a
slot-keyed object, so a consumer must impose a stable order.

The parser exposes this manifest as of **DsonParser 1.1.0** —
`DsonDocument_GetScenePostLoadAddon{Count,Slot,AssetName,AssetFile,MatPreset}` (paths only;
resolving against content roots and loading the referenced files stay importer work). Import
status + slicing: [`Roadmap.md`](Roadmap.md) → "Genesis 9 companion figures".

**Companion rigging (verified from `Genesis9Eyes.dsf`, 2026-06-08).** Each companion is a
`figure` DSF carrying a *partial copy* of the Genesis 9 skeleton (the eyes' `node_library` =
1 figure + 13 bones: `hip → pelvis/spine1‑4 → l_/r_shoulder, neck1‑2 → head → l_eye/r_eye`)
and a `SkinBinding` weighting the geometry to a **subset of those body bones by identical
name** (eyes → `l_eye, r_eye, head, neck1‑2, spine1‑4, hip`). Companions therefore **share
the body's Genesis 9 skeleton** — bind each companion mesh to the same `USkeleton` and
leader-pose it to the body, no per-companion skeleton. The `AssetFile` loader is a `wearable`
*preset* (no `scene.nodes`), so resolve a companion through its geometry DSF, not loader nodes.

**Companion-introduced bones (verified from `Genesis9Mouth.dsf`).**
Most companions' joints are a pure subset of the body skeleton (eyes above), but the Mouth's
`SkinBinding` references a tongue chain `tongue01→…→tongue05` (parented under the shared
`lowerteeth`) that the body skeleton does **not** have — `upperteeth`/`lowerjaw`/`lowerteeth`
themselves *are* on the body. `FDsonSkeletonBuilder::MergeCompanionBonesIntoSkeleton` (called by
`BuildCompanion` before the companion mesh is built) detects absent companion bones, builds their
full ref skeleton via `BuildReferenceSkeletonFromDsf`, and merges it into the body skeleton via
`MergeAllBonesToBoneTree` — so tongue verts bind to tongue01–05, not the root, and the body
skeleton is re-saved with the +5 bones. General: any companion-introduced bone with a resolvable
parent chain follows the same path; unresolvable parent chains log a warning and fall back.

**Companion materials (verified from `Genesis 9 Eyes MAT.duf`).** Each addon's `MatPreset` is a
`preset_hierarchical_material` `.duf` (path from the PostLoadAddon manifest), not the character
scene. Top-level keys: `image_library` + `material_library` + a **standard `scene.materials`**
array (id/url/geometry/`groups`/uv_set/`extra`→shader-type) the parser's scene-material accessors
read like the body's — so wire companion materials by reusing `FDsonMaterialBuilder` on the loaded
preset, keyed by `groups` → surface. Mixed shaders within one preset (eyes: `Eye L/R` = PBRSkin,
`EyeMoisture L/R` = IrayUber); some surfaces reference `material_library` via a `#fragment` url
rather than inline channels (verify those channels resolve).

**`scene.animations` key-0 binding (verified from `Genesis 9 Mouth MAT.duf`, 2026-06-08).** A
`preset_hierarchical_material` may **declare** channels in `scene.materials` as bare `{id,type}`
placeholders, then bind the *real* values **and `image_file`s** in a separate **`scene.animations`**
array of `{url, keys}` keyframes (`keys = [[0, <value>]]`, key 0 = init state). The `url` is a DSON
pointer `<node>#materials/<matId>:?<path>` in two forms: top-level (`:?diffuse/value`,
`:?diffuse/image_file`) and extra channel
(`:?extra/studio_material_channels/channels/<Name>/{value,image_file}`, `<Name>` **percent-encoded**,
e.g. `Specular%20Lobe%201%20Roughness`). Mouth/Teeth carry `diffuse` = `Genesis9_Mouth_D_1001.jpg`
(value `[1,0.78,0.78]`), `Specular Lobe 1 Roughness` (+ `_R_` map) and `Translucency Color` (+ `_D_`
map) under `animations`, with nothing useful in `scene.materials`. General rule: **DAZ parks
initialization data at `scene.animations` key 0 — never skip it**, and don't trust a "textureless"
conclusion without checking it. (When a PostLoadAddon has a `Presets→Mat→PresetFile`, that preset is
the operative material source, overriding the `AssetFile`.) **Parser** exposes this faithfully
(`DsonDocument_GetSceneAnimation*`, 1.2.0) and never merges it onto `scene.materials`; the **importer**
consumes key 0 in `DsonMaterialBuilder::ApplySceneAnimationOverrides` (2026-06-08), filtered by the
channel→param mapping. **matId gotcha:** the url `<matId>` is percent-encoded and can differ from
`GetSceneMaterialId` (eyes: url `EyeMoisture%20Left` vs id `EyeMoisture Left-1`; Mouth/Teeth match
verbatim). Status → [`DecisionLog.md`](DecisionLog.md).
