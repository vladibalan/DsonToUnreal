#pragma once
#include "CoreMinimal.h"
#include "DsonImportTypes.h"

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

    // Builds the shared parent base mesh at FigureRoot(FigureId), named
    // <FigureId>_SkeletalMesh, bound to the provided parent Skeleton.
    // Material slots all default to M_DazDefault (character materials belong to the
    // delta, P1). Morphs are limited to figure-owned scope via ApplyFigureOwned.
    // Permissive (R7): returns nullptr on failure; never aborts the character import.
    static USkeletalMesh* BuildParent(
        const FDsonImportSettings& Settings,
        USkeleton* Skeleton,
        UMaterial* DefaultMaterial,
        const FString& UvSetDsfPath);

    // Builds one companion geometry DSF as its own USkeletalMesh bound to the body
    // USkeleton by bone name. Sections are wired to MaterialsByGroup by group name;
    // unmatched sections fall back to DefaultMaterial (R7 — permissive).
    // CharacterName drives the output path; CompanionAssetName is the companion slot name.
    // UvSetDsfPath is the absolute path to the UV set DSF; empty = fallback to zero UVs.
    static USkeletalMesh* BuildCompanion(
        const FString& CharacterName,
        const FString& CompanionAssetName,
        const FString& GeometryDsfPath,
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
