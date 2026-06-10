#include "DsonImporter.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IMainFrameModule.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "ToolMenus.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"

#include "DsonParserFunctions.h"
#include "DsonParserVersion.h"  // compile-time DSONPARSER_VERSION_*
#include "DsonImportUtils.h"    // DsonImportUtils::FromUtf8
#include "SDsonImportWindow.h"
#include "DsonContentRoots.h"
#include "DsonValidator.h"
#include "DsonImportPipeline.h"
#include "DsonImportTypes.h"

/*
 * Intent:
 * - Bootstrap the editor module.
 * - Load the bundled DsonParser DLL and bind its C exports into GDsonParser.
 * - Register the File menu command that opens the DAZ import dialog.
 *
 * Read this file for parser DLL loading, module lifecycle, and menu wiring.
 * Import behavior after the dialog lives in the builder/validator classes.
 */

DEFINE_LOG_CATEGORY(LogDsonImporter);

#define LOCTEXT_NAMESPACE "FDsonImporterModule"

FDsonParserAPI GDsonParser;

namespace
{
    void* GDsonParserDllHandle = nullptr;

    // Idempotent: no-op if already bound in this image; returns false on load/ABI failure.
    bool EnsureDsonParserLoaded()
    {
        if (GDsonParser.IsValid())
            return true;

        const FString PluginBaseDir = IPluginManager::Get()
            .FindPlugin("DsonToUnreal")->GetBaseDir();

        const FString DllDir = FPaths::Combine(
            PluginBaseDir,
            TEXT("Source/ThirdParty/DsonParser/Libs/Win64"));

        const FString DllPath = FPaths::Combine(DllDir, TEXT("DsonParser.dll"));

        if (!FPaths::FileExists(DllPath))
        {
            UE_LOG(LogDsonImporter, Error,
                TEXT("DsonParser.dll not found at: %s"), *DllPath);
            return false;
        }

        GDsonParserDllHandle = FPlatformProcess::GetDllHandle(*DllPath);

        if (GDsonParserDllHandle == nullptr)
        {
            UE_LOG(LogDsonImporter, Error,
                TEXT("Failed to load DsonParser.dll from: %s"), *DllPath);
            return false;
        }

        UE_LOG(LogDsonImporter, Log,
            TEXT("DsonParser.dll loaded successfully from: %s"), *DllPath);

        // Bind every export from the single DSON_PARSER_API_LIST source of truth. Required
        // exports (1) log as Error; optional ones (0) log as Warning. To add/change an export,
        // edit the list in DsonParserFunctions.h - nothing here changes.
#define DSON_PARSER_BIND(Required, Ret, Member, ExportName, Params) \
        GDsonParser.Member = (decltype(GDsonParser.Member)) \
            FPlatformProcess::GetDllExport(GDsonParserDllHandle, TEXT(#ExportName)); \
        if (!GDsonParser.Member) \
        { \
            if constexpr ((Required) != 0) \
            { \
                UE_LOG(LogDsonImporter, Error, \
                    TEXT("DsonParser: missing export: " #ExportName)); \
            } \
            else \
            { \
                UE_LOG(LogDsonImporter, Warning, \
                    TEXT("DsonParser: missing export: " #ExportName)); \
            } \
        }

        DSON_PARSER_API_LIST(DSON_PARSER_BIND)

#undef DSON_PARSER_BIND

        if (!GDsonParser.IsValid())
        {
            UE_LOG(LogDsonImporter, Error,
                TEXT("DsonParser: required exports missing - plugin will not function"));
            return false;
        }

        // Reconcile the loaded DLL against the parser header this plugin was built
        // against. SemVer with C-ABI semantics (ThirdParty/DsonParser/Include/
        // CHANGELOG.md): a differing MAJOR can mean a broken ABI, so refuse to
        // register; a matching MAJOR is binary-compatible (warn only). Runtime
        // complement to DsonParserAbiCheck.cpp, which only proves header<->binding
        // agreement and cannot see the actual DLL.
        if (GDsonParser.GetVersion != nullptr)
        {
            const FString RuntimeVersion = DsonImportUtils::FromUtf8(GDsonParser.GetVersion());
            const FString BuiltAgainstVersion = TEXT(DSONPARSER_VERSION_STRING);
            const int32 RuntimeMajor = FCString::Atoi(*RuntimeVersion); // leading int; 0 if unparseable

            UE_LOG(LogDsonImporter, Log,
                TEXT("DsonParser DLL version: %s (plugin built against %s)"),
                *RuntimeVersion, *BuiltAgainstVersion);

            if (RuntimeMajor <= 0) // parser MAJOR is always >= 1 once versioned
            {
                UE_LOG(LogDsonImporter, Warning,
                    TEXT("DsonParser version string '%s' unparseable; cannot verify ABI MAJOR. Proceeding."),
                    *RuntimeVersion);
            }
            else if (RuntimeMajor != DSONPARSER_VERSION_MAJOR)
            {
                UE_LOG(LogDsonImporter, Error,
                    TEXT("DsonParser ABI MAJOR mismatch: DLL reports %s, plugin built against %s. ")
                    TEXT("The C ABI may be incompatible; not registering the importer."),
                    *RuntimeVersion, *BuiltAgainstVersion);
                GDsonParser = FDsonParserAPI{};
                FPlatformProcess::FreeDllHandle(GDsonParserDllHandle);
                GDsonParserDllHandle = nullptr;
                return false;
            }
            else if (RuntimeVersion != BuiltAgainstVersion)
            {
                UE_LOG(LogDsonImporter, Warning,
                    TEXT("DsonParser version skew (compatible MAJOR %d): DLL %s vs built-against %s."),
                    DSONPARSER_VERSION_MAJOR, *RuntimeVersion, *BuiltAgainstVersion);
            }
        }
        else
        {
            UE_LOG(LogDsonImporter, Warning,
                TEXT("DsonParser.dll predates versioning (no DsonParser_GetVersion export); ")
                TEXT("cannot verify ABI compatibility. Proceeding."));
        }

        UE_LOG(LogDsonImporter, Log,
            TEXT("DsonParser: all exports loaded successfully"));

        return true;
    }

    void OpenDsonImportWindow()
    {
        TSharedRef<SWindow> ImportWindow = SNew(SWindow)
            .Title(LOCTEXT("ImportWindowTitle", "Import DAZ Genesis Character"))
            .SizingRule(ESizingRule::FixedSize)
            .ClientSize(FVector2D(620.f, 480.f))
            .SupportsMaximize(false)
            .SupportsMinimize(false);

        TSharedRef<SDsonImportWindow> ImportWidget = SNew(SDsonImportWindow);
        ImportWindow->SetContent(ImportWidget);

        TSharedPtr<SWindow> ParentWindow;
        if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
        {
            IMainFrameModule& MainFrame =
                FModuleManager::GetModuleChecked<IMainFrameModule>("MainFrame");
            ParentWindow = MainFrame.GetParentWindow();
        }

        FSlateApplication::Get().AddModalWindow(ImportWindow, ParentWindow);
    }
}

void FDsonImporterModule::StartupModule()
{
    if (!EnsureDsonParserLoaded())
        return;

    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(
            this, &FDsonImporterModule::RegisterMenus));
}

void FDsonImporterModule::ShutdownModule()
{
    UToolMenus::UnRegisterStartupCallback(this);
    if (UToolMenus* ToolMenus = UToolMenus::TryGet())
    {
        ToolMenus->RemoveSection("MainFrame.MainMenu.File", "DsonImporter");
    }

    if (GDsonParserDllHandle != nullptr)
    {
        FPlatformProcess::FreeDllHandle(GDsonParserDllHandle);
        GDsonParserDllHandle = nullptr;
    }

    GDsonParser = FDsonParserAPI{};

    UE_LOG(LogDsonImporter, Log, TEXT("DsonParser.dll unloaded"));
}

void FDsonImporterModule::RegisterMenus()
{
    UToolMenu* FileMenu = UToolMenus::Get()->ExtendMenu("MainFrame.MainMenu.File");
    if (!FileMenu)
        return;

    FToolMenuSection& Section = FileMenu->AddSection(
        "DsonImporter",
        LOCTEXT("DazStudioSection", "DAZ Studio"));

    Section.AddMenuEntry(
        "ImportGenesisCharacter",
        LOCTEXT("ImportGenesisCharacter", "Import Genesis Character..."),
        LOCTEXT("ImportGenesisCharacterTooltip",
            "Import a DAZ Genesis character from a .duf or .dsf file"),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateStatic(&OpenDsonImportWindow))
    );
}

FDsonImportReport FDsonImporterModule::ImportDazAsset(const FDsonImportRequest& Request)
{
    FDsonImportReport Report;

    if (!EnsureDsonParserLoaded())
    {
        Report.Status = EDsonImportStatus::ValidationFailed;
        Report.DiagnosticSummary = TEXT("DsonParser library unavailable or ABI-incompatible");
        UE_LOG(LogDsonImporter, Error, TEXT("ImportDazAsset: %s"), *Report.DiagnosticSummary);
        return Report;
    }

    FString Path = FPaths::ConvertRelativePathToFull(Request.SourceAssetPath);
    FPaths::NormalizeFilename(Path);

    const TArray<FString> Roots = FDsonContentRoots::Detect();
    const FDsonValidationResult Validation = FDsonValidator::Validate(Path, Roots);

    if (!Validation.bIsValid)
    {
        Report.Status = EDsonImportStatus::ValidationFailed;
        Report.DiagnosticSummary = Validation.ErrorMessage.IsEmpty()
            ? TEXT("Validation failed") : Validation.ErrorMessage;
        UE_LOG(LogDsonImporter, Error,
            TEXT("ImportDazAsset: validation failed for '%s': %s"),
            *Path, *Report.DiagnosticSummary);
        return Report;
    }

    if (!Validation.AllDependenciesResolved())
    {
        TArray<FString> Unresolved;
        for (const FDsonDependency& Dep : Validation.Dependencies)
        {
            if (!Dep.bResolved)
                Unresolved.Add(FPaths::GetCleanFilename(Dep.Url));
        }
        Report.Status = EDsonImportStatus::DependenciesUnresolved;
        Report.DiagnosticSummary = FString::Printf(
            TEXT("Unresolved dependencies: %s"), *FString::Join(Unresolved, TEXT(", ")));
        UE_LOG(LogDsonImporter, Error,
            TEXT("ImportDazAsset: %s"), *Report.DiagnosticSummary);
        return Report;
    }

    const FDsonImportSettings Settings =
        FDsonValidator::ToImportSettings(Path, Validation, Request.bDumpMaterialDiagnostics);

    const FDsonImportResult Result = FDsonImportPipeline::Run(Settings, Roots);

    // Always copy assets (permissive: companion failures don't fail the import — R7).
    Report.Skeleton = Result.Skeleton;
    Report.Mesh = Result.Mesh;
    Report.CompanionMeshes = Result.CompanionMeshes;

    if (Result.bAbortedBeforeAssetBuild)
    {
        Report.Status = EDsonImportStatus::AbortedBeforeAssetBuild;
        Report.DiagnosticSummary = TEXT("Aborted before asset build (master material missing)");
    }
    else if (!Result.Skeleton)
    {
        Report.Status = EDsonImportStatus::SkeletonFailed;
        Report.DiagnosticSummary = TEXT("Skeleton build failed");
    }
    else if (!Result.Mesh)
    {
        Report.Status = EDsonImportStatus::MeshFailed;
        Report.DiagnosticSummary = FString::Printf(
            TEXT("Mesh build failed; skeleton: %s"), *Result.Skeleton->GetPathName());
    }
    else
    {
        Report.Status = EDsonImportStatus::Succeeded;
        Report.bSucceeded = true;
        Report.DiagnosticSummary = FString::Printf(
            TEXT("Succeeded: skeleton=%s mesh=%s companions=%d"),
            *Result.Skeleton->GetPathName(),
            *Result.Mesh->GetPathName(),
            Result.CompanionMeshes.Num());
    }

    if (Report.bSucceeded)
    {
        UE_LOG(LogDsonImporter, Log,  TEXT("ImportDazAsset: %s"), *Report.DiagnosticSummary);
    }
    else
    {
        UE_LOG(LogDsonImporter, Error, TEXT("ImportDazAsset: %s"), *Report.DiagnosticSummary);
    }

    return Report;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDsonImporterModule, DsonImporter)
