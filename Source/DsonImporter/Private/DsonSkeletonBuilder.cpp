#include "DsonSkeletonBuilder.h"
#include "DsonImporter.h"
#include "DsonParserFunctions.h"
#include "SDsonImportWindow.h"

#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "PackageTools.h"
#include "AssetRegistry/AssetRegistryModule.h"

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------

USkeleton* FDsonSkeletonBuilder::Build(const FDsonImportSettings& Settings)
{
    if (!GDsonParser.IsValid())
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonSkeletonBuilder: DsonParser API not fully loaded"));
        return nullptr;
    }

    uint64_t DsfHandle = 0;
    if (!LoadDsfDocument(Settings.ResolvedFigureDsfPath, DsfHandle))
        return nullptr;

    FReferenceSkeleton RefSkeleton;
    BuildReferenceSkeletonFromDsf(DsfHandle, RefSkeleton);

    if (RefSkeleton.GetRawBoneNum() == 0)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonSkeletonBuilder: no bones found in '%s'"),
            *Settings.ResolvedFigureDsfPath);
        GDsonParser.Destroy(reinterpret_cast<DsonDocumentHandle>(DsfHandle));
        return nullptr;
    }

    const FString AssetName = FPaths::GetBaseFilename(Settings.ResolvedFigureDsfPath);
    USkeleton* Skeleton = CreateSkeletonAsset(RefSkeleton, AssetName);

    GDsonParser.Destroy(reinterpret_cast<DsonDocumentHandle>(DsfHandle));
    return Skeleton;
}

// ---------------------------------------------------------------------------
// LoadDsfDocument
// ---------------------------------------------------------------------------

bool FDsonSkeletonBuilder::LoadDsfDocument(const FString& Path, uint64_t& OutHandle)
{
    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *Path))
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonSkeletonBuilder: failed to read file '%s'"), *Path);
        return false;
    }

    FTCHARToUTF8 Utf8Converter(*FileContent);
    const char* Utf8Str = Utf8Converter.Get();

    DsonDocumentHandle RawHandle = GDsonParser.Create();
    if (!RawHandle)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonSkeletonBuilder: GDsonParser.Create() returned null"));
        return false;
    }

    const int32 Result = GDsonParser.LoadFromString(RawHandle, Utf8Str);
    if (Result != 0)
    {
        const char* ErrRaw = GDsonParser.GetLastError ? GDsonParser.GetLastError() : nullptr;
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonSkeletonBuilder: LoadFromString failed for '%s': %s"),
            *Path, ErrRaw ? UTF8_TO_TCHAR(ErrRaw) : TEXT("unknown error"));
        GDsonParser.Destroy(RawHandle);
        return false;
    }

    OutHandle = reinterpret_cast<uint64_t>(RawHandle);
    return true;
}

// ---------------------------------------------------------------------------
// BuildReferenceSkeletonFromDsf
// ---------------------------------------------------------------------------

void FDsonSkeletonBuilder::BuildReferenceSkeletonFromDsf(uint64_t DsfHandle, FReferenceSkeleton& OutRefSkeleton)
{
    double UnitScale = GDsonParser.GetUnitScale ? GDsonParser.GetUnitScale(DsfHandle) : 1.0 / 100.0;
    if (UnitScale == 0.0)
        UnitScale = 1.0 / 100.0; // DAZ default: 1 DAZ unit = 1 cm

    const int32 NodeCount = GDsonParser.GetNodeCount ? GDsonParser.GetNodeCount(DsfHandle) : 0;

    struct FBoneEntry
    {
        FString Id;
        FString Name;
        FString ParentId;
        FTransform Transform;
    };

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

    // Topological sort: parent before child (UE5 requires parent index < child index)
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
    // DAZ (Y-up, right-hand) → UE5 (Z-up, left-hand) axis remap, matching the one
    // used for positions: UE_X = DAZ_Z, UE_Y = DAZ_X, UE_Z = DAZ_Y.
    //
    // As a rotation this even (det = +1) axis permutation is a 120° turn about the
    // (1,1,1) axis, i.e. the quaternion (w,x,y,z) = (0.5, 0.5, 0.5, 0.5). A rotation
    // expressed in DAZ space is carried into UE space by quaternion conjugation:
    //     q_ue = B * q_daz * B^-1
    // Using quaternions (rather than FMatrix) keeps this independent of UE's
    // row-vector matrix convention. Verified: this reproduces the bone directions
    // and local rotations derived directly from the figure's joint data.
    const FQuat& DazToUeBasisQuat()
    {
        static const FQuat B(0.5, 0.5, 0.5, 0.5); // (X, Y, Z, W)
        return B;
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
}

FTransform FDsonSkeletonBuilder::MakeBoneTransform(uint64_t DsfHandle, int32 NodeIndex, double UnitScale)
{
    const double CX = GDsonParser.GetNodeCenterPointX ? GDsonParser.GetNodeCenterPointX(DsfHandle, NodeIndex) : 0.0;
    const double CY = GDsonParser.GetNodeCenterPointY ? GDsonParser.GetNodeCenterPointY(DsfHandle, NodeIndex) : 0.0;
    const double CZ = GDsonParser.GetNodeCenterPointZ ? GDsonParser.GetNodeCenterPointZ(DsfHandle, NodeIndex) : 0.0;

    // World-space joint position. DAZ→UE: UE_X←DAZ_Z, UE_Y←DAZ_X, UE_Z←DAZ_Y.
    // ToCm = UnitScale: Genesis geometry/joints are authored in cm and unit_scale
    // defaults to 1.0, so 1 DAZ unit = 1 cm. Do NOT reintroduce a *100 here.
    const double ToCm = UnitScale;
    const FVector Translation(CZ * ToCm, CX * ToCm, CY * ToCm);

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

    const FQuat& B = DazToUeBasisQuat();
    const FQuat Rotation = (B * OriDaz * B.Inverse()).GetNormalized();

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
    const FString SkeletonName = AssetName + TEXT("_Skeleton");
    const FString PackagePath  = TEXT("/Game/DazImports/") + SkeletonName;

    UPackage* Package = CreatePackage(*PackagePath);
    if (!Package)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonSkeletonBuilder: failed to create package '%s'"), *PackagePath);
        return nullptr;
    }
    Package->FullyLoad();

    USkeleton* Skeleton = NewObject<USkeleton>(Package, *SkeletonName, RF_Public | RF_Standalone);
    if (!Skeleton)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonSkeletonBuilder: failed to create USkeleton object in '%s'"), *PackagePath);
        return nullptr;
    }

    // ── Approach 1 (active): drive MergeAllBonesToBoneTree via a transient USkeletalMesh ──
    // USkeleton has no public API to set its FReferenceSkeleton directly in UE5 5.4;
    // the standard path is to create a throwaway mesh, set its ref skeleton, then merge.
    {
        USkeletalMesh* TempMesh = NewObject<USkeletalMesh>(
            GetTransientPackage(), NAME_None, RF_Transient);
        TempMesh->SetRefSkeleton(RefSkeleton);
        Skeleton->MergeAllBonesToBoneTree(TempMesh);
    }

    // DIAGNOSTIC — remove before Phase 6
    {
        const FReferenceSkeleton& DumpSkel = Skeleton->GetReferenceSkeleton();
        UE_LOG(LogDsonImporter, Log,
            TEXT("DsonSkeletonBuilder: skeleton has %d bones"),
            DumpSkel.GetRawBoneNum());
        for (int32 b = 0; b < DumpSkel.GetRawBoneNum(); ++b)
        {
            const FTransform& Pose = DumpSkel.GetRawRefBonePose()[b];
            const FQuat Q = Pose.GetRotation();
            UE_LOG(LogDsonImporter, Log,
                TEXT("  Bone[%d] '%s' parent=%d pos=(%.2f, %.2f, %.2f) quat=(w%.5f, x%.5f, y%.5f, z%.5f)"),
                b,
                *DumpSkel.GetBoneName(b).ToString(),
                DumpSkel.GetRawParentIndex(b),
                Pose.GetTranslation().X,
                Pose.GetTranslation().Y,
                Pose.GetTranslation().Z,
                Q.W, Q.X, Q.Y, Q.Z);
        }
    }

#if 0
    // ── Approach 2 (fallback): direct FReferenceSkeleton assignment ──
    // Try if Approach 1 does not compile (e.g. SetRefSkeleton is inaccessible in the SDK
    // version you are targeting). Neither member is public in 5.4, so this may also fail.
    //   Skeleton->ReferenceSkeleton = RefSkeleton;
    //   Skeleton->CalculateInvRefMatrices();
#endif

    Package->MarkPackageDirty();

    const FString FileName = FPackageName::LongPackageNameToFilename(
        PackagePath, FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags       = RF_Public | RF_Standalone;
    SaveArgs.Error               = GError;
    SaveArgs.bWarnOfLongFilename = false;
    UPackage::SavePackage(Package, Skeleton, *FileName, SaveArgs);

    FAssetRegistryModule::GetRegistry().AssetCreated(Skeleton);

    return Skeleton;
}