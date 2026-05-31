#include "DsonImporter.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"

#include "DsonParserAPI.h"

DEFINE_LOG_CATEGORY_STATIC(LogDsonImporter, Log, All);

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
}

void FDsonImporterModule::ShutdownModule()
{
    if (DsonParserHandle != nullptr)
    {
        FPlatformProcess::FreeDllHandle(DsonParserHandle);
        DsonParserHandle = nullptr;
    }
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDsonImporterModule, DsonImporter)