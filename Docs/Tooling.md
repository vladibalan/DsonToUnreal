# DsonToUnreal Editor Tooling

Build/index tooling for working on this plugin: how to **build & verify** it
(below) and how to (re)generate the clangd compile database. Build responsibility
is a role split — see `Docs/AgentWorkflow.md`.

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
