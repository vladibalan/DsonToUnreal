#include "DsonTextureImporter.h"
#include "DsonImporter.h"
#include "DsonAssetUtils.h"
#include "DsonContentRoots.h"
#include "DsonImportUtils.h"

#include "Engine/Texture2D.h"
#include "Factories/TextureFactory.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"

/*
 * Intent:
 * - Resolve DAZ image URLs and import/reuse matching UTexture2D assets.
 * - Preserve DAZ relative folder structure under /Game/DazImports/Textures.
 * - Cache successful imports by absolute source path, sRGB mode, and optional asset suffix.
 *
 * Read this file for texture path resolution, package naming, sRGB flags, and image import failures.
 */

struct FTextureAssetPath
{
    FString AssetName;
    FString Extension;
    FString PackagePath;
};

struct FDecodedImage
{
    int32 Width = 0;
    int32 Height = 0;
    TArray<FColor> Pixels;

    bool IsValid() const { return Width > 0 && Height > 0 && Pixels.Num() == Width * Height; }
};

static float ByteToUnit(uint8 Value)
{
    return static_cast<float>(Value) / 255.0f;
}

static FString SanitizePackageSubdir(const FString& RelDir)
{
    if (RelDir.IsEmpty())
        return TEXT("");

    TArray<FString> DirParts;
    RelDir.ParseIntoArray(DirParts, TEXT("/"), /*bCullEmpty=*/true);

    TArray<FString> SanitizedParts;
    SanitizedParts.Reserve(DirParts.Num());
    for (const FString& Part : DirParts)
        SanitizedParts.Add(ObjectTools::SanitizeObjectName(Part));

    return FString::Join(SanitizedParts, TEXT("/"));
}

static FString NormalizeDirectoryPrefix(const FString& Directory)
{
    FString Normalized = Directory;
    FPaths::NormalizeFilename(Normalized);
    if (!Normalized.EndsWith(TEXT("/")))
        Normalized += TEXT("/");

    return Normalized;
}

static FString SanitizeAssetSuffix(const FString& AssetNameSuffix)
{
    if (AssetNameSuffix.IsEmpty())
        return TEXT("");

    FString Sanitized = ObjectTools::SanitizeObjectName(AssetNameSuffix);
    if (!Sanitized.IsEmpty() && !Sanitized.StartsWith(TEXT("_")))
        Sanitized = TEXT("_") + Sanitized;

    return Sanitized;
}

static FTextureAssetPath BuildTextureAssetPath(const FString& RelSubpath, const FString& AssetNameSuffix = FString())
{
    FTextureAssetPath AssetPath;

    const FString RelDir = FPaths::GetPath(RelSubpath);
    const FString BaseFilename = FPaths::GetBaseFilename(RelSubpath);
    const FString SanitizedDir = SanitizePackageSubdir(RelDir);
    const FString SanitizedSuffix = SanitizeAssetSuffix(AssetNameSuffix);

    AssetPath.AssetName = TEXT("T_") + ObjectTools::SanitizeObjectName(BaseFilename) + SanitizedSuffix;
    AssetPath.Extension = FPaths::GetExtension(RelSubpath);
    AssetPath.PackagePath = FDsonAssetUtils::ImportRootPath() / FString(TEXT("Textures"));
    if (!SanitizedDir.IsEmpty())
        AssetPath.PackagePath = AssetPath.PackagePath / SanitizedDir;
    AssetPath.PackagePath = AssetPath.PackagePath / AssetPath.AssetName;

    return AssetPath;
}

static FTextureAssetPath BuildBakedNormalAssetPath(const FString& RelSubpath)
{
    FTextureAssetPath AssetPath = BuildTextureAssetPath(RelSubpath);
    const FString BaseFilename = FPaths::GetBaseFilename(RelSubpath);
    AssetPath.AssetName = TEXT("T_") + ObjectTools::SanitizeObjectName(BaseFilename) + TEXT("_N");
    AssetPath.PackagePath = FPaths::GetPath(AssetPath.PackagePath) / AssetPath.AssetName;
    return AssetPath;
}

static bool DecodeImageFile(const FString& ResolvedPath, FDecodedImage& OutImage)
{
    TArray<uint8> FileBytes;
    if (!FFileHelper::LoadFileToArray(FileBytes, *ResolvedPath))
        return false;

    IImageWrapperModule& ImageWrapperModule =
        FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName(TEXT("ImageWrapper")));
    const EImageFormat ImageFormat =
        ImageWrapperModule.DetectImageFormat(FileBytes.GetData(), FileBytes.Num());
    if (ImageFormat == EImageFormat::Invalid)
        return false;

    TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);
    if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(FileBytes.GetData(), FileBytes.Num()))
        return false;

    TArray64<uint8> RawBgra;
    // UE's TIFF wrapper only supports BGRA8 for 8-bit images; requesting RGBA makes TIFF inputs fail to decode.
    if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawBgra))
        return false;

    OutImage.Width = ImageWrapper->GetWidth();
    OutImage.Height = ImageWrapper->GetHeight();
    OutImage.Pixels.SetNum(OutImage.Width * OutImage.Height);

    for (int32 i = 0; i < OutImage.Pixels.Num(); ++i)
    {
        const int64 RawIdx = static_cast<int64>(i) * 4;
        OutImage.Pixels[i] = FColor(
            RawBgra[RawIdx + 2],
            RawBgra[RawIdx + 1],
            RawBgra[RawIdx + 0],
            RawBgra[RawIdx + 3]);
    }

    return OutImage.IsValid();
}

static float HeightAt(const FDecodedImage& Image, int32 X, int32 Y)
{
    X = FMath::Clamp(X, 0, Image.Width - 1);
    Y = FMath::Clamp(Y, 0, Image.Height - 1);

    const FColor& Pixel = Image.Pixels[Y * Image.Width + X];
    return (ByteToUnit(Pixel.R) + ByteToUnit(Pixel.G) + ByteToUnit(Pixel.B)) / 3.0f;
}

static FVector3f DecodeNormalPixel(const FColor& Pixel)
{
    FVector3f Normal(
        ByteToUnit(Pixel.R) * 2.0f - 1.0f,
        ByteToUnit(Pixel.G) * 2.0f - 1.0f,
        ByteToUnit(Pixel.B) * 2.0f - 1.0f);
    return Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector3f(0.0f, 0.0f, 1.0f));
}

static FVector3f SampleBaseNormal(const FDecodedImage& Image, int32 X, int32 Y, int32 TargetWidth, int32 TargetHeight)
{
    if (!Image.IsValid())
        return FVector3f(0.0f, 0.0f, 1.0f);

    const float U = TargetWidth > 1 ? static_cast<float>(X) / static_cast<float>(TargetWidth - 1) : 0.0f;
    const float V = TargetHeight > 1 ? static_cast<float>(Y) / static_cast<float>(TargetHeight - 1) : 0.0f;
    const int32 SrcX = FMath::Clamp(FMath::RoundToInt(U * static_cast<float>(Image.Width - 1)), 0, Image.Width - 1);
    const int32 SrcY = FMath::Clamp(FMath::RoundToInt(V * static_cast<float>(Image.Height - 1)), 0, Image.Height - 1);
    return DecodeNormalPixel(Image.Pixels[SrcY * Image.Width + SrcX]);
}

static FColor EncodeNormalPixel(const FVector3f& Normal)
{
    const FVector3f N = Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector3f(0.0f, 0.0f, 1.0f));
    return FColor(
        static_cast<uint8>(FMath::Clamp(FMath::RoundToInt((N.X * 0.5f + 0.5f) * 255.0f), 0, 255)),
        static_cast<uint8>(FMath::Clamp(FMath::RoundToInt((N.Y * 0.5f + 0.5f) * 255.0f), 0, 255)),
        static_cast<uint8>(FMath::Clamp(FMath::RoundToInt((N.Z * 0.5f + 0.5f) * 255.0f), 0, 255)),
        255);
}

static FString BuildBakedNormalCacheKey(
    const FString& BumpPath,
    float BumpStrength,
    const FString& NormalPath,
    float NormalStrength)
{
    return FString::Printf(
        TEXT("%s|%.6f|%s|%.6f"),
        *BumpPath,
        BumpStrength,
        *NormalPath,
        NormalStrength);
}

static FString BuildTextureCacheKey(const FString& ResolvedPath, bool bSRGB, const FString& AssetNameSuffix)
{
    return FString::Printf(
        TEXT("%s|srgb=%d|suffix=%s"),
        *ResolvedPath,
        static_cast<int32>(bSRGB),
        *SanitizeAssetSuffix(AssetNameSuffix));
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

FDsonTextureImporter::FDsonTextureImporter(const TArray<FString>& InContentRoots)
    : ContentRoots(InContentRoots)
{
}

void FDsonTextureImporter::RecordFailure(const FString& ImageUrl)
{
    ++FailureCount;
    FailedUrls.Add(ImageUrl);
}

// ---------------------------------------------------------------------------
// DeriveRelativeSubpath
// ---------------------------------------------------------------------------

FString FDsonTextureImporter::DeriveRelativeSubpath(
    const FString& ImageUrl, const FString& ResolvedAbsPath) const
{
    // Strip fragment after '#', matching the behaviour of FDsonContentRoots::ResolveUrl
    const FString Url = DsonImportUtils::StripUrlFragment(ImageUrl);

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
        const FString NormalizedRoot = NormalizeDirectoryPrefix(Root);
        if (NormalizedAbs.StartsWith(NormalizedRoot, ESearchCase::IgnoreCase))
            return NormalizedAbs.RightChop(NormalizedRoot.Len());
    }

    // Last resort: use just the clean filename
    return FPaths::GetCleanFilename(ResolvedAbsPath);
}

// ---------------------------------------------------------------------------
// ImportOrFind
// ---------------------------------------------------------------------------

UTexture2D* FDsonTextureImporter::ImportOrFind(const FString& ImageUrl, bool bSRGB, const FString& AssetNameSuffix)
{
    // 1. Resolve URL to an absolute filesystem path
    const FString ResolvedPath = FDsonContentRoots::ResolveUrl(ImageUrl, ContentRoots);
    if (ResolvedPath.IsEmpty())
    {
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonTextureImporter: could not resolve '%s' in %d content root(s)"),
            *ImageUrl, ContentRoots.Num());
        RecordFailure(ImageUrl);
        return nullptr;
    }

    const FString CacheKey = BuildTextureCacheKey(ResolvedPath, bSRGB, AssetNameSuffix);

    // 2. Cache lookup - key is the resolved absolute path plus import settings so
    //    color/data requests for the same file do not return the wrong asset.
    if (TObjectPtr<UTexture2D>* Found = Cache.Find(CacheKey))
    {
        ++CacheHitCount;
        UE_LOG(LogDsonImporter, VeryVerbose,
            TEXT("DsonTextureImporter: cache hit '%s'"), *ResolvedPath);
        return Found->Get();
    }

    // 3. Derive the UE asset path
    //    Source:  D:/Daz_content/Runtime/Textures/Genesis8/Female/G8FBase.jpg
    //    Subpath: Runtime/Textures/Genesis8/Female/G8FBase.jpg
    //    Result:  /Game/DazImports/Textures/Runtime/Textures/Genesis8/Female/T_G8FBase
    const FString RelSubpath = DeriveRelativeSubpath(ImageUrl, ResolvedPath);
    FTextureAssetPath AssetPath = BuildTextureAssetPath(RelSubpath, AssetNameSuffix);

    // 4. If a .uasset already exists at the derived path with the requested sRGB,
    //    return it; if it exists with the other sRGB mode, use a one-off suffix so
    //    both variants can coexist without overwriting user-side tweaks.
    if (FPackageName::DoesPackageExist(AssetPath.PackagePath))
    {
        UTexture2D* Existing = LoadObject<UTexture2D>(nullptr, *AssetPath.PackagePath, nullptr, LOAD_NoWarn);
        if (Existing && (Existing->SRGB != 0) == bSRGB)
        {
            Cache.Add(CacheKey, Existing);
            ++CacheHitCount;
            UE_LOG(LogDsonImporter, VeryVerbose,
                TEXT("DsonTextureImporter: found existing asset '%s'"), *AssetPath.PackagePath);
            return Existing;
        }

        if (Existing)
        {
            const FString ColorSpaceSuffix = bSRGB ? TEXT("_srgb") : TEXT("_lin");
            AssetPath = BuildTextureAssetPath(RelSubpath, AssetNameSuffix + ColorSpaceSuffix);
            if (FPackageName::DoesPackageExist(AssetPath.PackagePath))
            {
                UTexture2D* ExistingVariant =
                    LoadObject<UTexture2D>(nullptr, *AssetPath.PackagePath, nullptr, LOAD_NoWarn);
                if (ExistingVariant && (ExistingVariant->SRGB != 0) == bSRGB)
                {
                    Cache.Add(CacheKey, ExistingVariant);
                    ++CacheHitCount;
                    UE_LOG(LogDsonImporter, VeryVerbose,
                        TEXT("DsonTextureImporter: found existing asset '%s'"), *AssetPath.PackagePath);
                    return ExistingVariant;
                }
            }
        }
    }

    // 5. Read source file bytes
    TArray<uint8> FileBytes;
    if (!FFileHelper::LoadFileToArray(FileBytes, *ResolvedPath))
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonTextureImporter: failed to read file '%s'"), *ResolvedPath);
        RecordFailure(ImageUrl);
        return nullptr;
    }

    // 6. Create the package
    UPackage* Package = FDsonAssetUtils::CreateLoadedPackage(AssetPath.PackagePath, TEXT("DsonTextureImporter"));
    if (!Package)
    {
        RecordFailure(ImageUrl);
        return nullptr;
    }

    // 7. Import via UTextureFactory - gives us explicit control over the package and
    //    asset name; the factory applies its own heuristics (e.g. normal map detection
    //    from the filename) and handles all formats it natively supports
    UTextureFactory* Factory = NewObject<UTextureFactory>();
    Factory->bCreateMaterial = false;

    const uint8* BufferStart = FileBytes.GetData();
    const uint8* BufferEnd   = BufferStart + FileBytes.Num();

    UObject* ImportedObj = Factory->FactoryCreateBinary(
        UTexture2D::StaticClass(),
        Package,
        *AssetPath.AssetName,
        RF_Public | RF_Standalone,
        nullptr,
        *AssetPath.Extension,
        BufferStart,
        BufferEnd,
        GWarn);

    UTexture2D* Texture = Cast<UTexture2D>(ImportedObj);
    if (!Texture)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonTextureImporter: FactoryCreateBinary failed for '%s'"), *ResolvedPath);
        RecordFailure(ImageUrl);
        return nullptr;
    }

    // 8. Apply caller-supplied sRGB flag; the caller knows the channel kind
    Texture->SRGB = bSRGB;
    Texture->UpdateResource();

    if (!FDsonAssetUtils::SaveAssetPackage(Package, Texture, AssetPath.PackagePath, TEXT("DsonTextureImporter")))
    {
        RecordFailure(ImageUrl);
        return nullptr;
    }

    Cache.Add(CacheKey, Texture);
    ++ImportedCount;

    UE_LOG(LogDsonImporter, Verbose,
        TEXT("DsonTextureImporter: imported '%s' from '%s' (sRGB=%d)"),
        *AssetPath.PackagePath, *ResolvedPath, static_cast<int32>(bSRGB));

    return Texture;
}

// ---------------------------------------------------------------------------
// CompositeImageLayers
// ---------------------------------------------------------------------------

UTexture2D* FDsonTextureImporter::CompositeImageLayers(
    const TArray<FString>& LayerPaths,
    const FString& ImageId,
    bool bSRGB,
    int32 CanvasW,
    int32 CanvasH)
{
    // Cache by image id + sRGB so Eye Left and Eye Right sharing the same image composite once.
    const FString CacheKey = FString::Printf(
        TEXT("composite|%s|srgb=%d"), *ImageId, static_cast<int32>(bSRGB));

    if (TObjectPtr<UTexture2D>* Found = CompositeCache.Find(CacheKey))
    {
        ++CacheHitCount;
        UE_LOG(LogDsonImporter, VeryVerbose,
            TEXT("DsonTextureImporter: composite cache hit '%s'"), *ImageId);
        return Found->Get();
    }

    if (LayerPaths.Num() == 0)
    {
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonTextureImporter: CompositeImageLayers: no layers for image '%s'"), *ImageId);
        return nullptr;
    }

    // Single layer: delegate to ImportOrFind (no pixel work needed).
    if (LayerPaths.Num() == 1)
    {
        UTexture2D* Tex = ImportOrFind(LayerPaths[0], bSRGB);
        if (Tex)
            CompositeCache.Add(CacheKey, Tex);
        return Tex;
    }

    // Multi-layer: resolve, decode, source-over composite in sRGB space (matches DAZ image-space compositing).
    TArray<FDecodedImage> Layers;
    Layers.Reserve(LayerPaths.Num());
    for (const FString& LayerPath : LayerPaths)
    {
        const FString ResolvedPath = FDsonContentRoots::ResolveUrl(LayerPath, ContentRoots);
        if (ResolvedPath.IsEmpty())
        {
            UE_LOG(LogDsonImporter, Warning,
                TEXT("DsonTextureImporter: CompositeImageLayers: could not resolve layer '%s' for image '%s'"),
                *LayerPath, *ImageId);
            return nullptr;
        }

        FDecodedImage Decoded;
        if (!DecodeImageFile(ResolvedPath, Decoded))
        {
            UE_LOG(LogDsonImporter, Warning,
                TEXT("DsonTextureImporter: CompositeImageLayers: failed to decode '%s' for image '%s'"),
                *ResolvedPath, *ImageId);
            return nullptr;
        }
        Layers.Add(MoveTemp(Decoded));
    }

    // Canvas size: use parser-supplied map_size if valid; else max across decoded layers.
    if (CanvasW <= 0 || CanvasH <= 0)
    {
        CanvasW = 0;
        CanvasH = 0;
        for (const FDecodedImage& L : Layers)
        {
            CanvasW = FMath::Max(CanvasW, L.Width);
            CanvasH = FMath::Max(CanvasH, L.Height);
        }
    }

    // Allocate transparent canvas; composite each layer 1:1, top-left anchored, no resampling.
    TArray<FColor> OutPixels;
    OutPixels.SetNumZeroed(CanvasW * CanvasH);
    for (const FDecodedImage& Layer : Layers)
    {
        const int32 CopyW = FMath::Min(Layer.Width, CanvasW);
        const int32 CopyH = FMath::Min(Layer.Height, CanvasH);
        for (int32 y = 0; y < CopyH; ++y)
        {
            for (int32 x = 0; x < CopyW; ++x)
            {
                const FColor& Src = Layer.Pixels[y * Layer.Width + x];
                FColor& Dst = OutPixels[y * CanvasW + x];
                // source-over in sRGB (DAZ composites in image space): out = lerp(dst, src.rgb, src.a)
                const float A = static_cast<float>(Src.A) / 255.0f;
                Dst.R = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Dst.R * (1.0f - A) + Src.R * A), 0, 255));
                Dst.G = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Dst.G * (1.0f - A) + Src.G * A), 0, 255));
                Dst.B = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Dst.B * (1.0f - A) + Src.B * A), 0, 255));
            }
        }
    }
    // Force alpha = 255 (opaque albedo) for all canvas pixels
    for (FColor& Pixel : OutPixels)
        Pixel.A = 255;

    // Asset path: /Game/DazImports/Textures/Composites/T_<sanitized ImageId>
    const FString SanitizedId = ObjectTools::SanitizeObjectName(ImageId);
    const FString AssetName   = TEXT("T_") + SanitizedId;
    const FString PackagePath = FDsonAssetUtils::ImportRootPath() / TEXT("Textures") / TEXT("Composites") / AssetName;

    // Check for an existing asset at this path (re-import may recreate without build).
    if (FPackageName::DoesPackageExist(PackagePath))
    {
        UTexture2D* Existing = LoadObject<UTexture2D>(nullptr, *PackagePath, nullptr, LOAD_NoWarn);
        if (Existing && (Existing->SRGB != 0) == bSRGB)
        {
            CompositeCache.Add(CacheKey, Existing);
            ++CacheHitCount;
            return Existing;
        }
    }

    UPackage* Package = FDsonAssetUtils::CreateLoadedPackage(PackagePath, TEXT("DsonTextureImporter"));
    if (!Package)
        return nullptr;

    UTexture2D* Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone);
    if (!Texture)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonTextureImporter: NewObject<UTexture2D> failed for composite '%s'"), *PackagePath);
        return nullptr;
    }

    // OutPixels is an array of FColor (R,G,B,A named fields); on Win64/LE FColor memory layout is BGRA,
    // which is what TSF_BGRA8 expects — same convention as the bump-bake path above.
    Texture->Source.Init(CanvasW, CanvasH, /*NumSlices=*/1, /*NumMips=*/1,
        TSF_BGRA8, reinterpret_cast<const uint8*>(OutPixels.GetData()));
    Texture->CompressionSettings = TC_Default;
    Texture->SRGB = bSRGB;
    Texture->UpdateResource();

    if (!FDsonAssetUtils::SaveAssetPackage(Package, Texture, PackagePath, TEXT("DsonTextureImporter")))
        return nullptr;

    CompositeCache.Add(CacheKey, Texture);
    ++ImportedCount;

    UE_LOG(LogDsonImporter, Verbose,
        TEXT("DsonTextureImporter: composited '%s' from %d layers (sRGB=%d)"),
        *PackagePath, LayerPaths.Num(), static_cast<int32>(bSRGB));

    return Texture;
}

UTexture2D* FDsonTextureImporter::ImportBumpAsNormal(
    const FString& BumpUrl,
    float BumpStrength,
    const FString& NormalUrl,
    float NormalStrength)
{
    const FString BumpPath = FDsonContentRoots::ResolveUrl(BumpUrl, ContentRoots);
    if (BumpPath.IsEmpty())
    {
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonTextureImporter: could not resolve bump map '%s' in %d content root(s)"),
            *BumpUrl, ContentRoots.Num());
        RecordFailure(BumpUrl);
        return nullptr;
    }

    FString NormalPath;
    if (!NormalUrl.IsEmpty())
    {
        NormalPath = FDsonContentRoots::ResolveUrl(NormalUrl, ContentRoots);
        if (NormalPath.IsEmpty())
        {
            UE_LOG(LogDsonImporter, Warning,
                TEXT("DsonTextureImporter: could not resolve normal map '%s' for bump bake '%s'"),
                *NormalUrl, *BumpUrl);
            RecordFailure(BumpUrl);
            return nullptr;
        }
    }

    const FString CacheKey = BuildBakedNormalCacheKey(BumpPath, BumpStrength, NormalPath, NormalStrength);
    if (TObjectPtr<UTexture2D>* Found = BakedNormalCache.Find(CacheKey))
    {
        ++CacheHitCount;
        return Found->Get();
    }

    FDecodedImage BumpImage;
    if (!DecodeImageFile(BumpPath, BumpImage))
    {
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonTextureImporter: failed to decode bump map '%s'"), *BumpPath);
        RecordFailure(BumpUrl);
        return nullptr;
    }

    FDecodedImage BaseNormalImage;
    if (!NormalPath.IsEmpty() && !DecodeImageFile(NormalPath, BaseNormalImage))
    {
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonTextureImporter: failed to decode normal map '%s' for bump bake '%s'"),
            *NormalPath, *BumpPath);
        RecordFailure(BumpUrl);
        return nullptr;
    }

    const FString RelSubpath = DeriveRelativeSubpath(BumpUrl, BumpPath);
    const FTextureAssetPath AssetPath = BuildBakedNormalAssetPath(RelSubpath);

    UPackage* Package = FDsonAssetUtils::CreateLoadedPackage(AssetPath.PackagePath, TEXT("DsonTextureImporter"));
    if (!Package)
    {
        RecordFailure(BumpUrl);
        return nullptr;
    }

    UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *AssetPath.PackagePath, nullptr, LOAD_NoWarn);
    if (!Texture)
    {
        Texture = NewObject<UTexture2D>(
            Package, *AssetPath.AssetName, RF_Public | RF_Standalone);
    }
    if (!Texture)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonTextureImporter: NewObject<UTexture2D> failed for baked normal '%s'"),
            *AssetPath.PackagePath);
        RecordFailure(BumpUrl);
        return nullptr;
    }

    TArray<FColor> NormalPixels;
    NormalPixels.SetNumUninitialized(BumpImage.Width * BumpImage.Height);

    constexpr float BumpGradientScale = 4.0f;
    for (int32 Y = 0; Y < BumpImage.Height; ++Y)
    {
        for (int32 X = 0; X < BumpImage.Width; ++X)
        {
            const float Gx = HeightAt(BumpImage, X + 1, Y) - HeightAt(BumpImage, X - 1, Y);
            const float Gy = HeightAt(BumpImage, X, Y + 1) - HeightAt(BumpImage, X, Y - 1);
            const float Scale = BumpGradientScale * BumpStrength;

            // UE tangent-space normal maps use the DirectX/MikkTSpace green convention:
            // image-space +Y height gradients tilt toward negative tangent Y for raised bumps.
            const FVector3f BumpNormal = FVector3f(-Gx * Scale, -Gy * Scale, 1.0f)
                .GetSafeNormal(UE_SMALL_NUMBER, FVector3f(0.0f, 0.0f, 1.0f));
            const FVector3f BaseNormal =
                SampleBaseNormal(BaseNormalImage, X, Y, BumpImage.Width, BumpImage.Height);
            const FVector3f CombinedNormal = BaseNormalImage.IsValid()
                ? FVector3f(BaseNormal.X + BumpNormal.X, BaseNormal.Y + BumpNormal.Y, BaseNormal.Z)
                : BumpNormal;

            NormalPixels[Y * BumpImage.Width + X] = EncodeNormalPixel(CombinedNormal);
        }
    }

    Texture->Source.Init(
        BumpImage.Width,
        BumpImage.Height,
        /*NumSlices=*/1,
        /*NumMips=*/1,
        TSF_BGRA8,
        reinterpret_cast<const uint8*>(NormalPixels.GetData()));
    Texture->CompressionSettings = TC_Normalmap;
    Texture->SRGB = false;
    Texture->UpdateResource();

    if (!FDsonAssetUtils::SaveAssetPackage(Package, Texture, AssetPath.PackagePath, TEXT("DsonTextureImporter")))
    {
        RecordFailure(BumpUrl);
        return nullptr;
    }

    BakedNormalCache.Add(CacheKey, Texture);
    ++ImportedCount;

    UE_LOG(LogDsonImporter, Verbose,
        TEXT("DsonTextureImporter: baked bump normal '%s' from '%s' normal='%s' bumpStrength=%.4f normalStrength=%.4f"),
        *AssetPath.PackagePath, *BumpPath, *NormalPath, BumpStrength, NormalStrength);

    return Texture;
}
