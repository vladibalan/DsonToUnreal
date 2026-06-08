#include "DsonImportPipeline.h"
#include "DsonAssetUtils.h"
#include "DsonContentRoots.h"
#include "DsonImporter.h"
#include "DsonMaterialBuilder.h"
#include "DsonMaterialDiagnostic.h"
#include "DsonMeshBuilder.h"
#include "DsonSkeletonBuilder.h"
#include "DsonTextureImporter.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"

static const TCHAR* kDefaultFallbackMaterialObjectPath =
    TEXT("/DsonToUnreal/Materials/M_DazDefault.M_DazDefault");
static const TCHAR* kDefaultFallbackMaterialPackagePath =
    TEXT("/DsonToUnreal/Materials/M_DazDefault");

static FString MakeMaterialOutputFolder(const FString& DsonFilePath)
{
    return FDsonAssetUtils::MakeImportSubfolderPath(
        TEXT("Materials"),
        ObjectTools::SanitizeObjectName(FPaths::GetBaseFilename(DsonFilePath)));
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
    Result.Settings = Settings;

    FDsonTextureImporter Importer(ContentRoots);
    FDsonMaterialBuilder Builder(ContentRoots, Importer);
    const FString MaterialOutputFolder = MakeMaterialOutputFolder(Settings.DsonFilePath);

    TMap<FString, UMaterialInstanceConstant*> MaterialsByGroup;
    FString UvSetUrl;
    Builder.BuildAllSceneMaterials(Settings.DsonFilePath, MaterialOutputFolder,
        MaterialsByGroup, UvSetUrl);

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

    Result.Skeleton = FDsonSkeletonBuilder::Build(Settings);
    if (Result.Skeleton)
    {
        Result.Mesh = FDsonMeshBuilder::Build(
            Settings, Result.Skeleton, MaterialsByGroup, DefaultMaterial, UvSetAbsPath);

        for (const FDsonCompanionSource& Companion : Settings.CompanionFigures)
        {
            if (Companion.GeometryDsfUrl.IsEmpty())
            {
                UE_LOG(LogDsonImporter, Warning,
                    TEXT("[companion] skipping '%s': no geometry DSF"), *Companion.AssetName);
                continue;
            }

            TMap<FString, UMaterialInstanceConstant*> CompanionMICs;
            if (!Companion.MatPresetPath.IsEmpty())
            {
                const FString CompanionMatFolder = MakeMaterialOutputFolder(Companion.MatPresetPath);
                FString CompanionUvSetUrl;
                Builder.BuildAllSceneMaterials(
                    Companion.MatPresetPath, CompanionMatFolder, CompanionMICs, CompanionUvSetUrl);
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

            USkeletalMesh* CompanionMesh = FDsonMeshBuilder::BuildCompanion(
                Companion.AssetName, Companion.GeometryDsfUrl, Result.Skeleton,
                CompanionMICs, DefaultMaterial);
            if (CompanionMesh)
                Result.CompanionMeshes.Add(CompanionMesh);
            else
                UE_LOG(LogDsonImporter, Warning,
                    TEXT("[companion] skipped (build failed): %s"), *Companion.AssetName);
        }
    }

    return Result;
}
