#include "DsonSkinWeightsBuilder.h"
#include "DsonImporter.h"
#include "DsonParserFunctions.h"
#include "DsonImportUtils.h"

#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "ReferenceSkeleton.h"
#include "MeshDescription.h"
#include "SkeletalMeshAttributes.h"
#include "BoneWeights.h"

/*
 * Intent:
 * - Apply DAZ skin binding data to the UE skeletal mesh.
 * - Map DSF joint/node IDs to UE skeleton bone indices.
 * - Write capped, normalized vertex influences into mesh description skin-weight attributes.
 *
 * Read this file for missing influences, wrong bone mapping, or skin deformation problems.
 */

static TArray<UE::AnimationCore::FBoneWeight> MakeRootBoneFallbackWeights()
{
    TArray<UE::AnimationCore::FBoneWeight> Weights;
    Weights.Add(UE::AnimationCore::FBoneWeight(
        static_cast<FBoneIndexType>(0), 1.0f));
    return Weights;
}

struct FVertexInfluenceRecord
{
    FString NodeId;
    double Weight = 0.0;
};

static bool ReadCappedVertexInfluence(
    uint64_t DsfHandle,
    int32 SkinModIdx,
    int32 VertexIdx,
    int32 InfluenceIdx,
    FVertexInfluenceRecord& OutInfluence)
{
    const char* BoneNodeIdRaw = nullptr;
    double Weight = 0.0;

    if (!GDsonParser.GetVertexBoneInfluenceCapped ||
        !GDsonParser.GetVertexBoneInfluenceCapped(
            DsfHandle, SkinModIdx, VertexIdx, InfluenceIdx, 8, &BoneNodeIdRaw, &Weight))
        return false;

    OutInfluence.NodeId = DsonImportUtils::NormalizeDazId(BoneNodeIdRaw);
    OutInfluence.Weight = Weight;
    return true;
}

// ---------------------------------------------------------------------------
// FindSkinModifierIndex
// ---------------------------------------------------------------------------

int32 FDsonSkinWeightsBuilder::FindSkinModifierIndex(uint64_t DsfHandle)
{
    if (!GDsonParser.GetModifierCount || !GDsonParser.GetModifierSkinVertexCount)
        return -1;

    const int32 ModCount = GDsonParser.GetModifierCount(DsfHandle);
    if (ModCount <= 0)
        return -1;

    TArray<int32> SkinModifiers;
    for (int32 i = 0; i < ModCount; ++i)
    {
        if (GDsonParser.GetModifierSkinVertexCount(DsfHandle, i) > 0)
            SkinModifiers.Add(i);
    }

    if (SkinModifiers.IsEmpty())
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonSkinWeightsBuilder: no skin modifier found in DSF"));
        return -1;
    }

    if (SkinModifiers.Num() > 1)
    {
        FString Indices;
        for (int32 Idx : SkinModifiers)
            Indices += FString::Printf(TEXT(" %d"), Idx);
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonSkinWeightsBuilder: multiple skin modifiers found at indices%s, using first"),
            *Indices);
    }

    return SkinModifiers[0];
}

// ---------------------------------------------------------------------------
// BuildBoneIdMap
// ---------------------------------------------------------------------------

void FDsonSkinWeightsBuilder::BuildBoneIdMap(
    const USkeleton* Skeleton,
    TMap<FString, int32>& OutMap)
{
    const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
    const int32 BoneCount = RefSkel.GetRawBoneNum();
    OutMap.Reserve(BoneCount);
    for (int32 b = 0; b < BoneCount; ++b)
    {
        OutMap.Add(RefSkel.GetBoneName(b).ToString(), b);
    }
}

void FDsonSkinWeightsBuilder::BuildDazNodeIdToBoneIndexMap(
    uint64_t DsfHandle,
    int32 SkinModIdx,
    const USkeleton* Skeleton,
    TMap<FString, int32>& OutMap)
{
    TMap<FString, int32> BoneNameMap;
    BuildBoneIdMap(Skeleton, BoneNameMap);

    if (!GDsonParser.GetSkinJointCount || !GDsonParser.GetSkinJointNodeId)
        return;

    const int32 JointCount = GDsonParser.GetSkinJointCount(DsfHandle, SkinModIdx);
    OutMap.Reserve(JointCount);

    for (int32 j = 0; j < JointCount; ++j)
    {
        const char* RawNodeId = GDsonParser.GetSkinJointNodeId(DsfHandle, SkinModIdx, j);
        if (!RawNodeId)
            continue;

        const FString NodeId = DsonImportUtils::NormalizeDazId(RawNodeId);

        const int32* BoneIdxPtr = BoneNameMap.Find(NodeId);
        if (!BoneIdxPtr)
        {
            UE_LOG(LogDsonImporter, Warning,
                TEXT("DsonSkinWeightsBuilder: skin joint node id '%s' not found in skeleton, skipping"),
                *NodeId);
            continue;
        }

        OutMap.Add(NodeId, *BoneIdxPtr);
    }
}

// ---------------------------------------------------------------------------
// Apply
// ---------------------------------------------------------------------------

bool FDsonSkinWeightsBuilder::Apply(
    uint64_t DsfHandle,
    USkeletalMesh* Mesh,
    USkeleton* Skeleton)
{
    // Step 1 - Guard
    if (!GDsonParser.IsValid())
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonSkinWeightsBuilder: DsonParser API not fully loaded"));
        return false;
    }

    // Step 2 - Find skin modifier
    const int32 SkinModIdx = FindSkinModifierIndex(DsfHandle);
    if (SkinModIdx < 0)
        return false;

    // Step 3 - Get vertex count from MeshDescription LOD 0
    FMeshDescription* MeshDesc = Mesh->GetMeshDescription(0);
    if (!MeshDesc)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonSkinWeightsBuilder: MeshDescription LOD 0 not found on mesh"));
        return false;
    }
    const int32 VertCount = MeshDesc->Vertices().Num();

    // Step 4 - Build DAZ node-id to UE5 bone-index lookup from the skeleton.
    TMap<FString, int32> DazNodeIdToBoneIndex;
    BuildDazNodeIdToBoneIndexMap(DsfHandle, SkinModIdx, Skeleton, DazNodeIdToBoneIndex);

    // Step 5 - Get skin weight attribute
    FSkeletalMeshAttributes SkelAttribs(*MeshDesc);
    FSkinWeightsVertexAttributesRef SkinWeights = SkelAttribs.GetVertexSkinWeights();

    // Step 6 - Iterate vertices and assign influences
    using namespace UE::AnimationCore;

    TSet<FString> WarnedNodeIds;
    int32 FallbackCount = 0;

    for (int32 i = 0; i < VertCount; ++i)
    {
        // a. Capped influence count for this vertex
        const int32 InfluenceCount = GDsonParser.GetVertexInfluenceCount
            ? GDsonParser.GetVertexInfluenceCount(DsfHandle, SkinModIdx, i, 8) : 0;

        // b. No influences - assign root-bone fallback
        if (InfluenceCount <= 0)
        {
            ++FallbackCount;
            SkinWeights.Set(FVertexID(i), MakeRootBoneFallbackWeights());
            continue;
        }

        // c. Build per-vertex influence list
        TArray<FBoneWeight> Influences;
        Influences.Reserve(InfluenceCount);

        for (int32 k = 0; k < InfluenceCount; ++k)
        {
            FVertexInfluenceRecord Influence;
            if (!ReadCappedVertexInfluence(DsfHandle, SkinModIdx, i, k, Influence))
                break;

            const int32* BoneIdxPtr = DazNodeIdToBoneIndex.Find(Influence.NodeId);
            if (!BoneIdxPtr)
            {
                if (!WarnedNodeIds.Contains(Influence.NodeId))
                {
                    WarnedNodeIds.Add(Influence.NodeId);
                    UE_LOG(LogDsonImporter, Warning,
                        TEXT("DsonSkinWeightsBuilder: unknown bone node id '%s', skipping influence"),
                        *Influence.NodeId);
                }
                continue;
            }

            Influences.Add(FBoneWeight(
                static_cast<FBoneIndexType>(*BoneIdxPtr),
                static_cast<float>(Influence.Weight)));
        }

        // d. All influences had unknown node ids - fall back to root bone
        if (Influences.IsEmpty())
        {
            ++FallbackCount;
            Influences = MakeRootBoneFallbackWeights();
        }

        // e. Commit
        SkinWeights.Set(FVertexID(i), Influences);
    }

    // Step 7 - Log aggregate fallback count
    if (FallbackCount > 0)
    {
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonSkinWeightsBuilder: %d vertices had no skin weights, assigned root-bone fallback"),
            FallbackCount);
    }

    return true;
}
