#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "DsonValidator.h"

// FDsonImportSettings must be declared before the delegate that references it
struct FDsonImportSettings
{
    FString DsonFilePath;
    FString ResolvedFigureDsfPath;  // absolute path to base figure DSF
    EGenesisGeneration Generation = EGenesisGeneration::Unknown;
    bool bImportSkeleton = true;
    bool bImportMesh = true;
    bool bImportMaterials = true;
    bool bImportMorphTargets = true;
    int32 MaxBoneInfluences = 8;
};

DECLARE_DELEGATE_OneParam(FOnDsonImportConfirmed, const FDsonImportSettings&)

class SDsonImportWindow : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SDsonImportWindow) {}
        SLATE_EVENT(FOnDsonImportConfirmed, OnImportConfirmed)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    // UI callbacks
    FReply OnBrowseClicked();
    FReply OnImportClicked();
    FReply OnCancelClicked();

    // Validation
    void RunValidation(const FString& FilePath);

    // Slate attribute helpers
    EVisibility GetValidationSuccessVisibility() const;
    EVisibility GetValidationErrorVisibility() const;
    EVisibility GetNoDazWarningVisibility() const;
    EVisibility GetDependencyListVisibility() const;
    bool IsImportEnabled() const;
    FText GetValidationStatusText() const;
    FText GetDependencyStatusText() const;
    FText GetUnresolvedDependencyText() const;

    // State
    TArray<FString> ContentRoots;
    FString SelectedFilePath;
    FDsonValidationResult ValidationResult;
    FDsonImportSettings PendingSettings;
    FOnDsonImportConfirmed OnImportConfirmed;

    // Import options
    bool bShouldImportSkeleton = true;
    bool bShouldImportMesh = true;
    bool bShouldImportMaterials = true;
    bool bShouldImportMorphTargets = true;

    // Max bone influences combo
    TArray<TSharedPtr<int32>> BoneInfluenceOptions;
    TSharedPtr<int32> SelectedBoneInfluences;
};