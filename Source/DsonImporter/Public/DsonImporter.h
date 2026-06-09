#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "DsonImportRequest.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDsonImporter, Log, All);

class FDsonImporterModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    static FDsonImporterModule& Get()
    {
        return FModuleManager::LoadModuleChecked<FDsonImporterModule>("DsonImporter");
    }

    static bool IsAvailable()
    {
        return FModuleManager::Get().IsModuleLoaded("DsonImporter");
    }

    // Runs a DAZ import headlessly from a source asset path.
    // Callers: FDsonImporterModule::Get().ImportDazAsset(Request).
    DSONIMPORTER_API FDsonImportReport ImportDazAsset(const FDsonImportRequest& Request);

private:
    void RegisterMenus();

    void* DsonParserHandle = nullptr;
};