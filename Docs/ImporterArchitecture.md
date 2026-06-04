# DsonToUnreal Importer Architecture

This document is for fast agent discovery. It explains where to look before opening full implementation files.

## Shape

The plugin is an Unreal Editor module:

- Module: `DsonImporter`
- Plugin descriptor: `DsonToUnreal.uplugin`
- Public module entry point: `Source/DsonImporter/Public/DsonImporter.h`
- Implementation code: `Source/DsonImporter/Private/`
- Bundled parser API: `Source/ThirdParty/DsonParser/`
- Master material assets: `Content/Materials/`

## Runtime Flow

1. `FDsonImporterModule::StartupModule` loads `DsonParser.dll`, resolves parser exports into `GDsonParser`, and registers the File menu entry.
2. The menu entry opens `SDsonImportWindow`.
3. `SDsonImportWindow` detects DAZ content roots, lets the user choose a `.duf` or `.dsf`, and calls `FDsonValidator`.
4. `FDsonValidator` parses basic metadata and resolves figure/material/geometry dependencies.
5. Import confirmation produces `FDsonImportSettings`.
6. The import path builds assets:
   - `FDsonSkeletonBuilder` creates a `USkeleton` from figure DSF nodes.
   - `FDsonTextureImporter` resolves and imports referenced image files.
   - `FDsonMaterialBuilder` creates material instances from scene material channels.
   - `FDsonMeshBuilder` creates the skeletal mesh, UVs, polygon groups, and material slots.
   - `FDsonSkinWeightsBuilder` applies DSF skin influences before mesh commit.

## Component Responsibilities

`DsonImporter.cpp`

- Owns module startup/shutdown.
- Loads the third-party parser DLL.
- Populates `GDsonParser`.
- Adds the File menu entry that launches the import dialog.

`SDsonImportWindow.*`

- Slate modal dialog for import.
- Stores the selected file and pending settings.
- Runs validation and gates the Import action.

`DsonContentRoots.*`

- Reads DAZ Studio content directories from Windows Registry.
- Resolves DSON URL paths against content roots.
- URL-decodes DSON paths.

`DsonValidator.*`

- Loads the selected DSON document through the parser.
- Determines asset type and Genesis generation.
- Resolves dependencies needed by the import.

`DsonSkeletonBuilder.*`

- Reads figure nodes from the resolved base figure DSF.
- Converts node transforms into UE reference skeleton bones.
- Saves the skeleton asset.

`DsonMeshBuilder.*`

- Loads geometry DSF and optional UV-set DSF.
- Converts vertices, faces, UVs, polygon groups, and material slots into a `USkeletalMesh`.
- Calls `FDsonSkinWeightsBuilder` before committing mesh data.

`DsonSkinWeightsBuilder.*`

- Finds the skin binding modifier.
- Maps DSF joint/node names to UE skeleton bone indices.
- Writes capped/normalized influences to mesh description skin-weight attributes.

`DsonMaterialBuilder.*`

- Detects DAZ shader kind from scene material metadata.
- Maps DAZ material channels onto Unreal material instance parameters.
- Imports textures through `FDsonTextureImporter`.
- Outputs material instances keyed by material group name.

`DsonTextureImporter.*`

- Resolves image URLs to disk.
- Imports or reuses `UTexture2D` assets.
- Sets sRGB according to material channel needs.
- Caches imports by resolved absolute path.

`DsonMaterialDiagnostic.*`

- Debug-only material channel dump used for planning and verification.

`DsonParserFunctions.h`

- Function pointer typedefs and `FDsonParserAPI`.
- Keep this synchronized with exports provided by the bundled parser DLL.

## Common Change Areas

- Parser export missing or new parser function: update `DsonParserFunctions.h` and `DsonImporter.cpp`.
- Import dialog behavior: edit `SDsonImportWindow.*`.
- Path/dependency failures: start in `DsonContentRoots.*`, then `DsonValidator.*`.
- Bad bone hierarchy or transforms: start in `DsonSkeletonBuilder.*`.
- Bad geometry, UVs, material slots, or mesh asset save: start in `DsonMeshBuilder.*`.
- Bad skin weights: start in `DsonSkinWeightsBuilder.*`.
- Bad shader detection or channel mapping: start in `DsonMaterialBuilder.*`, then `MaterialMastersV1.md`.
- Missing or wrong textures: start in `DsonTextureImporter.*`.

## Discovery Rule

When asked to change behavior, identify the component using the routing table first. Read the relevant header and top-of-file `.cpp` comment, then inspect only the function involved.

