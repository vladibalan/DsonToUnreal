#include "DsonMaterialDiagnostic.h"
#include "DsonValidator.h"      // GenerationToString
#include "DsonImporter.h"
#include "DsonParserFunctions.h"
#include "DsonImportUtils.h"
#include "DsonLoadedDocument.h"
#include "DsonTextureImporter.h"

/*
 * Intent:
 * - Dump verbose material/channel information from DSON documents for debugging.
 * - Exercise texture resolution/import code without changing the main material builder.
 *
 * Read this file for diagnostic output shape or temporary investigation helpers.
 */

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

using DsonImportUtils::FromUtf8;

static bool IsColorChannel(const FString& ChannelId)
{
    // Mirrors the sRGB policy in the material builder/texture importer path.
    // Known sRGB color channels per Material Masters v1 spec.
    // Everything else (including unknown ids) defaults to linear/data.
    static const TSet<FString> ColorIds = {
        TEXT("diffuse"),
        TEXT("Translucency Color"),
        TEXT("Top Coat Color"),
        TEXT("Makeup Base Color"),
    };
    return ColorIds.Contains(ChannelId);
}

struct FSceneMaterialChannelDiagnostic
{
    FString Id;
    FString Type;
    double Value = 0.0;
    bool bHasColor = false;
    double ColorR = 0.0;
    double ColorG = 0.0;
    double ColorB = 0.0;
    FString ImageUrl;
    FString TexturePath;
};

static FSceneMaterialChannelDiagnostic ReadSceneMaterialChannelDiagnostic(
    uint64_t DsonHandle,
    int32 SceneMatIdx,
    int32 ChannelIdx)
{
    FSceneMaterialChannelDiagnostic Channel;
    Channel.Id = FromUtf8(GDsonParser.GetSceneMaterialChannelId
        ? GDsonParser.GetSceneMaterialChannelId(DsonHandle, SceneMatIdx, ChannelIdx) : nullptr);
    Channel.Type = FromUtf8(GDsonParser.GetSceneMaterialChannelType
        ? GDsonParser.GetSceneMaterialChannelType(DsonHandle, SceneMatIdx, ChannelIdx) : nullptr);
    Channel.Value = GDsonParser.GetSceneMaterialChannelValue
        ? GDsonParser.GetSceneMaterialChannelValue(DsonHandle, SceneMatIdx, ChannelIdx) : 0.0;
    Channel.bHasColor = GDsonParser.GetSceneMaterialChannelHasColor
        ? GDsonParser.GetSceneMaterialChannelHasColor(DsonHandle, SceneMatIdx, ChannelIdx) : false;
    Channel.ColorR = GDsonParser.GetSceneMaterialChannelColorR
        ? GDsonParser.GetSceneMaterialChannelColorR(DsonHandle, SceneMatIdx, ChannelIdx) : 0.0;
    Channel.ColorG = GDsonParser.GetSceneMaterialChannelColorG
        ? GDsonParser.GetSceneMaterialChannelColorG(DsonHandle, SceneMatIdx, ChannelIdx) : 0.0;
    Channel.ColorB = GDsonParser.GetSceneMaterialChannelColorB
        ? GDsonParser.GetSceneMaterialChannelColorB(DsonHandle, SceneMatIdx, ChannelIdx) : 0.0;
    Channel.ImageUrl = FromUtf8(GDsonParser.GetSceneMaterialChannelImageUrl
        ? GDsonParser.GetSceneMaterialChannelImageUrl(DsonHandle, SceneMatIdx, ChannelIdx) : nullptr);
    Channel.TexturePath = FromUtf8(GDsonParser.GetSceneMaterialChannelTexturePath
        ? GDsonParser.GetSceneMaterialChannelTexturePath(DsonHandle, SceneMatIdx, ChannelIdx) : nullptr);
    return Channel;
}

static TArray<FString> ReadLibraryMaterialGroups(uint64_t DsonHandle, int32 MaterialIdx)
{
    TArray<FString> Groups;

    const int32 GroupCount = GDsonParser.GetMaterialGroupCount
        ? GDsonParser.GetMaterialGroupCount(DsonHandle, MaterialIdx) : 0;
    Groups.Reserve(GroupCount);

    for (int32 g = 0; g < GroupCount; ++g)
    {
        Groups.Add(FromUtf8(GDsonParser.GetMaterialGroupName
            ? GDsonParser.GetMaterialGroupName(DsonHandle, MaterialIdx, g) : nullptr));
    }

    return Groups;
}

static TArray<FString> ReadSceneMaterialGroups(uint64_t DsonHandle, int32 SceneMatIdx)
{
    TArray<FString> Groups;

    const int32 GroupCount = GDsonParser.GetSceneMaterialGroupCount
        ? GDsonParser.GetSceneMaterialGroupCount(DsonHandle, SceneMatIdx) : 0;
    Groups.Reserve(GroupCount);

    for (int32 g = 0; g < GroupCount; ++g)
    {
        Groups.Add(FromUtf8(GDsonParser.GetSceneMaterialGroupName
            ? GDsonParser.GetSceneMaterialGroupName(DsonHandle, SceneMatIdx, g) : nullptr));
    }

    return Groups;
}

// ---------------------------------------------------------------------------
// DumpOneFile
// ---------------------------------------------------------------------------

static void DumpOneFile(const FString& FilePath, const FDsonImportSettings& Settings,
    FDsonTextureImporter& Importer)
{
    // Dumps one DSON file without mutating main import state. This is intentionally
    // verbose evidence collection for material audits.
    if (FilePath.IsEmpty())
    {
        UE_LOG(LogDsonImporter, Warning, TEXT("[MatDiag] Skipping empty file path"));
        return;
    }

    FDsonLoadedDocument Document;
    if (!Document.LoadFromFileAsWarning(FilePath, TEXT("[MatDiag]")))
        return;

    // H is used for all uint64_t-based API calls; Handle for DsonDocumentHandle-based calls
    DsonDocumentHandle Handle = Document.GetHandle();
    const uint64_t H = reinterpret_cast<uint64_t>(Handle);

    // Header
    FString AssetTypeStr = FromUtf8(GDsonParser.GetAssetType ? GDsonParser.GetAssetType(Handle) : nullptr);

    UE_LOG(LogDsonImporter, Log, TEXT("=== DSON Material Diagnostic ==="));
    UE_LOG(LogDsonImporter, Log, TEXT("File: %s"), *FilePath);
    UE_LOG(LogDsonImporter, Log, TEXT("Asset type: %s"), *AssetTypeStr);
    UE_LOG(LogDsonImporter, Log, TEXT("Generation: %s"), *GenerationToString(Settings.Generation));

    // Library Materials - compact: one line per channel.
    const int32 MatCount = GDsonParser.GetMaterialCount ? GDsonParser.GetMaterialCount(H) : 0;
    UE_LOG(LogDsonImporter, Log, TEXT(""));
    UE_LOG(LogDsonImporter, Log, TEXT("--- Library Materials (%d) ---"), MatCount);

    for (int32 i = 0; i < MatCount; ++i)
    {
        // Each const char* is converted to FString before the next parser call
        FString MatId      = FromUtf8(GDsonParser.GetMaterialId         ? GDsonParser.GetMaterialId(H, i)         : nullptr);
        FString MatName    = FromUtf8(GDsonParser.GetMaterialName       ? GDsonParser.GetMaterialName(H, i)       : nullptr);
        FString MatType    = FromUtf8(GDsonParser.GetMaterialType       ? GDsonParser.GetMaterialType(H, i)       : nullptr);
        FString ShaderType = FromUtf8(GDsonParser.GetMaterialShaderType ? GDsonParser.GetMaterialShaderType(H, i) : nullptr);
        FString GeomId     = FromUtf8(GDsonParser.GetMaterialGeometryId ? GDsonParser.GetMaterialGeometryId(H, i) : nullptr);
        FString UVSetId    = FromUtf8(GDsonParser.GetMaterialUVSetId    ? GDsonParser.GetMaterialUVSetId(H, i)    : nullptr);

        UE_LOG(LogDsonImporter, Log, TEXT("[%d] id=\"%s\" name=\"%s\""), i, *MatId, *MatName);
        UE_LOG(LogDsonImporter, Log, TEXT("    type=\"%s\"  shader_type=\"%s\""), *MatType, *ShaderType);
        UE_LOG(LogDsonImporter, Log, TEXT("    geometry=\"%s\"  uv_set=\"%s\""), *GeomId, *UVSetId);

        const TArray<FString> Groups = ReadLibraryMaterialGroups(H, i);
        UE_LOG(LogDsonImporter, Log, TEXT("    groups: [%s]"), *FString::Join(Groups, TEXT(", ")));

        const int32 ChCount = GDsonParser.GetMaterialChannelCount
            ? GDsonParser.GetMaterialChannelCount(H, i) : 0;
        UE_LOG(LogDsonImporter, Log, TEXT("    Channels (%d):"), ChCount);
        for (int32 c = 0; c < ChCount; ++c)
        {
            FString ChId   = FromUtf8(GDsonParser.GetMaterialChannelId   ? GDsonParser.GetMaterialChannelId(H, i, c)   : nullptr);
            FString ChType = FromUtf8(GDsonParser.GetMaterialChannelType ? GDsonParser.GetMaterialChannelType(H, i, c) : nullptr);
            UE_LOG(LogDsonImporter, Log, TEXT("      [%d] id=\"%s\" type=\"%s\""), c, *ChId, *ChType);
        }
    }

    // Scene Materials - full per-field breakdown per channel.
    const int32 SceneMatCount = GDsonParser.GetSceneMaterialCount
        ? GDsonParser.GetSceneMaterialCount(H) : 0;
    UE_LOG(LogDsonImporter, Log, TEXT(""));
    UE_LOG(LogDsonImporter, Log, TEXT("--- Scene Materials (%d) ---"), SceneMatCount);

    for (int32 i = 0; i < SceneMatCount; ++i)
    {
        FString SceneId      = FromUtf8(GDsonParser.GetSceneMaterialId         ? GDsonParser.GetSceneMaterialId(H, i)         : nullptr);
        FString SceneName    = FromUtf8(GDsonParser.GetSceneMaterialName       ? GDsonParser.GetSceneMaterialName(H, i)       : nullptr);
        FString SceneType    = FromUtf8(GDsonParser.GetSceneMaterialType       ? GDsonParser.GetSceneMaterialType(H, i)       : nullptr);
        FString SceneShader  = FromUtf8(GDsonParser.GetSceneMaterialShaderType ? GDsonParser.GetSceneMaterialShaderType(H, i) : nullptr);
        FString SceneGeomId  = FromUtf8(GDsonParser.GetSceneMaterialGeometryId ? GDsonParser.GetSceneMaterialGeometryId(H, i) : nullptr);
        FString SceneUVSetId = FromUtf8(GDsonParser.GetSceneMaterialUVSetId    ? GDsonParser.GetSceneMaterialUVSetId(H, i)    : nullptr);
        FString SceneUrl     = FromUtf8(GDsonParser.GetSceneMaterialUrl        ? GDsonParser.GetSceneMaterialUrl(H, i)        : nullptr);

        UE_LOG(LogDsonImporter, Log, TEXT("[%d] id=\"%s\" name=\"%s\""), i, *SceneId, *SceneName);
        UE_LOG(LogDsonImporter, Log, TEXT("    type=\"%s\"  shader_type=\"%s\""), *SceneType, *SceneShader);
        UE_LOG(LogDsonImporter, Log, TEXT("    geometry=\"%s\"  uv_set=\"%s\""), *SceneGeomId, *SceneUVSetId);

        const TArray<FString> SGroups = ReadSceneMaterialGroups(H, i);
        UE_LOG(LogDsonImporter, Log, TEXT("    groups: [%s]"), *FString::Join(SGroups, TEXT(", ")));
        UE_LOG(LogDsonImporter, Log, TEXT("    url=\"%s\""), *SceneUrl);

        const int32 ChCount = GDsonParser.GetSceneMaterialChannelCount
            ? GDsonParser.GetSceneMaterialChannelCount(H, i) : 0;
        UE_LOG(LogDsonImporter, Log, TEXT("    Channels (%d):"), ChCount);
        for (int32 c = 0; c < ChCount; ++c)
        {
            const FSceneMaterialChannelDiagnostic Channel =
                ReadSceneMaterialChannelDiagnostic(H, i, c);

            UE_LOG(LogDsonImporter, Log, TEXT("      [%d] id=\"%s\" type=\"%s\""), c, *Channel.Id, *Channel.Type);
            UE_LOG(LogDsonImporter, Log, TEXT("        value=%f"), Channel.Value);
            UE_LOG(LogDsonImporter, Log, TEXT("        hasColor=%s  rgb=(%f, %f, %f)"),
                Channel.bHasColor ? TEXT("true") : TEXT("false"),
                Channel.ColorR, Channel.ColorG, Channel.ColorB);
            UE_LOG(LogDsonImporter, Log, TEXT("        imageUrl=\"%s\""), *Channel.ImageUrl);
            UE_LOG(LogDsonImporter, Log, TEXT("        texturePath=\"%s\""), *Channel.TexturePath);
            if (!Channel.ImageUrl.IsEmpty())
            {
                const bool bSRGB = IsColorChannel(Channel.Id);
                UTexture2D* Tex = Importer.ImportOrFind(Channel.ImageUrl, bSRGB);
                UE_LOG(LogDsonImporter, Log,
                    TEXT("    [import] %s -> %s (sRGB=%d) %s"),
                    *Channel.ImageUrl,
                    Tex ? *Tex->GetPathName() : TEXT("<failed>"),
                    static_cast<int32>(bSRGB),
                    Tex ? TEXT("OK") : TEXT("FAIL"));
            }
        }

    }
}

// ---------------------------------------------------------------------------
// FDsonMaterialDiagnostic::Dump
// ---------------------------------------------------------------------------

void FDsonMaterialDiagnostic::Dump(const FDsonImportSettings& Settings,
    FDsonTextureImporter& Importer)
{
    if (!GDsonParser.IsValid())
    {
        UE_LOG(LogDsonImporter, Warning, TEXT("[MatDiag] Parser not ready - skipping diagnostic"));
        return;
    }

    DumpOneFile(Settings.DsonFilePath, Settings, Importer);

    if (!Settings.ResolvedFigureDsfPath.IsEmpty())
        DumpOneFile(Settings.ResolvedFigureDsfPath, Settings, Importer);
}
