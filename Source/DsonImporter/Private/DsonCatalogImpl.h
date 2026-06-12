#pragma once
#include "CoreMinimal.h"
#include "DsonCatalog.h"   // FDsonCatalogRoot, FDsonCatalogResult, FDsonCatalogEntry, etc.
#include <atomic>

/*
 * Intent:
 * - Private implementation class for the DAZ library catalog walk and classification.
 * - Long-lived: the module holds a TUniquePtr<FDsonCatalog> (lazy-init in DsonImporter.cpp).
 * - Threadsafety: DoEnumerate runs on one worker thread at a time (serialized by the module).
 *   GetThumbnail is game-thread-only (thumb cache has no mutex). Abort flag is atomic.
 *
 * Named DsonCatalogImpl.h (not DsonCatalog.h) to avoid shadowing Public/DsonCatalog.h
 * in the include search path. The class itself is still named FDsonCatalog per the spec.
 */

class FDsonCatalog
{
public:
    // Runs the full filesystem walk synchronously (called from the thread-pool lambda
    // in FDsonImporterModule::BeginCatalogEnumerate). Resets bAbortRequested at entry.
    FDsonCatalogResult DoEnumerate(
        const TArray<FDsonCatalogRoot>& Roots,
        const TFunction<void(int32, int32)>& ProgressCallback);

    // Deletes every cache file and clears the thumbnail cache. The next DoEnumerate
    // performs a full rescan of every root regardless of cached data.
    void Invalidate();

    // Sets the abort flag; DoEnumerate polls it between files and between roots and
    // returns early with bCompleted=false when it sees it. Called from ShutdownModule
    // before waiting on the walk future.
    void RequestAbort();

    // Returns the raw PNG bytes for the preview image next to <RootAbsPath>/<Id>.
    // Probes <asset>.png (appended) then FPaths::ChangeExtension(<asset>, "png").
    // Lazily fills an LRU cache (cap ThumbCacheCap); cleared by Invalidate().
    // Game-thread-only — the cache has no mutex.
    TOptional<TArray<uint8>> GetThumbnail(const FString& RootAbsPath, const FString& Id);

private:

    // Walk one root, classify each .duf/.dsf, append to OutEntries, fill OutStatus.
    void EnumerateRoot(
        const FDsonCatalogRoot& Root,
        const TArray<FString>& ContentRoots,
        TArray<FDsonCatalogEntry>& OutEntries,
        FDsonCatalogRootStatus& OutStatus);

    // Open one file, classify it, and return a populated entry.
    // Returns an entry with empty Id on any failure (caller skips it).
    FDsonCatalogEntry ClassifyAsset(
        const FString& AbsPath,
        const FDsonCatalogRoot& Root,
        const TArray<FString>& ContentRoots) const;

    // Map a presentation.type string (+ asset_info.type fallback) to EDsonCatalogAssetType.
    // Logs every unmapped string at Verbose (Delta F).
    static EDsonCatalogAssetType MapPresentationType(
        const FString& PresentationType,
        const FString& AssetInfoType);

    // Returns AbsPath relative to Root (forward slashes, no leading slash).
    // Returns empty string if AbsPath is not under Root (case-insensitive prefix match).
    static FString StripRootPrefix(const FString& AbsPath, const FString& Root);

    // ---- Disk cache ----
    // Cache files: ProjectSaved/DsonCatalogCache/<MD5(RootAbsPath)>.bin
    // Format v2: [int32 CacheSchemaVersion][int32 Count][Entry...] (FArchive binary)
    //   Per entry: RelPath(FString) Label(FString) Type(uint8) Generation(uint8)
    //              DependsOn(TArray<FString>) bBrowsable(uint8) MTime(int64) Size(int64)
    // Version mismatch discards the cache; v1 caches are discarded on first v2 read.
    static constexpr int32 CacheSchemaVersion = 2;

    struct FCachedEntry
    {
        FDsonCatalogEntry Entry;
        int64             MTime = 0;
        int64             Size  = 0;
    };

    static FString  GetCacheFilePath(const FString& RootAbsPath);
    static bool     LoadCache(const FString& RootAbsPath, TArray<FCachedEntry>& OutEntries);
    static void     SaveCache(const FString& RootAbsPath, const TArray<FCachedEntry>& Entries);
    static void     DeleteCacheFile(const FString& RootAbsPath);

    // ---- Abort flag ----
    std::atomic<bool> bAbortRequested{false};

    // ---- Thumbnail LRU cache (game-thread-only) ----
    static constexpr int32 ThumbCacheCap = 256;

    struct FThumbEntry
    {
        TArray<uint8> Bytes;
        int32         Rank = 0; // monotonic clock; higher = more-recently used
    };

    TMap<FString, FThumbEntry> ThumbCache;
    int32                      ThumbClock = 0;
};
