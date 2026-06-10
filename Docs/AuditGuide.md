# DsonToUnreal Audit Guide

Use this document for audit, review, debugging, and diagnostic requests. The goal is to collect evidence and route by symptom before reading broad implementation files.

## Audit Entry Order

1. Identify the symptom row in the routing table below.
2. Collect that row's evidence — always start with the latest editor log (see Evidence Sources), then the row's **added evidence**.
3. Read the routed header's purpose comment (the `.cpp` files carry none).
4. Inspect only the relevant functions.
5. Report findings with file/line references and the observed risk.

For code-review style audits, lead with findings. Do not summarize first. Use
`Docs/CodeReviewRules.md` as the checklist — it owns this repo's DRY, UE 5.4.4
compatibility, parser-ABI, RAII, compactness, orientation-doc-sync, and
roadmap-upkeep rules (R1–R12).

## Evidence Sources

- Latest editor log: `Saved/Logs/DsonHost.log`
- Older editor logs: `Saved/Logs/DsonHost-backup-*.log`
- Crash logs: `Saved/Crashes/*/DsonHost.log`
- Crash metadata: `Saved/Crashes/*/CrashContext.runtime-xml`
- Plugin descriptor: `Plugins/DsonToUnreal/DsonToUnreal.uplugin`
- Build dependencies: `Plugins/DsonToUnreal/Source/DsonImporter/DsonImporter.Build.cs`
- Material contract: `Plugins/DsonToUnreal/MaterialMastersV1.md`
- Parser API surface: `Plugins/DsonToUnreal/Source/DsonImporter/Private/DsonParserFunctions.h`

Only inspect `Binaries/` or `Intermediate/` when the audit is specifically about packaging, generated code, stale binaries, or build output.

## Symptom Routing

Find the symptom, collect its evidence (the editor log first, then the **added
evidence** column), then read the routed source. The **Source** column is the
audit view of the same component routing owned by `AGENTS.md` (Task Routing) —
consult it for the canonical routing, and `Docs/ImporterArchitecture.md`
(Component Responsibilities) for code layout; this table adds the per-symptom evidence and checks.

| Symptom | Added evidence | Source | Key checks |
|---|---|---|---|
| **Plugin does not load** | `DsonToUnreal.uplugin`; `DsonImporter.Build.cs` | `DsonImporter.cpp`; `DsonParserFunctions.h`; `Source/ThirdParty/DsonParser/` | `DsonParser.dll` path matches module-startup expectation; required exports loaded before import actions can run; `GDsonParser.IsValid()` matches the required pointer set; Build.cs includes the modules startup/menu/UI code needs |
| **Import dialog / validation fails** | selected `.duf`/`.dsf` path; logged content-root list | `SDsonImportWindow.*`; `DsonValidator.*`; `DsonContentRoots.*` | Import button gated by validation + dependency resolution; asset-type detection accepts the intended type; dependency URLs decoded + resolved against all content roots; UI preserves the resolved base-figure path in `FDsonImportSettings` |
| **Missing deps / bad DSON URL resolution** | raw DSON URL; DAZ content-root paths | `DsonContentRoots.*`; `DsonValidator.*` | URL fragments removed only when appropriate; URL escapes decoded correctly; absolute / root-relative / content-root-relative paths handled consistently; failed resolution reported with reproducible context |
| **Bad skeleton / bone hierarchy** | skeleton asset path; base figure DSF path | `DsonSkeletonBuilder.*`; `DsonParserFunctions.h` | Node parent IDs yield a valid UE reference-skeleton order; root-bone handling deterministic; unit scale + coordinate conversion applied consistently; bone names match the IDs skin weights use later |
| **Bad mesh shape / faces / UVs / material slots** | skeletal-mesh asset path; geometry DSF + UV-set DSF paths | `DsonMeshBuilder.*` (+ `DsonSkinWeightsBuilder.*` if deformation involved) | Vertex/face counts + polygon vertex indices bounds-checked; coordinate conversion matches the skeleton; UVs assigned per polygon corner incl. override data; polygon material groups become stable UE slots; skin weights run before mesh-description commit |
| **Bad skinning / deformation** | skeleton + mesh asset paths; base-figure / geometry DSF path | `DsonSkinWeightsBuilder.*`; `DsonSkeletonBuilder.*`; `DsonParserFunctions.h` | Skin-modifier selection deterministic; DSF joint/node IDs map to UE bone indices; influences capped, normalized, applied to the correct vertex IDs; missing bones/influences warn (no silent success) |
| **Missing / wrong morph targets** | morphed mesh asset; base figure DSF; scene / external morph DSF paths | `DsonMorphBuilder.*`; `DsonParserFunctions.h` (morph, scene-modifier, formula-output exports) | Morphs registered via MeshDescription attributes **before** `CommitMeshDescription` — post-build registration silently fails (see `Docs/Reference.md` → carry-forward lessons); only delta-bearing morphs; `?value` formula-reachable leaf files discovered + transitively resolved; deltas converted through `DazPointToUe` |
| **Bad materials** | material diagnostic output; imported MIC paths; `MaterialMastersV1.md` | `DsonMaterialBuilder.*`; `DsonMaterialDiagnostic.*` (+ `DsonTextureImporter.*` for textures) | Shader detection uses scene-material URL + `shader_type` consistently; channel IDs match `MaterialMastersV1.md` + master params; MICs keyed by the material-group name the mesh builder expects; ignored channels are intentional v1 omissions, not missing core channels |
| **Missing / wrong textures** | material diagnostic output; raw `image_url` / texture path; imported texture asset path | `DsonTextureImporter.*`; `DsonContentRoots.*`; `DsonMaterialBuilder.*` | Image URL resolves to an existing file under a content root; package path preserves useful DAZ folder structure; sRGB flag matches the channel role; cache key = resolved absolute source path; failed URLs retained for reporting |
| **Parser API / third-party boundary** | parser DLL/lib under `Source/ThirdParty/DsonParser/`; export names `DsonImporter.cpp` expects | `DsonParserFunctions.h`; `DsonImporter.cpp`; `Source/ThirdParty/DsonParser/Include/DsonParserAPI.h` | Function-pointer typedefs match exported signatures; required exports fail loudly; optional exports degrade gracefully; parser `const char*` returns converted to `FString` immediately |

## Core Invariants

- `GDsonParser.IsValid()` must be true before any builder uses parser functions.
- Parser-owned `const char*` results must be copied to `FString` before another parser call can overwrite internal storage.
- DSON URLs may be URL-encoded and may include fragments.
- Material group names are the bridge between DAZ scene materials and mesh polygon groups.
- Skin weights must be applied before mesh description commit.
- Builder failures should log enough file/path/index context to reproduce.
- Created assets should use stable, sanitized package names.
- Material parameter names must stay synchronized with `MaterialMastersV1.md` and the UE master material assets.

## Audit Report Shape

Use this structure:

1. Findings, ordered by severity, with file/line references.
2. Evidence checked.
3. Open questions or assumptions.
4. Suggested fixes or next diagnostic step.

If no issues are found, say so clearly and name the remaining risk or missing evidence.
