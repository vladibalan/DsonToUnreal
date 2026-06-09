# DsonToUnreal Roadmap & Status

This is the **single source of truth for project status**: what phase the
importer is at, what shipped in each, what was deliberately deferred, and the
open bug/cleanup backlog. It replaces the external "handoff" documents that went
stale between sessions — anything an agent or the maintainer needs to know about
*where the project stands* lives here, and is updated **in the same change that
moves it** (see `Docs/CodeReviewRules.md` R9).

This status is **subordinate to the Importer's governing principles**
(`Docs/Principles.md`) — when an item here conflicts with a principle, the principle
wins and the roadmap is what changes.

This file tracks *status* only. Three siblings own the rest, so each reads on its
own and none drifts into the others:
- *How the code is organized* → `Docs/ImporterArchitecture.md`.
- *Why shipped decisions were made* — dated postmortems and slice handoff history
  → `Docs/DecisionLog.md`. When a decision lands, its rationale goes there, not here.
- *Durable engineering facts, hard-won lessons, recurring gotchas* — the DAZ→UE
  coordinate flip, verified vert counts, the LIE recipe / `#fragment` diagnostic
  → `Docs/Reference.md`.
- *Material-master parameter contracts* → `MaterialMastersV1.md`.

Load-bearing invariants (coordinate flip, winding, scale) are owned by
`CodeReviewRules.md` R4 / the `DazPointToUe` helper (and restated in
`Docs/Reference.md`) — referenced here, never restated, so they cannot drift.

_Last updated: 2026-06-09._

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
| 6 v2 | Materials v2 — faithful makeup + LIE import, SSS Profile, eye-moisture | ✅ Done — slices #1 + #2 (2026-06-07) + #3 eye-moisture (runtime-verified G9 Nancy 2026-06-09) |
| 7 | Morph targets (`UMorphTarget` per morph) | ✅ Done — delta-bearing morphs, including formula-reachable `?value` leaf files, via MeshDescription morph attributes |
| 8 | Save to Content Browser (`/Game/DazImports/`) | ✅ Implemented per-phase, working |

## Phase 6 — what shipped (v1)

Per scene-material `UMaterialInstanceConstant` parented to one of three hand-authored
masters in `Content/Materials/` (spec: `MaterialMastersV1.md`): `M_DazIrayUber`
(G8/G8.1/G3), `M_DazPBRSkin` (G9/Laura), `M_DazDefault` (fallback). Shader detection:
URL fragment → `shader_type`; channel→parameter mapping in `DsonMaterialBuilder.cpp`
(`GetIrayUberMapping()`/`GetPBRSkinMapping()`); textures via `DsonTextureImporter`
(per-channel sRGB). IrayUber washy-skin fix: `TranslucencyWeight` (0.1) gates the
translucency tint + SSS map into Subsurface Color. Outputs — MICs
`/Game/DazImports/Materials/<basename>/MI_<sceneMatId>`, textures
`…/Textures/<mirrored DAZ path>/T_<filename>`. "v1" = acceptable on tested figures;
**Deferred to v2** items are knowingly out of scope.

## Figure / generation support

Figure support is gated by **shader**, not geometry: mesh + skeleton + skin
import is generation-agnostic, but a figure only renders correctly if its DAZ
shader has a matching master + channel mapping.

| Generation | Geometry / skeleton / skin | Materials | Status |
|---|---|---|---|
| Genesis 8 / 8.1 | ✅ | IrayUber → `M_DazIrayUber` | ✅ Supported, verified |
| Genesis 9 (Laura, Nancy) | ✅ | PBRSkin → `M_DazPBRSkin` | ✅ Supported, verified; makeup/LIE source textures import standalone. Companion figures import as separate `USkeletalMesh`es with MAT-preset MICs (Slice B+C ✅) — see "Genesis 9 companion figures" |
| Genesis 3 (Victoria 7 HD) | ✅ | IrayUber → `M_DazIrayUber` | ✅ Supported, verified |

## Phase 6 v2 — Materials v2

**Closes Phase 6 (Materials).** Picks up the deferred-from-v1 work under a
two-part filter:

1. **Runtime perf > visual fidelity.** Game-runtime cost decides; the same
   filter used for the IrayUber bump→normal decision (2026-06-06; full rationale
   in `Docs/DecisionLog.md`). If a DAZ feature requires runtime shader work that
   can't be made free-or-near-free, v1's approximation stands as the runtime answer.
2. **Content options preserved.** The importer never bakes away authoring choices
   (`Docs/Principles.md` P1). Source assets (`Makeup Base Color`, LIE layers) land
   as standalone `UTexture2D`s; combining them into a runtime Diffuse is
   interpretation — out of importer scope.

**Acceptance set** (same as v1): G8 Jordina Full Character + G8.1
Genesis8_1Female base, G9 Laura + Nancy, G3 Victoria 7 HD. For the master
parameter-contract format, `MaterialMastersV1.md` remains the source of record;
its "Open follow-ups" subset is now superseded by the slice list below.

Slices are sized to ship independently and are taken in order; each one updates
this section as it lands. **All three slices shipped — #3 (eye-moisture) runtime-verified on G9 Nancy 2026-06-09.**

### Planned slices

1. **Faithful makeup + LIE import** — ✅ Done & signed off; implemented +
   Nancy 9-verified 2026-06-06, full acceptance regression passed 2026-06-07
   (implementation detail, parser-ABI accessors, and session log →
   `Docs/DecisionLog.md`). The importer imports `Makeup Base Color` textures and
   each non-base LIE layer as standalone `UTexture2D` assets under
   `/Game/DazImports/Textures/`. **No** `Makeup *` entries added to
   `GetPBRSkinMapping()`, **no** `Makeup *` parameters added to `M_DazPBRSkin` —
   per-surface makeup values (Enable/Weight/Roughness Mult) are not baked into the
   material; they remain DAZ-source authoring data the importer does not yet surface.
   Folds in the sRGB-cache-conflict fix in `DsonTextureImporter`.
2. **Subsurface Profile pipeline** — ✅ **Done & verified 2026-06-07** (full
   acceptance set). Both skin masters → Subsurface Profile shading,
   `SubsurfaceWeight`→Opacity, per-character `USubsurfaceProfile` on skin. The two
   verification fixes (IrayUber SSS-binding; PBRSkin darkening, B1), the gated-but-
   evaluated-node master audit (cost-when-disabled paths removed), and the "profile
   redistributes, doesn't add light" finding →
   [`SubsurfaceProfileV2.md`](SubsurfaceProfileV2.md) §Revision + `DecisionLog.md`.
3. **Eye-moisture / cornea master** (`M_DazEyeMoisture`) — ✅ **Done; runtime-verified
   on G9 Nancy 2026-06-09.** `EyeMoisture L/R` / `Cornea` / `Tear` route to a translucent
   `M_DazEyeMoisture` (Surface ForwardShading; Fresnel-weighted opacity). Importer:
   `EDsonSurfaceClass::EyeMoisture` + `GetEyeMoistureSurfaceGroups()` (single source,
   removed from NonSkin); `GetEyeMoistureMapping()`
   (BaseColor/Specular/Roughness/RefractionIOR/Opacity); channels read from
   `material_library` via the scene-material's bare `#fragment` (`ResolveChannelSource`
   — no parser change); key-0 matId reconciliation (UrlDecode + strip `-<n>` suffix).
   Master spec → `MaterialMastersV1.md`; resolution detail → `DecisionLog.md` "Slice #3
   heads-up". **Note:** the now-transparent shell exposed the **untextured G9 eyeball**
   (`Eye L/R`, PBRSkin) — separate, see Known issues.

### Dropped from v2 — runtime cost > visual-fidelity gain

v1's approximation stands as the runtime answer (same framing as the IrayUber bump
decision → `Docs/DecisionLog.md`):
- **Full dual-lobe specular** (second GGX/pixel) and **clear-coat split** (clear-coat
  GGX + transmission) — v1's single-lobe / tinted-top-coat approximations stand;
  parameter detail in `MaterialMastersV1.md`.
- **Metallic Flakes (skin)** — non-trivial procedural-noise ALU; no current content
  needs it (Nancy ships flakes at weight 0). A future strong-flakes asset → handle
  per-character outside the default skin pipeline.

### Out of importer scope (interpretation / authoring)

Some DAZ-faithful results require *interpreting* the imported data — composition,
baking, assembly — which is authoring, not translation, so the importer does not do
it (`Docs/Principles.md` P1). These land as faithful **source** for a later authoring
step, never as a finished result:

- **Diffuse composition / bake-out.** The imported `Makeup Base Color` and LIE layers
  are standalone `UTexture2D`s; compositing them into a runtime Diffuse and rebinding
  the MIC is an authoring step. Originals stay untouched (P5) so variants stay possible.
- **Per-surface makeup values** (Enable/Weight/Roughness Mult) — DAZ-source authoring
  data, surfaced faithfully rather than interpreted.

The **LIE (layered-image) composition recipe** — the ordered layer stack with
per-layer blend ops, the worked Nancy-9 example, what the importer does with it today,
and the `#fragment` diagnostic — lives in `Docs/Reference.md` → "LIE (layered-image)
composition" (read it before chasing any `#fragment` reference). Bringing the recipe
across faithfully is blocked on an additive parser exposure of the per-layer
compositing metadata the parser currently drops — taken when a concrete need lands
(`Docs/Principles.md` P4), not yet.

### Parked — revisit if content needs it

- **True multi-tile UDIM** — one material/section whose UVs span multiple tiles
  needing *different* textures (VT/atlas territory). This is **not** the
  per-section integer `UVTileOffset` that was built and reverted: under UE Wrap
  addressing `frac(u−n) = frac(u)`, so an integer offset is a visual no-op.
  Current content does not need it (DAZ ships each skin zone as its own 0–1
  section).

## Genesis 9 companion figures (eyes / mouth / eyelashes / tear) — importer work in progress

G9 declares eyes, mouth (teeth), eyelashes, and tear as separate conforming figures in the
preset's `scene.extra → PostLoadAddons`, **not** `scene.nodes`, so only the body imports
today — chain + per-figure data in [`Reference.md`](Reference.md) → "Genesis 9 companion
figures". **Packaging: separate `USkeletalMesh` per companion, leader-posed to the body
skeleton** (not merged) — rationale in [`DecisionLog.md`](DecisionLog.md). Work, in order:

1. **Parser ABI** — ✅ done (DsonParser 1.1.0): `DsonDocument_GetScenePostLoadAddon{Count,Slot,AssetName,AssetFile,MatPreset}`, paths only.
2. **Slice A — ✅ done** (2026-06-08): 5 PostLoadAddon exports bound (optional); each `AssetFile` resolved → loader .duf → geometry DSF + node id into `FDsonCompanionSource` list; logged. No meshes built.
3. **Slice B — ✅ done** (2026-06-08): each companion geometry DSF imported as its own
   `USkeletalMesh` via `FDsonMeshBuilder::BuildCompanion`, bound to body `USkeleton` by bone name; `FDsonImportResult.CompanionMeshes`; UV-set DSF resolved and applied like body (was zero-UV, single-texel — fix 2026-06-08). No materials (Slice C).
4. **Slice C — ✅ done** (2026-06-08): `BuildAllSceneMaterials` on each addon's MAT preset;
   MICs keyed by group name wired to companion sections via updated `BuildCompanion`; R7
   fallback to `M_DazDefault` per section. `EyeMoisture` `#fragment` channels resolve empty
   (expected — slice #3 owns the master).

**Deferred:** fiber eyebrows (`G9EyebrowFibers`) → groom; some characters (Nancy) have no
brow mesh. **Unblocks** slice #3 on G9 (`EyeMoisture Left/Right` live only in the Eyes companion).

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

## Known latent issues (not blocking)

- `SavePackage` return value not checked (hardening).
- `IsValid()` does not include the UV function pointers — consistent with the
  permissive-parser convention (they are optional exports).
- **Benign `could not resolve '#…'` warning on LIE characters** (e.g. G9 Nancy):
  the channel `image` is a `#fragment` LIE composition-recipe id, not a file —
  see `Docs/Reference.md` → "LIE (layered-image) composition". Every real
  texture still resolves and imports; cosmetic only. Cleanup: have the texture
  importer skip `#`-prefixed refs before resolving.
- **G9 eyeball (`Eye L/R`) imports untextured (grey)** — surfaced once slice #3 made the
  eye-moisture shell transparent. The generic Genesis 9 Eyes preset's `Eye L/R` PBRSkin
  material has no textures; the real iris/sclera detail likely comes from a character
  file/override the companion import doesn't apply. Separate from eye-moisture; own
  Director ticket open.

## Cleanup backlog

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
- **Consolidate the duplicated subsystem→file routing** in `AGENTS.md` (Task Routing)
  and `ImporterArchitecture.md` ("Common Change Areas") — two overlapping tables.
  Deferred from the 2026-06-09 doc-diet pass as a structural call (which doc owns
  routing); rationale in `Docs/DecisionLog.md`.

## Next up

**Phase 7 v2 — formula evaluation / composed dialed shape** (queued; discovery-only
portion done — see "Deferred to v2" → [`FormulaMorphsV2.md`](FormulaMorphsV2.md)).
Phase 6 v2 (Materials v2) closed with slice #3 (eye-moisture) runtime-verified on G9
Nancy 2026-06-09; the open thread off it is the untextured G9 eyeball (Known issues).
