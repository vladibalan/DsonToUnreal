#include "DsonAssetUtils.h"
#include "DsonImporter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

const FString& FDsonAssetUtils::ImportRootPath()
{
    static const FString RootPath = TEXT("/Game/DazImports");
    return RootPath;
}

FString FDsonAssetUtils::CharacterRoot(const FString& CharacterName)
{
    return ImportRootPath() / TEXT("Characters") / CharacterName;
}

FString FDsonAssetUtils::FigureRoot(const FString& FigureId)
{
    return ImportRootPath() / TEXT("Figures") / FigureId;
}

bool FDsonAssetUtils::FigureImportComplete(const FString& FigureId)
{
    const FString RecipePath = FigureRoot(FigureId) / (FigureId + TEXT("_Recipe"));
    return FPackageName::DoesPackageExist(RecipePath);
}

FString FDsonAssetUtils::SharedTexturesRoot()
{
    return ImportRootPath() / TEXT("Library") / TEXT("Textures");
}

UPackage* FDsonAssetUtils::CreateLoadedPackage(const FString& PackagePath, const TCHAR* LogPrefix)
{
    UPackage* Package = CreatePackage(*PackagePath);
    if (!Package)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("%s: failed to create package '%s'"), LogPrefix, *PackagePath);
        return nullptr;
    }

    Package->FullyLoad();
    return Package;
}

bool FDsonAssetUtils::SaveAssetPackage(
    UPackage* Package,
    UObject* Asset,
    const FString& PackagePath,
    const TCHAR* LogPrefix)
{
    if (!Package || !Asset)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("%s: cannot save package '%s' with null package or asset"),
            LogPrefix, *PackagePath);
        return false;
    }

    Package->MarkPackageDirty();

    const FString FileName = FPackageName::LongPackageNameToFilename(
        PackagePath, FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags       = RF_Public | RF_Standalone;
    SaveArgs.Error               = GError;
    SaveArgs.bWarnOfLongFilename = false;

    const bool bSaved = UPackage::SavePackage(Package, Asset, *FileName, SaveArgs);
    if (!bSaved)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("%s: failed to save asset '%s' to '%s'"),
            LogPrefix, *Asset->GetPathName(), *FileName);
        return false;
    }

    FAssetRegistryModule::GetRegistry().AssetCreated(Asset);
    return true;
}
