# DsonToUnreal Code Review Rules

Read this before reviewing or writing any code under
`Source/DsonImporter/`. It encodes the recurring hazards in this plugin and the
review focus the maintainer cares about: **DRY**, **modern-C++ practice at the
codebase's compiler level**, **a compact codebase with no lost
functionality**, and **agent-orientation docs kept in sync with the code, tight,
and tiered**. It is the standing checklist for code-review runs on this repo, and
for any edit to the orientation docs themselves (R10).

This plugin targets **Unreal Engine 5.4.4** and is an editor module
(`DsonImporter`). It binds a third-party C ABI from `DsonParser.dll`. Those two
facts drive most of the rules below.

## How to conduct the review

1. Read `Docs/ImporterArchitecture.md` for the file map, then route to the
   component(s) in scope. Do not scan the whole tree.
2. **Lead with findings**, ordered by severity, each with a `file:line`
   reference. Don't summarize the code first.
3. Separate findings into the three buckets the maintainer reviews on:
   **DRY**, **recommended C++ practice (for this codebase's standard)**, and
   **compactness (remove without losing functionality)**. Note correctness/
   efficiency issues too, but label them as out-of-focus.
4. Propose fixes in batches by risk: pure deletions first, mechanical
   consolidation next, structural rewrites last. The maintainer runs builds and
   signs off between batches — hand each batch back to build before the next.
5. After any edit, self-audit against the Quick Checklist at the end and state
   the result naming the rules checked, not "looks fine".

## Hard rules

### R1 — UE 5.4.4 API compatibility is mandatory
Every API used must exist and have the **5.4** signature. UE changes container
and math APIs between minor versions; do not assume a newer overload.
- Known trap: `TArray`/`FString::RemoveAt(Index, Count, bAllowShrinking)` takes a
  **`bool`** in 5.4. The `EAllowShrinking` enum overload is **5.5+** — do not use
  it here.
- When introducing any container/string/math call you're not certain about,
  verify it against 5.4, and prefer the form already used elsewhere in the repo.
- Stay within the editor/engine modules already listed in `DsonImporter.Build.cs`;
  adding a dependency is an architectural decision, not a cleanup.

### R2 — The parser C ABI has one source of truth
`DsonParserFunctions.h` defines `DSON_PARSER_API_LIST` (an X-macro). It generates
the `FDsonParserAPI` struct fields, `IsValid()`, and the DLL-binding loop in
`DsonImporter.cpp`.
- **Add/change/remove an export in exactly one place**: a row in that list.
  Never reintroduce parallel typedef/struct/load lists.
- Row shape: `X(Required, Ret, Member, ExportName, Params)`. `Required==1` means
  the export logs as **Error** on load failure **and** is checked by `IsValid()`;
  `0` means optional (logs **Warning**). Keep those two meanings aligned — if a
  member is needed by `IsValid()`, it is `Required==1`.
- Signatures must match the DLL's C ABI exactly. The historical mix of
  `DsonDocumentHandle`/`int` (asset & scene-node accessors) vs
  `uint64_t`/`int32_t` (everything else) is deliberate — preserve it.

### R3 — Parser handles are RAII; parser strings are copied immediately
- Acquire every document handle through `FDsonLoadedDocument`
  (`LoadFromFileAsError`/`LoadFromFileAsWarning`). Do **not** hand-roll
  `Create`/`LoadFromString`/`Destroy`; that reintroduces leak-on-early-return
  paths the wrapper exists to remove. Use the optional `OutError` out-param when a
  caller must surface the failure text in UI.
- A `const char*` returned by the parser is transient. Convert it to `FString`
  **before the next parser call** (use `DsonImportUtils::FromUtf8`). Flag any code
  that holds a parser `const char*` across another parser call.

### R4 — Don't duplicate logic; shared helpers have a home
Before adding a small helper, check whether it already exists:
- `DsonImportUtils.h` — `StripUrlScheme`, `StripUrlFragment`, `FromUtf8`,
  `NormalizeDazId`, `ReadDazUnitScale`, `DazPointToUe`. Header-only on purpose (used
  across several TUs, no new translation unit).
- `FDsonContentRoots` — URL decoding and content-root resolution.
- `FDsonAssetUtils` — package creation and asset saving.
- `GenerationToString` (in `DsonValidator.h`) — Genesis generation labels.

Reviewer actions:
- Reject a second copy of any of the above; route the call site to the shared one.
- **The DAZ→UE coordinate flip (`DazPointToUe`) is a correctness-critical shared
  helper.** Skeleton bone transforms and mesh vertex positions MUST use the same
  mapping or mesh and skeleton mirror apart and skin weights tear. Never inline a
  second copy of the `(z, -x, y)` conversion.
- If you write a helper used in 2+ files, put it in a shared header, not a
  file-local `static`.

### R5 — Compact, but never drop functionality
The goal is the smallest code that keeps every behavior. Look for and remove:
- **Dead parameters** threaded through call chains (e.g. an `OutputFolder` no one
  reads). Verify a param is actually used before keeping it.
- **`UENUM()`/`UPROPERTY()`/`USTRUCT()` with no reflection consumer.** There is no
  `*.generated.h` include anywhere in this module and no Blueprint exposure;
  reflection macros here only add UHT cost. Plain `enum class`/structs are correct
  for internal-only types. If reflection is genuinely needed later, that's a
  deliberate change with the required `.generated.h` include — not a default.
- **Debug scaffolding in the import path.** Verbose `[uv]`/fn-pointer-presence
  dumps, per-element sample logging, and `[0..N]` array dumps do not belong at
  `Log` in shipping import code. Keep one-line summaries and genuine warnings;
  gate deep diagnostics behind `Verbose`/`VeryVerbose` or move them to
  `DsonMaterialDiagnostic`.
- Redundant locals (declare at point of use, `const` where possible), and
  re-declared typedefs (include the canonical header instead).

When removing anything, confirm it isn't load-bearing: an unused-looking
parameter, enum, or field may be intentional scaffolding — if in doubt, ask
rather than delete.

### R6 — Match the codebase's C++ idiom
The module compiles at UE 5.4's default (**C++20**). Prefer the modern form when
it matches surrounding code:
- `static_cast<float>(x)` over C-style `(float)x` in new/edited lines.
- `if constexpr` for compile-time branches (e.g. in the X-macro bind) to avoid
  MSVC C4127 constant-conditional warnings.
- Don't reach for C++ features that fight the engine's conventions; mirror the
  patterns already in the file you're editing.

### R7 — Preserve the permissive-parser and return-value conventions
- The importer is intentionally permissive: missing optional parser functions and
  fields keep defaults; malformed entries are skipped with a warning, not a hard
  failure. Don't "tighten" this into rejections during a cleanup.
- Builders return `nullptr`/`false` on failure and log enough file/path/index
  context to reproduce. Keep that contract; don't swallow the context when
  consolidating log calls.
- Changing a public signature in `DsonParserFunctions.h` or a builder's public
  method is a breaking change for the rest of the module — call it out explicitly,
  don't slip it into a "minor" edit.

### R8 — Keep agent-orientation docs in sync with the change
The orientation docs are how an LLM agent (or human) navigates this plugin under
a limited context budget; stale orientation silently misroutes the next session,
so treat doc updates as part of the change, not a follow-up. When a change alters
the file layout, a component's responsibility, the routing an agent follows, or
the available tooling, update the relevant doc **in the same change**:
- **A source file is added, removed, renamed, or its responsibility changes** →
  update the file map and component list in `Docs/ImporterArchitecture.md`
  (Shape, Component Responsibilities) **and** the Read Order
  + Task Routing table in `AGENTS.md`.
- **A parser export or ABI detail changes** (see R2) → also keep the
  `DsonParserFunctions.h` description in `Docs/ImporterArchitecture.md` accurate.
- **New tooling, build/index step, or workflow** → document it where agents look
  (`AGENTS.md`) so the next session does not rediscover it.
- **A rule, helper, or convention referenced by name elsewhere changes** → fix
  the references (e.g. an `R1–R8` enumeration becomes `R1–R9`).

A doc edit is in scope even when the diff is "just code": stale orientation is a
defect, the same as a stale code comment. Reviewer action: if a change adds,
removes, or renames a file, or changes routing or tooling, and no orientation doc
was touched, flag it and name the doc + section that needs the update.

### R9 — Keep the project roadmap current as work lands
`Docs/Roadmap.md` is the single source of truth for **project status**: phase
completion, deferred-to-v2 features, known latent issues, and the cleanup
backlog. It exists to replace external, quickly-stale handoff documents — so it
is only worth having if it is updated **in the same change that alters the status
it records**, never as a follow-up. Update it when, and as part of the change
where:
- **A phase or feature completes** → mark it Done with a one-line note on the
  shipped scope (and what was deliberately left as v1).
- **A bug is fixed** → remove it from Known Issues, noting the fix location.
- **A new bug or limitation is discovered** → add it to Known Issues with enough
  file/symptom context to reproduce.
- **Work is deferred** → record it under Deferred / v2 rather than losing it in a
  chat that will not be re-read.
- **A cleanup item is done or newly identified** → tick or add it in the backlog.

Keep `Roadmap.md` about *status*, not mechanism: things owned elsewhere are
referenced, not restated, so they cannot drift — load-bearing technical
invariants (the coordinate flip — see R4), material-parameter contracts
(`MaterialMastersV1.md`), dated decision rationale and postmortems
(`Docs/DecisionLog.md`), and durable engineering facts/lessons/gotchas
(`Docs/Reference.md`).

A status change with no roadmap edit is the same defect class as stale
orientation under R8. Reviewer action: if a change completes or defers a phase,
or fixes or adds a bug, and `Docs/Roadmap.md` was not touched, flag it.

### R10 — Keep the orientation docs tight and tiered
The orientation docs are read under a limited context budget, so bloat in a
hot-path doc taxes every future session — and guidance alone has already failed
once (R8 and R9 existed while `Docs/Roadmap.md` grew to 480 lines). Keep them
economical; this rule binds **both roles**, a Director doing doc-only work
included.
- **One tier per doc — put content where it belongs, not where you happen to be.**
  governing principles → `Docs/Principles.md`; status → `Docs/Roadmap.md`; dated
  rationale/postmortems → `Docs/DecisionLog.md`;
  durable facts/lessons/gotchas → `Docs/Reference.md`; rules → this file; roles →
  `Docs/AgentWorkflow.md`; code layout → `Docs/ImporterArchitecture.md`; audit
  routing → `Docs/AuditGuide.md`; editor tooling → `Docs/Tooling.md`;
  entry/routing → `AGENTS.md`; master-parameter contract → `MaterialMastersV1.md`.
- **Point, don't duplicate.** If another doc owns a fact, link it by name. The
  R1–R12 list and the Director/Implementer role model each have exactly one home;
  never re-list them elsewhere.
- **Status holds *current* state only.** When work ships, move its rationale to
  `Docs/DecisionLog.md` and its durable facts to `Docs/Reference.md`; do not let
  `Docs/Roadmap.md` accumulate history — the precise failure that caused the
  480-line bloat.
- **Name no specific downstream consumer in forward-looking docs** (preserves P3):
  they refer to a generic *downstream consumer*. A downstream Director's request may
  name its own plugin, but the resolution landing here says "a consumer requested...";
  only `Docs/DecisionLog.md` (dated history) may name *which* consumer, when relevant.
- **Prefer a table or a pointer over repeated prose blocks.**
- **Soft line budgets for hot-path docs** — crossing a ceiling is a signal to
  relocate or split, not to keep growing (mirrored in the `dson-doc-guard` hook,
  which warns on save): `AGENTS.md` ≤ 135, `Roadmap.md` ≤ 260, `AuditGuide.md` ≤
  120, `ImporterArchitecture.md` ≤ 195, `AgentWorkflow.md` ≤ 245,
  `CodeReviewRules.md` ≤ 265, `Tooling.md` ≤ 80. The cold/on-demand docs —
  `DecisionLog.md`, `Reference.md`, `FormulaMorphsV2.md`, `SubsurfaceProfileV2.md`,
  `MaterialMastersV1.md`, `Principles.md`, `Versioning.md` — are **exempt** (off the hot path).

Reviewer action: if a change restates content another doc owns, parks history in a
status doc, or pushes a hot-path doc past its budget without relocating, flag it
and name the tier the content belongs in.

### R11 — Keep the in-repo settings mirror in sync with the active global settings
The settings that actually run are the **user-global** `~/.claude/settings.json`;
this repo's `.claude/settings.json` is an unused mirror (added dirs don't load their
own `.claude`) kept for review and version history. The two must stay
**content-equivalent**: change one — a Director task, settings being config — and
make the same change to the other in the same edit. The only allowed divergence is
hook paths: the mirror uses repo-relative paths (`.claude/hooks/...`) for
portability, the global file absolute. A drifted mirror is stale-orientation-class
drift (R8).

### R12 — Version the consumer surface; announce the change with it
A co-built downstream plugin consumes this one. Its **consumer surface**
is (1) the public programmatic API — `Source/DsonImporter/Public/DsonImporter.h`
(`FDsonImporterModule::IsAvailable`/`Get`/`ImportDazAsset`) and the request/report/status
types in `DsonImportRequest.h` — and (2) the documented *shape* of emitted output a
consumer binds to (the `/Game/DazImports/…` layout owned by `FDsonAssetUtils`). Version
the **surface**, not any one consumer's needs (P3). Any change that touches it must, in
the same change:
- **Classify** it MAJOR / MINOR / PATCH per [`Versioning.md`](Versioning.md) — MAJOR also
  covers a breaking change to emitted-output shape; a no-effect doc/whitespace edit is no bump.
- **Bump** `VersionName` in `DsonToUnreal.uplugin` (the SemVer source of truth; also
  increment the integer `Version`).
- **Add a `CHANGELOG.md` entry** (root, newest first) under a heading leading with the new
  `VersionName` (`X.Y.Z — date · CLASS`), one sigil-prefixed line per change (`+` `~` `-` `!`).
Full scheme, baseline, and what is deliberately not ported from DsonParser →
[`Versioning.md`](Versioning.md). Reviewer action: a consumer-surface change with no
`VersionName` bump + `CHANGELOG.md` entry is stale-orientation-class drift (R8).

## Quick checklist (state results after each change)

- [ ] R1: every API call exists with its **UE 5.4** signature (no 5.5+ overloads).
- [ ] R2: parser-export changes are a single `DSON_PARSER_API_LIST` row; `Required`
      flag matches `IsValid()` intent; signature matches the DLL ABI.
- [ ] R3: handle via `FDsonLoadedDocument`; parser `const char*` converted before
      the next parser call.
- [ ] R4: no duplicated helper; shared ones reused; coordinate flip not re-inlined.
- [ ] R5: no dead params, no reflection macros without a consumer, no debug
      scaffolding at `Log`; nothing load-bearing removed.
- [ ] R6: `static_cast` and `if constexpr` per codebase idiom.
- [ ] R7: permissive parsing and failure-return/log-context contracts intact;
      breaking changes flagged.
- [ ] R8: agent-orientation docs updated to match the change —
      `Docs/ImporterArchitecture.md` (file map / components) and `AGENTS.md`
      (Read Order, Task Routing, tooling); name-referenced enumerations fixed.
- [ ] R9: `Docs/Roadmap.md` updated for any phase/feature/bug/deferral/cleanup
      status change this diff makes.
- [ ] R10: doc content sits in the tier that owns it; nothing another doc owns is
      restated; no hot-path doc pushed past its soft line budget without relocating;
      forward-looking docs name no specific downstream consumer.
- [ ] R11: in-repo `.claude/settings.json` mirrors the active global
      `~/.claude/settings.json` (content-equivalent; only hook paths differ).
- [ ] R12: consumer-surface change (public API or emitted-output shape) bumps
      `VersionName` + integer `Version` and adds a `CHANGELOG.md` entry.
