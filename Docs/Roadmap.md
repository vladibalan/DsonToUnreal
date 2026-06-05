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

_Last updated: 2026-06-05._

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
| 7 | Morph targets (`UMorphTarget` per morph) | ⬜ Not started — **next** |
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

## Deferred to v2 (material follow-ups)

Source of record: `MaterialMastersV1.md` "Open follow-ups", confirmed in the 6.y
close-out.

- Eye / cornea / eye-moisture translucent master (`M_DazEyeMoisture`).
- Subsurface Profile pipeline (per-character `USubsurfaceProfile` instead of
  inline subsurface).
- PBRSkin makeup, transmission, SSS-direction / sub-surface-enable mappings.
- Full dual-lobe specular implementation (v1 approximates with a single lobe).
- Clear-coat split masters (top-coat is approximated in v1).
- **True multi-tile UDIM** — one material/section whose UVs span multiple tiles
  needing *different* textures (VT/atlas territory). This is **not** the
  per-section integer `UVTileOffset` that was built and reverted: under UE Wrap
  addressing `frac(u−n) = frac(u)`, so an integer offset is a visual no-op.
  Current content does not need it (DAZ ships each skin zone as its own 0–1
  section).

## Known latent issues (not blocking)

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

**Phase 7 — Morph targets.** One `UMorphTarget` per morph. Morph position deltas
must go through the **same det−1 coordinate flip as vertices** (`DazPointToUe`) or
morphs mirror relative to the mesh. Parser API is present (`GetMorphCount`,
`GetMorphDeltaCount`, `GetMorphDeltaVertexIndex`, `GetMorphDeltaX/Y/Z`, plus
normal deltas).
