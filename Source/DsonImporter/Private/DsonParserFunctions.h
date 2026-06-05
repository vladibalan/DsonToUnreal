#pragma once

#include <cstdint>

// Opaque handle returned by DsonDocument_Create and passed to all API functions
typedef void* DsonDocumentHandle;

// Function pointer types for each API function currently called by the plugin
typedef DsonDocumentHandle (*Fn_Create)();
typedef void               (*Fn_Destroy)(DsonDocumentHandle handle);
typedef int                (*Fn_LoadFromString)(DsonDocumentHandle handle, const char* json);
typedef const char*        (*Fn_GetLastError)();
typedef const char*        (*Fn_GetAssetType)(DsonDocumentHandle handle);
typedef const char*        (*Fn_GetAssetId)(DsonDocumentHandle handle);
typedef int                (*Fn_GetSceneNodeCount)(DsonDocumentHandle handle);
typedef int                (*Fn_GetSceneNodeGeometryCount)(DsonDocumentHandle handle, int sceneNodeIndex);
typedef const char*        (*Fn_GetSceneNodeGeometryUrl)(DsonDocumentHandle handle, int sceneNodeIndex, int geomRefIndex);

// Node iteration
typedef int32_t            (*DsonDocument_GetNodeCountFn)(uint64_t handle);
typedef const char*        (*DsonDocument_GetNodeIdFn)(uint64_t handle, int32_t nodeIndex);
typedef const char*        (*DsonDocument_GetNodeNameFn)(uint64_t handle, int32_t nodeIndex);
typedef const char*        (*DsonDocument_GetNodeTypeFn)(uint64_t handle, int32_t nodeIndex);
typedef const char*        (*DsonDocument_GetNodeParentFn)(uint64_t handle, int32_t nodeIndex);
typedef double             (*DsonDocument_GetNodeCenterPointXFn)(uint64_t handle, int32_t nodeIndex);
typedef double             (*DsonDocument_GetNodeCenterPointYFn)(uint64_t handle, int32_t nodeIndex);
typedef double             (*DsonDocument_GetNodeCenterPointZFn)(uint64_t handle, int32_t nodeIndex);
typedef double             (*DsonDocument_GetNodeOrientationXFn)(uint64_t handle, int32_t nodeIndex);
typedef double             (*DsonDocument_GetNodeOrientationYFn)(uint64_t handle, int32_t nodeIndex);
typedef double             (*DsonDocument_GetNodeOrientationZFn)(uint64_t handle, int32_t nodeIndex);
typedef const char*        (*DsonDocument_GetNodeRotationOrderFn)(uint64_t handle, int32_t nodeIndex);
typedef double             (*DsonDocument_GetNodeGeneralScaleFn)(uint64_t handle, int32_t nodeIndex);
typedef double             (*DsonDocument_GetUnitScaleFn)(uint64_t handle);

// Geometry
typedef int32_t  (*DsonDocument_GetGeometryCountFn)(uint64_t handle);
typedef int32_t  (*DsonDocument_GetVertexCountFn)(uint64_t handle, int32_t geomIndex);
typedef double   (*DsonDocument_GetVertexXFn)(uint64_t handle, int32_t geomIndex, int32_t vertIndex);
typedef double   (*DsonDocument_GetVertexYFn)(uint64_t handle, int32_t geomIndex, int32_t vertIndex);
typedef double   (*DsonDocument_GetVertexZFn)(uint64_t handle, int32_t geomIndex, int32_t vertIndex);
typedef int32_t  (*DsonDocument_GetPolylistCountFn)(uint64_t handle, int32_t geomIndex);
typedef int32_t  (*DsonDocument_GetPolylistFaceVertexCountFn)(uint64_t handle, int32_t geomIndex, int32_t faceIndex);
typedef int32_t  (*DsonDocument_GetPolylistFaceVertexFn)(uint64_t handle, int32_t geomIndex, int32_t faceIndex, int32_t cornerIndex);
typedef int32_t  (*DsonDocument_GetPolylistFaceMaterialIndexFn)(uint64_t handle, int32_t geomIndex, int32_t faceIndex);
typedef int32_t  (*DsonDocument_GetUVSetCountFn)(uint64_t handle);
typedef int32_t  (*DsonDocument_GetUVCountFn)(uint64_t handle, int32_t uvSetIndex);
typedef double   (*DsonDocument_GetUVUFn)(uint64_t handle, int32_t uvSetIndex, int32_t uvIndex);
typedef double   (*DsonDocument_GetUVVFn)(uint64_t handle, int32_t uvSetIndex, int32_t uvIndex);
typedef int32_t  (*DsonDocument_GetUVPolygonVertexIndexCountFn)(uint64_t handle, int32_t uvSetIndex);
typedef int32_t  (*DsonDocument_GetUVPolygonVertexIndexFn)(uint64_t handle, int32_t uvSetIndex, int32_t index);
typedef int (*DsonDocument_GetUVSetVertexCountFn)(uint64_t handle, int32_t uvSetIndex);
typedef int (*DsonDocument_GetUVOverrideCountFn)(uint64_t handle, int32_t uvSetIndex);
typedef int (*DsonDocument_GetUVOverrideFaceFn)(uint64_t handle, int32_t uvSetIndex, int32_t overrideIndex);
typedef int (*DsonDocument_GetUVOverrideCornerFn)(uint64_t handle, int32_t uvSetIndex, int32_t overrideIndex);
typedef int (*DsonDocument_GetUVOverrideUVIndexFn)(uint64_t handle, int32_t uvSetIndex, int32_t overrideIndex);
typedef int         (*DsonDocument_GetPolygonMaterialGroupCountFn)(uint64_t handle, int32_t geomIndex);
typedef const char* (*DsonDocument_GetPolygonMaterialGroupNameFn)(uint64_t handle, int32_t geomIndex, int32_t groupIndex);

// Modifier info
typedef const char* (*DsonDocument_GetModifierIdFn)(uint64_t handle, int32_t index);
typedef const char* (*DsonDocument_GetModifierNameFn)(uint64_t handle, int32_t index);
typedef const char* (*DsonDocument_GetModifierTypeFn)(uint64_t handle, int32_t index);
typedef int32_t     (*DsonDocument_GetModifierCountFn)(uint64_t handle);
typedef int32_t     (*DsonDocument_GetModifierSkinVertexCountFn)(uint64_t handle, int32_t modifierIndex);
typedef int32_t     (*DsonDocument_GetModifierSkinJointCountFn)(uint64_t handle, int32_t modifierIndex);

// Skin weights - joint-based access
typedef int32_t     (*DsonDocument_GetSkinJointCountFn)(uint64_t handle, int32_t modifierIndex);
typedef const char* (*DsonDocument_GetSkinJointNodeIdFn)(uint64_t handle, int32_t modifierIndex, int32_t jointIndex);
typedef int32_t     (*DsonDocument_GetSkinJointWeightCountFn)(uint64_t handle, int32_t modifierIndex, int32_t jointIndex);
typedef int32_t     (*DsonDocument_GetSkinJointWeightVertexIndexFn)(uint64_t handle, int32_t modifierIndex, int32_t jointIndex, int32_t weightIndex);
typedef double      (*DsonDocument_GetSkinJointWeightFn)(uint64_t handle, int32_t modifierIndex, int32_t jointIndex, int32_t weightIndex);

// Skin weights - per-vertex access (capped and normalized)
typedef int32_t     (*DsonDocument_GetVertexInfluenceCountFn)(uint64_t handle, int32_t modifierIndex, int32_t vertexIndex, int32_t maxInfluences);
typedef bool        (*DsonDocument_GetVertexBoneInfluenceFn)(uint64_t handle, int32_t modifierIndex, int32_t vertexIndex, int32_t influenceIndex, const char** boneNodeId, double* weight);
typedef bool        (*DsonDocument_GetVertexBoneInfluenceCappedFn)(uint64_t handle, int32_t modifierIndex, int32_t vertexIndex, int32_t influenceIndex, int32_t maxInfluences, const char** boneNodeId, double* weight);

// Library materials
typedef int32_t     (*DsonDocument_GetMaterialCountFn)(uint64_t handle);
typedef const char* (*DsonDocument_GetMaterialIdFn)(uint64_t handle, int32_t matIndex);
typedef const char* (*DsonDocument_GetMaterialNameFn)(uint64_t handle, int32_t matIndex);
typedef const char* (*DsonDocument_GetMaterialGeometryIdFn)(uint64_t handle, int32_t matIndex);
typedef const char* (*DsonDocument_GetMaterialUVSetIdFn)(uint64_t handle, int32_t matIndex);
typedef const char* (*DsonDocument_GetMaterialTypeFn)(uint64_t handle, int32_t matIndex);
typedef const char* (*DsonDocument_GetMaterialShaderTypeFn)(uint64_t handle, int32_t matIndex);
typedef int32_t     (*DsonDocument_GetMaterialGroupCountFn)(uint64_t handle, int32_t matIndex);
typedef const char* (*DsonDocument_GetMaterialGroupNameFn)(uint64_t handle, int32_t matIndex, int32_t groupIndex);

// Library material channel accessors (indexed)
typedef int32_t     (*DsonDocument_GetMaterialChannelCountFn)(uint64_t handle, int32_t matIndex);
typedef const char* (*DsonDocument_GetMaterialChannelIdFn)(uint64_t handle, int32_t matIndex, int32_t channelIdx);
typedef const char* (*DsonDocument_GetMaterialChannelTypeFn)(uint64_t handle, int32_t matIndex, int32_t channelIdx);
typedef double      (*DsonDocument_GetMaterialChannelValueFn)(uint64_t handle, int32_t matIndex, int32_t channelIdx);
typedef double      (*DsonDocument_GetMaterialChannelColorRFn)(uint64_t handle, int32_t matIndex, int32_t channelIdx);
typedef double      (*DsonDocument_GetMaterialChannelColorGFn)(uint64_t handle, int32_t matIndex, int32_t channelIdx);
typedef double      (*DsonDocument_GetMaterialChannelColorBFn)(uint64_t handle, int32_t matIndex, int32_t channelIdx);
typedef bool        (*DsonDocument_GetMaterialChannelHasColorFn)(uint64_t handle, int32_t matIndex, int32_t channelIdx);
typedef const char* (*DsonDocument_GetMaterialChannelImageUrlFn)(uint64_t handle, int32_t matIndex, int32_t channelIdx);
typedef const char* (*DsonDocument_GetMaterialChannelTexturePathFn)(uint64_t handle, int32_t matIndex, int32_t channelIdx);

// Scene materials
typedef int32_t     (*DsonDocument_GetSceneMaterialCountFn)(uint64_t handle);
typedef const char* (*DsonDocument_GetSceneMaterialIdFn)(uint64_t handle, int32_t matIndex);
typedef const char* (*DsonDocument_GetSceneMaterialUrlFn)(uint64_t handle, int32_t matIndex);
typedef int32_t     (*DsonDocument_GetSceneMaterialGroupCountFn)(uint64_t handle, int32_t matIndex);
typedef const char* (*DsonDocument_GetSceneMaterialGroupNameFn)(uint64_t handle, int32_t matIndex, int32_t groupIndex);

// Scene material surface accessors
typedef const char* (*DsonDocument_GetSceneMaterialNameFn)(uint64_t handle, int32_t sceneMatIndex);
typedef const char* (*DsonDocument_GetSceneMaterialGeometryIdFn)(uint64_t handle, int32_t sceneMatIndex);
typedef const char* (*DsonDocument_GetSceneMaterialUVSetIdFn)(uint64_t handle, int32_t sceneMatIndex);
typedef const char* (*DsonDocument_GetSceneMaterialTypeFn)(uint64_t handle, int32_t sceneMatIndex);
typedef const char* (*DsonDocument_GetSceneMaterialShaderTypeFn)(uint64_t handle, int32_t sceneMatIndex);

// Scene material channel accessors (indexed)
typedef int32_t     (*DsonDocument_GetSceneMaterialChannelCountFn)(uint64_t handle, int32_t sceneMatIndex);
typedef const char* (*DsonDocument_GetSceneMaterialChannelIdFn)(uint64_t handle, int32_t sceneMatIndex, int32_t channelIdx);
typedef const char* (*DsonDocument_GetSceneMaterialChannelTypeFn)(uint64_t handle, int32_t sceneMatIndex, int32_t channelIdx);
typedef double      (*DsonDocument_GetSceneMaterialChannelValueFn)(uint64_t handle, int32_t sceneMatIndex, int32_t channelIdx);
typedef double      (*DsonDocument_GetSceneMaterialChannelColorRFn)(uint64_t handle, int32_t sceneMatIndex, int32_t channelIdx);
typedef double      (*DsonDocument_GetSceneMaterialChannelColorGFn)(uint64_t handle, int32_t sceneMatIndex, int32_t channelIdx);
typedef double      (*DsonDocument_GetSceneMaterialChannelColorBFn)(uint64_t handle, int32_t sceneMatIndex, int32_t channelIdx);
typedef bool        (*DsonDocument_GetSceneMaterialChannelHasColorFn)(uint64_t handle, int32_t sceneMatIndex, int32_t channelIdx);
typedef const char* (*DsonDocument_GetSceneMaterialChannelImageUrlFn)(uint64_t handle, int32_t sceneMatIndex, int32_t channelIdx);
typedef const char* (*DsonDocument_GetSceneMaterialChannelTexturePathFn)(uint64_t handle, int32_t sceneMatIndex, int32_t channelIdx);

struct FDsonParserAPI
{
    Fn_Create                    Create                    = nullptr;
    Fn_Destroy                   Destroy                   = nullptr;
    Fn_LoadFromString            LoadFromString            = nullptr;
    Fn_GetLastError              GetLastError              = nullptr;
    Fn_GetAssetType              GetAssetType              = nullptr;
    Fn_GetAssetId                GetAssetId                = nullptr;
    Fn_GetSceneNodeCount         GetSceneNodeCount         = nullptr;
    Fn_GetSceneNodeGeometryCount GetSceneNodeGeometryCount = nullptr;
    Fn_GetSceneNodeGeometryUrl   GetSceneNodeGeometryUrl   = nullptr;

    DsonDocument_GetNodeCountFn        GetNodeCount        = nullptr;
    DsonDocument_GetNodeIdFn           GetNodeId           = nullptr;
    DsonDocument_GetNodeNameFn         GetNodeName         = nullptr;
    DsonDocument_GetNodeTypeFn         GetNodeType         = nullptr;
    DsonDocument_GetNodeParentFn       GetNodeParent       = nullptr;
    DsonDocument_GetNodeCenterPointXFn GetNodeCenterPointX = nullptr;
    DsonDocument_GetNodeCenterPointYFn GetNodeCenterPointY = nullptr;
    DsonDocument_GetNodeCenterPointZFn GetNodeCenterPointZ = nullptr;
    DsonDocument_GetNodeOrientationXFn GetNodeOrientationX = nullptr;
    DsonDocument_GetNodeOrientationYFn GetNodeOrientationY = nullptr;
    DsonDocument_GetNodeOrientationZFn GetNodeOrientationZ = nullptr;
    DsonDocument_GetNodeRotationOrderFn GetNodeRotationOrder = nullptr;
    DsonDocument_GetNodeGeneralScaleFn  GetNodeGeneralScale  = nullptr;
    DsonDocument_GetUnitScaleFn         GetUnitScale         = nullptr;

    DsonDocument_GetGeometryCountFn            GetGeometryCount            = nullptr;
    DsonDocument_GetVertexCountFn              GetVertexCount              = nullptr;
    DsonDocument_GetVertexXFn                  GetVertexX                  = nullptr;
    DsonDocument_GetVertexYFn                  GetVertexY                  = nullptr;
    DsonDocument_GetVertexZFn                  GetVertexZ                  = nullptr;
    DsonDocument_GetPolylistCountFn            GetPolylistCount            = nullptr;
    DsonDocument_GetPolylistFaceVertexCountFn  GetPolylistFaceVertexCount  = nullptr;
    DsonDocument_GetPolylistFaceVertexFn       GetPolylistFaceVertex       = nullptr;
    DsonDocument_GetPolylistFaceMaterialIndexFn GetPolylistFaceMaterialIndex = nullptr;
    DsonDocument_GetUVSetCountFn                  GetUVSetCount                  = nullptr;
    DsonDocument_GetUVCountFn                     GetUVCount                     = nullptr;
    DsonDocument_GetUVUFn                         GetUVU                         = nullptr;
    DsonDocument_GetUVVFn                         GetUVV                         = nullptr;
    DsonDocument_GetUVPolygonVertexIndexCountFn   GetUVPolygonVertexIndexCount   = nullptr;
    DsonDocument_GetUVPolygonVertexIndexFn        GetUVPolygonVertexIndex        = nullptr;
    DsonDocument_GetUVSetVertexCountFn   GetUVSetVertexCount   = nullptr;
    DsonDocument_GetUVOverrideCountFn    GetUVOverrideCount    = nullptr;
    DsonDocument_GetUVOverrideFaceFn     GetUVOverrideFace     = nullptr;
    DsonDocument_GetUVOverrideCornerFn   GetUVOverrideCorner   = nullptr;
    DsonDocument_GetUVOverrideUVIndexFn  GetUVOverrideUVIndex  = nullptr;
    DsonDocument_GetPolygonMaterialGroupCountFn   GetPolygonMaterialGroupCount   = nullptr;
    DsonDocument_GetPolygonMaterialGroupNameFn    GetPolygonMaterialGroupName    = nullptr;

    DsonDocument_GetModifierIdFn                  GetModifierId                  = nullptr;
    DsonDocument_GetModifierNameFn                GetModifierName                = nullptr;
    DsonDocument_GetModifierTypeFn                GetModifierType                = nullptr;
    DsonDocument_GetModifierCountFn               GetModifierCount               = nullptr;
    DsonDocument_GetModifierSkinVertexCountFn      GetModifierSkinVertexCount     = nullptr;
    DsonDocument_GetModifierSkinJointCountFn       GetModifierSkinJointCount      = nullptr;
    DsonDocument_GetSkinJointCountFn              GetSkinJointCount              = nullptr;
    DsonDocument_GetSkinJointNodeIdFn             GetSkinJointNodeId             = nullptr;
    DsonDocument_GetSkinJointWeightCountFn        GetSkinJointWeightCount        = nullptr;
    DsonDocument_GetSkinJointWeightVertexIndexFn  GetSkinJointWeightVertexIndex  = nullptr;
    DsonDocument_GetSkinJointWeightFn             GetSkinJointWeight             = nullptr;
    DsonDocument_GetVertexInfluenceCountFn        GetVertexInfluenceCount        = nullptr;
    DsonDocument_GetVertexBoneInfluenceFn         GetVertexBoneInfluence         = nullptr;
    DsonDocument_GetVertexBoneInfluenceCappedFn   GetVertexBoneInfluenceCapped   = nullptr;

    DsonDocument_GetMaterialCountFn        GetMaterialCount        = nullptr;
    DsonDocument_GetMaterialIdFn           GetMaterialId           = nullptr;
    DsonDocument_GetMaterialNameFn         GetMaterialName         = nullptr;
    DsonDocument_GetMaterialGeometryIdFn   GetMaterialGeometryId   = nullptr;
    DsonDocument_GetMaterialUVSetIdFn      GetMaterialUVSetId      = nullptr;
    DsonDocument_GetMaterialTypeFn         GetMaterialType         = nullptr;
    DsonDocument_GetMaterialShaderTypeFn   GetMaterialShaderType   = nullptr;
    DsonDocument_GetMaterialGroupCountFn   GetMaterialGroupCount   = nullptr;
    DsonDocument_GetMaterialGroupNameFn    GetMaterialGroupName    = nullptr;

    DsonDocument_GetMaterialChannelCountFn       GetMaterialChannelCount       = nullptr;
    DsonDocument_GetMaterialChannelIdFn          GetMaterialChannelId          = nullptr;
    DsonDocument_GetMaterialChannelTypeFn        GetMaterialChannelType        = nullptr;
    DsonDocument_GetMaterialChannelValueFn       GetMaterialChannelValue       = nullptr;
    DsonDocument_GetMaterialChannelColorRFn      GetMaterialChannelColorR      = nullptr;
    DsonDocument_GetMaterialChannelColorGFn      GetMaterialChannelColorG      = nullptr;
    DsonDocument_GetMaterialChannelColorBFn      GetMaterialChannelColorB      = nullptr;
    DsonDocument_GetMaterialChannelHasColorFn    GetMaterialChannelHasColor    = nullptr;
    DsonDocument_GetMaterialChannelImageUrlFn    GetMaterialChannelImageUrl    = nullptr;
    DsonDocument_GetMaterialChannelTexturePathFn GetMaterialChannelTexturePath = nullptr;

    DsonDocument_GetSceneMaterialCountFn      GetSceneMaterialCount      = nullptr;
    DsonDocument_GetSceneMaterialIdFn         GetSceneMaterialId         = nullptr;
    DsonDocument_GetSceneMaterialUrlFn        GetSceneMaterialUrl        = nullptr;
    DsonDocument_GetSceneMaterialGroupCountFn GetSceneMaterialGroupCount = nullptr;
    DsonDocument_GetSceneMaterialGroupNameFn  GetSceneMaterialGroupName  = nullptr;

    DsonDocument_GetSceneMaterialNameFn          GetSceneMaterialName          = nullptr;
    DsonDocument_GetSceneMaterialGeometryIdFn    GetSceneMaterialGeometryId    = nullptr;
    DsonDocument_GetSceneMaterialUVSetIdFn       GetSceneMaterialUVSetId       = nullptr;
    DsonDocument_GetSceneMaterialTypeFn          GetSceneMaterialType          = nullptr;
    DsonDocument_GetSceneMaterialShaderTypeFn    GetSceneMaterialShaderType    = nullptr;

    DsonDocument_GetSceneMaterialChannelCountFn       GetSceneMaterialChannelCount       = nullptr;
    DsonDocument_GetSceneMaterialChannelIdFn          GetSceneMaterialChannelId          = nullptr;
    DsonDocument_GetSceneMaterialChannelTypeFn        GetSceneMaterialChannelType        = nullptr;
    DsonDocument_GetSceneMaterialChannelValueFn       GetSceneMaterialChannelValue       = nullptr;
    DsonDocument_GetSceneMaterialChannelColorRFn      GetSceneMaterialChannelColorR      = nullptr;
    DsonDocument_GetSceneMaterialChannelColorGFn      GetSceneMaterialChannelColorG      = nullptr;
    DsonDocument_GetSceneMaterialChannelColorBFn      GetSceneMaterialChannelColorB      = nullptr;
    DsonDocument_GetSceneMaterialChannelHasColorFn    GetSceneMaterialChannelHasColor    = nullptr;
    DsonDocument_GetSceneMaterialChannelImageUrlFn    GetSceneMaterialChannelImageUrl    = nullptr;
    DsonDocument_GetSceneMaterialChannelTexturePathFn GetSceneMaterialChannelTexturePath = nullptr;

    bool IsValid() const
    {
        return Create                    != nullptr
            && Destroy                   != nullptr
            && LoadFromString            != nullptr
            && GetLastError              != nullptr
            && GetAssetType              != nullptr
            && GetAssetId                != nullptr
            && GetSceneNodeCount         != nullptr
            && GetSceneNodeGeometryCount != nullptr
            && GetSceneNodeGeometryUrl   != nullptr
            && GetNodeCount              != nullptr
            && GetNodeName               != nullptr;
    }
};

extern FDsonParserAPI GDsonParser;
