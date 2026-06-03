#pragma once
#include "CoreMinimal.h"

struct FDsonImportSettings;
class FDsonTextureImporter;

// Verbose channel-level material diagnostic dump
class FDsonMaterialDiagnostic
{
public:
    static void Dump(const FDsonImportSettings& Settings,
                     FDsonTextureImporter& Importer,
                     const FString& OutputFolder);
};