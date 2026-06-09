> **FENCED — not part of Importer discovery.** This document is cross-repo intent
> that the Importer's own docs are deliberately kept agnostic to. Nothing here is
> required for, or should inform, any Importer task. If you reached this during
> Importer work, return to `Docs/` proper unless the user explicitly sent you here.

# Cross-repo intent — the consuming "Designer" tool

Off-Importer context for a separate, planned authoring plugin (working name
**Designer**) that consumes the Importer's output. Kept in-repo (for now) so the
*motivation* behind the Importer's intrinsic principles is not lost — but the
Importer's own docs (`Docs/Principles.md`, `Roadmap.md`, `Reference.md`,
`DecisionLog.md`, `MaterialMastersV1.md`, `SubsurfaceProfileV2.md`) state those
principles on their own terms and never name a consumer. **Do not add Designer
awareness to the unfenced docs.**

> Everything in the "Speculative" section below is a cloud-level inference, not
> grounded in any DSON asset inspection. Treat it as direction to validate at
> implementation, never as settled fact.

## The shape: three faithful-projection layers

- **Parser** — DSON → typed model / flat C ABI. Faithful, permissive, knows no consumer.
- **Importer** — model → UE assets + a faithful authoring-metadata artifact. Maximal,
  mechanical, knows no consumer *feature*. Its docs are agnostic precisely because of
  this layer boundary.
- **Designer** — the only layer with authoring intelligence: compose, bake, fit, rig.
  Reads UE assets + the artifact; never calls the parser.

Each layer is a faithful projection of the one below, except the Designer, where all
interpretation concentrates. That concentration is *why* the Importer can stay a thin
translator — and why merging the two would be feature creep and context bloat in both
directions.

## The Designer's purpose

A DAZ-Studio-like character **baking** authoring tool. Scope (not yet started): browse
DAZ content; on demand, call the Importer to bring a chosen asset into UE; then operate
purely on UE assets to bake a character — materials (LIE / makeup composition), shape
(bake morph targets + recompute bone positions and skin binding), add clothes / hair /
accessories (each a fresh Importer call), optionally a predefined rig. Derivation is
destructive and happens on copies; imported originals stay intact.

## How it consumes the Importer

- **Trigger** — one thin programmatic entry point on the Importer ("import this DAZ
  asset on demand; report what was produced"). The Designer is a repeat, selective
  caller (per clothing / hair / accessory), not a one-shot consumer.
- **Assets** — discovered by path/naming convention via the AssetRegistry; layer
  textures matched by **id**, not path.
- **Authoring metadata** — read from the Importer's faithful artifact, with no parser
  dependency. The Designer interprets it (e.g. executes the LIE recipe); the Importer
  only records it.
- Importer and Designer are assumed to run on the **same engine version**, so a binary
  or source dependency is link-equivalent; the trigger facade is the sole shared
  compiled surface.

## Why the Importer emits the metadata (rather than the Designer reading the parser)

So the Designer operates only on UE assets and never links the parser. The cost is an
artifact-emit subsystem on the Importer; it is kept from bloating Importer tasks by the
mechanical-emit rule (`Docs/Principles.md` P3 — one generic projector, isolated, not
per-feature extractors). This is a deliberate reversal of an earlier "Importer stays
agnostic; the consumer reads the parser" stance; the driver is that the Designer must
stay pure-UE and that interpretation should concentrate in exactly one layer.

## Speculative — finalize at implementation, against real DSON

None of the data-structure specifics below were grounded in asset inspection; treat
every item as "further analysis needed":

- The artifact (Designer-term "recipe") schema, format, and field set.
- Whether it reduces to **pure data** (no `UObject` references). If asset inspection
  shows it cannot, a shared compiled type (a small shared module) returns; if it can, a
  convention-located data file plus a versioned format spec is the lighter path, and no
  shared module is needed.
- Formula families and fields: LIE compositing (operation / opacity / color / invert /
  transforms / active), per-surface makeup (Enable / Weight / Roughness Mult), JCM
  rigging (`center_point` / rotation) for shape-bake rig recalculation.
- The facade's exact signature / return contract; whether match-by-id covers every
  reference case.
- Which parser exposures are required and their shape (LIE compositing,
  `center_point` / JCM) — each an additive parser-ABI extension, taken just-in-time.

## How this maps onto the agnostic Importer docs

| Consumer-motivated (lives here) | Intrinsic form (in the Importer docs) |
|---|---|
| Emit a recipe so the Designer avoids the parser | Bring all authoring metadata faithfully; nothing stranded in the parser layer (P2) |
| Emit mechanically so the Designer's context stays small | Translate, don't interpret; mechanical, consumer-blind emission (P3) |
| Parser must expose X because the Designer needs it | Completeness bounded by parser exposure; widen additively, just-in-time (P4) |
| Designer composes LIE; importer defers to it | Composition is interpretation — out of importer scope (P1) |
| A facade so the Designer can drive imports | A programmatic, on-demand import entry point |
| Designer derives on copies; originals untouched | Imported originals are immutable source-of-truth (P5) |
