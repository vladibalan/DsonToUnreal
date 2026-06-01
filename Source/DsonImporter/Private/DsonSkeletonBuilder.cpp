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

    // Build world-space position map for local-space conversion
    TMap<FString, FVector> BoneWorldPositions;
    BoneWorldPositions.Reserve(Bones.Num());
    for (const FBoneEntry& B : Bones)
        BoneWorldPositions.Add(B.Id, B.Transform.GetTranslation());

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

        FTransform LocalTransform = B.Transform;
        if (ParentRefIndex != INDEX_NONE)
        {
            const FBoneEntry* ParentEntry = Bones.FindByPredicate(
                [&](const FBoneEntry& E){ return E.Id == B.ParentId; });
            if (ParentEntry)
            {
                const FVector ParentWorldPos = ParentEntry->Transform.GetTranslation();
                const FVector LocalPos = B.Transform.GetTranslation() - ParentWorldPos;
                LocalTransform.SetTranslation(LocalPos);
            }
        }

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

FTransform FDsonSkeletonBuilder::MakeBoneTransform(uint64_t DsfHandle, int32 NodeIndex, double UnitScale)
{
    const double CX = GDsonParser.GetNodeCenterPointX ? GDsonParser.GetNodeCenterPointX(DsfHandle, NodeIndex) : 0.0;
    const double CY = GDsonParser.GetNodeCenterPointY ? GDsonParser.GetNodeCenterPointY(DsfHandle, NodeIndex) : 0.0;
    const double CZ = GDsonParser.GetNodeCenterPointZ ? GDsonParser.GetNodeCenterPointZ(DsfHandle, NodeIndex) : 0.0;

    // DAZ (Y-up, right-hand) → UE5 (Z-up, left-hand): X←Z, Y←X, Z←Y
    const double ToCm = UnitScale ;
    const FVector Translation(CZ * ToCm, CX * ToCm, CY * ToCm);

    const double OX = GDsonParser.GetNodeOrientationX ? GDsonParser.GetNodeOrientationX(DsfHandle, NodeIndex) : 0.0;
    const double OY = GDsonParser.GetNodeOrientationY ? GDsonParser.GetNodeOrientationY(DsfHandle, NodeIndex) : 0.0;
    const double OZ = GDsonParser.GetNodeOrientationZ ? GDsonParser.GetNodeOrientationZ(DsfHandle, NodeIndex) : 0.0;

    // Apply the same axis remap to the Euler angles (degrees).
    // DAZ_X → UE_Y (Pitch), DAZ_Y → UE_Z (Yaw), DAZ_Z → UE_X (Roll)
    // FRotator ctor order: (Pitch, Yaw, Roll)
    const FQuat Rotation = FRotator(
        static_cast<float>(OX),   // Pitch  ← DAZ_OX
        static_cast<float>(OY),   // Yaw    ← DAZ_OY
        static_cast<float>(OZ)    // Roll   ← DAZ_OZ
    ).Quaternion();

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
            UE_LOG(LogDsonImporter, Log,
                TEXT("  Bone[%d] '%s' parent=%d pos=(%.2f, %.2f, %.2f)"),
                b,
                *DumpSkel.GetBoneName(b).ToString(),
                DumpSkel.GetRawParentIndex(b),
                Pose.GetTranslation().X,
                Pose.GetTranslation().Y,
                Pose.GetTranslation().Z);
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