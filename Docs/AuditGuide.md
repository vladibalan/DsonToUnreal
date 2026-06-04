# DsonToUnreal Audit Guide

Use this document for audit, review, debugging, and diagnostic requests. The goal is to collect evidence and route by symptom before reading broad implementation files.

## Audit Entry Order

1. Identify the symptom category from the sections below.
2. Check the listed evidence sources.
3. Read the listed header or top-of-file `Intent` comment.
4. Inspect only the relevant functions.
5. Report findings with file/line references and the observed risk.

For code-review style audits, lead with findings. Do not summarize first.

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

### Plugin Does Not Load

Evidence:

- `Saved/Logs/DsonHost.log`
- `DsonToUnreal.uplugin`
- `DsonImporter.Build.cs`

Source:

- `DsonImporter.cpp`
- `DsonParserFunctions.h`
- `Source/ThirdParty/DsonParser/`

Checks:

- `DsonParser.dll` path matches module startup expectations.
- Required parser exports are loaded before import actions can run.
- `GDsonParser.IsValid()` matches the set of required function pointers.
- Build.cs includes modules needed by startup/menu/UI code.

### Import Dialog Or Validation Fails

Evidence:

- `Saved/Logs/DsonHost.log`
- Selected `.duf` or `.dsf` path if provided by the user.
- DAZ content root list if logged.

Source:

- `SDsonImportWindow.*`
- `DsonValidator.*`
- `DsonContentRoots.*`

Checks:

- Import button is gated by validation and dependency resolution.
- Asset type detection accepts the intended file type.
- Dependency URLs are decoded and resolved against all content roots.
- The UI preserves the resolved base figure path in `FDsonImportSettings`.

### Missing Dependencies Or Bad DSON URL Resolution

Evidence:

- `Saved/Logs/DsonHost.log`
- Raw DSON URL from diagnostics or parser output.
- DAZ Studio content root paths.

Source:

- `DsonContentRoots.*`
- `DsonValidator.*`

Checks:

- URL fragments are removed only when appropriate.
- URL escapes are decoded correctly.
- Absolute, root-relative, and content-root-relative paths are handled consistently.
- Failed resolution is reported with enough context to reproduce.

### Bad Skeleton Or Bone Hierarchy

Evidence:

- `Saved/Logs/DsonHost.log`
- Imported skeleton asset name/path.
- Base figure DSF path.

Source:

- `DsonSkeletonBuilder.*`
- `DsonParserFunctions.h`

Checks:

- Node parent IDs produce a valid UE reference skeleton order.
- Root bone handling is deterministic.
- Unit scale and coordinate conversion are applied consistently.
- Bone names match the IDs later used by skin weights.

### Bad Mesh Shape, Faces, UVs, Or Material Slots

Evidence:

- `Saved/Logs/DsonHost.log`
- Imported skeletal mesh asset path.
- Geometry DSF path and UV-set DSF path.

Source:

- `DsonMeshBuilder.*`
- `DsonSkinWeightsBuilder.*` only if deformation is involved.

Checks:

- Vertex count, face count, and polygon vertex indices are bounds-checked.
- Coordinate conversion matches skeleton conversion.
- UVs are assigned per polygon corner, including override data.
- Polygon material groups become stable UE material slots.
- `FDsonSkinWeightsBuilder` runs before mesh description commit.

### Bad Skinning Or Deformation

Evidence:

- `Saved/Logs/DsonHost.log`
- Skeleton asset path.
- Mesh asset path.
- Base figure or geometry DSF path.

Source:

- `DsonSkinWeightsBuilder.*`
- `DsonSkeletonBuilder.*`
- `DsonParserFunctions.h`

Checks:

- Skin modifier selection is deterministic.
- DSF joint/node IDs map to UE bone indices.
- Influences are capped, normalized, and applied to the correct vertex IDs.
- Missing bones or influences produce warnings, not silent success.

### Bad Materials

Evidence:

- `Saved/Logs/DsonHost.log`
- Material diagnostic output if enabled.
- Imported material instance paths.
- `MaterialMastersV1.md`

Source:

- `DsonMaterialBuilder.*`
- `DsonMaterialDiagnostic.*`
- `DsonTextureImporter.*` if textures are involved.

Checks:

- Shader detection uses scene material URL and `shader_type` consistently.
- Channel IDs match `MaterialMastersV1.md` and master asset parameters.
- Material instances are keyed by material group name expected by the mesh builder.
- Ignored channels are intentional v1 omissions, not missing core channels.

### Missing Or Wrong Textures

Evidence:

- `Saved/Logs/DsonHost.log`
- Material diagnostic output if enabled.
- Raw `image_url` or texture path.
- Imported texture asset path.

Source:

- `DsonTextureImporter.*`
- `DsonContentRoots.*`
- `DsonMaterialBuilder.*`

Checks:

- Image URL resolves to an existing file under a content root.
- Package path preserves useful relative DAZ folder structure.
- sRGB flag matches the channel role.
- Cache key is the resolved absolute source path.
- Failed URLs are retained for reporting.

### Parser API Or Third-Party Boundary Issues

Evidence:

- `Saved/Logs/DsonHost.log`
- Parser DLL/lib files under `Source/ThirdParty/DsonParser/`
- Export names expected by `DsonImporter.cpp`

Source:

- `DsonParserFunctions.h`
- `DsonImporter.cpp`
- `Source/ThirdParty/DsonParser/Include/DsonParserAPI.h`

Checks:

- Function pointer typedefs match exported signatures.
- Required exports fail loudly.
- Optional exports degrade gracefully.
- Parser string-return rules are respected by immediate conversion to `FString`.

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

