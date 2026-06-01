#pragma once
#include "CoreMinimal.h"

class USkeletalMesh;
class USkeleton;

class FDsonSkinWeightsBuilder
{
public:
    // Applies real bone influences from the DSF skin modifier to the
    // MeshDescription skin weight attribute on Mesh (LOD 0).
    // Must be called after the MeshDescription has been created and vertices
    // added, but before CommitMeshDescription.
    // Returns true on success, false on failure (logs all errors).
    static bool Apply(
        uint64_t DsfHandle,
        USkeletalMesh* Mesh,
        USkeleton* Skeleton);

private:
    // Finds the index of the first skin binding modifier in the DSF.
    // Logs a warning if more than one skin modifier is found.
    // Returns -1 if none found.
    static int32 FindSkinModifierIndex(uint64_t DsfHandle);

    // Builds a bone-name → UE5 bone-index map from the skeleton's FReferenceSkeleton.
    static void BuildBoneIdMap(
        const USkeleton* Skeleton,
        TMap<FString, int32>& OutMap);
};