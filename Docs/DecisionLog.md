# DsonToUnreal Decision Log

Cold archive of **dated decisions, postmortems, and session handoff history**
extracted from `Docs/Roadmap.md`. Read this when you need the *why* behind a
shipped decision, or the detail of how a slice landed — it is **not** needed for
current project status (see `Docs/Roadmap.md`) or for durable engineering facts
(see `Docs/Reference.md`). As work lands, the rationale is appended here and the
Roadmap keeps only the one-line outcome plus a pointer back.

Contents (newest decisions appended):
- IrayUber bump-map seam — root cause & fix decision (2026-06-06)
- Materials v2 slice #1 (faithful makeup + LIE import) — handoff & session log (2026-06-06 → 2026-06-07)
- Materials v2 slice #2 (Subsurface Profile pipeline) — decisions & verification (2026-06-07)

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

## Materials v2 slice #1 (faithful makeup + LIE import) — handoff & session log

_Slice summary and current status live in `Docs/Roadmap.md` (Phase 6 v2 → Planned
slices). This is the implementation detail, parser-ABI record, and session log,
2026-06-06 → signed off 2026-06-07._

### Decomposition & sequencing

The parser-surface investigation the previous note called for is **done**
(2026-06-06). Slice #1 decomposes into three independent parts; only one
needs the parser, and that one was the gating item.

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

**Sequencing (2026-06-06): parser-first — parser done; plugin side done.** The
DsonParser-repo change shipped; the rebuilt `DsonParser.dll`/`.lib`/header are in
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

**Verification asset.** HID Nancy 9 (`D:/Daz_content/People/Genesis 9/
Characters/HID Nancy 9.duf`) carries both PBRSkin `Makeup *` channels
(every surface) and a LIE-based head diffuse — confirmed during planning.
Used as the slice's primary verification figure; the rest of the acceptance set
was retested to confirm no regression on figures without makeup/LIE.

### Session handoff (2026-06-06 → 2026-06-07) — status & open items

- **Implemented; Nancy 9 was the primary end-to-end check.** End-to-end on HID
  Nancy 9: base skin binds (head renders correctly), every non-base LIE layer
  imports as a standalone `T_…_lie_<idx>[_label]` asset (incl. `makeup_02` +
  `brows_base`, cross-checked against the on-disk `lie/` folder), and no sRGB
  conflicts.
- **Acceptance-set regression complete (2026-06-07) — slice #1 signed off.** All
  acceptance figures import clean — **G8 Jordina, G8.1 base female, G9 Laura, G3
  Victoria 7 HD** — plus extra figures spot-checked by the maintainer. G8.1 was
  the only one to log warnings, resolved by the bump-bake TIFF fix below. Slice
  #2 (SSS Profile) is now the active work.
- **G8.1 acceptance regression bug fixed 2026-06-07.** The IrayUber bump bake
  now decodes DAZ's LZW-compressed 8-bit RGB TIFF base-normal maps by requesting
  BGRA8 from UE's image wrapper (`DsonTextureImporter.cpp::DecodeImageFile`).
  Previously the RGBA request failed on TIFF, so G8.1 fell back to a plain
  normal map, lost baked bump detail, and inflated "failed to decode normal map"
  warnings/failure counts.
- **One open follow-up — the benign `#fragment` warning** (cosmetic; see
  `Docs/Roadmap.md` "Known latent issues" + `Docs/Reference.md` "LIE
  (layered-image) composition"). A ready-to-use Implementer prompt was drafted:
  skip `#`-prefixed refs in `ImportOrFind` + a Verbose diagnostic to capture
  origin. The exact call passing the raw `#fragment` was never pinned statically
  (all importer paths prefer the resolved `texture_path` / real layer urls), but
  the fix is safe regardless — a `#`-ref is never a file. Backlog, or fold in
  before slice #2.

## Materials v2 slice #2 (Subsurface Profile pipeline) — decisions & verification (2026-06-07)

**Shipped.** Both skin masters (`M_DazPBRSkin`, `M_DazIrayUber`) → **Subsurface
Profile** shading; `SubsurfaceWeight`→Opacity per-surface SSS gate (skin → DAZ
`Translucency Weight`, else 0 → reverts to default lit); one per-character
`USubsurfaceProfile` (tinted toward scene skin colour) assigned to skin surfaces.
Locked design decisions (Option 1 sourcing; one profile/character; per-character
tint) → `SubsurfaceProfileV2.md`. Verified on the full acceptance set (G8.1,
Jordina, Nancy, Laura, V7HD).

**Verification fix 1 — IrayUber SSS didn't bind at import.** Imported skin rendered
default-lit until a manual MIC param toggle. Root cause: the MIC's cached shading
model lagged the render-proxy propagate on a raw-parented MIC, so
`UpdateMaterialRenderProxy`'s subsurface branch (gated on
`UseSubsurfaceProfile(GetShadingModels())`) was skipped. Fixed by parenting via
`SetParentEditorOnly(Master, /*RecacheShader=*/true)`. Full mechanism →
`Reference.md` (carry-forward lessons).

**Verification fix 2 — PBRSkin much darker (decision B1).** Removing PBRSkin's
inline `Translucency → Subsurface Color` term darkened the skin: on PBRSkin that
term (weight ~0.85) was the *active* skin-brightness path (its `Sub Surface`
section is inert), unlike IrayUber's weak 0.1 washy gate. **Finding:** the UE
Subsurface Profile *redistributes* diffuse light (≈energy-conserving) — it does
**not add** luminance, so it cannot replace an additive translucency term (toggling
the profile changed only the pink hue, not brightness). A DAZ Iray reference placed
the correct look *between* v1 (full translucency, too bright) and profile-only (too
dark). **Decision B1:** keep the profile for *scatter*; restore a **tuned-down**
translucency contribution routed into **Base Color** for *brightness* (the profile
model exposes no Subsurface Color pin). The importer re-feeds the **raw** DAZ
translucency params (`GetPBRSkinMapping()` rows restored); the **tuning scale lives
in the master**. IrayUber's translucency stays removed (negligible contribution).
- **Perf.** B1 adds ~1 texture sample + a multiply per skin pixel, skin-only —
  modest by the bump-decision yardstick (which baked out ~4 samples/skin-pixel).
- **Per-character brightness → MIC, not master.** Laura renders slightly darker
  than Nancy and is *also* darker in DAZ Iray → faithful, left as-is. The master
  holds the one global tuning `Scale`; per-character nuance comes from each MIC's
  own DAZ values (or a manual MIC nudge), never by bending the master per figure.
