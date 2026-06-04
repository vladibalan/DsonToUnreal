#pragma once
#include "CoreMinimal.h"

struct FDsonImportSettings;
class USkeleton;

class FDsonSkeletonBuilder
{
public:
    // Returns the created USkeleton on success, nullptr on failure.
    // Logs all errors via UE_LOG(LogDsonImporter, Error, ...).
    static USkeleton* Build(const FDsonImportSettings& Settings);

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
