# Layered Figure Import — parent / lean-delta split (design)

Design + scope for the import-output re-architecture that splits a base **figure** from
the **characters** built on it, so shared figure data (notably the base + JCM morph set)
is imported once instead of copied into every character.

This is a **design/scoping** doc (cold, off the hot path). Status lives in
[`Roadmap.md`](Roadmap.md); shipped rationale will go to [`DecisionLog.md`](DecisionLog.md);
the durable UE constraint below belongs in [`Reference.md`](Reference.md) once the work
lands. Governing principles: [`Principles.md`](Principles.md) (P1/P3/P5).

## Problem

Today each character imports as a **self-contained** asset set under `Characters/<name>/`:
its own `USkeletalMesh` carrying *all* morphs — base shaping + the scene-gated JCM
correctives (≈216 on a current G9 character) + the character's own morphs — plus its own
`_Skeleton` and materials. Only source textures are shared (`Library/Textures/`). The base
figure's morph set is identical across every character built on that figure, so importing N
characters stores N copies of it. This does not scale to dozens of characters.

## Model

**Split the tree.** A base **figure** (e.g. the G9 base) is imported once as a shared
**parent** asset. Each character imports as a **lean delta**: its own standalone
`USkeletalMesh`, bound to the parent's shared `_Skeleton`, carrying **only the morphs the
parent doesn't already have**. Each morph is emitted **once, on the asset it belongs to** —
base shaping + JCM correctives on the parent, character-specific morphs on the delta. This
is faithful, mechanical projection (P1/P3) with no duplication, and the parent stays an
immutable original (P5).

**Deltas are intentionally incomplete.** A standalone delta mesh cannot apply the parent's
morphs — UE stores morph targets per-`USkeletalMesh` (serialized into the mesh; each
`UMorphTarget` is bound 1:1 to one `BaseSkelMesh`) with **no cross-mesh sharing**. So base
expressions and JCM correctives are absent from the delta mesh on its own. **Composing**
parent + delta into a finished, posable character is a **downstream authoring step** — out
of importer scope (P1). The importer emits faithful split parts and stops.

## Folder scheme — identity-keyed, not nested

- Parent: `…/DazImports/Figures/<figureId>/`
- Delta: `…/DazImports/Characters/<characterId>/` (today's `Characters/<name>/`;
  `CharacterName` is already the per-character identity)

`<figureId>` is a deterministic, stable function of the base figure (derived from the
resolved base-figure DSF — exact source confirmed at implementation). Every character built
on the same figure resolves to the same parent.

**The dependency tree is carried as data, not as folder nesting.** Folders stay flat and
identity-keyed; each delta declares its parent(s) in **ancestry metadata** on its recipe.
Nesting is avoided deliberately: a character can derive from *several* parents (a DAG, not a
tree); nested paths can hold only one parent and would re-home assets — breaking references —
when ancestry changes; and the downstream composer needs the ancestry as data regardless.

## Ancestry metadata

A list-shaped field on `UDsonAssetRecipe` naming the delta's parent node(s). Shaped for
N-level / DAG from the start; exercised at **depth-1** (figure → character) now.

## Morph partition

- **Parent exists** → the delta is a **name set-difference**: the character's reachable
  morphs minus the morph names already on the parent (the parent is the authority).
- **Parent absent** (lazy create) → split by **provenance**: morphs sourced from the base
  figure's own files (its `modifier_library` + the shared corrective subtree) seed the new
  **minimum parent**; morphs from the character's own product go on the delta.
  - *Implemented in S2* as **curated** — figure DSF `modifier_library` + accepted correctives
    (`DiscoveredCorrectiveDsfPaths`) → parent; formula-reachable externals → delta. **Not**
    path-based (deferred to an explicit full-parent import). Rationale + the real-Nancy
    grounding: `DecisionLog.md` (2026-06-14).

## Skip & no-overwrite

- A parent counts as "present, skip it" by a **completeness marker** — the presence of its
  emitted **recipe/manifest** asset (emitted last, so its presence ⇒ a finished import) —
  not bare folder existence.
- **Never overwrite an existing parent/ancestor.** Missing levels are created lazily up the
  chain to the base figure; an existing level is skipped untouched.
- Growing a lazily-created **minimum** parent into a fuller one, or any reconciliation, is a
  **separate explicit full-parent import and/or a downstream task** — not this importer.
- **Consequence (accepted):** because the minimum parent is whatever the first character
  needed, delta contents are **import-order-dependent**. The clean path is to import the
  figure's dedicated full-parent source first; lazy-minimum is the fallback.

## Staging (P4 — widen just-in-time)

Build the **figure → character** tier now. Keep the ancestry field N-level-ready, but
generalize the chain-walk to **character-on-character** (depth-2+) only when a concrete
multi-level asset is in hand — no speculative deep-tree machinery up front. (Such assets are
known to exist for older generations; ground the node/edge rule against a real one when that
work is taken.)

## Slice plan

| Slice | Scope |
|---|---|
| S1 | Foundation: parent-figure **identity** + `Figures/<id>/` path API + **completeness-marker** check + **no-overwrite/skip** primitive — established and exercised by creating the parent node lazily. Additive; existing character output unchanged. |
| S2 | Emit the base figure as the standalone **parent asset** (shared `_Skeleton` + base mesh + base/JCM morphs) under `Figures/<id>/`, lazily, honoring skip + no-overwrite. |
| S3 | **Lean delta**: character mesh omits the parent's morphs (the partition rule), binds the parent's shared `_Skeleton`. The core emitted-output-shape change. |
| S4 | **Ancestry** field on `UDsonAssetRecipe` (+ builder), populated at depth-1. |

(Companion meshes follow the same split — shared base geometry + base morphs on the parent,
character materials on the delta — handled within the slices, not re-decided.)

## Governance

This changes the `/Game/DazImports/` **emitted-output shape** a consumer binds to →
**MAJOR** (R12). To avoid shipping a half-built major, internal slices accumulate on `Base`
without a per-slice bump; a **single** `VersionName`→`2.0.0` bump + `CHANGELOG` entry +
`v2.0.0` tag lands at the workstream **release close-gate**, when the lean-delta model is
functional end-to-end and a consumer can adopt it. A co-built downstream consumer adopts the
new layout + ancestry field in lockstep (coordinated via the maintainer; this doc names no
specific consumer, per R10/P3).

## Open questions for the first design read (S1)

The S1 task is `Feedback requested: yes` — the Implementer confirms these in a design read
before coding:

1. **Figure identity source** — DSF basename vs the figure's `asset_info` id vs node id;
   must be stable and collision-free across figures.
2. **Completeness marker** — reuse the recipe asset's presence, or a dedicated lightweight
   sentinel?
3. **S1 boundary** — does S1 also hoist the shared `_Skeleton` into the parent, or does that
   stay in S2?
4. **Versioning sequencing** — confirm no per-slice bump; one MAJOR at the release
   close-gate.
