# DsonToUnreal — Versioning & Change Announcement

How DsonToUnreal tells a downstream consumer *what it is* and *what changed*. The
consumer is a **co-built UE plugin** that has this repo's source on
disk — so unlike DsonParser (whose consumer is binary-blind), one lightweight carrier
set is enough. Governed by [`Principles.md`](Principles.md); the per-change gate is
[`CodeReviewRules.md`](CodeReviewRules.md) R12.

## The consumer surface (what is versioned)

Version the **published surface**, not any one consumer's needs (P3). Two parts:

1. **Programmatic API** — the public `DsonImporter` module surface:
   - `FDsonImporterModule::IsAvailable()`, `::Get()`, and the `DSONIMPORTER_API`
     entry `::ImportDazAsset(const FDsonImportRequest&) → FDsonImportReport`
     (`Source/DsonImporter/Public/DsonImporter.h`).
   - the request/report/status types `FDsonImportRequest`, `FDsonImportReport`,
     `EDsonImportStatus` (`Source/DsonImporter/Public/DsonImportRequest.h`) —
     **including their fields and enumerators** (e.g. `FDsonImportReport::CompanionMeshes`,
     the `EDsonImportStatus` values), which a consumer reads.
2. **Emitted-output shape** — the documented structure and naming of the assets the
   import writes that a consumer binds to: the `/Game/DazImports/...` layout owned by
   `FDsonAssetUtils` (`CharacterRoot` / `SharedTexturesRoot`). The authoring-metadata
   artifact of P2, once it exists, joins this surface.

Internal implementation (builders, parser-ABI binding, private helpers, the private
`FDsonImportResult`) is **not** the surface — changing it without changing the two
parts above is a PATCH.

## Carriers (what announces a change)

| Carrier | Answers | Consumed by |
|---|---|---|
| **`VersionName`** in `DsonToUnreal.uplugin` | "Which version is this?" | the consumer (read at runtime via UE `IPluginManager`, or off disk) |
| **git tag `vX.Y.Z`** | "Pin / validate against an exact point." | the consumer's reciprocal pin |
| **`CHANGELOG.md`** (repo root) | "What changed each release, and what to re-wire." | the consumer, one targeted read |

`VersionName` is the **single source of truth**; the `CHANGELOG.md` top heading and
the git tag mirror it. The integer `Version` increments by one per release (UE
ordering only). There is **no** macro header and **no** runtime version accessor —
see "Not ported from DsonParser".

## Versioning scheme — SemVer over the consumer surface

`MAJOR.MINOR.PATCH`, where the boundary is **compatibility of the surface above**:

- **MAJOR** — a breaking change: a removed/renamed/re-signatured public symbol, a
  changed call/return contract, **or** a breaking change to the emitted-output shape
  a consumer relies on (moved/renamed output folders, changed asset naming, a removed
  emitted field).
- **MINOR** — additive & back-compatible: a new public symbol, a new optional request
  field, or new emitted output that does not move/rename/remove existing output.
  Existing consumers keep working untouched.
- **PATCH** — an internal fix with the surface unchanged (a builder bug fix, a perf
  change, a private-helper refactor).
- **No bump** — a comment/whitespace/doc-only edit with no functional effect on the
  surface: not versioned, no CHANGELOG entry.

Capability milestones are **not** SemVer: shipping a new figure/shader or import
capability is an **additive MINOR**, not a new MAJOR. A `2.0.0` comes only from a
breaking surface change.

## Baseline — 1.0.0

**1.0.0** is the first *versioned* release: it labels the entire current tree as the
consumer baseline (the API and emitted-output shape above). Pre-versioning history is
**not** retro-numbered — including the 2026-06-10 asset-folder-structure change, which
was breaking for path-reconstructing consumers ([`Roadmap.md`](Roadmap.md)). A
consumer pins to `v1.0.0` = the current tree, new folder layout included.

## Per-change workflow (the R12 gate)

Any change that touches the consumer surface must, in the same change:

1. **Classify** it MAJOR / MINOR / PATCH per the scheme above.
2. **Bump** `VersionName` in `DsonToUnreal.uplugin` (and increment integer `Version`).
3. **Add a `CHANGELOG.md` entry**, newest first, under a heading that leads with the
   new `VersionName` (`X.Y.Z — date · CLASS`): one sigil-prefixed line per change
   (`+` added · `~` changed · `-` removed/deprecated · `!` fixed). Lean — no empty
   scaffolding.
4. **Tag** the release commit `vX.Y.Z` (the Director; the user pushes — pushing stays
   with the user per [`AgentWorkflow.md`](AgentWorkflow.md)).

Enforced as **R12** in [`CodeReviewRules.md`](CodeReviewRules.md).

## Not ported from DsonParser (and why)

DsonParser serves a **binary-blind** consumer that has only shipped artifacts, so it
runs a four-carrier contract (version macros, a `GetVersion()` C export, `@since` tags
+ header banner, CHANGELOG). DsonToUnreal's consumer is **co-built with our source on
disk**, so those extra carriers earn nothing here and are deliberately omitted:

- **No `*Version.h` macro header** — `VersionName` in the `.uplugin` is the one source
  of truth.
- **No runtime version accessor / Blueprint node** — adding consumer-serving runtime
  code cuts against P3 (mechanical, consumer-agnostic emission) and P4 (widen
  additively, just-in-time). UE already exposes `VersionName` at runtime via
  `IPluginManager` (`FindPlugin(TEXT("DsonToUnreal"))->GetDescriptor().VersionName`),
  so a bespoke export would only duplicate engine-provided data. If a concrete
  runtime-gate need ever lands, add it then (P4).
- **No `@since` tags / header banner** — the CHANGELOG + git tag carry "what's new"
  for a source-on-disk consumer.

DsonParser's `docs/versioning.md` is the prior art this adapts **down**, not a
template to copy.

## How a consumer takes up a change

1. **Pin** — record the `VersionName` / git tag validated against (the consumer records
   "validated against DsonToUnreal vX.Y.Z" on its side, e.g. in its own reference doc).
2. **What changed** — read this repo's `CHANGELOG.md` (one targeted read).
3. **Re-wire** — each entry names the API or emitted-output change to adopt; a MAJOR
   entry is the signal to branch/adapt before uptaking.
