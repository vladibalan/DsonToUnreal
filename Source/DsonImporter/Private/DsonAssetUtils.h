#pragma once

#include "CoreMinimal.h"

class UObject;
class UPackage;

struct FDsonAssetPath
{
    FString AssetName;
    FString PackagePath;
};

class FDsonAssetUtils
{
public:
    static const FString& ImportRootPath();
    static FDsonAssetPath MakeImportAssetPath(const FString& BaseName, const TCHAR* Suffix);
    static FString MakeImportSubfolderPath(const TCHAR* SubfolderName, const FString& BaseName);
    static UPackage* CreateLoadedPackage(const FString& PackagePath, const TCHAR* LogPrefix);
    static bool SaveAssetPackage(UPackage* Package, UObject* Asset, const FString& PackagePath, const TCHAR* LogPrefix);
};
