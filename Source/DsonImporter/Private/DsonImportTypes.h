#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class USkeleton;

// Moved here from DsonValidator.h so DsonValidator.h can include DsonImportTypes.h
// without a circular dependency (DsonImportTypes.h -> DsonValidator.h -> ...).
// EGenesisGeneration is an inter-stage value carried in FDsonImportSettings, so it
// belongs alongside the other inter-stage types.
enum class EGenesisGeneration : uint8
{
    Unknown,
    Genesis3,
    Genesis8,
    Genesis9
};

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
    FString ResolvedFigureDsfPath;  // absolute path to base figure DSF
    EGenesisGeneration Generation = EGenesisGeneration::Unknown;
    bool bDumpMaterialDiagnostics = false;
    TArray<FDsonCompanionSource> CompanionFigures;  // G9 companion figures (Slice A+)
};

struct FDsonImportResult
{
    FDsonImportSettings Settings;
    USkeleton* Skeleton = nullptr;
    USkeletalMesh* Mesh = nullptr;
    bool bAbortedBeforeAssetBuild = false;
};