#include "DsonImporter.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IMainFrameModule.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "ToolMenus.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"

#include "DsonParserAPI.h"
#include "SDsonImportWindow.h"

DEFINE_LOG_CATEGORY(LogDsonImporter);

#define LOCTEXT_NAMESPACE "FDsonImporterModule"

void FDsonImporterModule::StartupModule()
{
    // Manually load the DsonParser DLL from the plugin's ThirdParty folder
    const FString PluginBaseDir = IPluginManager::Get()
        .FindPlugin("DsonToUnreal")->GetBaseDir();

    const FString DllPath = FPaths::Combine(
        PluginBaseDir,
        TEXT("Source/ThirdParty/DsonParser/Libs/Win64/DsonParser.dll"));

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
            TEXT("Failed to load DsonParser.dll"));
        return;
    }

    UE_LOG(LogDsonImporter, Log,
        TEXT("DsonParser.dll loaded successfully"));

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
        FUIAction(FExecuteAction::CreateLambda([]()
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
        }))
    );
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDsonImporterModule, DsonImporter)