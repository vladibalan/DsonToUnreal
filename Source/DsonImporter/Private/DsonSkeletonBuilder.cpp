#include "DsonSkeletonBuilder.h"
#include "DsonImporter.h"
#include "DsonAssetUtils.h"
#include "DsonParserFunctions.h"
#include "DsonLoadedDocument.h"

#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "Misc/Paths.h"
#include "PackageTools.h"

/*
 * Intent:
 * - Build a UE USkeleton from the resolved base figure DSF.
 * - Convert DAZ node hierarchy and transforms into a UE reference skeleton.
 * - Save the resulting skeleton asset for the mesh builder to use.
 *
 * Read this file for bone hierarchy, transform conversion, and skeleton asset creation.
 */

namespace
{
    struct FBoneEntry
    {
        FString Id;
        FString Name;
        FString ParentId;
        FTransform Transform;
    };

    void SortBonesParentsFirst(TArray<FBoneEntry>& Bones)
    {
        // Topological sort: parent before child (UE5 requires parent index < child index).
        TMap<FString, int32> IdToArrayIndex;
        IdToArrayIndex.Reserve(Bones.Num());
        for (int32 i = 0; i < Bones.Num(); ++i)
            IdToArrayIndex.Add(Bones[i].Id, i);

        TArray<FBoneEntry> Sorted;
        Sorted.Reserve(Bones.Num());
        TSet<FString> Visited;
        Visited.Reserve(Bones.Num());

        TFunction<void(const FBoneEntry&)> Visit = [&](const FBoneEntry& Bone)
        {
            if (Visited.Contains(Bone.Id))
                return;
            if (!Bone.ParentId.IsEmpty())
            {
                const int32* ParentArrayIdx = IdToArrayIndex.Find(Bone.ParentId);
                if (ParentArrayIdx)
                    Visit(Bones[*ParentArrayIdx]);
            }
            Visited.Add(Bone.Id);
            Sorted.Add(Bone);
        };

        for (const FBoneEntry& B : Bones)
            Visit(B);

        Bones = MoveTemp(Sorted);
    }
}

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------

USkeleton* FDsonSkeletonBuilder::Build(const FDsonImportSettings& Settings)
{
    // Public orchestration wrapper: load the base figure DSF, build the reference
    // skeleton, save the asset, and release the parser handle.
    if (!GDsonParser.IsValid())
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonSkeletonBuilder: DsonParser API not fully loaded"));
        return nullptr;
    }

    uint64_t DsfHandle = 0;
    FDsonLoadedDocument DsfDocument;
    if (!DsfDocument.LoadFromFileAsError(Settings.ResolvedFigureDsfPath, TEXT("DsonSkeletonBuilder")))
        return nullptr;

    DsfHandle = DsfDocument.GetHandle64();
    FReferenceSkeleton RefSkeleton;
    BuildReferenceSkeletonFromDsf(DsfHandle, RefSkeleton);

    if (RefSkeleton.GetRawBoneNum() == 0)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonSkeletonBuilder: no bones found in '%s'"),
            *Settings.ResolvedFigureDsfPath);
        return nullptr;
    }

    const FString AssetName = FPaths::GetBaseFilename(Settings.ResolvedFigureDsfPath);
    return CreateSkeletonAsset(RefSkeleton, AssetName);
}

// ---------------------------------------------------------------------------
// BuildReferenceSkeletonFromDsf
// ---------------------------------------------------------------------------

void FDsonSkeletonBuilder::BuildReferenceSkeletonFromDsf(uint64_t DsfHandle, FReferenceSkeleton& OutRefSkeleton)
{
    // Collect bone nodes, sort parents before children, then add parent-relative transforms.
    // This method bridges parser node order and UE's strict reference skeleton ordering.
    double UnitScale = GDsonParser.GetUnitScale ? GDsonParser.GetUnitScale(DsfHandle) : 1.0 / 100.0;
    if (UnitScale == 0.0)
        UnitScale = 1.0 / 100.0; // DAZ default: 1 DAZ unit = 1 cm

    const int32 NodeCount = GDsonParser.GetNodeCount ? GDsonParser.GetNodeCount(DsfHandle) : 0;

    TArray<FBoneEntry> Bones;
    Bones.Reserve(NodeCount);

    for (int32 i = 0; i < NodeCount; ++i)
    {
        const char* TypeRaw = GDsonParser.GetNodeType ? GDsonParser.GetNodeType(DsfHandle, i) : nullptr;
        if (!TypeRaw || FCStringAnsi::Stricmp(TypeRaw, "bone") != 0)
            continue;

        FBoneEntry& Entry = Bones.AddDefaulted_GetRef();

        const char* IdRaw  = GDsonParser.GetNodeId     ? GDsonParser.GetNodeId(DsfHandle, i)     : nullptr;
        const char* NmRaw  = GDsonParser.GetNodeName   ? GDsonParser.GetNodeName(DsfHandle, i)   : nullptr;
        const char* PrRaw  = GDsonParser.GetNodeParent ? GDsonParser.GetNodeParent(DsfHandle, i) : nullptr;

        Entry.Id       = IdRaw ? UTF8_TO_TCHAR(IdRaw) : FString::Printf(TEXT("Bone_%d"), i);
        Entry.Name     = Entry.Id;
        {
            FString RawParent = PrRaw ? UTF8_TO_TCHAR(PrRaw) : TEXT("");
            // DAZ parent refs are URL fragment ids like "#hip" — strip the leading '#'
            if (RawParent.StartsWith(TEXT("#")))
                RawParent.RemoveAt(0, 1, false);
            Entry.ParentId = MoveTemp(RawParent);
        }
        Entry.Transform = MakeBoneTransform(DsfHandle, i, UnitScale);
    }

    if (Bones.IsEmpty())
        return;

    SortBonesParentsFirst(Bones);

    // Set of all bone ids — any parent id NOT in this set means the bone is a root.
    TSet<FString> BoneIdSet;
    BoneIdSet.Reserve(Bones.Num());
    for (const FBoneEntry& B : Bones)
        BoneIdSet.Add(B.Id);

    // bone id → index in OutRefSkeleton, populated as bones are added
    TMap<FString, int32> IdToRefIndex;
    IdToRefIndex.Reserve(Bones.Num());

    FReferenceSkeletonModifier Modifier(OutRefSkeleton, nullptr);
    int32 RefBoneCount = 0;

    for (const FBoneEntry& B : Bones)
    {
        int32 ParentRefIndex = INDEX_NONE;
        if (BoneIdSet.Contains(B.ParentId))
        {
            const int32* Found = IdToRefIndex.Find(B.ParentId);
            if (!Found)
            {
                UE_LOG(LogDsonImporter, Warning,
                    TEXT("DsonSkeletonBuilder: parent '%s' of bone '%s' not yet resolved, skipping"),
                    *B.ParentId, *B.Name);
                continue;
            }
            ParentRefIndex = *Found;
        }

        // B.Transform holds this bone's WORLD-space transform:
        //   rotation    = world-space joint orientation (UE space)
        //   translation = world-space joint position (UE space, cm)
        // UE5 ref-pose bones are stored as parent-relative LOCAL transforms, so we
        // convert here once the parent's world transform is known.
        FTransform LocalTransform = B.Transform;
        if (ParentRefIndex != INDEX_NONE)
        {
            const FBoneEntry* ParentEntry = Bones.FindByPredicate(
                [&](const FBoneEntry& E){ return E.Id == B.ParentId; });
            if (ParentEntry)
            {
                const FQuat   ParentWorldRot = ParentEntry->Transform.GetRotation();
                const FVector ParentWorldPos = ParentEntry->Transform.GetTranslation();
                const FQuat   SelfWorldRot   = B.Transform.GetRotation();
                const FVector SelfWorldPos   = B.Transform.GetTranslation();

                const FQuat ParentWorldRotInv = ParentWorldRot.Inverse();

                // Local rotation: delta from parent's world frame to this bone's.
                const FQuat LocalRot = ParentWorldRotInv * SelfWorldRot;
                // Local translation: world offset expressed in the parent's rotated frame.
                const FVector LocalPos =
                    ParentWorldRotInv.RotateVector(SelfWorldPos - ParentWorldPos);

                LocalTransform.SetRotation(LocalRot);
                LocalTransform.SetTranslation(LocalPos);
            }
        }
        // Root bones keep their world transform as their local transform.

        FMeshBoneInfo BoneInfo;
        BoneInfo.Name        = FName(*B.Name);
        BoneInfo.ParentIndex = ParentRefIndex;
        Modifier.Add(BoneInfo, LocalTransform);

        IdToRefIndex.Add(B.Id, RefBoneCount++);
    }
    // Modifier destructor finalises OutRefSkeleton
}

// ---------------------------------------------------------------------------
// MakeBoneTransform
// ---------------------------------------------------------------------------

namespace
{
    // DAZ (Y-up, RIGHT-handed) → UE5 (Z-up, LEFT-handed). Converting between opposite
    // handedness REQUIRES a reflection (a determinant −1 map). The position remap is:
    //     UE_X =  DAZ_Z
    //     UE_Y = -DAZ_X      ← negated; this is the reflection that flips handedness
    //     UE_Z =  DAZ_Y
    //
    // Without the negation the map is a pure rotation (det +1) which cannot convert
    // handedness: it silently mirrors the whole figure, swapping anatomical left/right
    // (l_* bones land on the figure's right). The negation fixes that.
    //
    // For rotations, the DAZ orientation is carried into UE space by conjugation
    //     R_ue = M^-1 * R_daz * M
    // where M is this basis change as a UE row-vector FMatrix (v_ue = v_daz * M).
    // M is orthogonal with det −1; conjugating a proper rotation by it yields another
    // proper rotation (det +1), so bones stay valid rotations — verified against the
    // figure's joint data (FK reconstructs to zero error).
    const FMatrix& DazToUeBasisMatrix()
    {
        // Row-vector convention: FMatrix(r0,r1,r2,r3) sets ROWS, transform is p' = p * M.
        // Rows chosen so (x,y,z) * M = (z, -x, y):
        static const FMatrix M(
            FPlane(0.0, -1.0, 0.0, 0.0),
            FPlane(0.0,  0.0, 1.0, 0.0),
            FPlane(1.0,  0.0, 0.0, 0.0),
            FPlane(0.0,  0.0, 0.0, 1.0));
        return M;
    }

    // Single-axis rotation quaternion. 'axis' is 'x'|'y'|'z' (lowercase), angle in degrees.
    FQuat AxisQuat(char Axis, double AngleDeg)
    {
        FVector V = FVector::ZeroVector;
        switch (Axis)
        {
            case 'x': V = FVector(1.0, 0.0, 0.0); break;
            case 'y': V = FVector(0.0, 1.0, 0.0); break;
            case 'z': V = FVector(0.0, 0.0, 1.0); break;
            default:  return FQuat::Identity;
        }
        return FQuat(V, FMath::DegreesToRadians(static_cast<float>(AngleDeg)));
    }

    // Compose DAZ orientation Euler angles into a single quaternion, honouring the
    // node's rotation order. In DAZ the first axis listed is applied first (innermost),
    // so for order "XYZ" the composite is Rz * Ry * Rx. RotationOrder is e.g. "XYZ",
    // "XZY", "YZX". Falls back to "XYZ" when missing/unrecognised.
    FQuat ComposeDazOrientation(double OX, double OY, double OZ, const FString& RotationOrder)
    {
        FString Order = RotationOrder.IsEmpty() ? TEXT("XYZ") : RotationOrder.ToUpper();
        if (Order.Len() != 3)
            Order = TEXT("XYZ");

        auto AngleFor = [&](char UpperAxis) -> double
        {
            switch (UpperAxis)
            {
                case 'X': return OX;
                case 'Y': return OY;
                case 'Z': return OZ;
                default:  return 0.0;
            }
        };

        // Apply first-listed axis first (innermost). Composite = q3 * q2 * q1, where
        // q1 is the first listed. Building left-to-right as Composite = Composite * qi
        // starting from identity yields exactly that.
        FQuat Composite = FQuat::Identity;
        for (int32 i = 0; i < 3; ++i)
        {
            const char UpperAxis = static_cast<char>(Order[i]);
            const char LowerAxis = static_cast<char>(FChar::ToLower(Order[i]));
            Composite = AxisQuat(LowerAxis, AngleFor(UpperAxis)) * Composite;
        }
        return Composite;
    }

    void MergeReferenceSkeletonIntoSkeleton(USkeleton* Skeleton, const FReferenceSkeleton& RefSkeleton)
    {
        // USkeleton has no public API to set its FReferenceSkeleton directly in UE5 5.4.
        // The standard path is to create a throwaway mesh, set its ref skeleton, then merge.
        USkeletalMesh* TempMesh = NewObject<USkeletalMesh>(
            GetTransientPackage(), NAME_None, RF_Transient);
        TempMesh->SetRefSkeleton(RefSkeleton);
        Skeleton->MergeAllBonesToBoneTree(TempMesh);
    }
}

FTransform FDsonSkeletonBuilder::MakeBoneTransform(uint64_t DsfHandle, int32 NodeIndex, double UnitScale)
{
    // Returns a world-space UE bone transform for a DAZ node. The caller converts this
    // to parent-relative local space after the full hierarchy is known.
    const double CX = GDsonParser.GetNodeCenterPointX ? GDsonParser.GetNodeCenterPointX(DsfHandle, NodeIndex) : 0.0;
    const double CY = GDsonParser.GetNodeCenterPointY ? GDsonParser.GetNodeCenterPointY(DsfHandle, NodeIndex) : 0.0;
    const double CZ = GDsonParser.GetNodeCenterPointZ ? GDsonParser.GetNodeCenterPointZ(DsfHandle, NodeIndex) : 0.0;

    // World-space joint position. DAZ→UE with handedness flip: UE_X←DAZ_Z,
    // UE_Y←-DAZ_X, UE_Z←DAZ_Y. The -DAZ_X is the reflection that converts DAZ's
    // right-handed frame to UE's left-handed frame (see DazToUeBasisMatrix).
    // ToCm = UnitScale: Genesis is authored in cm and unit_scale defaults to 1.0.
    // Do NOT reintroduce a *100 here.
    const double ToCm = UnitScale;
    const FVector Translation(CZ * ToCm, -CX * ToCm, CY * ToCm);

    const double OX = GDsonParser.GetNodeOrientationX ? GDsonParser.GetNodeOrientationX(DsfHandle, NodeIndex) : 0.0;
    const double OY = GDsonParser.GetNodeOrientationY ? GDsonParser.GetNodeOrientationY(DsfHandle, NodeIndex) : 0.0;
    const double OZ = GDsonParser.GetNodeOrientationZ ? GDsonParser.GetNodeOrientationZ(DsfHandle, NodeIndex) : 0.0;

    const char* OrderRaw = GDsonParser.GetNodeRotationOrder
        ? GDsonParser.GetNodeRotationOrder(DsfHandle, NodeIndex) : nullptr;
    const FString RotationOrder = OrderRaw ? UTF8_TO_TCHAR(OrderRaw) : TEXT("XYZ");

    // The DAZ `orientation` is a JOINT ORIENTATION: it defines the bone's local axis
    // frame expressed in the figure (world) frame — it is NOT a parent-relative local
    // rotation, and the Euler angles cannot be axis-permuted directly. Build the DAZ
    // orientation quaternion honouring the node's rotation order, then change basis
    // into UE space by conjugation R_ue = B * R_daz * B^-1.
    //
    // The result here is the bone's WORLD-space rotation in UE space. The hierarchy
    // pass in BuildReferenceSkeletonFromDsf converts it to the parent-relative local
    // rotation that UE5 ref poses require.
    const FQuat OriDaz = ComposeDazOrientation(OX, OY, OZ, RotationOrder);

    // Carry the DAZ orientation into UE space by conjugating with the basis matrix:
    //   R_ue = M^-1 * R_daz * M   (UE row-vector convention; see DazToUeBasisMatrix).
    const FMatrix M       = DazToUeBasisMatrix();
    const FMatrix OriDazM = FRotationMatrix::Make(OriDaz);
    const FMatrix OriUeM  = M.Inverse() * OriDazM * M;
    const FQuat   Rotation = OriUeM.ToQuat().GetNormalized();

    double GenScale = GDsonParser.GetNodeGeneralScale
        ? GDsonParser.GetNodeGeneralScale(DsfHandle, NodeIndex) : 0.0;
    if (GenScale == 0.0) GenScale = 1.0;

    return FTransform(Rotation, Translation, FVector(static_cast<float>(GenScale)));
}

// ---------------------------------------------------------------------------
// CreateSkeletonAsset
// ---------------------------------------------------------------------------

USkeleton* FDsonSkeletonBuilder::CreateSkeletonAsset(
    const FReferenceSkeleton& RefSkeleton, const FString& AssetName)
{
    // Saves the reference skeleton as /Game/DazImports/<AssetName>_Skeleton.
    // UE 5.4 requires the transient mesh merge path to populate USkeleton bones.
    const FString SkeletonName = AssetName + TEXT("_Skeleton");
    const FString PackagePath  = TEXT("/Game/DazImports/") + SkeletonName;

    UPackage* Package = FDsonAssetUtils::CreateLoadedPackage(PackagePath, TEXT("DsonSkeletonBuilder"));
    if (!Package)
        return nullptr;

    USkeleton* Skeleton = NewObject<USkeleton>(Package, *SkeletonName, RF_Public | RF_Standalone);
    if (!Skeleton)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonSkeletonBuilder: failed to create USkeleton object in '%s'"), *PackagePath);
        return nullptr;
    }

    MergeReferenceSkeletonIntoSkeleton(Skeleton, RefSkeleton);

    return FDsonAssetUtils::SaveAssetPackage(Package, Skeleton, PackagePath, TEXT("DsonSkeletonBuilder"))
        ? Skeleton
        : nullptr;
}
