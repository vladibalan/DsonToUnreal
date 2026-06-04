#pragma once

#include "CoreMinimal.h"
#include "DsonImportTypes.h"

class FDsonImportPipeline
{
public:
    static FDsonImportResult Run(
        const FDsonImportSettings& Settings,
        const TArray<FString>& ContentRoots);
};

