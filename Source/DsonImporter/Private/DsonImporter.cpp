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

DEFINE_LOG_CATEGORY(LogDsonImporter);

#define LOCTEXT_NAMESPACE "FDsonImporterModule"

FDsonParserAPI GDsonParser;

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

#define LOAD_FN(Member, ExportName) \
    GDsonParser.Member = (decltype(GDsonParser.Member)) \
        FPlatformProcess::GetDllExport(DsonParserHandle, TEXT(#ExportName)); \
    if (!GDsonParser.Member) { \
        UE_LOG(LogDsonImporter, Error, \
            TEXT("DsonParser: missing export: " #ExportName)); \
    }

    LOAD_FN(Create,                     DsonDocument_Create)
    LOAD_FN(Destroy,                    DsonDocument_Destroy)
    LOAD_FN(GetLastError,               DsonParser_GetLastError)
    LOAD_FN(GetAssetType,               DsonDocument_GetAssetType)
    LOAD_FN(GetAssetId,                 DsonDocument_GetAssetId)
    LOAD_FN(GetSceneNodeCount,          DsonDocument_GetSceneNodeCount)
    LOAD_FN(GetSceneNodeGeometryCount,  DsonDocument_GetSceneNodeGeometryCount)
    LOAD_FN(GetSceneNodeGeometryUrl,    DsonDocument_GetSceneNodeGeometryUrl)
    LOAD_FN(LoadFromString,             DsonDocument_LoadFromString)

#undef LOAD_FN

#define LOAD_FN_WARN(Member, ExportName) \
    GDsonParser.Member = (decltype(GDsonParser.Member)) \
        FPlatformProcess::GetDllExport(DsonParserHandle, TEXT(#ExportName)); \
    if (!GDsonParser.Member) { \
        UE_LOG(LogDsonImporter, Warning, \
            TEXT("DsonParser: missing export: " #ExportName)); \
    }

    LOAD_FN_WARN(GetNodeCount,        DsonDocument_GetNodeCount)
    LOAD_FN_WARN(GetNodeId,           DsonDocument_GetNodeId)
    LOAD_FN_WARN(GetNodeName,         DsonDocument_GetNodeName)
    LOAD_FN_WARN(GetNodeType,         DsonDocument_GetNodeType)
    LOAD_FN_WARN(GetNodeParent,       DsonDocument_GetNodeParent)
    LOAD_FN_WARN(GetNodeCenterPointX, DsonDocument_GetNodeCenterPointX)
    LOAD_FN_WARN(GetNodeCenterPointY, DsonDocument_GetNodeCenterPointY)
    LOAD_FN_WARN(GetNodeCenterPointZ, DsonDocument_GetNodeCenterPointZ)
    LOAD_FN_WARN(GetNodeOrientationX, DsonDocument_GetNodeOrientationX)
    LOAD_FN_WARN(GetNodeOrientationY, DsonDocument_GetNodeOrientationY)
    LOAD_FN_WARN(GetNodeOrientationZ, DsonDocument_GetNodeOrientationZ)
    LOAD_FN_WARN(GetNodeRotationOrder,DsonDocument_GetNodeRotationOrder)
    LOAD_FN_WARN(GetNodeGeneralScale, DsonDocument_GetNodeGeneralScale)
    LOAD_FN_WARN(GetUnitScale,        DsonDocument_GetUnitScale)

    LOAD_FN_WARN(GetGeometryCount,            DsonDocument_GetGeometryCount)
    LOAD_FN_WARN(GetVertexCount,              DsonDocument_GetVertexCount)
    LOAD_FN_WARN(GetVertexX,                  DsonDocument_GetVertexX)
    LOAD_FN_WARN(GetVertexY,                  DsonDocument_GetVertexY)
    LOAD_FN_WARN(GetVertexZ,                  DsonDocument_GetVertexZ)
    LOAD_FN_WARN(GetPolylistCount,            DsonDocument_GetPolylistCount)
    LOAD_FN_WARN(GetPolylistFaceVertexCount,  DsonDocument_GetPolylistFaceVertexCount)
    LOAD_FN_WARN(GetPolylistFaceVertex,       DsonDocument_GetPolylistFaceVertex)
    LOAD_FN_WARN(GetPolylistFaceMaterialIndex,DsonDocument_GetPolylistFaceMaterialIndex)
    LOAD_FN_WARN(GetUVSetCount,               DsonDocument_GetUVSetCount)
    LOAD_FN_WARN(GetUVCount,                  DsonDocument_GetUVCount)
    LOAD_FN_WARN(GetUVU,                      DsonDocument_GetUVU)
    LOAD_FN_WARN(GetUVV,                      DsonDocument_GetUVV)
    LOAD_FN_WARN(GetUVPolygonVertexIndex,     DsonDocument_GetUVPolygonVertexIndex)
    LOAD_FN_WARN(GetMaterialGroupCount,       DsonDocument_GetMaterialGroupCount)
    LOAD_FN_WARN(GetMaterialGroupName,        DsonDocument_GetMaterialGroupName)

#undef LOAD_FN_WARN

    if (!GDsonParser.IsValid())
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonParser: required exports missing — plugin will not function"));
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