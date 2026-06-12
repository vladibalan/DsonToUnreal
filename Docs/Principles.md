# DsonToUnreal Importer — Governing Principles

The Importer's purpose, stated once. **The Roadmap is subordinate to this:** when a
roadmap item conflicts with a principle here, the principle wins and the roadmap is
what changes. These are *requirements*, not rationale — the *why* behind a shipped
decision lives in `Docs/DecisionLog.md`, durable facts in `Docs/Reference.md`, and
current status in `Docs/Roadmap.md`.

These principles are intrinsic: they are motivated by what the Importer is *for*, not
by any particular consumer of its output. The Importer is deliberately agnostic to
what reads its results.

## P1 — Bring everything; translate, don't interpret

The Importer brings into Unreal **everything it can** from a DAZ source, as faithfully
as the engine allows, and **interprets nothing**. It is a translator: DAZ data in,
UE-native data out, with no authoring decisions baked in.

- **Bring all discoverable assets**, including ones a given scene does not use.
  Importing "all" is the same uniform discovery loop as importing "some"; completeness
  is the default, not an opt-in. "Discoverable" is bounded by the imported asset's
  **reference graph** — its dependencies, companions, and transitively-referenced files —
  **not** the wider content library: alternate authoring presets a user applies separately
  (eye-color, lip, and makeup option presets, and the LIE textures they carry) are out of
  scope; selecting and applying them is the authoring layer's job.
- **Don't bake authoring choices.** Where DAZ offers content options (overlay layers,
  per-surface authoring values), the source lands faithfully; combining, composing, or
  baking them is *interpretation*, and interpretation is **out of scope** — it belongs
  to whatever authoring step later consumes the import.

**Survey vs. import — a read-only widening of discovery, not of import.** The
reference-graph bound above scopes what an *import* brings; it does not preclude a
separate **read-only survey** of the installed library that reports faithful, pickable
facts (what an asset declares/structurally *is* — type, generation, dependencies,
preview) **without importing**. The survey enumerates and describes; it **selects,
dedups, buckets, and composes nothing** — those, and all presentation, stay with the
downstream authoring layer (P3). It bakes and decides nothing, so it is translation, not
interpretation: it widens *discovery*, never *import*.

## P2 — Authoring metadata with no UE-asset form is still brought across, faithfully

Not everything in a DAZ source maps to a UE asset. Authoring metadata that has no
native UE-asset home (compositing instructions, per-surface authoring values,
rigging-follow formulas) must still be brought across — emitted as a faithful,
self-describing data artifact alongside the imported assets, **not** left stranded in
the parser layer and **not** interpreted.

> **Speculative — further analysis needed.** The artifact's concrete form, schema, and
> field set are unsettled and are to be finalized against real DSON assets when the
> emitting feature is implemented. The standing rule is only: *carry faithfully what
> has no UE-asset home, and resolve references by id-convention rather than re-encoding
> asset handles.* Whether the artifact can stay pure data (versus needing a typed UE
> object) is itself open, decided at implementation against the actual data.

## P3 — Emission is mechanical, never consumer- or feature-specific

The Importer projects DAZ data uniformly and makes **no assumptions about how its
output is used**. It does not grow bespoke logic per downstream feature — that would
couple it to a consumer and tax every task's context budget with concerns the Importer
should not carry. One generic, faithful projector; all interpretation lives downstream.
(The parser keeps the same posture toward the Importer.)

## P4 — Completeness is bounded by parser exposure; widen it additively, just-in-time

The Importer can only bring what the parser exposes. When a class of DAZ data needs to
be brought but is not exposed yet, the answer is an **additive** parser-ABI extension
(parser repo + DLL), after which the Importer projects it — never a DAZ-parsing
shortcut inside the Importer. New exposures are taken **just-in-time**, when a concrete
need lands, not speculatively up front.

## P5 — Imported originals are immutable source-of-truth

The Importer's outputs are faithful originals. Anything that derives, bakes, or
transforms them does so on **copies**, leaving the imports untouched so variants remain
possible. (Derivation and baking are interpretation — out of scope per P1.)
