#pragma once

#include "CoreMinimal.h"

class UObject;
class UPackage;

class FDsonAssetUtils
{
public:
    static UPackage* CreateLoadedPackage(const FString& PackagePath, const TCHAR* LogPrefix);
    static bool SaveAssetPackage(UPackage* Package, UObject* Asset, const FString& PackagePath, const TCHAR* LogPrefix);
};
