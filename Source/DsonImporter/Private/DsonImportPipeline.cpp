#include "DsonImportPipeline.h"
#include "DsonAssetUtils.h"
#include "DsonContentRoots.h"
#include "DsonImporter.h"
#include "DsonMaterialBuilder.h"
#include "DsonMaterialDiagnostic.h"
#include "DsonMeshBuilder.h"
#include "DsonMorphBuilder.h"
#include "DsonRecipeBuilder.h"
#include "DsonSkeletonBuilder.h"
#include "DsonTextureImporter.h"

#include "Animation/MorphTarget.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/Paths.h"

static const TCHAR* kDefaultFallbackMaterialObjectPath =
    TEXT("/DsonToUnreal/Materials/M_DazDefault.M_DazDefault");
static const TCHAR* kDefaultFallbackMaterialPackagePath =
    TEXT("/DsonToUnreal/Materials/M_DazDefault");

static FString MakeBodyMaterialFolder(const FString& CharacterName)
{
    return FDsonAssetUtils::CharacterRoot(CharacterName) / TEXT("Materials");
}

static FString MakeCompanionMaterialFolder(
    const FString& CharacterName, const FString& CompanionAssetName)
{
    return FDsonAssetUtils::CharacterRoot(CharacterName) / TEXT("Materials") / CompanionAssetName;
}

static FString ResolveUvSetDsfPath(const FString& UvSetUrl, const TArray<FString>& ContentRoots)
{
    if (UvSetUrl.IsEmpty())
    {
        UE_LOG(LogDsonImporter, Warning, TEXT("[uv] no UV set URL found in scene materials"));
        return TEXT("");
    }

    const FString UvSetAbsPath = FDsonContentRoots::ResolveUrl(UvSetUrl, ContentRoots);
    if (UvSetAbsPath.IsEmpty())
    {
        UE_LOG(LogDsonImporter, Warning, TEXT("[uv] failed to resolve UV set URL: %s"), *UvSetUrl);
    }
    else
    {
        UE_LOG(LogDsonImporter, Log, TEXT("[uv] resolved UV set DSF: %s"), *UvSetAbsPath);
    }

    return UvSetAbsPath;
}

static void LogTextureImportSummary(const FDsonTextureImporter& Importer)
{
    UE_LOG(LogDsonImporter, Log, TEXT("=== DsonTextureImporter summary ==="));
    UE_LOG(LogDsonImporter, Log, TEXT("  Imported:   %d"), Importer.GetImportedCount());
    UE_LOG(LogDsonImporter, Log, TEXT("  Cache hits: %d"), Importer.GetCacheHitCount());
    UE_LOG(LogDsonImporter, Log, TEXT("  Failures:   %d"), Importer.GetFailureCount());
    for (const FString& Url : Importer.GetFailedUrls())
    {
        UE_LOG(LogDsonImporter, Warning, TEXT("    failed: %s"), *Url);
    }
}

static void LogMaterialBuildSummary(
    const FDsonMaterialBuilder& Builder,
    const TMap<FString, UMaterialInstanceConstant*>& MaterialsByGroup)
{
    UE_LOG(LogDsonImporter, Log, TEXT("=== DsonMaterialBuilder summary ==="));
    UE_LOG(LogDsonImporter, Log, TEXT("  Built:     %d"), Builder.GetBuiltCount());
    UE_LOG(LogDsonImporter, Log, TEXT("  Failures:  %d"), Builder.GetFailureCount());
    UE_LOG(LogDsonImporter, Log, TEXT("  Iray Uber: %d"), Builder.GetIrayUberCount());
    UE_LOG(LogDsonImporter, Log, TEXT("  PBRSkin:   %d"), Builder.GetPBRSkinCount());
    UE_LOG(LogDsonImporter, Log, TEXT("  Default:   %d"), Builder.GetDefaultCount());
    UE_LOG(LogDsonImporter, Log, TEXT("  Mapped:    %d groups"), MaterialsByGroup.Num());
}

static UMaterial* LoadDefaultFallbackMaterial()
{
    UMaterial* DefaultMaterial = LoadObject<UMaterial>(
        nullptr, kDefaultFallbackMaterialObjectPath);
    if (!DefaultMaterial)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("Failed to load M_DazDefault master at %s - aborting import"),
            kDefaultFallbackMaterialPackagePath);
    }

    return DefaultMaterial;
}

FDsonImportResult FDsonImportPipeline::Run(
    const FDsonImportSettings& Settings,
    const TArray<FString>& ContentRoots)
{
    FDsonImportResult Result;

    if (!Settings.FigureId.IsEmpty())
        UE_LOG(LogDsonImporter, Log,
            TEXT("[figure] '%s': FigureId set; parent-first layered import path"),
            *Settings.FigureId);

    FDsonTextureImporter Importer(ContentRoots, Settings.CharacterName);
    FDsonMaterialBuilder Builder(ContentRoots, Importer);
    const FString MaterialOutputFolder = MakeBodyMaterialFolder(Settings.CharacterName);

    TMap<FString, UMaterialInstanceConstant*> MaterialsByGroup;
    FString UvSetUrl;
    Builder.BuildAllSceneMaterials(Settings.DsonFilePath, MaterialOutputFolder,
        Settings.CharacterName, MaterialsByGroup, UvSetUrl);

    const FString UvSetAbsPath = ResolveUvSetDsfPath(UvSetUrl, ContentRoots);

    LogTextureImportSummary(Importer);
    LogMaterialBuildSummary(Builder, MaterialsByGroup);

    if (Settings.bDumpMaterialDiagnostics)
    {
        FDsonMaterialDiagnostic::Dump(Settings, Importer);
    }

    UMaterial* DefaultMaterial = LoadDefaultFallbackMaterial();
    if (!DefaultMaterial)
    {
        Result.bAbortedBeforeAssetBuild = true;
        return Result;
    }

    if (!Settings.FigureId.IsEmpty())
    {
        // === Layered import path (FigureId set): parent-first, delta body, shared skeleton ===

        // H5 cache warm-up: ApplyFigureOwned (inside BuildParent) reads
        // Settings.DiscoveredCorrectiveDsfPaths; under the parent-first order that cache
        // is empty on first import since Apply (which fills it) would otherwise run after.
        // DiscoverFormulaReachableDocuments calls ScanAndEnqueueCorrectives which populates
        // the cache as a side-effect; the subsequent delta-body Apply then hits the M1 guard
        // (DsonMorphBuilder.cpp L227) and skips the re-scan — no double scan.
        {
            TArray<FDsonLoadedDocument> WarmDocs;
            TArray<uint64_t> WarmHandles;
            FDsonMorphBuilder::DiscoverFormulaReachableDocuments(Settings, WarmDocs, WarmHandles);
        }

        // Ensure the shared parent figure asset exists (lazy build, first import only;
        // no-overwrite: FigureImportComplete checks the completeness marker recipe).
        if (!FDsonAssetUtils::FigureImportComplete(Settings.FigureId))
        {
            UE_LOG(LogDsonImporter, Log,
                TEXT("[figure] '%s': parent absent; building now"), *Settings.FigureId);
            USkeleton* BuiltParentSkeleton = FDsonSkeletonBuilder::BuildParent(Settings);
            if (BuiltParentSkeleton)
            {
                USkeletalMesh* BuiltParentMesh = FDsonMeshBuilder::BuildParent(
                    Settings, BuiltParentSkeleton, DefaultMaterial, UvSetAbsPath);
                if (BuiltParentMesh)
                    FDsonRecipeBuilder::BuildParentMarker(
                        Settings, BuiltParentSkeleton, BuiltParentMesh);
                else
                    UE_LOG(LogDsonImporter, Warning,
                        TEXT("[figure] '%s': parent build incomplete — mesh null; completeness marker not emitted"),
                        *Settings.FigureId);
            }
            else
                UE_LOG(LogDsonImporter, Warning,
                    TEXT("[figure] '%s': parent build incomplete — skeleton null; completeness marker not emitted"),
                    *Settings.FigureId);
        }

        // Load parent assets — uniform path for both just-built this run and pre-existing.
        const FString ParentSkelPkg = FDsonAssetUtils::FigureRoot(Settings.FigureId) /
            (Settings.FigureId + TEXT("_Skeleton"));
        const FString ParentMeshPkg = FDsonAssetUtils::FigureRoot(Settings.FigureId) /
            (Settings.FigureId + TEXT("_SkeletalMesh"));
        USkeleton*     ParentSkeleton = LoadObject<USkeleton>(nullptr,
            *(ParentSkelPkg + TEXT(".") + Settings.FigureId + TEXT("_Skeleton")));
        USkeletalMesh* ParentMesh     = LoadObject<USkeletalMesh>(nullptr,
            *(ParentMeshPkg + TEXT(".") + Settings.FigureId + TEXT("_SkeletalMesh")));

        // H1: hard abort when parent is unavailable on the FigureId-set path.
        // R7 exception (H1): user-directed hard fail; R7 revision pending.
        if (!ParentSkeleton || !ParentMesh)
        {
            UE_LOG(LogDsonImporter, Error,
                TEXT("[figure] '%s': parent assets unavailable after build/load attempt "
                     "(skeleton=%s mesh=%s) — aborting character import"),
                *Settings.FigureId,
                ParentSkeleton ? TEXT("ok") : TEXT("null"),
                ParentMesh     ? TEXT("ok") : TEXT("null"));
            Result.bAbortedBeforeAssetBuild = true;
            return Result;
        }

        // Collect parent morph names (lowercased) for the delta exclusion set.
        // FDsonMorphBuilder::Apply pre-seeds SeenMorphNames from this so shared figure
        // morphs are not re-emitted on the delta mesh (S3 morph partition rule).
        for (const TObjectPtr<UMorphTarget>& MT : ParentMesh->GetMorphTargets())
            Settings.DeltaMorphExclusionKeysLower.Add(MT->GetFName().ToString().ToLower());

        UE_LOG(LogDsonImporter, Log,
            TEXT("[figure] '%s': parent loaded — %d morph(s) excluded from delta"),
            *Settings.FigureId, Settings.DeltaMorphExclusionKeysLower.Num());

        // Delta body mesh: binds the shared ParentSkeleton; morphs exclude parent-owned set.
        // No per-character _Skeleton emitted on this path (step 4).
        Result.Skeleton = ParentSkeleton;
        Result.Mesh = FDsonMeshBuilder::Build(
            Settings, ParentSkeleton, MaterialsByGroup, DefaultMaterial, UvSetAbsPath);
    }
    else
    {
        // === Legacy path (FigureId empty): per-character skeleton + full morph set ===
        Result.Skeleton = FDsonSkeletonBuilder::Build(Settings);
        if (Result.Skeleton)
        {
            Result.Mesh = FDsonMeshBuilder::Build(
                Settings, Result.Skeleton, MaterialsByGroup, DefaultMaterial, UvSetAbsPath);
        }
    }

    // Companions bind Result.Skeleton — ParentSkeleton on the layered path,
    // per-character skeleton on the legacy path. Shared loop; no duplication (R4).
    if (Result.Skeleton)
    {
        for (const FDsonCompanionSource& Companion : Settings.CompanionFigures)
        {
            if (Companion.GeometryDsfUrl.IsEmpty())
            {
                UE_LOG(LogDsonImporter, Warning,
                    TEXT("[companion] skipping '%s': no geometry DSF"), *Companion.AssetName);
                continue;
            }

            TMap<FString, UMaterialInstanceConstant*> CompanionMICs;
            FString CompanionUvSetUrl;
            if (!Companion.MatPresetPath.IsEmpty())
            {
                const FString CompanionMatFolder =
                    MakeCompanionMaterialFolder(Settings.CharacterName, Companion.AssetName);
                const FString SspOwnerName =
                    Settings.CharacterName + TEXT("_") + Companion.AssetName;
                Builder.BuildAllSceneMaterials(
                    Companion.MatPresetPath, CompanionMatFolder, SspOwnerName,
                    CompanionMICs, CompanionUvSetUrl);
                UE_LOG(LogDsonImporter, Log,
                    TEXT("[companion-mat] %s: %d MIC(s) from '%s'"),
                    *Companion.AssetName, CompanionMICs.Num(),
                    *FPaths::GetCleanFilename(Companion.MatPresetPath));
                if (CompanionMICs.Num() == 0)
                {
                    UE_LOG(LogDsonImporter, Warning,
                        TEXT("[companion-mat] %s: 0 MICs built — preset has no scene materials or all failed; sections use M_DazDefault"),
                        *Companion.AssetName);
                }
            }

            const FString CompanionUvSetAbsPath = ResolveUvSetDsfPath(CompanionUvSetUrl, ContentRoots);
            USkeletalMesh* CompanionMesh = FDsonMeshBuilder::BuildCompanion(
                Settings.CharacterName, Companion.AssetName, Companion.GeometryDsfUrl,
                Result.Skeleton, CompanionMICs, DefaultMaterial, CompanionUvSetAbsPath);
            if (CompanionMesh)
            {
                Result.CompanionMeshes.Add(CompanionMesh);
                Result.CompanionSlots.Add(Companion.Slot);
            }
            else
                UE_LOG(LogDsonImporter, Warning,
                    TEXT("[companion] skipped (build failed): %s"), *Companion.AssetName);
        }
    }

    // Plumb pre-baked composites into Result so the recipe builder can set bImporterPreBaked markers.
    // R4: do not re-derive the bake decision — record what CompositeImageLayers actually did.
    for (const auto& Pair : Importer.GetPreBakedComposites())
        Result.PreBakedComposites.Add(Pair.Key, TSoftObjectPtr<UTexture2D>(Pair.Value.Get()));

    // Copy Settings into Result now so DiscoveredCorrectiveDsfPaths (M1 cache) and
    // DeltaMorphExclusionKeysLower are captured before the recipe builder re-uses them.
    Result.Settings = Settings;

    // Emit recipe asset after all meshes are built (R7 additive: never aborts the import)
    FDsonRecipeBuilder::Build(Result);

    return Result;
}
