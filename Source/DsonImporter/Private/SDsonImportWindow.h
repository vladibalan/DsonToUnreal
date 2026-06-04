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
    bool bDumpMaterialDiagnostics = false;  // temporary — Phase 6 planning diagnostic
};

DECLARE_DELEGATE_OneParam(FOnDsonImportConfirmed, const FDsonImportSettings&)

class SDsonImportWindow : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SDsonImportWindow) {}
        SLATE_EVENT(FOnDsonImportConfirmed, OnImportConfirmed)
    SLATE_END_ARGS()

    // Builds the modal import UI and initializes detected content roots.
    // Validation and import work are triggered by callbacks below, not during construction.
    void Construct(const FArguments& InArgs);

private:
    // UI callbacks
    // Opens a native file picker, stores the chosen file, and immediately validates it.
    FReply OnBrowseClicked();

    // Runs the import sequence using PendingSettings: diagnostics, skeleton, materials,
    // textures, mesh, and skin weights. This is the high-level orchestration point.
    FReply OnImportClicked();

    // Closes the modal window without touching import state or assets.
    FReply OnCancelClicked();

    // Validation
    // Calls FDsonValidator and updates PendingSettings only when the selected file is valid.
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
    bool bDumpMaterialDiagnostics = false;

};
