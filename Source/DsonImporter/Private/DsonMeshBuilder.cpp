#include "DsonMeshBuilder.h"
#include "DsonImporter.h"
#include "DsonParserFunctions.h"
#include "DsonSkinWeightsBuilder.h"
#include "SDsonImportWindow.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Animation/Skeleton.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "PackageTools.h"
#include "IMeshBuilderModule.h"
#include "ImportUtils/SkeletalMeshImportUtils.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "MeshDescription.h"
#include "SkeletalMeshAttributes.h"
#include "BoneWeights.h"

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------

USkeletalMesh* FDsonMeshBuilder::Build(
    const FDsonImportSettings& Settings,
    USkeleton* Skeleton,
    const TMap<FString, UMaterialInstanceConstant*>& MaterialsByGroup,
    UMaterial* DefaultMaterial)
{
    if (!GDsonParser.IsValid())
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonMeshBuilder: DsonParser API not fully loaded"));
        return nullptr;
    }

    uint64_t DsfHandle = 0;
    if (!LoadDsfDocument(Settings.ResolvedFigureDsfPath, DsfHandle))
        return nullptr;

    USkeletalMesh* Mesh = CreateMeshAsset(Settings, Skeleton, DsfHandle, MaterialsByGroup, DefaultMaterial);

    GDsonParser.Destroy(reinterpret_cast<DsonDocumentHandle>(DsfHandle));
    return Mesh;
}

// ---------------------------------------------------------------------------
// LoadDsfDocument
// ---------------------------------------------------------------------------

bool FDsonMeshBuilder::LoadDsfDocument(const FString& Path, uint64_t& OutHandle)
{
    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *Path))
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonMeshBuilder: failed to read file '%s'"), *Path);
        return false;
    }

    FTCHARToUTF8 Utf8Converter(*FileContent);
    const char* Utf8Str = Utf8Converter.Get();

    DsonDocumentHandle RawHandle = GDsonParser.Create();
    if (!RawHandle)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonMeshBuilder: GDsonParser.Create() returned null"));
        return false;
    }

    const int32 Result = GDsonParser.LoadFromString(RawHandle, Utf8Str);
    if (Result != 0)
    {
        const char* ErrRaw = GDsonParser.GetLastError ? GDsonParser.GetLastError() : nullptr;
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonMeshBuilder: LoadFromString failed for '%s': %s"),
            *Path, ErrRaw ? UTF8_TO_TCHAR(ErrRaw) : TEXT("unknown error"));
        GDsonParser.Destroy(RawHandle);
        return false;
    }

    OutHandle = reinterpret_cast<uint64_t>(RawHandle);
    return true;
}

// ---------------------------------------------------------------------------
// CreateMeshAsset
// ---------------------------------------------------------------------------

USkeletalMesh* FDsonMeshBuilder::CreateMeshAsset(
    const FDsonImportSettings& Settings,
    USkeleton* Skeleton,
    uint64_t DsfHandle,
    const TMap<FString, UMaterialInstanceConstant*>& MaterialsByGroup,
    UMaterial* DefaultMaterial)
{
    // Step 1 — Find the first geometry in the DSF
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
    double UnitScale = GDsonParser.GetUnitScale
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
        // DAZ (Y-up, RH) → UE5 (Z-up, LH) with handedness flip:
        // UE_X=DAZ_Z, UE_Y=-DAZ_X, UE_Z=DAZ_Y. The -DAZ_X reflection converts
        // right-handed DAZ to left-handed UE and MUST match DsonSkeletonBuilder's
        // bone conversion, or mesh and skeleton are mirrored relative to each other
        // and skin weights tear. (Reflection also flips polygon winding — handled
        // in the triangulation step below.)
        Positions.Add(FVector3f(
            (float)(VZ * ToCm),
            (float)(-VX * ToCm),
            (float)(VY * ToCm)
        ));
    }

    // Step 3 — Read UV set 0
    const int32 RawUVSetCount = GDsonParser.GetUVSetCount
        ? GDsonParser.GetUVSetCount(DsfHandle) : 0;
    if (RawUVSetCount < 0)
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonMeshBuilder: GetUVSetCount returned %d"), RawUVSetCount);
    const int32 UVSetCount = FMath::Max(0, RawUVSetCount);
    TArray<FVector2f> UVs;
    if (UVSetCount > 0)
    {
        const int32 RawUVCount = GDsonParser.GetUVCount
            ? GDsonParser.GetUVCount(DsfHandle, 0) : 0;
        if (RawUVCount < 0)
            UE_LOG(LogDsonImporter, Warning,
                TEXT("DsonMeshBuilder: GetUVCount returned %d for uvset 0"), RawUVCount);
        const int32 UVCount = FMath::Max(0, RawUVCount);
        UVs.Reserve(UVCount);
        for (int32 i = 0; i < UVCount; ++i)
        {
            const double U = GDsonParser.GetUVU ? GDsonParser.GetUVU(DsfHandle, 0, i) : 0.0;
            const double V = GDsonParser.GetUVV ? GDsonParser.GetUVV(DsfHandle, 0, i) : 0.0;
            UVs.Add(FVector2f((float)U, 1.0f - (float)V));  // flip V for UE5
        }
    }

    // Step 4 — Read material group names
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

    // Step 5 — Read faces and triangulate
    struct FDsonTriangle
    {
        int32 VertIndex[3];
        int32 UVIndex[3];
        int32 MaterialIndex;
    };

    TArray<FDsonTriangle> Triangles;
    const int32 RawFaceCount = GDsonParser.GetPolylistCount
        ? GDsonParser.GetPolylistCount(DsfHandle, 0) : 0;
    if (RawFaceCount < 0)
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonMeshBuilder: GetPolylistCount returned %d for geom 0"), RawFaceCount);
    const int32 FaceCount = FMath::Max(0, RawFaceCount);
    Triangles.Reserve(FaceCount * 2);

    // Build flat UV polygon vertex index array for uvset 0
    TArray<int32> UVPolyVertIndices;
    if (UVSetCount > 0 && GDsonParser.GetUVPolygonVertexIndexCount && GDsonParser.GetUVPolygonVertexIndex)
    {
        const int32 FlatCount = GDsonParser.GetUVPolygonVertexIndexCount(DsfHandle, 0);
        UVPolyVertIndices.Reserve(FlatCount > 0 ? FlatCount : 0);
        for (int32 i = 0; i < FlatCount; ++i)
            UVPolyVertIndices.Add(GDsonParser.GetUVPolygonVertexIndex(DsfHandle, 0, i));
    }

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
        // left-handed space, needs exactly one net flip to face normals outward — the
        // axis negation supplies it, so we keep the NATURAL corner order here.
        // (Empirically verified: natural order + Y-negation gives outward normals;
        // reversing here as well would double-flip and leave normals inward.)

        // Triangle 1: corners (0, 1, 2)
        FDsonTriangle& T0 = Triangles.AddDefaulted_GetRef();
        T0.VertIndex[0] = VIdx[0];  T0.VertIndex[1] = VIdx[1];  T0.VertIndex[2] = VIdx[2];
        T0.UVIndex[0]   = UVIdx[0]; T0.UVIndex[1]   = UVIdx[1]; T0.UVIndex[2]   = UVIdx[2];
        T0.MaterialIndex = MatIdx;

        if (CornerCount == 4)
        {
            // Triangle 2: corners (0, 2, 3)
            FDsonTriangle& T1 = Triangles.AddDefaulted_GetRef();
            T1.VertIndex[0] = VIdx[0];  T1.VertIndex[1] = VIdx[2];  T1.VertIndex[2] = VIdx[3];
            T1.UVIndex[0]   = UVIdx[0]; T1.UVIndex[1]   = UVIdx[2]; T1.UVIndex[2]   = UVIdx[3];
            T1.MaterialIndex = MatIdx;
        }

        UVFlatOffset += CornerCount;
    }

    // Step 6 — Create USkeletalMesh asset
    const FString MeshName    = FPaths::GetBaseFilename(Settings.ResolvedFigureDsfPath) + TEXT("_SkeletalMesh");
    const FString PackagePath = TEXT("/Game/DazImports/") + MeshName;

    UPackage* Package = CreatePackage(*PackagePath);
    if (!Package)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonMeshBuilder: failed to create package '%s'"), *PackagePath);
        return nullptr;
    }
    Package->FullyLoad();

    USkeletalMesh* Mesh = NewObject<USkeletalMesh>(Package, *MeshName, RF_Public | RF_Standalone);
    if (!Mesh)
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonMeshBuilder: failed to create USkeletalMesh in '%s'"), *PackagePath);
        return nullptr;
    }

    // Step 7 — Build FMeshDescription for LOD 0 directly and commit it

    // 7a — Populate mesh material slots (must exist before polygon groups reference them)
    const auto AssignSlot = [&](const FString& GroupName, int32 SlotIdx)
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
    };

    if (MaterialGroupNames.Num() == 0)
    {
        AssignSlot(TEXT("DefaultMaterial"), 0);
    }
    else
    {
        for (int32 m = 0; m < MaterialGroupNames.Num(); ++m)
        {
            AssignSlot(MaterialGroupNames[m], m);
        }
    }

    // 8d — LODInfo (required before CreateMeshDescription)
    Mesh->ResetLODInfo();
    FSkeletalMeshLODInfo& NewLODInfo = Mesh->AddLODInfo();
    NewLODInfo.ReductionSettings.NumOfTrianglesPercentage = 1.0f;
    NewLODInfo.ReductionSettings.NumOfVertPercentage      = 1.0f;
    NewLODInfo.ReductionSettings.MaxDeviationPercentage   = 0.0f;
    NewLODInfo.LODHysteresis                              = 0.02f;

    // 8b — LODModels[0] must exist before CommitMeshDescription (line 2514 of SkeletalMesh.cpp
    //      ensures LODModels.IsValidIndex(0) to stamp bulk-data fields onto the LODModel)
    FSkeletalMeshModel* ImportedResource = Mesh->GetImportedModel();
    ImportedResource->LODModels.Empty();
    ImportedResource->LODModels.Add(new FSkeletalMeshLODModel());
    FSkeletalMeshLODModel& LODModel = ImportedResource->LODModels[0];

    // 7b — Create the MeshDescription and register all skeletal mesh attributes
    FMeshDescription* MeshDesc = Mesh->CreateMeshDescription(0);
    check(MeshDesc);
    FSkeletalMeshAttributes SkelAttribs(*MeshDesc);
    SkelAttribs.Register();

    // 7c — Populate bone attributes from the reference skeleton
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

    // 7d — Vertex positions and skin weights (one root-bone influence per vertex)
    TVertexAttributesRef<FVector3f> VertexPositions   = SkelAttribs.GetVertexPositions();
    FSkinWeightsVertexAttributesRef VertexSkinWeights = SkelAttribs.GetVertexSkinWeights();

    TArray<FVertexID> VertexIDs;
    VertexIDs.Reserve(Positions.Num());
    {
        using namespace UE::AnimationCore;
        TArray<FBoneWeight> SingleInfluence;
        SingleInfluence.Add(FBoneWeight(static_cast<FBoneIndexType>(0), 1.0f));

        for (int32 i = 0; i < Positions.Num(); ++i)
        {
            const FVertexID VID = MeshDesc->CreateVertex();
            VertexIDs.Add(VID);
            VertexPositions  .Set(VID, Positions[i]);
            VertexSkinWeights.Set(VID, SingleInfluence);
        }
    }

    // 7e — UV channels, polygon groups, vertex instances, and triangles
    TVertexInstanceAttributesRef<FVector2f> VertexInstanceUVs      = SkelAttribs.GetVertexInstanceUVs();
    TPolygonGroupAttributesRef<FName>       PolyGroupMaterialNames = SkelAttribs.GetPolygonGroupMaterialSlotNames();

    VertexInstanceUVs.SetNumChannels(1);

    const int32 NumMaterials = Mesh->GetMaterials().Num();
    TArray<FPolygonGroupID> PolyGroups;
    PolyGroups.Reserve(NumMaterials);
    for (int32 m = 0; m < NumMaterials; ++m)
    {
        const FPolygonGroupID PGID = MeshDesc->CreatePolygonGroup();
        PolyGroupMaterialNames.Set(PGID, Mesh->GetMaterials()[m].ImportedMaterialSlotName);
        PolyGroups.Add(PGID);
    }

    {
        TArray<FVertexInstanceID> CornerInstances;
        CornerInstances.SetNum(3);
        for (int32 t = 0; t < Triangles.Num(); ++t)
        {
            const FDsonTriangle& Tri       = Triangles[t];
            const int32          SafeMatIdx = FMath::Clamp(Tri.MaterialIndex, 0, PolyGroups.Num() - 1);

            for (int32 c = 0; c < 3; ++c)
            {
                const FVertexID         VID  = VertexIDs[Tri.VertIndex[c]];
                const FVertexInstanceID VIID = MeshDesc->CreateVertexInstance(VID);
                const FVector2f         UV   = UVs.IsValidIndex(Tri.UVIndex[c])
                                                   ? UVs[Tri.UVIndex[c]] : FVector2f::ZeroVector;
                VertexInstanceUVs.Set(VIID, 0, UV);
                CornerInstances[c] = VIID;
            }

            MeshDesc->CreateTriangle(PolyGroups[SafeMatIdx], CornerInstances);
        }
    }

    // Apply real skin weights from the DSF skin modifier (replaces placeholder)
    if (!FDsonSkinWeightsBuilder::Apply(DsfHandle, Mesh, Skeleton))
    {
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonMeshBuilder: skin weight application failed, "
                 "mesh will use root-bone fallback for all vertices"));
        // Non-fatal: continue with placeholder weights
    }

    // 7f — Commit to bulk storage (replaces deprecated SaveLODImportedData)
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

    // 8c — Populate Mesh->GetRefSkeleton() via a minimal FSkeletalMeshImportData;
    //      BuildSkeletalMesh reads the mesh's own FReferenceSkeleton, not the MeshDescription bones.
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
            return nullptr;
        }
    }

    // 8e — Bounds, vertex-color state, and tex-coord count
    FBox3f BoundingBox(Positions.GetData(), Positions.Num());
    Mesh->SetImportedBounds(FBoxSphereBounds(FBox(BoundingBox)));
    Mesh->SetHasVertexColors(false);
    Mesh->SetVertexColorGuid(FGuid());
    LODModel.NumTexCoords = 1;

    // 8f — Build settings, then invoke IMeshBuilderModule (requires LODInfo + MeshDescription)
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

    // 8g — Post-build: inv-ref matrices, skeleton merge, then skeleton assignment
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
    Package->MarkPackageDirty();

    const FString FileName = FPackageName::LongPackageNameToFilename(
        PackagePath, FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags       = RF_Public | RF_Standalone;
    SaveArgs.Error               = GError;
    SaveArgs.bWarnOfLongFilename = false;
    UPackage::SavePackage(Package, Mesh, *FileName, SaveArgs);

    FAssetRegistryModule::GetRegistry().AssetCreated(Mesh);
    return Mesh;
}