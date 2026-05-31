#include "DsonValidator.h"
#include "DsonContentRoots.h"
#include "DsonParserAPI.h"
#include "Misc/Paths.h"
#include "DsonImporter.h"

bool FDsonValidationResult::AllDependenciesResolved() const
{
    for (const FDsonDependency& Dep : Dependencies)
    {
        if (!Dep.bResolved)
            return false;
    }
    return true;
}

FString FDsonValidationResult::GetGenerationString() const
{
    switch (Generation)
    {
        case EGenesisGeneration::Genesis3: return TEXT("Genesis 3");
        case EGenesisGeneration::Genesis8: return TEXT("Genesis 8");
        case EGenesisGeneration::Genesis9: return TEXT("Genesis 9");
        default:                           return TEXT("Unknown");
    }
}

FDsonValidationResult FDsonValidator::Validate(
    const FString& FilePath,
    const TArray<FString>& ContentRoots)
{
    FDsonValidationResult Result;

    if (!FPaths::FileExists(FilePath))
    {
        Result.ErrorMessage = TEXT("File does not exist");
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonValidator: rejected — %s"), *Result.ErrorMessage);
        return Result;
    }

    UE_LOG(LogDsonImporter, Log,
        TEXT("DsonValidator: loading file: %s"), *FilePath);

    DsonDocumentHandle DocHandle = DsonDocument_Create();
    UE_LOG(LogDsonImporter, Log,
        TEXT("DsonValidator: document handle %s"),
        DocHandle ? TEXT("created successfully") : TEXT("FAILED — null handle"));

    if (!DocHandle)
    {
        Result.ErrorMessage = FString::Printf(TEXT("Failed to load file: %s"), *FilePath);
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonValidator: rejected — %s"), *Result.ErrorMessage);
        return Result;
    }

    const int LoadResult = DsonDocument_LoadFromFile(DocHandle, TCHAR_TO_UTF8(*FilePath));
    UE_LOG(LogDsonImporter, Log,
        TEXT("DsonValidator: LoadFile returned %s"),
        LoadResult == 0 ? TEXT("success") : TEXT("failure"));

    if (LoadResult != 0)
    {
        const char* LastError = DsonParser_GetLastError();
        Result.ErrorMessage = FString::Printf(TEXT("Failed to load DSON file: %s"),
            LastError ? UTF8_TO_TCHAR(LastError) : TEXT("unknown error"));
        DsonDocument_Destroy(DocHandle);
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonValidator: rejected — %s"), *Result.ErrorMessage);
        return Result;
    }

    const char* AssetTypeStr = DsonDocument_GetAssetType(DocHandle);
    UE_LOG(LogDsonImporter, Log,
        TEXT("DsonValidator: asset type = '%s'"),
        AssetTypeStr ? UTF8_TO_TCHAR(AssetTypeStr) : TEXT("(null)"));

    Result.AssetType = ParseAssetType(AssetTypeStr ? AssetTypeStr : "");

    if (Result.AssetType == EDsonAssetType::Unsupported ||
        Result.AssetType == EDsonAssetType::Unknown)
    {
        Result.ErrorMessage = FString::Printf(
            TEXT("Unsupported asset type: '%s'. Expected 'figure' or 'character'."),
            AssetTypeStr ? UTF8_TO_TCHAR(AssetTypeStr) : TEXT("(none)"));
        DsonDocument_Destroy(DocHandle);
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonValidator: rejected — %s"), *Result.ErrorMessage);
        return Result;
    }

    const char* AssetId = DsonDocument_GetAssetId(DocHandle);
    UE_LOG(LogDsonImporter, Log,
        TEXT("DsonValidator: asset id = '%s'"),
        AssetId ? UTF8_TO_TCHAR(AssetId) : TEXT("(null)"));

    // Note: DsonDocument_GetAssetName does not exist in the C API —
    // generation is detected from the asset ID alone.
    Result.Generation = DetectGeneration(AssetId ? AssetId : "");
    UE_LOG(LogDsonImporter, Log,
        TEXT("DsonValidator: generation detected = %s"), *Result.GetGenerationString());

    ResolveDependencies(DocHandle, Result.AssetType, ContentRoots, Result.Dependencies);

    DsonDocument_Destroy(DocHandle);

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

    const FString Id = UTF8_TO_TCHAR(AssetId);

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
    // Figure files are self-contained — geometry lives in the file itself
    if (AssetType == EDsonAssetType::Figure)
        return;

    // Character / scene files reference external figure DSFs via scene node geometry URLs
    TSet<FString> SeenUrls;
    const int SceneNodeCount = DsonDocument_GetSceneNodeCount(DocHandle);

    for (int i = 0; i < SceneNodeCount; ++i)
    {
        const int GeomCount = DsonDocument_GetSceneNodeGeometryCount(DocHandle, i);
        for (int j = 0; j < GeomCount; ++j)
        {
            const char* RawUrl = DsonDocument_GetSceneNodeGeometryUrl(DocHandle, i, j);
            if (!RawUrl || RawUrl[0] == '\0')
                continue;

            const FString UrlStr = UTF8_TO_TCHAR(RawUrl);

            // Use the path portion (before #) as the deduplication key
            FString UrlKey = UrlStr;
            int32 HashIdx = INDEX_NONE;
            if (UrlKey.FindChar(TEXT('#'), HashIdx))
                UrlKey = UrlKey.Left(HashIdx);

            if (SeenUrls.Contains(UrlKey))
                continue;
            SeenUrls.Add(UrlKey);

            FDsonDependency Dep;
            Dep.Url = UrlStr;
            UE_LOG(LogDsonImporter, Log,
                TEXT("DsonValidator: dependency URL: %s"), *Dep.Url);

            Dep.ResolvedPath = FDsonContentRoots::ResolveUrl(UrlStr, ContentRoots);
            Dep.bResolved = !Dep.ResolvedPath.IsEmpty();

            if (Dep.bResolved)
            {
                UE_LOG(LogDsonImporter, Log,
                    TEXT("DsonValidator: resolved to: %s"), *Dep.ResolvedPath);
            }
            else
            {
                UE_LOG(LogDsonImporter, Warning,
                    TEXT("DsonValidator: UNRESOLVED dependency: %s"), *Dep.Url);
            }

            OutDependencies.Add(Dep);
        }
    }
}