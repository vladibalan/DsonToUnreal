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

    // Builds the shared parent skeleton at FigureRoot(FigureId), named
    // <FigureId>_Skeleton. Same figure DSF open + bone-build path as Build();
    // diverges only at the save location (FigureRoot instead of CharacterRoot).
    // Permissive (R7): returns nullptr on failure; never aborts the character import.
    static USkeleton* BuildParent(const FDsonImportSettings& Settings);

    // Merges any bones in CompanionDsfHandle's node_library that are absent from
    // BodySkeleton into BodySkeleton's bone tree, using the companion's parent chain
    // to anchor them correctly. Saves BodySkeleton if any bones were added.
    // Returns the number of new bones merged (0 = all companion bones already present).
    // Permissive (R7): logs a warning and returns 0 on bad input.
    static int32 MergeCompanionBonesIntoSkeleton(uint64_t CompanionDsfHandle, USkeleton* BodySkeleton);

private:
    // Shared open+build path used by Build and BuildParent: guards parser validity, opens
    // ResolvedFigureDsfPath (RAII), builds OutRefSkeleton, checks bone count.
    // Returns false (already logged) on any failure.
    static bool BuildFigureReferenceSkeleton(const FDsonImportSettings& Settings, FReferenceSkeleton& OutRefSkeleton, const TCHAR* LogTag);

    // Converts parser node records into UE reference-skeleton bones.
    // Parent bones must be added before children for UE reference skeleton validity.
    static void BuildReferenceSkeletonFromDsf(uint64_t DsfHandle, FReferenceSkeleton& OutRefSkeleton);

    // Converts one DAZ node's center/orientation/scale into a UE bone transform.
    // This is the key place to audit coordinate-system and unit-scale issues.
    static FTransform MakeBoneTransform(uint64_t DsfHandle, int32 NodeIndex, double UnitScale);

    // Creates or replaces the skeleton package asset after the reference skeleton is built.
    // PackagePath is the full UE package path; AssetName is the UObject name within it.
    static USkeleton* CreateSkeletonAsset(const FReferenceSkeleton& RefSkeleton, const FString& PackagePath, const FString& AssetName);
};
