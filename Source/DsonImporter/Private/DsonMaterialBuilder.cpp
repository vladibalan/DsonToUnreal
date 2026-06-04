#include "DsonMaterialBuilder.h"
#include "DsonImporter.h"
#include "DsonAssetUtils.h"
#include "DsonParserFunctions.h"
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
// FDazParamBinding — file-private parameter mapping descriptor
// ---------------------------------------------------------------------------

struct FDazParamBinding
{
    FName ColorParam;    // SetVectorParameterValueEditorOnly; NAME_None if this channel has no color
    FName ScalarParam;   // SetScalarParameterValueEditorOnly; NAME_None if this channel has no scalar
    FName TextureParam;  // SetTextureParameterValueEditorOnly; NAME_None if no texture slot
    FName UseFlag;       // Scalar Use*Map flag set to 1.0f when a texture is applied; NAME_None if N/A
    bool  bSRGB;         // Passed to FDsonTextureImporter::ImportOrFind
};

// ---------------------------------------------------------------------------
// Mapping tables — initialized once, returned by const reference
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
        M.Add(TEXT("Bump Strength"),
            { NAME_None,                        FName(TEXT("BumpStrength")),    FName(TEXT("BumpMap")),          FName(TEXT("UseBumpMap")),          false });
        M.Add(TEXT("Top Coat Weight"),
            { NAME_None,                        FName(TEXT("TopCoatWeight")),   FName(TEXT("TopCoatMap")),       FName(TEXT("UseTopCoatMap")),       false });
        M.Add(TEXT("Top Coat Color"),
            { FName(TEXT("TopCoatColor")),      NAME_None,                      FName(TEXT("TopCoatColorMap")),  FName(TEXT("UseTopCoatColorMap")),  true  });
        M.Add(TEXT("Normal Map"),
            { NAME_None,                        FName(TEXT("NormalStrength")),  FName(TEXT("NormalMap")),        FName(TEXT("UseNormalMap")),        false });
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

// ---------------------------------------------------------------------------
// Helper: nullable const char* → FString (same pattern as the diagnostic)
// ---------------------------------------------------------------------------

static FString S(const char* Raw)
{
    // Parser string pointers are transient; convert immediately before another parser call.
    return Raw ? FString(UTF8_TO_TCHAR(Raw)) : TEXT("");
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
    // URL wins — PBRSkin's external .dsf reference is unambiguous
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
                 "— defaulting to IrayUber. Url='%s'"), *Url);
        return EDazShaderKind::IrayUber;
    }

    UE_LOG(LogDsonImporter, Warning,
        TEXT("DsonMaterialBuilder: unknown shader_type='%s' url='%s' — using M_DazDefault"),
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

    // Step 1 — read id / url / shader_type; detect shader; increment per-shader counter
    FString MatId = S(GDsonParser.GetSceneMaterialId
        ? GDsonParser.GetSceneMaterialId(H, SceneMatIdx) : nullptr);
    FString Url = S(GDsonParser.GetSceneMaterialUrl
        ? GDsonParser.GetSceneMaterialUrl(H, SceneMatIdx) : nullptr);
    FString ShaderType = S(GDsonParser.GetSceneMaterialShaderType
        ? GDsonParser.GetSceneMaterialShaderType(H, SceneMatIdx) : nullptr);

    const EDazShaderKind Kind = DetectShader(Url, ShaderType);

    switch (Kind)
    {
        case EDazShaderKind::IrayUber: ++IrayUberCount; break;
        case EDazShaderKind::PBRSkin:  ++PBRSkinCount;  break;
        case EDazShaderKind::Default:  ++DefaultCount;  break;
    }

    // Step 2 — load master; do NOT substitute a different master on failure
    UMaterial* Master = LoadMasterForShader(Kind);
    if (!Master)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonMaterialBuilder: aborting build for scene material '%s' — master load failed"),
            *MatId);
        ++FailureCount;
        return nullptr;
    }

    // Step 3 — sanitize id, derive asset name and package path
    const FString SanitizedId = ObjectTools::SanitizeObjectName(MatId);
    const FString AssetName   = TEXT("MI_") + SanitizedId;
    const FString PackagePath = OutputFolder / AssetName;

    // Step 4 — create and fully load package
    UPackage* Package = FDsonAssetUtils::CreateLoadedPackage(PackagePath, TEXT("DsonMaterialBuilder"));
    if (!Package)
    {
        ++FailureCount;
        return nullptr;
    }

    // Step 5 — create MIC
    UMaterialInstanceConstant* MIC = NewObject<UMaterialInstanceConstant>(
        Package, *AssetName, RF_Public | RF_Standalone);
    if (!MIC)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonMaterialBuilder: NewObject<UMaterialInstanceConstant> failed for '%s'"),
            *PackagePath);
        ++FailureCount;
        return nullptr;
    }

    // Step 6 — assign parent
    MIC->Parent = Master;

    // Step 7 — iterate channels and apply parameters from the active mapping table.
    // Default shader has no defined mapping so no parameter overrides are applied.
    const TMap<FString, FDazParamBinding>* MappingPtr = nullptr;
    if (Kind == EDazShaderKind::IrayUber) MappingPtr = &GetIrayUberMapping();
    else if (Kind == EDazShaderKind::PBRSkin)  MappingPtr = &GetPBRSkinMapping();

    if (MappingPtr)
    {
        const int32 ChCount = GDsonParser.GetSceneMaterialChannelCount
            ? GDsonParser.GetSceneMaterialChannelCount(H, SceneMatIdx) : 0;

        for (int32 c = 0; c < ChCount; ++c)
        {
            FString ChId = S(GDsonParser.GetSceneMaterialChannelId
                ? GDsonParser.GetSceneMaterialChannelId(H, SceneMatIdx, c) : nullptr);

            const FDazParamBinding* Binding = MappingPtr->Find(ChId);
            if (!Binding)
                continue;

            const bool bHasColor = GDsonParser.GetSceneMaterialChannelHasColor
                ? GDsonParser.GetSceneMaterialChannelHasColor(H, SceneMatIdx, c) : false;

            // Color or scalar — both get applied even when a texture is also present.
            // The master multiplies the constant by the texture sample when UseFlag=1.
            if (bHasColor && Binding->ColorParam != NAME_None)
            {
                const double R = GDsonParser.GetSceneMaterialChannelColorR
                    ? GDsonParser.GetSceneMaterialChannelColorR(H, SceneMatIdx, c) : 0.0;
                const double G = GDsonParser.GetSceneMaterialChannelColorG
                    ? GDsonParser.GetSceneMaterialChannelColorG(H, SceneMatIdx, c) : 0.0;
                const double B = GDsonParser.GetSceneMaterialChannelColorB
                    ? GDsonParser.GetSceneMaterialChannelColorB(H, SceneMatIdx, c) : 0.0;
                MIC->SetVectorParameterValueEditorOnly(
                    FMaterialParameterInfo(Binding->ColorParam),
                    FLinearColor((float)R, (float)G, (float)B, 1.0f));
            }
            else if (!bHasColor && Binding->ScalarParam != NAME_None)
            {
                const double Val = GDsonParser.GetSceneMaterialChannelValue
                    ? GDsonParser.GetSceneMaterialChannelValue(H, SceneMatIdx, c) : 0.0;
                MIC->SetScalarParameterValueEditorOnly(
                    FMaterialParameterInfo(Binding->ScalarParam),
                    (float)Val);
            }

            // Texture + UseFlag — orthogonal to color/scalar, applied when an image url exists
            if (Binding->TextureParam != NAME_None)
            {
                FString ImgUrl = S(GDsonParser.GetSceneMaterialChannelImageUrl
                    ? GDsonParser.GetSceneMaterialChannelImageUrl(H, SceneMatIdx, c) : nullptr);
                if (!ImgUrl.IsEmpty())
                {
                    UTexture2D* Tex = TextureImporter.ImportOrFind(ImgUrl, Binding->bSRGB);
                    if (Tex)
                    {
                        MIC->SetTextureParameterValueEditorOnly(
                            FMaterialParameterInfo(Binding->TextureParam), Tex);
                        if (Binding->UseFlag != NAME_None)
                        {
                            MIC->SetScalarParameterValueEditorOnly(
                                FMaterialParameterInfo(Binding->UseFlag), 1.0f);
                        }
                    }
                }
            }
        }
    }

    // Step 8 — finalise parameter override arrays before save
    MIC->PostEditChange();

    // Step 9 — save (mirrors DsonTextureImporter exactly)
    if (!FDsonAssetUtils::SaveAssetPackage(Package, MIC, PackagePath, TEXT("DsonMaterialBuilder")))
    {
        ++FailureCount;
        return nullptr;
    }

    // Step 10 — notify asset registry
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

        const int32 GroupCount = GDsonParser.GetSceneMaterialGroupCount
            ? GDsonParser.GetSceneMaterialGroupCount(H, i) : 0;

        const char* NameRaw = (GroupCount > 0 && GDsonParser.GetSceneMaterialGroupName)
            ? GDsonParser.GetSceneMaterialGroupName(H, i, 0) : nullptr;

        if (OutUvSetUrl.IsEmpty() && GDsonParser.GetSceneMaterialUVSetId)
        {
            if (const char* UvSetRaw = GDsonParser.GetSceneMaterialUVSetId(H, i))
            {
                const FString Candidate = UTF8_TO_TCHAR(UvSetRaw);
                if (!Candidate.IsEmpty())
                {
                    OutUvSetUrl = Candidate;
                }
            }
        }

        if (GroupCount == 0 || !NameRaw)
        {
            FString SceneId = S(GDsonParser.GetSceneMaterialId
                ? GDsonParser.GetSceneMaterialId(H, i) : nullptr);
            UE_LOG(LogDsonImporter, Warning,
                TEXT("[mat] scene material '%s' has no mappable group — MIC built but not wired"), *SceneId);
            continue;
        }

        const FString GroupName = UTF8_TO_TCHAR(NameRaw);

        if (OutByGroup.Contains(GroupName))
        {
            UE_LOG(LogDsonImporter, Warning,
                TEXT("[mat] duplicate group \"%s\" — later MIC wins"), *GroupName);
        }

        OutByGroup.Add(GroupName, MIC);
    }
}
