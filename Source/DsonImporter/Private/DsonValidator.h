#pragma once
#include "CoreMinimal.h"
#include "DsonImportTypes.h"  // EGenesisGeneration, FDsonCompanionSource

// Plain C++ enum: used only internally (no UPROPERTY/Blueprint exposure), so no UENUM/reflection.
enum class EDsonAssetType : uint8
{
    Unknown,
    Figure,      // asset_info.type == "figure" (base mesh DSF)
    Character,   // asset_info.type == "character" (scene DUF)
    Modifier,    // morph/pose
    Unsupported
};

// Human-readable Genesis generation label, shared by the validator UI text and the
// material diagnostic dump so the mapping lives in exactly one place.
FString GenerationToString(EGenesisGeneration Generation);

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
    TArray<FDsonCompanionSource> CompanionFigures;  // resolved G9 companions (Slice A+)
    FString ErrorMessage;

    // True only when every dependency discovered by validation resolved to disk.
    // Import UI uses this to decide whether the selected file is actionable.
    bool AllDependenciesResolved() const;

    // Human-readable generation label for UI/status text.
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

    // Assembles FDsonImportSettings from a validated result. Single source used by
    // both SDsonImportWindow and ImportDazAsset — call only when Validation.bIsValid.
    static FDsonImportSettings ToImportSettings(
        const FString& SourceAssetPath,
        const FDsonValidationResult& Validation,
        bool bDumpMaterialDiagnostics);

    // Maps parser asset_info.type strings onto the plugin's import categories.
    // Public so the catalog can classify assets without calling Validate() (R4 — reuse, no double-open).
    static EDsonAssetType ParseAssetType(const char* TypeStr);

    // Infers Genesis generation from DAZ asset IDs.
    // Public so the catalog can call it directly on an already-open document handle (R4).
    static EGenesisGeneration DetectGeneration(const char* AssetId);

    // Reads parser-reported references for the chosen asset type and resolves each URL
    // through the DAZ content roots. OutDependencies preserves raw URL plus disk result.
    // Public so the catalog can call it on its own FDsonLoadedDocument without opening extras (R4).
    static void ResolveDependencies(
        void* DocHandle,
        EDsonAssetType AssetType,
        const TArray<FString>& ContentRoots,
        TArray<FDsonDependency>& OutDependencies);

private:
    // Discovers companion figures from scene.extra PostLoadAddons (G9 character presets).
    // Requires DsonParser >= 1.1.0 exports; exits silently when exports are absent.
    // Null-guards all 5 addon exports before use; skips any slot that fails to resolve.
    static void DiscoverCompanionFigures(
        void* DocHandle,
        const TArray<FString>& ContentRoots,
        TArray<FDsonCompanionSource>& OutCompanions);
};