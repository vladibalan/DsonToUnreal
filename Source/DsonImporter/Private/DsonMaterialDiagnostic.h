#pragma once
#include "CoreMinimal.h"
#include "DsonImportTypes.h"

class FDsonTextureImporter;

// Verbose channel-level material diagnostic dump
class FDsonMaterialDiagnostic
{
public:
    // Emits verbose material/channel diagnostics for the selected import file and dependencies.
    // Intended for audit/planning; it should not be required for the normal import path.
    static void Dump(const FDsonImportSettings& Settings,
                     FDsonTextureImporter& Importer);
};
