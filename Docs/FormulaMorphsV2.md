# Formula-Driven Character Morphs — Discovery Record + Out-of-Scope Evaluator Reference

> **Scope (2026-06-10):** the formula **evaluator / composer** described below is
> **out of importer scope.** Evaluating formulas and baking the composed dialed shape
> is interpretation, which belongs to the downstream authoring layer, not this importer
> (`Docs/Principles.md` P1; `Docs/Roadmap.md` → "Out of importer scope — composed dialed
> shape"; why → `Docs/DecisionLog.md` 2026-06-10). This is **not** an importer feature
> plan — it is kept as the record of the in-scope discovery import that shipped, plus
> reference for whoever builds that evaluator downstream.

**Status:** parser formula-output API **delivered** for discovery. The importer
now follows scene and external modifier formula outputs whose query property is
exactly `?value`, resolves those referenced files transitively, and imports every
delta-bearing morph in each reached file as its own ordinary morph target at
weight 0. It does **not** evaluate formulas, seed dial values, or compose a net
character shape — and, per the scope banner above, will not; that is the authoring
layer's job.

See also: the parser side (`E:/Work/Code/DsonTest2/DsonParser_Roadmap.md`, v2
"Formula use cases") owns the authoritative DSON formula structure and the parser
API plan. This doc is the downstream **evaluator reference**, not an importer plan.

## The remaining problem (what discovery-only import can't do)

A DAZ character is a **control morph**: a `channel` dial (0..1, label e.g.
"Laura") with **no `morph`/`deltas` of its own**, only `formulas` that drive
*other* morphs' `?value` channels. Those children are often **also** pure
controls — a multi-level tree that bottoms out at delta-bearing leaf morphs in
**separate `.dsf` files**. The `.duf` lists only the top control (with
`channel.current_value` = the dial); the children appear only inside formulas.

The importer discovers direct `scene.modifiers` URLs plus transitive `?value`
formula-output files and imports any morphs with deltas. For Laura that exposes
her leaf morph targets at rest, but it still does **not** evaluate the formula
tree or compose `Σ(leaf_deltas × evaluated_value)` into a dialed Laura shape.

Worked example (verified):
```
Laura for Genesis 9.duf  → scene.modifiers:
  body_bs_Navel_HD3            cv=1 → 72 deltas              [imports]
  SkinBinding                       → Genesis9.dsf (not a morph)
  Laura_figure_ctrl_Character  cv=1 → 0 deltas, 2 formulas:
     ├─ …/Laura_head_bs_Head.dsf#Laura_head_bs_Head?value  (×dial) → 0 deltas, 44 formulas → leaves [discovered/imported at 0]
     └─ …/Laura_body_bs_body.dsf#Laura_body_bs_body?value  (×dial) → 0 deltas,  9 formulas → leaves [discovered/imported at 0]
  (files under …/data/Daz 3D/Genesis 9/Base/Morphs/Daz 3D/Base Characters 9/)
```

## Parser surface — delivered; evaluator accessors await importer binding (corrected 2026-06-08)

The parser now exposes formula outputs in **two index spaces** — raw
`modifier_library` index and `scene.modifiers` index — because control morphs
have no `morph`/`deltas` block and so never appear in the filtered morph list.
The importer binds the output-count/output accessors as optional exports and uses
them for discovery only:

```
GetModifierFormulaCount / Output
GetSceneModifierFormulaCount / Output
```

The parser-side request for formula-output discovery is therefore **delivered**.
For the future evaluator, the parser's stored RPN operations remain relevant:
`Stage`, `OperationCount`, `Op`, `Val`, and `Url` on both formula index spaces.
Do not bind those in the discovery-only importer.

Those operation accessors, plus the existing `GetModifierCount/Id/Name/Type`,
`GetMorph{Count,Name,Label,GeometryId,Delta*}`, and `GetSceneModifier{Count,Url}`,
are enough to *traverse and evaluate* the tree. Three further facts the evaluator
needs are **also already exposed by the parser** — the discovery-only importer just
hasn't bound them, so this is an importer-side binding gap, not a parser request:

1. **Channel `current_value` (the dial).** Evaluation seeds from each modifier's
   dial, and a correct RPN pass must resolve `push(url)` ops that reference *another*
   channel's value; defaulting every seed/push to 1.0 mis-evaluates any non-unit push
   (common in auto-follow / corrective chains). Read via the channel-value accessors
   below.
2. **Channel domain (`min`/`max`/`clamped`).** Needed to honor DAZ clamping while
   propagating values down the tree. Read via the channel-domain accessors below.
3. **Output→leaf correlation.** Formula `output` URLs name a modifier by `id`
   (`#fragment`); `GetMorphId` gives that id directly, so an output maps to its
   delta-bearing leaf without assuming `GetMorphName == id`.

### Future evaluator: exports to bind (importer-side, not a parser request)

These exports already exist in the shipped ABI but are **unbound** today (the
discovery-only importer doesn't need them). When the importer grows formula
evaluation/composition, add each as an optional row in `DsonParserFunctions.h` (R2)
and confirm its runtime semantics on first use — naming the real exports is fine
here: they ship, so this is binding, not a parser request.

**Seed / `push(url)` operand values (correctness-critical):**
- `DsonDocument_GetSceneModifierChannelValue`, `DsonDocument_GetModifierChannelValue`
  — the channel dial that seeds evaluation and resolves `push(url)` operands. Confirm
  whether each returns `current_value` (with a `value` fallback), as the evaluator
  assumes.
- `DsonDocument_GetSceneModifierId` — to key seed dials by scene-modifier id.

**Channel domain (clamp-correct evaluation):**
- `DsonDocument_Get{SceneModifier,Modifier}ChannelMin` / `…Max` / `…Clamped`.

**Output→leaf correlation:**
- `DsonDocument_GetMorphId` — the morph `id` matched against the `#fragment` in a
  formula output URL, so an output maps to its delta leaf without assuming the morph
  name equals its id.

**Confirm only (existing-export behaviour the evaluator relies on):**
- `Get{Modifier,SceneModifier}FormulaOutput` returns the **full** output URL with the
  `?property` suffix verbatim (e.g. `…/Head.dsf#Laura_head_bs_Head?value`), so the
  importer can keep only `?value` outputs and drop rigging outputs (`?center_point/x`,
  rotation).
- Operation `op` strings are the literal DAZ tokens (`push`, `mult`, `div`, `add`,
  `sub`, `pow`, `spline_tcb`), and a `push` carries exactly one of `Val`/`Url` so a
  constant-push is distinguishable from a url-push.

## Future evaluator algorithm

Extend `DsonMorphBuilder` beyond the current discovery-only file walk:

1. **Seed.** For each `scene.modifiers` entry, capture its `current_value` (the
   dial) keyed by the resolved morph `#fragment`/file. Default 1.0 if absent.
2. **Traverse the formula graph (cross-file, recursive).** Starting from each
   control morph that has formulas:
   - For each formula, parse the `output` URL:
     `Scheme:/path/File.dsf#ModifierId?property`. **Only follow `?value`
     outputs** (ignore bone/rigging targets like `?center_point/x` or rotation —
     those are pose-driven JCMs, the *other* formula consumer, out of scope here).
   - Strip `?property`, resolve the file path via `FDsonContentRoots::ResolveUrl`
     (which ignores `#fragment` for disk lookup), load with `FDsonLoadedDocument`,
     and keep the fragment separately for modifier identity. Recurse into that
     file's morph: if it has deltas → leaf; if it has formulas → recurse. Guard
     against cycles with a visited-set of `file#fragment`.
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
  the discovery loader, name dedup, bounds checks.
- `DsonParserFunctions.h` — new formula export rows (R2).
- `FDsonMeshBuilder::ReadVertexPositions` — only if option (A) baking is chosen.

## Edge cases / gotchas
- Follow only `?value` morph outputs; skip rigging/pose outputs (JCM territory).
- Current discovery strips the formula `?property` query, then passes the URL with
  `#fragment` intact to `FDsonContentRoots::ResolveUrl`; that resolver strips the
  fragment for disk lookup.
- Leaf morphs are shared across characters — dedupe by `file#fragment`, not name.
- Cycle/diamond protection in the graph walk (visited set).
- Leaf delta vertex indices are base-figure indices — same bounds check as v1.
- `spline_tcb` and `pow` are rare for character dials but must not crash the
  evaluator; implement them or fail that edge permissively (skip + warn, R7).
- Keep it permissive: a missing/odd formula skips that branch, never aborts import.

This evaluator is out of importer scope (see the scope banner above). Picking it up
would be a scope **reversal** — a Roadmap/Principles decision recorded in
`Docs/DecisionLog.md` first (R9), not a quiet pickup of this plan.
