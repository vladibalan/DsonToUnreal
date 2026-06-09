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
- Parser version-awareness gate — consume DsonParser versioning; keep the four ABI checks (2026-06-07)
- Genesis 9 companion figures — packaging decision (separate meshes, leader-pose) + import plan (2026-06-08)
- Director/Implementer handoff — file-based `.handoff/`, Director-defers, option D doc fold (2026-06-08)
- Importer scope codified — bring-everything, translate-don't-interpret, consumer-agnostic docs (2026-06-09)

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
transforms) stays in the parser model, **deferred** — out of importer scope (composition is
interpretation), not exposed and not this slice.

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

## Parser version-awareness gate & why the ABI checks all stay (2026-06-07)

_Why the load-time version gate was added, and why none of the existing parser-ABI
detection layers were pruned alongside it — including the specific "an LLM wrote it,
so drop the static_assert" argument, which is a trap. Change status is at the end._

**Context.** DsonParser shipped a versioning system (parser v1.0.0): compile-time
`DSONPARSER_VERSION_*` macros in the vendored `DsonParserVersion.h`, a runtime
`DsonParser_GetVersion()` export, and a SemVer-with-C-ABI `CHANGELOG.md` (MAJOR =
breaking ABI, MINOR = additive/binary-compatible, PATCH = internal). The plugin was
unaware of it — the export was unbound and nothing checked a version.

**Decision — add a load-time version gate, hard on MAJOR.** Bind
`DsonParser_GetVersion` as one *optional* `DSON_PARSER_API_LIST` row; in
`DsonImporter.cpp`, after `IsValid()`, compare the loaded DLL's version to the
compile-time `DSONPARSER_VERSION_*` the plugin built against:
- MAJOR ≠ built-against MAJOR → Error + skip importer registration (same early-return
  as an `IsValid()` failure; the ABI may have broken);
- same MAJOR, different MINOR/PATCH → Warning + proceed (binary-compatible);
- version missing (pre-1.0.0 DLL) or unparseable → Warning + proceed (cannot verify).

**Why MAJOR-only / warn-on-minor.** Mirrors the SemVer-C-ABI contract: a MAJOR bump
is the one case where the ABI is allowed to break, so it is the one worth refusing;
MINOR/PATCH are binary-compatible, so warn-not-fail — which also keeps dev iteration
unbricked (a header minor-bump never refuses the load). Optional (not in `IsValid()`)
so a pre-versioning DLL gives a precise "predates versioning" message, not the generic
required-exports failure.

**Why the gate does not replace the compile-time ABI check.** The property that
matters is **binding ↔ DLL**; neither half proves it alone. `DsonParserAbiCheck.cpp`
proves **binding ↔ vendored header** (compile-time); the gate proves **header ↔ DLL**
for the shared MAJOR (runtime, by contract). Chained → **binding ↔ DLL**. Before the
gate, the static_assert silently *assumed* header == DLL (they are vendored
side-by-side); the gate **verifies** that at load. They compose — the gate is the
missing link, not a replacement.

**Decision — keep all four existing detection layers; none is redundant, including
under LLM authorship.** A future cold session may argue "an LLM authored this, typos
≈ 0, prune the static_assert." Trap: these layers guard **drift between
independently-maintained artifacts**, not human clumsiness — and that drift is
unchanged, or *worsened*, by LLM authorship (each session starts cold, the parser is a
separate repo, the DLL is copied in by a manual build step).

| Layer (when; against what) | Guards against | Authorship-agnostic because |
|---|---|---|
| `DsonParserAbiCheck.cpp` static_assert (compile; binding↔header) | wrong return/param type, order, count, or a name the header doesn't declare, in an X-macro row | dominant fault is cross-repo / cold-session header↔binding desync; the residual LLM fault is *confident-plausible* mis-transcription — what human review skims past and a mechanical check catches deterministically; cost 0 |
| per-export bind Error/Warning — `DsonImporter.cpp` (load; DLL) | a symbol genuinely absent from the shipped DLL | stale-DLL packaging (manual rebuild + copy) desyncs regardless of who wrote the source |
| `IsValid()` — `DsonParserFunctions.h` (load + per stage; DLL) | the *required* subset actually being present | free aggregation of the above; hard gate before any import runs |
| call-site presence guards (per feature; e.g. `DsonMorphBuilder`) | an optional feature's exports missing → skip that feature cleanly | the same-MAJOR-lower-MINOR degradation path + the R7 permissive convention; the gate only *warns* on that skew, it null-guards nothing |

**Cost — near-zero, none in the game-runtime path.** static_assert is compile-time
only (emits no code, auto-extends per X-macro row); presence binding is ~110
`GetProcAddress` + pointer compares once at editor module load; call-site guards are
predicted branches on the editor-time import path. There is no perf basis for pruning
any of them.

**Status.** ✅ Done & verified 2026-06-07. Compiles (the `DsonParserAbiCheck.cpp`
static_assert for the new `GetVersion` row passed → binding ↔ vendored prototype); at
editor startup `LogDsonImporter` reports `DsonParser DLL version: 1.0.0 (plugin built
against 1.0.0)`, confirming the DLL→binding→gate path end to end on the matching-version
(log-only) branch. The mismatch branches (MAJOR-abort; minor-skew / unparseable /
missing-export warnings) are wired but not runtime-exercised — they need a deliberately
mismatched DLL. Implemented exactly as decided: optional bind row + MAJOR gate, R8 sync
in `Docs/ImporterArchitecture.md`. `Docs/Roadmap.md` left untouched: advances no phase,
fixes no listed bug, so R9 does not trigger.

## Genesis 9 companion figures — packaging decision & import plan (2026-06-08)

**Context.** G9 character presets instance only the body in `scene.nodes`; eyes, mouth
(teeth), eyelashes, and tear are separate conforming figures declared in `scene.extra →
PostLoadAddons` (resolution chain in `Docs/Reference.md` → "Genesis 9 companion figures").
The parser prerequisite shipped in **DsonParser 1.1.0** (`DsonDocument_GetScenePostLoadAddon{
Count,Slot,AssetName,AssetFile,MatPreset}`, paths only), so the remaining work is importer-side.

**Decision — separate `USkeletalMesh` per companion, leader-posed to the body skeleton; not
merged.** Selection criteria, in order (same filter as the IrayUber bump decision):
(1) game-runtime performance, (2) importer complexity/risk, (3) content modularity.
- **Runtime (decisive, ~neutral).** Companions bound via a leader-pose (MasterPose) component
  copy the body's evaluated pose and skip their own animation evaluation, so per-follower CPU
  is near-zero; the meshes are tiny (low vert counts, ~1% screen on close-ups). Merging does
  **not** cut draw calls — companions carry distinct materials (opaque sclera/iris, translucent
  cornea/tear, masked eyelash), so each draws as its own section either way. The only runtime
  delta is a few extra `USkeletalMeshComponent`s in the separate case — the standard, optimized
  UE modular-character pattern.
- **Complexity/risk (favors separate).** Separate reuses the existing per-figure import path
  verbatim (new work is a *list* of sources, not one); merging adds MeshDescription
  combination, material-slot offsetting, and folding skin weights into one bone-index space.
- **Modularity (favors separate).** Preserves DAZ's per-figure separation (toggle the tear,
  re-import one companion) and keeps the translucent cornea/tear as clean small meshes rather
  than translucent sections on the body mesh (sorting risk).

The merged edge — one asset, one component, lowest per-component overhead — does not outweigh
the above when the runtime delta is marginal.

**Plan.** Importer slices, dependency-ordered (status tracked in `Docs/Roadmap.md`):
- **A — plumbing + discovery:** bind the 5 exports (`DsonParserFunctions.h` + `DsonImporter.cpp`
  + ABI-check list); resolve each `AssetFile` → loader `.duf` → geometry DSF/node into a
  companion-source list. No meshes built.
- **B — geometry + packaging:** import each companion geometry DSF as its own `USkeletalMesh`
  via the per-figure path, bound to the body `USkeleton` by bone-name and leader-posed.
- **C — materials:** from each addon's `MatPreset` (`preset_hierarchical_material`), matched by
  geometry-id + group.

Fiber eyebrows (`G9EyebrowFibers`) deferred → groom. **Feasibility check for B — resolved
2026-06-08:** confirmed each companion rigs to a *subset of the body's Genesis 9 skeleton*
(verified from `Genesis9Eyes.dsf`: 13-bone partial skeleton, `SkinBinding` weights to 10 body
bones by name; details in `Docs/Reference.md` → "Genesis 9 companion figures"), so the
leader-pose / shared-skeleton plan holds.

**Assembly — decided 2026-06-08: standalone.** The importer emits each companion as its own
`USkeletalMesh` sharing the body `USkeleton`; it does **not** wire runtime leader-pose or emit
an actor/Blueprint. Rationale: the importer is a data pump that stays agnostic — predictable
asset paths/names, consumers attach + `SetLeaderPoseComponent` themselves (the same
translate-don't-interpret principle — `Docs/Principles.md`). Declined: an assembled actor/BP (most turnkey, but a new importer output type)
and editor-preview-only attachment — both are importer-side coupling beyond data-pump scope.

**Two axes, don't conflate (clarified 2026-06-08).** "Standalone" is the *mesh* axis — separate
`USkeletalMesh` assets, no actor/BP wiring. The *skeleton* is deliberately **shared**: every
companion binds to the one `Genesis9_Skeleton`, and a companion carrying bones the body lacks (the
Mouth's tongue chain) **merges them into that shared skeleton**, not a private one — the standard
UE modular pattern that keeps leader-pose clean. So the Slice-B tongue-bone merge is on the
skeleton axis and does **not** breach mesh-standalone; the shared skeleton is an intentional
coupling chosen for animate-as-one-character (the price of not giving each companion its own
skeleton). Consequence: `Genesis9_Skeleton` is the union of body + whatever companions imported
together — the body mesh references bones it doesn't use, which is harmless/standard in UE.

**Session log — 2026-06-08.**
- **Slice A** (discovery) ✅ — bound the 5 PostLoadAddon exports; resolve each `AssetFile` → loader
  `.duf` → geometry DSF/node + `MatPreset` into `FDsonCompanionSource` (loader is a wearable
  *preset*, 0 scene nodes → resolve via the geometry DSF). Verified Nancy 9 (4 companions).
- **Slice B** (geometry + packaging) ✅ — each companion → its own `USkeletalMesh` on the shared
  `Genesis9_Skeleton`; leader-pose left to the consumer. Tongue fix:
  `FDsonSkeletonBuilder::MergeCompanionBonesIntoSkeleton` merges companion-introduced bones (Mouth
  `tongue01–05` under `lowerteeth`, +5) so the tongue binds, not root-fallback.
- **Slice C** (materials) ✅ — reuse `FDsonMaterialBuilder` on each `MatPreset`
  (`preset_hierarchical_material`, standard `scene.materials`); MICs wired by surface group; 7
  companion surfaces added to `GetNonSkinSurfaceGroups()`.

**Companion zero-UV defect — found + fixed 2026-06-08 (commit `3f60375`).** Slice B built companion
meshes with **all-zero UVs**: `FDsonMeshBuilder::BuildCompanion` passed an empty UV-set path to
`ReadUvData` on the wrong assumption "companions carry no UV sets (same as body)" — but the body does
not embed UVs either; it resolves an *external* UV-set DSF and passes it, a step the companion path
skipped. Every companion sampled a single texel; surfaced as the G9 "pink teeth" (teeth sampled the
gum region). Invisible until now because companions had no working texture until the
`scene.animations` key-0 fix landed — a defect masked by an upstream-missing feature, exposed once
that feature shipped. Fix mirrors the body path: the pipeline resolves each `CompanionUvSetUrl` via
`ResolveUvSetDsfPath` and passes it to `BuildCompanion` → `ReadUvData`. (Nancy's Mouth UV set is
single-tile "default", not the body's "Base Multi UDIM" — no UDIM work needed.)

**Mouth/Teeth metallic — root cause found, fix deferred (corrected 2026-06-08, source-traced with
the user).** The earlier diagnosis in this entry (Mouth/Teeth "omitted from
`GetNonSkinSurfaceGroups()`" / "textureless gray inheriting base `PBRSkin.dsf`") was **wrong** and is
superseded. The surfaces are **not** textureless: `Genesis 9 Mouth MAT.duf` binds a real
`Genesis9_Mouth_D_1001.jpg` Base Color + 0.3 roughness + 0.8 translucency via **`scene.animations`
key 0**, which the parser doesn't apply → importer gets gray + no map → metallic (mechanism + the
`PresetFile`-overrides-`AssetFile` rule → [`Reference.md`](Reference.md) → "Companion materials").
`Mouth`/`Teeth` have been in `GetNonSkinSurfaceGroups()` since slice #2; that set only gates SSS,
never the master.
- **Abandoned — do not redo:** "reroute textureless non-skin/oral surfaces to `M_DazDefault`" (would
  discard the real mouth texture and mask the parser bug).
- **Resolved — importer-side, not parser-side (corrected 2026-06-08).** The earlier "fix = parser-side
  (add `scene.animations` processing that resolves pointers onto channels)" plan was **dropped**. The
  parser instead exposes `scene.animations` *faithfully* (DsonParser 1.2.0 `DsonDocument_GetSceneAnimation*`
  — verbatim url + key-0 typed value) and deliberately does **not** apply it onto `scene.materials` (its
  "faithful, no cross-section merge" stance) — the consumer merges. The importer now consumes key 0 in
  `DsonMaterialBuilder::ApplySceneAnimationOverrides` (commit `e4002b7`, 2026-06-08): after the base
  scene.materials pass, each key-0 `value`/`image_file` whose matId matches the scene material and whose
  channel is a key in the active mapping table overrides the placeholder. Scope held to v1 (`value` +
  `image_file`, key 0; `image_modification`/tiling + multi-key deferred). **Verified on Nancy G9
  re-import 2026-06-08: textures apply and (with the companion UV-set fix) UVs are correct; the residual
  over-shininess is a separate PBRSkin-master issue (below + `Roadmap.md` Known issues).**

**PBRSkin over-shininess — investigation note (2026-06-08).** After the key-0 + companion-UV fixes made
textures visible, all `M_DazPBRSkin` surfaces (G9 Nancy body skin + Mouth/Teeth/tongue) read too
glossy/reflective vs DAZ. Lead found while diagnosing the Mouth: DAZ `Specular Lobe 2 Roughness Mult`
(~0.025 on the Mouth — the *second* dual-lobe's roughness **relative to lobe 1**, the sharp highlight)
is mapped (`GetPBRSkinMapping`) to the master's **global** `SpecularRoughnessMult`. The master computes
Roughness = `SpecularRoughness × SpecularRoughnessMap × SpecularRoughnessMult`, so a ~0.025 global
multiplier collapses roughness toward a mirror. Root question: how the v1 single-lobe approximation
should treat DAZ's two specular lobes (lobe-1 roughness + lobe-2 relative mult + dual-lobe weight) — a
fidelity-vs-runtime-cost call (`MaterialMastersV1.md` §M_DazPBRSkin).

**Resolved (2026-06-09) — importer-only, dropped both lobe-2 mappings.** The lead above was
*incomplete*. Tracing the `M_DazPBRSkin` graph (Root.Roughness ← Add_0 ← LinearInterpolate_5) showed the
master computes Roughness = `(SpecularRoughness × SpecularRoughnessMap) × SpecularRoughnessMult ×
(1 − 0.3 × DualLobeWeight)` — so a **second** importer mapping, `Dual Lobe Specular Weight` (=1.0) →
`DualLobeWeight`, was independently crushing roughness ×0.7 (its only effect in the master is that
roughness lerp; it feeds nothing else). Combined with the `SpecularRoughnessMult` (=0.55) factor, skin
roughness landed at **0.385×** the DAZ value. Verified across both PBRSkin characters (Nancy + Laura, all
7 surfaces): `Dual Lobe Specular Enable` is **false** everywhere — and false by the PBRSkin base default
(Laura's preset omits the channel and it still resolves false), so DAZ uses neither lobe-2 quantity. Fix
(`GetPBRSkinMapping`): **dropped both mapping rows** + a guard comment, leaving `SpecularRoughnessMult`=1
and `DualLobeWeight`=0 at their master defaults → Roughness = `SpecularRoughness × spec map` (faithful
single-lobe). **No master-asset edit** — the master graph correctly implements the single-lobe
approximation; the bug was purely the importer feeding gated-off inputs. **Option B' parked:** honor
`Dual Lobe Specular Enable` (feed the two lobe-2 channels only when a character enables the dual lobe),
revisit if such content appears. Code-complete + clean `Build.bat DsonHostEditor` (0 warnings / 0
errors). **Verified (2026-06-09):** Nancy G9 re-import confirmed the skin reads correctly (no longer
crushed). The Mouth companion (`MI_Mouth`/`MI_Teeth`, also on `M_DazPBRSkin`) had minor residual gloss,
resolved **per-character via a manual `SpecularRoughnessMult` > 1 MIC override** — deliberately *not*
baked into the importer (an aesthetic correction, not a faithfulness fix; DAZ mouth interiors are
genuinely wet — revisit as part of slice #3 wet-surface handling if a durable path is wanted). Laura not
re-checked.

**Slice #3 heads-up.** EyeMoisture `L/R` import but their channels reference `material_library` via a
`#fragment` url the parser doesn't resolve → the interim `M_DazIrayUber` MICs are near
parameter-free. `M_DazEyeMoisture` (slice #3) must handle those EyeMoisture channels (may need
parser-side `material_library` `#fragment` resolution). Also: the key-0 override pass
(`ApplySceneAnimationOverrides`) is a **no-op on the eyes companion** today — eye material ids carry
spaces + a uniquifying suffix (`Eye Left`, `EyeMoisture Left-1`) while the animation urls reference them
percent-encoded and **unsuffixed** (`#materials/EyeMoisture%20Left:?…`), so the raw matId compare never
matches; slice #3 must reconcile that (UrlDecode + suffix) before eyes get key-0 values. Harmless until
then — no regression (eyes already import without key-0).

**Next:** (1) Mouth/Teeth metallic fix — ✅ key-0 consumption (`e4002b7`) + companion UV-set fix
(`3f60375`) verified on Nancy G9 2026-06-08; then (2) slice #3 (`M_DazEyeMoisture`) and the
PBRSkin-shininess investigation (Roadmap Known issues).

## Director/Implementer handoff — file-based `.handoff/`, Director-defers, option D doc fold (2026-06-08)

**Decision.** Ported the Director/Implementer **file-based handoff** (from the
DsonParser repo's `docs/handoff-system-port-guide.md`) into this plugin: a change
now travels through `.handoff/task-<id>.md` / `feedback-<id>.md` on disk rather than
a chat-pasted prompt, so the Implementer can be any LLM agent and every task-file is
on disk and reviewable before it runs. Spec folded into `Docs/AgentWorkflow.md`
(roles, id convention, two-tier reporting, review gate, history, both templates).

**Build ownership — kept "Director defers," diverging from the port guide.** The
guide bakes in "Implementer builds; **Director re-runs the build** to confirm" and
flags it non-negotiable. We deliberately did **not** adopt that. Grounding it in
cost vs. coverage:
- The Director's ground-truth check splits into `git diff` (what changed) + a
  `CodeReviewRules.md` review pass (compliance) — both cheap — and an independent
  recompile (does it build) — **expensive here**: `Build.bat DsonHostEditor` needs
  the UE Editor closed plus a slow link, unlike DsonParser's fast `msbuild` DLL +
  console-harness build the guide was written for.
- The recompile is also **redundant**: the user builds and opens the editor to use
  the import, so a bad compile fails loudly and immediately — a natural second
  verifier DsonParser lacks.
- So the Director keeps the two cheap checks and **defers the recompile**,
  re-building only at its discretion for a build-risky change (parser-ABI / X-macro
  list, `*.Build.cs`, public headers, added/removed `.cpp`) or an unconvincing build
  claim. `BUILD_OWNERSHIP` is thus an adaptation slot for this repo, not the guide's
  constant.

## Importer scope codified — bring-everything, translate-don't-interpret, consumer-agnostic (2026-06-09)

**Decision.** Codified the Importer's governing principles in `Docs/Principles.md`
(P1–P5) and made the Roadmap subordinate to them. Substance: the Importer **brings
everything it can** from a DAZ source and **interprets nothing** — assets (even unused
ones) plus authoring metadata that has no UE-asset home, emitted faithfully;
composition / baking / assembly are interpretation and stay out of scope. Emission is
mechanical and consumer-agnostic; completeness is bounded by parser exposure, widened
additively and just-in-time.

**Docs made consumer-agnostic.** The orientation docs no longer name any specific
downstream consumer: consumer-named rationale was removed from `Roadmap.md`,
`Reference.md`, this log, `MaterialMastersV1.md`, and `SubsurfaceProfileV2.md`, each
re-motivated from the intrinsic principles. The cross-repo intent that motivated the
direction was moved behind a discovery fence (`Docs/_OutOfScope/`, excluded from
Importer discovery via `AGENTS.md` and self-flagged). Future Importer work is directed
by the principles, not by a consumer's needs.

**Speculative — further analysis needed.** The mechanisms this implies — the
authoring-metadata artifact's schema / format, the specific metadata families (LIE
compositing, per-surface makeup, JCM rigging formulas), the programmatic import
entry-point signature, and whether the artifact can stay pure data — are **not**
settled here. They are to be finalized against real DSON assets when each emitting
feature is implemented; no asset inspection grounded them at codification time.

**Motivation (intrinsic).** A faithful, complete DAZ→UE translation, and a minimal
per-task context budget (an Importer task must not have to load any consumer's
concerns). Grounding both in one rule keeps the Importer a thin, mechanical translator.

**Doc architecture — option D (one doc + raised budget), not a split.** The fold
pushed `Docs/AgentWorkflow.md` from 119 → 222 lines, over its R10 soft budget of
120. Chose to **raise that budget to 240** (in R10 and the mirrored `dson-doc-guard`
hook) rather than split the mechanics/templates into a companion doc: the content is
one cohesive topic (roles + their handoff), and a split would leave a thin index
pointing at a fat companion — gaming the line metric, not serving the reader.
Considered B (split into `Docs/HandoffProtocol.md`) and rejected it.

**Consequences.**
- `.handoff/` is gitignored and listed do-not-browse in `AGENTS.md`; the dirs are
  created on first task; history pruned > 30 days.
- **No `settings.json` edit** — the budget lives in the hook *script*, referenced by
  both the in-repo and global settings via path, so R11's mirror stays intact.
- Future workflow additions should still route to their owning tier (rationale here,
  status → `Roadmap.md`), not grow `AgentWorkflow.md` toward the new ceiling.

**Status:** ✅ done & committed 2026-06-08 — `Docs/AgentWorkflow.md` rewritten;
`AGENTS.md`, `.gitignore`, `Docs/CodeReviewRules.md` (R10), and
`.claude/hooks/dson-doc-guard.ps1` updated; `.handoff/` ignore verified via
`git check-ignore`.

## Director commits — branch-per-task, squash-merge, gate-at-merge (2026-06-08)

**Decision.** Granted the **Director** git-commit authority (previously "user handles
all commits/pushes"), wired into the task system as a **branch-per-task** flow. Per
task the Director branches `task/<id>` off `Base` (default `main`) before writing the
task-file; the Implementer edits that checked-out branch and leaves the tree dirty (it
never runs git); after verifying, the Director **squash-merges into `Base`** — one
reviewed commit per task. **Push stays with the user.**

**Forks settled** (rationale):
- **Gate at the merge, not the commit.** Task-branch commits may be WIP; the invariant
  is that nothing reaches a parent without passing the Director's verification — which
  is what lets nested tasks carry checkpoints without weakening the gate.
- **Squash-merge per task** (vs `--no-ff` / rebase) — one reviewed commit per task maps
  to the `task/<id>` unit and the LLM-changelog style; collapses intra-task WIP.
- **Push stays with the user** (vs delegating) — commits/merges are local and
  reversible; push is outward-facing, so the user reviews the local history first.
- **Source merge conflicts route back through the handoff** (vs Director hand-resolve)
  — resolving a source conflict is a source edit, outside the Director's boundary; only
  non-source (docs/config) conflicts the Director resolves. Serialized integration
  (branch off current `main`, integrate before the next task) keeps the common case
  conflict-free.
- **Nested exception** — a minor task spawned mid-task branches off the in-progress
  parent *only if it depends on that parent's unmerged changes*, else off `main`;
  children merge up (minor → major → main).
- **Director-authored doc/config-only changes** (no Implementer, no handoff) commit
  straight to `main` — no task branch.

**Doc placement (R10).** `AgentWorkflow.md` was at 222/240 with a standing directive
not to grow it toward the ceiling, so only the **policy** landed there (boundary
bullet, role bodies, flow steps, template `Branch:` line); the **command mechanics**
went to `Docs/Tooling.md` (57 → ~78/80), mirroring how build mechanics already live
there. `AGENTS.md` lines 16/35 repointed (net-neutral).

**Consequences.**
- The sibling `DsonParser` repo and its `docs/handoff-system-port-guide.md` still say
  "never commit" — the two repos' workflows now diverge; porting is a separate call.
- Bootstrap: this doc change itself was left in the working tree for the **user** to
  commit (not self-applied), since it is the change that grants the authority.

**Status:** decided 2026-06-08; docs updated in the working tree, pending user commit.
