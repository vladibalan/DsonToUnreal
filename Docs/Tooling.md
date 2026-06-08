# DsonToUnreal Editor Tooling

Build/git/index tooling for working on this plugin: how to **build & verify** it,
the **git branch-per-task workflow**, and how to (re)generate the clangd compile
database. Build and git are a role split — see `Docs/AgentWorkflow.md`.

## Build & verify (read before building)

The plugin compiles as part of the **host project's editor target**
(`DsonHostEditor`), not standalone. From a terminal (shell-only — no Rider build
action needed), against the source-build engine at `D:\UE_5.4`:

```
"D:\UE_5.4\Engine\Build\BatchFiles\Build.bat" DsonHostEditor Win64 Development -Project="D:\Unreal Projects\DsonHost\DsonHost.uproject" -WaitMutex
```

Verified 2026-06-08: clean incremental build of `UnrealEditor-DsonImporter.dll`,
exit 0, no warnings. An "up-to-date" result also counts as success; a full clean
rebuild takes considerably longer.

Gotchas:
- **Close the UE Editor first.** If it is running with the plugin loaded, the link
  step fails with a file-in-use error on `UnrealEditor-DsonImporter.dll` — the most
  common failure.
- **No GenerateProjectFiles needed to compile** — UBT builds directly; that step
  only refreshes Rider/VS IntelliSense and the clangd `.rsp` inputs below.
- `-WaitMutex` queues behind a running UBT (e.g. Rider's own build) instead of
  failing; `-FromMsBuild` is not needed for a terminal invocation.
- Requires the **VS2022 C++ toolchain**; the engine's bundled .NET SDK is used (no
  separate install). No LLVM/Clang needed.
- Rider equivalent: the `DsonHostEditor | Win64 | Development` build configuration
  (it calls the same UBT under the hood).

## Git workflow (branch-per-task)

Branch/commit/merge mechanics for the **Director** (policy + role split:
`Docs/AgentWorkflow.md`). The Implementer never runs git — it edits the checked-out
branch and leaves the tree dirty. `<id>` is the handoff id; the branch is `task/<id>`.

- **Open** a task off `Base` (default `main`): `git switch -c task/<id> <Base>`.
- **Base / nesting.** A minor task spawned mid-task branches off the in-progress
  parent **only if it needs that parent's unmerged changes**, else off `main`;
  children merge up (minor → major → main). The task-file's `Branch:` line records it.
- **Integrate** once the Director has verified (`git diff` + review pass):
  `git switch <Base>` → `git merge --squash task/<id>` → `git commit` (one reviewed
  commit, message from the task) → `git branch -D task/<id>`.
- **Serialize** — open off current `main` and integrate before the next task, so the
  common merge is a conflict-free fast path.
- **Conflicts** — the Director resolves only non-source (docs/config); a **source**
  conflict is a source edit: abort and route it back via a merge task-file (or
  escalate), never hand-resolved.
- **Doc/config-only Director changes** commit straight to `main` (no branch).
- **Push stays with the user** — the Director commits and merges locally only.

## clangd index (read before regenerating it)

A clangd compile database already exists for this plugin. If you need to
(re)generate it, **do not reach for UBT's `Build.bat -mode=GenerateClangDatabase`** —
that mode aborts immediately with "Clang x64 must be installed" because this
machine has **no LLVM/Clang toolchain installed** (only `clang-format`/`clang-tidy`
ship with VS; there is no `clang-cl.exe`/`clang.exe`). clangd itself needs no such
install — it has its own frontend — so the toolchain is not worth installing.

The working, no-install approach (already set up):

- Generator: `tools/gen-compile-commands.ps1` at the **host project root**
  (`<project>/`, one level **above** this plugin repo — it is not under this git
  repo, same as the `.clangd` and `compile_commands.json` it produces).
- It **harvests** the per-TU response files UBT wrote on the last build
  (`Plugins/DsonToUnreal/Intermediate/Build/Win64/x64/*/Development/DsonImporter/*.cpp.obj.rsp`),
  inlines the shared `/I` list, and strips the MSVC-only flags clangd can't use
  (`/Yu /Fp /Fo /sourceDependencies`). No compiler runs.
- Prerequisite: the **editor target must have been built once** so those `.rsp`
  files exist. Regenerate after adding/removing `.cpp` files, changing module
  dependencies, or rebuilding with new flags:
  `pwsh -File tools/gen-compile-commands.ps1` (from the host project root).
- If clangd warns on a stray MSVC flag, add it to the script's drop-list — do
  not switch to installing LLVM.
