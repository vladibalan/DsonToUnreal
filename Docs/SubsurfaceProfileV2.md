# Materials v2 — Slice #2: Subsurface Profile Pipeline (Design)

Design spec for **Phase 6 v2, slice #2**. Replaces the inline subsurface
approximation on the two skin masters with UE **Subsurface Profile** scattering.
Status lives in [`Roadmap.md`](Roadmap.md) (Phase 6 v2 → Planned slices);
rationale/postmortems land in [`DecisionLog.md`](DecisionLog.md) and durable DAZ
facts in [`Reference.md`](Reference.md) **as the slice lands** — this doc is the
working design the manual master-rework and the Implementer prompt are cut from.

_Authored 2026-06-07 (Director); **slice landed & verified 2026-06-07** — see
§Revision below + `Roadmap.md`. Values marked "calibrate" were starting points
tuned on the figure, not asserted truth._

## Decisions locked (with the user, 2026-06-07)

1. **Scope: both skin masters** — `M_DazPBRSkin` (G9) and `M_DazIrayUber`
   (G8/G8.1/G3). `M_DazDefault` unchanged. Eye-moisture/cornea is **slice #3**,
   out of scope here.
2. **IrayUber SSS sourcing: Option 1 — scene + calibrated skin defaults**, *not*
   library-faithful. Why: a `USubsurfaceProfile` is **constant per material** and
   cannot sample DAZ's per-pixel SSS texture, so the per-character scatter
   *scalars* are the only thing library reads would add; runtime perf is
   **identical** either way (see "Perf" below); and that fidelity gain is small
   and not free (extra scene→library resolution + an unverified parser behavior).
   Per the perf-first / "approximation stands" principle, v1-style approximation wins.
3. **One `USubsurfaceProfile` per character** (per imported DUF), shared by that
   character's skin surfaces.
4. **Per-character tint is in scope** — the profile is tinted toward the
   character's scene skin color, not left as a single untinted skin preset
   (confirmed with the user 2026-06-07).

## Revision — PBRSkin translucency restored (2026-06-07, decision **B1**)

Slice-2 verification surfaced two issues; this revises the plan for the second.

1. **IrayUber SSS didn't bind at import** (rendered default-lit until a manual MIC
   param toggle). Root cause: the MIC's cached shading model lagged the
   render-proxy propagate on a raw-parented MIC, so the subsurface branch was
   skipped. Fixed by parenting via `SetParentEditorOnly(Master, /*RecacheShader=*/true)`
   (durable note in `Reference.md` → carry-forward lessons). ✅ verified G8.1 + Josina.
2. **PBRSkin (G9 Laura/Nancy) rendered much darker.** Cause: removing the inline
   `Translucency → Subsurface Color` term — which on PBRSkin (weight ~0.85) was the
   *active* skin-brightness path, not IrayUber's weak 0.1 washy gate. The Subsurface
   Profile **redistributes** diffuse light (≈energy-conserving); it does **not add**
   luminance, so it cannot replace that brightness. Confirmed by a constant-
   Subsurface-Color injection test, then a DAZ Iray reference: the correct look sits
   **between** v1 (full translucency, too bright) and profile-only (too dark).

**Resolution (B1):** keep the Subsurface Profile for *scatter*; restore a
**tuned-down** `TranslucencyColor × TranslucencyMap × TranslucencyWeight`
contribution routed into **Base Color** for *brightness* (the profile model has no
Subsurface Color pin). The two are complementary, not redundant. The importer
re-feeds the **raw** DAZ translucency params (faithful data pump); the **tuning
scale lives in the master**, dialed against the DAZ Iray reference. **IrayUber is
unchanged** (its translucency removal stands). This **supersedes** Master-rework
spec #3 and Code plan #3 *for PBRSkin only*, and the "Doc updates when the slice
lands" `MaterialMastersV1` item (PBRSkin keeps translucency, now → Base Color, tuned).

## DAZ data facts (from real `[MatDiag]` channel dumps, 2026-06-06/07 logs)

Exact channel ids, verified — do not paraphrase:

| Concept | PBRSkin (`studio/material/daz_brick`) | IrayUber (`studio/material/uber_iray`) |
|---|---|---|
| Translucency | `Translucency Enable` / `Translucency Weight` / `Translucency Color` | `Translucency Weight` / `Translucency Color` / `Base Color Effect` (enum) |
| Transmission | `Transmitted Color` / `Transmitted Measurement Distance` | `Transmitted Color` / `Transmitted Measurement Distance` |
| SSS section | `Sub Surface Enable` / `SSS Color` / `Scattering Measurement Distance` / `SSS Direction` | `SSS Mode` (enum) / `SSS Amount` / `SSS Color` / `SSS Reflectance Tint` / `Scattering Measurement Distance` / `SSS Direction` |

Two structural findings that drive the design:

- **PBRSkin: the SSS section is inert.** On Nancy, both Head and nails show
  `Sub Surface Enable=0` with default `SSS Color=(0.996,0.780,0.659)`,
  `Scattering Measurement Distance=0.001`, `SSS Direction=-0.5`. The **active**
  subsurface path is **Translucency** (`Translucency Weight=0.85`,
  `Translucency Color` carrying a `*_sss` map, `Transmitted Measurement Distance=0.03`).
  Scene materials are **full** (~50 channels) → all readable via existing
  `GetSceneMaterialChannel*`.
- **IrayUber: scene overrides are sparse; SSS lives in the library material.**
  Victoria 7 HD's scene "Face" has **5** channels only: `diffuse`,
  `Translucency Color` = `(0.702,0.176,0.004)` + a `*SSS*` map, `Glossy Layered
  Weight`, `Bump Strength`, `Top Coat Weight`. The SSS-section channels are **not**
  in the scene — they sit in the `.duf` library material. The scene **reliably**
  carries `Translucency Color` (a reddish skin scatter tint + map); that is our
  IrayUber tint source under Option 1.

**UE constraint (bounds both shaders).** `FSubsurfaceProfileStruct` parameters
are constant per material; the profile **cannot** sample the DAZ `*_sss` /
translucency texture. Per-pixel SSS-color variation is therefore *not* carried by
the profile — in UE that variation comes from Base Color. Expected, not a defect.

## UE mechanism (verified, 5.4 source)

- **Shading model:** `Subsurface Profile` (replaces today's *Default Lit +
  Subsurface*).
- **Asset:** `USubsurfaceProfile` holding `FSubsurfaceProfileStruct Settings`
  ([SubsurfaceProfile.h](../../../UE_5.4/Engine/Source/Runtime/Engine/Classes/Engine/SubsurfaceProfile.h)).
- **Per-MIC assignment:** `MIC->bOverrideSubsurfaceProfile = true;`
  `MIC->SubsurfaceProfile = Profile;` (the pointer is the `UMaterialInterface`
  member; the override bool is on `UMaterialInstance`). Followed by the existing
  `PostEditChange()` in `BuildSceneMaterial` step 6.
- **Opacity = per-surface SSS strength.** `ShadingModelsMaterial.usf` stores
  `GBuffer.CustomData.a = Opacity` for the profile model, and **reverts the pixel
  to the default lit model when `Opacity ≤ ε`**. So a single shared master can
  carry both skin (Opacity high) and non-skin (Opacity 0 → no SSS, no cost).

**Perf.** Both Option 1 and Option 2 emit the *same* runtime artifact — a
constant profile + Subsurface Profile shading model. Per-frame SSS cost depends
on shading model, the fixed screen-space SSS pass, and on-screen skin coverage —
**not** on the profile's values or how they were sourced. Profiles are a tiny
load-time LUT. Net runtime delta between the options: **zero**. Versus v1, this
slice is perf-**neutral-to-positive** (removing gated-but-evaluated inline
subsurface nodes; non-skin reverts to default lit).

## The mapping (DAZ → `FSubsurfaceProfileStruct`)

Start from UE's built-in Burley **skin** defaults (the struct's constructor is
already skin-tuned), set `bEnableBurley = true`, then calibrate:

| Profile field | Starting value | Source / calibration |
|---|---|---|
| `bEnableBurley` | `true` | Modern normalized model. |
| `SurfaceAlbedo` | `(0.911, 0.338, 0.272)` (UE default) | Keep, or gently tint toward the character skin color (below). |
| `MeanFreePathColor` | `(1.0, 0.089, 0.072)` (UE default) | The reddish skin falloff; primary tint target. |
| `MeanFreePathDistance` | ~`2.6` (UE default) | **Calibrate on the figure** — DAZ measurement-distance units are not asserted; tune visually. |
| `WorldUnitScale` | `0.1` (UE default) | Keep unless scale reads wrong. |
| `Tint` | `(1,1,1)` | Optional per-channel mix; leave neutral initially. |

**Per-character tint (in scope):** tint the profile toward the character's scene
skin-scatter color — PBRSkin `SSS Color` (fallback `Translucency Color`),
IrayUber scene `Translucency Color` — read from a representative skin surface
(prefer group `Face`, else `Head`, else `Torso`/`Body`). Apply gently (e.g. lerp
~30–50% into `MeanFreePathColor`) and **clamp** to avoid oversaturated raw values
(V7HD's `(0.70,0.18,0.004)` is too strong raw). The mix **strength** is the only
calibration knob here — if a character oversaturates, dial the lerp down rather
than dropping the tint. Tune on the **figure in the skeletal-mesh editor**, never
the MIC preview sphere ([Reference.md](Reference.md)).

**Per-surface SSS strength → Opacity (`SubsurfaceWeight`):**
- **Skin** surfaces: full strength (from PBRSkin `Translucency Weight` where
  present, else a calibrated skin default ~0.8).
- **Non-skin** surfaces: `0` (reverts to default lit; no SSS).

## Skin vs non-skin classification

Gate by **surface group name** (signal "A"), against **two maintained sets** —
the zone names are stable across G3/G8/G9 (confirmed in the dumps). For each
surface on a skin master (PBRSkin/IrayUber):

- **Skin set → SSS on** (`Opacity` from skin weight, profile assigned):
  `Face`, `Head`, `Body`, `Torso`, `Arms`, `Legs`, `Ears`, `Lips`.
- **Known-ignored set → SSS off**, *silently* (`Opacity 0`, no profile):
  `Teeth`, `Mouth`, `Mouth Cavity`, `Tongue`, `EyeSocket`, `EyeMoisture`,
  `Cornea`, `Pupils`, `Irises`, `Sclera`, `Eyelashes`, `Fingernails`, `Toenails`.
- **In neither set (unrecognized) → SSS off + WARN.** Default to no-SSS — the
  safe bias (a non-scattering surface is invisible; a wrongly-scattering one is
  jarring) — **and** `UE_LOG(LogDsonImporter, Warning, …)` naming the group, so
  the maintainer can triage it into the skin set or the ignored set afterward.

Two explicit `static const TSet<FString>&` lists (mirroring the existing
`GetStandaloneImageChannelIds()` pattern) keep the policy auditable and the
warning meaningful: once a group is sorted into a list it stops warning. **Warn
once per distinct unrecognized group** (dedupe via a small builder-held
`TSet<FString>`) to avoid per-surface spam. Sets hold the verbatim DAZ group
strings; match is exact. The warning is the maintenance loop — odd/third-party
figures surface their unknown zones instead of silently mis-shading.

## Master-rework spec (MANUAL — user, in the UE Material Editor)

Applies to **both** `M_DazPBRSkin` and `M_DazIrayUber`. The Implementer cannot
edit `.uasset`; this precedes or accompanies the code change, and the two must
land together (the mapping/master contract is breaking — see Code plan #3).

1. **Shading model → `Subsurface Profile`** (Blend mode stays `Opaque`).
2. **Add scalar param `SubsurfaceWeight`** (default `0`) → wire to the material
   **Opacity** output. This is the per-surface SSS gate.
3. **Remove the inline subsurface wiring** where dead under the profile model:
   - PBRSkin: **superseded — see Revision (B1).** Keep a *tuned* translucency term,
     re-routed `→ Base Color` (the profile model exposes no Subsurface Color pin).
   - IrayUber: remove the `TranslucencyColor × TranslucencyMap → Subsurface Color`
     path **and** the `TranslucencyWeight` washy-skin gate (stands).
4. **Audit & remove gated-but-evaluated nodes** (this *is* the work the Roadmap's
   mislabeled "Slice #3 — note for the master rework" describes — see Roadmap fix
   below): no SSS branch should sample/compute when disabled. Same pattern as the
   IrayUber bump cleanup ([DecisionLog.md](DecisionLog.md)).
5. Leave Base Color, normal, roughness, specular, AO, top-coat paths as they are.

## Code plan (Implementer — `DsonMaterialBuilder`)

1. **Build one `USubsurfaceProfile` per character.** Once per
   `BuildAllSceneMaterials` run: pick the representative skin surface, read its
   tint color, fill `FSubsurfaceProfileStruct` per the mapping, `NewObject` +
   save via `FDsonAssetUtils::CreateLoadedPackage`/`SaveAssetPackage` (same
   pattern as MIC/texture). Path `/Game/DazImports/Materials/<basename>/SSP_<basename>`.
   Cache the pointer on the builder (like `FDsonTextureImporter` caches).
2. **In `BuildSceneMaterial`,** for the two skin shader kinds: classify the
   surface against the two group sets (skin / known-ignored / unrecognized→warn,
   per §"Skin vs non-skin classification"); set `SubsurfaceWeight` (skin →
   weight, otherwise 0); for skin surfaces assign the profile
   (`bOverrideSubsurfaceProfile` + `SubsurfaceProfile`). Hold a
   `TSet<FString>` of already-warned groups on the builder for dedupe.
3. **Mapping rows.** IrayUber: **remove** the `Translucency *` rows from
   `GetIrayUberMapping()` (stands). PBRSkin: **re-add** to `GetPBRSkinMapping()` the
   `Translucency Color` (→ `TranslucencyColor` + `TranslucencyMap` +
   `UseTranslucencyMap`, sRGB on) and `Translucency Weight` (→ `TranslucencyWeight`)
   rows, feeding **raw** DAZ values — tuning lives in the master (B1, see Revision).
   ⚠️ **Breaking change** to the mapping↔master contract (CodeReviewRules R2/R7): the
   PBRSkin master re-wire and these rows land in lockstep; do not ship one without the
   other.
4. Keep all parser reads through existing accessors — **no parser change** (Option
   1 needs only scene channels, which carry what we use).

## Verification

- Acceptance set: **G8 Jordina, G8.1 base, G9 Laura, G9 Nancy, G3 Victoria 7 HD.**
- On the **figure in the skeletal-mesh editor** (not the sphere): backlit skin
  (ears/nose/fingers) scatters; **teeth/nails/eyes do not** (Opacity 0 → default
  lit); no perf regression; no shading creases.
- **PBRSkin skin brightness** matches the DAZ Iray reference — between v1 (full
  translucency, too bright) and profile-only (too dark). Tune the master
  translucency scale against an **Iray render** (not the Texture-Shaded viewport).

## Open items (resolve during implementation)

- Calibrated `MeanFreePathDistance` and per-character tint strength — visual.
- Skin / ignored group-set coverage across the acceptance figures — the
  unrecognized-group warning surfaces any gaps to triage (expect zero warnings on
  the acceptance set; any that appear get sorted into a list).
- Optional: import the now-unbound translucency/`*_sss` texture **standalone**
  (like makeup/LIE) so it survives for the Designer — defer unless cheap.

## Doc updates when the slice lands (Implementer, same change — R8/R9)

- **Roadmap.md:** slice #2 → done; **fix the mislabel** — the "Slice #3 — note
  for the master rework" block describes *this* slice's `M_DazPBRSkin`/IrayUber
  rework, not slice #3 (eye-moisture).
- **MaterialMastersV1.md:** both skin masters → shading model `Subsurface
  Profile`, drop the translucency-subsurface params, add `SubsurfaceWeight`
  (→Opacity), note SSS is profile-driven.
- **DecisionLog.md:** the Option-1 decision + the scene-vs-library finding +
  perf-parity rationale.
- **Reference.md:** durable DAZ facts — where SSS lives per shader; PBRSkin SSS
  section inert / Translucency is the active path; UE "Opacity gates SSS and
  reverts to default lit at 0".
