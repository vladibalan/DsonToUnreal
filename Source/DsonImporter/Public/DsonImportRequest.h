#pragma once
#include "CoreMinimal.h"

class USkeleton;
class USkeletalMesh;

// Input to the programmatic import entry point (FDsonImporterModule::ImportDazAsset).
struct FDsonImportRequest
{
    FString SourceAssetPath;
    bool bDumpMaterialDiagnostics = false;
};

// Outcome of a programmatic import run.
enum class EDsonImportStatus : uint8
{
    Succeeded,
    ValidationFailed,
    DependenciesUnresolved,
    AbortedBeforeAssetBuild,
    SkeletonFailed,
    MeshFailed
};

// Report returned by FDsonImporterModule::ImportDazAsset. Decoupled from the private
// FDsonImportResult so the public surface is stable regardless of pipeline internals.
struct FDsonImportReport
{
    EDsonImportStatus Status = EDsonImportStatus::ValidationFailed;
    bool bSucceeded = false;
    USkeleton* Skeleton = nullptr;
    USkeletalMesh* Mesh = nullptr;
    TArray<USkeletalMesh*> CompanionMeshes;
    FString DiagnosticSummary;
};
