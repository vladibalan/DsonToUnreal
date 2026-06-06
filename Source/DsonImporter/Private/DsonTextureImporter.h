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
    UTexture2D* ImportOrFind(const FString& ImageUrl, bool bSRGB);

    // Bakes a grayscale DAZ bump height image into a tangent-space normal map,
    // optionally combining it with an existing normal map. The returned texture
    // is saved as a generated normal asset and cached by bump+normal inputs.
    UTexture2D* ImportBumpAsNormal(
        const FString& BumpUrl,
        float BumpStrength,
        const FString& NormalUrl,
        float NormalStrength);

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
    TMap<FString, TObjectPtr<UTexture2D>> Cache;   // key: resolved absolute path
    TMap<FString, TObjectPtr<UTexture2D>> BakedNormalCache; // key: resolved paths + strengths
    int32                               ImportedCount = 0;
    int32                               CacheHitCount = 0;
    int32                               FailureCount  = 0;
    TArray<FString>                     FailedUrls;
};
