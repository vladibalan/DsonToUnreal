# Agent Workflow — Director & Implementer

This plugin is worked through a **two-role, human-mediated workflow** with a
**file-based handoff**. A session plays exactly one role, and the **Implementer is
LLM-agnostic** — any coding agent the user launches — so a change's handoff travels
through files in `.handoff/`, not through any one tool's chat. The user passes the
launch instruction between roles by hand.

At the start of a session the user declares the role ("You are the Director" /
"You are the Implementer"). If no role is declared, ask which one before doing
work that is role-specific.

> Note: this plugin lives inside the `DsonHost` project, alongside the separate
> `DsonParser` repo, which runs the same workflow. Roles and boundaries do **not**
> carry across repos — confirm the role applies to *this* plugin when a session
> opens from the host root.

## Roles

### Director (coordination + verification, no source edits)

The Director receives the user's instructions and queries and does everything
needed to accomplish or answer them *except* edit plugin source and *except*
launch the Implementer — the user launches it:

- **No silent fails — surface problems explicitly, and require the same of the
  Implementer.** When a task can't be fully or confidently done, say so up front:
  name the blocker, missing input, gap, uncertainty, or part left undone — never
  present partial, assumed, or unverified work as finished, and report what was
  *not* done or verified alongside what was. This is carried into every task-file
  (the Constraints line) so the Implementer reports back the same way.
- Reads project files and the docs to build the context for a task or answer.
- Writes **documentation, instruction, and configuration files** (anything that is
  not C++ source under `Source/DsonImporter/`).
- For a source change, **creates the task branch** (`task/<id>` off `Base`, default
  `main`) and **writes a task-file** (`.handoff/task-<id>.md`), then hands the user a
  one-line launch instruction. A task-file may ask the Implementer for feedback
  (feasibility, trade-offs, a counter-proposal) before any code is written.
- **After the run, verifies against the repo** — `git diff` for what changed and a
  [`CodeReviewRules.md`](CodeReviewRules.md) pass over the finished diff — then
  **commits and squash-merges the task branch into `Base`** and reports (see
  Reporting). The repo is ground truth; the feedback-file is advisory.
- Answers the user's questions directly when no code change is required.
- Keeps `Docs/Roadmap.md` and the orientation docs current **and tight** when a
  doc-only change is the whole task (R8/R9/R10).

The Director does **not** edit C++ source and does **not** launch the Implementer.
Anything the review surfaces goes back through a follow-up task-file, never a
Director hand-edit (see Verification & the review gate).

### Implementer (source edits — any LLM agent the user launches)

The Implementer is whatever coding agent the user points at the task-file. Its
contract is tool-neutral — *read the task-file, edit the tree, write the
feedback-file* — so it holds for any agent:

- **Reads the one task-file it is handed**, and — since a non-default agent won't
  auto-load them — reads [`../AGENTS.md`](../AGENTS.md) and
  [`CodeReviewRules.md`](CodeReviewRules.md) as the task-file instructs.
- Performs the code change, and **self-audits each edit** against
  `CodeReviewRules.md` (R1–R11) per [`../AGENTS.md`](../AGENTS.md) "Before editing
  source".
- **Builds and verifies** its own change and reports the real result; it **never runs
  git** — it edits the checked-out branch and leaves the tree dirty for the Director.
- Keeps `Docs/Roadmap.md` and the orientation docs in sync **in the same change**
  (R8/R9) when the edit moves status or changes code layout.
- **Writes its report to `.handoff/feedback-<id>.md`** (template below). **On a
  block — build failure, ambiguity, needed assumption, rule conflict — it halts and
  records it there rather than guessing past it**, and may raise a blocking concern
  even when the task didn't ask for one.

## Shared boundaries (both roles)

- **Builds: the Implementer builds and verifies its own changes** and reports the
  real result. **The Director defers the recompile** — it verifies via `git diff` +
  a review pass, not its own build, and re-builds only at its discretion for a
  build-risky change (parser-ABI / X-macro list, a `*.Build.cs`, public headers,
  added or removed `.cpp`) or an unconvincing build claim. Never claim a build or
  run you didn't actually do.
- **Git is the Director's; the Implementer never runs it; push stays with the user.**
  Per task the Director branches `task/<id>` off `Base`, commits, and squash-merges
  back after verifying — one reviewed commit (the gate is the merge, not the commit);
  the Implementer just leaves the tree dirty. Doc/config-only Director changes commit
  straight to `main`. Mechanics in [`Tooling.md`](Tooling.md); plugin git lives in
  this repo (`Plugins/DsonToUnreal`), not the host root — see
  [`../AGENTS.md`](../AGENTS.md) "Version Control".
- **Missing inputs — ask for the file *first*, before engineering around its
  absence.** If the task needs a fact that lives in a file you don't have (a
  `.duf`/`.dsf` asset, a log, a header), **ask the user to upload it**. Ask *before*
  proposing a build, a diagnostic dump, or any workaround that makes the user
  compile/run code to surface data a file already holds: a diagnostic is the
  fallback only for data no static file holds (runtime/engine behavior), never the
  default for inspecting an asset. Never fabricate or guess contents.
- The UE 5.4.4 / C++20 / parser-ABI / breaking-change constraints in
  [`../AGENTS.md`](../AGENTS.md) and [`CodeReviewRules.md`](CodeReviewRules.md)
  apply to both roles.

## The handoff is file-based (`.handoff/`)

All Director↔Implementer traffic for a change travels through two files:

| File | Direction | Contents |
| --- | --- | --- |
| `.handoff/task-<id>.md` | Director → Implementer | the self-contained prompt |
| `.handoff/feedback-<id>.md` | Implementer → Director | the report (advisory) |

- **`<id>` = `YYYYMMDD-HHMMSS-<slug>`** — e.g. `task-20260608-143022-fix-normals.md`.
  It pairs a task with its feedback, needs no counter state, and sorts
  chronologically.
  - **Timestamp** — UTC, 24-hour, minted by *running the clock* when the task-file
    is written (never typed from memory): PowerShell
    `(Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')`.
  - **Slug** — 2–4 words naming the task, lowercase kebab-case, ASCII `[a-z0-9-]`.
  - The Director mints `<id>` **once** for `task-<id>.md` and reuses the **same**
    `<id>` for `feedback-<id>.md`.
- **`.handoff/` is gitignored and excluded from agent discovery in
  [`../AGENTS.md`](../AGENTS.md).** That keeps it out of the `git diff` the Director
  verifies against and out of discovery. An agent reads **only the one task-file it
  is explicitly handed** — it never browses `.handoff/`.
- **The repo is ground truth; the feedback-file is advisory.** The Director confirms
  what changed / whether it complies from the repo itself, not from the
  feedback-file's claims. An unsubstantiated "success" is treated as a block.
- **History:** on task-close the `task`/`feedback` pair moves to `.handoff/history/`,
  pruned of entries older than 30 days (on archive or at session start). The whole
  tree is gitignored and not browsed; only the Director dips into history, for audit.

## Flow

1. **User → Director:** instruction or query.
2. **Director:** gathers context, then either answers directly (no code change) or
   **creates `task/<id>` off `Base`** and writes `.handoff/task-<id>.md`. For a
   **substantial** task it asks the user to review the task-file before launching; for
   a **minor** one it just reports the task-file is ready.
3. **Director → User:** the one-line launch instruction — *"Read and follow
   `.handoff/task-<id>.md`."*
4. **User → Implementer:** pastes that into whichever agent. The agent edits the
   tree, builds, self-audits, and writes `.handoff/feedback-<id>.md`.
5. **User → Director:** "done, `<id>`."
6. **Director:** reads the feedback-file (advisory), **verifies against the repo**
   (`git diff` + review pass; build per the deferral rule above), then **commits and
   squash-merges `task/<id>` into `Base`** and **reports** two-tier.
7. **User:** reviews the integrated result and **pushes** — pushing stays with the user.
8. **Director:** on task-close, archives the pair to `.handoff/history/`.

Because the user launches every run, **every task-file is on disk and reviewable
before it executes.**

## Verification & the review gate

The Director's review pass is **not redundant** with the Implementer's self-audit:
the self-audit is the author grading its own work mid-write; the Director pass is
independent second-eyes on the finished diff — and different agents apply
`CodeReviewRules.md` differently, or not at all. It is the **single uniform quality
gate**, positioned to catch whole-change issues a single-file author misses — e.g.
an export drifting from the parser-ABI X-macro list, or a status change that
skipped `Docs/Roadmap.md` (R9). The Director reviews; it does not hand-fix source:

- **Determinate rule violation with an obvious fix** → the Director issues a fix
  task-file (shown to the user first), re-verifies, and **discloses the loop** in
  the report. Not silent.
- **Judgment call / ambiguous / implies a breaking change or design decision** →
  that's a block: full details, the user decides.

## Reporting (two-tier)

- **Smooth → short after-action report.** "Smooth" = completed as written, the
  Implementer's build clean, review clean, no ambiguity or assumption hit. A few
  lines: what changed and which files; the **Implementer's real build line** +
  result (a summary, not an unverified "looks good"); "committed and squash-merged into
  `<Base>`, ready for you to review and push"; any **new** warnings; "Director review:
  clean."
- **Block → full details, the user decides.** A block is anything that isn't clean
  completion: build failure, an ambiguity or missing input, an assumption the
  Implementer made, a rule conflict, partial completion, deviation from the task, or
  a concern the Implementer raised. The report gives the blockage; a full account of
  what the Implementer did (files / diff, how far it got); the raw build/check
  output; the agent's reasoning or options; and the working-tree state — so the user
  can decide.

## Task-file template (Director → Implementer)

The task-file must stand alone — the agent starts cold and may not be the default
agent, so the `Role:` line is mandatory (it is itself the role declaration the
workflow expects):

```
Role: You are the **Implementer** for the DsonToUnreal plugin — the role that
      edits source. Read ../AGENTS.md and Docs/CodeReviewRules.md first, then make
      the change below. You may be any coding agent; these rules still apply.

Branch: `task/<id>` is checked out (off `<Base>`). Do **not** run git — leave the tree
        dirty; the Director commits and squash-merges after verifying.

Goal: <what the change should accomplish>

Context: <relevant files + the specific facts the Implementer needs; point to
          Docs/ImporterArchitecture.md (code layout) and Docs/Roadmap.md (status),
          plus the one or two source files in scope>

Task: <concrete, ordered steps or the precise change required>

Constraints: follow Docs/CodeReviewRules.md (R1-R11) and self-audit against its
             Quick checklist after each edit. No silent fails — surface any
             blocker, gap, uncertainty, assumption, or step left undone.

Build & verify: per Docs/Tooling.md (close the UE Editor first). Iterate to a clean
                build before reporting.

Report: write your results to .handoff/feedback-<id>.md using the feedback template
        in Docs/AgentWorkflow.md. On any block — build failure, ambiguity, needed
        assumption, rule conflict — halt and report it there rather than guessing.

Feedback requested: <yes/no — if yes, what to assess before/instead of coding>
```

## Feedback-file template (Implementer → Director)

```
Status: smooth | blocked

Files changed: <paths, one per line>

Build result: <exact command> -> <clean | warnings | errors, with the key lines>

What I did: <concise account of the change>

Blockers & assumptions: <anything that blocked, any assumption made, any question
                         for the Director — or "none">

Notes: <optional: reasoning, alternatives considered, follow-ups>
```
