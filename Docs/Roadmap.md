# DsonToUnreal Roadmap & Status

This is the **single source of truth for project status**: what phase the
importer is at, what shipped in each, what was deliberately deferred, and the
open bug/cleanup backlog. It replaces the external "handoff" documents that went
stale between sessions — anything an agent or the maintainer needs to know about
*where the project stands* lives here, and is updated **in the same change that
moves it** (see `Docs/CodeReviewRules.md` R9).

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

_Last updated: 2026-06-07._

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
| 6 v2 | Materials v2 — faithful makeup + LIE import, then SSS Profile (current), eye-moisture | 🔄 In progress — slice #1 ✅ done (acceptance set verified 2026-06-07, incl. extra spot-checks); slice #2 (SSS Profile) — in verification: IrayUber SSS-binding fix ✅ (SetParentEditorOnly); PBRSkin darkening → translucency restore (B1) in progress |
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
   filter used for the IrayUber bump→normal decision (2026-06-06; full rationale
   in `Docs/DecisionLog.md`). If a DAZ feature requires runtime shader work that
   can't be made free-or-near-free, v1's approximation stands as the runtime answer.
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

1. **Faithful makeup + LIE import** — ✅ Done & signed off; implemented +
   Nancy 9-verified 2026-06-06, full acceptance regression passed 2026-06-07
   (implementation detail, parser-ABI accessors, and session log →
   `Docs/DecisionLog.md`). The importer imports `Makeup Base Color` textures and
   each non-base LIE layer as standalone `UTexture2D` assets under
   `/Game/DazImports/Textures/`. **No** `Makeup *` entries added to
   `GetPBRSkinMapping()`, **no** `Makeup *` parameters added to `M_DazPBRSkin` —
   per-surface makeup values (Enable/Weight/Roughness Mult) stay in the DAZ
   source for the future Designer plugin to consume directly via the parser.
   Folds in the sRGB-cache-conflict fix in `DsonTextureImporter`.
2. **Subsurface Profile pipeline** — generate per-character
   `USubsurfaceProfile` assets; rewire skin masters' subsurface input. Maps
   the SSS-family channels (`Sub Surface Enable`, `SSS Color`, `SSS
   Direction`, `Transmitted Color`, `Translucency *`, `Scattering Measurement
   Distance`) into the profile. Perf-neutral vs. inline subsurface.
   **Design:** [`SubsurfaceProfileV2.md`](SubsurfaceProfileV2.md) (both skin
   masters; IrayUber SSS via Option 1 — scene + calibrated defaults).
3. **Eye-moisture / cornea master** (`M_DazEyeMoisture`) — new translucent
   master + eye-surface detection + mapping. Translucent shading cost
   absorbed by the small pixel footprint of eyes (~1% on close-ups, much less
   normally).

### Slice #3 — note for the master rework

While reworking `M_DazPBRSkin` to wire the Subsurface Profile, audit it for
the same gated-but-evaluated-nodes pattern that motivated the IrayUber bump
cleanup (`Docs/DecisionLog.md`): parameters do not fold to zero at compile time;
gated branches still sample. Remove any cost-when-disabled paths in the same pass.

### Dropped from v2 — runtime cost > visual-fidelity gain

v1's approximation is the runtime answer; the full DAZ-faithful version is not
pursued for game runtime. Same framing as the IrayUber bump decision
(`Docs/DecisionLog.md`).

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

The **LIE (layered-image) composition recipe** the Designer must execute — the
ordered layer stack with per-layer blend ops, the worked Nancy-9 example, what
the importer does with it today, and the `#fragment` diagnostic shortcut — lives
in `Docs/Reference.md` → "LIE (layered-image) composition" (read it before
chasing any `#fragment` reference). Its stated prerequisite: the parser must
first expose the per-layer compositing metadata it currently drops — an additive
ABI extension on the Designer's critical path, not the importer's.

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

## Known latent issues (not blocking)

- `SavePackage` return value not checked (hardening).
- `IsValid()` does not include the UV function pointers — consistent with the
  permissive-parser convention (they are optional exports).
- **Benign `could not resolve '#…'` warning on LIE characters** (e.g. G9 Nancy):
  the channel `image` is a `#fragment` LIE composition-recipe id, not a file —
  see `Docs/Reference.md` → "LIE (layered-image) composition". Every real
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

## Next up

**Phase 6 v2 — Materials v2.** Active. **Slice #1 (faithful makeup + LIE import)
is done and signed off** — full acceptance regression passed 2026-06-07 (G8
Jordina, G8.1 base, G9 Laura, G3 Victoria 7 HD, plus extra spot-checks).
**Slice #2 (Subsurface Profile pipeline) — in verification (2026-06-07).** Two
findings (full detail → [`SubsurfaceProfileV2.md`](SubsurfaceProfileV2.md) §Revision):
(1) IrayUber SSS wasn't binding at import — **fixed** (parent via
`SetParentEditorOnly`), verified G8.1 + Josina; (2) PBRSkin rendered much darker —
the Subsurface Profile can't replace PBRSkin's removed inline translucency
(profile redistributes, doesn't add light). **Resolution B1 in progress:** keep the
profile for scatter, restore a tuned translucency into Base Color (importer re-feeds
raw params, master holds the tuning). **Next:** Implementer re-adds the PBRSkin
`Translucency *` mapping rows + user master re-wire (lockstep), then re-run
acceptance + finalize docs (§"Doc updates when the slice lands").

**Phase 7 v2 — formula evaluation/composed character shape** (queued behind
Phase 6 v2). The discovery-only portion is done: formula-reachable `?value`
files import their leaf morph targets at weight 0. Next is the evaluator/compose
feature in [`Docs/FormulaMorphsV2.md`](FormulaMorphsV2.md): channel
`current_value`, `min`/`max`/`clamped`, and a fragment-to-leaf bridge are still
needed before the importer can bake or emit a combined dialed character shape.
