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
    // Per-character folder: {Root}/Characters/{CharacterName}
    static FString CharacterRoot(const FString& CharacterName);
    // Per-figure-id folder: {Root}/Figures/{FigureId}
    static FString FigureRoot(const FString& FigureId);
    // True when a completed parent import exists at FigureRoot(FigureId)
    // (checks for the recipe asset emitted last by a finished import).
    static bool FigureImportComplete(const FString& FigureId);
    // Shared source-texture root: {Root}/Library/Textures
    static FString SharedTexturesRoot();
    static UPackage* CreateLoadedPackage(const FString& PackagePath, const TCHAR* LogPrefix);
    static bool SaveAssetPackage(UPackage* Package, UObject* Asset, const FString& PackagePath, const TCHAR* LogPrefix);
};
