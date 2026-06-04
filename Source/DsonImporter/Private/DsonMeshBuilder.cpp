#include "DsonMeshBuilder.h"
#include "DsonImporter.h"
#include "DsonAssetUtils.h"
#include "DsonParserFunctions.h"
#include "DsonLoadedDocument.h"
#include "DsonSkinWeightsBuilder.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Animation/Skeleton.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "IMeshBuilderModule.h"
#include "ImportUtils/SkeletalMeshImportUtils.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "MeshDescription.h"
#include "SkeletalMeshAttributes.h"
#include "BoneWeights.h"

/*
 * Intent:
 * - Build a UE USkeletalMesh from geometry DSF data and optional UV-set DSF data.
 * - Create vertices, triangles, UVs, polygon/material groups, and material slots.
 * - Call FDsonSkinWeightsBuilder before committing mesh description data.
 *
 * Read this file for geometry conversion, UV assignment, material slot mapping, and mesh save issues.
 */

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------

USkeletalMesh* FDsonMeshBuilder::Build(
    const FDsonImportSettings& Settings,
    USkeleton* Skeleton,
    const TMap<FString, UMaterialInstanceConstant*>& MaterialsByGroup,
    UMaterial* DefaultMaterial,
    const FString& UvSetDsfPath)
{
    // Public orchestration wrapper: open the figure DSF, build the mesh asset, then
    // always release the parser handle before returning.
    if (!GDsonParser.IsValid())
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonMeshBuilder: DsonParser API not fully loaded"));
        return nullptr;
    }

    FDsonLoadedDocument DsfDocument;
    if (!DsfDocument.LoadFromFileAsError(Settings.ResolvedFigureDsfPath, TEXT("DsonMeshBuilder")))
        return nullptr;

    return CreateMeshAsset(Settings, Skeleton, DsfDocument.GetHandle64(),
        MaterialsByGroup, DefaultMaterial, UvSetDsfPath);
}

namespace
{
    struct FDsonUvData
    {
        FDsonUvData() = default;
        FDsonUvData(FDsonUvData&&) = default;
        FDsonUvData& operator=(FDsonUvData&&) = default;
        FDsonUvData(const FDsonUvData&) = delete;
        FDsonUvData& operator=(const FDsonUvData&) = delete;

        FDsonLoadedDocument UvDocument;
        uint64_t UvHandle = 0;
        int32 UVSetCount = 0;
        TArray<FVector2f> UVs;
        TArray<int32> UVPolyVertIndices;
    };

    struct FDsonTriangle
    {
        int32 VertIndex[3];
        int32 UVIndex[3];
        int32 MaterialIndex;
    };

    static TArray<FString> ReadMaterialGroupNames(uint64_t DsfHandle)
    {
        const int32 RawMatGroupCount = GDsonParser.GetPolygonMaterialGroupCount
            ? GDsonParser.GetPolygonMaterialGroupCount(DsfHandle, 0) : 0;
        if (RawMatGroupCount < 0)
            UE_LOG(LogDsonImporter, Warning,
                TEXT("DsonMeshBuilder: GetPolygonMaterialGroupCount returned %d for geom 0"), RawMatGroupCount);

        const int32 MatGroupCount = FMath::Max(0, RawMatGroupCount);
        TArray<FString> MaterialGroupNames;
        MaterialGroupNames.Reserve(MatGroupCount);
        for (int32 m = 0; m < MatGroupCount; ++m)
        {
            const char* NameRaw = GDsonParser.GetPolygonMaterialGroupName
                ? GDsonParser.GetPolygonMaterialGroupName(DsfHandle, 0, m) : nullptr;
            MaterialGroupNames.Add(NameRaw
                ? UTF8_TO_TCHAR(NameRaw)
                : FString::Printf(TEXT("MatGroup_%d"), m));
        }

        return MaterialGroupNames;
    }

    static TArray<FVector3f> ReadVertexPositions(uint64_t DsfHandle)
    {
        const double UnitScale = GDsonParser.GetUnitScale
            ? GDsonParser.GetUnitScale(DsfHandle) : 1.0 / 100.0;
        const double ToCm = UnitScale;

        const int32 RawVertCount = GDsonParser.GetVertexCount
            ? GDsonParser.GetVertexCount(DsfHandle, 0) : 0;
        if (RawVertCount < 0)
            UE_LOG(LogDsonImporter, Warning,
                TEXT("DsonMeshBuilder: GetVertexCount returned %d for geom 0"), RawVertCount);

        const int32 VertCount = FMath::Max(0, RawVertCount);
        TArray<FVector3f> Positions;
        Positions.Reserve(VertCount);
        for (int32 i = 0; i < VertCount; ++i)
        {
            const double VX = GDsonParser.GetVertexX ? GDsonParser.GetVertexX(DsfHandle, 0, i) : 0.0;
            const double VY = GDsonParser.GetVertexY ? GDsonParser.GetVertexY(DsfHandle, 0, i) : 0.0;
            const double VZ = GDsonParser.GetVertexZ ? GDsonParser.GetVertexZ(DsfHandle, 0, i) : 0.0;
            // DAZ (Y-up, RH) -> UE5 (Z-up, LH) with handedness flip:
            // UE_X=DAZ_Z, UE_Y=-DAZ_X, UE_Z=DAZ_Y. The -DAZ_X reflection converts
            // right-handed DAZ to left-handed UE and MUST match DsonSkeletonBuilder's
            // bone conversion, or mesh and skeleton are mirrored relative to each other
            // and skin weights tear. Reflection also flips polygon winding, which
            // ReadTriangles accounts for by preserving source corner order.
            Positions.Add(FVector3f(
                static_cast<float>(VZ * ToCm),
                static_cast<float>(-VX * ToCm),
                static_cast<float>(VY * ToCm)
            ));
        }

        return Positions;
    }

    static void AddMeshMaterialSlot(
        USkeletalMesh* Mesh,
        const TMap<FString, UMaterialInstanceConstant*>& MaterialsByGroup,
        UMaterial* DefaultMaterial,
        const FString& GroupName,
        int32 SlotIdx)
    {
        FSkeletalMaterial Mat;
        Mat.ImportedMaterialSlotName = FName(*GroupName);

        UMaterialInterface* Assigned = nullptr;
        bool bFallback = false;
        if (UMaterialInstanceConstant* const* Found = MaterialsByGroup.Find(GroupName))
        {
            Assigned = *Found;
        }
        if (!Assigned)
        {
            Assigned = DefaultMaterial;
            bFallback = true;
        }
        Mat.MaterialInterface = Assigned;
        Mesh->GetMaterials().Add(Mat);

        if (bFallback)
        {
            UE_LOG(LogDsonImporter, Warning,
                TEXT("[wire] no MIC for group \"%s\" — using M_DazDefault"), *GroupName);
            UE_LOG(LogDsonImporter, Log,
                TEXT("[wire] section %d -> %s -> M_DazDefault (no MIC for group \"%s\")"),
                SlotIdx, *GroupName, *GroupName);
        }
        else
        {
            UE_LOG(LogDsonImporter, Log,
                TEXT("[wire] section %d -> %s -> %s"),
                SlotIdx, *GroupName, *Assigned->GetName());
        }
    }

    static void PopulateMeshMaterialSlots(
        USkeletalMesh* Mesh,
        const TArray<FString>& MaterialGroupNames,
        const TMap<FString, UMaterialInstanceConstant*>& MaterialsByGroup,
        UMaterial* DefaultMaterial)
    {
        if (MaterialGroupNames.Num() == 0)
        {
            AddMeshMaterialSlot(Mesh, MaterialsByGroup, DefaultMaterial, FString(TEXT("DefaultMaterial")), 0);
            return;
        }

        for (int32 m = 0; m < MaterialGroupNames.Num(); ++m)
        {
            AddMeshMaterialSlot(Mesh, MaterialsByGroup, DefaultMaterial, MaterialGroupNames[m], m);
        }
    }

    static FSkeletalMeshLODModel& PrepareSkeletalMeshLod0(USkeletalMesh* Mesh)
    {
        Mesh->ResetLODInfo();
        FSkeletalMeshLODInfo& NewLODInfo = Mesh->AddLODInfo();
        NewLODInfo.ReductionSettings.NumOfTrianglesPercentage = 1.0f;
        NewLODInfo.ReductionSettings.NumOfVertPercentage      = 1.0f;
        NewLODInfo.ReductionSettings.MaxDeviationPercentage   = 0.0f;
        NewLODInfo.LODHysteresis                              = 0.02f;

        // CommitMeshDescription stamps bulk-data fields onto LODModels[0], so the
        // imported model slot must exist before commit.
        FSkeletalMeshModel* ImportedResource = Mesh->GetImportedModel();
        ImportedResource->LODModels.Empty();
        ImportedResource->LODModels.Add(new FSkeletalMeshLODModel());
        return ImportedResource->LODModels[0];
    }

    static void PopulateMeshDescriptionBones(
        FSkeletalMeshAttributes& SkelAttribs,
        const USkeleton* Skeleton)
    {
        const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
        SkelAttribs.ReserveNewBones(RefSkel.GetRawBoneNum());

        FSkeletalMeshAttributes::FBoneNameAttributesRef        BoneNames   = SkelAttribs.GetBoneNames();
        FSkeletalMeshAttributes::FBoneParentIndexAttributesRef BoneParents = SkelAttribs.GetBoneParentIndices();
        FSkeletalMeshAttributes::FBonePoseAttributesRef        BonePoses   = SkelAttribs.GetBonePoses();

        for (int32 b = 0; b < RefSkel.GetRawBoneNum(); ++b)
        {
            const FBoneID BoneID = SkelAttribs.CreateBone();
            BoneNames  .Set(BoneID, RefSkel.GetBoneName(b));
            BoneParents.Set(BoneID, RefSkel.GetRawParentIndex(b));
            BonePoses  .Set(BoneID, RefSkel.GetRawRefBonePose()[b]);
        }
    }

    static TArray<FVertexID> PopulateMeshDescriptionVertices(
        FMeshDescription& MeshDesc,
        FSkeletalMeshAttributes& SkelAttribs,
        const TArray<FVector3f>& Positions)
    {
        TVertexAttributesRef<FVector3f> VertexPositions   = SkelAttribs.GetVertexPositions();
        FSkinWeightsVertexAttributesRef VertexSkinWeights = SkelAttribs.GetVertexSkinWeights();

        TArray<FVertexID> VertexIDs;
        VertexIDs.Reserve(Positions.Num());

        using namespace UE::AnimationCore;
        TArray<FBoneWeight> SingleInfluence;
        SingleInfluence.Add(FBoneWeight(static_cast<FBoneIndexType>(0), 1.0f));

        for (int32 i = 0; i < Positions.Num(); ++i)
        {
            const FVertexID VID = MeshDesc.CreateVertex();
            VertexIDs.Add(VID);
            VertexPositions  .Set(VID, Positions[i]);
            VertexSkinWeights.Set(VID, SingleInfluence);
        }

        return VertexIDs;
    }

    static void PopulateMeshDescriptionTriangles(
        FMeshDescription& MeshDesc,
        FSkeletalMeshAttributes& SkelAttribs,
        USkeletalMesh* Mesh,
        const TArray<FVertexID>& VertexIDs,
        const TArray<FDsonTriangle>& Triangles,
        const TArray<FVector2f>& UVs)
    {
        TVertexInstanceAttributesRef<FVector2f> VertexInstanceUVs      = SkelAttribs.GetVertexInstanceUVs();
        TPolygonGroupAttributesRef<FName>       PolyGroupMaterialNames = SkelAttribs.GetPolygonGroupMaterialSlotNames();

        VertexInstanceUVs.SetNumChannels(1);

        const int32 NumMaterials = Mesh->GetMaterials().Num();
        TArray<FPolygonGroupID> PolyGroups;
        PolyGroups.Reserve(NumMaterials);
        for (int32 m = 0; m < NumMaterials; ++m)
        {
            const FPolygonGroupID PGID = MeshDesc.CreatePolygonGroup();
            PolyGroupMaterialNames.Set(PGID, Mesh->GetMaterials()[m].ImportedMaterialSlotName);
            PolyGroups.Add(PGID);
        }

        TArray<FVertexInstanceID> CornerInstances;
        CornerInstances.SetNum(3);
        for (int32 t = 0; t < Triangles.Num(); ++t)
        {
            const FDsonTriangle& Tri = Triangles[t];
            const int32 SafeMatIdx = FMath::Clamp(Tri.MaterialIndex, 0, PolyGroups.Num() - 1);

            for (int32 c = 0; c < 3; ++c)
            {
                const FVertexID VID = VertexIDs[Tri.VertIndex[c]];
                const FVertexInstanceID VIID = MeshDesc.CreateVertexInstance(VID);
                const FVector2f UV = UVs.IsValidIndex(Tri.UVIndex[c])
                    ? UVs[Tri.UVIndex[c]] : FVector2f::ZeroVector;
                VertexInstanceUVs.Set(VIID, 0, UV);
                CornerInstances[c] = VIID;
            }

            MeshDesc.CreateTriangle(PolyGroups[SafeMatIdx], CornerInstances);
        }
    }

    static bool PopulateMeshRefSkeleton(USkeletalMesh* Mesh, USkeleton* Skeleton)
    {
        const FReferenceSkeleton& SkelRef = Skeleton->GetReferenceSkeleton();
        FSkeletalMeshImportData TempData;
        TempData.RefBonesBinary.Reserve(SkelRef.GetRawBoneNum());
        for (int32 b = 0; b < SkelRef.GetRawBoneNum(); ++b)
        {
            SkeletalMeshImportData::FBone Bone;
            Bone.Name        = SkelRef.GetBoneName(b).ToString();
            Bone.ParentIndex = SkelRef.GetRawParentIndex(b);
            Bone.NumChildren = 0;
            Bone.BonePos.Transform = FTransform3f(SkelRef.GetRawRefBonePose()[b]);
            TempData.RefBonesBinary.Add(Bone);
        }

        int32 SkeletalDepth = 0;
        if (!SkeletalMeshImportUtils::ProcessImportMeshSkeleton(
                Skeleton, Mesh->GetRefSkeleton(), SkeletalDepth, TempData))
        {
            UE_LOG(LogDsonImporter, Error,
                TEXT("DsonMeshBuilder: ProcessImportMeshSkeleton failed"));
            return false;
        }

        return true;
    }

    static TArray<FDsonTriangle> ReadTriangles(
        uint64_t DsfHandle,
        int32 FaceCount,
        const TArray<int32>& UVPolyVertIndices)
    {
        TArray<FDsonTriangle> Triangles;
        Triangles.Reserve(FaceCount * 2);

        int32 UVFlatOffset = 0;
        for (int32 f = 0; f < FaceCount; ++f)
        {
            const int32 RawCornerCount = GDsonParser.GetPolylistFaceVertexCount
                ? GDsonParser.GetPolylistFaceVertexCount(DsfHandle, 0, f) : 0;
            if (RawCornerCount < 0)
                UE_LOG(LogDsonImporter, Warning,
                    TEXT("DsonMeshBuilder: GetPolylistFaceVertexCount returned %d for geom 0 face %d"), RawCornerCount, f);
            const int32 CornerCount = FMath::Max(0, RawCornerCount);
            const int32 MatIdx = GDsonParser.GetPolylistFaceMaterialIndex
                ? GDsonParser.GetPolylistFaceMaterialIndex(DsfHandle, 0, f) : 0;

            if (CornerCount < 3)
            {
                UVFlatOffset += CornerCount;
                continue;
            }
            if (CornerCount > 4)
            {
                UE_LOG(LogDsonImporter, Warning,
                    TEXT("DsonMeshBuilder: face %d has %d corners (>4), skipping"), f, CornerCount);
                UVFlatOffset += CornerCount;
                continue;
            }

            int32 VIdx[4]  = {};
            int32 UVIdx[4] = {};
            for (int32 c = 0; c < CornerCount; ++c)
            {
                VIdx[c] = GDsonParser.GetPolylistFaceVertex
                    ? GDsonParser.GetPolylistFaceVertex(DsfHandle, 0, f, c) : 0;

                UVIdx[c] = UVPolyVertIndices.IsValidIndex(UVFlatOffset + c)
                    ? UVPolyVertIndices[UVFlatOffset + c] : 0;
            }

            // Winding: the vertex conversion negates one axis (a reflection), which flips
            // winding sense once relative to the source. The source mesh, brought into UE's
            // left-handed space, needs exactly one net flip to face normals outward. The
            // axis negation supplies it, so keep the natural source corner order here.
            FDsonTriangle& T0 = Triangles.AddDefaulted_GetRef();
            T0.VertIndex[0] = VIdx[0];  T0.VertIndex[1] = VIdx[1];  T0.VertIndex[2] = VIdx[2];
            T0.UVIndex[0]   = UVIdx[0]; T0.UVIndex[1]   = UVIdx[1]; T0.UVIndex[2]   = UVIdx[2];
            T0.MaterialIndex = MatIdx;

            if (CornerCount == 4)
            {
                FDsonTriangle& T1 = Triangles.AddDefaulted_GetRef();
                T1.VertIndex[0] = VIdx[0];  T1.VertIndex[1] = VIdx[2];  T1.VertIndex[2] = VIdx[3];
                T1.UVIndex[0]   = UVIdx[0]; T1.UVIndex[1]   = UVIdx[2]; T1.UVIndex[2]   = UVIdx[3];
                T1.MaterialIndex = MatIdx;
            }

            UVFlatOffset += CornerCount;
        }

        return Triangles;
    }

    static void ReadUvSetValues(uint64_t UvHandle, FDsonUvData& OutData)
    {
        UE_LOG(LogDsonImporter, Log,
            TEXT("[uv] fn-ptrs: SetCount=%d Count=%d U=%d V=%d PVIxCount=%d PVIx=%d VtxCount=%d OvrCount=%d OvrFace=%d OvrCorner=%d OvrUVIdx=%d"),
            GDsonParser.GetUVSetCount ? 1 : 0,
            GDsonParser.GetUVCount ? 1 : 0,
            GDsonParser.GetUVU ? 1 : 0,
            GDsonParser.GetUVV ? 1 : 0,
            GDsonParser.GetUVPolygonVertexIndexCount ? 1 : 0,
            GDsonParser.GetUVPolygonVertexIndex ? 1 : 0,
            GDsonParser.GetUVSetVertexCount ? 1 : 0,
            GDsonParser.GetUVOverrideCount ? 1 : 0,
            GDsonParser.GetUVOverrideFace ? 1 : 0,
            GDsonParser.GetUVOverrideCorner ? 1 : 0,
            GDsonParser.GetUVOverrideUVIndex ? 1 : 0);

        const int32 RawUVSetCount = GDsonParser.GetUVSetCount
            ? GDsonParser.GetUVSetCount(UvHandle) : 0;
        if (RawUVSetCount < 0)
            UE_LOG(LogDsonImporter, Warning,
                TEXT("DsonMeshBuilder: GetUVSetCount returned %d"), RawUVSetCount);
        OutData.UVSetCount = FMath::Max(0, RawUVSetCount);
        UE_LOG(LogDsonImporter, Log, TEXT("[uv] UVSetCount=%d (raw=%d)"),
            OutData.UVSetCount, RawUVSetCount);

        if (OutData.UVSetCount > 0)
        {
            const int32 RawUVCount = GDsonParser.GetUVCount
                ? GDsonParser.GetUVCount(UvHandle, 0) : 0;
            if (RawUVCount < 0)
                UE_LOG(LogDsonImporter, Warning,
                    TEXT("DsonMeshBuilder: GetUVCount returned %d for uvset 0"), RawUVCount);
            const int32 UVCount = FMath::Max(0, RawUVCount);
            OutData.UVs.Reserve(UVCount);
            for (int32 i = 0; i < UVCount; ++i)
            {
                const double U = GDsonParser.GetUVU ? GDsonParser.GetUVU(UvHandle, 0, i) : 0.0;
                const double V = GDsonParser.GetUVV ? GDsonParser.GetUVV(UvHandle, 0, i) : 0.0;
                OutData.UVs.Add(FVector2f((float)U, 1.0f - (float)V));
            }
        }

        UE_LOG(LogDsonImporter, Log, TEXT("[uv] UVs.Num()=%d"), OutData.UVs.Num());
        if (OutData.UVs.Num() > 0)
        {
            const int32 LastIdx = FMath::Min(OutData.UVs.Num() - 1, 99);
            UE_LOG(LogDsonImporter, Log,
                TEXT("[uv] UVs[0]=(%.4f,%.4f) UVs[%d]=(%.4f,%.4f)"),
                OutData.UVs[0].X, OutData.UVs[0].Y,
                LastIdx, OutData.UVs[LastIdx].X, OutData.UVs[LastIdx].Y);
        }
    }

    static void ExpandUvCornerIndices(
        uint64_t DsfHandle,
        uint64_t UvHandle,
        int32 FaceCount,
        FDsonUvData& OutData)
    {
        if (OutData.UVSetCount <= 0 || !GDsonParser.GetPolylistCount ||
            !GDsonParser.GetPolylistFaceVertexCount || !GDsonParser.GetPolylistFaceVertex)
        {
            UE_LOG(LogDsonImporter, Log, TEXT("[uv] UVPolyVertIndices.Num()=%d"),
                OutData.UVPolyVertIndices.Num());
            return;
        }

        TArray<int32> FaceCornerOffsets;
        FaceCornerOffsets.Reserve(FaceCount);

        int32 RunningCorners = 0;
        for (int32 f = 0; f < FaceCount; ++f)
        {
            FaceCornerOffsets.Add(RunningCorners);
            RunningCorners += GDsonParser.GetPolylistFaceVertexCount(DsfHandle, 0, f);
        }

        OutData.UVPolyVertIndices.SetNumUninitialized(RunningCorners);
        for (int32 f = 0; f < FaceCount; ++f)
        {
            const int32 CornerCount = GDsonParser.GetPolylistFaceVertexCount(DsfHandle, 0, f);
            const int32 Offset = FaceCornerOffsets[f];
            for (int32 c = 0; c < CornerCount; ++c)
            {
                OutData.UVPolyVertIndices[Offset + c] = GDsonParser.GetPolylistFaceVertex(DsfHandle, 0, f, c);
            }
        }

        const int32 OverrideCount = GDsonParser.GetUVOverrideCount
            ? GDsonParser.GetUVOverrideCount(UvHandle, 0) : 0;
        int32 AppliedOverrides                  = 0;
        int32 SkippedFaceOob                    = 0;
        int32 SkippedCornerNeg                  = 0;
        int32 SkippedUvIdxNeg                   = 0;
        int32 SkippedVertexNotInFace            = 0;
        int32 SkippedUvIdxOob                   = 0;

        const int32 SamplesToLog                    = FMath::Min(OverrideCount, 10);
        int32       VertNotInFaceSamples            = 0;
        const int32 VertNotInFaceSamplesMax         = 5;

        for (int32 i = 0; i < OverrideCount; ++i)
        {
            const int32 Face = GDsonParser.GetUVOverrideFace ? GDsonParser.GetUVOverrideFace(UvHandle, 0, i) : -1;
            const int32 VertIdx = GDsonParser.GetUVOverrideCorner ? GDsonParser.GetUVOverrideCorner(UvHandle, 0, i) : -1;
            const int32 UvIdx = GDsonParser.GetUVOverrideUVIndex ? GDsonParser.GetUVOverrideUVIndex(UvHandle, 0, i) : -1;

            if (i < SamplesToLog)
            {
                UE_LOG(LogDsonImporter, Log,
                    TEXT("[uv] override[%d] face=%d vertIdx=%d uv=%d"),
                    i, Face, VertIdx, UvIdx);
            }

            if (Face < 0 || Face >= FaceCount) { ++SkippedFaceOob; continue; }
            if (VertIdx < 0)                   { ++SkippedCornerNeg; continue; }
            if (UvIdx < 0)                     { ++SkippedUvIdxNeg; continue; }
            if (UvIdx >= OutData.UVs.Num())    { ++SkippedUvIdxOob; continue; }

            const int32 CornerCount = (Face + 1 < FaceCount)
                ? FaceCornerOffsets[Face + 1] - FaceCornerOffsets[Face]
                : RunningCorners - FaceCornerOffsets[Face];

            int32 MatchedCorner = -1;
            for (int32 c = 0; c < CornerCount; ++c)
            {
                if (GDsonParser.GetPolylistFaceVertex(DsfHandle, 0, Face, c) == VertIdx)
                {
                    MatchedCorner = c;
                    break;
                }
            }

            if (MatchedCorner < 0)
            {
                ++SkippedVertexNotInFace;
                if (VertNotInFaceSamples < VertNotInFaceSamplesMax)
                {
                    FString FaceVerts;
                    for (int32 c = 0; c < CornerCount; ++c)
                    {
                        if (c > 0) FaceVerts += TEXT(",");
                        FaceVerts += FString::FromInt(GDsonParser.GetPolylistFaceVertex(DsfHandle, 0, Face, c));
                    }
                    UE_LOG(LogDsonImporter, Log,
                        TEXT("[uv] skip[vert-not-in-face] override[%d]: face=%d vertIdx=%d faceVerts=[%s] uv=%d"),
                        i, Face, VertIdx, *FaceVerts, UvIdx);
                    ++VertNotInFaceSamples;
                }
                continue;
            }

            OutData.UVPolyVertIndices[FaceCornerOffsets[Face] + MatchedCorner] = UvIdx;
            ++AppliedOverrides;
        }

        UE_LOG(LogDsonImporter, Log,
            TEXT("[uv] expansion: faces=%d corners=%d uvs=%d overrides applied=%d skipped: face-oob=%d vert-neg=%d uv-idx-neg=%d uv-idx-oob=%d vert-not-in-face=%d"),
            FaceCount, RunningCorners, OutData.UVs.Num(),
            AppliedOverrides,
            SkippedFaceOob, SkippedCornerNeg, SkippedUvIdxNeg, SkippedUvIdxOob, SkippedVertexNotInFace);

        UE_LOG(LogDsonImporter, Log, TEXT("[uv] UVPolyVertIndices.Num()=%d"),
            OutData.UVPolyVertIndices.Num());
        if (OutData.UVPolyVertIndices.Num() >= 6)
        {
            UE_LOG(LogDsonImporter, Log,
                TEXT("[uv] UVPolyVertIndices[0..5]=%d,%d,%d,%d,%d,%d"),
                OutData.UVPolyVertIndices[0], OutData.UVPolyVertIndices[1], OutData.UVPolyVertIndices[2],
                OutData.UVPolyVertIndices[3], OutData.UVPolyVertIndices[4], OutData.UVPolyVertIndices[5]);
        }
    }

    static FDsonUvData ReadUvData(uint64_t DsfHandle, const FString& UvSetDsfPath, int32 FaceCount)
    {
        FDsonUvData Data;
        Data.UvHandle = DsfHandle;

        if (!UvSetDsfPath.IsEmpty() && Data.UvDocument.LoadFromFileAsWarning(UvSetDsfPath, TEXT("[uv]")))
        {
            Data.UvHandle = Data.UvDocument.GetHandle64();
            UE_LOG(LogDsonImporter, Log,
                TEXT("[uv] opened UV set DSF in second session"));
        }

        ReadUvSetValues(Data.UvHandle, Data);
        ExpandUvCornerIndices(DsfHandle, Data.UvHandle, FaceCount, Data);
        return Data;
    }
}

// ---------------------------------------------------------------------------
// CreateMeshAsset
// ---------------------------------------------------------------------------

USkeletalMesh* FDsonMeshBuilder::CreateMeshAsset(
    const FDsonImportSettings& Settings,
    USkeleton* Skeleton,
    uint64_t DsfHandle,
    const TMap<FString, UMaterialInstanceConstant*>& MaterialsByGroup,
    UMaterial* DefaultMaterial,
    const FString& UvSetDsfPath)
{
    // Step 1 — Find the first geometry in the DSF
    // Main geometry conversion path. It assumes geometry index 0 is the base mesh and
    // that material group names will match keys produced by FDsonMaterialBuilder.
    const int32 RawGeomCount = GDsonParser.GetGeometryCount
        ? GDsonParser.GetGeometryCount(DsfHandle) : 0;
    if (RawGeomCount < 0)
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonMeshBuilder: GetGeometryCount returned %d, clamping to 0"), RawGeomCount);
    const int32 GeomCount = FMath::Max(0, RawGeomCount);
    if (GeomCount == 0)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonMeshBuilder: no geometry found in '%s'"),
            *Settings.ResolvedFigureDsfPath);
        return nullptr;
    }
    // geomIndex = 0: base mesh only

    // Step 2 — Read vertices
    const TArray<FVector3f> Positions = ReadVertexPositions(DsfHandle);

    const int32 RawFaceCount = GDsonParser.GetPolylistCount
        ? GDsonParser.GetPolylistCount(DsfHandle, 0) : 0;
    if (RawFaceCount < 0)
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonMeshBuilder: GetPolylistCount returned %d for geom 0"), RawFaceCount);
    const int32 FaceCount = FMath::Max(0, RawFaceCount);

    // Step 3 — Read UV values and expand sparse DAZ overrides into face-corner indices
    FDsonUvData UvData = ReadUvData(DsfHandle, UvSetDsfPath, FaceCount);
    const TArray<FVector2f>& UVs = UvData.UVs;
    const TArray<int32>& UVPolyVertIndices = UvData.UVPolyVertIndices;

    // Step 4 — Read material group names
    const TArray<FString> MaterialGroupNames = ReadMaterialGroupNames(DsfHandle);

    // Step 5 — Read faces and triangulate
    const TArray<FDsonTriangle> Triangles = ReadTriangles(DsfHandle, FaceCount, UVPolyVertIndices);

    // Step 6 — Create USkeletalMesh asset
    const FString MeshName    = FPaths::GetBaseFilename(Settings.ResolvedFigureDsfPath) + TEXT("_SkeletalMesh");
    const FString PackagePath = TEXT("/Game/DazImports/") + MeshName;

    UPackage* Package = FDsonAssetUtils::CreateLoadedPackage(PackagePath, TEXT("DsonMeshBuilder"));
    if (!Package)
        return nullptr;

    USkeletalMesh* Mesh = NewObject<USkeletalMesh>(Package, *MeshName, RF_Public | RF_Standalone);
    if (!Mesh)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonMeshBuilder: failed to create USkeletalMesh in '%s'"), *PackagePath);
        return nullptr;
    }

    // Step 7 — Build FMeshDescription for LOD 0 directly and commit it

    // 7a — Populate mesh material slots (must exist before polygon groups reference them)
    PopulateMeshMaterialSlots(Mesh, MaterialGroupNames, MaterialsByGroup, DefaultMaterial);

    // 7b — LODInfo and LODModels[0] must exist before Create/CommitMeshDescription
    FSkeletalMeshLODModel& LODModel = PrepareSkeletalMeshLod0(Mesh);

    // 7c — Create the MeshDescription and register all skeletal mesh attributes
    FMeshDescription* MeshDesc = Mesh->CreateMeshDescription(0);
    check(MeshDesc);
    FSkeletalMeshAttributes SkelAttribs(*MeshDesc);
    SkelAttribs.Register();

    // 7d — Populate bone attributes from the reference skeleton
    PopulateMeshDescriptionBones(SkelAttribs, Skeleton);

    // 7e — Vertex positions and skin weights (one root-bone influence per vertex)
    const TArray<FVertexID> VertexIDs = PopulateMeshDescriptionVertices(*MeshDesc, SkelAttribs, Positions);

    // 7f — UV channels, polygon groups, vertex instances, and triangles
    PopulateMeshDescriptionTriangles(*MeshDesc, SkelAttribs, Mesh, VertexIDs, Triangles, UVs);

    // Apply real skin weights from the DSF skin modifier (replaces placeholder)
    if (!FDsonSkinWeightsBuilder::Apply(DsfHandle, Mesh, Skeleton))
    {
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonMeshBuilder: skin weight application failed, "
                 "mesh will use root-bone fallback for all vertices"));
        // Non-fatal: continue with placeholder weights
    }

    // 7g — Commit to bulk storage (replaces deprecated SaveLODImportedData)
    if (!Mesh->CommitMeshDescription(0))
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonMeshBuilder: CommitMeshDescription failed for '%s'"),
            *Settings.ResolvedFigureDsfPath);
        return nullptr;
    }

    // Step 8 — Build the USkeletalMesh from the committed MeshDescription

    // 8a — Prepare asset
    Mesh->PreEditChange(nullptr);
    Mesh->InvalidateDeriveDataCacheGUID();

    // 8b — Populate Mesh->GetRefSkeleton(); BuildSkeletalMesh reads this, not MeshDescription bones.
    if (!PopulateMeshRefSkeleton(Mesh, Skeleton))
        return nullptr;

    // 8c — Bounds, vertex-color state, and tex-coord count
    FBox3f BoundingBox(Positions.GetData(), Positions.Num());
    Mesh->SetImportedBounds(FBoxSphereBounds(FBox(BoundingBox)));
    Mesh->SetHasVertexColors(false);
    Mesh->SetVertexColorGuid(FGuid());
    LODModel.NumTexCoords = 1;

    // 8d — Build settings, then invoke IMeshBuilderModule (requires LODInfo + MeshDescription)
    Mesh->GetLODInfo(0)->BuildSettings.bRecomputeNormals  = true;
    Mesh->GetLODInfo(0)->BuildSettings.bRecomputeTangents = true;

    IMeshBuilderModule& MeshBuilderModule = IMeshBuilderModule::GetForRunningPlatform();
    FSkeletalMeshBuildParameters BuildParams(
        Mesh,
        GetTargetPlatformManagerRef().GetRunningTargetPlatform(),
        /*LODIndex=*/0,
        /*bRegenDepLODs=*/false);
    if (!MeshBuilderModule.BuildSkeletalMesh(BuildParams))
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonMeshBuilder: BuildSkeletalMesh failed for '%s'"),
            *Settings.ResolvedFigureDsfPath);
        return nullptr;
    }

    // 8e — Post-build: inv-ref matrices, skeleton merge, then skeleton assignment
    Mesh->CalculateInvRefMatrices();
    if (!Skeleton->MergeAllBonesToBoneTree(Mesh))
    {
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonMeshBuilder: MergeAllBonesToBoneTree failed — skeleton may be mismatched"));
    }
    Mesh->SetSkeleton(Skeleton);

    if (!Mesh->GetResourceForRendering() || !Mesh->GetResourceForRendering()->LODRenderData.IsValidIndex(0))
    {
        Mesh->Build();
    }

    // Step 9 — Save
    return FDsonAssetUtils::SaveAssetPackage(Package, Mesh, PackagePath, TEXT("DsonMeshBuilder"))
        ? Mesh
        : nullptr;
}
