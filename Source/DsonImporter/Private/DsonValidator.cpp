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

    if (Result.AssetType == EDsonAssetType::Character)
        DiscoverCompanionFigures(DocHandle, ContentRoots, Result.CompanionFigures);

    Result.bIsValid = true;
    return Result;
}

FDsonImportSettings FDsonValidator::ToImportSettings(
    const FString& SourceAssetPath,
    const FDsonValidationResult& Validation,
    bool bDumpMaterialDiagnostics)
{
    FDsonImportSettings Settings;
    Settings.DsonFilePath = SourceAssetPath;
    Settings.Generation = Validation.Generation;
    Settings.bDumpMaterialDiagnostics = bDumpMaterialDiagnostics;
    Settings.CompanionFigures = Validation.CompanionFigures;

    for (const FDsonDependency& Dep : Validation.Dependencies)
    {
        if (Dep.bResolved)
        {
            Settings.ResolvedFigureDsfPath = Dep.ResolvedPath;
            break;
        }
    }

    return Settings;
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

void FDsonValidator::DiscoverCompanionFigures(
    void* DocHandle,
    const TArray<FString>& ContentRoots,
    TArray<FDsonCompanionSource>& OutCompanions)
{
    // All 5 PostLoadAddon exports are optional (DsonParser >= 1.1.0). Bail silently on
    // a pre-1.1.0 DLL so older parsers degrade gracefully (R7).
    if (!GDsonParser.GetScenePostLoadAddonCount
        || !GDsonParser.GetScenePostLoadAddonSlot
        || !GDsonParser.GetScenePostLoadAddonAssetName
        || !GDsonParser.GetScenePostLoadAddonAssetFile
        || !GDsonParser.GetScenePostLoadAddonMatPreset)
    {
        return;
    }

    const int AddonCount = GDsonParser.GetScenePostLoadAddonCount(DocHandle);
    if (AddonCount <= 0)
        return;

    UE_LOG(LogDsonImporter, Log, TEXT("DsonValidator: found %d PostLoadAddon slot(s)"), AddonCount);

    for (int i = 0; i < AddonCount; ++i)
    {
        // Copy all parser strings before the next parser call (R3).
        const FString Slot         = DsonImportUtils::FromUtf8(GDsonParser.GetScenePostLoadAddonSlot(DocHandle, i));
        const FString AssetName    = DsonImportUtils::FromUtf8(GDsonParser.GetScenePostLoadAddonAssetName(DocHandle, i));
        const FString AssetFile    = DsonImportUtils::FromUtf8(GDsonParser.GetScenePostLoadAddonAssetFile(DocHandle, i));
        const FString MatPresetRaw = DsonImportUtils::FromUtf8(GDsonParser.GetScenePostLoadAddonMatPreset(DocHandle, i));

        if (AssetFile.IsEmpty())
        {
            UE_LOG(LogDsonImporter, Warning,
                TEXT("DsonValidator: companion slot '%s' has empty AssetFile — skipping"), *Slot);
            continue;
        }

        // Resolve the loader .duf to an absolute disk path.
        const FString LoaderDufPath = FDsonContentRoots::ResolveUrl(AssetFile, ContentRoots);
        if (LoaderDufPath.IsEmpty())
        {
            UE_LOG(LogDsonImporter, Warning,
                TEXT("DsonValidator: companion '%s' (slot '%s') AssetFile not found — skipping"),
                *AssetName, *Slot);
            continue;
        }

        // Load the loader .duf as its own RAII document (R3).
        FDsonLoadedDocument LoaderDoc;
        if (!LoaderDoc.LoadFromFileAsWarning(LoaderDufPath, TEXT("DsonValidator[companion]")))
        {
            UE_LOG(LogDsonImporter, Warning,
                TEXT("DsonValidator: failed to load companion loader '%s' — skipping"), *AssetName);
            continue;
        }

        const DsonDocumentHandle LoaderHandle   = LoaderDoc.GetHandle();
        const uint64_t           LoaderHandle64 = LoaderDoc.GetHandle64();

        // Extract geometry DSF URL + node id from loader scene nodes — same pattern as the
        // body figure in ResolveDependencies (GetSceneNodeCount / GetSceneNodeGeometryUrl).
        FString GeometryDsfRawUrl;
        FString NodeId;

        const int SceneNodeCount = GDsonParser.GetSceneNodeCount(LoaderHandle);
        for (int ni = 0; ni < SceneNodeCount && GeometryDsfRawUrl.IsEmpty(); ++ni)
        {
            const int GeomCount = GDsonParser.GetSceneNodeGeometryCount(LoaderHandle, ni);
            for (int gi = 0; gi < GeomCount; ++gi)
            {
                const char* RawUrl = GDsonParser.GetSceneNodeGeometryUrl(LoaderHandle, ni, gi);
                if (!RawUrl || RawUrl[0] == '\0')
                    continue;

                const FString UrlStr = DsonImportUtils::FromUtf8(RawUrl); // copy before next call (R3)

                // Fragment (e.g. "#Genesis9Eyes-1") is the geometry node id; strip for disk path.
                GeometryDsfRawUrl = DsonImportUtils::StripUrlFragment(UrlStr);
                int32 HashIdx = INDEX_NONE;
                if (UrlStr.FindChar(TEXT('#'), HashIdx))
                {
                    NodeId = UrlStr.Mid(HashIdx + 1);
                    // Strip any trailing ?query from the node id (formula URLs use #id?value).
                    int32 QueryIdx = INDEX_NONE;
                    if (NodeId.FindChar(TEXT('?'), QueryIdx))
                        NodeId = NodeId.Left(QueryIdx);
                }
                break;
            }
        }

        if (GeometryDsfRawUrl.IsEmpty())
        {
            UE_LOG(LogDsonImporter, Warning,
                TEXT("DsonValidator: companion '%s' loader has no scene geometry — skipping"),
                *AssetName);
            continue;
        }

        // Resolve geometry DSF URL to an absolute disk path (ResolveUrl handles scheme/fragment).
        const FString ResolvedGeomDsfPath = FDsonContentRoots::ResolveUrl(GeometryDsfRawUrl, ContentRoots);
        if (ResolvedGeomDsfPath.IsEmpty())
        {
            UE_LOG(LogDsonImporter, Warning,
                TEXT("DsonValidator: companion '%s' geometry DSF not resolved — skipping"),
                *AssetName);
            continue;
        }

        // Resolve mat preset (may be empty = none; permissive — proceed even if absent).
        FString ResolvedMatPreset;
        if (!MatPresetRaw.IsEmpty())
            ResolvedMatPreset = FDsonContentRoots::ResolveUrl(MatPresetRaw, ContentRoots);

        // Stash resolved companion source.
        FDsonCompanionSource Companion;
        Companion.Slot           = Slot;
        Companion.AssetName      = AssetName;
        Companion.GeometryDsfUrl = ResolvedGeomDsfPath;
        Companion.NodeId         = NodeId;
        Companion.MatPresetPath  = ResolvedMatPreset;
        OutCompanions.Add(MoveTemp(Companion));

        UE_LOG(LogDsonImporter, Log,
            TEXT("DsonValidator: companion resolved — slot='%s' asset='%s' node='%s' matPreset='%s'"),
            *Slot, *AssetName, *NodeId,
            ResolvedMatPreset.IsEmpty() ? TEXT("(none)") : *ResolvedMatPreset);
        UE_LOG(LogDsonImporter, Log,
            TEXT("  GeomDSF: %s"), *ResolvedGeomDsfPath);

        // Log library nodes from the loader doc (informs Slice B skeleton-binding analysis).
        if (GDsonParser.GetNodeCount && GDsonParser.GetNodeName)
        {
            const int32 NodeCount = GDsonParser.GetNodeCount(LoaderHandle64);
            TArray<FString> NodeNames;
            NodeNames.Reserve(NodeCount);
            for (int32 ni = 0; ni < NodeCount; ++ni)
            {
                const char* RawName = GDsonParser.GetNodeName(LoaderHandle64, ni);
                if (RawName && RawName[0] != '\0')
                    NodeNames.Add(DsonImportUtils::FromUtf8(RawName));
            }
            UE_LOG(LogDsonImporter, Log,
                TEXT("  Loader nodes (%d): %s"),
                NodeNames.Num(),
                NodeNames.IsEmpty() ? TEXT("(none)") : *FString::Join(NodeNames, TEXT(", ")));
        }
    }
}
