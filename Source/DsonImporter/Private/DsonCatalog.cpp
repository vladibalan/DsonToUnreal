#include "DsonCatalog.h"          // public types first (IWYU)
#include "DsonCatalogImpl.h"
#include "DsonImporter.h"         // LogDsonImporter
#include "DsonValidator.h"        // FDsonValidator, FDsonDependency, EDsonAssetType
#include "DsonLoadedDocument.h"   // FDsonLoadedDocument (R3)
#include "DsonParserFunctions.h"  // GDsonParser
#include "DsonImportUtils.h"      // DsonImportUtils::FromUtf8, StripUrlScheme, StripUrlFragment
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"      // FMD5::HashAnsiString
#include "HAL/FileManager.h"      // IFileManager
#include "Async/TaskGraphInterfaces.h" // ENamedThreads, AsyncTask (progress callback)
#include "Serialization/MemoryReader.h"
#include "Serialization/BufferArchive.h"

/*
 * Intent:
 * - Implements FDsonCatalog: async filesystem walk, asset classification, and incremental disk cache.
 * - One FDsonLoadedDocument opened per asset (R3); validator helpers called directly (R4 — no Validate()).
 * - Classification: node_library presentation.type first, then modifier_library, then asset_info.type.
 * - Cache v2: [int32 version][int32 count][entries...] via FArchive binary.
 *   Each entry carries MTime+Size for incremental reuse; version mismatch forces rescan.
 * - Abort: bAbortRequested polled between files and between roots; cooperative with ShutdownModule.
 */

// ---- Path helper ----

// static
FString FDsonCatalog::StripRootPrefix(const FString& AbsPath, const FString& Root)
{
    FString NormAbs  = AbsPath;
    FString NormRoot = Root;
    FPaths::NormalizeFilename(NormAbs);
    FPaths::NormalizeFilename(NormRoot);
    if (!NormRoot.EndsWith(TEXT("/")))
        NormRoot += TEXT("/");
    if (!NormAbs.StartsWith(NormRoot, ESearchCase::IgnoreCase))
        return {};
    return NormAbs.Mid(NormRoot.Len());
}

// ---- Disk cache ----

FString FDsonCatalog::GetCacheFilePath(const FString& RootAbsPath)
{
    const FString Hash = FMD5::HashAnsiString(*RootAbsPath);
    const FString CacheDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DsonCatalogCache"));
    return FPaths::Combine(CacheDir, Hash + TEXT(".bin"));
}

bool FDsonCatalog::LoadCache(const FString& RootAbsPath, TArray<FCachedEntry>& OutEntries)
{
    const FString CachePath = GetCacheFilePath(RootAbsPath);

    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *CachePath))
        return false;

    FMemoryReader Ar(Bytes);

    int32 Version = 0;
    Ar << Version;
    if (Ar.IsError() || Version != CacheSchemaVersion)
        return false;

    int32 Count = 0;
    Ar << Count;
    if (Ar.IsError() || Count < 0)
        return false;

    OutEntries.Reserve(Count);
    for (int32 i = 0; i < Count; ++i)
    {
        FCachedEntry CE;
        FDsonCatalogEntry& Entry = CE.Entry;

        FString RelPath;
        Ar << RelPath;
        Ar << Entry.Label;
        uint8 TypeByte = 0;
        Ar << TypeByte;
        Entry.Type = static_cast<EDsonCatalogAssetType>(TypeByte);
        uint8 GenByte = 0;
        Ar << GenByte;
        Entry.Generation = static_cast<EGenesisGeneration>(GenByte);
        Ar << Entry.DependsOn;
        uint8 Browsable = 0;
        Ar << Browsable;
        Entry.bBrowsable = (Browsable != 0);
        Ar << CE.MTime;
        Ar << CE.Size;

        if (Ar.IsError())
            return false;

        Entry.Id           = RelPath;
        Entry.RelativePath = RelPath;
        OutEntries.Add(MoveTemp(CE));
    }

    return !Ar.IsError();
}

void FDsonCatalog::SaveCache(const FString& RootAbsPath, const TArray<FCachedEntry>& Entries)
{
    const FString CachePath = GetCacheFilePath(RootAbsPath);

    IFileManager::Get().MakeDirectory(*FPaths::GetPath(CachePath), /*Tree=*/true);

    FBufferArchive Ar;
    int32 Version = CacheSchemaVersion;
    Ar << Version;
    int32 Count = Entries.Num();
    Ar << Count;
    for (const FCachedEntry& CE : Entries)
    {
        FString RelPath = CE.Entry.RelativePath;
        Ar << RelPath;
        FString Label = CE.Entry.Label;
        Ar << Label;
        uint8 TypeByte = static_cast<uint8>(CE.Entry.Type);
        Ar << TypeByte;
        uint8 GenByte = static_cast<uint8>(CE.Entry.Generation);
        Ar << GenByte;
        TArray<FString> Deps = CE.Entry.DependsOn;
        Ar << Deps;
        uint8 Browsable = CE.Entry.bBrowsable ? 1 : 0;
        Ar << Browsable;
        int64 MTime = CE.MTime;
        Ar << MTime;
        int64 Size  = CE.Size;
        Ar << Size;
    }

    FFileHelper::SaveArrayToFile(Ar, *CachePath);
}

void FDsonCatalog::DeleteCacheFile(const FString& RootAbsPath)
{
    const FString CachePath = GetCacheFilePath(RootAbsPath);
    IFileManager::Get().Delete(*CachePath, /*RequireExists=*/false);
}

void FDsonCatalog::Invalidate()
{
    const FString CacheDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DsonCatalogCache"));
    TArray<FString> CacheFiles;
    IFileManager::Get().FindFiles(CacheFiles, *(CacheDir / TEXT("*.bin")), /*Files=*/true, /*Dirs=*/false);
    for (const FString& File : CacheFiles)
        IFileManager::Get().Delete(*(CacheDir / File), /*RequireExists=*/false);

    ThumbCache.Empty();

    UE_LOG(LogDsonImporter, Log, TEXT("FDsonCatalog: cache invalidated (%d file(s) deleted)"), CacheFiles.Num());
}

// ---- Abort ----

void FDsonCatalog::RequestAbort()
{
    bAbortRequested.store(true, std::memory_order_release);
}

// ---- Thumbnail cache ----

TOptional<TArray<uint8>> FDsonCatalog::GetThumbnail(const FString& RootAbsPath, const FString& Id)
{
    const FString AssetAbsPath = FPaths::Combine(RootAbsPath, Id);

    // Cache hit — update rank and return.
    if (FThumbEntry* Hit = ThumbCache.Find(AssetAbsPath))
    {
        Hit->Rank = ++ThumbClock;
        return Hit->Bytes;
    }

    // Probe disk: appended form first (Foo.duf.png — observed on some products),
    // then change-extension form (Foo.png — the more common convention).
    FString ThumbPath = AssetAbsPath + TEXT(".png");
    if (!FPaths::FileExists(ThumbPath))
    {
        ThumbPath = FPaths::ChangeExtension(AssetAbsPath, TEXT("png"));
        if (!FPaths::FileExists(ThumbPath))
            return {};
    }

    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *ThumbPath))
        return {};

    // Evict the least-recently-used entry when at capacity.
    if (ThumbCache.Num() >= ThumbCacheCap)
    {
        FString EvictKey;
        int32 MinRank = INT32_MAX;
        for (const auto& Pair : ThumbCache)
        {
            if (Pair.Value.Rank < MinRank)
            {
                MinRank  = Pair.Value.Rank;
                EvictKey = Pair.Key;
            }
        }
        ThumbCache.Remove(EvictKey);
    }

    FThumbEntry& NewEntry = ThumbCache.Add(AssetAbsPath);
    NewEntry.Bytes = Bytes;
    NewEntry.Rank  = ++ThumbClock;
    return NewEntry.Bytes;
}

// ---- Classification ----

// static
EDsonCatalogAssetType FDsonCatalog::MapPresentationType(
    const FString& PresentationType,
    const FString& AssetInfoType)
{
    if (PresentationType.IsEmpty())
    {
        if (AssetInfoType.Equals(TEXT("material"), ESearchCase::IgnoreCase) ||
            AssetInfoType.Equals(TEXT("material preset"), ESearchCase::IgnoreCase))
            return EDsonCatalogAssetType::MaterialPreset;
        return EDsonCatalogAssetType::Other_Unknown;
    }

    if (PresentationType.Equals(TEXT("Follower/Expression"), ESearchCase::CaseSensitive))
        return EDsonCatalogAssetType::Expression;
    if (PresentationType.Equals(TEXT("Follower/Pose"), ESearchCase::CaseSensitive))
        return EDsonCatalogAssetType::Pose;
    if (PresentationType.EndsWith(TEXT("/Animation"), ESearchCase::CaseSensitive))
        return EDsonCatalogAssetType::Animation;

    if (PresentationType.Equals(TEXT("Actor"), ESearchCase::CaseSensitive) ||
        PresentationType.Equals(TEXT("Figure"), ESearchCase::CaseSensitive))
        return EDsonCatalogAssetType::Figure_Character;

    if (PresentationType.Equals(TEXT("Wardrobe/Clothing"), ESearchCase::CaseSensitive) ||
        PresentationType.Equals(TEXT("Wardrobe/Outfit"),   ESearchCase::CaseSensitive))
        return EDsonCatalogAssetType::Wearable_Clothing;
    if (PresentationType.Equals(TEXT("Wardrobe/Hair"), ESearchCase::CaseSensitive))
        return EDsonCatalogAssetType::Wearable_Hair;
    if (PresentationType.Equals(TEXT("Wardrobe/Accessory"), ESearchCase::CaseSensitive))
        return EDsonCatalogAssetType::Wearable_Accessory;
    if (PresentationType.StartsWith(TEXT("Wardrobe/"), ESearchCase::CaseSensitive))
        return EDsonCatalogAssetType::Wearable_Accessory;

    if (PresentationType.Equals(TEXT("Modifier/Shape"), ESearchCase::CaseSensitive))
        return EDsonCatalogAssetType::Morph;

    if (PresentationType.Equals(TEXT("Pose Preset"), ESearchCase::CaseSensitive) ||
        PresentationType.StartsWith(TEXT("Pose Preset/"), ESearchCase::CaseSensitive))
        return EDsonCatalogAssetType::Pose;

    if (PresentationType.Equals(TEXT("Prop"), ESearchCase::CaseSensitive) ||
        PresentationType.StartsWith(TEXT("Prop/"), ESearchCase::CaseSensitive))
        return EDsonCatalogAssetType::Prop;

    if (PresentationType.Equals(TEXT("Follower"), ESearchCase::CaseSensitive))
        return EDsonCatalogAssetType::Wearable_Accessory;
    if (PresentationType.StartsWith(TEXT("Follower/"), ESearchCase::CaseSensitive))
        return EDsonCatalogAssetType::Wearable_Accessory;

    UE_LOG(LogDsonImporter, Verbose,
        TEXT("FDsonCatalog: unmapped presentation.type '%s' -> Other_Unknown"), *PresentationType);
    return EDsonCatalogAssetType::Other_Unknown;
}

FDsonCatalogEntry FDsonCatalog::ClassifyAsset(
    const FString& AbsPath,
    const FDsonCatalogRoot& Root,
    const TArray<FString>& ContentRoots) const
{
    FDsonCatalogEntry Entry;

    const FString RelPath = StripRootPrefix(AbsPath, Root.AbsPath);
    if (RelPath.IsEmpty())
        return Entry; // empty Id signals "skip"

    Entry.Id           = RelPath;
    Entry.RelativePath = RelPath;
    Entry.RootId       = Root.Id;

    // Open the document (R3 — RAII handle; logs at Warning on failure).
    FDsonLoadedDocument Doc;
    if (!Doc.LoadFromFileAsWarning(AbsPath, TEXT("FDsonCatalog")))
    {
        UE_LOG(LogDsonImporter, Verbose, TEXT("FDsonCatalog: skipping unloadable file '%s'"), *RelPath);
        Entry.Id.Empty();
        return Entry;
    }

    const DsonDocumentHandle DocHandle = Doc.GetHandle();
    const uint64_t           Handle64  = Doc.GetHandle64();

    // --- 1. asset_info.type and generation (R3: copy const char* before next parser call) ---
    const char* RawAssetType = GDsonParser.GetAssetType(DocHandle);
    const EDsonAssetType ValidatorType = FDsonValidator::ParseAssetType(RawAssetType ? RawAssetType : "");
    const FString AssetTypeStr = DsonImportUtils::FromUtf8(RawAssetType);

    const char* RawAssetId = GDsonParser.GetAssetId(DocHandle);
    Entry.Generation = FDsonValidator::DetectGeneration(RawAssetId ? RawAssetId : "");
    const FString AssetId = DsonImportUtils::FromUtf8(RawAssetId);

    // --- 2. Check geometry_library for graft (optional export — null-guarded) ---
    bool bAnyGraft = false;
    if (GDsonParser.GetGeometryCount && GDsonParser.GetGeometryIsGraft)
    {
        const int32 GeomCount = GDsonParser.GetGeometryCount(Handle64);
        for (int32 gi = 0; gi < GeomCount; ++gi)
        {
            if (GDsonParser.GetGeometryIsGraft(Handle64, static_cast<int32_t>(gi)))
            {
                bAnyGraft = true;
                break;
            }
        }
    }

    // --- 3. First non-empty presentation type from node_library ---
    FString PresentationType;
    FString PresentationLabel;
    bool bFoundPresentation = false;

    if (GDsonParser.GetNodePresentationType)
    {
        const int32 NodeCount = GDsonParser.GetNodeCount(Handle64);
        for (int32 ni = 0; ni < NodeCount && !bFoundPresentation; ++ni)
        {
            const FString Type = DsonImportUtils::FromUtf8(
                GDsonParser.GetNodePresentationType(Handle64, static_cast<int32_t>(ni)));
            if (!Type.IsEmpty())
            {
                PresentationType = Type;
                if (GDsonParser.GetNodePresentationLabel)
                    PresentationLabel = DsonImportUtils::FromUtf8(
                        GDsonParser.GetNodePresentationLabel(Handle64, static_cast<int32_t>(ni)));
                bFoundPresentation = true;
            }
        }
    }

    // --- 4. Fall back to modifier_library if no node presentation found ---
    if (!bFoundPresentation && GDsonParser.GetModifierPresentationType)
    {
        const int32 ModCount = GDsonParser.GetModifierCount
            ? GDsonParser.GetModifierCount(Handle64) : 0;
        for (int32 mi = 0; mi < ModCount && !bFoundPresentation; ++mi)
        {
            const FString Type = DsonImportUtils::FromUtf8(
                GDsonParser.GetModifierPresentationType(Handle64, static_cast<int32_t>(mi)));
            if (!Type.IsEmpty())
            {
                PresentationType = Type;
                if (GDsonParser.GetModifierPresentationLabel)
                    PresentationLabel = DsonImportUtils::FromUtf8(
                        GDsonParser.GetModifierPresentationLabel(Handle64, static_cast<int32_t>(mi)));
                bFoundPresentation = true;
            }
        }
    }

    // --- 5. Map to EDsonCatalogAssetType; graft takes precedence ---
    if (bAnyGraft)
        Entry.Type = EDsonCatalogAssetType::Geograft;
    else
        Entry.Type = MapPresentationType(PresentationType, AssetTypeStr);

    // --- 6. Label: presentation.label → asset-id stem → filename stem ---
    if (!PresentationLabel.IsEmpty())
    {
        Entry.Label = PresentationLabel;
    }
    else if (!AssetId.IsEmpty())
    {
        const FString Cleaned = DsonImportUtils::StripUrlScheme(DsonImportUtils::StripUrlFragment(AssetId));
        Entry.Label = FPaths::GetBaseFilename(Cleaned);
    }
    if (Entry.Label.IsEmpty())
        Entry.Label = FPaths::GetBaseFilename(AbsPath);

    // --- 7. bBrowsable ---
    if (AssetTypeStr.Equals(TEXT("figure"), ESearchCase::IgnoreCase))
        Entry.bBrowsable = false;
    else if (AssetTypeStr.Equals(TEXT("modifier"), ESearchCase::IgnoreCase) && PresentationType.IsEmpty())
        Entry.bBrowsable = false;
    else if (AssetTypeStr.Equals(TEXT("uv_set"), ESearchCase::IgnoreCase))
        Entry.bBrowsable = false;
    else
        Entry.bBrowsable = true;

    if (bAnyGraft)
        Entry.bBrowsable = true;

    // --- 8. DependsOn: convert to root-relative ids ---
    TArray<FDsonDependency> Deps;
    FDsonValidator::ResolveDependencies(DocHandle, ValidatorType, ContentRoots, Deps);
    for (const FDsonDependency& Dep : Deps)
    {
        if (!Dep.bResolved)
            continue;
        for (const FString& CRoot : ContentRoots)
        {
            const FString Rel = StripRootPrefix(Dep.ResolvedPath, CRoot);
            if (!Rel.IsEmpty())
            {
                Entry.DependsOn.Add(Rel);
                break;
            }
        }
    }

    return Entry;
}

// ---- Root enumeration ----

void FDsonCatalog::EnumerateRoot(
    const FDsonCatalogRoot& Root,
    const TArray<FString>& ContentRoots,
    TArray<FDsonCatalogEntry>& OutEntries,
    FDsonCatalogRootStatus& OutStatus)
{
    // Load v2 cache (if available and schema matches) into a map keyed by RelPath.
    TArray<FCachedEntry> CachedList;
    TMap<FString, FCachedEntry> CacheMap;
    if (LoadCache(Root.AbsPath, CachedList))
    {
        CacheMap.Reserve(CachedList.Num());
        for (FCachedEntry& CE : CachedList)
            CacheMap.Add(CE.Entry.RelativePath, MoveTemp(CE));
    }

    // Collect .duf and .dsf files recursively.
    TArray<FString> AllFiles;
    IFileManager::Get().FindFilesRecursive(AllFiles, *Root.AbsPath, TEXT("*.duf"), true, false, false);
    {
        TArray<FString> DsfFiles;
        IFileManager::Get().FindFilesRecursive(DsfFiles, *Root.AbsPath, TEXT("*.dsf"), true, false, false);
        AllFiles.Append(MoveTemp(DsfFiles));
    }

    UE_LOG(LogDsonImporter, Log,
        TEXT("FDsonCatalog: scanning root '%s' (%d files, %d cached)"),
        *Root.Id, AllFiles.Num(), CacheMap.Num());

    TArray<FCachedEntry> NewCacheList;
    NewCacheList.Reserve(AllFiles.Num());

    int32 Reused = 0;
    int32 Reclassified = 0;

    for (const FString& AbsPath : AllFiles)
    {
        if (bAbortRequested.load(std::memory_order_acquire))
        {
            OutStatus.Status       = EDsonCatalogRootStatus::Error;
            OutStatus.ErrorMessage = TEXT("Walk aborted");
            return;
        }

        // Stat the file for incremental check.
        const FFileStatData Stat = IFileManager::Get().GetStatData(*AbsPath);
        const int64 MTime = Stat.ModificationTime.GetTicks();
        const int64 Size  = Stat.FileSize;

        // Compute RelPath to look up in the cache.
        const FString RelPath = StripRootPrefix(AbsPath, Root.AbsPath);
        if (RelPath.IsEmpty())
            continue;

        // Reuse cached entry when MTime+Size match.
        if (const FCachedEntry* Cached = CacheMap.Find(RelPath))
        {
            if (Cached->MTime == MTime && Cached->Size == Size)
            {
                FCachedEntry CE = *Cached;
                CE.Entry.RootId = Root.Id;
                OutEntries.Add(CE.Entry);
                NewCacheList.Add(MoveTemp(CE));
                ++Reused;
                continue;
            }
        }

        // New or modified — re-classify.
        FDsonCatalogEntry Entry = ClassifyAsset(AbsPath, Root, ContentRoots);
        if (Entry.Id.IsEmpty())
            continue;

        Entry.RootId = Root.Id;
        OutEntries.Add(Entry);

        FCachedEntry CE;
        CE.Entry = MoveTemp(Entry);
        CE.MTime = MTime;
        CE.Size  = Size;
        NewCacheList.Add(MoveTemp(CE));
        ++Reclassified;
    }

    if (bAbortRequested.load(std::memory_order_acquire))
    {
        OutStatus.Status       = EDsonCatalogRootStatus::Error;
        OutStatus.ErrorMessage = TEXT("Walk aborted");
        return;
    }

    SaveCache(Root.AbsPath, NewCacheList);

    OutStatus.Status = EDsonCatalogRootStatus::Ok;
    UE_LOG(LogDsonImporter, Log,
        TEXT("FDsonCatalog: root '%s' — %d reused, %d reclassified; cache written"),
        *Root.Id, Reused, Reclassified);
}

// ---- Top-level enumerate ----

FDsonCatalogResult FDsonCatalog::DoEnumerate(
    const TArray<FDsonCatalogRoot>& Roots,
    const TFunction<void(int32, int32)>& ProgressCallback)
{
    bAbortRequested.store(false, std::memory_order_release);

    FDsonCatalogResult Result;

    if (!GDsonParser.IsValid())
    {
        Result.bCompleted = false;
        Result.ErrorMessage = TEXT("DsonParser library is not loaded");
        UE_LOG(LogDsonImporter, Error, TEXT("FDsonCatalog: %s"), *Result.ErrorMessage);
        return Result;
    }

    TArray<FString> ContentRoots;
    ContentRoots.Reserve(Roots.Num());
    for (const FDsonCatalogRoot& Root : Roots)
        ContentRoots.Add(Root.AbsPath);

    const int32 TotalRoots = Roots.Num();
    for (int32 RootIdx = 0; RootIdx < TotalRoots; ++RootIdx)
    {
        if (bAbortRequested.load(std::memory_order_acquire))
        {
            Result.bCompleted = false;
            Result.ErrorMessage = TEXT("Walk aborted");
            UE_LOG(LogDsonImporter, Log, TEXT("FDsonCatalog: walk aborted before root %d"), RootIdx);
            return Result;
        }

        const FDsonCatalogRoot& Root = Roots[RootIdx];

        FDsonCatalogRootStatus& Status = Result.RootStatuses.AddDefaulted_GetRef();
        Status.RootId = Root.Id;

        if (!FPaths::DirectoryExists(Root.AbsPath))
        {
            TArray<FCachedEntry> StaleEntries;
            if (LoadCache(Root.AbsPath, StaleEntries) && StaleEntries.Num() > 0)
            {
                for (FCachedEntry& CE : StaleEntries)
                {
                    CE.Entry.RootId = Root.Id;
                    Result.Entries.Add(CE.Entry);
                }
                Status.Status       = EDsonCatalogRootStatus::Offline;
                Status.ErrorMessage = FString::Printf(
                    TEXT("Root offline; showing %d cached entries"), StaleEntries.Num());
                UE_LOG(LogDsonImporter, Warning, TEXT("FDsonCatalog: %s"), *Status.ErrorMessage);
            }
            else
            {
                Status.Status       = EDsonCatalogRootStatus::Missing;
                Status.ErrorMessage = FString::Printf(TEXT("Root path not found: %s"), *Root.AbsPath);
                UE_LOG(LogDsonImporter, Warning, TEXT("FDsonCatalog: %s"), *Status.ErrorMessage);
            }
        }
        else
        {
            EnumerateRoot(Root, ContentRoots, Result.Entries, Status);
        }

        if (ProgressCallback)
        {
            const int32 Done  = RootIdx + 1;
            const int32 Total = TotalRoots;
            AsyncTask(ENamedThreads::GameThread,
                [ProgressCallback, Done, Total]
                {
                    ProgressCallback(Done, Total);
                });
        }
    }

    if (bAbortRequested.load(std::memory_order_acquire))
    {
        Result.bCompleted = false;
        Result.ErrorMessage = TEXT("Walk aborted");
        return Result;
    }

    Result.bCompleted = true;
    return Result;
}

// Async launch and shutdown coordination live in DsonImporter.cpp (BeginCatalogEnumerate).
// DoEnumerate is called directly from that lambda.
