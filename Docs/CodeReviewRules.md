# DsonToUnreal Code Review Rules

Read this before reviewing or writing any code under
`Source/DsonImporter/`. It encodes the recurring hazards in this plugin and the
review focus the maintainer cares about: **DRY**, **modern-C++ practice at the
codebase's compiler level**, **a compact codebase with no lost
functionality**, and **agent-orientation docs kept in sync with the code**. It
is the standing checklist for code-review runs on this repo.

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
- `DsonImportUtils.h` — `StripUrlFragment`, `FromUtf8`, `NormalizeDazId`,
  `ReadDazUnitScale`, `DazPointToUe`. Header-only on purpose (used across several
  TUs, no new translation unit).
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
  (Shape, Component Responsibilities, Common Change Areas) **and** the Read Order
  + Task Routing table in `AGENTS.md`.
- **A parser export or ABI detail changes** (see R2) → also keep the
  `DsonParserFunctions.h` description in `Docs/ImporterArchitecture.md` accurate.
- **New tooling, build/index step, or workflow** → document it where agents look
  (`AGENTS.md`) so the next session does not rediscover it.
- **A rule, helper, or convention referenced by name elsewhere changes** → fix
  the references (e.g. an `R1–R7` enumeration becomes `R1–R8`).

A doc edit is in scope even when the diff is "just code": stale orientation is a
defect, the same as a stale code comment. Reviewer action: if a change adds,
removes, or renames a file, or changes routing or tooling, and no orientation doc
was touched, flag it and name the doc + section that needs the update.

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
