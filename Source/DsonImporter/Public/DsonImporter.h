#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "DsonImportRequest.h"
#include "DsonCatalog.h"   // FDsonCatalogRoot, FDsonCatalogResult
#include "Async/Future.h"  // TFuture

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
    DSONIMPORTER_API FDsonImportReport ImportDazAsset(const FDsonImportRequest& Request);

    // Starts a background enumeration and classification of all .duf/.dsf assets under
    // the supplied roots. Returns a TFuture that resolves when all roots are processed.
    // ProgressCallback (optional) fires on the game thread after each root completes.
    // If a walk is already in progress the returned future resolves immediately with
    // bCompleted=false — the caller should retry after the first walk finishes.
    DSONIMPORTER_API TFuture<FDsonCatalogResult> BeginCatalogEnumerate(
        TArray<FDsonCatalogRoot> Roots,
        TFunction<void(int32 /*Done*/, int32 /*Total*/)> ProgressCallback = nullptr);

    // Deletes the on-disk catalog cache and clears the thumbnail cache. The next
    // BeginCatalogEnumerate call performs a full rescan of every root.
    DSONIMPORTER_API void InvalidateCatalog();

    // Returns the raw PNG bytes of the preview image next to <RootAbsPath>/<Id>.
    // Probes <asset>.png (appended form) then <basename>.png (change-extension form).
    // Lazy LRU cache (cap 256); cleared by InvalidateCatalog(). Game-thread-only.
    DSONIMPORTER_API TOptional<TArray<uint8>> GetCatalogThumbnail(
        const FString& RootAbsPath, const FString& Id);

private:
    void RegisterMenus();
};
