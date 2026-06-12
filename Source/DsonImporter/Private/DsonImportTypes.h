#pragma once

#include "CoreMinimal.h"
#include "DsonCatalog.h"   // EGenesisGeneration (moved to the public catalog header so it is
                           // accessible to catalog consumers without pulling in private types)
#include "UObject/SoftObjectPtr.h"

class USkeletalMesh;
class USkeleton;
class UTexture2D;

// One resolved companion figure produced by DiscoverCompanionFigures (Slice A).
// GeometryDsfUrl stores the resolved absolute disk path to the companion geometry DSF
// (same semantics as FDsonImportSettings::ResolvedFigureDsfPath for the body).
struct FDsonCompanionSource
{
    FString Slot;            // PostLoadAddons slot path (e.g. ".../Face/Eyes")
    FString AssetName;       // e.g. "Genesis9Eyes"
    FString GeometryDsfUrl;  // resolved absolute path to companion geometry DSF
    FString NodeId;          // geometry node id from DSF URL fragment (e.g. "Genesis9Eyes-1")
    FString MatPresetPath;   // resolved absolute path to MAT preset DUF; empty = none
};

struct FDsonImportSettings
{
    FString DsonFilePath;
    FString CharacterName;            // sanitized imported-DUF basename; per-character identity
    FString ResolvedFigureDsfPath;    // absolute path to base figure DSF
    EGenesisGeneration Generation = EGenesisGeneration::Unknown;
    bool bDumpMaterialDiagnostics = false;
    TArray<FDsonCompanionSource> CompanionFigures;  // G9 companion figures (Slice A+)
};

struct FDsonImportResult
{
    FDsonImportSettings Settings;
    USkeleton* Skeleton = nullptr;
    USkeletalMesh* Mesh = nullptr;
    TArray<USkeletalMesh*> CompanionMeshes;  // Slice B+: one per successfully-built companion, bound to Skeleton
    TArray<FString>        CompanionSlots;   // 1:1 aligned with CompanionMeshes — same index = same companion
    bool bAbortedBeforeAssetBuild = false;
    // Slice 2: image ids that were alpha-composited (N>=2 layers) into a single UTexture2D at import.
    // Populated by FDsonTextureImporter; read by FDsonRecipeBuilder to set bImporterPreBaked markers.
    TMap<FString, TSoftObjectPtr<UTexture2D>> PreBakedComposites;
};