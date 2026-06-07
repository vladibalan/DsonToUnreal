# DsonToUnreal Editor Tooling

Build/index tooling for working on this plugin in an editor. Currently this covers
the clangd compile database. Builds themselves are run by the user — see
`Docs/AgentWorkflow.md` (shared boundaries).

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
