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
| 7 | Morph targets (`UMorphTarget` per morph) | ✅ Done — delta-bearing morphs via MeshDescription morph attributes (position deltas; normals engine-recomputed) |
| 8 | Save to Content Browser (`/Game/DazImports/`) | ✅ Implemented per-phase, working |

## Phase 6 — what shipped (v1)

- Per scene-material `UMaterialInstanceConstant`, parented to one of three
  hand-authored masters in `Content/Materials/` (spec: `MaterialMastersV1.md`):
  `M_DazIrayUber` (G8/G8.1), `M_DazPBRSkin` (G9/Laura), `M_DazDefault` (fallback).
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
| Genesis 9 (Laura, Nancy) | ✅ | PBRSkin → `M_DazPBRSkin` | ✅ Supported, verified (LIE/makeup chars = base skin only) |
| Genesis 3 | ✅ imports | non-IrayUber shader, no mapping → falls back to `M_DazDefault` | ⚠️ Not supported in v1 (materials wrong) |

Genesis 3 geometry, skeleton, and skin weights import correctly, but G3 surfaces
use a different DAZ shader (not IrayUber) with no master/mapping yet, so its
materials fall back to the matte `M_DazDefault` and look wrong. Proper G3 material
support is a v2 item (see Deferred).

## Deferred to v2 (material follow-ups)

Source of record: `MaterialMastersV1.md` "Open follow-ups", confirmed in the 6.y
close-out.

- Eye / cornea / eye-moisture translucent master (`M_DazEyeMoisture`).
- Subsurface Profile pipeline (per-character `USubsurfaceProfile` instead of
  inline subsurface).
- PBRSkin makeup, transmission, SSS-direction / sub-surface-enable mappings.
- **Layered-image (LIE) compositing** — DAZ LIE images stack a base map plus
  overlay layers (makeup, brows, etc.) with blend operations. v1 uses only the
  base layer; overlays are not composited. Full support means compositing (or
  baking) the layer stack to a texture.
- Full dual-lobe specular implementation (v1 approximates with a single lobe).
- Clear-coat split masters (top-coat is approximated in v1).
- **Genesis 3 material support** — identify G3's surface shader and add a master +
  channel mapping for it. G3 geometry/skeleton/skin already import; only the
  material mapping is missing (currently falls back to `M_DazDefault`).
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
- **Formula-driven character/control morphs are not imported.** DAZ `_figure_ctrl_`
  characters such as Laura carry no direct deltas; they drive leaf morphs through
  multi-level `formulas`, which require formula evaluation beyond the parser's
  documented v1 boundary. Phase 7 imports only delta-bearing morphs: correctives,
  expressions, and directly stored shaping morphs.

## Known latent issues (not blocking)

- **Layered (LIE) images use the base layer only.** Characters whose surfaces
  reference layered `image_library` entries by `#fragment` (e.g. G9 "Nancy")
  import with the base skin map; makeup/brow/overlay layers are not composited.
  Not a failure — the surface renders correct base skin. (This was the G9
  white-head bug: the parser now percent-decodes the `#fragment` and parses the
  `map` array to resolve `texture_path` to the base layer, and the material
  builder uses `texture_path` when present, falling back to the raw image url.)
- **sRGB cache conflict** in `DsonTextureImporter`: the cache is keyed by resolved
  path, so the same image requested with two different sRGB flags returns the
  first-cached one (first-write-wins). Not the cause of any observed rendering
  bug. v2 fix: duplicate import with a disambiguating suffix.
- `SavePackage` return value not checked (hardening).
- `IsValid()` does not include the UV function pointers — consistent with the
  permissive-parser convention (they are optional exports).

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

## Carry-forward lessons (hard-won; don't relearn)

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

## Next up

**Post-Phase 7 hardening.** Build-verify the MeshDescription morph attribute path,
then decide whether scene dial current values should be applied as a character
shape bake or left to runtime controls.
