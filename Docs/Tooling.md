# DsonToUnreal Editor Tooling

Build/git/index tooling for working on this plugin: how to **build & verify** it,
the **git branch-per-task workflow**, and how to (re)generate the clangd compile
database. Build and git are a role split — see `Docs/AgentWorkflow.md`.

## Build & verify (read before building)

The plugin compiles only as part of the **host editor target** (`DsonHostEditor`),
from a terminal against the source-build engine at `D:\UE_5.4` (no Rider action needed):

```
"D:\UE_5.4\Engine\Build\BatchFiles\Build.bat" DsonHostEditor Win64 Development -Project="D:\Unreal Projects\DsonHost\DsonHost.uproject" -WaitMutex
```

A clean incremental build of `UnrealEditor-DsonImporter.dll` exits 0 with no warnings;
an "up-to-date" result also counts as success (a full rebuild is much slower).

Gotchas:
- **Close the UE Editor first** — else the link step fails with a file-in-use error on `UnrealEditor-DsonImporter.dll` (the most common failure).
- **No GenerateProjectFiles needed to compile** — UBT builds directly; that step only refreshes Rider/VS IntelliSense and the clangd `.rsp` inputs below.
- `-WaitMutex` queues behind a running UBT (e.g. Rider's own build) instead of failing; `-FromMsBuild` is not needed for a terminal invocation.
- Requires the **VS2022 C++ toolchain** (the engine's bundled .NET SDK is used; no LLVM/Clang).
- Rider equivalent: the `DsonHostEditor | Win64 | Development` configuration (same UBT under the hood).

## Sync the vendored parser (read before updating it)

`Tools/Sync-Parser.ps1` pulls the 4-file DsonParser bundle — `DsonParserAPI.h`,
`DsonParserVersion.h`, `CHANGELOG.md`, `DsonParser.dll` — from a local DsonParser repo
working tree into `Source/ThirdParty/DsonParser/`. It is a one-way pull launched by
hand, so the parser stays plugin-agnostic (nothing in the parser repo references this plugin).

```
pwsh -File Tools/Sync-Parser.ps1 -ParserRepo <DsonParser repo root>
```

- Source path defaults to `$env:DSONPARSER_REPO`; the DLL is taken from `x64\Release` (`-Configuration Debug` to override).
- **Compat gate:** clean no-op when already current, refuses a downgrade, warns on a MAJOR (breaking-ABI) bump; `-Force` overrides the no-op / downgrade refusal.
- Prints the new CHANGELOG entries, then `git status`; it **never stages or commits** — review the drop and commit branch-per-task (push stays with you).
- New exports still need an `X-macro` row in `DsonParserFunctions.h` (R2) before they bind; `DsonParserAbiCheck.cpp` then validates the binding on the next build.
- **Close the UE Editor first** — same DLL-lock reason as a build; the script copies the DLL first and aborts before touching headers if it is locked.
- The import `.lib` is deliberately **not** bundled (binding is runtime `GetProcAddress`; see `Source/ThirdParty/DsonParser/DsonParser.Build.cs`).

## Git workflow (branch-per-task)

Branch/commit/merge mechanics for the **Director**; the git policy and role split (who
runs git, push ownership) live in `Docs/AgentWorkflow.md`. `<id>` is the handoff id;
the branch is `task/<id>`.

- **Open** a task off `Base` (default `main`): `git switch -c task/<id> <Base>`.
- **Base / nesting.** A minor task spawned mid-task branches off the in-progress parent **only if it needs that parent's unmerged changes**, else off `main`; children merge up (minor → major → main). The task-file's `Branch:` line records it.
- **Integrate** once verified (`git diff` + review): `git switch <Base>` → `git merge --squash task/<id>` → `git commit` (one reviewed commit, message from the task) → `git branch -D task/<id>`.
- **Serialize** — open off current `main` and integrate before the next task, so the common merge is a conflict-free fast path.
- **Conflicts** — the Director resolves only non-source (docs/config); a **source** conflict is a source edit: abort and route it back via a merge task-file (or escalate), never hand-resolved.
- **Doc/config-only Director changes** commit straight to `main` (no branch).

## clangd index (read before regenerating it)

A clangd compile database already exists for this plugin. To regenerate it, **do not
reach for UBT's `Build.bat -mode=GenerateClangDatabase`** — it aborts with "Clang x64
must be installed", and this machine has **no LLVM/Clang** (only `clang-format`/`clang-tidy`
ship with VS). clangd has its own frontend, so don't install one. The working,
no-install approach (already set up):

- Generator: `tools/gen-compile-commands.ps1` at the **host project root** — one level **above** this plugin repo (not under this git repo, same as the `.clangd` / `compile_commands.json` it produces).
- It **harvests** the per-TU response files UBT wrote on the last build (`Plugins/DsonToUnreal/Intermediate/Build/Win64/x64/*/Development/DsonImporter/*.cpp.obj.rsp`), inlines the shared `/I` list, and strips MSVC-only flags clangd can't use (`/Yu /Fp /Fo /sourceDependencies`). No compiler runs.
- Prerequisite: the editor target must have been **built once** so those `.rsp` files exist. Regenerate after adding/removing `.cpp` files, changing module dependencies, or rebuilding with new flags: `pwsh -File tools/gen-compile-commands.ps1` (from the host project root).
- If clangd warns on a stray MSVC flag, add it to the script's drop-list — do not switch to installing LLVM.
