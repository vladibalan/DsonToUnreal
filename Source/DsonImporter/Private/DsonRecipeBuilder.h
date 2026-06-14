#pragma once

#include "CoreMinimal.h"

struct FDsonImportResult;
struct FDsonImportSettings;
class USkeleton;
class USkeletalMesh;

// Emits one UDsonAssetRecipe asset beside the imported character.
// Called at the end of FDsonImportPipeline::Run after all meshes are built.
// Permissive (R7): a missing/empty section logs a warning and is skipped —
// recipe emission never aborts the import and never alters FDsonImportReport.
class FDsonRecipeBuilder
{
public:
    static void Build(const FDsonImportResult& Result);

    // Emits the minimal parent completeness marker: <FigureId>_Recipe under
    // FigureRoot(FigureId). Its presence is the signal FigureImportComplete()
    // checks — so it MUST be emitted last (after skeleton + mesh). Ancestry
    // field is S4; not set here. Permissive (R7): failure logs a warning and
    // returns; never aborts the character import.
    static void BuildParentMarker(
        const FDsonImportSettings& Settings,
        USkeleton* ParentSkeleton,
        USkeletalMesh* ParentMesh);
};
