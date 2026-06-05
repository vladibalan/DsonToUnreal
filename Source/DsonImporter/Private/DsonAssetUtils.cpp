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

FDsonAssetPath FDsonAssetUtils::MakeImportAssetPath(const FString& BaseName, const TCHAR* Suffix)
{
    FDsonAssetPath AssetPath;
    AssetPath.AssetName = BaseName + FString(Suffix);
    AssetPath.PackagePath = ImportRootPath() / AssetPath.AssetName;
    return AssetPath;
}

FString FDsonAssetUtils::MakeImportSubfolderPath(const TCHAR* SubfolderName, const FString& BaseName)
{
    return ImportRootPath() / FString(SubfolderName) / BaseName;
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
