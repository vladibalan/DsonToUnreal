#include "DsonImportPipeline.h"
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

FDsonImportResult FDsonImportPipeline::Run(
    const FDsonImportSettings& Settings,
    const TArray<FString>& ContentRoots)
{
    FDsonImportResult Result;
    Result.Settings = Settings;

    FDsonTextureImporter Importer(ContentRoots);
    FDsonMaterialBuilder Builder(ContentRoots, Importer);
    const FString MaterialOutputFolder = TEXT("/Game/DazImports/Materials/") +
        ObjectTools::SanitizeObjectName(FPaths::GetBaseFilename(Settings.DsonFilePath));

    TMap<FString, UMaterialInstanceConstant*> MaterialsByGroup;
    FString UvSetUrl;
    Builder.BuildAllSceneMaterials(Settings.DsonFilePath, MaterialOutputFolder,
        MaterialsByGroup, UvSetUrl);

    FString UvSetAbsPath;
    if (!UvSetUrl.IsEmpty())
    {
        UvSetAbsPath = FDsonContentRoots::ResolveUrl(UvSetUrl, ContentRoots);
        if (UvSetAbsPath.IsEmpty())
        {
            UE_LOG(LogDsonImporter, Warning, TEXT("[uv] failed to resolve UV set URL: %s"), *UvSetUrl);
        }
        else
        {
            UE_LOG(LogDsonImporter, Log, TEXT("[uv] resolved UV set DSF: %s"), *UvSetAbsPath);
        }
    }
    else
    {
        UE_LOG(LogDsonImporter, Warning, TEXT("[uv] no UV set URL found in scene materials"));
    }

    UE_LOG(LogDsonImporter, Log, TEXT("=== DsonTextureImporter summary ==="));
    UE_LOG(LogDsonImporter, Log, TEXT("  Imported:   %d"), Importer.GetImportedCount());
    UE_LOG(LogDsonImporter, Log, TEXT("  Cache hits: %d"), Importer.GetCacheHitCount());
    UE_LOG(LogDsonImporter, Log, TEXT("  Failures:   %d"), Importer.GetFailureCount());
    for (const FString& Url : Importer.GetFailedUrls())
    {
        UE_LOG(LogDsonImporter, Warning, TEXT("    failed: %s"), *Url);
    }
    UE_LOG(LogDsonImporter, Log, TEXT("=== DsonMaterialBuilder summary ==="));
    UE_LOG(LogDsonImporter, Log, TEXT("  Built:     %d"), Builder.GetBuiltCount());
    UE_LOG(LogDsonImporter, Log, TEXT("  Failures:  %d"), Builder.GetFailureCount());
    UE_LOG(LogDsonImporter, Log, TEXT("  Iray Uber: %d"), Builder.GetIrayUberCount());
    UE_LOG(LogDsonImporter, Log, TEXT("  PBRSkin:   %d"), Builder.GetPBRSkinCount());
    UE_LOG(LogDsonImporter, Log, TEXT("  Default:   %d"), Builder.GetDefaultCount());
    UE_LOG(LogDsonImporter, Log, TEXT("  Mapped:    %d groups"), MaterialsByGroup.Num());

    if (Settings.bDumpMaterialDiagnostics)
    {
        FDsonMaterialDiagnostic::Dump(Settings, Importer, MaterialOutputFolder);
    }

    UMaterial* DefaultMaterial = LoadObject<UMaterial>(
        nullptr, TEXT("/DsonToUnreal/Materials/M_DazDefault.M_DazDefault"));
    if (!DefaultMaterial)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("Failed to load M_DazDefault master at /DsonToUnreal/Materials/M_DazDefault - aborting import"));
        Result.bAbortedBeforeAssetBuild = true;
        return Result;
    }

    Result.Skeleton = FDsonSkeletonBuilder::Build(Settings);
    if (Result.Skeleton)
    {
        Result.Mesh = FDsonMeshBuilder::Build(
            Settings, Result.Skeleton, MaterialsByGroup, DefaultMaterial, UvSetAbsPath);
    }

    return Result;
}
