#pragma once

#include "CoreMinimal.h"
#include "DsonValidator.h"

class USkeletalMesh;
class USkeleton;

struct FDsonImportSettings
{
    FString DsonFilePath;
    FString ResolvedFigureDsfPath;  // absolute path to base figure DSF
    EGenesisGeneration Generation = EGenesisGeneration::Unknown;
    bool bDumpMaterialDiagnostics = false;  // temporary diagnostic toggle
};

struct FDsonImportResult
{
    FDsonImportSettings Settings;
    USkeleton* Skeleton = nullptr;
    USkeletalMesh* Mesh = nullptr;
    bool bAbortedBeforeAssetBuild = false;
};
