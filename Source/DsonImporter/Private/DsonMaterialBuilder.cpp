#include "DsonMaterialBuilder.h"
#include "DsonImporter.h"
#include "DsonAssetUtils.h"
#include "DsonParserFunctions.h"
#include "DsonImportUtils.h"
#include "DsonLoadedDocument.h"
#include "DsonTextureImporter.h"

#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "ObjectTools.h"

/*
 * Intent:
 * - Convert DAZ scene material channels into Unreal material instances.
 * - Detect shader kind, choose the matching master material, and set scalar/vector/texture parameters.
 * - Build a material map keyed by polygon material group for FDsonMeshBuilder.
 *
 * Read this file for shader detection, DAZ-channel-to-UE-parameter mapping, and MIC save issues.
 * Cross-reference MaterialMastersV1.md before changing parameter names.
 */

// ---------------------------------------------------------------------------
// Master material asset paths
// ---------------------------------------------------------------------------

static const TCHAR* kIrayUberMasterPath = TEXT("/DsonToUnreal/Materials/M_DazIrayUber");
static const TCHAR* kPBRSkinMasterPath  = TEXT("/DsonToUnreal/Materials/M_DazPBRSkin");
static const TCHAR* kDefaultMasterPath  = TEXT("/DsonToUnreal/Materials/M_DazDefault");

// ---------------------------------------------------------------------------
// FDazParamBinding - file-private parameter mapping descriptor
// ---------------------------------------------------------------------------

struct FDazParamBinding
{
    FName ColorParam;    // SetVectorParameterValueEditorOnly; NAME_None if this channel has no color
    FName ScalarParam;   // SetScalarParameterValueEditorOnly; NAME_None if this channel has no scalar
    FName TextureParam;  // SetTextureParameterValueEditorOnly; NAME_None if no texture slot
    FName UseFlag;       // Scalar Use*Map flag set to 1.0f when a texture is applied; NAME_None if N/A
    bool  bSRGB;         // Passed to FDsonTextureImporter::ImportOrFind
};

struct FMaterialInstanceAssetContext
{
    UPackage* Package = nullptr;
    UMaterialInstanceConstant* MIC = nullptr;
    FDsonAssetPath AssetPath;

    bool IsValid() const { return Package && MIC; }
};

struct FSceneMaterialMetadata
{
    FString MatId;
    FString Url;
    FString ShaderType;
};

struct FDazChannelInfo
{
    FString TextureRef;
    float Value = 0.0f;
};

// ---------------------------------------------------------------------------
// Mapping tables - initialized once, returned by const reference
// ---------------------------------------------------------------------------

static const TMap<FString, FDazParamBinding>& GetIrayUberMapping()
{
    // DAZ channel id -> Unreal master parameter binding for legacy Iray Uber materials.
    // Keep these ids and parameter names synchronized with MaterialMastersV1.md.
    static const TMap<FString, FDazParamBinding> Map = []()
    {
        TMap<FString, FDazParamBinding> M;
        M.Add(TEXT("diffuse"),
            { FName(TEXT("DiffuseColor")),     NAME_None,                       FName(TEXT("DiffuseMap")),       FName(TEXT("UseDiffuseMap")),       true  });
        M.Add(TEXT("Translucency Color"),
            { FName(TEXT("TranslucencyColor")), NAME_None,                      FName(TEXT("TranslucencyMap")),  FName(TEXT("UseTranslucencyMap")),  true  });
        M.Add(TEXT("Translucency Weight"),
            { NAME_None,                        FName(TEXT("TranslucencyWeight")), NAME_None,                       NAME_None,                          false });
        M.Add(TEXT("Glossy Layered Weight"),
            { NAME_None,                        FName(TEXT("GlossyWeight")),    FName(TEXT("GlossyMap")),        FName(TEXT("UseGlossyMap")),        false });
        M.Add(TEXT("Top Coat Weight"),
            { NAME_None,                        FName(TEXT("TopCoatWeight")),   FName(TEXT("TopCoatMap")),       FName(TEXT("UseTopCoatMap")),       false });
        M.Add(TEXT("Top Coat Color"),
            { FName(TEXT("TopCoatColor")),      NAME_None,                      FName(TEXT("TopCoatColorMap")),  FName(TEXT("UseTopCoatColorMap")),  true  });
        return M;
    }();
    return Map;
}

static const TMap<FString, FDazParamBinding>& GetPBRSkinMapping()
{
    // DAZ channel id -> Unreal master parameter binding for PBRSkin materials.
    // Data maps are marked bSRGB=false so texture import treats them as linear data.
    static const TMap<FString, FDazParamBinding> Map = []()
    {
        TMap<FString, FDazParamBinding> M;
        M.Add(TEXT("diffuse"),
            { FName(TEXT("DiffuseColor")),       NAME_None,                              FName(TEXT("DiffuseMap")),           FName(TEXT("UseDiffuseMap")),           true  });
        M.Add(TEXT("Translucency Color"),
            { FName(TEXT("TranslucencyColor")),  NAME_None,                              FName(TEXT("TranslucencyMap")),      FName(TEXT("UseTranslucencyMap")),      true  });
        M.Add(TEXT("Translucency Weight"),
            { NAME_None,                         FName(TEXT("TranslucencyWeight")),      NAME_None,                           NAME_None,                              false });
        M.Add(TEXT("Specular Lobe 1 Roughness"),
            { NAME_None,                         FName(TEXT("SpecularRoughness")),       FName(TEXT("SpecularRoughnessMap")), FName(TEXT("UseSpecularRoughnessMap")), false });
        M.Add(TEXT("Specular Lobe 2 Roughness Mult"),
            { NAME_None,                         FName(TEXT("SpecularRoughnessMult")),   NAME_None,                           NAME_None,                              false });
        M.Add(TEXT("Dual Lobe Specular Weight"),
            { NAME_None,                         FName(TEXT("DualLobeWeight")),          FName(TEXT("DualLobeMap")),          FName(TEXT("UseDualLobeMap")),          false });
        M.Add(TEXT("Detail Normal Map"),
            { NAME_None,                         FName(TEXT("DetailNormalStrength")),    FName(TEXT("DetailNormalMap")),      FName(TEXT("UseDetailNormalMap")),      false });
        M.Add(TEXT("Ambient Occlusion Weight"),
            { NAME_None,                         FName(TEXT("AOWeight")),                FName(TEXT("AOMap")),                FName(TEXT("UseAOMap")),                false });
        M.Add(TEXT("Top Coat Weight"),
            { NAME_None,                         FName(TEXT("TopCoatWeight")),           NAME_None,                           NAME_None,                              false });
        M.Add(TEXT("Top Coat Roughness"),
            { NAME_None,                         FName(TEXT("TopCoatRoughness")),        NAME_None,                           NAME_None,                              false });
        M.Add(TEXT("Top Coat Bump Weight"),
            { NAME_None,                         FName(TEXT("TopCoatBumpWeight")),       NAME_None,                           NAME_None,                              false });
        return M;
    }();
    return Map;
}

static const TMap<FString, FDazParamBinding>* GetMappingForShader(EDazShaderKind Kind)
{
    switch (Kind)
    {
        case EDazShaderKind::IrayUber: return &GetIrayUberMapping();
        case EDazShaderKind::PBRSkin:  return &GetPBRSkinMapping();
        case EDazShaderKind::Default:  return nullptr;
    }

    return nullptr;
}

// ---------------------------------------------------------------------------
// Helper: nullable const char* -> FString (same pattern as the diagnostic)
// ---------------------------------------------------------------------------

// Short local alias for the shared nullable-utf8 -> FString helper (used heavily below).
static FString S(const char* Raw) { return DsonImportUtils::FromUtf8(Raw); }

static FSceneMaterialMetadata ReadSceneMaterialMetadata(uint64_t DsonHandle, int32 SceneMatIdx)
{
    FSceneMaterialMetadata Metadata;
    Metadata.MatId = S(GDsonParser.GetSceneMaterialId
        ? GDsonParser.GetSceneMaterialId(DsonHandle, SceneMatIdx) : nullptr);
    Metadata.Url = S(GDsonParser.GetSceneMaterialUrl
        ? GDsonParser.GetSceneMaterialUrl(DsonHandle, SceneMatIdx) : nullptr);
    Metadata.ShaderType = S(GDsonParser.GetSceneMaterialShaderType
        ? GDsonParser.GetSceneMaterialShaderType(DsonHandle, SceneMatIdx) : nullptr);
    return Metadata;
}

static FMaterialInstanceAssetContext CreateMaterialInstanceAsset(
    const FString& MatId,
    const FString& OutputFolder)
{
    FMaterialInstanceAssetContext AssetContext;

    const FString SanitizedId = ObjectTools::SanitizeObjectName(MatId);
    AssetContext.AssetPath.AssetName = TEXT("MI_") + SanitizedId;
    AssetContext.AssetPath.PackagePath = OutputFolder / AssetContext.AssetPath.AssetName;

    AssetContext.Package = FDsonAssetUtils::CreateLoadedPackage(
        AssetContext.AssetPath.PackagePath, TEXT("DsonMaterialBuilder"));
    if (!AssetContext.Package)
        return AssetContext;

    AssetContext.MIC = NewObject<UMaterialInstanceConstant>(
        AssetContext.Package, *AssetContext.AssetPath.AssetName, RF_Public | RF_Standalone);
    if (!AssetContext.MIC)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonMaterialBuilder: NewObject<UMaterialInstanceConstant> failed for '%s'"),
            *AssetContext.AssetPath.PackagePath);
    }

    return AssetContext;
}

static void ApplySceneMaterialChannel(
    uint64_t DsonHandle,
    int32 SceneMatIdx,
    int32 ChannelIdx,
    const FDazParamBinding& Binding,
    UMaterialInstanceConstant* MIC,
    FDsonTextureImporter& TextureImporter)
{
    const bool bHasColor = GDsonParser.GetSceneMaterialChannelHasColor
        ? GDsonParser.GetSceneMaterialChannelHasColor(DsonHandle, SceneMatIdx, ChannelIdx) : false;

    // Color or scalar: both get applied even when a texture is also present.
    // The master multiplies the constant by the texture sample when UseFlag=1.
    if (bHasColor && Binding.ColorParam != NAME_None)
    {
        const double R = GDsonParser.GetSceneMaterialChannelColorR
            ? GDsonParser.GetSceneMaterialChannelColorR(DsonHandle, SceneMatIdx, ChannelIdx) : 0.0;
        const double G = GDsonParser.GetSceneMaterialChannelColorG
            ? GDsonParser.GetSceneMaterialChannelColorG(DsonHandle, SceneMatIdx, ChannelIdx) : 0.0;
        const double B = GDsonParser.GetSceneMaterialChannelColorB
            ? GDsonParser.GetSceneMaterialChannelColorB(DsonHandle, SceneMatIdx, ChannelIdx) : 0.0;
        MIC->SetVectorParameterValueEditorOnly(
            FMaterialParameterInfo(Binding.ColorParam),
            FLinearColor(static_cast<float>(R), static_cast<float>(G), static_cast<float>(B), 1.0f));
    }
    else if (!bHasColor && Binding.ScalarParam != NAME_None)
    {
        const double Val = GDsonParser.GetSceneMaterialChannelValue
            ? GDsonParser.GetSceneMaterialChannelValue(DsonHandle, SceneMatIdx, ChannelIdx) : 0.0;
        MIC->SetScalarParameterValueEditorOnly(
            FMaterialParameterInfo(Binding.ScalarParam),
            static_cast<float>(Val));
    }

    // Texture + UseFlag: orthogonal to color/scalar, applied when an image path exists.
    if (Binding.TextureParam != NAME_None)
    {
        const FString TexturePath = S(GDsonParser.GetSceneMaterialChannelTexturePath
            ? GDsonParser.GetSceneMaterialChannelTexturePath(DsonHandle, SceneMatIdx, ChannelIdx) : nullptr);
        const FString ImgUrl = S(GDsonParser.GetSceneMaterialChannelImageUrl
            ? GDsonParser.GetSceneMaterialChannelImageUrl(DsonHandle, SceneMatIdx, ChannelIdx) : nullptr);

        // texture_path is the parser's resolved image_library link (e.g. layered/LIE images
        // referenced by #fragment); image_url is the raw ref, which may be an unresolvable fragment.
        const FString& TextureRef = !TexturePath.IsEmpty() ? TexturePath : ImgUrl;
        if (!TextureRef.IsEmpty())
        {
            UTexture2D* Tex = TextureImporter.ImportOrFind(TextureRef, Binding.bSRGB);
            if (Tex)
            {
                MIC->SetTextureParameterValueEditorOnly(
                    FMaterialParameterInfo(Binding.TextureParam), Tex);
                if (Binding.UseFlag != NAME_None)
                {
                    MIC->SetScalarParameterValueEditorOnly(
                        FMaterialParameterInfo(Binding.UseFlag), 1.0f);
                }
            }
        }
    }
}

static void ApplyMappedSceneMaterialChannels(
    uint64_t DsonHandle,
    int32 SceneMatIdx,
    const TMap<FString, FDazParamBinding>& Mapping,
    UMaterialInstanceConstant* MIC,
    FDsonTextureImporter& TextureImporter)
{
    const int32 ChCount = GDsonParser.GetSceneMaterialChannelCount
        ? GDsonParser.GetSceneMaterialChannelCount(DsonHandle, SceneMatIdx) : 0;

    for (int32 c = 0; c < ChCount; ++c)
    {
        const FString ChId = S(GDsonParser.GetSceneMaterialChannelId
            ? GDsonParser.GetSceneMaterialChannelId(DsonHandle, SceneMatIdx, c) : nullptr);

        const FDazParamBinding* Binding = Mapping.Find(ChId);
        if (!Binding)
            continue;

        ApplySceneMaterialChannel(DsonHandle, SceneMatIdx, c, *Binding, MIC, TextureImporter);
    }
}

static FDazChannelInfo FindSceneMaterialChannelById(
    uint64_t DsonHandle,
    int32 SceneMatIdx,
    const FString& ChannelId,
    float DefaultValue)
{
    FDazChannelInfo Info;
    Info.Value = DefaultValue;

    const int32 ChCount = GDsonParser.GetSceneMaterialChannelCount
        ? GDsonParser.GetSceneMaterialChannelCount(DsonHandle, SceneMatIdx) : 0;

    for (int32 c = 0; c < ChCount; ++c)
    {
        const FString ChId = S(GDsonParser.GetSceneMaterialChannelId
            ? GDsonParser.GetSceneMaterialChannelId(DsonHandle, SceneMatIdx, c) : nullptr);
        if (ChId != ChannelId)
            continue;

        if (GDsonParser.GetSceneMaterialChannelValue)
        {
            Info.Value = static_cast<float>(
                GDsonParser.GetSceneMaterialChannelValue(DsonHandle, SceneMatIdx, c));
        }

        const FString TexturePath = S(GDsonParser.GetSceneMaterialChannelTexturePath
            ? GDsonParser.GetSceneMaterialChannelTexturePath(DsonHandle, SceneMatIdx, c) : nullptr);
        const FString ImgUrl = S(GDsonParser.GetSceneMaterialChannelImageUrl
            ? GDsonParser.GetSceneMaterialChannelImageUrl(DsonHandle, SceneMatIdx, c) : nullptr);
        Info.TextureRef = !TexturePath.IsEmpty() ? TexturePath : ImgUrl;
        return Info;
    }

    return Info;
}

static void SetIrayUberNormalParams(
    UMaterialInstanceConstant* MIC,
    UTexture2D* Texture,
    float NormalStrength)
{
    MIC->SetTextureParameterValueEditorOnly(
        FMaterialParameterInfo(FName(TEXT("NormalMap"))), Texture);
    MIC->SetScalarParameterValueEditorOnly(
        FMaterialParameterInfo(FName(TEXT("UseNormalMap"))), 1.0f);
    MIC->SetScalarParameterValueEditorOnly(
        FMaterialParameterInfo(FName(TEXT("NormalStrength"))), NormalStrength);
}

static void ApplyIrayUberNormalChannels(
    uint64_t DsonHandle,
    int32 SceneMatIdx,
    UMaterialInstanceConstant* MIC,
    FDsonTextureImporter& TextureImporter)
{
    const FDazChannelInfo Bump = FindSceneMaterialChannelById(
        DsonHandle, SceneMatIdx, TEXT("Bump Strength"), 0.0f);
    const FDazChannelInfo Normal = FindSceneMaterialChannelById(
        DsonHandle, SceneMatIdx, TEXT("Normal Map"), 1.0f);

    if (!Bump.TextureRef.IsEmpty())
    {
        UTexture2D* CombinedNormal = TextureImporter.ImportBumpAsNormal(
            Bump.TextureRef, Bump.Value, Normal.TextureRef, Normal.Value);
        if (CombinedNormal)
        {
            SetIrayUberNormalParams(MIC, CombinedNormal, Normal.Value);
            return;
        }

        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonMaterialBuilder: bump-normal bake failed for scene material %d; falling back to plain normal map"),
            SceneMatIdx);
    }

    if (!Normal.TextureRef.IsEmpty())
    {
        if (UTexture2D* NormalTexture = TextureImporter.ImportOrFind(Normal.TextureRef, false))
        {
            SetIrayUberNormalParams(MIC, NormalTexture, Normal.Value);
        }
    }
}

static bool ReadFirstSceneMaterialGroupName(
    uint64_t DsonHandle,
    int32 SceneMatIdx,
    FString& OutGroupName)
{
    const int32 GroupCount = GDsonParser.GetSceneMaterialGroupCount
        ? GDsonParser.GetSceneMaterialGroupCount(DsonHandle, SceneMatIdx) : 0;

    const char* NameRaw = (GroupCount > 0 && GDsonParser.GetSceneMaterialGroupName)
        ? GDsonParser.GetSceneMaterialGroupName(DsonHandle, SceneMatIdx, 0) : nullptr;

    if (GroupCount == 0 || !NameRaw)
        return false;

    OutGroupName = S(NameRaw);
    return true;
}

static void CaptureFirstUvSetUrl(uint64_t DsonHandle, int32 SceneMatIdx, FString& InOutUvSetUrl)
{
    if (!InOutUvSetUrl.IsEmpty() || !GDsonParser.GetSceneMaterialUVSetId)
        return;

    if (const char* UvSetRaw = GDsonParser.GetSceneMaterialUVSetId(DsonHandle, SceneMatIdx))
    {
        const FString Candidate = UTF8_TO_TCHAR(UvSetRaw);
        if (!Candidate.IsEmpty())
        {
            InOutUvSetUrl = Candidate;
        }
    }
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

FDsonMaterialBuilder::FDsonMaterialBuilder(const TArray<FString>& InContentRoots,
                                           FDsonTextureImporter& InTextureImporter)
    : ContentRoots(InContentRoots)
    , TextureImporter(InTextureImporter)
{
}

// ---------------------------------------------------------------------------
// DetectShader
// ---------------------------------------------------------------------------

EDazShaderKind FDsonMaterialBuilder::DetectShader(
    const FString& Url, const FString& ShaderType) const
{
    // URL wins: PBRSkin's external .dsf reference is unambiguous.
    // Shader selection controls both master material and channel mapping table.
    // Audits for wrong-looking skin usually start here, then inspect the mapping table.
    if (Url.Contains(TEXT("PBRSkin")))   return EDazShaderKind::PBRSkin;
    if (Url.Contains(TEXT("uber_iray"))) return EDazShaderKind::IrayUber;

    if (ShaderType == TEXT("studio/material/uber_iray")) return EDazShaderKind::IrayUber;
    if (ShaderType == TEXT("studio/material/pbr_skin"))  return EDazShaderKind::PBRSkin;

    if (ShaderType == TEXT("studio/material/daz_brick"))
    {
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonMaterialBuilder: ambiguous daz_brick shader with no PBRSkin URL marker "
                 "- defaulting to IrayUber. Url='%s'"), *Url);
        return EDazShaderKind::IrayUber;
    }

    UE_LOG(LogDsonImporter, Warning,
        TEXT("DsonMaterialBuilder: unknown shader_type='%s' url='%s' - using M_DazDefault"),
        *ShaderType, *Url);
    return EDazShaderKind::Default;
}

// ---------------------------------------------------------------------------
// LoadMasterForShader
// ---------------------------------------------------------------------------

UMaterial* FDsonMaterialBuilder::LoadMasterForShader(EDazShaderKind Kind)
{
    // Master materials are plugin content assets. Cache only weak references so UE can
    // still unload/reload assets during editor workflows.
    TWeakObjectPtr<UMaterial>* Cached = nullptr;
    const TCHAR* Path = nullptr;

    switch (Kind)
    {
        case EDazShaderKind::IrayUber: Cached = &CachedIrayUberMaster; Path = kIrayUberMasterPath; break;
        case EDazShaderKind::PBRSkin:  Cached = &CachedPBRSkinMaster;  Path = kPBRSkinMasterPath;  break;
        case EDazShaderKind::Default:  Cached = &CachedDefaultMaster;  Path = kDefaultMasterPath;  break;
    }

    if (Cached->IsValid())
        return Cached->Get();

    UMaterial* Master = LoadObject<UMaterial>(nullptr, Path);
    if (!Master)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonMaterialBuilder: failed to load master material '%s'"), Path);
        return nullptr;
    }

    *Cached = Master;
    return Master;
}

void FDsonMaterialBuilder::RecordShaderKind(EDazShaderKind Kind)
{
    switch (Kind)
    {
        case EDazShaderKind::IrayUber: ++IrayUberCount; break;
        case EDazShaderKind::PBRSkin:  ++PBRSkinCount;  break;
        case EDazShaderKind::Default:  ++DefaultCount;  break;
    }
}

void FDsonMaterialBuilder::RecordFailure()
{
    ++FailureCount;
}

// ---------------------------------------------------------------------------
// BuildSceneMaterial
// ---------------------------------------------------------------------------

UMaterialInstanceConstant* FDsonMaterialBuilder::BuildSceneMaterial(
    DsonDocumentHandle Doc,
    int32 SceneMatIdx,
    const FString& OutputFolder)
{
    // Builds exactly one MIC for one scene_material entry. All parser strings are copied
    // into FString immediately, because later parser calls may invalidate returned char*.
    const uint64_t H = reinterpret_cast<uint64_t>(Doc);

    // Step 1 - read id / url / shader_type; detect shader; increment per-shader counter
    const FSceneMaterialMetadata Metadata = ReadSceneMaterialMetadata(H, SceneMatIdx);
    const EDazShaderKind Kind = DetectShader(Metadata.Url, Metadata.ShaderType);
    RecordShaderKind(Kind);

    // Step 2 - load master; do NOT substitute a different master on failure
    UMaterial* Master = LoadMasterForShader(Kind);
    if (!Master)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonMaterialBuilder: aborting build for scene material '%s' - master load failed"),
            *Metadata.MatId);
        RecordFailure();
        return nullptr;
    }

    // Step 3 - create and fully load MIC package
    const FMaterialInstanceAssetContext AssetContext =
        CreateMaterialInstanceAsset(Metadata.MatId, OutputFolder);
    if (!AssetContext.IsValid())
    {
        RecordFailure();
        return nullptr;
    }
    UMaterialInstanceConstant* MIC = AssetContext.MIC;

    // Step 4 - assign parent
    MIC->Parent = Master;

    // Step 5 - iterate channels and apply parameters from the active mapping table.
    // Default shader has no defined mapping so no parameter overrides are applied.
    if (const TMap<FString, FDazParamBinding>* Mapping = GetMappingForShader(Kind))
    {
        ApplyMappedSceneMaterialChannels(H, SceneMatIdx, *Mapping, MIC, TextureImporter);
    }
    if (Kind == EDazShaderKind::IrayUber)
    {
        ApplyIrayUberNormalChannels(H, SceneMatIdx, MIC, TextureImporter);
    }

    // Step 6 - finalise parameter override arrays before save
    MIC->PostEditChange();

    // Step 7 - save (mirrors DsonTextureImporter exactly)
    if (!FDsonAssetUtils::SaveAssetPackage(
            AssetContext.Package, MIC, AssetContext.AssetPath.PackagePath, TEXT("DsonMaterialBuilder")))
    {
        RecordFailure();
        return nullptr;
    }

    // Step 8 - notify asset registry
    ++BuiltCount;
    return MIC;
}

// ---------------------------------------------------------------------------
// BuildAllSceneMaterials
// ---------------------------------------------------------------------------

void FDsonMaterialBuilder::BuildAllSceneMaterials(
    const FString& DufPath,
    const FString& OutputFolder,
    TMap<FString, UMaterialInstanceConstant*>& OutByGroup,
    FString& OutUvSetUrl)
{
    FDsonLoadedDocument Document;
    if (!Document.LoadFromFileAsWarning(DufPath, TEXT("DsonMaterialBuilder")))
        return;

    DsonDocumentHandle Handle = Document.GetHandle();
    const uint64_t H = reinterpret_cast<uint64_t>(Handle);

    const int32 SceneMatCount = GDsonParser.GetSceneMaterialCount
        ? GDsonParser.GetSceneMaterialCount(H) : 0;

    for (int32 i = 0; i < SceneMatCount; ++i)
    {
        UMaterialInstanceConstant* MIC = BuildSceneMaterial(Handle, i, OutputFolder);
        if (!MIC)
            continue;

        FString GroupName;
        const bool bHasGroupName = ReadFirstSceneMaterialGroupName(H, i, GroupName);
        CaptureFirstUvSetUrl(H, i, OutUvSetUrl);

        if (!bHasGroupName)
        {
            FString SceneId = S(GDsonParser.GetSceneMaterialId
                ? GDsonParser.GetSceneMaterialId(H, i) : nullptr);
            UE_LOG(LogDsonImporter, Warning,
                TEXT("[mat] scene material '%s' has no mappable group - MIC built but not wired"), *SceneId);
            continue;
        }

        if (OutByGroup.Contains(GroupName))
        {
            UE_LOG(LogDsonImporter, Warning,
                TEXT("[mat] duplicate group \"%s\" - later MIC wins"), *GroupName);
        }

        OutByGroup.Add(GroupName, MIC);
    }
}
