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
    /* Core lifecycle + asset/scene basics */ \
    X(1, DsonDocumentHandle, Create,                    DsonDocument_Create,                    ()) \
    X(1, void,               Destroy,                   DsonDocument_Destroy,                   (DsonDocumentHandle)) \
    X(0, int,                LoadFromString,            DsonDocument_LoadFromString,            (DsonDocumentHandle, const char*)) /* optional: unused; LoadFromBuffer is the loader (gzip auto-detect) */ \
    X(1, int,                LoadFromBuffer,            DsonDocument_LoadFromBuffer,            (DsonDocumentHandle, const char*, int)) \
    X(1, const char*,        GetLastError,              DsonParser_GetLastError,                ()) \
    X(1, const char*,        GetAssetType,              DsonDocument_GetAssetType,              (DsonDocumentHandle)) \
    X(1, const char*,        GetAssetId,                DsonDocument_GetAssetId,                (DsonDocumentHandle)) \
    X(1, int,                GetSceneNodeCount,         DsonDocument_GetSceneNodeCount,         (DsonDocumentHandle)) \
    X(1, int,                GetSceneNodeGeometryCount, DsonDocument_GetSceneNodeGeometryCount, (DsonDocumentHandle, int)) \
    X(1, const char*,        GetSceneNodeGeometryUrl,   DsonDocument_GetSceneNodeGeometryUrl,   (DsonDocumentHandle, int, int)) \
    \
    /* Library version (optional) - runtime ABI-compatibility gate in DsonImporter.cpp */ \
    X(0, const char*, GetVersion, DsonParser_GetVersion, ()) \
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
    X(0, double,      GetNodeEndPointX,     DsonDocument_GetNodeEndPointX,     (uint64_t, int32_t)) \
    X(0, double,      GetNodeEndPointY,     DsonDocument_GetNodeEndPointY,     (uint64_t, int32_t)) \
    X(0, double,      GetNodeEndPointZ,     DsonDocument_GetNodeEndPointZ,     (uint64_t, int32_t)) \
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
    X(0, int32_t,     GetModifierFormulaCount,          DsonDocument_GetModifierFormulaCount,          (uint64_t, int32_t)) \
    X(0, const char*, GetModifierFormulaOutput,         DsonDocument_GetModifierFormulaOutput,         (uint64_t, int32_t, int32_t)) \
    X(0, const char*, GetModifierFormulaStage,          DsonDocument_GetModifierFormulaStage,          (uint64_t, int32_t, int32_t)) \
    X(0, int32_t,     GetModifierFormulaOperationCount, DsonDocument_GetModifierFormulaOperationCount, (uint64_t, int32_t, int32_t)) \
    X(0, const char*, GetModifierFormulaOperationOp,    DsonDocument_GetModifierFormulaOperationOp,    (uint64_t, int32_t, int32_t, int32_t)) \
    X(0, double,      GetModifierFormulaOperationVal,   DsonDocument_GetModifierFormulaOperationVal,   (uint64_t, int32_t, int32_t, int32_t)) \
    X(0, const char*, GetModifierFormulaOperationUrl,   DsonDocument_GetModifierFormulaOperationUrl,   (uint64_t, int32_t, int32_t, int32_t)) \
    X(0, double,      GetModifierChannelValue,    DsonDocument_GetModifierChannelValue,    (uint64_t, int32_t)) \
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
    X(0, int32_t,     GetSceneModifierCount,          DsonDocument_GetSceneModifierCount,          (uint64_t)) \
    X(0, const char*, GetSceneModifierUrl,            DsonDocument_GetSceneModifierUrl,            (uint64_t, int32_t)) \
    X(0, const char*, GetSceneModifierId,             DsonDocument_GetSceneModifierId,             (uint64_t, int32_t)) \
    X(0, double,      GetSceneModifierChannelValue,   DsonDocument_GetSceneModifierChannelValue,   (uint64_t, int32_t)) \
    X(0, double,      GetSceneModifierChannelMin,     DsonDocument_GetSceneModifierChannelMin,     (uint64_t, int32_t)) \
    X(0, double,      GetSceneModifierChannelMax,     DsonDocument_GetSceneModifierChannelMax,     (uint64_t, int32_t)) \
    X(0, bool,        GetSceneModifierChannelClamped, DsonDocument_GetSceneModifierChannelClamped, (uint64_t, int32_t)) \
    X(0, int32_t,     GetSceneModifierFormulaCount,          DsonDocument_GetSceneModifierFormulaCount,          (uint64_t, int32_t)) \
    X(0, const char*, GetSceneModifierFormulaOutput,         DsonDocument_GetSceneModifierFormulaOutput,         (uint64_t, int32_t, int32_t)) \
    X(0, const char*, GetSceneModifierFormulaStage,          DsonDocument_GetSceneModifierFormulaStage,          (uint64_t, int32_t, int32_t)) \
    X(0, int32_t,     GetSceneModifierFormulaOperationCount, DsonDocument_GetSceneModifierFormulaOperationCount, (uint64_t, int32_t, int32_t)) \
    X(0, const char*, GetSceneModifierFormulaOperationOp,    DsonDocument_GetSceneModifierFormulaOperationOp,    (uint64_t, int32_t, int32_t, int32_t)) \
    X(0, double,      GetSceneModifierFormulaOperationVal,   DsonDocument_GetSceneModifierFormulaOperationVal,   (uint64_t, int32_t, int32_t, int32_t)) \
    X(0, const char*, GetSceneModifierFormulaOperationUrl,   DsonDocument_GetSceneModifierFormulaOperationUrl,   (uint64_t, int32_t, int32_t, int32_t)) \
    \
    /* Morph targets (morphIndex addresses the filtered list of type=="morph" modifiers) */ \
    X(0, int32_t,     GetMorphCount,                  DsonDocument_GetMorphCount,                  (uint64_t)) \
    X(0, const char*, GetMorphId,                     DsonDocument_GetMorphId,                     (uint64_t, int32_t)) \
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
    X(0, const char*, GetSceneMaterialChannelTexturePath, DsonDocument_GetSceneMaterialChannelTexturePath, (uint64_t, int32_t, int32_t)) \
    X(0, int32_t,     GetSceneMaterialChannelLayerCount,       DsonDocument_GetSceneMaterialChannelLayerCount,       (uint64_t, int32_t, int32_t)) \
    X(0, const char*, GetSceneMaterialChannelLayerTexturePath, DsonDocument_GetSceneMaterialChannelLayerTexturePath, (uint64_t, int32_t, int32_t, int32_t)) \
    X(0, const char*, GetSceneMaterialChannelLayerLabel,       DsonDocument_GetSceneMaterialChannelLayerLabel,       (uint64_t, int32_t, int32_t, int32_t)) \
    \
    /* PostLoadAddons: companion figures declared in scene.extra (DsonParser >= 1.1.0; optional). */ \
    /* Count is flattened across all PostLoadAddons maps in document order; 0 = none.           */ \
    X(0, int,         GetScenePostLoadAddonCount,     DsonDocument_GetScenePostLoadAddonCount,     (DsonDocumentHandle)) \
    X(0, const char*, GetScenePostLoadAddonSlot,      DsonDocument_GetScenePostLoadAddonSlot,      (DsonDocumentHandle, int)) \
    X(0, const char*, GetScenePostLoadAddonAssetName, DsonDocument_GetScenePostLoadAddonAssetName, (DsonDocumentHandle, int)) \
    X(0, const char*, GetScenePostLoadAddonAssetFile, DsonDocument_GetScenePostLoadAddonAssetFile, (DsonDocumentHandle, int)) \
    X(0, const char*, GetScenePostLoadAddonMatPreset,  DsonDocument_GetScenePostLoadAddonMatPreset,  (DsonDocumentHandle, int)) \
    \
    /* Catalog classification: presentation type/label per node/modifier library item,          */ \
    /* graft signal per geometry library item (DsonParser >= 1.5.0; all optional).              */ \
    /* Params follow the node/modifier/geometry library family (uint64_t handle, int32_t index) */ \
    /* matching the existing GetNodeId/GetModifierId/GetGeometryCount rows (Delta C, R2).       */ \
    X(0, const char*, GetNodePresentationType,      DsonDocument_GetNodePresentationType,      (uint64_t, int32_t)) \
    X(0, const char*, GetNodePresentationLabel,     DsonDocument_GetNodePresentationLabel,     (uint64_t, int32_t)) \
    X(0, const char*, GetModifierPresentationType,  DsonDocument_GetModifierPresentationType,  (uint64_t, int32_t)) \
    X(0, const char*, GetModifierPresentationLabel, DsonDocument_GetModifierPresentationLabel, (uint64_t, int32_t)) \
    X(0, bool,        GetGeometryIsGraft,           DsonDocument_GetGeometryIsGraft,           (uint64_t, int32_t)) \
    \
    /* Scene animations: scene.animations keyframe channels (DsonParser >= 1.2.0; optional). Each entry */ \
    /* carries the verbatim DSON property url pointer + the first key's typed value; the parser does NOT */ \
    /* apply these onto scene.materials (faithful passthrough) — the consumer reads both and decides.    */ \
    /* ValueKind: 0 null, 1 number, 2 bool, 3 string, 4 color; -1 = invalid. animIndex in [0, Count).    */ \
    X(0, int,         GetSceneAnimationCount,     DsonDocument_GetSceneAnimationCount,     (DsonDocumentHandle)) \
    X(0, const char*, GetSceneAnimationUrl,       DsonDocument_GetSceneAnimationUrl,       (DsonDocumentHandle, int)) \
    X(0, int,         GetSceneAnimationValueKind, DsonDocument_GetSceneAnimationValueKind, (DsonDocumentHandle, int)) \
    X(0, double,      GetSceneAnimationFloat,     DsonDocument_GetSceneAnimationFloat,     (DsonDocumentHandle, int)) \
    X(0, bool,        GetSceneAnimationBool,      DsonDocument_GetSceneAnimationBool,      (DsonDocumentHandle, int)) \
    X(0, const char*, GetSceneAnimationString,    DsonDocument_GetSceneAnimationString,    (DsonDocumentHandle, int)) \
    X(0, double,      GetSceneAnimationColorR,    DsonDocument_GetSceneAnimationColorR,    (DsonDocumentHandle, int)) \
    X(0, double,      GetSceneAnimationColorG,    DsonDocument_GetSceneAnimationColorG,    (DsonDocumentHandle, int)) \
    X(0, double,      GetSceneAnimationColorB,    DsonDocument_GetSceneAnimationColorB,    (DsonDocumentHandle, int)) \
    \
    /* Image library (image_library by index; layer accessors since DsonParser 1.3.0; all optional). */ \
    X(0, int,         GetImageCount,            DsonDocument_GetImageCount,            (DsonDocumentHandle)) \
    X(0, const char*, GetImageId,               DsonDocument_GetImageId,               (DsonDocumentHandle, int)) \
    X(0, int,         GetImageLayerCount,       DsonDocument_GetImageLayerCount,       (DsonDocumentHandle, int)) \
    X(0, const char*, GetImageLayerTexturePath, DsonDocument_GetImageLayerTexturePath, (DsonDocumentHandle, int, int)) \
    X(0, const char*, GetImageLayerLabel,       DsonDocument_GetImageLayerLabel,       (DsonDocumentHandle, int, int)) \
    X(0, int,         GetImageMapWidth,         DsonDocument_GetImageMapWidth,         (DsonDocumentHandle, int)) \
    X(0, int,         GetImageMapHeight,        DsonDocument_GetImageMapHeight,        (DsonDocumentHandle, int)) \
    \
    /* Per-layer LIE compositing metadata — image_library family (DsonParser >= 1.4.0; all optional). */ \
    /* Opacity is raw "transparency" (1=opaque); sentinel 0.0 collides with a transparent layer —    */ \
    /* always bounds-check GetImageLayerCount before indexing. ScaleX/Y sentinel = 1.0.              */ \
    X(0, const char*, GetImageLayerBlendMode, DsonDocument_GetImageLayerBlendMode, (DsonDocumentHandle, int, int)) \
    X(0, double,      GetImageLayerOpacity,   DsonDocument_GetImageLayerOpacity,   (DsonDocumentHandle, int, int)) \
    X(0, bool,        GetImageLayerActive,    DsonDocument_GetImageLayerActive,    (DsonDocumentHandle, int, int)) \
    X(0, bool,        GetImageLayerInvert,    DsonDocument_GetImageLayerInvert,    (DsonDocumentHandle, int, int)) \
    X(0, double,      GetImageLayerColorR,    DsonDocument_GetImageLayerColorR,    (DsonDocumentHandle, int, int)) \
    X(0, double,      GetImageLayerColorG,    DsonDocument_GetImageLayerColorG,    (DsonDocumentHandle, int, int)) \
    X(0, double,      GetImageLayerColorB,    DsonDocument_GetImageLayerColorB,    (DsonDocumentHandle, int, int)) \
    X(0, double,      GetImageLayerRotation,  DsonDocument_GetImageLayerRotation,  (DsonDocumentHandle, int, int)) \
    X(0, double,      GetImageLayerScaleX,    DsonDocument_GetImageLayerScaleX,    (DsonDocumentHandle, int, int)) \
    X(0, double,      GetImageLayerScaleY,    DsonDocument_GetImageLayerScaleY,    (DsonDocumentHandle, int, int)) \
    X(0, double,      GetImageLayerOffsetX,   DsonDocument_GetImageLayerOffsetX,   (DsonDocumentHandle, int, int)) \
    X(0, double,      GetImageLayerOffsetY,   DsonDocument_GetImageLayerOffsetY,   (DsonDocumentHandle, int, int)) \
    X(0, bool,        GetImageLayerMirrorX,   DsonDocument_GetImageLayerMirrorX,   (DsonDocumentHandle, int, int)) \
    X(0, bool,        GetImageLayerMirrorY,   DsonDocument_GetImageLayerMirrorY,   (DsonDocumentHandle, int, int)) \
    \
    /* Per-layer LIE compositing metadata — scene material channel family (DsonParser >= 1.4.0; all optional). */ \
    X(0, const char*, GetSceneMaterialChannelLayerBlendMode, DsonDocument_GetSceneMaterialChannelLayerBlendMode, (uint64_t, int32_t, int32_t, int32_t)) \
    X(0, double,      GetSceneMaterialChannelLayerOpacity,   DsonDocument_GetSceneMaterialChannelLayerOpacity,   (uint64_t, int32_t, int32_t, int32_t)) \
    X(0, bool,        GetSceneMaterialChannelLayerActive,    DsonDocument_GetSceneMaterialChannelLayerActive,    (uint64_t, int32_t, int32_t, int32_t)) \
    X(0, bool,        GetSceneMaterialChannelLayerInvert,    DsonDocument_GetSceneMaterialChannelLayerInvert,    (uint64_t, int32_t, int32_t, int32_t)) \
    X(0, double,      GetSceneMaterialChannelLayerColorR,    DsonDocument_GetSceneMaterialChannelLayerColorR,    (uint64_t, int32_t, int32_t, int32_t)) \
    X(0, double,      GetSceneMaterialChannelLayerColorG,    DsonDocument_GetSceneMaterialChannelLayerColorG,    (uint64_t, int32_t, int32_t, int32_t)) \
    X(0, double,      GetSceneMaterialChannelLayerColorB,    DsonDocument_GetSceneMaterialChannelLayerColorB,    (uint64_t, int32_t, int32_t, int32_t)) \
    X(0, double,      GetSceneMaterialChannelLayerRotation,  DsonDocument_GetSceneMaterialChannelLayerRotation,  (uint64_t, int32_t, int32_t, int32_t)) \
    X(0, double,      GetSceneMaterialChannelLayerScaleX,    DsonDocument_GetSceneMaterialChannelLayerScaleX,    (uint64_t, int32_t, int32_t, int32_t)) \
    X(0, double,      GetSceneMaterialChannelLayerScaleY,    DsonDocument_GetSceneMaterialChannelLayerScaleY,    (uint64_t, int32_t, int32_t, int32_t)) \
    X(0, double,      GetSceneMaterialChannelLayerOffsetX,   DsonDocument_GetSceneMaterialChannelLayerOffsetX,   (uint64_t, int32_t, int32_t, int32_t)) \
    X(0, double,      GetSceneMaterialChannelLayerOffsetY,   DsonDocument_GetSceneMaterialChannelLayerOffsetY,   (uint64_t, int32_t, int32_t, int32_t)) \
    X(0, bool,        GetSceneMaterialChannelLayerMirrorX,   DsonDocument_GetSceneMaterialChannelLayerMirrorX,   (uint64_t, int32_t, int32_t, int32_t)) \
    X(0, bool,        GetSceneMaterialChannelLayerMirrorY,   DsonDocument_GetSceneMaterialChannelLayerMirrorY,   (uint64_t, int32_t, int32_t, int32_t))

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
