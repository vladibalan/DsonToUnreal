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

Fiber eyebrows (`G9EyebrowFibers`) deferred → groom. **Open feasibility check for B** (Implementer
to verify against real assets, not assume): that each companion rigs to the *same* Genesis 9
skeleton as the body (a leader-pose prerequisite). Pre-implementation decision record; session
logs append here as slices land.
