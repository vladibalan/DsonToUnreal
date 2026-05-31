#pragma once
#include "CoreMinimal.h"

UENUM()
enum class EDsonAssetType : uint8
{
    Unknown,
    Figure,      // asset_info.type == "figure" (base mesh DSF)
    Character,   // asset_info.type == "character" (scene DUF)
    Modifier,    // morph/pose
    Unsupported
};

UENUM()
enum class EGenesisGeneration : uint8
{
    Unknown,
    Genesis3,
    Genesis8,
    Genesis9
};

struct FDsonDependency
{
    FString Url;            // raw DSON URL from the file
    FString ResolvedPath;   // absolute path on disk; empty if not found
    bool bResolved = false;
};

struct FDsonValidationResult
{
    bool bIsValid = false;
    EDsonAssetType AssetType = EDsonAssetType::Unknown;
    EGenesisGeneration Generation = EGenesisGeneration::Unknown;
    TArray<FDsonDependency> Dependencies;
    FString ErrorMessage;

    bool AllDependenciesResolved() const;
    FString GetGenerationString() const;
};

class FDsonValidator
{
public:
    /**
     * Validates a DSON file and resolves its dependencies.
     * @param FilePath      Absolute path to the .duf or .dsf file
     * @param ContentRoots  List of DAZ content root folders
     * @return Validation result with asset type, generation, and dependencies
     */
    static FDsonValidationResult Validate(
        const FString& FilePath,
        const TArray<FString>& ContentRoots);

private:
    static EDsonAssetType ParseAssetType(const char* TypeStr);
    static EGenesisGeneration DetectGeneration(const char* AssetId);
    static void ResolveDependencies(
        void* DocHandle,
        EDsonAssetType AssetType,
        const TArray<FString>& ContentRoots,
        TArray<FDsonDependency>& OutDependencies);
};