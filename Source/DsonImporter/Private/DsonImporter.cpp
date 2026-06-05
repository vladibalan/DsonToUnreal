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
#include "SDsonImportWindow.h"

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
        return;
    }

    DsonParserHandle = FPlatformProcess::GetDllHandle(*DllPath);

    if (DsonParserHandle == nullptr)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("Failed to load DsonParser.dll from: %s"), *DllPath);
        return;
    }

    UE_LOG(LogDsonImporter, Log,
        TEXT("DsonParser.dll loaded successfully from: %s"), *DllPath);

    // Bind every export from the single DSON_PARSER_API_LIST source of truth. Required
    // exports (1) log as Error; optional ones (0) log as Warning. To add/change an export,
    // edit the list in DsonParserFunctions.h - nothing here changes.
#define DSON_PARSER_BIND(Required, Ret, Member, ExportName, Params) \
    GDsonParser.Member = (decltype(GDsonParser.Member)) \
        FPlatformProcess::GetDllExport(DsonParserHandle, TEXT(#ExportName)); \
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
        return;
    }

    UE_LOG(LogDsonImporter, Log,
        TEXT("DsonParser: all exports loaded successfully"));

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

    if (DsonParserHandle != nullptr)
    {
        FPlatformProcess::FreeDllHandle(DsonParserHandle);
        DsonParserHandle = nullptr;
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

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDsonImporterModule, DsonImporter)
