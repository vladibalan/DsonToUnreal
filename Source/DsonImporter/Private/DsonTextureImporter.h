#pragma once
#include "CoreMinimal.h"

class UTexture2D;

class FDsonTextureImporter
{
public:
    // Stores content roots used for resolving DAZ image URLs; no registry probing happens here.
    explicit FDsonTextureImporter(const TArray<FString>& InContentRoots);

    // Returns existing UTexture2D if already imported (cache hit or asset already
    // exists on disk at the derived /Game/DazImports/Textures/... path). Otherwise
    // resolves the URL, imports via UTextureFactory, saves the package, and returns
    // the new texture. Returns nullptr on any failure; logs a warning with details.
    UTexture2D* ImportOrFind(const FString& ImageUrl, bool bSRGB, const FString& AssetNameSuffix = FString());

    // Bakes a grayscale DAZ bump height image into a tangent-space normal map,
    // optionally combining it with an existing normal map. The returned texture
    // is saved as a generated normal asset and cached by bump+normal inputs.
    UTexture2D* ImportBumpAsNormal(
        const FString& BumpUrl,
        float BumpStrength,
        const FString& NormalUrl,
        float NormalStrength);

    // Alpha-composites a LIE layer stack (from image_library) into a single UTexture2D.
    // LayerPaths are the DSON URLs for each textured layer, layer 0 = bottom.
    // N==0 -> null+warn. N==1 -> ImportOrFind on the single path (no pixel work).
    // N>=2 -> decode, source-over composite (sRGB space), save under
    //   /Game/DazImports/Textures/Composites/T_<sanitized ImageId>.
    // Cached by ImageId so Eye Left and Eye Right sharing a color entry composite once.
    UTexture2D* CompositeImageLayers(
        const TArray<FString>& LayerPaths,
        const FString& ImageId,
        bool bSRGB);

    int32 GetImportedCount()  const { return ImportedCount;  }
    int32 GetCacheHitCount()  const { return CacheHitCount;  }
    int32 GetFailureCount()   const { return FailureCount;   }
    const TArray<FString>& GetFailedUrls() const { return FailedUrls; }

private:
    // Derives the content-root-relative subpath from the image URL.
    // For full DSON URLs (start with '/'), decodes and strips the leading slash.
    // For bare filenames, strips the matched content root prefix from ResolvedAbsPath.
    FString DeriveRelativeSubpath(const FString& ImageUrl, const FString& ResolvedAbsPath) const;

    // Records a failed import attempt after the caller has logged the specific reason.
    void RecordFailure(const FString& ImageUrl);

    TArray<FString>                     ContentRoots;
    TMap<FString, TObjectPtr<UTexture2D>> Cache;          // key: resolved absolute path + sRGB + optional asset suffix
    TMap<FString, TObjectPtr<UTexture2D>> BakedNormalCache; // key: resolved paths + strengths
    TMap<FString, TObjectPtr<UTexture2D>> CompositeCache;   // key: image id + sRGB
    int32                               ImportedCount = 0;
    int32                               CacheHitCount = 0;
    int32                               FailureCount  = 0;
    TArray<FString>                     FailedUrls;
};
