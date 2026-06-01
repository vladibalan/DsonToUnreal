#include "DsonMeshBuilder.h"
#include "DsonImporter.h"
#include "DsonParserFunctions.h"
#include "SDsonImportWindow.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
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
#include "Engine/SkeletalMesh.h"
#include "ImportUtils/SkeletalMeshImportUtils.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------

USkeletalMesh* FDsonMeshBuilder::Build(const FDsonImportSettings& Settings, USkeleton* Skeleton)
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

    USkeletalMesh* Mesh = CreateMeshAsset(Settings, Skeleton, DsfHandle);

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
    const FDsonImportSettings& Settings, USkeleton* Skeleton, uint64_t DsfHandle)
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
    const double ToCm = UnitScale * 100.0;

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
        // DAZ (Y-up, RH) → UE5 (Z-up, LH): UE_X=DAZ_Z, UE_Y=DAZ_X, UE_Z=DAZ_Y
        Positions.Add(FVector3f(
            (float)(VZ * ToCm),
            (float)(VX * ToCm),
            (float)(VY * ToCm)
        ));
    }

    // Step 3 — Read UV set 0
    const int32 RawUVSetCount = GDsonParser.GetUVSetCount
        ? GDsonParser.GetUVSetCount(DsfHandle, 0) : 0;
    if (RawUVSetCount < 0)
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonMeshBuilder: GetUVSetCount returned %d for geom 0"), RawUVSetCount);
    const int32 UVSetCount = FMath::Max(0, RawUVSetCount);
    TArray<FVector2f> UVs;
    if (UVSetCount > 0)
    {
        const int32 RawUVCount = GDsonParser.GetUVCount
            ? GDsonParser.GetUVCount(DsfHandle, 0, 0) : 0;
        if (RawUVCount < 0)
            UE_LOG(LogDsonImporter, Warning,
                TEXT("DsonMeshBuilder: GetUVCount returned %d for geom 0 uvset 0"), RawUVCount);
        const int32 UVCount = FMath::Max(0, RawUVCount);
        UVs.Reserve(UVCount);
        for (int32 i = 0; i < UVCount; ++i)
        {
            const double U = GDsonParser.GetUVU ? GDsonParser.GetUVU(DsfHandle, 0, 0, i) : 0.0;
            const double V = GDsonParser.GetUVV ? GDsonParser.GetUVV(DsfHandle, 0, 0, i) : 0.0;
            UVs.Add(FVector2f((float)U, 1.0f - (float)V));  // flip V for UE5
        }
    }

    // Step 4 — Read material group names
    const int32 RawMatGroupCount = GDsonParser.GetMaterialGroupCount
        ? GDsonParser.GetMaterialGroupCount(DsfHandle, 0) : 0;
    if (RawMatGroupCount < 0)
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonMeshBuilder: GetMaterialGroupCount returned %d for geom 0"), RawMatGroupCount);
    const int32 MatGroupCount = FMath::Max(0, RawMatGroupCount);
    TArray<FString> MaterialGroupNames;
    MaterialGroupNames.Reserve(MatGroupCount);
    for (int32 m = 0; m < MatGroupCount; ++m)
    {
        const char* NameRaw = GDsonParser.GetMaterialGroupName
            ? GDsonParser.GetMaterialGroupName(DsfHandle, 0, m) : nullptr;
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
            continue;
        if (CornerCount > 4)
        {
            UE_LOG(LogDsonImporter, Warning,
                TEXT("DsonMeshBuilder: face %d has %d corners (>4), skipping"), f, CornerCount);
            continue;
        }

        int32 VIdx[4]  = {};
        int32 UVIdx[4] = {};
        for (int32 c = 0; c < CornerCount; ++c)
        {
            VIdx[c] = GDsonParser.GetPolylistFaceVertex
                ? GDsonParser.GetPolylistFaceVertex(DsfHandle, 0, f, c) : 0;

            UVIdx[c] = (UVSetCount > 0 && GDsonParser.GetUVPolygonVertexIndex)
                ? GDsonParser.GetUVPolygonVertexIndex(DsfHandle, 0, 0, f, c) : 0;
        }

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

    // Step 7 — Populate FSkeletalMeshImportData
    FSkeletalMeshImportData ImportData;

    // Material slots
    for (const FString& GroupName : MaterialGroupNames)
    {
        SkeletalMeshImportData::FMaterial Mat;
        Mat.MaterialImportName = GroupName;
        ImportData.Materials.Add(Mat);
    }
    if (ImportData.Materials.IsEmpty())
    {
        SkeletalMeshImportData::FMaterial DefaultMat;
        DefaultMat.MaterialImportName = TEXT("DefaultMaterial");
        ImportData.Materials.Add(DefaultMat);
    }

    // Points (vertex positions)
    ImportData.Points.Reserve(Positions.Num());
    for (const FVector3f& P : Positions)
        ImportData.Points.Add(P);

    // Wedges (one per triangle corner)
    ImportData.Wedges.Reserve(Triangles.Num() * 3);
    for (const FDsonTriangle& Tri : Triangles)
    {
        for (int32 c = 0; c < 3; ++c)
        {
            SkeletalMeshImportData::FVertex Wedge;
            Wedge.VertexIndex = (uint32)Tri.VertIndex[c];
            Wedge.UVs[0]      = UVs.IsValidIndex(Tri.UVIndex[c])
                                    ? UVs[Tri.UVIndex[c]]
                                    : FVector2f::ZeroVector;
            Wedge.MatIndex    = (uint16)FMath::Clamp(Tri.MaterialIndex, 0,
                                    ImportData.Materials.Num() - 1);
            Wedge.Color       = FColor::White;
            ImportData.Wedges.Add(Wedge);
        }
    }

    // Faces (one per triangle)
    ImportData.Faces.Reserve(Triangles.Num());
    for (int32 t = 0; t < Triangles.Num(); ++t)
    {
        SkeletalMeshImportData::FTriangle Face;
        Face.WedgeIndex[0]   = (uint32)(t * 3 + 0);
        Face.WedgeIndex[1]   = (uint32)(t * 3 + 1);
        Face.WedgeIndex[2]   = (uint32)(t * 3 + 2);
        Face.MatIndex        = (uint16)FMath::Clamp(
                                   Triangles[t].MaterialIndex, 0,
                                   ImportData.Materials.Num() - 1);
        Face.AuxMatIndex     = Face.MatIndex;
        Face.SmoothingGroups = 255;
        Face.TangentX[0] = Face.TangentX[1] = Face.TangentX[2] = FVector3f::ZeroVector;
        Face.TangentY[0] = Face.TangentY[1] = Face.TangentY[2] = FVector3f::ZeroVector;
        Face.TangentZ[0] = Face.TangentZ[1] = Face.TangentZ[2] = FVector3f::ZeroVector;
        ImportData.Faces.Add(Face);
    }

    ImportData.NumTexCoords        = 1;
    ImportData.bHasVertexColors    = false;
    ImportData.bHasNormals         = false;
    ImportData.bHasTangents        = false;

    // Influences — one root-bone influence per vertex (Phase 5 replaces these)
    ImportData.Influences.Reserve(Positions.Num());
    for (int32 i = 0; i < Positions.Num(); ++i)
    {
        SkeletalMeshImportData::FRawBoneInfluence Inf;
        Inf.VertexIndex = (uint32)i;
        Inf.BoneIndex   = 0;
        Inf.Weight      = 1.0f;
        ImportData.Influences.Add(Inf);
    }

    // PointToRawMap — identity map required by SaveLODImportedData → GetMeshDescription;
    // without it GetMeshDescription skips the ImportPointIndex vertex attribute entirely.
    ImportData.PointToRawMap.SetNumUninitialized(Positions.Num());
    for (int32 i = 0; i < Positions.Num(); ++i)
        ImportData.PointToRawMap[i] = i;

    // Step 8 — Build the USkeletalMesh from import data
    // Follows the exact sequence of FFbxImporter::ImportSkeletalMesh()
    // (FbxSkeletalMeshImport.cpp lines 1869–2161)

    // 8a — Prepare the mesh object for editing and dirty the DDC key
    Mesh->PreEditChange(nullptr);
    Mesh->InvalidateDeriveDataCacheGUID();

    // 8b — Set up LODModels (must be empty, then have exactly one entry at index 0)
    FSkeletalMeshModel* ImportedResource = Mesh->GetImportedModel();
    ImportedResource->LODModels.Empty();
    ImportedResource->LODModels.Add(new FSkeletalMeshLODModel());
    FSkeletalMeshLODModel& LODModel = ImportedResource->LODModels[0];

    // 8c — Process materials into mesh material slots
    SkeletalMeshImportUtils::ProcessImportMeshMaterials(Mesh->GetMaterials(), ImportData);

    // 8d — Populate RefBonesBinary from the USkeleton, then build Mesh->GetRefSkeleton()
    {
        const FReferenceSkeleton& SkelRefSkeleton = Skeleton->GetReferenceSkeleton();
        ImportData.RefBonesBinary.Empty();
        ImportData.RefBonesBinary.Reserve(SkelRefSkeleton.GetRawBoneNum());
        for (int32 b = 0; b < SkelRefSkeleton.GetRawBoneNum(); ++b)
        {
            SkeletalMeshImportData::FBone Bone;
            Bone.Name        = SkelRefSkeleton.GetBoneName(b).ToString();
            Bone.ParentIndex = SkelRefSkeleton.GetRawParentIndex(b);
            Bone.NumChildren = 0; // filled by ProcessImportMeshSkeleton
            const FTransform& BonePose = SkelRefSkeleton.GetRawRefBonePose()[b];
            Bone.BonePos.Transform = FTransform3f(BonePose);
            ImportData.RefBonesBinary.Add(Bone);
        }
    }

    int32 SkeletalDepth = 0;
    if (!SkeletalMeshImportUtils::ProcessImportMeshSkeleton(
            Skeleton, Mesh->GetRefSkeleton(), SkeletalDepth, ImportData))
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("DsonMeshBuilder: ProcessImportMeshSkeleton failed"));
        return nullptr;
    }

    // 8e — Normalize and sort bone influences
    SkeletalMeshImportUtils::ProcessImportMeshInfluences(ImportData, Mesh->GetPathName());

    // 8f — LODInfo: must exist before SaveLODImportedData and BuildSkeletalMesh
    Mesh->ResetLODInfo();
    FSkeletalMeshLODInfo& NewLODInfo = Mesh->AddLODInfo();
    NewLODInfo.ReductionSettings.NumOfTrianglesPercentage = 1.0f;
    NewLODInfo.ReductionSettings.NumOfVertPercentage      = 1.0f;
    NewLODInfo.ReductionSettings.MaxDeviationPercentage   = 0.0f;
    NewLODInfo.LODHysteresis                              = 0.02f;

    // 8g — Convert ImportData to MeshDescription and commit it; must precede Build
    PRAGMA_DISABLE_DEPRECATION_WARNINGS
    Mesh->SaveLODImportedData(0, ImportData);
    PRAGMA_ENABLE_DEPRECATION_WARNINGS

    // 8h — Bounds, vertex-color state, and tex-coord count
    FBox3f BoundingBox(ImportData.Points.GetData(), ImportData.Points.Num());
    Mesh->SetImportedBounds(FBoxSphereBounds(FBox(BoundingBox)));
    Mesh->SetHasVertexColors(ImportData.bHasVertexColors);
    Mesh->SetVertexColorGuid(Mesh->GetHasVertexColors() ? FGuid::NewGuid() : FGuid());
    LODModel.NumTexCoords = FMath::Max<uint32>(1, ImportData.NumTexCoords);

    // 8i — Build settings, then invoke IMeshBuilderModule (requires LODInfo + MeshDescription)
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

    // 8j — Post-build: inv-ref matrices, skeleton merge, then skeleton assignment
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