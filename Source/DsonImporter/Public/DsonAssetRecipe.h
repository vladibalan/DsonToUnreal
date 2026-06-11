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
    UPROPERTY() FString              SourceCompanionSlot; // empty for body surfaces; companion slot path for companion-figure surfaces
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

// Which DAZ output channel a formula targets; derived mechanically from the output URL
// ?property suffix only — never evaluated, never composed. The consumer uses this tag
// to route ERC (BoneCenterPoint/BoneEndPoint) vs morph (MorphValue) vs unclassified.
UENUM()
enum class EDsonFormulaTarget : uint8
{
    Other,           // unrecognized ?property, or no ?property in the URL
    MorphValue,      // ?value — drives a morph modifier's channel
    BoneCenterPoint, // ?center_point/... — drives a bone origin component
    BoneEndPoint,    // ?end_point/... — drives a bone end component
};

// One raw RPN operation from a DAZ formula. Op is the literal DAZ token
// (push/mult/div/add/sub/pow/spline_tcb) and is the discriminator:
//   Op=="push" with Url set => url-push (driver input channel, Val unused)
//   Op=="push" with Url empty => const-push (Val is the constant)
//   other ops use neither operand field
// Both fields are carried raw regardless of which branch applies.
USTRUCT()
struct DSONIMPORTER_API FDsonFormulaOp
{
    GENERATED_BODY()

    UPROPERTY() FString Op;           // literal DAZ RPN token
    UPROPERTY() double  Val = 0.0;    // constant value (used when Op=="push" and Url is empty)
    UPROPERTY() FString Url;          // input-channel URL (used when Op=="push" and Url is non-empty)
};

// One raw DAZ formula record. All fields are raw DAZ values; never evaluated or
// composed by the importer. A consumer that evaluates these formulas works in DAZ
// semantics and applies the DAZ->UE coordinate flip to the result as a unit.
USTRUCT()
struct DSONIMPORTER_API FDsonFormula
{
    GENERATED_BODY()

    UPROPERTY() FString SourceModifierId;      // carrier modifier id (scene: UrlDecoded #fragment of its URL; figure: GetModifierId)
    UPROPERTY() FString SourceModifierName;    // GetModifierName for figure-lib formulas; empty for scene-modifier formulas
    UPROPERTY() bool    bFromSceneModifier = false; // true = carrier document reached via scene.modifiers walk (applied/dialed control or its formula tree); false = base figure DSF (intrinsic rig)
    UPROPERTY() FString BoundMorphTargetName;  // imported UMorphTarget bound to the CARRIER modifier, if any; empty for ERC/controls
    UPROPERTY() float   SourceValue       = 0.0f;  // raw scene dial value (GetSceneModifierChannelValue); 0 for figure-lib formulas
    UPROPERTY() FString OutputUrl;             // full output channel URL including ?property (raw)
    UPROPERTY() EDsonFormulaTarget OutputTarget = EDsonFormulaTarget::Other; // mechanical tag derived from OutputUrl ?property
    UPROPERTY() FString Stage;                 // raw DAZ stage string ("sum" or "mult")
    UPROPERTY() TArray<FDsonFormulaOp> Operations;
};

// Base rig point for one bone in raw DAZ coordinates (NOT UE-flipped). Used by
// ERC-follow consumers to compute followed-position = base +/- evaluated delta.
// Keeping raw DAZ matches the rest of the FDsonFormula block (ops and Vals are
// also DAZ-space); the consumer applies the DAZ->UE flip to the composed result.
USTRUCT()
struct DSONIMPORTER_API FDsonNodeRigPoint
{
    GENERATED_BODY()

    UPROPERTY() FString NodeName;
    UPROPERTY() float   CenterX = 0.0f;
    UPROPERTY() float   CenterY = 0.0f;
    UPROPERTY() float   CenterZ = 0.0f;
    UPROPERTY() float   EndX    = 0.0f;
    UPROPERTY() float   EndY    = 0.0f;
    UPROPERTY() float   EndZ    = 0.0f;
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

    // Raw DAZ formula records from three sources: scene.modifiers inline formulas
    // (bFromSceneModifier=true), external modifier DSFs reached via the scene.modifiers
    // reachability walk (bFromSceneModifier=true), and the body figure modifier_library
    // (bFromSceneModifier=false). Never evaluated or composed — the consumer evaluates
    // and applies DAZ->UE flip to the result as a unit.
    UPROPERTY() TArray<FDsonFormula>              Formulas;

    // Base rig points (raw DAZ coordinates) for bones referenced by ERC-follow formulas
    // (OutputTarget==BoneCenterPoint or BoneEndPoint). One entry per unique NodeName.
    UPROPERTY() TArray<FDsonNodeRigPoint>         RigPoints;
};
