#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPtr.h"

class USkeletalMesh;
class USkeleton;
class UTexture2D;

#include "DsonAssetRecipe.generated.h"

// One textured layer in a LIE (layered-image editor) composition stack.
// All fields are raw DAZ values; never composed or interpreted by the importer.
// Sentinel notes: Opacity 0.0 collides with a legitimately-transparent layer —
// always index via a prior LayerCount check. ScaleX/Y use 1.0 as invalid sentinel.
USTRUCT()
struct DSONIMPORTER_API FDsonLieLayer
{
    GENERATED_BODY()

    UPROPERTY() FString TexturePath;          // raw DAZ disk path (copy of parser output)
    UPROPERTY() FString BlendMode;            // e.g. "blend_source_over", "blend_multiply"
    UPROPERTY() float   Opacity   = 1.0f;     // raw "transparency" (1 = opaque)
    UPROPERTY() bool    bActive   = false;
    UPROPERTY() bool    bInvert   = false;
    UPROPERTY() float   ColorR    = 1.0f;
    UPROPERTY() float   ColorG    = 1.0f;
    UPROPERTY() float   ColorB    = 1.0f;
    UPROPERTY() float   Rotation  = 0.0f;
    UPROPERTY() float   ScaleX    = 1.0f;
    UPROPERTY() float   ScaleY    = 1.0f;
    UPROPERTY() float   OffsetX   = 0.0f;
    UPROPERTY() float   OffsetY   = 0.0f;
    UPROPERTY() bool    bMirrorX  = false;
    UPROPERTY() bool    bMirrorY  = false;
};

// Per-channel LIE recipe for one surface: ordered layer stack for one DAZ channel.
USTRUCT()
struct DSONIMPORTER_API FDsonLieSurface
{
    GENERATED_BODY()

    UPROPERTY() FString              MaterialGroupName;  // first polygon group name (surface key)
    UPROPERTY() FString              ChannelId;          // DAZ channel id, e.g. "Diffuse Color"
    UPROPERTY() TArray<FDsonLieLayer> Layers;
    // Set when the importer alpha-composited a >=2-layer stack into one UTexture2D at import;
    // BakedComposite points at the saved composite. A downstream consumer must NOT re-composite
    // this surface from the raw Layers — the baked texture IS the realized result.
    UPROPERTY() bool                          bImporterPreBaked = false;
    UPROPERTY() TSoftObjectPtr<UTexture2D>    BakedComposite;
};

// Dialed weight for one imported morph target: the raw channel value (and range) the
// DAZ scene carries for this modifier, plus the bound UE morph-target name so a
// downstream step can re-apply the dial to the already-imported UMorphTarget.
// Values are raw (P1/P3) — no formula expansion, no composed dialed shape.
USTRUCT()
struct DSONIMPORTER_API FDsonDialWeight
{
    GENERATED_BODY()

    UPROPERTY() FString BoundMorphTargetName;  // ObjectTools::SanitizeObjectName(MorphName??Label) — matches actual UMorphTarget
    UPROPERTY() FString SourceUrl;             // scene.modifiers URL (e.g. ".../morph.dsf#ModifierId")
    UPROPERTY() float   Value    = 0.0f;       // raw dialed channel value from scene.modifiers
    UPROPERTY() float   Min      = 0.0f;       // channel minimum
    UPROPERTY() float   Max      = 1.0f;       // channel maximum
    UPROPERTY() bool    bClamped = false;       // whether channel is clamped to [Min, Max]
};

// Companion figure slot pairing: the DAZ PostLoadAddons slot path matched to the
// imported USkeletalMesh at the same position in the companion build order.
USTRUCT()
struct DSONIMPORTER_API FDsonCompanionSlotEntry
{
    GENERATED_BODY()

    UPROPERTY() FString                          Slot;  // e.g. ".../Face/Eyes"
    UPROPERTY() TSoftObjectPtr<USkeletalMesh>    Mesh;
};

// Persisted recipe asset emitted beside each imported character.
// Carries raw, uncomposed DAZ authoring metadata — never interpreted by the importer.
// Slice 1: manifest + companion slot tags + per-surface LIE layer recipes.
UCLASS()
class DSONIMPORTER_API UDsonAssetRecipe : public UObject
{
    GENERATED_BODY()
public:
    // Manifest
    UPROPERTY() FString                          SourceId;       // DAZ asset_id from the DUF
    UPROPERTY() FString                          CharacterName;
    UPROPERTY() TSoftObjectPtr<USkeleton>        Skeleton;
    UPROPERTY() TSoftObjectPtr<USkeletalMesh>    BodyMesh;

    // Companion slot tags (order matches FDsonImportResult::CompanionMeshes)
    UPROPERTY() TArray<FDsonCompanionSlotEntry>  CompanionSlots;

    // Per-surface LIE recipe (one entry per channel that has a layer stack > 0)
    UPROPERTY() TArray<FDsonLieSurface>          LieSurfaces;

    // Per-morph dial weights from scene.modifiers (raw, uncomposed; correlated to
    // imported UMorphTargets by bound name; uncorrelated modifiers are omitted)
    UPROPERTY() TArray<FDsonDialWeight>           DialWeights;
};
