#include "DsonMaterialBuilder.h"
#include "DsonImporter.h"
#include "DsonAssetUtils.h"
#include "DsonContentRoots.h"
#include "DsonParserFunctions.h"
#include "DsonImportUtils.h"
#include "DsonLoadedDocument.h"
#include "DsonTextureImporter.h"

#include "Engine/Texture2D.h"
#include "Engine/SubsurfaceProfile.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "ObjectTools.h"
#include "Misc/Paths.h"

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

static const TCHAR* kIrayUberMasterPath    = TEXT("/DsonToUnreal/Materials/M_DazIrayUber");
static const TCHAR* kPBRSkinMasterPath     = TEXT("/DsonToUnreal/Materials/M_DazPBRSkin");
static const TCHAR* kDefaultMasterPath     = TEXT("/DsonToUnreal/Materials/M_DazDefault");
static const TCHAR* kEyeMoistureMasterPath = TEXT("/DsonToUnreal/Materials/M_DazEyeMoisture");
static constexpr float kSkinSubsurfaceWeight = 0.85f;
static constexpr float kSssTintStrength = 0.4f;
static constexpr float kSssMeanFreePathDistance = 2.6f;

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

struct FDazColorChannelInfo
{
    bool bHasColor = false;
    FLinearColor Color = FLinearColor::White;
};

struct FStandaloneTextureImportCounts
{
    int32 Makeup = 0;
    int32 LieLayers = 0;
};

// Identifies which parser channel family to read: scene_material[MatIdx] or material_library[MatIdx].
struct FDazChannelSource
{
    bool  bLibrary = false;
    int32 MatIdx   = 0;
};

enum class EDsonSurfaceClass : uint8
{
    Skin,
    EyeMoisture,
    NonSkin,
    Unknown,
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
        // PBRSkin keeps translucency, re-fed per decision B1 (see SubsurfaceProfileV2.md,
        // "Revision" section): the master routes a tuned TranslucencyColor * Map * Weight
        // into Base Color for the skin brightness the Subsurface Profile cannot add. Feed
        // RAW DAZ values here - the tuning scale is master-side. Not a Slice-2 leftover;
        // these land in lockstep with the M_DazPBRSkin re-wire, so do not remove on cleanup.
        M.Add(TEXT("Translucency Color"),
            { FName(TEXT("TranslucencyColor")),  NAME_None,                              FName(TEXT("TranslucencyMap")),      FName(TEXT("UseTranslucencyMap")),      true  });
        M.Add(TEXT("Translucency Weight"),
            { NAME_None,                         FName(TEXT("TranslucencyWeight")),      NAME_None,                           NAME_None,                              false });
        M.Add(TEXT("Specular Lobe 1 Roughness"),
            { NAME_None,                         FName(TEXT("SpecularRoughness")),       FName(TEXT("SpecularRoughnessMap")), FName(TEXT("UseSpecularRoughnessMap")), false });
        // "Specular Lobe 2 Roughness Mult" and "Dual Lobe Specular Weight" are deliberately
        // NOT mapped here. Both are DAZ dual-lobe (lobe-2) quantities gated by
        // "Dual Lobe Specular Enable", which is false by the PBRSkin base default and is
        // false on every verified surface (Nancy + Laura, all 7 surfaces). The master folds
        // them into Roughness as: SpecularRoughness × SpecularRoughnessMult ×
        // (1 − 0.3 × DualLobeWeight). At DAZ lobe-2 defaults (Mult=0.55, Weight=1.0) this
        // crushes skin roughness to 0.385× the DAZ value (near-mirror). The master defaults
        // (SpecularRoughnessMult=1, DualLobeWeight=0) are no-ops, so leaving them unbound
        // is faithful. Honoring the enable gate for dual-lobe-on characters is parked; see
        // Docs/DecisionLog.md.
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

static const TMap<FString, FDazParamBinding>& GetEyeMoistureMapping()
{
    // DAZ channel id -> Unreal master parameter binding for eye-moisture (wet-eye) surfaces.
    // All channels are pure parametric (no textures). Feed raw DAZ values; the master handles
    // translucency tuning (same pattern as PBRSkin decision B1).
    // Parameter names define the M_DazEyeMoisture contract (MaterialMastersV1.md).
    static const TMap<FString, FDazParamBinding> Map = []()
    {
        TMap<FString, FDazParamBinding> M;
        M.Add(TEXT("diffuse"),
            { FName(TEXT("BaseColor")),    NAME_None,                       NAME_None, NAME_None, false });
        M.Add(TEXT("Glossy Reflectivity"),
            { NAME_None,  FName(TEXT("Specular")),      NAME_None, NAME_None, false });
        M.Add(TEXT("Glossy Roughness"),
            { NAME_None,  FName(TEXT("Roughness")),     NAME_None, NAME_None, false });
        M.Add(TEXT("Cutout Opacity"),
            { NAME_None,  FName(TEXT("Opacity")),       NAME_None, NAME_None, false });
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

static const TSet<FString>& GetStandaloneImageChannelIds()
{
    static const TSet<FString> ChannelIds = {
        TEXT("Makeup Base Color"),
    };
    return ChannelIds;
}

static const TSet<FString>& GetSkinSurfaceGroups()
{
    static const TSet<FString> Groups = {
        TEXT("Face"),
        TEXT("Head"),
        TEXT("Body"),
        TEXT("Torso"),
        TEXT("Arms"),
        TEXT("Legs"),
        TEXT("Ears"),
        TEXT("Lips"),
    };
    return Groups;
}

static const TSet<FString>& GetEyeMoistureSurfaceGroups()
{
    static const TSet<FString> Groups = {
        TEXT("EyeMoisture"),
        TEXT("EyeMoisture Left"),
        TEXT("EyeMoisture Right"),
        TEXT("Cornea"),
        TEXT("Tear"),
    };
    return Groups;
}

static const TSet<FString>& GetNonSkinSurfaceGroups()
{
    static const TSet<FString> Groups = {
        TEXT("Teeth"),
        TEXT("Mouth"),
        TEXT("Mouth Cavity"),
        TEXT("Tongue"),
        TEXT("EyeSocket"),
        TEXT("Eye Left"),
        TEXT("Eye Right"),
        TEXT("Pupils"),
        TEXT("Irises"),
        TEXT("Sclera"),
        TEXT("Eyelashes"),
        TEXT("Eyelashes Lower"),
        TEXT("Eyelashes Upper"),
        TEXT("Fingernails"),
        TEXT("Toenails"),
    };
    return Groups;
}

static EDsonSurfaceClass ClassifySurfaceGroup(const FString& GroupName)
{
    if (GetSkinSurfaceGroups().Contains(GroupName))
        return EDsonSurfaceClass::Skin;
    if (GetEyeMoistureSurfaceGroups().Contains(GroupName))
        return EDsonSurfaceClass::EyeMoisture;
    if (GetNonSkinSurfaceGroups().Contains(GroupName))
        return EDsonSurfaceClass::NonSkin;
    return EDsonSurfaceClass::Unknown;
}

// ---------------------------------------------------------------------------
// Helper: nullable const char* -> FString (same pattern as the diagnostic)
// ---------------------------------------------------------------------------

// Short local alias for the shared nullable-utf8 -> FString helper (used heavily below).
static FString S(const char* Raw) { return DsonImportUtils::FromUtf8(Raw); }

static FString BuildLayerTextureSuffix(const FString& LayerLabel, int32 LayerIdx)
{
    const FString LabelPart = ObjectTools::SanitizeObjectName(LayerLabel);
    if (LabelPart.IsEmpty())
        return FString::Printf(TEXT("_lie_%d"), LayerIdx);

    return FString::Printf(TEXT("_lie_%d_%s"), LayerIdx, *LabelPart);
}

static FString ReadSceneMaterialChannelTextureRef(uint64_t DsonHandle, int32 SceneMatIdx, int32 ChannelIdx)
{
    const FString TexturePath = S(GDsonParser.GetSceneMaterialChannelTexturePath
        ? GDsonParser.GetSceneMaterialChannelTexturePath(DsonHandle, SceneMatIdx, ChannelIdx) : nullptr);
    const FString ImgUrl = S(GDsonParser.GetSceneMaterialChannelImageUrl
        ? GDsonParser.GetSceneMaterialChannelImageUrl(DsonHandle, SceneMatIdx, ChannelIdx) : nullptr);

    // texture_path is the parser's resolved image_library link (including LIE base);
    // image_url is the raw fallback for plain channels.
    return !TexturePath.IsEmpty() ? TexturePath : ImgUrl;
}

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

static void ApplyMaterialChannel(
    uint64_t H,
    const FDazChannelSource& Src,
    int32 c,
    const FDazParamBinding& Binding,
    UMaterialInstanceConstant* MIC,
    FDsonTextureImporter& TextureImporter)
{
    const bool bLib = Src.bLibrary;
    const int32 Mid = Src.MatIdx;

    const bool bHasColor = bLib
        ? (GDsonParser.GetMaterialChannelHasColor    ? GDsonParser.GetMaterialChannelHasColor(H, Mid, c)    : false)
        : (GDsonParser.GetSceneMaterialChannelHasColor ? GDsonParser.GetSceneMaterialChannelHasColor(H, Mid, c) : false);

    // Color or scalar: both applied even when a texture is also present.
    if (bHasColor && Binding.ColorParam != NAME_None)
    {
        const double R = bLib
            ? (GDsonParser.GetMaterialChannelColorR    ? GDsonParser.GetMaterialChannelColorR(H, Mid, c)    : 0.0)
            : (GDsonParser.GetSceneMaterialChannelColorR ? GDsonParser.GetSceneMaterialChannelColorR(H, Mid, c) : 0.0);
        const double G = bLib
            ? (GDsonParser.GetMaterialChannelColorG    ? GDsonParser.GetMaterialChannelColorG(H, Mid, c)    : 0.0)
            : (GDsonParser.GetSceneMaterialChannelColorG ? GDsonParser.GetSceneMaterialChannelColorG(H, Mid, c) : 0.0);
        const double B = bLib
            ? (GDsonParser.GetMaterialChannelColorB    ? GDsonParser.GetMaterialChannelColorB(H, Mid, c)    : 0.0)
            : (GDsonParser.GetSceneMaterialChannelColorB ? GDsonParser.GetSceneMaterialChannelColorB(H, Mid, c) : 0.0);
        MIC->SetVectorParameterValueEditorOnly(
            FMaterialParameterInfo(Binding.ColorParam),
            FLinearColor(static_cast<float>(R), static_cast<float>(G), static_cast<float>(B), 1.0f));
    }
    else if (!bHasColor && Binding.ScalarParam != NAME_None)
    {
        const double Val = bLib
            ? (GDsonParser.GetMaterialChannelValue    ? GDsonParser.GetMaterialChannelValue(H, Mid, c)    : 0.0)
            : (GDsonParser.GetSceneMaterialChannelValue ? GDsonParser.GetSceneMaterialChannelValue(H, Mid, c) : 0.0);
        MIC->SetScalarParameterValueEditorOnly(
            FMaterialParameterInfo(Binding.ScalarParam),
            static_cast<float>(Val));
    }

    // Texture + UseFlag: orthogonal to color/scalar, applied when an image path exists.
    if (Binding.TextureParam != NAME_None)
    {
        const FString TexPath = bLib
            ? S(GDsonParser.GetMaterialChannelTexturePath    ? GDsonParser.GetMaterialChannelTexturePath(H, Mid, c)    : nullptr)
            : S(GDsonParser.GetSceneMaterialChannelTexturePath ? GDsonParser.GetSceneMaterialChannelTexturePath(H, Mid, c) : nullptr);
        const FString ImgUrl = bLib
            ? S(GDsonParser.GetMaterialChannelImageUrl    ? GDsonParser.GetMaterialChannelImageUrl(H, Mid, c)    : nullptr)
            : S(GDsonParser.GetSceneMaterialChannelImageUrl ? GDsonParser.GetSceneMaterialChannelImageUrl(H, Mid, c) : nullptr);
        const FString TextureRef = !TexPath.IsEmpty() ? TexPath : ImgUrl;
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

static void ApplyMappedMaterialChannels(
    uint64_t H,
    const FDazChannelSource& Src,
    const TMap<FString, FDazParamBinding>& Mapping,
    UMaterialInstanceConstant* MIC,
    FDsonTextureImporter& TextureImporter)
{
    const bool bLib = Src.bLibrary;
    const int32 Mid = Src.MatIdx;

    const int32 ChCount = bLib
        ? (GDsonParser.GetMaterialChannelCount      ? GDsonParser.GetMaterialChannelCount(H, Mid)      : 0)
        : (GDsonParser.GetSceneMaterialChannelCount ? GDsonParser.GetSceneMaterialChannelCount(H, Mid) : 0);

    for (int32 c = 0; c < ChCount; ++c)
    {
        const FString ChId = bLib
            ? S(GDsonParser.GetMaterialChannelId      ? GDsonParser.GetMaterialChannelId(H, Mid, c)      : nullptr)
            : S(GDsonParser.GetSceneMaterialChannelId ? GDsonParser.GetSceneMaterialChannelId(H, Mid, c) : nullptr);

        const FDazParamBinding* Binding = Mapping.Find(ChId);
        if (!Binding)
            continue;

        ApplyMaterialChannel(H, Src, c, *Binding, MIC, TextureImporter);
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

        Info.TextureRef = ReadSceneMaterialChannelTextureRef(DsonHandle, SceneMatIdx, c);
        return Info;
    }

    return Info;
}

static FDazColorChannelInfo FindSceneMaterialColorChannelById(
    uint64_t DsonHandle,
    int32 SceneMatIdx,
    const FString& ChannelId)
{
    FDazColorChannelInfo Info;
    const int32 ChCount = GDsonParser.GetSceneMaterialChannelCount
        ? GDsonParser.GetSceneMaterialChannelCount(DsonHandle, SceneMatIdx) : 0;

    for (int32 c = 0; c < ChCount; ++c)
    {
        const FString ChId = S(GDsonParser.GetSceneMaterialChannelId
            ? GDsonParser.GetSceneMaterialChannelId(DsonHandle, SceneMatIdx, c) : nullptr);
        if (ChId != ChannelId)
            continue;

        Info.bHasColor = GDsonParser.GetSceneMaterialChannelHasColor
            ? GDsonParser.GetSceneMaterialChannelHasColor(DsonHandle, SceneMatIdx, c) : false;
        if (Info.bHasColor)
        {
            const double R = GDsonParser.GetSceneMaterialChannelColorR
                ? GDsonParser.GetSceneMaterialChannelColorR(DsonHandle, SceneMatIdx, c) : 1.0;
            const double G = GDsonParser.GetSceneMaterialChannelColorG
                ? GDsonParser.GetSceneMaterialChannelColorG(DsonHandle, SceneMatIdx, c) : 1.0;
            const double B = GDsonParser.GetSceneMaterialChannelColorB
                ? GDsonParser.GetSceneMaterialChannelColorB(DsonHandle, SceneMatIdx, c) : 1.0;
            Info.Color = FLinearColor(static_cast<float>(R), static_cast<float>(G), static_cast<float>(B), 1.0f);
        }
        return Info;
    }

    return Info;
}

static bool TryReadSkinTintColor(
    uint64_t DsonHandle,
    int32 SceneMatIdx,
    EDazShaderKind Kind,
    FLinearColor& OutColor)
{
    if (Kind == EDazShaderKind::PBRSkin)
    {
        const FDazColorChannelInfo SssColor =
            FindSceneMaterialColorChannelById(DsonHandle, SceneMatIdx, TEXT("SSS Color"));
        if (SssColor.bHasColor)
        {
            OutColor = SssColor.Color;
            return true;
        }
    }

    const FDazColorChannelInfo TranslucencyColor =
        FindSceneMaterialColorChannelById(DsonHandle, SceneMatIdx, TEXT("Translucency Color"));
    if (TranslucencyColor.bHasColor)
    {
        OutColor = TranslucencyColor.Color;
        return true;
    }

    return false;
}

static FLinearColor ClampSkinTintColor(const FLinearColor& Color)
{
    return FLinearColor(
        FMath::Clamp(Color.R, 0.0f, 1.0f),
        FMath::Clamp(Color.G, 0.0f, 1.0f),
        FMath::Clamp(Color.B, 0.0f, 1.0f),
        1.0f);
}

static int32 GetRepresentativeSkinGroupPriority(const FString& GroupName)
{
    if (GroupName == TEXT("Face")) return 0;
    if (GroupName == TEXT("Head")) return 1;
    if (GroupName == TEXT("Torso") || GroupName == TEXT("Body")) return 2;
    return INDEX_NONE;
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

// Strips a trailing -<digits> uniquifying suffix (e.g. "EyeMoisture Left-1" -> "EyeMoisture Left").
// Only strips when the entire tail after the last '-' is digits and the dash is not at position 0.
static FString StripUniquifyingSuffix(const FString& Id)
{
    const int32 Len = Id.Len();
    int32 i = Len - 1;
    while (i >= 0 && FChar::IsDigit(Id[i]))
        --i;
    if (i < 0 || i == Len - 1 || Id[i] != TEXT('-') || i == 0)
        return Id;
    return Id.Left(i);
}

// Returns the channel source for a scene material: if its url is a bare #fragment with
// zero inline channels, resolve the fragment to a material_library index and return it;
// otherwise return the scene-material index. Permissive: logs a warning and falls back to
// the scene source (zero channels) when the fragment is not found in the library.
static FDazChannelSource ResolveChannelSource(uint64_t H, int32 SceneMatIdx, const FString& Url)
{
    const FDazChannelSource SceneDefault = { false, SceneMatIdx };

    if (!Url.StartsWith(TEXT("#")))
        return SceneDefault;

    const int32 SceneChCount = GDsonParser.GetSceneMaterialChannelCount
        ? GDsonParser.GetSceneMaterialChannelCount(H, SceneMatIdx) : 0;
    if (SceneChCount != 0)
        return SceneDefault;

    // Bare #fragment — resolve to material_library entry (UrlDecode: %20 -> space etc.).
    const FString FragmentId = FDsonContentRoots::UrlDecode(Url.Mid(1));
    const int32 MatCount = GDsonParser.GetMaterialCount ? GDsonParser.GetMaterialCount(H) : 0;
    for (int32 i = 0; i < MatCount; ++i)
    {
        const FString LibId = S(GDsonParser.GetMaterialId ? GDsonParser.GetMaterialId(H, i) : nullptr);
        if (LibId == FragmentId)
            return { true, i };
    }

    UE_LOG(LogDsonImporter, Warning,
        TEXT("[mat] bare #fragment url '%s' not resolved in material_library (decoded: '%s')"),
        *Url, *FragmentId);
    return SceneDefault;
}

// ---------------------------------------------------------------------------
// Scene animation (key-0) override helpers
// ---------------------------------------------------------------------------

// Parses one scene.animations url into (matId, channelId, leaf).
// Accepts top-level form (<channel>/<leaf>, 2 segments) and extra form
// (extra/studio_material_channels/channels/<Name>/<leaf>, 5 segments).
// Returns false and leaves out-params untouched for anything else.
static bool ParseAnimationUrl(
    const FString& Url,
    FString& OutMatId,
    FString& OutChannelId,
    FString& OutLeaf)
{
    // Require "#materials/" marker
    static const FString MaterialsMarker(TEXT("#materials/"));
    const int32 MatSegIdx = Url.Find(MaterialsMarker, ESearchCase::CaseSensitive);
    if (MatSegIdx == INDEX_NONE)
        return false;

    // matId runs from after "#materials/" up to the ":?" separator
    const int32 AfterMarker = MatSegIdx + MaterialsMarker.Len();
    const int32 SepIdx = Url.Find(TEXT(":?"), ESearchCase::CaseSensitive,
        ESearchDir::FromStart, AfterMarker);
    if (SepIdx == INDEX_NONE)
        return false;

    FString MatId = Url.Mid(AfterMarker, SepIdx - AfterMarker);
    const FString PropertyPath = Url.Mid(SepIdx + 2); // skip ":?"

    // Split on "/"; encoded channel names use %20 for spaces, no literal "/"
    TArray<FString> Segs;
    PropertyPath.ParseIntoArray(Segs, TEXT("/"), /*bCullEmpty=*/true);

    FString ChannelId;
    FString Leaf;

    if (Segs.Num() == 2)
    {
        // Top-level: <channel>/<leaf>
        ChannelId = Segs[0];
        Leaf = Segs[1];
    }
    else if (Segs.Num() == 5
        && Segs[0] == TEXT("extra")
        && Segs[1] == TEXT("studio_material_channels")
        && Segs[2] == TEXT("channels"))
    {
        // Extra: extra/studio_material_channels/channels/<Name>/<leaf>
        ChannelId = FDsonContentRoots::UrlDecode(Segs[3]);
        Leaf = Segs[4];
    }
    else
    {
        return false;
    }

    if (Leaf != TEXT("value") && Leaf != TEXT("image_file") && Leaf != TEXT("image"))
        return false;

    OutMatId    = MoveTemp(MatId);
    OutChannelId = MoveTemp(ChannelId);
    OutLeaf     = MoveTemp(Leaf);
    return true;
}

// Applies scene.animations key-0 values for the given matId onto MIC,
// overriding any placeholders set by the base scene.materials pass.
static void ApplySceneAnimationOverrides(
    uint64_t H,
    const FString& MatId,
    const TMap<FString, FDazParamBinding>& Mapping,
    UMaterialInstanceConstant* MIC,
    FDsonTextureImporter& TextureImporter)
{
    // Animation accessors use DsonDocumentHandle (not uint64_t) as first param.
    const DsonDocumentHandle Doc = reinterpret_cast<DsonDocumentHandle>(H);

    const int32 AnimCount = GDsonParser.GetSceneAnimationCount
        ? GDsonParser.GetSceneAnimationCount(Doc) : 0;
    if (AnimCount == 0)
        return;

    int32 MatchedForMat = 0;
    int32 AppliedTextures = 0;
    int32 AppliedColors = 0;
    int32 AppliedScalars = 0;

    for (int32 i = 0; i < AnimCount; ++i)
    {
        // R3: copy const char* to FString before the next parser call
        const FString UrlStr = S(GDsonParser.GetSceneAnimationUrl
            ? GDsonParser.GetSceneAnimationUrl(Doc, i) : nullptr);
        if (UrlStr.IsEmpty())
            continue;

        FString ParsedMatId, ChannelId, Leaf;
        if (!ParseAnimationUrl(UrlStr, ParsedMatId, ChannelId, Leaf))
            continue;

        // UrlDecode the parsed matId (e.g. "EyeMoisture%20Left" -> "EyeMoisture Left") then
        // match against both the raw scene id and the scene id with its uniquifying -<n> suffix
        // stripped (e.g. "EyeMoisture Left-1" -> "EyeMoisture Left"). Regular surface ids like
        // "Face" are unchanged by both transforms, so there is no regression on them.
        const FString DecodedParsedMatId = FDsonContentRoots::UrlDecode(ParsedMatId);
        if (DecodedParsedMatId != MatId && DecodedParsedMatId != StripUniquifyingSuffix(MatId))
            continue;

        ++MatchedForMat;

        const FDazParamBinding* Binding = Mapping.Find(ChannelId);
        if (!Binding)
            continue;

        const int32 ValueKind = GDsonParser.GetSceneAnimationValueKind
            ? GDsonParser.GetSceneAnimationValueKind(Doc, i) : -1;

        if (Leaf == TEXT("image_file"))
        {
            if (Binding->TextureParam != NAME_None && ValueKind == 3)
            {
                // R3: copy string result before next parser call
                const FString TextureRef = S(GDsonParser.GetSceneAnimationString
                    ? GDsonParser.GetSceneAnimationString(Doc, i) : nullptr);
                if (!TextureRef.IsEmpty())
                {
                    if (UTexture2D* Tex = TextureImporter.ImportOrFind(TextureRef, Binding->bSRGB))
                    {
                        MIC->SetTextureParameterValueEditorOnly(
                            FMaterialParameterInfo(Binding->TextureParam), Tex);
                        if (Binding->UseFlag != NAME_None)
                        {
                            MIC->SetScalarParameterValueEditorOnly(
                                FMaterialParameterInfo(Binding->UseFlag), 1.0f);
                        }
                        ++AppliedTextures;
                    }
                }
            }
        }
        else if (Leaf == TEXT("image"))
        {
            if (Binding->TextureParam != NAME_None && ValueKind == 3)
            {
                // R3: copy string before next parser call
                const FString ImageRef = S(GDsonParser.GetSceneAnimationString
                    ? GDsonParser.GetSceneAnimationString(Doc, i) : nullptr);
                if (!ImageRef.IsEmpty())
                {
                    UTexture2D* Tex = nullptr;
                    if (ImageRef.StartsWith(TEXT("#")))
                    {
                        // R4: shared helper resolves #fragment to image_library index
                        const int32 ImageIndex = DsonImportUtils::FindImageLibraryIndex(Doc, ImageRef);
                        if (ImageIndex == INDEX_NONE)
                        {
                            UE_LOG(LogDsonImporter, Warning,
                                TEXT("[mat-anim] '%s': image_library entry not found (ref='%s')"),
                                *MatId, *ImageRef);
                        }
                        else
                        {
                            const FString ImageId = FDsonContentRoots::UrlDecode(ImageRef.Mid(1));
                            const int32 CanvasW = GDsonParser.GetImageMapWidth
                                ? GDsonParser.GetImageMapWidth(Doc, ImageIndex) : 0;
                            const int32 CanvasH = GDsonParser.GetImageMapHeight
                                ? GDsonParser.GetImageMapHeight(Doc, ImageIndex) : 0;
                            const int32 LayerCount = GDsonParser.GetImageLayerCount
                                ? GDsonParser.GetImageLayerCount(Doc, ImageIndex) : 0;
                            TArray<FString> LayerPaths;
                            LayerPaths.Reserve(LayerCount);
                            for (int32 k = 0; k < LayerCount; ++k)
                            {
                                // R3: copy each path before the next parser call
                                LayerPaths.Add(S(GDsonParser.GetImageLayerTexturePath
                                    ? GDsonParser.GetImageLayerTexturePath(Doc, ImageIndex, k) : nullptr));
                            }
                            Tex = TextureImporter.CompositeImageLayers(LayerPaths, ImageId, Binding->bSRGB, CanvasW, CanvasH);
                        }
                    }
                    else
                    {
                        Tex = TextureImporter.ImportOrFind(ImageRef, Binding->bSRGB);
                    }

                    if (Tex)
                    {
                        MIC->SetTextureParameterValueEditorOnly(
                            FMaterialParameterInfo(Binding->TextureParam), Tex);
                        if (Binding->UseFlag != NAME_None)
                        {
                            MIC->SetScalarParameterValueEditorOnly(
                                FMaterialParameterInfo(Binding->UseFlag), 1.0f);
                        }
                        ++AppliedTextures;
                    }
                }
            }
        }
        else if (Leaf == TEXT("value"))
        {
            if (ValueKind == 4 && Binding->ColorParam != NAME_None)
            {
                const float R = static_cast<float>(GDsonParser.GetSceneAnimationColorR
                    ? GDsonParser.GetSceneAnimationColorR(Doc, i) : 0.0);
                const float G = static_cast<float>(GDsonParser.GetSceneAnimationColorG
                    ? GDsonParser.GetSceneAnimationColorG(Doc, i) : 0.0);
                const float B = static_cast<float>(GDsonParser.GetSceneAnimationColorB
                    ? GDsonParser.GetSceneAnimationColorB(Doc, i) : 0.0);
                MIC->SetVectorParameterValueEditorOnly(
                    FMaterialParameterInfo(Binding->ColorParam),
                    FLinearColor(R, G, B, 1.0f));
                ++AppliedColors;
            }
            else if (ValueKind == 1 && Binding->ScalarParam != NAME_None)
            {
                const float Val = static_cast<float>(GDsonParser.GetSceneAnimationFloat
                    ? GDsonParser.GetSceneAnimationFloat(Doc, i) : 0.0);
                MIC->SetScalarParameterValueEditorOnly(
                    FMaterialParameterInfo(Binding->ScalarParam), Val);
                ++AppliedScalars;
            }
        }
    }

    if (MatchedForMat > 0)
    {
        UE_LOG(LogDsonImporter, Verbose,
            TEXT("[mat-anim] '%s': textures=%d colors=%d scalars=%d (%d matched)"),
            *MatId, AppliedTextures, AppliedColors, AppliedScalars, MatchedForMat);

        if ((AppliedTextures + AppliedColors + AppliedScalars) == 0)
        {
            UE_LOG(LogDsonImporter, Warning,
                TEXT("[mat-anim] '%s': %d entries matched but nothing applied - channel mapping may be incomplete"),
                *MatId, MatchedForMat);
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
// LoadAndCacheMaster / LoadMasterForShader
// ---------------------------------------------------------------------------

// Master materials are plugin content assets. Cache only weak references so UE can
// still unload/reload assets during editor workflows.
static UMaterial* LoadAndCacheMaster(TWeakObjectPtr<UMaterial>& Cache, const TCHAR* Path)
{
    if (Cache.IsValid())
        return Cache.Get();

    UMaterial* Master = LoadObject<UMaterial>(nullptr, Path);
    if (!Master)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonMaterialBuilder: failed to load master material '%s'"), Path);
        return nullptr;
    }

    Cache = Master;
    return Master;
}

UMaterial* FDsonMaterialBuilder::LoadMasterForShader(EDazShaderKind Kind)
{
    TWeakObjectPtr<UMaterial>* Cached = nullptr;
    const TCHAR* Path = nullptr;

    switch (Kind)
    {
        case EDazShaderKind::IrayUber: Cached = &CachedIrayUberMaster; Path = kIrayUberMasterPath; break;
        case EDazShaderKind::PBRSkin:  Cached = &CachedPBRSkinMaster;  Path = kPBRSkinMasterPath;  break;
        case EDazShaderKind::Default:  Cached = &CachedDefaultMaster;  Path = kDefaultMasterPath;  break;
    }

    return LoadAndCacheMaster(*Cached, Path);
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

void FDsonMaterialBuilder::ImportStandaloneChannelTextures(
    uint64_t DsonHandle,
    int32 SceneMatIdx,
    FDsonTextureImporter& InTextureImporter) const
{
    FStandaloneTextureImportCounts Counts;
    const int32 ChCount = GDsonParser.GetSceneMaterialChannelCount
        ? GDsonParser.GetSceneMaterialChannelCount(DsonHandle, SceneMatIdx) : 0;

    for (int32 c = 0; c < ChCount; ++c)
    {
        const FString ChId = S(GDsonParser.GetSceneMaterialChannelId
            ? GDsonParser.GetSceneMaterialChannelId(DsonHandle, SceneMatIdx, c) : nullptr);

        if (GetStandaloneImageChannelIds().Contains(ChId))
        {
            const FString TextureRef = ReadSceneMaterialChannelTextureRef(DsonHandle, SceneMatIdx, c);
            if (!TextureRef.IsEmpty() && InTextureImporter.ImportOrFind(TextureRef, true))
                ++Counts.Makeup;
        }

        const int32 LayerCount = GDsonParser.GetSceneMaterialChannelLayerCount
            ? GDsonParser.GetSceneMaterialChannelLayerCount(DsonHandle, SceneMatIdx, c) : 0;
        if (LayerCount < 2 || !GDsonParser.GetSceneMaterialChannelLayerTexturePath)
            continue;

        for (int32 LayerIdx = 1; LayerIdx < LayerCount; ++LayerIdx)
        {
            const FString LayerPath = S(GDsonParser.GetSceneMaterialChannelLayerTexturePath(
                DsonHandle, SceneMatIdx, c, LayerIdx));
            if (LayerPath.IsEmpty())
                continue;

            const FString LayerLabel = S(GDsonParser.GetSceneMaterialChannelLayerLabel
                ? GDsonParser.GetSceneMaterialChannelLayerLabel(DsonHandle, SceneMatIdx, c, LayerIdx) : nullptr);

            // Parser exposes no per-layer colorspace; makeup and LIE overlays are treated as color-domain.
            if (InTextureImporter.ImportOrFind(LayerPath, true, BuildLayerTextureSuffix(LayerLabel, LayerIdx)))
                ++Counts.LieLayers;
        }
    }

    if (Counts.Makeup > 0 || Counts.LieLayers > 0)
    {
        UE_LOG(LogDsonImporter, Verbose,
            TEXT("[mat] standalone textures: makeup=%d lie-layers=%d"),
            Counts.Makeup, Counts.LieLayers);
    }
}

USubsurfaceProfile* FDsonMaterialBuilder::BuildSubsurfaceProfileForDocument(
    uint64_t DsonHandle,
    const FString& OutputFolder,
    const FString& OwnerName)
{
    CachedSubsurfaceProfile.Reset();

    const int32 SceneMatCount = GDsonParser.GetSceneMaterialCount
        ? GDsonParser.GetSceneMaterialCount(DsonHandle) : 0;

    int32 BestSceneMatIdx = INDEX_NONE;
    int32 BestPriority = MAX_int32;
    EDazShaderKind BestKind = EDazShaderKind::Default;

    for (int32 i = 0; i < SceneMatCount; ++i)
    {
        FString GroupName;
        if (!ReadFirstSceneMaterialGroupName(DsonHandle, i, GroupName))
            continue;

        const int32 Priority = GetRepresentativeSkinGroupPriority(GroupName);
        if (Priority == INDEX_NONE || Priority >= BestPriority)
            continue;

        const FSceneMaterialMetadata Metadata = ReadSceneMaterialMetadata(DsonHandle, i);
        const EDazShaderKind Kind = DetectShader(Metadata.Url, Metadata.ShaderType);
        if (Kind != EDazShaderKind::IrayUber && Kind != EDazShaderKind::PBRSkin)
            continue;

        BestSceneMatIdx = i;
        BestPriority = Priority;
        BestKind = Kind;
        if (BestPriority == 0)
            break;
    }

    if (BestSceneMatIdx == INDEX_NONE)
        return nullptr;

    FLinearColor TintColor = FLinearColor::White;
    const bool bHasTintColor = TryReadSkinTintColor(DsonHandle, BestSceneMatIdx, BestKind, TintColor);
    if (!bHasTintColor)
    {
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonMaterialBuilder: no skin tint color found for subsurface profile; using UE skin defaults"));
    }

    const FString AssetName = TEXT("SSP_") + OwnerName;
    const FString PackagePath = OutputFolder / AssetName;

    UPackage* Package = FDsonAssetUtils::CreateLoadedPackage(PackagePath, TEXT("DsonMaterialBuilder"));
    if (!Package)
        return nullptr;

    USubsurfaceProfile* Profile = NewObject<USubsurfaceProfile>(
        Package, *AssetName, RF_Public | RF_Standalone);
    if (!Profile)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonMaterialBuilder: NewObject<USubsurfaceProfile> failed for '%s'"),
            *PackagePath);
        return nullptr;
    }

    Profile->Settings.bEnableBurley = true;
    Profile->Settings.bEnableMeanFreePath = true;
    if (bHasTintColor)
    {
        const FLinearColor ClampedTint = ClampSkinTintColor(TintColor);
        Profile->Settings.MeanFreePathColor = FLinearColor(
            FMath::Lerp(Profile->Settings.MeanFreePathColor.R, ClampedTint.R, kSssTintStrength),
            FMath::Lerp(Profile->Settings.MeanFreePathColor.G, ClampedTint.G, kSssTintStrength),
            FMath::Lerp(Profile->Settings.MeanFreePathColor.B, ClampedTint.B, kSssTintStrength),
            1.0f);
    }
    Profile->Settings.MeanFreePathDistance = kSssMeanFreePathDistance;
    Profile->PostEditChange();

    if (!FDsonAssetUtils::SaveAssetPackage(
            Package, Profile, PackagePath, TEXT("DsonMaterialBuilder")))
    {
        return nullptr;
    }

    CachedSubsurfaceProfile = Profile;
    return Profile;
}

void FDsonMaterialBuilder::ApplySubsurfaceProfileSettings(
    uint64_t DsonHandle,
    int32 SceneMatIdx,
    EDazShaderKind Kind,
    UMaterialInstanceConstant* MIC)
{
    if (Kind != EDazShaderKind::IrayUber && Kind != EDazShaderKind::PBRSkin)
        return;

    FString GroupName;
    if (!ReadFirstSceneMaterialGroupName(DsonHandle, SceneMatIdx, GroupName))
        return;

    const EDsonSurfaceClass SurfaceClass = ClassifySurfaceGroup(GroupName);
    float SubsurfaceWeight = 0.0f;

    if (SurfaceClass == EDsonSurfaceClass::Skin)
    {
        const FDazChannelInfo Weight = FindSceneMaterialChannelById(
            DsonHandle, SceneMatIdx, TEXT("Translucency Weight"), kSkinSubsurfaceWeight);
        SubsurfaceWeight = Weight.Value;
        if (CachedSubsurfaceProfile.IsValid())
        {
            MIC->bOverrideSubsurfaceProfile = true;
            MIC->SubsurfaceProfile = CachedSubsurfaceProfile.Get();
        }
    }
    else if (SurfaceClass == EDsonSurfaceClass::Unknown
        && !WarnedUnknownSubsurfaceGroups.Contains(GroupName))
    {
        WarnedUnknownSubsurfaceGroups.Add(GroupName);
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonMaterialBuilder: unrecognized subsurface surface group '%s' - disabling SSS for this group"),
            *GroupName);
    }

    MIC->SetScalarParameterValueEditorOnly(
        FMaterialParameterInfo(FName(TEXT("SubsurfaceWeight"))),
        SubsurfaceWeight);
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

    // Step 2 - classify surface; choose per-material inputs up front.
    FString GroupName;
    ReadFirstSceneMaterialGroupName(H, SceneMatIdx, GroupName);
    const EDsonSurfaceClass SurfaceClass = ClassifySurfaceGroup(GroupName);
    const bool bIsEyeMoisture = (SurfaceClass == EDsonSurfaceClass::EyeMoisture);

    UMaterial* Master = nullptr;
    const TMap<FString, FDazParamBinding>* Mapping = nullptr;
    FDazChannelSource Src;

    if (bIsEyeMoisture)
    {
        // Eye-moisture uses its own master; channels may live in material_library when the
        // scene url is a bare #fragment (ResolveChannelSource handles the lookup).
        // No SSS and no IrayUber normal/bump pass (pure parametric, no texture maps).
        Master = LoadAndCacheMaster(CachedEyeMoistureMaster, kEyeMoistureMasterPath);
        if (!Master)
        {
            UE_LOG(LogDsonImporter, Error,
                TEXT("DsonMaterialBuilder: aborting eye material '%s' - M_DazEyeMoisture master missing"),
                *Metadata.MatId);
            RecordFailure();
            return nullptr;
        }
        Mapping = &GetEyeMoistureMapping();
        Src = ResolveChannelSource(H, SceneMatIdx, Metadata.Url);
    }
    else
    {
        // Standard path: do NOT substitute a different master on failure.
        Master = LoadMasterForShader(Kind);
        if (!Master)
        {
            UE_LOG(LogDsonImporter, Error,
                TEXT("DsonMaterialBuilder: aborting build for scene material '%s' - master load failed"),
                *Metadata.MatId);
            RecordFailure();
            return nullptr;
        }
        // Default shader has no defined mapping so no parameter overrides are applied.
        Mapping = GetMappingForShader(Kind);
        Src = {false, SceneMatIdx};
    }

    // Single create -> SetParent -> apply channels -> conditional passes -> save tail.
    const FMaterialInstanceAssetContext AssetContext =
        CreateMaterialInstanceAsset(Metadata.MatId, OutputFolder);
    if (!AssetContext.IsValid())
    {
        RecordFailure();
        return nullptr;
    }
    UMaterialInstanceConstant* MIC = AssetContext.MIC;

    MIC->SetParentEditorOnly(Master, /*RecacheShader=*/true);

    if (Mapping)
    {
        ApplyMappedMaterialChannels(H, Src, *Mapping, MIC, TextureImporter);
    }
    // IrayUber normal/bump pass: standard path only (eye-moisture is pure parametric).
    if (!bIsEyeMoisture && Kind == EDazShaderKind::IrayUber)
    {
        ApplyIrayUberNormalChannels(H, SceneMatIdx, MIC, TextureImporter);
    }
    // Apply scene.animations key-0 values after the base pass so they win over placeholders.
    // Guard is the same as the base pass: Default shader (null Mapping) is skipped.
    if (Mapping)
    {
        ApplySceneAnimationOverrides(H, Metadata.MatId, *Mapping, MIC, TextureImporter);
    }
    ImportStandaloneChannelTextures(H, SceneMatIdx, TextureImporter);
    if (!bIsEyeMoisture)
    {
        ApplySubsurfaceProfileSettings(H, SceneMatIdx, Kind, MIC);
    }

    MIC->PostEditChange();

    if (!FDsonAssetUtils::SaveAssetPackage(
            AssetContext.Package, MIC, AssetContext.AssetPath.PackagePath, TEXT("DsonMaterialBuilder")))
    {
        RecordFailure();
        return nullptr;
    }

    ++BuiltCount;
    return MIC;
}

// ---------------------------------------------------------------------------
// BuildAllSceneMaterials
// ---------------------------------------------------------------------------

void FDsonMaterialBuilder::BuildAllSceneMaterials(
    const FString& DufPath,
    const FString& OutputFolder,
    const FString& SspOwnerName,
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

    WarnedUnknownSubsurfaceGroups.Reset();
    BuildSubsurfaceProfileForDocument(H, OutputFolder, SspOwnerName);

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
