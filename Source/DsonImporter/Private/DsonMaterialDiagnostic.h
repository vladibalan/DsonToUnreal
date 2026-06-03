#pragma once
#include "CoreMinimal.h"

struct FDsonImportSettings;
class FDsonTextureImporter;
class FDsonMaterialBuilder;

// Temporary diagnostic — remove once Phase 6 material-builder is implemented
class FDsonMaterialDiagnostic
{
public:
    static void Dump(const FDsonImportSettings& Settings,
                     FDsonTextureImporter& Importer,
                     FDsonMaterialBuilder& Builder,
                     const FString& OutputFolder);
};