# Agent Workflow — Director & Implementer

This plugin is worked through a **two-agent, human-mediated workflow**. There are
two roles. A session plays exactly one of them, and the **user passes prompts
between the two roles by hand** — the agents do not call each other directly.

At the start of a session the user declares the role ("You are the Director" /
"You are the Implementer"). If no role is declared, ask which one before doing
work that is role-specific.

> Note: this plugin lives inside the `DsonHost` project, alongside the separate
> `DsonParser` repo, which runs the same workflow. Roles and boundaries do **not**
> carry across repos — confirm the role applies to *this* plugin when a session
> opens from the host root.

## Roles

### Director (coordination, no source edits)

The Director receives the user's instructions and queries and does everything
needed to accomplish or answer them *except* edit plugin source code:

- Reads project files and the docs to build the context for a task or answer.
- Writes **documentation, instruction, and configuration files** (anything that
  is not C++ source under `Source/DsonImporter/`).
- **Authors prompts for the Implementer** — self-contained instructions the user
  will hand to an Implementer session. A prompt may explicitly ask the
  Implementer for feedback (e.g. feasibility, trade-offs, a counter-proposal)
  before any code is written.
- Answers the user's questions directly when no code change is required.
- Keeps `Docs/Roadmap.md` and the orientation docs current when a doc-only change
  is the whole task (per `Docs/CodeReviewRules.md` R8/R9).

The Director does **not** edit C++ source under `Source/DsonImporter/`. When a
task needs source changes, the Director produces a prompt (see template below)
rather than editing the code itself.

### Implementer (source edits)

The Implementer executes the prompts the user passes in from the Director:

- Performs the code writing described by the prompt.
- **Follows [`CodeReviewRules.md`](CodeReviewRules.md)** (R1–R9) while authoring,
  and self-audits each edit against that doc's Quick checklist, per
  [`../AGENTS.md`](../AGENTS.md) "Before editing source".
- Keeps `Docs/Roadmap.md` and the orientation docs in sync **in the same change**
  (R8/R9) when the edit moves project status or changes code layout.
- Provides feedback when the prompt asks for it (and may raise blocking concerns
  even when it doesn't, instead of implementing something it believes is wrong).

## Shared boundaries (both roles)

- **The user handles binary builds.** Never claim something is built or run
  unless you actually did it; otherwise ask the user to build and report back.
- **The user handles git commits and pushes.** Do not commit or push; leave the
  working tree for the user to review and commit. Plugin git lives in this
  repo (`Plugins/DsonToUnreal`), not the host root — see [`../AGENTS.md`](../AGENTS.md)
  "Version Control".
- **Missing inputs:** if a file needed for the task is not in the project folder,
  **ask the user to upload it** rather than fabricating or guessing its contents.
- The UE 5.4.4 / C++20 / parser-ABI / breaking-change constraints in
  [`../AGENTS.md`](../AGENTS.md) and [`CodeReviewRules.md`](CodeReviewRules.md)
  apply to both roles.

## Handoff

The flow for a change is:

1. **User → Director:** instruction or query.
2. **Director:** gathers context, then either answers directly or produces an
   Implementer prompt (and/or writes docs/config).
3. **User → Implementer:** pastes the Director's prompt.
4. **Implementer:** makes the source changes, self-audits, reports back (and any
   requested feedback).
5. **User:** builds and commits; relays results or follow-ups back to the
   Director as needed.

## Director prompt template

When the Director authors a prompt for the Implementer, make it stand alone —
the Implementer session has none of the Director's context:

```
Goal: <what the change should accomplish>

Context: <relevant files + the specific facts the Implementer needs;
          point to Docs/ImporterArchitecture.md (code layout) and
          Docs/Roadmap.md (status), plus the one or two source files in scope>

Task: <concrete, ordered steps or the precise change required>

Constraints: follow Docs/CodeReviewRules.md (R1–R9) and self-audit against its
             Quick checklist after each edit.

Feedback requested: <yes/no — if yes, what to assess before/instead of coding>
```
