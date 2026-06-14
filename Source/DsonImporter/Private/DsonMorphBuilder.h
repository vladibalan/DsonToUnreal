#pragma once
#include "CoreMinimal.h"

#include "DsonLoadedDocument.h"
#include "MeshDescription.h"
#include "SkeletalMeshAttributes.h"

struct FDsonImportSettings;

/*
 * Intent:
 * - Create UE morph targets from DAZ morph modifiers in the base figure DSF and
 *   external morph DSFs referenced directly by the scene DUF or transitively by
 *   `?value` formula outputs.
 * - Convert position deltas through the shared DAZ-to-UE coordinate mapping and
 *   write them into MeshDescription morph attributes before mesh commit.
 * - Let the engine recompute morph normals from the LOD build settings.
 *
 * Read this file for missing morph targets, bad morph direction, or duplicate
 * morph-name handling.
 */
class FDsonMorphBuilder
{
public:
    // Registers MeshDescription morph attributes for each delta-bearing DAZ morph
    // found in the figure DSF and external morph .dsf files referenced by the
    // scene .duf or by transitive formula `?value` outputs. Permissive: missing
    // exports / OOB deltas are skipped, never fatal. Must be called before
    // CommitMeshDescription; NumBaseVertices is VertexIDs.Num().
    static void Apply(
        const FDsonImportSettings& Settings,
        uint64_t FigureDsfHandle,
        FMeshDescription& MeshDesc,
        FSkeletalMeshAttributes& SkelAttribs,
        const TArray<FVertexID>& VertexIDs);

    // Registers only the figure-owned morph set: morphs from FigureDsfHandle (the
    // base figure DSF's modifier_library) plus the JCM correctives cached in
    // Settings.DiscoveredCorrectiveDsfPaths. Does NOT walk the scene DUF formula
    // graph — no re-scan. Permissive (R7): corrective files that fail to open are
    // warned and skipped; never fatal. First-wins dedup is preserved.
    // Called by DsonMeshBuilder::BuildParent for the parent asset's morph scope.
    static void ApplyFigureOwned(
        const FDsonImportSettings& Settings,
        uint64_t FigureDsfHandle,
        FMeshDescription& MeshDesc,
        FSkeletalMeshAttributes& SkelAttribs,
        const TArray<FVertexID>& VertexIDs);

    // Discovers all formula-reachable external morph DSFs from the scene.modifiers
    // walk and opens them. OutHandles[i] == OutDocs[i].GetHandle64(). The base
    // figure DSF is excluded (callers handle it separately). Permissive: files
    // that fail to open are skipped with a warning; never fatal.
    static void DiscoverFormulaReachableDocuments(
        const FDsonImportSettings& Settings,
        TArray<FDsonLoadedDocument>& OutDocs,
        TArray<uint64_t>& OutHandles);
};
