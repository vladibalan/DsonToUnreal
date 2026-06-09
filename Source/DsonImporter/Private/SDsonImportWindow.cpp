#include "SDsonImportWindow.h"
#include "DsonContentRoots.h"
#include "DsonImportPipeline.h"
#include "DsonImporter.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "AssetRegistry/AssetData.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"

/*
 * Intent:
 * - Provide the modal Slate UI for choosing and validating a DAZ DSON file.
 * - Detect content roots, display validation/dependency status, and emit import settings.
 * - Launch the import pipeline once the user confirms, then show UI feedback.
 *
 * Read this file for UI state, button behavior, and the top-level import sequence.
 */

#define LOCTEXT_NAMESPACE "SDsonImportWindow"

static TArray<FString> GetMissingDependencyFileNames(const TArray<FDsonDependency>& Dependencies)
{
    TArray<FString> Missing;
    for (const FDsonDependency& Dep : Dependencies)
    {
        if (!Dep.bResolved)
            Missing.Add(FPaths::GetCleanFilename(Dep.Url));
    }

    return Missing;
}

static void ShowImportNotification(
    const TCHAR* Message,
    SNotificationItem::ECompletionState CompletionState,
    float ExpireDuration)
{
    FNotificationInfo Info(FText::FromString(FString(Message)));
    Info.ExpireDuration = ExpireDuration;
    Info.bUseLargeFont = false;

    TSharedPtr<SNotificationItem> Notification =
        FSlateNotificationManager::Get().AddNotification(Info);
    if (Notification.IsValid())
        Notification->SetCompletionState(CompletionState);
}

void SDsonImportWindow::Construct(const FArguments& InArgs)
{
    // Build the modal UI and cache DAZ content roots once for this dialog session.
    // User edits and Browse both flow through RunValidation.
    OnImportConfirmed = InArgs._OnImportConfirmed;

    ContentRoots = FDsonContentRoots::Detect();

    ChildSlot
    [
        SNew(SVerticalBox)

        // File picker.
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8.f, 8.f, 8.f, 4.f)
        [
            SNew(STextBlock)
            .Text(LOCTEXT("FileLabel", "DSON File"))
            .Font(FAppStyle::GetFontStyle("NormalFontBold"))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8.f, 0.f, 8.f, 8.f)
        [
            SNew(SHorizontalBox)

            + SHorizontalBox::Slot()
            .FillWidth(1.f)
            [
                SNew(SEditableTextBox)
                .Text_Lambda([this]() { return FText::FromString(SelectedFilePath); })
                .HintText(LOCTEXT("FileHint", "path/to/file.duf"))
                .OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
                {
                    SetSelectedFilePathAndValidate(NewText.ToString());
                })
            ]

            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(4.f, 0.f, 0.f, 0.f)
            [
                SNew(SButton)
                .Text(LOCTEXT("BrowseButton", "Browse..."))
                .OnClicked(this, &SDsonImportWindow::OnBrowseClicked)
            ]
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SSeparator)
        ]

        // No DAZ Studio warning.
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8.f, 6.f)
        [
            SNew(STextBlock)
            .Visibility(this, &SDsonImportWindow::GetNoDazWarningVisibility)
            .Text(LOCTEXT("NoDazWarning",
                "Warning: DAZ Studio content root not detected. "
                "Dependencies cannot be resolved."))
            .ColorAndOpacity(FLinearColor(1.f, 0.7f, 0.f))
            .AutoWrapText(true)
        ]

        // Validation success.
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8.f, 6.f, 8.f, 2.f)
        [
            SNew(STextBlock)
            .Visibility(this, &SDsonImportWindow::GetValidationSuccessVisibility)
            .Text(this, &SDsonImportWindow::GetValidationStatusText)
            .ColorAndOpacity(FLinearColor(0.2f, 0.9f, 0.2f))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8.f, 0.f, 8.f, 6.f)
        [
            SNew(STextBlock)
            .Visibility(this, &SDsonImportWindow::GetDependencyListVisibility)
            .Text(this, &SDsonImportWindow::GetDependencyStatusText)
            .ColorAndOpacity_Lambda([this]()
            {
                return ValidationResult.AllDependenciesResolved()
                    ? FLinearColor(0.2f, 0.9f, 0.2f)
                    : FLinearColor(1.f, 0.3f, 0.3f);
            })
            .AutoWrapText(true)
        ]

        // Unresolved dependency detail.
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8.f, 0.f, 8.f, 6.f)
        [
            SNew(STextBlock)
            .Visibility_Lambda([this]()
            {
                return (!ValidationResult.AllDependenciesResolved()
                    && ValidationResult.bIsValid
                    && !SelectedFilePath.IsEmpty())
                    ? EVisibility::Visible : EVisibility::Collapsed;
            })
            .Text(this, &SDsonImportWindow::GetUnresolvedDependencyText)
            .ColorAndOpacity(FLinearColor(1.f, 0.5f, 0.2f))
            .AutoWrapText(true)
        ]

        // Validation error.
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8.f, 6.f)
        [
            SNew(STextBlock)
            .Visibility(this, &SDsonImportWindow::GetValidationErrorVisibility)
            .Text(this, &SDsonImportWindow::GetValidationStatusText)
            .ColorAndOpacity(FLinearColor(1.f, 0.3f, 0.3f))
            .AutoWrapText(true)
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SSeparator)
        ]

        // Diagnostics checkbox.
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8.f, 4.f)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]()
            {
                return bDumpMaterialDiagnostics
                    ? ECheckBoxState::Checked
                    : ECheckBoxState::Unchecked;
            })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
            {
                bDumpMaterialDiagnostics = (NewState == ECheckBoxState::Checked);
            })
            [
                SNew(STextBlock)
                .Text(LOCTEXT("DumpMaterialDiagnosticsLabel",
                    "Dump material diagnostics to log (Phase 6 planning)"))
            ]
        ]

        // Buttons.
        + SVerticalBox::Slot()
        .AutoHeight()
        .HAlign(HAlign_Right)
        .Padding(8.f)
        [
            SNew(SHorizontalBox)

            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0.f, 0.f, 8.f, 0.f)
            [
                SNew(SButton)
                .Text(LOCTEXT("CancelButton", "Cancel"))
                .OnClicked(this, &SDsonImportWindow::OnCancelClicked)
            ]

            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                SNew(SButton)
                .Text(LOCTEXT("ImportButton", "Import"))
                .IsEnabled(this, &SDsonImportWindow::IsImportEnabled)
                .OnClicked(this, &SDsonImportWindow::OnImportClicked)
            ]
        ]
    ];
}

FReply SDsonImportWindow::OnBrowseClicked()
{
    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (!DesktopPlatform)
        return FReply::Handled();

    TArray<FString> OutFiles;
    const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared());
    const bool bOpened = DesktopPlatform->OpenFileDialog(
        ParentWindowHandle,
        TEXT("Select DSON File"),
        FPaths::GetPath(SelectedFilePath),
        TEXT(""),
        TEXT("DSON Files (*.duf;*.dsf)|*.duf;*.dsf"),
        EFileDialogFlags::None,
        OutFiles);

    if (bOpened && OutFiles.Num() > 0)
    {
        SetSelectedFilePathAndValidate(OutFiles[0]);
    }

    return FReply::Handled();
}

FReply SDsonImportWindow::OnImportClicked()
{
    RefreshPendingSettingsFromValidation();

    const FDsonImportResult ImportResult = FDsonImportPipeline::Run(PendingSettings, ContentRoots);
    if (ImportResult.bAbortedBeforeAssetBuild)
        return FReply::Handled();

    OnImportConfirmed.ExecuteIfBound(PendingSettings);

    USkeleton* Skeleton = ImportResult.Skeleton;
    USkeletalMesh* Mesh = ImportResult.Mesh;

    if (Skeleton)
    {
        UE_LOG(LogDsonImporter, Log,
            TEXT("Skeleton imported successfully: %s"), *Skeleton->GetPathName());

        if (Mesh)
        {
            UE_LOG(LogDsonImporter, Log,
                TEXT("Skeletal mesh imported successfully: %s"), *Mesh->GetPathName());
        }
        else
        {
            UE_LOG(LogDsonImporter, Error,
                TEXT("Skeletal mesh import failed. Check the Output Log for details."));
        }

        ShowImportNotification(
            TEXT("Skeleton imported successfully"),
            SNotificationItem::CS_Success,
            4.0f);

        FContentBrowserModule& CBModule =
            FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
        if (Mesh)
            CBModule.Get().SyncBrowserToAssets({ FAssetData(Skeleton), FAssetData(Mesh) });
        else
            CBModule.Get().SyncBrowserToAssets({ FAssetData(Skeleton) });
    }
    else
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("Skeleton import failed. Check the Output Log for details."));

        ShowImportNotification(
            TEXT("Skeleton import failed - see Output Log"),
            SNotificationItem::CS_Fail,
            6.0f);
    }

    CloseOwningWindow();

    return FReply::Handled();
}

FReply SDsonImportWindow::OnCancelClicked()
{
    CloseOwningWindow();

    return FReply::Handled();
}

void SDsonImportWindow::CloseOwningWindow()
{
    TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared());
    if (Window.IsValid())
        Window->RequestDestroyWindow();
}

void SDsonImportWindow::RunValidation(const FString& FilePath)
{
    // Validation is the only place PendingSettings is refreshed from a selected path.
    // The first resolved dependency is treated as the base figure DSF for character DUFs.
    ValidationResult = FDsonValidator::Validate(FilePath, ContentRoots);

    if (ValidationResult.bIsValid)
    {
        RefreshPendingSettingsFromValidation();
    }
}

void SDsonImportWindow::SetSelectedFilePathAndValidate(const FString& FilePath)
{
    FString AbsPath = FPaths::ConvertRelativePathToFull(FilePath);
    FPaths::NormalizeFilename(AbsPath);
    SelectedFilePath = AbsPath;
    RunValidation(SelectedFilePath);
}

void SDsonImportWindow::RefreshPendingSettingsFromValidation()
{
    PendingSettings = FDsonValidator::ToImportSettings(
        SelectedFilePath, ValidationResult, bDumpMaterialDiagnostics);
}

EVisibility SDsonImportWindow::GetValidationSuccessVisibility() const
{
    return (ValidationResult.bIsValid && !SelectedFilePath.IsEmpty())
        ? EVisibility::Visible
        : EVisibility::Collapsed;
}

EVisibility SDsonImportWindow::GetValidationErrorVisibility() const
{
    return (!ValidationResult.bIsValid && !SelectedFilePath.IsEmpty())
        ? EVisibility::Visible
        : EVisibility::Collapsed;
}

EVisibility SDsonImportWindow::GetNoDazWarningVisibility() const
{
    return ContentRoots.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SDsonImportWindow::GetDependencyListVisibility() const
{
    return (ValidationResult.bIsValid && ValidationResult.Dependencies.Num() > 0)
        ? EVisibility::Visible
        : EVisibility::Collapsed;
}

bool SDsonImportWindow::IsImportEnabled() const
{
    return ValidationResult.bIsValid && ValidationResult.AllDependenciesResolved();
}

FText SDsonImportWindow::GetValidationStatusText() const
{
    if (SelectedFilePath.IsEmpty())
        return FText::GetEmpty();

    if (ValidationResult.bIsValid)
    {
        const FString GenStr = ValidationResult.GetGenerationString();
        FString TypeStr;
        switch (ValidationResult.AssetType)
        {
            case EDsonAssetType::Figure:    TypeStr = TEXT("Figure");    break;
            case EDsonAssetType::Character: TypeStr = TEXT("Character"); break;
            case EDsonAssetType::Modifier:  TypeStr = TEXT("Modifier");  break;
            default:                        TypeStr = TEXT("Asset");     break;
        }
        return FText::FromString(FString::Printf(TEXT("%s %s"), *GenStr, *TypeStr));
    }

    return FText::FromString(ValidationResult.ErrorMessage.IsEmpty()
        ? TEXT("Not a supported figure file")
        : ValidationResult.ErrorMessage);
}

FText SDsonImportWindow::GetDependencyStatusText() const
{
    if (!ValidationResult.bIsValid || ValidationResult.Dependencies.IsEmpty())
        return FText::GetEmpty();

    if (ValidationResult.AllDependenciesResolved())
    {
        return FText::FromString(FString::Printf(
            TEXT("All dependencies resolved (%d file%s)"),
            ValidationResult.Dependencies.Num(),
            ValidationResult.Dependencies.Num() == 1 ? TEXT("") : TEXT("s")));
    }

    TArray<FString> Missing;
    for (const FString& FileName : GetMissingDependencyFileNames(ValidationResult.Dependencies))
    {
        Missing.Add(FileName + TEXT(" - not found"));
    }
    return FText::FromString(TEXT("Missing dependencies:\n") + FString::Join(Missing, TEXT("\n")));
}

FText SDsonImportWindow::GetUnresolvedDependencyText() const
{
    if (ValidationResult.AllDependenciesResolved() || !ValidationResult.bIsValid)
        return FText::GetEmpty();

    FString Lines;
    for (const FString& FileName : GetMissingDependencyFileNames(ValidationResult.Dependencies))
    {
        Lines += FString::Printf(TEXT("Missing: %s\n"), *FileName);
    }

    if (!ContentRoots.IsEmpty())
    {
        Lines += TEXT("Searched in:\n");
        for (const FString& Root : ContentRoots)
            Lines += FString::Printf(TEXT("  %s\n"), *Root);
    }

    return FText::FromString(Lines.TrimEnd());
}

#undef LOCTEXT_NAMESPACE
