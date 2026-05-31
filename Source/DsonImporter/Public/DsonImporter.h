#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

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

private:
    void* DsonParserHandle = nullptr;
};