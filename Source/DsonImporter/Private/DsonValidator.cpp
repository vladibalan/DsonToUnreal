#include "DsonValidator.h"
#include "DsonContentRoots.h"
#include "DsonImportUtils.h"
#include "DsonParserFunctions.h"
#include "DsonLoadedDocument.h"
#include "Misc/Paths.h"
#include "DsonImporter.h"

/*
 * Intent:
 * - Validate the selected .duf/.dsf before import.
 * - Determine whether the file is a figure, character, modifier, or unsupported asset.
 * - Resolve dependencies such as base figure and geometry references through content roots.
 *
 * Read this file for import eligibility, Genesis generation detection, and dependency resolution.
 */

bool FDsonValidationResult::AllDependenciesResolved() const
{
    for (const FDsonDependency& Dep : Dependencies)
    {
        if (!Dep.bResolved)
            return false;
    }
    return true;
}

FString GenerationToString(EGenesisGeneration Generation)
{
    switch (Generation)
    {
        case EGenesisGeneration::Genesis3: return TEXT("Genesis 3");
        case EGenesisGeneration::Genesis8: return TEXT("Genesis 8");
        case EGenesisGeneration::Genesis9: return TEXT("Genesis 9");
        default:                           return TEXT("Unknown");
    }
}

FString FDsonValidationResult::GetGenerationString() const
{
    return GenerationToString(Generation);
}

static FDsonDependency BuildDependency(const FString& Url, const TArray<FString>& ContentRoots)
{
    FDsonDependency Dep;
    Dep.Url = Url;
    UE_LOG(LogDsonImporter, Verbose,
        TEXT("DsonValidator: dependency URL: %s"), *Dep.Url);

    Dep.ResolvedPath = FDsonContentRoots::ResolveUrl(Url, ContentRoots);
    Dep.bResolved = !Dep.ResolvedPath.IsEmpty();

    if (Dep.bResolved)
    {
        UE_LOG(LogDsonImporter, Verbose,
            TEXT("DsonValidator: resolved to: %s"), *Dep.ResolvedPath);
    }
    else
    {
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonValidator: UNRESOLVED dependency: %s"), *Dep.Url);
    }

    return Dep;
}

static bool IsImportableAssetType(EDsonAssetType AssetType)
{
    return AssetType != EDsonAssetType::Unsupported
        && AssetType != EDsonAssetType::Unknown;
}

FDsonValidationResult FDsonValidator::Validate(
    const FString& FilePath,
    const TArray<FString>& ContentRoots)
{
    // Parser-level gate before the UI enables import. This reads metadata and dependency
    // references only; actual asset creation happens later in the builders.
    FDsonValidationResult Result;

    if (!GDsonParser.IsValid())
    {
        Result.ErrorMessage = TEXT("DsonParser library is not loaded");
        return Result;
    }

    if (!FPaths::FileExists(FilePath))
    {
        Result.ErrorMessage = TEXT("File does not exist");
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonValidator: rejected - %s"), *Result.ErrorMessage);
        return Result;
    }

    UE_LOG(LogDsonImporter, Log,
        TEXT("DsonValidator: loading file: %s"), *FilePath);

    // RAII document: handles read + UTF-8 conversion + parse, and is destroyed on every
    // return path below. OutError carries the parser's failure detail for the UI.
    FDsonLoadedDocument Document;
    FString LoadError;
    if (!Document.LoadFromFileAsWarning(FilePath, TEXT("DsonValidator"), &LoadError))
    {
        Result.ErrorMessage = FString::Printf(
            TEXT("Failed to load DSON file: %s\nFile: %s"), *LoadError, *FilePath);
        return Result;
    }

    const DsonDocumentHandle DocHandle = Document.GetHandle();

    const char* AssetTypeStr = GDsonParser.GetAssetType(DocHandle);
    UE_LOG(LogDsonImporter, Verbose,
        TEXT("DsonValidator: asset type = '%s'"),
        AssetTypeStr ? UTF8_TO_TCHAR(AssetTypeStr) : TEXT("(null)"));

    Result.AssetType = ParseAssetType(AssetTypeStr ? AssetTypeStr : "");

    if (!IsImportableAssetType(Result.AssetType))
    {
        Result.ErrorMessage = FString::Printf(
            TEXT("Unsupported asset type: '%s'. Expected 'figure' or 'character'."),
            AssetTypeStr ? UTF8_TO_TCHAR(AssetTypeStr) : TEXT("(none)"));
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonValidator: rejected - %s"), *Result.ErrorMessage);
        return Result;
    }

    const char* AssetId = GDsonParser.GetAssetId(DocHandle);
    UE_LOG(LogDsonImporter, Verbose,
        TEXT("DsonValidator: asset id = '%s'"),
        AssetId ? UTF8_TO_TCHAR(AssetId) : TEXT("(null)"));

    // Note: DsonDocument_GetAssetName does not exist in the C API.
    // generation is detected from the asset ID alone.
    Result.Generation = DetectGeneration(AssetId ? AssetId : "");
    UE_LOG(LogDsonImporter, Log,
        TEXT("DsonValidator: generation detected = %s"), *Result.GetGenerationString());

    ResolveDependencies(DocHandle, Result.AssetType, ContentRoots, Result.Dependencies);

    Result.bIsValid = true;
    return Result;
}

EDsonAssetType FDsonValidator::ParseAssetType(const char* TypeStr)
{
    if (!TypeStr || TypeStr[0] == '\0')
        return EDsonAssetType::Unknown;

    const FString Type = UTF8_TO_TCHAR(TypeStr);
    if (Type.Equals(TEXT("figure"),    ESearchCase::IgnoreCase)) return EDsonAssetType::Figure;
    if (Type.Equals(TEXT("character"), ESearchCase::IgnoreCase)) return EDsonAssetType::Character;
    if (Type.Equals(TEXT("modifier"),  ESearchCase::IgnoreCase)) return EDsonAssetType::Modifier;

    return EDsonAssetType::Unsupported;
}

EGenesisGeneration FDsonValidator::DetectGeneration(const char* AssetId)
{
    if (!AssetId || AssetId[0] == '\0')
        return EGenesisGeneration::Unknown;

    // URL-decode before checking - DAZ asset ids use %20 for spaces (e.g. Genesis%209).
    const FString Id = FDsonContentRoots::UrlDecode(UTF8_TO_TCHAR(AssetId));

    if (Id.Contains(TEXT("Genesis9"),   ESearchCase::IgnoreCase) ||
        Id.Contains(TEXT("Genesis 9"),  ESearchCase::IgnoreCase))
        return EGenesisGeneration::Genesis9;

    if (Id.Contains(TEXT("Genesis8"),   ESearchCase::IgnoreCase) ||
        Id.Contains(TEXT("Genesis 8"),  ESearchCase::IgnoreCase))
        return EGenesisGeneration::Genesis8;

    if (Id.Contains(TEXT("Genesis3"),   ESearchCase::IgnoreCase) ||
        Id.Contains(TEXT("Genesis 3"),  ESearchCase::IgnoreCase))
        return EGenesisGeneration::Genesis3;

    return EGenesisGeneration::Unknown;
}

void FDsonValidator::ResolveDependencies(
    void* DocHandle,
    EDsonAssetType AssetType,
    const TArray<FString>& ContentRoots,
    TArray<FDsonDependency>& OutDependencies)
{
    // Figure files are self-contained; geometry lives in the file itself.
    // Character DUFs point at external figure/geometry DSFs. Dependencies are de-duped by
    // disk file URL before fragment because multiple references can target one DSF file.
    if (AssetType == EDsonAssetType::Figure)
        return;

    // Character / scene files reference external figure DSFs via scene node geometry URLs
    TSet<FString> SeenUrls;
    const int SceneNodeCount = GDsonParser.GetSceneNodeCount(DocHandle);

    for (int i = 0; i < SceneNodeCount; ++i)
    {
        const int GeomCount = GDsonParser.GetSceneNodeGeometryCount(DocHandle, i);
        for (int j = 0; j < GeomCount; ++j)
        {
            const char* RawUrl = GDsonParser.GetSceneNodeGeometryUrl(DocHandle, i, j);
            if (!RawUrl || RawUrl[0] == '\0')
                continue;

            const FString UrlStr = UTF8_TO_TCHAR(RawUrl);

            // Use the path portion (before #) as the deduplication key.
            const FString UrlKey = DsonImportUtils::StripUrlFragment(UrlStr);
            if (SeenUrls.Contains(UrlKey))
                continue;
            SeenUrls.Add(UrlKey);

            OutDependencies.Add(BuildDependency(UrlStr, ContentRoots));
        }
    }
}
