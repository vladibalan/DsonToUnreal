#pragma once
#include "CoreMinimal.h"

struct FDsonImportSettings;
class USkeleton;
class USkeletalMesh;
class UMaterialInstanceConstant;
class UMaterial;
class UMaterialInterface;

class FDsonMeshBuilder
{
public:
    // Returns the created USkeletalMesh on success, nullptr on failure.
    // Skeleton must be a valid non-null USkeleton created in Phase 3.
    // UvSetDsfPath is the absolute path to the UV set DSF; empty = fallback to zero UVs.
    static USkeletalMesh* Build(
        const FDsonImportSettings& Settings,
        USkeleton* Skeleton,
        const TMap<FString, UMaterialInstanceConstant*>& MaterialsByGroup,
        UMaterial* DefaultMaterial,
        const FString& UvSetDsfPath);

private:
    // Converts parser geometry into a skeletal mesh asset.
    // MaterialsByGroup is keyed by DAZ polygon material group name, because those
    // groups are how DSF faces are matched to scene material instances.
    static USkeletalMesh* CreateMeshAsset(
        const FDsonImportSettings& Settings,
        USkeleton* Skeleton,
        uint64_t DsfHandle,
        const TMap<FString, UMaterialInstanceConstant*>& MaterialsByGroup,
        UMaterial* DefaultMaterial,
        const FString& UvSetDsfPath);
};
