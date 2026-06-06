#pragma once
#include "CoreMinimal.h"

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
};
