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

**The Designer feature.** The "in-editor diffuse composition" bullet in
`Docs/Roadmap.md` ("Deferred to Designer") *is* the feature that executes this
recipe — composite the ingredient layers per their blend ops / transparency /
order into a faithful Diffuse, then bake-out and rebind the MIC. Prerequisite:
the **parser must first expose the per-layer compositing metadata it currently
drops** (operation/transparency/color/invert/transforms/active) — an additive
ABI extension on the Designer's critical path, not the importer's.

**Diagnostic shortcut (the time-saver).** A channel `image` beginning with `#`
is a LIE recipe id, *never* a file path. The importer gets the **base** from the
channel's `texture_path` and the **overlay ingredients** from the
`GetSceneMaterialChannelLayer*` accessors; the raw `#fragment` is neither, so if
it reaches `ImportOrFind` it logs a harmless "could not resolve '#…'". That is
cosmetic — every real texture is present and imported, and the surface renders.
Do not treat it as a missing asset.
