#pragma once

#include <cstdint>

// Opaque handle returned by DsonDocument_Create and passed to all API functions
typedef void* DsonDocumentHandle;

/*
 * Single source of truth for every parser export the plugin binds.
 *
 *   X(Required, Ret, Member, ExportName, Params)
 *     Required   : 1 => load failure logged as Error and required by IsValid();
 *                  0 => optional, load failure logged as Warning.
 *     Ret        : function pointer return type.
 *     Member     : field name on FDsonParserAPI (also how call sites reference it).
 *     ExportName : exact symbol name exported by DsonParser.dll.
 *     Params     : parenthesized parameter-type list for the function pointer.
 *
 * This one list generates the struct fields (DsonParserFunctions.h), the IsValid()
 * check, and the DLL-binding loop (DsonImporter.cpp). Add or change an export in
 * exactly one place here. Signatures must match the DLL's C ABI; the historical mix
 * of DsonDocumentHandle/int (scene/asset accessors) and uint64_t/int32_t (everything
 * else) is preserved deliberately.
 */
#define DSON_PARSER_API_LIST(X) \
    /* Core lifecycle + asset/scene basics (required) */ \
    X(1, DsonDocumentHandle, Create,                    DsonDocument_Create,                    ()) \
    X(1, void,               Destroy,                   DsonDocument_Destroy,                   (DsonDocumentHandle)) \
    X(1, int,                LoadFromString,            DsonDocument_LoadFromString,            (DsonDocumentHandle, const char*)) \
    X(1, int,                LoadFromBuffer,            DsonDocument_LoadFromBuffer,            (DsonDocumentHandle, const char*, int)) \
    X(1, const char*,        GetLastError,              DsonParser_GetLastError,                ()) \
    X(1, const char*,        GetAssetType,              DsonDocument_GetAssetType,              (DsonDocumentHandle)) \
    X(1, const char*,        GetAssetId,                DsonDocument_GetAssetId,                (DsonDocumentHandle)) \
    X(1, int,                GetSceneNodeCount,         DsonDocument_GetSceneNodeCount,         (DsonDocumentHandle)) \
    X(1, int,                GetSceneNodeGeometryCount, DsonDocument_GetSceneNodeGeometryCount, (DsonDocumentHandle, int)) \
    X(1, const char*,        GetSceneNodeGeometryUrl,   DsonDocument_GetSceneNodeGeometryUrl,   (DsonDocumentHandle, int, int)) \
    \
    /* Node iteration (GetNodeCount/GetNodeName required by IsValid) */ \
    X(1, int32_t,     GetNodeCount,         DsonDocument_GetNodeCount,         (uint64_t)) \
    X(0, const char*, GetNodeId,            DsonDocument_GetNodeId,            (uint64_t, int32_t)) \
    X(1, const char*, GetNodeName,          DsonDocument_GetNodeName,          (uint64_t, int32_t)) \
    X(0, const char*, GetNodeType,          DsonDocument_GetNodeType,          (uint64_t, int32_t)) \
    X(0, const char*, GetNodeParent,        DsonDocument_GetNodeParent,        (uint64_t, int32_t)) \
    X(0, double,      GetNodeCenterPointX,  DsonDocument_GetNodeCenterPointX,  (uint64_t, int32_t)) \
    X(0, double,      GetNodeCenterPointY,  DsonDocument_GetNodeCenterPointY,  (uint64_t, int32_t)) \
    X(0, double,      GetNodeCenterPointZ,  DsonDocument_GetNodeCenterPointZ,  (uint64_t, int32_t)) \
    X(0, double,      GetNodeOrientationX,  DsonDocument_GetNodeOrientationX,  (uint64_t, int32_t)) \
    X(0, double,      GetNodeOrientationY,  DsonDocument_GetNodeOrientationY,  (uint64_t, int32_t)) \
    X(0, double,      GetNodeOrientationZ,  DsonDocument_GetNodeOrientationZ,  (uint64_t, int32_t)) \
    X(0, const char*, GetNodeRotationOrder, DsonDocument_GetNodeRotationOrder, (uint64_t, int32_t)) \
    X(0, double,      GetNodeGeneralScale,  DsonDocument_GetNodeGeneralScale,  (uint64_t, int32_t)) \
    X(0, double,      GetUnitScale,         DsonDocument_GetUnitScale,         (uint64_t)) \
    \
    /* Geometry */ \
    X(0, int32_t,     GetGeometryCount,             DsonDocument_GetGeometryCount,             (uint64_t)) \
    X(0, int32_t,     GetVertexCount,               DsonDocument_GetVertexCount,               (uint64_t, int32_t)) \
    X(0, double,      GetVertexX,                   DsonDocument_GetVertexX,                   (uint64_t, int32_t, int32_t)) \
    X(0, double,      GetVertexY,                   DsonDocument_GetVertexY,                   (uint64_t, int32_t, int32_t)) \
    X(0, double,      GetVertexZ,                   DsonDocument_GetVertexZ,                   (uint64_t, int32_t, int32_t)) \
    X(0, int32_t,     GetPolylistCount,             DsonDocument_GetPolylistCount,             (uint64_t, int32_t)) \
    X(0, int32_t,     GetPolylistFaceVertexCount,   DsonDocument_GetPolylistFaceVertexCount,   (uint64_t, int32_t, int32_t)) \
    X(0, int32_t,     GetPolylistFaceVertex,        DsonDocument_GetPolylistFaceVertex,        (uint64_t, int32_t, int32_t, int32_t)) \
    X(0, int32_t,     GetPolylistFaceMaterialIndex, DsonDocument_GetPolylistFaceMaterialIndex, (uint64_t, int32_t, int32_t)) \
    X(0, int32_t,     GetUVSetCount,                DsonDocument_GetUVSetCount,                (uint64_t)) \
    X(0, int32_t,     GetUVCount,                   DsonDocument_GetUVCount,                   (uint64_t, int32_t)) \
    X(0, double,      GetUVU,                       DsonDocument_GetUVU,                       (uint64_t, int32_t, int32_t)) \
    X(0, double,      GetUVV,                       DsonDocument_GetUVV,                       (uint64_t, int32_t, int32_t)) \
    X(0, int32_t,     GetUVPolygonVertexIndexCount, DsonDocument_GetUVPolygonVertexIndexCount, (uint64_t, int32_t)) \
    X(0, int32_t,     GetUVPolygonVertexIndex,      DsonDocument_GetUVPolygonVertexIndex,      (uint64_t, int32_t, int32_t)) \
    X(0, int,         GetUVSetVertexCount,          DsonDocument_GetUVSetVertexCount,          (uint64_t, int32_t)) \
    X(0, int,         GetUVOverrideCount,           DsonDocument_GetUVOverrideCount,           (uint64_t, int32_t)) \
    X(0, int,         GetUVOverrideFace,            DsonDocument_GetUVOverrideFace,            (uint64_t, int32_t, int32_t)) \
    X(0, int,         GetUVOverrideCorner,          DsonDocument_GetUVOverrideCorner,          (uint64_t, int32_t, int32_t)) \
    X(0, int,         GetUVOverrideUVIndex,         DsonDocument_GetUVOverrideUVIndex,         (uint64_t, int32_t, int32_t)) \
    X(0, int,         GetPolygonMaterialGroupCount, DsonDocument_GetPolygonMaterialGroupCount, (uint64_t, int32_t)) \
    X(0, const char*, GetPolygonMaterialGroupName,  DsonDocument_GetPolygonMaterialGroupName,  (uint64_t, int32_t, int32_t)) \
    \
    /* Modifier info */ \
    X(0, const char*, GetModifierId,             DsonDocument_GetModifierId,             (uint64_t, int32_t)) \
    X(0, const char*, GetModifierName,           DsonDocument_GetModifierName,           (uint64_t, int32_t)) \
    X(0, const char*, GetModifierType,           DsonDocument_GetModifierType,           (uint64_t, int32_t)) \
    X(0, int32_t,     GetModifierCount,          DsonDocument_GetModifierCount,          (uint64_t)) \
    X(0, int32_t,     GetModifierSkinVertexCount, DsonDocument_GetModifierSkinVertexCount, (uint64_t, int32_t)) \
    X(0, int32_t,     GetModifierSkinJointCount,  DsonDocument_GetModifierSkinJointCount,  (uint64_t, int32_t)) \
    \
    /* Skin weights */ \
    X(0, int32_t,     GetSkinJointCount,             DsonDocument_GetSkinJointCount,             (uint64_t, int32_t)) \
    X(0, const char*, GetSkinJointNodeId,            DsonDocument_GetSkinJointNodeId,            (uint64_t, int32_t, int32_t)) \
    X(0, int32_t,     GetSkinJointWeightCount,       DsonDocument_GetSkinJointWeightCount,       (uint64_t, int32_t, int32_t)) \
    X(0, int32_t,     GetSkinJointWeightVertexIndex, DsonDocument_GetSkinJointWeightVertexIndex, (uint64_t, int32_t, int32_t, int32_t)) \
    X(0, double,      GetSkinJointWeight,            DsonDocument_GetSkinJointWeight,            (uint64_t, int32_t, int32_t, int32_t)) \
    X(0, int32_t,     GetVertexInfluenceCount,       DsonDocument_GetVertexInfluenceCount,       (uint64_t, int32_t, int32_t, int32_t)) \
    X(0, bool,        GetVertexBoneInfluence,        DsonDocument_GetVertexBoneInfluence,        (uint64_t, int32_t, int32_t, int32_t, const char**, double*)) \
    X(0, bool,        GetVertexBoneInfluenceCapped,  DsonDocument_GetVertexBoneInfluenceCapped,  (uint64_t, int32_t, int32_t, int32_t, int32_t, const char**, double*)) \
    \
    /* Scene modifiers (external morph-file discovery: each url points at a morph .dsf) */ \
    X(0, int32_t,     GetSceneModifierCount, DsonDocument_GetSceneModifierCount, (uint64_t)) \
    X(0, const char*, GetSceneModifierUrl,   DsonDocument_GetSceneModifierUrl,   (uint64_t, int32_t)) \
    \
    /* Morph targets (morphIndex addresses the filtered list of type=="morph" modifiers) */ \
    X(0, int32_t,     GetMorphCount,                  DsonDocument_GetMorphCount,                  (uint64_t)) \
    X(0, const char*, GetMorphName,                   DsonDocument_GetMorphName,                   (uint64_t, int32_t)) \
    X(0, const char*, GetMorphLabel,                  DsonDocument_GetMorphLabel,                  (uint64_t, int32_t)) \
    X(0, const char*, GetMorphGeometryId,             DsonDocument_GetMorphGeometryId,             (uint64_t, int32_t)) \
    X(0, int32_t,     GetMorphDeltaCount,             DsonDocument_GetMorphDeltaCount,             (uint64_t, int32_t)) \
    X(0, int32_t,     GetMorphDeltaVertexIndex,       DsonDocument_GetMorphDeltaVertexIndex,       (uint64_t, int32_t, int32_t)) \
    X(0, double,      GetMorphDeltaX,                 DsonDocument_GetMorphDeltaX,                 (uint64_t, int32_t, int32_t)) \
    X(0, double,      GetMorphDeltaY,                 DsonDocument_GetMorphDeltaY,                 (uint64_t, int32_t, int32_t)) \
    X(0, double,      GetMorphDeltaZ,                 DsonDocument_GetMorphDeltaZ,                 (uint64_t, int32_t, int32_t)) \
    /* Morph normal-delta exports are reserved; MeshDescription builds recompute morph normals. */ \
    X(0, int32_t,     GetMorphNormalDeltaCount,       DsonDocument_GetMorphNormalDeltaCount,       (uint64_t, int32_t)) \
    X(0, int32_t,     GetMorphNormalDeltaVertexIndex, DsonDocument_GetMorphNormalDeltaVertexIndex, (uint64_t, int32_t, int32_t)) \
    X(0, double,      GetMorphNormalDeltaX,           DsonDocument_GetMorphNormalDeltaX,           (uint64_t, int32_t, int32_t)) \
    X(0, double,      GetMorphNormalDeltaY,           DsonDocument_GetMorphNormalDeltaY,           (uint64_t, int32_t, int32_t)) \
    X(0, double,      GetMorphNormalDeltaZ,           DsonDocument_GetMorphNormalDeltaZ,           (uint64_t, int32_t, int32_t)) \
    \
    /* Library materials */ \
    X(0, int32_t,     GetMaterialCount,      DsonDocument_GetMaterialCount,      (uint64_t)) \
    X(0, const char*, GetMaterialId,         DsonDocument_GetMaterialId,         (uint64_t, int32_t)) \
    X(0, const char*, GetMaterialName,       DsonDocument_GetMaterialName,       (uint64_t, int32_t)) \
    X(0, const char*, GetMaterialGeometryId, DsonDocument_GetMaterialGeometryId, (uint64_t, int32_t)) \
    X(0, const char*, GetMaterialUVSetId,    DsonDocument_GetMaterialUVSetId,    (uint64_t, int32_t)) \
    X(0, const char*, GetMaterialType,       DsonDocument_GetMaterialType,       (uint64_t, int32_t)) \
    X(0, const char*, GetMaterialShaderType, DsonDocument_GetMaterialShaderType, (uint64_t, int32_t)) \
    X(0, int32_t,     GetMaterialGroupCount, DsonDocument_GetMaterialGroupCount, (uint64_t, int32_t)) \
    X(0, const char*, GetMaterialGroupName,  DsonDocument_GetMaterialGroupName,  (uint64_t, int32_t, int32_t)) \
    \
    /* Library material channels (indexed) */ \
    X(0, int32_t,     GetMaterialChannelCount,       DsonDocument_GetMaterialChannelCount,       (uint64_t, int32_t)) \
    X(0, const char*, GetMaterialChannelId,          DsonDocument_GetMaterialChannelId,          (uint64_t, int32_t, int32_t)) \
    X(0, const char*, GetMaterialChannelType,        DsonDocument_GetMaterialChannelType,        (uint64_t, int32_t, int32_t)) \
    X(0, double,      GetMaterialChannelValue,       DsonDocument_GetMaterialChannelValue,       (uint64_t, int32_t, int32_t)) \
    X(0, double,      GetMaterialChannelColorR,      DsonDocument_GetMaterialChannelColorR,      (uint64_t, int32_t, int32_t)) \
    X(0, double,      GetMaterialChannelColorG,      DsonDocument_GetMaterialChannelColorG,      (uint64_t, int32_t, int32_t)) \
    X(0, double,      GetMaterialChannelColorB,      DsonDocument_GetMaterialChannelColorB,      (uint64_t, int32_t, int32_t)) \
    X(0, bool,        GetMaterialChannelHasColor,    DsonDocument_GetMaterialChannelHasColor,    (uint64_t, int32_t, int32_t)) \
    X(0, const char*, GetMaterialChannelImageUrl,    DsonDocument_GetMaterialChannelImageUrl,    (uint64_t, int32_t, int32_t)) \
    X(0, const char*, GetMaterialChannelTexturePath, DsonDocument_GetMaterialChannelTexturePath, (uint64_t, int32_t, int32_t)) \
    \
    /* Scene materials */ \
    X(0, int32_t,     GetSceneMaterialCount,      DsonDocument_GetSceneMaterialCount,      (uint64_t)) \
    X(0, const char*, GetSceneMaterialId,         DsonDocument_GetSceneMaterialId,         (uint64_t, int32_t)) \
    X(0, const char*, GetSceneMaterialUrl,        DsonDocument_GetSceneMaterialUrl,        (uint64_t, int32_t)) \
    X(0, int32_t,     GetSceneMaterialGroupCount, DsonDocument_GetSceneMaterialGroupCount, (uint64_t, int32_t)) \
    X(0, const char*, GetSceneMaterialGroupName,  DsonDocument_GetSceneMaterialGroupName,  (uint64_t, int32_t, int32_t)) \
    X(0, const char*, GetSceneMaterialName,       DsonDocument_GetSceneMaterialName,       (uint64_t, int32_t)) \
    X(0, const char*, GetSceneMaterialGeometryId, DsonDocument_GetSceneMaterialGeometryId, (uint64_t, int32_t)) \
    X(0, const char*, GetSceneMaterialUVSetId,    DsonDocument_GetSceneMaterialUVSetId,    (uint64_t, int32_t)) \
    X(0, const char*, GetSceneMaterialType,       DsonDocument_GetSceneMaterialType,       (uint64_t, int32_t)) \
    X(0, const char*, GetSceneMaterialShaderType, DsonDocument_GetSceneMaterialShaderType, (uint64_t, int32_t)) \
    \
    /* Scene material channels (indexed) */ \
    X(0, int32_t,     GetSceneMaterialChannelCount,       DsonDocument_GetSceneMaterialChannelCount,       (uint64_t, int32_t)) \
    X(0, const char*, GetSceneMaterialChannelId,          DsonDocument_GetSceneMaterialChannelId,          (uint64_t, int32_t, int32_t)) \
    X(0, const char*, GetSceneMaterialChannelType,        DsonDocument_GetSceneMaterialChannelType,        (uint64_t, int32_t, int32_t)) \
    X(0, double,      GetSceneMaterialChannelValue,       DsonDocument_GetSceneMaterialChannelValue,       (uint64_t, int32_t, int32_t)) \
    X(0, double,      GetSceneMaterialChannelColorR,      DsonDocument_GetSceneMaterialChannelColorR,      (uint64_t, int32_t, int32_t)) \
    X(0, double,      GetSceneMaterialChannelColorG,      DsonDocument_GetSceneMaterialChannelColorG,      (uint64_t, int32_t, int32_t)) \
    X(0, double,      GetSceneMaterialChannelColorB,      DsonDocument_GetSceneMaterialChannelColorB,      (uint64_t, int32_t, int32_t)) \
    X(0, bool,        GetSceneMaterialChannelHasColor,    DsonDocument_GetSceneMaterialChannelHasColor,    (uint64_t, int32_t, int32_t)) \
    X(0, const char*, GetSceneMaterialChannelImageUrl,    DsonDocument_GetSceneMaterialChannelImageUrl,    (uint64_t, int32_t, int32_t)) \
    X(0, const char*, GetSceneMaterialChannelTexturePath, DsonDocument_GetSceneMaterialChannelTexturePath, (uint64_t, int32_t, int32_t))

struct FDsonParserAPI
{
#define DSON_PARSER_DECLARE_MEMBER(Required, Ret, Member, ExportName, Params) \
    Ret (*Member) Params = nullptr;
    DSON_PARSER_API_LIST(DSON_PARSER_DECLARE_MEMBER)
#undef DSON_PARSER_DECLARE_MEMBER

    bool IsValid() const
    {
        return true
#define DSON_PARSER_CHECK_REQUIRED(Required, Ret, Member, ExportName, Params) \
            && (Required == 0 || Member != nullptr)
        DSON_PARSER_API_LIST(DSON_PARSER_CHECK_REQUIRED)
#undef DSON_PARSER_CHECK_REQUIRED
            ;
    }
};

extern FDsonParserAPI GDsonParser;
