#pragma once

#include "CoreMinimal.h"

struct FDsonImportResult;

// Emits one UDsonAssetRecipe asset beside the imported character.
// Called at the end of FDsonImportPipeline::Run after all meshes are built.
// Permissive (R7): a missing/empty section logs a warning and is skipped —
// recipe emission never aborts the import and never alters FDsonImportReport.
class FDsonRecipeBuilder
{
public:
    static void Build(const FDsonImportResult& Result);
};
