#include "DsonMaterialDiagnostic.h"
#include "SDsonImportWindow.h"
#include "DsonImporter.h"
#include "DsonParserFunctions.h"
#include "Misc/FileHelper.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static const TCHAR* MatChannelLabel(int32 Ch)
{
    static const TCHAR* Labels[8] = {
        TEXT("diffuse"), TEXT("specular"), TEXT("roughness"), TEXT("normal"),
        TEXT("opacity"), TEXT("subsurface"), TEXT("emission"), TEXT("bump")
    };
    return (Ch >= 0 && Ch < 8) ? Labels[Ch] : TEXT("?");
}

static FString GenString(EGenesisGeneration Gen)
{
    switch (Gen)
    {
        case EGenesisGeneration::Genesis3: return TEXT("Genesis 3");
        case EGenesisGeneration::Genesis8: return TEXT("Genesis 8");
        case EGenesisGeneration::Genesis9: return TEXT("Genesis 9");
        default:                           return TEXT("Unknown");
    }
}

// Converts a nullable const char* (already retrieved from the parser) to FString.
// The caller must not make further parser calls between obtaining the raw pointer
// and passing it here — see the "immediate conversion" rule in the hard rules.
static FString S(const char* Raw)
{
    return Raw ? FString(UTF8_TO_TCHAR(Raw)) : TEXT("");
}

// ---------------------------------------------------------------------------
// DumpOneFile
// ---------------------------------------------------------------------------

static void DumpOneFile(const FString& FilePath, const FDsonImportSettings& Settings)
{
    if (FilePath.IsEmpty())
    {
        UE_LOG(LogDsonImporter, Warning, TEXT("[MatDiag] Skipping empty file path"));
        return;
    }

    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *FilePath))
    {
        UE_LOG(LogDsonImporter, Warning, TEXT("[MatDiag] Failed to read: %s"), *FilePath);
        return;
    }

    FTCHARToUTF8 Utf8(*FileContent);

    DsonDocumentHandle Handle = GDsonParser.Create();
    if (!Handle)
    {
        UE_LOG(LogDsonImporter, Warning,
            TEXT("[MatDiag] GDsonParser.Create() returned null for: %s"), *FilePath);
        return;
    }

    const int32 LoadResult = GDsonParser.LoadFromString(Handle, Utf8.Get());
    if (LoadResult != 0)
    {
        const char* ErrRaw = GDsonParser.GetLastError ? GDsonParser.GetLastError() : nullptr;
        UE_LOG(LogDsonImporter, Warning,
            TEXT("[MatDiag] LoadFromString failed for '%s': %s"),
            *FilePath, ErrRaw ? UTF8_TO_TCHAR(ErrRaw) : TEXT("unknown error"));
        GDsonParser.Destroy(Handle);
        return;
    }

    // H is used for all uint64_t-based API calls; Handle for DsonDocumentHandle-based calls
    const uint64_t H = reinterpret_cast<uint64_t>(Handle);

    // Header
    FString AssetTypeStr = S(GDsonParser.GetAssetType ? GDsonParser.GetAssetType(Handle) : nullptr);

    UE_LOG(LogDsonImporter, Log, TEXT("=== DSON Material Diagnostic ==="));
    UE_LOG(LogDsonImporter, Log, TEXT("File: %s"), *FilePath);
    UE_LOG(LogDsonImporter, Log, TEXT("Asset type: %s"), *AssetTypeStr);
    UE_LOG(LogDsonImporter, Log, TEXT("Generation: %s"), *GenString(Settings.Generation));

    // ── Library Materials ────────────────────────────────────────────────────
    const int32 MatCount = GDsonParser.GetMaterialCount ? GDsonParser.GetMaterialCount(H) : 0;
    UE_LOG(LogDsonImporter, Log, TEXT(""));
    UE_LOG(LogDsonImporter, Log, TEXT("--- Library Materials (%d) ---"), MatCount);

    for (int32 i = 0; i < MatCount; ++i)
    {
        // Each const char* is converted to FString before the next parser call
        FString MatId      = S(GDsonParser.GetMaterialId         ? GDsonParser.GetMaterialId(H, i)         : nullptr);
        FString MatName    = S(GDsonParser.GetMaterialName       ? GDsonParser.GetMaterialName(H, i)       : nullptr);
        FString MatType    = S(GDsonParser.GetMaterialType       ? GDsonParser.GetMaterialType(H, i)       : nullptr);
        FString ShaderType = S(GDsonParser.GetMaterialShaderType ? GDsonParser.GetMaterialShaderType(H, i) : nullptr);
        FString GeomId     = S(GDsonParser.GetMaterialGeometryId ? GDsonParser.GetMaterialGeometryId(H, i) : nullptr);
        FString UVSetId    = S(GDsonParser.GetMaterialUVSetId    ? GDsonParser.GetMaterialUVSetId(H, i)    : nullptr);

        UE_LOG(LogDsonImporter, Log, TEXT("[%d] id=\"%s\" name=\"%s\""), i, *MatId, *MatName);
        UE_LOG(LogDsonImporter, Log, TEXT("    type=\"%s\"  shader_type=\"%s\""), *MatType, *ShaderType);
        UE_LOG(LogDsonImporter, Log, TEXT("    geometry=\"%s\"  uv_set=\"%s\""), *GeomId, *UVSetId);

        // Groups
        const int32 GroupCount = GDsonParser.GetMaterialGroupCount
            ? GDsonParser.GetMaterialGroupCount(H, i) : 0;
        TArray<FString> Groups;
        for (int32 g = 0; g < GroupCount; ++g)
        {
            Groups.Add(S(GDsonParser.GetMaterialGroupName
                ? GDsonParser.GetMaterialGroupName(H, i, g) : nullptr));
        }
        UE_LOG(LogDsonImporter, Log, TEXT("    groups: [%s]"), *FString::Join(Groups, TEXT(", ")));

        // 8 channels
        for (int32 Ch = 0; Ch < 8; ++Ch)
        {
            const double Val     = GDsonParser.GetMaterialChannelValue
                ? GDsonParser.GetMaterialChannelValue(H, i, Ch) : 0.0;
            const bool bHasColor = GDsonParser.GetMaterialChannelHasColor
                ? GDsonParser.GetMaterialChannelHasColor(H, i, Ch) : false;
            const double CR      = GDsonParser.GetMaterialChannelColorR
                ? GDsonParser.GetMaterialChannelColorR(H, i, Ch) : 0.0;
            const double CG      = GDsonParser.GetMaterialChannelColorG
                ? GDsonParser.GetMaterialChannelColorG(H, i, Ch) : 0.0;
            const double CB      = GDsonParser.GetMaterialChannelColorB
                ? GDsonParser.GetMaterialChannelColorB(H, i, Ch) : 0.0;
            FString ImgUrl  = S(GDsonParser.GetMaterialChannelImageUrl
                ? GDsonParser.GetMaterialChannelImageUrl(H, i, Ch) : nullptr);
            FString TexPath = S(GDsonParser.GetMaterialChannelTexturePath
                ? GDsonParser.GetMaterialChannelTexturePath(H, i, Ch) : nullptr);

            UE_LOG(LogDsonImporter, Log, TEXT("    Channel %d (%s):"), Ch, MatChannelLabel(Ch));
            UE_LOG(LogDsonImporter, Log, TEXT("      value=%f"), Val);
            UE_LOG(LogDsonImporter, Log, TEXT("      hasColor=%s  rgb=(%f, %f, %f)"),
                bHasColor ? TEXT("true") : TEXT("false"), CR, CG, CB);
            UE_LOG(LogDsonImporter, Log, TEXT("      imageUrl=\"%s\""), *ImgUrl);
            UE_LOG(LogDsonImporter, Log, TEXT("      texturePath=\"%s\""), *TexPath);
        }
    }

    // ── Scene Materials ──────────────────────────────────────────────────────
    const int32 SceneMatCount = GDsonParser.GetSceneMaterialCount
        ? GDsonParser.GetSceneMaterialCount(H) : 0;
    UE_LOG(LogDsonImporter, Log, TEXT(""));
    UE_LOG(LogDsonImporter, Log, TEXT("--- Scene Materials (%d) ---"), SceneMatCount);

    for (int32 i = 0; i < SceneMatCount; ++i)
    {
        FString SceneId  = S(GDsonParser.GetSceneMaterialId  ? GDsonParser.GetSceneMaterialId(H, i)  : nullptr);
        FString SceneUrl = S(GDsonParser.GetSceneMaterialUrl ? GDsonParser.GetSceneMaterialUrl(H, i) : nullptr);

        UE_LOG(LogDsonImporter, Log, TEXT("[%d] id=\"%s\""), i, *SceneId);
        UE_LOG(LogDsonImporter, Log, TEXT("    url=\"%s\""), *SceneUrl);

        const int32 SGCount = GDsonParser.GetSceneMaterialGroupCount
            ? GDsonParser.GetSceneMaterialGroupCount(H, i) : 0;
        TArray<FString> SGroups;
        for (int32 g = 0; g < SGCount; ++g)
        {
            SGroups.Add(S(GDsonParser.GetSceneMaterialGroupName
                ? GDsonParser.GetSceneMaterialGroupName(H, i, g) : nullptr));
        }
        UE_LOG(LogDsonImporter, Log, TEXT("    groups: [%s]"), *FString::Join(SGroups, TEXT(", ")));
    }

    GDsonParser.Destroy(Handle);
}

// ---------------------------------------------------------------------------
// FDsonMaterialDiagnostic::Dump
// ---------------------------------------------------------------------------

void FDsonMaterialDiagnostic::Dump(const FDsonImportSettings& Settings)
{
    if (!GDsonParser.IsValid())
    {
        UE_LOG(LogDsonImporter, Warning, TEXT("[MatDiag] Parser not ready — skipping diagnostic"));
        return;
    }

    DumpOneFile(Settings.DsonFilePath, Settings);

    if (!Settings.ResolvedFigureDsfPath.IsEmpty())
        DumpOneFile(Settings.ResolvedFigureDsfPath, Settings);
}