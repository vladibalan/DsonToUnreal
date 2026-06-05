#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "DsonImportTypes.h"
#include "DsonValidator.h"

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

    // Prepares PendingSettings, runs the import pipeline, then reports the result in the UI.
    FReply OnImportClicked();

    // Closes the modal window without touching import state or assets.
    FReply OnCancelClicked();

    // Validation
    // Calls FDsonValidator and updates PendingSettings only when the selected file is valid.
    void RunValidation(const FString& FilePath);

    // Normalizes a user-entered or browser-selected path, stores it, then validates it.
    void SetSelectedFilePathAndValidate(const FString& FilePath);

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
