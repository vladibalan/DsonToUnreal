# Formula-Driven Character Morphs — Importer v2 Handoff

**Status:** deferred / not implemented. This is the design handoff for finishing
morph-target import so DAZ *character* morphs (e.g. "Laura") import correctly.
Phase 7 v1 imports only **delta-bearing** morphs; this doc covers the missing
**formula-driven control** morphs. It exists so the next session does not have to
re-derive the DSON structure from gzipped files — the structural facts below were
verified directly (Jun 2026).

See also: the parser side (`E:/Work/Code/DsonTest2/DsonParser_Roadmap.md`, v2
"Formula use cases") owns the authoritative DSON formula structure and the parser
API plan. This doc is the **importer** consumer plan.

## The problem (what v1 can't do)

A DAZ character is a **control morph**: a `channel` dial (0..1, label e.g.
"Laura") with **no `morph`/`deltas` of its own**, only `formulas` that drive
*other* morphs' `?value` channels. Those children are often **also** pure
controls — a multi-level tree that bottoms out at delta-bearing leaf morphs in
**separate `.dsf` files**. The `.duf` lists only the top control (with
`channel.current_value` = the dial); the children appear only inside formulas.

v1 discovers morphs from `scene.modifiers` URLs and imports any with deltas. For
Laura that yields the navel HD corrective (72 deltas) but **not** her face/body
identity, because the identity is `Σ(leaf_deltas × evaluated_value)` across a
formula tree v1 never traverses or evaluates.

Worked example (verified):
```
Laura for Genesis 9.duf  → scene.modifiers:
  body_bs_Navel_HD3            cv=1 → 72 deltas              [imports today]
  SkinBinding                       → Genesis9.dsf (not a morph)
  Laura_figure_ctrl_Character  cv=1 → 0 deltas, 2 formulas:
     ├─ …/Laura_head_bs_Head.dsf#Laura_head_bs_Head?value  (×dial) → 0 deltas, 44 formulas → leaves
     └─ …/Laura_body_bs_body.dsf#Laura_body_bs_body?value  (×dial) → 0 deltas,  9 formulas → leaves
  (files under …/data/Daz 3D/Genesis 9/Base/Morphs/Daz 3D/Base Characters 9/)
```

## Prerequisite (parser v2)

The parser must expose per-morph formulas. Planned API (see parser roadmap items
1–3): `GetMorphFormulaCount`, `GetMorphFormulaOutput`, and per-operation
`...OperationCount/Op/Val/Url`. The parser stays single-document — it does **not**
recurse external files or evaluate. Bind these as new optional rows in
`DsonParserFunctions.h` (R2), same `uint64_t/int32_t` style.

## Importer algorithm

Extend `DsonMorphBuilder` (it already loads external morph `.dsf` files and has
the discovery/dedup scaffolding):

1. **Seed.** For each `scene.modifiers` entry, capture its `current_value` (the
   dial) keyed by the resolved morph `#fragment`/file. Default 1.0 if absent.
2. **Traverse the formula graph (cross-file, recursive).** Starting from each
   control morph that has formulas:
   - For each formula, parse the `output` URL:
     `Scheme:/path/File.dsf#ModifierId?property`. **Only follow `?value`
     outputs** (ignore bone/rigging targets like `?center_point/x` or rotation —
     those are pose-driven JCMs, the *other* formula consumer, out of scope here).
   - Strip `?property` and `#fragment`, resolve the path via
     `FDsonContentRoots::ResolveUrl`, load with `FDsonLoadedDocument` (dedupe by
     normalized path; you already do this). Recurse into that file's morph: if it
     has deltas → leaf; if it has formulas → recurse. Guard against cycles with a
     visited-set of `file#fragment`.
3. **Evaluate the RPN** (`operations`: `push` const/url, `mult`, `div`, `add`,
   `sub`, `pow`, `spline_tcb`) to get each edge's multiplier, and propagate the
   driver value down the tree: `child_value = parent_value ⊗ formula_result`.
   Respect channel `clamped`/`min`/`max`. For the common character case the ops
   are just `push(url) push(1) mult`, i.e. `child_value = parent_value`.
4. **Compose.** Accumulate per leaf: `effective_value = Σ over paths`. Net vertex
   delta = `Σ_leaves (leaf_delta[v] × effective_value)`, each leaf delta through
   `DsonImportUtils::DazPointToUe` (same flip as v1 — R4).

## Compose target — open design decision

Two ways to deliver the composed result (pick per product need; this is the main
unresolved choice):

- **(A) Bake the dialed character into the base mesh.** Add the net delta to base
  vertex positions in `FDsonMeshBuilder` (at `ReadVertexPositions`, before
  `CommitMeshDescription`). Result: the imported rest mesh *is* Laura. Simplest to
  make "Laura look like Laura"; loses the ability to dial her down.
- **(B) Emit a combined morph target** (e.g. `MT_Laura`) at weight 0, plus
  optionally the individual leaf morphs. Preserves dialing; the user sets weight 1
  to get Laura. Fits the existing MeshDescription morph path (one extra registered
  attribute whose deltas are the composed net).

Recommendation: **(B)** as the default (consistent with "morph targets" framing
and the v1 MeshDescription pipeline), with (A) as an optional toggle later.

## Integration points
- `DsonMorphBuilder.{h,cpp}` — formula traversal/eval + compose; reuse
  `CollectExternalMorphPaths`/loader, name dedup, bounds checks.
- `DsonParserFunctions.h` — new formula export rows (R2).
- `FDsonMeshBuilder::ReadVertexPositions` — only if option (A) baking is chosen.

## Edge cases / gotchas
- Follow only `?value` morph outputs; skip rigging/pose outputs (JCM territory).
- Leaf morphs are shared across characters — dedupe by `file#fragment`, not name.
- Cycle/diamond protection in the graph walk (visited set).
- Leaf delta vertex indices are base-figure indices — same bounds check as v1.
- `spline_tcb` and `pow` are rare for character dials but must not crash the
  evaluator; implement them or fail that edge permissively (skip + warn, R7).
- Keep it permissive: a missing/odd formula skips that branch, never aborts import.

When implemented, update `Docs/Roadmap.md` (move this from Deferred to Done) and
`Docs/ImporterArchitecture.md` (DsonMorphBuilder responsibility), per R8/R9.
