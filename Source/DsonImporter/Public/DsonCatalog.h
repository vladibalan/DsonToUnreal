#pragma once
#include "CoreMinimal.h"

/*
 * Intent:
 * - Public consumer-facing types for the DAZ library catalog (enumerate + classify).
 * - Also owns EGenesisGeneration, which the import pipeline uses too (moved here so
 *   the public catalog header is the single definition point — DsonImportTypes.h
 *   transitively re-exports it via #include below).
 *
 * Read this file for the catalog entry shape, root/status types, and the enumerate result.
 * The catalog implementation lives in Private/DsonCatalogImpl.h + Private/DsonCatalog.cpp.
 * Entry point: FDsonImporterModule::BeginCatalogEnumerate / InvalidateCatalog (DsonImporter.h).
 */

// Genesis generation inferred from DAZ asset IDs.
// Shared by the catalog (public) and the import pipeline (private DsonImportTypes.h).
// R5: plain enum class — no UENUM/reflection consumer in scope.
enum class EGenesisGeneration : uint8
{
    Unknown,
    Genesis3,
    Genesis8,
    Genesis9
};

// DAZ content classification for library browsing.
// Distinct from EDsonAssetType, which is an import-routing enum (coarse, private to the pipeline).
// R5: plain enum class — no UENUM/reflection consumer; consumer binds from editor C++/Slate.
enum class EDsonCatalogAssetType : uint8
{
    Other_Unknown = 0,   // unmappable or empty presentation — never guessed (P1)
    Figure_Character,    // "Actor" (IsGraft=false), "Figure", character preset
    Wearable_Clothing,   // "Wardrobe/Clothing", "Wardrobe/Outfit"
    Wearable_Hair,       // "Wardrobe/Hair"
    Wearable_Accessory,  // "Wardrobe/Accessory", bare "Follower", other "Wardrobe/…"
    MaterialPreset,      // asset_info.type == "material" or "material preset"
    Morph,               // "Modifier/Shape"
    Pose,                // "Pose Preset/…", "Follower/Pose"
    Expression,          // "Follower/Expression"
    Animation,           // "…/Animation"
    Geograft,            // any geometry_library item has IsGraft==true (takes precedence)
    Prop,                // "Prop/…" or standalone prop not matching above
};

// One DAZ content root supplied to BeginCatalogEnumerate.
struct FDsonCatalogRoot
{
    FString Id;      // stable opaque identifier for this root (consumer-provided)
    FString AbsPath; // absolute path to the root folder on disk
};

// One catalogued asset from a library root (consumer-blind: R5/P3 — no consumer name in types).
struct FDsonCatalogEntry
{
    // Root-relative canonical reference: forward slashes, no leading slash, filesystem-exact case.
    // CONTRACT (frozen): Id == RelativePath. Consumer forms SourceAssetPath as
    //   Root.AbsPath + "/" + Id to pass to ImportDazAsset. Changing this is a MAJOR bump.
    FString Id;
    FString RootId;        // Id of the root this entry came from
    FString RelativePath;  // == Id (same value; distinct role label for the consumer)
    FString Label;         // display name: presentation.label → asset-id stem → filename stem
    EDsonCatalogAssetType Type     = EDsonCatalogAssetType::Other_Unknown;
    EGenesisGeneration    Generation = EGenesisGeneration::Unknown;
    TArray<FString>       DependsOn;  // root-relative ids of resolved direct dependencies
    bool bBrowsable = true;           // false for dependency-only DSFs (base mesh, raw morph, UV set)
};

// Per-root walk outcome, one entry per supplied root in FDsonCatalogResult::RootStatuses.
enum class EDsonCatalogRootStatus : uint8
{
    Ok,
    Missing,  // root path not found on disk at walk time
    Error,    // root found but walk failed (I/O or parse error)
    Offline,  // drive temporarily unavailable; stale cache carried forward
};

struct FDsonCatalogRootStatus
{
    FString RootId;
    EDsonCatalogRootStatus Status = EDsonCatalogRootStatus::Ok;
    FString ErrorMessage;
};

// Result returned by FDsonImporterModule::BeginCatalogEnumerate (via TFuture).
struct FDsonCatalogResult
{
    TArray<FDsonCatalogEntry>      Entries;
    TArray<FDsonCatalogRootStatus> RootStatuses;
    bool    bCompleted   = false; // false when aborted (shutdown) or rejected (re-entrant call)
    FString ErrorMessage;         // set when bCompleted == false
};
