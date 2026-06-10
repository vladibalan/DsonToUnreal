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
- Director commits — branch-per-task, squash-merge, gate-at-merge (2026-06-08)
- Doc-diet / tiering pass — budgets retuned; scope-broadening went to exempt docs (2026-06-09)
- G9 untextured eyeball — anim-bound LIE composite, baked at import (2026-06-09)
- Importer discovery boundary — reference-graph-only; authoring presets out of scope (2026-06-09)
- Routing consolidated to a single owner — `AGENTS.md` Task Routing (2026-06-09)
- Programmatic import entry point — public `ImportDazAsset`; report decoupled from the private pipeline result (2026-06-09)
- G9 eye-moisture cornea lensing — refraction shell minified the iris; fixed via Refraction Method = None (2026-06-10)
- Composed dialed shape out of importer scope — formula evaluator dropped, kept as downstream reference (2026-06-10)
- ImportDazAsset multi-instance bind — public entry idempotently binds GDsonParser when the plugin is hosted via AdditionalPluginDirectories (2026-06-10)
- Asset import folder structure — per-character `Characters/<char>/` + shared deduped `Library/Textures/`; fixes same-generation multi-DUF collision (2026-06-10)
- Consumer versioning contract — lean SemVer (`.uplugin` VersionName + git tag + `CHANGELOG.md`), baseline 1.0.0, no runtime accessor; R12 gate (2026-06-10)
- Authoring-metadata recipe emission (UDsonAssetRecipe) — intake, per-item parser-reachability triage, one parser FR (per-layer LIE compositing metadata) (2026-06-10)

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

**Slice #3 heads-up.** EyeMoisture `L/R` scene-materials carry zero inline channels — their `url` is a bare
same-file `#fragment` (`#EyeMoisture%20Left`) into this file's `material_library`, where the `uber_iray`
channels actually live. **Determined importer-side (2026-06-09), no parser change:** the parser already
relays `material_library` faithfully via the `GetMaterial*` family (distinct from `GetSceneMaterial*`), so
slice #3 resolves the bare `#fragment` in the importer (UrlDecode → match `GetMaterialId`) and reads the
wet-eye channels (Glossy, Refraction IOR, Cutout Opacity) from there. **Worked precedent** for the
parser-request gate in [`AgentWorkflow.md`](AgentWorkflow.md) ("Requesting parser features"): a
cross-section join of already-exposed data is importer work, not a parser ask. Also: the key-0 override pass
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

## Doc-diet / tiering pass — budgets retuned; scope-broadening went to exempt docs (2026-06-09)

**Decision.** Ran the standalone Director doc-diet pass from the Roadmap backlog. Brought the
hot-path orientation docs back within `CodeReviewRules.md` R10 by **deduplicating and relocating,
not shaving nuance** — every compressed block was confirmed already owned by an exempt doc, or kept
inline where it had no other home.

**What changed.**
- `Roadmap.md` 258 → 228: collapsed "Next up" (it duplicated the slice section), compressed
  shipped-v1 detail, folded the gated-node audit into slice #2, trimmed "Dropped from v2" rationale
  to a `MaterialMastersV1.md` pointer.
- `AGENTS.md` 138 → 130: removed the Director/Implementer role + git/build re-enumeration (single
  home `AgentWorkflow.md`) and the re-listed R10 tier map → pointers.
- `Tooling.md` 78 → 77: dropped two policy restatements ("Implementer never runs git", "push stays
  with the user"); git command mechanics stay (its tier).
- `Reference.md`: fixed a stale Contents TOC + a doubled LIE "drops the recipe" sentence.

**Budgets retuned** (R10 list + `dson-doc-guard` hook, kept in sync): `AGENTS.md` 140 → **135**
(tightened — it auto-loads every session, so it earns the strictest ceiling); `ImporterArchitecture.md`
185 → **195** (raised — pinned at ceiling by realized companion-builder growth + P2's forecast
metadata-emission component). Other hot-path budgets held. `SubsurfaceProfileV2.md` added to the
exempt list (a cold V2 design doc like `FormulaMorphsV2.md`).

**Why budgets mostly held rather than rising with scope.** The 2026-06-09 scope codification (P1–P5)
broadened the Importer's mandate but landed in **exempt** docs (`Principles.md`, this log) by design —
so the hot path needed no bigger ceilings; nuance is preserved by relocating it into the budget-free
cold docs (what R10 tiering is for). Net: hot-path read-load −~50 lines, ceilings ~flat.

**Remaining follow-up — resolved 2026-06-09.** Those overlapping `AGENTS.md` Task Routing /
`ImporterArchitecture.md` "Common Change Areas" subsystem→file tables were consolidated to a
single owner (`AGENTS.md`); see "Routing consolidated to a single owner" below.

**Status:** ✅ done 2026-06-09. Doc-only Director change → one squashed commit to `main`; push with the user.

## G9 untextured eyeball — anim-bound LIE composite, baked at import (2026-06-09)

**Symptom.** After slice #3 made the eye-moisture shell transparent, the Genesis 9 eyeball
(`Eye L/R`, PBRSkin, in the Eyes companion) imported flat grey — no iris/sclera.

**Root cause (corrects the earlier Roadmap guess).** It was **not** "the generic eyes MAT
has no textures / they come from a character override." The textures are in the generic
`Genesis 9 Eyes MAT.duf` the companion import already loads. `Eye L/R`.`diffuse` in
`scene.materials` is a bare grey placeholder; the real albedo is bound in `scene.animations`
key-0 as `…:?diffuse/image = "#Eye Color-3"` — a `#fragment` to an `image_library` entry that
is a LIE stack (white base + `…Sclera_01.jpg` + `…Iris_01.png`, `blend_source_over`). The
importer dropped it twice over: `ParseAnimationUrl` rejected any leaf but `value`/`image_file`,
and animation-bound `image_library` layers were reachable through no parser accessor (the
per-channel LIE accessors need an inline channel, and the parser never merges animations onto
`scene.materials`).

**Decision: bake the eye LIE into one albedo `UTexture2D` at import** (user, 2026-06-09).
Composite the textured layers source-over (layer 0 = bottom) into a single texture, bind to
the eye diffuse. Criteria match the IrayUber bump decision: (1) runtime perf — one static
texture, zero per-pixel cost, vs. a runtime multi-sample shader composite; (2) fidelity —
reproduces what DAZ does at load.

**Scope rule set (reusable principle).** A **fixed-factory** LIE (the base figure's eyes — no
user-facing layer choice) is in-scope *translation* and may be baked. A **variant** LIE
(makeup styles, where keeping layers separate preserves an authoring choice — P1/P5) stays
out-of-scope *source*, imported as standalone layers, never baked. Discriminator: does the LIE
stack encode a selectable option set, or one fixed appearance?

**Cross-repo + implementation.** Needed an additive parser exposure: DsonParser **1.3.0**
`GetImageLayer{Count,TexturePath,Label}` over the `GetImageId` index space (per-image
textured-layer stack; the no-`url` base is not counted, so `Eye Color-3` reports 2). Importer:
open `ParseAnimationUrl` to the `image` leaf; new `image` branch in `ApplySceneAnimationOverrides`
resolves the `#fragment` → image index → composites via `FDsonTextureImporter::CompositeImageLayers`
(reuses the bump→normal bake decode/encode/save path; cached by image id; saves to
`…/Textures/Composites/T_<id>`).

**Status.** ✅ Runtime-verified on G9 Nancy 2026-06-09 — iris centered on the sclera, eyeball
front-mapped, matching DAZ. Shipped across two commits: `c636ada` (the bake) + `480f234` (an
alignment fix — see Postmortem).

**Postmortem — the alignment bug (`c636ada` → `480f234`).** `c636ada` passed Director build +
diff-review but was visually wrong: `CompositeImageLayers` **stretched** every LIE layer to the
bottom layer's size, so the half-height iris (`Iris_01.png` is 4096×2048 vs the 4096² sclera) was
doubled in height and pushed below the sclera → mapped to the bottom of the eyeball. Review missed
it by checking the source-over math without checking that the layers are **different sizes**; the
Implementer reported "smooth" without running the import. Caught only when the user ran it and
supplied the DAZ ground-truth composite. Fix (`480f234`): composite each layer at **native size,
top-left anchored, onto the `map_size` canvas** (new `GetImageMapWidth/Height` bindings), no
resampling. **Lesson:** a LIE composite must place layers on the `map_size` canvas at native scale
— never stretch-to-fit — and a *visual* fix isn't verified until it's seen at runtime (build+review
is necessary, not sufficient).

## Importer discovery boundary — reference-graph-only; authoring presets out of scope (2026-06-09)

**Decision (user).** The Importer brings only what the imported asset **references** (its own
reference graph — dependencies, companions, transitively-reached files); it does **not** scan
the content library for sibling authoring presets a user would apply separately. Selecting and
applying those is the authoring layer's job (DsonArtisan), not the Importer's. Codified into
`Principles.md` P1 ("discoverable" = the reference graph, not the library).

**Why this needed saying.** P1's "bring all discoverable assets / completeness is the default"
reads, taken alone, as license to haul in every sibling preset. It reconciles only once
"discoverable" is pinned to the imported asset's graph — otherwise the boundary is ambiguous,
which is what left the question below open.

**Worked example — Genesis 9 / Nancy eyes (option C).** `HID Nancy 9.duf` contains **no**
eye-color material at all — only eye bones, a focal-point control, and the generic-eyes
PostLoadAddon. So a faithful import yields the **generic** G9 iris, and that is correct, not a
gap. Nancy's authored eye colors live in 10 separate `HID Nancy Eyes 0X *.duf` presets
(`.../Materials/2 Eyes/`) — `preset_hierarchical_material`s targeting the same Eye/EyeMoisture
surfaces, each a single pre-baked albedo (`g09_Nancy_eyes_base_07.jpg`, not a LIE). They are
**not** referenced by `HID Nancy 9.duf`; in DAZ the user applies one by hand after load — so
matching her eyes is an authoring step, out of Importer scope. **Eyes stay generic by design.**

**Same boundary, accepted & deferred.** The LIE *makeup* textures aren't imported either, for
the same reason — they live in unreferenced option presets (`0 Face Options` / `1 LIE Options` /
`3 Lips`), not in the character file. The user flagged the broader "load separate authoring
assets" need as **explicitly out of scope for now** (a future authoring-plugin concern), not a
TODO. Not a bug — the documented boundary.

## Routing consolidated to a single owner — `AGENTS.md` (Task Routing) (2026-06-09)

**Decision (user).** The duplicated subsystem→file routing was consolidated to one owner:
**`AGENTS.md` (Task Routing)**. `ImporterArchitecture.md`'s "Common Change Areas" table — the
deferred doc-diet follow-up — was removed.

**Why `AGENTS.md`.** R10's tier list already assigns `entry/routing → AGENTS.md`, and its
26-row Task Routing was the superset of the 16-row "Common Change Areas" (it adds the parser
ABI check, ThirdParty lib, `Sync-Parser`, the diagnostic dump, and the status/why/facts doc
pointers). "Common Change Areas"'s only non-duplicated content — the companion mesh/material
flow and the morph→`DsonParserFunctions.h` hop — already lives in `ImporterArchitecture.md`'s
own Runtime Flow + Component Responsibilities, so removing the table lost no nuance.

**Why not `ImporterArchitecture.md`.** Moving detailed routing there and leaning out the
auto-loaded `AGENTS.md` was the alternative, but it contradicts R10's tier and
`ImporterArchitecture.md` sat at 190/195 — it could not absorb the ~30-row union without
breaching its budget.

**Same-change cleanup.** `ImporterArchitecture.md`'s Discovery Rule repointed at `AGENTS.md`
(Task Routing); stale "Common Change Areas" pointers fixed in `AuditGuide.md` (§Symptom Routing)
and `CodeReviewRules.md` (R8 doc-sync list); the cleanup item removed from `Roadmap.md` (R9).
Net: ~17 hot-path lines removed (`ImporterArchitecture.md` 190 → ~173).

**Status:** ✅ done 2026-06-09. Doc-only Director change → commit to `main`; push with the user.

## Programmatic import entry point — public report decoupled from the private pipeline result (2026-06-09)

**Decision (user-approved).** Expose imports programmatically via a thin public module entry
point — `FDsonImporterModule::ImportDazAsset(FDsonImportRequest) → FDsonImportReport`, types in
`Public/DsonImportRequest.h` — so imports are scriptable/testable (editor automation, commandlets,
pipeline tests) without the Slate window. Purely additive; the interactive window is unchanged.

**Why a new public report, not the existing `FDsonImportResult`.** The private `FDsonImportResult`
(and the `FDsonImportSettings` it carries) hold inter-stage staging detail — `ResolvedFigureDsfPath`,
`Generation`, the `FDsonCompanionSource` list. Re-exposing those as public ABI would couple callers
to pipeline internals and widen the breaking-change surface (R7), against P3 (a generic,
consumer-agnostic surface). So `FDsonImportReport` carries only what a caller needs — produced
assets (skeleton / mesh / companions), a success flag + `EDsonImportStatus`, and a diagnostics
summary — and the `FDsonImportResult → FDsonImportReport` mapping stays behind the module boundary
in `DsonImporter.cpp`.

**DRY (R4).** No second import path: both the window and `ImportDazAsset` funnel through the one
`FDsonImportPipeline::Run`, and the path→settings preparation is single-sourced in
`FDsonValidator::ToImportSettings` (the window's former `RefreshPendingSettingsFromValidation` body).

**Status.** ✅ Shipped 2026-06-09 via Director→Implementer task `20260609-142930-programmatic-import-api`;
Director-verified build clean (`DsonHostEditor`), squash-merged to `main` as one reviewed commit (push
with the user). One verification loop: the first Implementer pass did **not** compile — brace-less
`UE_LOG` in an `if`/`else` (C2181) — wrote no feedback-file, and skipped the R8 doc-sync. Caught on the
Director's build (no build claim existed to trust); the user applied the brace fix and the Director
completed the orientation-doc updates during integration. **Lesson:** `UE_LOG` expands to a braced
block, so it cannot be the brace-less body of an `if`/`else` — and an Implementer "smooth" without an
actual build is not trustworthy (the standing reason the Director re-runs the build).

## G9 eye-moisture cornea lensing — refraction shell minifies the iris (RESOLVED via Refraction Method = None, 2026-06-10)

**Symptom.** On Genesis 9 (Nancy), the translucent cornea / eye-moisture shell (`M_DazEyeMoisture`)
visibly **minifies — shrinks — the iris** behind it: pronounced ("appears very little"), not the subtle
wetness the master intends (`MaterialMastersV1.md` — "not a glass lens").

**How it was lost.** Slice #3 and the eyeball-bake session (see "G9 untextured eyeball") verified only the
eyeball **albedo** — the iris/sclera LIE bake and its placement ("iris centered on the sclera"). The
refraction-shell lensing was never recorded, so the Roadmap's two slice-#3 "✅ runtime-verified" claims
inherited a **texture-only** verification. Surfaced and recovered in a 2026-06-09 Director session.

**Confirmed (from the `M_DazEyeMoisture` node dump, 2026-06-09).** Refraction **Method = Index Of
Refraction** (the root pin is literally `Refraction (Index Of Refraction)`), driven by a scalar parameter
**`RefractionIOR`** (master default 1.33; Nancy's MIC reads **1.38** — the importer feeds DAZ channel
`Refraction Index` straight in). That IOR pin is the lensing source; the rest of the shell — `Specular`,
near-zero `Roughness`, and a **Fresnel→×Opacity** chain (grazing-angle wetness) — is legitimate and stays.
In IOR mode, **IOR = 1.0 → zero offset → no lensing**, so the artifact is fixable from the MIC alone; the
master edit below is chosen for a stronger reason than necessity (see Decision).

**Decision — Option B (user, 2026-06-09): disconnect refraction at the master + drop the importer mapping.**
- **Master** (user hand-edit): disconnect the `Refraction (Index Of Refraction)` input in `M_DazEyeMoisture`
  and delete the now-orphan `RefractionIOR` scalar node.
- **Importer** (Implementer): remove the `Refraction Index` → `RefractionIOR` entry from
  `GetEyeMoistureMapping()` (`DsonMaterialBuilder.cpp:185-186`; `RefractionIOR` has no other source ref).

**Why B over A** (A = importer pins `RefractionIOR` = 1.0): both kill the lensing and are fidelity-identical
(DAZ asset untouched, P1/P5). B wins on the **governing runtime-perf filter** — A only zeroes the *offset*
but leaves a refractive-translucent material UE still compiles the refraction path for; B sheds that pass at
the source. B is also **footgun-free**: A leaves a live `RefractionIOR` knob every MIC must override forever,
which silently re-lenses if dialed or missed. "Loses dial-able refraction" is the only cost, and it is weak —
reconnecting the pin is one master edit away, and UE refraction is the wrong representation here. Same
master-edits-allowed call: the non-master fix did not serve the governing principles equally, so the master
edit is warranted (framed as the IrayUber bump→normal / dual-lobe decisions).

**Status.** Integrated 2026-06-10 as `cb96b13` (squash-merged to `main`): importer one-entry deletion in
`GetEyeMoistureMapping()` + master Refraction-pin disconnect / `RefractionIOR`-node delete (verified from
the copied-node dump); Implementer build clean, Director review clean (recompile deferred — non-build-risky).
`MaterialMastersV1.md` reconciled (RefractionIOR dropped from the contract). **Resolution 2026-06-10 — it WAS refraction; the input pin was the wrong lever.** The first attempt only
*disconnected the Refraction input pin* (`cb96b13`) and changed nothing visible — because **UE gates refraction
by the Refraction *Method* (material Details), not the pin**, and the Method was still `Index Of Refraction`.
That sent a mid-investigation detour wrongly suspecting geometry — but soloing the sections proved the
**eyeball mesh and baked albedo were always fine** (round, correct iris, matches the DAZ render), and the
`EyeMoisture` shell is a sphere coincident with the eyeball (faithful, not oversized). The shrink was the
shell's UE screen-space refraction minifying the eyeball behind it — *opacity-independent*, the refraction
signature (it persisted at Opacity 0, which I first misread as "not the shell"). **Real fix (user): set
`M_DazEyeMoisture` Refraction Method = None** — root pin now reads `Refraction (Disabled)`, verified from the
dump; the eyeball renders full-size. Master-only, propagates to existing imports (no re-import). `cb96b13`
(pin / `RefractionIOR` node / importer-mapping removal) stays as harmless cleanup.

**Original intent.** The master author enabled the refraction deliberately — *"it makes the eyes look alive"* —
a fidelity call. But UE's *screen-space* approximation can't reproduce DAZ/Iray's *ray-traced* cornea
refraction; it minifies the iris instead. So this is **defect-vs-correct, not fidelity-vs-perf**: Method=None is
both cheaper and correct (same framing as IrayUber bump→normal / dual-lobe). A correct *alive-eyes* pass (real
refraction or a faked iris parallax) is a **deferred enhancement**, not a regression.

**Lessons.** (1) Verify a *visual* fix at runtime before trusting it; an adjacent fix isn't the slice's own
surface (reinforces the eyeball-bake postmortem). (2) In UE, **refraction is gated by the Refraction Method
(Details), not the input pin** — disconnecting the pin does **not** disable it; the copied-node dump shows the
Method only in the root pin's label (`Refraction (Index Of Refraction)` vs `Refraction (Disabled)`) → durable
gotcha in `Reference.md`. (3) An opacity-*independent* minification through a translucent shell is the
**refraction signature** — toggle the Method, not the opacity, to test it. (4) Validate the symptom in a
**representative view**: these are leader-posed companions, and the alarming look was partly an isolation-view
artifact — chasing it (and the first refraction misfix) on the un-posed view cost a full loop.

## Composed dialed shape out of importer scope — formula evaluator dropped, kept as downstream reference (2026-06-10)

**Decision (user).** Evaluating DAZ formulas and baking the composed *dialed character shape*
(`Σ(leaf_deltas × evaluated_value)`) is **out of importer scope.** The "Phase 7 v2 — formula
evaluation / composed dialed shape" item the Roadmap had queued as the active front is removed —
it is not a planned importer phase. The importer's scope is converting raw DAZ assets to
Unreal-native assets, faithfully; nothing more.

**Why.** P1 — the importer translates, it does not interpret: *"combining, composing, or baking …
is interpretation, and interpretation is out of scope — it belongs to whatever authoring step later
consumes the import."* Composing a dialed shape is exactly that. The prior Roadmap framing
("Deferred to v2"; "Next up: Phase 7 v2 … the active front") implied the importer would eventually
do it, which **conflicts** with P1; `Docs/Principles.md` resolves such a conflict in the principle's
favor (*"the principle wins and the roadmap is what changes"*). So the roadmap changed.

**Still in scope (unchanged, shipped).** Faithful discovery import of delta-bearing morphs: the
importer follows `?value` formula outputs, resolves those files transitively, and imports each leaf
morph as a rest-state target at weight 0 (Phase 7). That is translation. Carrying the dial/formula
*metadata* itself across faithfully (vs. evaluating it) is a separate P2/P4 question, taken
just-in-time — not ruled out here.

**Doc changes.** `Docs/Roadmap.md`: section "Deferred to v2 (morph follow-ups)" → "Out of importer
scope — composed dialed shape (interpretation, P1)"; "Next up" rewritten (no feature phase queued;
remaining work reactive). `Docs/FormulaMorphsV2.md`: re-scoped with a banner — retained as the
discovery record + reference for the downstream authoring layer, **not** an importer feature plan;
its closing "when implemented, update Roadmap" line now reads as a scope-reversal gate.
`Docs/Reference.md`: the "control vs. complete dialed character" fact is unchanged (still accurate).
No source or build impact (doc-only).

## ImportDazAsset multi-instance bind — public entry binds GDsonParser idempotently (2026-06-10)

**Symptom.** A downstream consumer mounting this plugin into a *second* UE host via
`AdditionalPluginDirectories`, with a dependent editor module that does
`PrivateDependencyModuleNames += "DsonImporter"` and calls
`FDsonImporterModule::Get().ImportDazAsset(Request)`, hit validation failure
`"DsonParser library is not loaded"` on that call — even though startup logged the DLL
`loaded successfully` / `all exports loaded successfully` / version match. In the *same*
editor session the plugin's own import window imported the same `.duf` fine.

**Root cause — two `DsonImporter` images, two `GDsonParser`.** `GDsonParser` is a
per-DLL-image file-global whose only write site is `StartupModule`'s bind loop. Mounting the
plugin into a second host yields two `UnrealEditor-DsonImporter.dll` images (the plugin's own
`…/DsonToUnreal/Binaries/Win64` plus one in the consuming host's `Binaries`). UE runs
`StartupModule` on one image — it binds *that* image's `GDsonParser`, and the window (opened
from that image's menu) runs there → works. But `ImportDazAsset` is `DSONIMPORTER_API`
(imported), and the dependent module resolves it to the *other* image, whose `StartupModule`
never ran → its `GDsonParser` is default-constructed → `FDsonValidator::Validate`'s
`IsValid()` gate rejects the call. The inline `Get()` can hand back image A's module object
while the executing `ImportDazAsset` body is image B's — so the bug is per-image and the DLL
handle cannot live on the module object. `ImportDazAsset` had **no in-tree caller** (the
window calls `FDsonImportPipeline::Run` directly), so this public entry was never
runtime-exercised until the first external call surfaced it. Reporter ruled out DLL
path/staging (binds at startup), editor staleness (clean rebuilds), and a stale plugin
binary; the discriminator is one session where the window imports while the external
`ImportDazAsset` fails — same symbol, opposite validity.

**Decision: bind at the public entry point (idempotent), not a build/packaging fix.** Two
directions were on the table. (A) *Avoid the duplicate binary* — have the consuming host
mount the plugin without rebuilding it — lives in the **consumer's** project, can't be
enforced from plugin code, and any consumer rebuild reintroduces it. (B) *Guarantee
`GDsonParser` is bound in whatever image services the call.* B is self-contained, durable,
and consumer-agnostic (**P3** — the importer makes no assumptions about how it is hosted), so
B was taken; A stands as an optional complementary measure noted to the consumer.

**Change (`b13a2b9`).** Extracted the load + export-bind + version-reconcile out of
`StartupModule` into an idempotent file-scope `EnsureDsonParserLoaded()` (early-out on
`GDsonParser.IsValid()`) over `GDsonParser` + a new file-global `GDsonParserDllHandle`; called
from `StartupModule` (single-host behavior unchanged) and at the top of `ImportDazAsset`
(refuse with `EDsonImportStatus::ValidationFailed` if it cannot bind). On a MAJOR-version
mismatch the helper resets `GDsonParser` and frees the handle before returning false, so the
`IsValid()` early-out invariant holds and an ABI-incompatible parser never reaches `Validate`
— a small, safer change from the old startup path, which left the parser bound-but-
unregistered. The public `ImportDazAsset` / `FDsonImportReport` / `Get()` contract and
`DSON_PARSER_API_LIST` are unchanged; the only header edit removes the now-dead private
`DsonParserHandle` member (layout-internal — `FModuleManager` allocates the module object
inside this DLL, so no consumer ABI break).

**Status.** Integrated 2026-06-10 as `b13a2b9` (squash-merged to `main`): Implementer build
clean (`DsonHostEditor`, 17 actions, 0 errors, 0 warnings), Director re-build up-to-date,
review clean (R1–R11). The **two-host runtime repro** (external host + dependent module) is
the consumer's to run; the fix is correct by construction (idempotent; a no-op in the normal
single-host case). Durable gotcha → `Docs/Reference.md` "per-DLL-image module globals".

**Lessons.** (1) A public API with **no in-tree caller** is unverified until something calls
it — exercise programmatic entry points; "Done" in the Roadmap is not "runtime-proven". (2) A
non-exported UE module global is **per-DLL-image**; a plugin mounted into a second host via
`AdditionalPluginDirectories` can have two images, so state bound only in `StartupModule`
isn't there for the image that services an imported call — bind idempotently at the entry and
keep the resource handle in the same image as the code (not on the module object, which the
inline `Get()` may resolve to a different image).

## Asset import folder structure — per-character folders + shared texture library (2026-06-10)

**Problem.** Every imported DUF dumped flat into `/Game/DazImports/`, and the body
mesh, skeleton, and companion meshes were named from the shared **figure DSF**
(`Settings.ResolvedFigureDsfPath`). For Genesis 9 that DSF is the same base geometry
across all G9 characters, so importing a second same-generation character (Nancy after
Laura) silently **overwrote** the first's mesh/skeleton and orphaned its per-character
MICs. Bulk multi-DUF import was the trigger.

**Decision.** Two asset classes with opposite grouping needs, split per **P5/P1**
(immutable shared originals vs. character-scoped derivations):
- **Per-character assets** — body + companion meshes, skeleton, MICs, subsurface
  profile, baked LIE composites — grouped under
  `/Game/DazImports/Characters/<CharacterName>/`, named off the **imported DUF**
  identity (not the shared geometry DSF). Fixes the collision and gives the structure.
- **Shared source textures** — kept deduped under `/Game/DazImports/Library/Textures/`
  (DAZ-path-mirrored). A 4K skin texture shared by N characters imports **once**;
  per-character copies would waste disk/VRAM and defeat the resolve cache.

Three forks, decided with the maintainer:
- **Re-import overwrites/refreshes** the character folder in place (matches the usual
  re-import-it workflow; distinct characters sharing a DUF basename are rare).
- **LIE composites are per-character** (`Characters/<char>/Textures/Composites/`) —
  character-specific derivations keyed by image id; two characters with an id like
  `Eye Color` but different recipes would otherwise collide in a shared zone.
- **Shared textures live under `Library/`** so the shared-vs-per-character split is
  self-documenting in the Content Browser.

**Implementation.** `CharacterName` (sanitized imported-DUF basename) is derived once
in `FDsonValidator::ToImportSettings` and carried on `FDsonImportSettings`; the texture
importer takes it by ctor, for composite paths only. Roots are centralized in
`FDsonAssetUtils::CharacterRoot`/`SharedTexturesRoot` (R4) — no path literals scattered
across builders. The subsurface-profile asset name now comes from an explicit
`OwnerName` argument rather than the material folder's leaf segment, which the
restructure would otherwise have turned into `SSP_Materials`.

**R7 (output-path contract).** Breaking: existing imports must be re-imported; no
migration shim (decided). The lone external consumer, **DsonArtisan**, was verified
unaffected — it consumes imports through `FDsonImporterModule::ImportDazAsset` /
`FDsonImportReport` (asset pointers, path-agnostic) and hardcodes no
`/Game/DazImports/...` path in source, config, or `.uasset`.

**Status.** Integrated 2026-06-10 as `1e4ad64` (fast-forwarded to `main`). Implementer
builds clean (`DsonHostEditor`: restructure 17 actions, cleanup 9 actions, 0 warnings /
0 errors); Director re-build up-to-date; review clean (R1–R11). A review-gate fix loop
removed two R5 dead items before merge — the now-zero-caller `MakeImportAssetPath` /
`MakeImportSubfolderPath` (also the flat-`{Root}/{name}` footgun that had caused the
collision) and a stale `ObjectTools` include — via follow-up task
`20260610-061409-import-folder-cleanup`. **Runtime-confirmed 2026-06-10:** three figures
imported in-editor — G9 Laura and G9 Nancy (the previously collision-prone same-generation
pair) plus G8 Jordina — each landed in its own `Characters/<name>/` tree with one shared
`Library/Textures/` and 0 import failures (log-verified); maintainer confirmed visually,
then removed the test imports.

**Lessons.** (1) Name per-character assets off the **imported preset** identity, never
the shared geometry DSF — the shared-DSF name was the whole bug. (2) When a name is
derived from a path's leaf segment, a folder-shape change can silently corrupt it (the
`SSP_Materials` trap); pass the identity explicitly. (3) Removing the superseded
flat-path helper was not just tidiness — leaving it invited a future caller to
reintroduce the collision.

## Consumer versioning contract — lean SemVer for the downstream consumer; baseline 1.0.0 (2026-06-10)

**Request.** The downstream **DsonArtisan** Director (the consumer verified in the
output-path decision above) asked for a low-noise way to learn when DsonToUnreal's
consumer-facing surface changes and whether a change is breaking — a version handle
plus a one-read change channel.

**Decision — lean, one carrier.** Version the **published surface** (P3), not a
consumer's needs. Carriers: `VersionName` in `DsonToUnreal.uplugin` (single source of
truth), a per-release git tag `vX.Y.Z`, and a root `CHANGELOG.md`. The surface = the
public `DsonImporter` module API (`Public/DsonImporter.h` + `DsonImportRequest.h`,
incl. struct fields / `EDsonImportStatus`) **and** the emitted-output shape
(`/Game/DazImports/...`, owned by `FDsonAssetUtils`). Scheme, baseline, and the
"what we don't port" rationale live in `Docs/Versioning.md`; the per-change gate is
`CodeReviewRules.md` R12.

**Baseline 1.0.0.** Labels the current tree (the `.uplugin` already carried 1.0.0);
tagged `v1.0.0`. Pre-versioning history is not retro-numbered — including the same-day
asset-folder change (breaking, but verified harmless for DsonArtisan above).

**Declined — a runtime version accessor (request item c).** Consumer-serving runtime
code cuts against P3 (mechanical, consumer-agnostic emission) and P4 (additive,
just-in-time); and UE already exposes `VersionName` at runtime via `IPluginManager`,
so a bespoke export would duplicate engine data. Revisit only if a concrete
runtime-gate need lands. Also not ported from DsonParser (whose consumer is
binary-blind): the `*Version.h` macro header, the `GetVersion()` export, `@since`/banner.

**Surface delta flagged back.** The request under-described the report:
`FDsonImportReport` also carries `Status`, `Skeleton`, `Mesh`, and `CompanionMeshes`,
and `FDsonImportRequest` has `bDumpMaterialDiagnostics`. The versioned surface is the
actual one, not the request's summary.

**Mechanics.** Doc/config-only (no `Source/` change, no build): new `CHANGELOG.md` +
`Docs/Versioning.md`; R12 + a Quick-Checklist line in `CodeReviewRules.md`; `R1–R11`→
`R1–R12` across AGENTS/AuditGuide/AgentWorkflow (the two dated `R1–R11` mentions in
this log are left as historical record). R12 took `CodeReviewRules.md` past its 240
ceiling, so the R10 budget was raised to 265 and mirrored in the `dson-doc-guard`
hook. Committed straight to `main` (doc-only convention); the user pushes.

## Authoring-metadata recipe emission (UDsonAssetRecipe) — intake & parser FR (2026-06-10)

**Request (from the DsonArtisan consumer, relayed through the user — the sanctioned
cross-repo channel).** Emit a persisted `UDsonAssetRecipe` asset beside each imported
character, carrying the DAZ authoring metadata the importer already parses but currently
discards, so a downstream authoring step can realize the character faithfully from the
already-imported UE assets. It is a **new** persisted asset, **not** a change to the live
`FDsonImportReport` (kept the thin, stable handle set). Raw and uncomposed only — never
interpret, compose, bake, or realize (that is the consumer's job). Grounded on G9 HID Nancy 9
(PBRSkin); the consumer's field-level contract is `DsonArtisan/Docs/RecipeContract.md`.

**Why it's intrinsically in scope (not a consumer-specific feature).** This is exactly the
**P2** artifact (`Principles.md`: "authoring metadata with no UE-asset home … emitted as a
faithful, self-describing data artifact alongside the imported assets"), whose schema P2 left
"to be finalized … when the emitting feature is implemented." The concrete need P4 waits for has
now landed. Emission stays mechanical and consumer-agnostic (P3); imported originals stay
immutable (P5). So the Roadmap/forward-docs frame it as "a downstream consumer requested…" and
only this dated entry names DsonArtisan (R10).

**Per-item triage — against the published parser surface (`DsonParserAPI.h` v1.3.0), NOT the
importer's current X-macro binding list.** The distinction is the whole story: the importer
binds only a subset of what the parser already publishes, so most of the "EMIT" set is reachable
today by *binding more exports* (R2 rows) and projecting them — not by a parser change.

| Recipe datum | Reachability | Basis (published accessor / mechanism) |
| --- | --- | --- |
| Companion slot tag (eyes/lashes/tear/mouth) | importer now | `GetScenePostLoadAddonSlot` already read at import; dropped when companions flatten into the untagged `CompanionMeshes` array — carry it through to the recipe |
| Evaluated dial weight + bound UE morph-target | importer now | morphs already import as `UMorphTarget`; dial value via `GetSceneModifierChannelValue` / `GetModifierChannelValue` (published, **unbound**) + correlate modifier↔morph by id/url. Per-leaf "expands to" weights are formula expansion = the consumer's to evaluate (out of importer scope, P1 — "composed dialed shape") |
| ERC rigging-follow deltas (bone `center_point`/`end_point`) | importer now | `GetNodeEndPointX/Y/Z` (published, **unbound**) + the RPN formula API `GetModifierFormulaOutput` / `…FormulaOperation{Count,Op,Val,Url}` whose output targets a bone `center_point`/`end_point` channel. Carried raw; "at full weight" is the consumer's evaluation |
| JCM identity + driving joint | importer now | JCM morphs enumerable (`GetMorphId/Name/Label`); driving joint = the formula operation **input url** (`…FormulaOperationUrl`, published, **unbound**) |
| Per-surface LIE recipe — raw layers (texture + label) | importer now | `Get(SceneMaterialChannel\|Image)Layer{Count,TexturePath,Label}` (published) |
| Per-surface LIE recipe — **blend mode / opacity / transform (mask)** | **parser FR** | unmodeled in the parser — CHANGELOG 1.3.0: "per-layer blend op/transform stay unmodeled." The only datum in the opened file exposed nowhere → the single warranted parser ask |
| Pre-baked iris/sclera marker | importer now | the importer already bakes these (see "G9 untextured eyeball"); flag them so the consumer does not re-composite |
| Recipe schema + emitter version | importer now | from `.uplugin` VersionName / a macro; backs the consumer's SemVer pin — versioning gate R12 |
| HD flag + SubD level | **deferred** | forward-only, no consumer yet (P4); whether SubD level needs a parser exposure is unverified — revisit when UE-native subdivision lands |
| Preset/variant option sets (eye-color/makeup/lip) | **deferred** | the consumer's Preset-resolver stage (P4); applied authoring presets are out of the importer's discovery scope (P1) |

**Correction logged (no silent fails).** A first pass mis-scoped the ERC-follow and JCM-driving
data as parser gaps by reading the importer's binding list (`DsonParserFunctions.h`) instead of
the published header. The RPN formula API and `…NodeEndPoint*` already ship — so those are
importer binding+projection work, not a parser ask. This is exactly the trap the AgentWorkflow
"Requesting parser features" gate warns about ("grep the published accessors first; a 'how'
risks re-specifying an accessor that already ships").

**Outcome.** Intake recorded; the feature is **~mostly importer-side**: bind the already-
published exports the importer doesn't yet wire (formula ops, node end_point, modifier channel
value), project them, and emit a new `UDsonAssetRecipe` from the pipeline — to be sliced as
Implementer task-files, schema first (a reachable-now slice can proceed independent of the FR).
One additive parser exposure is the sole cross-repo dependency. **Implementation note:** the
accessors *exist*; that they are *populated* for Nancy's specific ERC/JCM/dial assets — which
span several loaded documents (figure DSF + morph DSFs) — is to be confirmed in the implementing
slice against the actual asset, not assumed here.

**Parser feature request (what, not how — for relay to the DsonParser Director).**
> **DSON data needed:** per-layer LIE (layered-image) compositing metadata. For each layer of a
> layered `map` stack, in addition to the texture path + label already exposed, the layer's
> **blend mode**, **opacity**, and **transform/offset (mask)** — the layering instructions DAZ
> stores per element of the `map` array, which the parser currently drops (CHANGELOG 1.3.0 notes
> them unmodeled). **Surfaces concerned:** both the per-image (`image_library` LIE entries) and
> per-channel (scene-material channel) layer stacks already exposed for path+label. **Importer
> behaviour it unblocks:** carry each per-surface LIE composite as raw, ordered layers into the
> emitted recipe so a downstream step can re-composite faithfully — the importer composites
> nothing. Subject to the parser's own faithful-passthrough rule (raw values, no evaluation,
> no merge across sections). **Concrete target:** G9 HID Nancy head `diffuse` + `SSS Color`
> (4-layer stacks). This promotes the already-pending exposure noted in `Roadmap.md` ("Out of
> importer scope" → LIE composition recipe) from deferred to a live request.
