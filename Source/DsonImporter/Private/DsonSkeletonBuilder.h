#pragma once
#include "CoreMinimal.h"
#include "DsonImportTypes.h"

class USkeleton;

class FDsonSkeletonBuilder
{
public:
    // Returns the created USkeleton on success, nullptr on failure.
    // Logs all errors via UE_LOG(LogDsonImporter, Error, ...).
    static USkeleton* Build(const FDsonImportSettings& Settings);

    // Merges any bones in CompanionDsfHandle's node_library that are absent from
    // BodySkeleton into BodySkeleton's bone tree, using the companion's parent chain
    // to anchor them correctly. Saves BodySkeleton if any bones were added.
    // Returns the number of new bones merged (0 = all companion bones already present).
    // Permissive (R7): logs a warning and returns 0 on bad input.
    static int32 MergeCompanionBonesIntoSkeleton(uint64_t CompanionDsfHandle, USkeleton* BodySkeleton);

private:
    // Converts parser node records into UE reference-skeleton bones.
    // Parent bones must be added before children for UE reference skeleton validity.
    static void BuildReferenceSkeletonFromDsf(uint64_t DsfHandle, FReferenceSkeleton& OutRefSkeleton);

    // Converts one DAZ node's center/orientation/scale into a UE bone transform.
    // This is the key place to audit coordinate-system and unit-scale issues.
    static FTransform MakeBoneTransform(uint64_t DsfHandle, int32 NodeIndex, double UnitScale);

    // Creates or replaces the skeleton package asset after the reference skeleton is built.
    static USkeleton* CreateSkeletonAsset(const FReferenceSkeleton& RefSkeleton, const FString& AssetName);
};
