#include "DsonTextureImporter.h"
#include "DsonImporter.h"
#include "DsonContentRoots.h"

#include "Engine/Texture2D.h"
#include "Factories/TextureFactory.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ObjectTools.h"

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

FDsonTextureImporter::FDsonTextureImporter(const TArray<FString>& InContentRoots)
    : ContentRoots(InContentRoots)
{
}

// ---------------------------------------------------------------------------
// DeriveRelativeSubpath
// ---------------------------------------------------------------------------

FString FDsonTextureImporter::DeriveRelativeSubpath(
    const FString& ImageUrl, const FString& ResolvedAbsPath) const
{
    // Strip fragment after '#', matching the behaviour of FDsonContentRoots::ResolveUrl
    FString Url = ImageUrl;
    int32 HashIndex = INDEX_NONE;
    if (Url.FindChar(TEXT('#'), HashIndex))
        Url = Url.Left(HashIndex);

    if (Url.StartsWith(TEXT("/")))
    {
        // Full DSON URL: the URL-decoded, slash-stripped form is the content-relative subpath
        return FDsonContentRoots::UrlDecode(Url).RightChop(1);
    }

    // Bare filename: strip the matched content root prefix from the resolved absolute path
    FString NormalizedAbs = ResolvedAbsPath;
    FPaths::NormalizeFilename(NormalizedAbs);

    for (const FString& Root : ContentRoots)
    {
        FString NormalizedRoot = Root;
        FPaths::NormalizeFilename(NormalizedRoot);
        if (!NormalizedRoot.EndsWith(TEXT("/")))
            NormalizedRoot += TEXT("/");

        if (NormalizedAbs.StartsWith(NormalizedRoot, ESearchCase::IgnoreCase))
            return NormalizedAbs.RightChop(NormalizedRoot.Len());
    }

    // Last resort: use just the clean filename
    return FPaths::GetCleanFilename(ResolvedAbsPath);
}

// ---------------------------------------------------------------------------
// ImportOrFind
// ---------------------------------------------------------------------------

UTexture2D* FDsonTextureImporter::ImportOrFind(const FString& ImageUrl, bool bSRGB)
{
    // 1. Resolve URL to an absolute filesystem path
    const FString ResolvedPath = FDsonContentRoots::ResolveUrl(ImageUrl, ContentRoots);
    if (ResolvedPath.IsEmpty())
    {
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonTextureImporter: could not resolve '%s' in %d content root(s)"),
            *ImageUrl, ContentRoots.Num());
        ++FailureCount;
        FailedUrls.Add(ImageUrl);
        return nullptr;
    }

    // 2. Cache lookup — key is the resolved absolute path so two different URL spellings
    //    that resolve to the same file share one cache entry
    if (TObjectPtr<UTexture2D>* Found = Cache.Find(ResolvedPath))
    {
        UTexture2D* CachedTex = Found->Get();
        if (CachedTex && (CachedTex->SRGB != 0) != bSRGB)
        {
            UE_LOG(LogDsonImporter, Warning,
                TEXT("DsonTextureImporter: sRGB conflict for '%s' — cached=%d requested=%d; returning cached unmodified"),
                *ImageUrl, (int32)CachedTex->SRGB, (int32)bSRGB);
        }
        ++CacheHitCount;
        UE_LOG(LogDsonImporter, VeryVerbose,
            TEXT("DsonTextureImporter: cache hit '%s'"), *ResolvedPath);
        return CachedTex;
    }

    // 3. Derive the UE asset path
    //    Source:  D:/Daz_content/Runtime/Textures/Genesis8/Female/G8FBase.jpg
    //    Subpath: Runtime/Textures/Genesis8/Female/G8FBase.jpg
    //    Result:  /Game/DazImports/Textures/Runtime/Textures/Genesis8/Female/T_G8FBase
    const FString RelSubpath   = DeriveRelativeSubpath(ImageUrl, ResolvedPath);
    const FString RelDir       = FPaths::GetPath(RelSubpath);
    const FString BaseFilename = FPaths::GetBaseFilename(RelSubpath);
    const FString Extension    = FPaths::GetExtension(RelSubpath);

    FString SanitizedDir;
    if (!RelDir.IsEmpty())
    {
        TArray<FString> DirParts;
        RelDir.ParseIntoArray(DirParts, TEXT("/"), /*bCullEmpty=*/true);
        TArray<FString> SanitizedParts;
        SanitizedParts.Reserve(DirParts.Num());
        for (const FString& Part : DirParts)
            SanitizedParts.Add(ObjectTools::SanitizeObjectName(Part));
        SanitizedDir = FString::Join(SanitizedParts, TEXT("/"));
    }

    const FString AssetName = TEXT("T_") + ObjectTools::SanitizeObjectName(BaseFilename);

    FString PackagePath = TEXT("/Game/DazImports/Textures/");
    if (!SanitizedDir.IsEmpty())
        PackagePath += SanitizedDir + TEXT("/");
    PackagePath += AssetName;

    // 4. If a .uasset already exists at the derived path, load and return it without
    //    re-importing — preserving any user-side tweaks made since the last import
    if (FPackageName::DoesPackageExist(PackagePath))
    {
        UTexture2D* Existing = LoadObject<UTexture2D>(nullptr, *PackagePath, nullptr, LOAD_NoWarn);
        if (Existing)
        {
            Cache.Add(ResolvedPath, Existing);
            ++CacheHitCount;
            UE_LOG(LogDsonImporter, VeryVerbose,
                TEXT("DsonTextureImporter: found existing asset '%s'"), *PackagePath);
            return Existing;
        }
    }

    // 5. Read source file bytes
    TArray<uint8> FileBytes;
    if (!FFileHelper::LoadFileToArray(FileBytes, *ResolvedPath))
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonTextureImporter: failed to read file '%s'"), *ResolvedPath);
        ++FailureCount;
        FailedUrls.Add(ImageUrl);
        return nullptr;
    }

    // 6. Create the package
    UPackage* Package = CreatePackage(*PackagePath);
    if (!Package)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonTextureImporter: failed to create package '%s'"), *PackagePath);
        ++FailureCount;
        FailedUrls.Add(ImageUrl);
        return nullptr;
    }
    Package->FullyLoad();

    // 7. Import via UTextureFactory — gives us explicit control over the package and
    //    asset name; the factory applies its own heuristics (e.g. normal map detection
    //    from the filename) and handles all formats it natively supports
    UTextureFactory* Factory = NewObject<UTextureFactory>();
    Factory->bCreateMaterial = false;

    const uint8* BufferStart = FileBytes.GetData();
    const uint8* BufferEnd   = BufferStart + FileBytes.Num();

    UObject* ImportedObj = Factory->FactoryCreateBinary(
        UTexture2D::StaticClass(),
        Package,
        *AssetName,
        RF_Public | RF_Standalone,
        nullptr,
        *Extension,
        BufferStart,
        BufferEnd,
        GWarn);

    UTexture2D* Texture = Cast<UTexture2D>(ImportedObj);
    if (!Texture)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonTextureImporter: FactoryCreateBinary failed for '%s'"), *ResolvedPath);
        ++FailureCount;
        FailedUrls.Add(ImageUrl);
        return nullptr;
    }

    // 8. Apply caller-supplied sRGB flag; the caller knows the channel kind
    Texture->SRGB = bSRGB;
    Texture->UpdateResource();

    Package->MarkPackageDirty();

    const FString FileName = FPackageName::LongPackageNameToFilename(
        PackagePath, FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags       = RF_Public | RF_Standalone;
    SaveArgs.Error               = GError;
    SaveArgs.bWarnOfLongFilename = false;
    UPackage::SavePackage(Package, Texture, *FileName, SaveArgs);

    FAssetRegistryModule::GetRegistry().AssetCreated(Texture);

    Cache.Add(ResolvedPath, Texture);
    ++ImportedCount;

    UE_LOG(LogDsonImporter, Verbose,
        TEXT("DsonTextureImporter: imported '%s' from '%s' (sRGB=%d)"),
        *PackagePath, *ResolvedPath, (int32)bSRGB);

    return Texture;
}