#pragma once

#ifdef DSONPARSER_EXPORTS
#define DSONPARSER_API __declspec(dllexport)
#else
#define DSONPARSER_API __declspec(dllimport)
#endif

// Public C ABI orientation:
// This header exposes a parsed DSON/DSF/DUF document through an opaque handle and
// index-based accessors. The implementation owns all returned const char*
// strings; copy them if they must survive DsonDocument_Clear/Destroy or later
// scratch-string API calls. Invalid handles or indexes return a family-specific
// "empty" value: count functions and numeric getters return 0 (count functions
// never return -1), bool getters return false, string getters return "", and the
// value/index accessors that have no element to report return -1.
//
// Index conventions:
// - Node/geometry/material/modifier indexes address the corresponding library
//   arrays parsed from *_library sections.
// - Scene node/material/modifier/UV indexes address scene.* instance arrays.
// - Morph indexes address a filtered list of modifiers where type == "morph";
//   they are not raw modifier_library indexes.
// - Skin APIs use raw modifier_library indexes for skin_binding modifiers.

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle to DsonDocument
typedef void* DsonDocumentHandle;

// Create a new DSON document
DSONPARSER_API DsonDocumentHandle DsonDocument_Create();

// Returns 0 on success, non-zero on failure.
// Call DsonParser_GetLastError() for error detail on failure.
DSONPARSER_API int DsonDocument_LoadFromFile(DsonDocumentHandle handle, const char* filepath);

// Returns 0 on success, non-zero on failure.
// Call DsonParser_GetLastError() for error detail on failure.
DSONPARSER_API int DsonDocument_LoadFromString(DsonDocumentHandle handle, const char* jsonString);

// Get file version
DSONPARSER_API const char* DsonDocument_GetFileVersion(DsonDocumentHandle handle);

// Get asset info
DSONPARSER_API const char* DsonDocument_GetAssetId(DsonDocumentHandle handle);
DSONPARSER_API const char* DsonDocument_GetAssetType(DsonDocumentHandle handle);
DSONPARSER_API double      DsonDocument_GetUnitScale(DsonDocumentHandle handle);

// Get counts
DSONPARSER_API int DsonDocument_GetNodeCount(DsonDocumentHandle handle);
DSONPARSER_API int DsonDocument_GetGeometryCount(DsonDocumentHandle handle);
DSONPARSER_API int DsonDocument_GetMaterialCount(DsonDocumentHandle handle);
DSONPARSER_API int DsonDocument_GetModifierCount(DsonDocumentHandle handle);
DSONPARSER_API int DsonDocument_GetImageCount(DsonDocumentHandle handle);
DSONPARSER_API int DsonDocument_GetUVSetCount(DsonDocumentHandle handle);

// Get node info by index
DSONPARSER_API const char* DsonDocument_GetNodeId(DsonDocumentHandle handle, int index);
DSONPARSER_API const char* DsonDocument_GetNodeName(DsonDocumentHandle handle, int index);
DSONPARSER_API const char* DsonDocument_GetNodeType(DsonDocumentHandle handle, int index);
// Node center_point (joint origin) components
DSONPARSER_API double DsonDocument_GetNodeCenterPointX(DsonDocumentHandle handle, int index);
DSONPARSER_API double DsonDocument_GetNodeCenterPointY(DsonDocumentHandle handle, int index);
DSONPARSER_API double DsonDocument_GetNodeCenterPointZ(DsonDocumentHandle handle, int index);

// Get scene node info by index (the "scene.nodes" array, distinct from node_library).
// Scene nodes are instances: they reference a library node via Url and carry a Label,
// and typically have no Type of their own.
DSONPARSER_API int DsonDocument_GetSceneNodeCount(DsonDocumentHandle handle);
DSONPARSER_API const char* DsonDocument_GetSceneNodeId(DsonDocumentHandle handle, int index);
DSONPARSER_API const char* DsonDocument_GetSceneNodeName(DsonDocumentHandle handle, int index);
DSONPARSER_API const char* DsonDocument_GetSceneNodeLabel(DsonDocumentHandle handle, int index);
DSONPARSER_API const char* DsonDocument_GetSceneNodeType(DsonDocumentHandle handle, int index);
DSONPARSER_API const char* DsonDocument_GetSceneNodeUrl(DsonDocumentHandle handle, int index);
DSONPARSER_API int         DsonDocument_GetSceneNodeGeometryCount(DsonDocumentHandle handle, int sceneNodeIndex);
DSONPARSER_API const char* DsonDocument_GetSceneNodeGeometryId(DsonDocumentHandle handle, int sceneNodeIndex, int geomRefIndex);
DSONPARSER_API const char* DsonDocument_GetSceneNodeGeometryUrl(DsonDocumentHandle handle, int sceneNodeIndex, int geomRefIndex);

// Other scene instance collections (scene.modifiers / scene.materials / scene.uvs)
DSONPARSER_API int DsonDocument_GetSceneModifierCount(DsonDocumentHandle handle);
DSONPARSER_API const char* DsonDocument_GetSceneModifierId(DsonDocumentHandle handle, int index);
DSONPARSER_API const char* DsonDocument_GetSceneModifierUrl(DsonDocumentHandle handle, int index);

DSONPARSER_API int DsonDocument_GetSceneMaterialCount(DsonDocumentHandle handle);
DSONPARSER_API const char* DsonDocument_GetSceneMaterialId(DsonDocumentHandle handle, int index);
DSONPARSER_API const char* DsonDocument_GetSceneMaterialUrl(DsonDocumentHandle handle, int index);

DSONPARSER_API int DsonDocument_GetSceneUVSetCount(DsonDocumentHandle handle);
DSONPARSER_API const char* DsonDocument_GetSceneUVSetId(DsonDocumentHandle handle, int index);
DSONPARSER_API const char* DsonDocument_GetSceneUVSetUrl(DsonDocumentHandle handle, int index);

// Get material info by index
DSONPARSER_API const char* DsonDocument_GetMaterialId(DsonDocumentHandle handle, int index);

// Get geometry info by index
DSONPARSER_API const char* DsonDocument_GetGeometryId(DsonDocumentHandle handle, int index);
DSONPARSER_API const char* DsonDocument_GetGeometryName(DsonDocumentHandle handle, int index);
DSONPARSER_API int DsonDocument_GetGeometryVertexCount(DsonDocumentHandle handle, int index);
DSONPARSER_API int DsonDocument_GetGeometryPolygonCount(DsonDocumentHandle handle, int index);
DSONPARSER_API const char* DsonDocument_GetGeometryDefaultUVSetId(DsonDocumentHandle handle, int geomIndex);

// ---- A. Geometry: vertex positions ----
DSONPARSER_API int    DsonDocument_GetVertexCount(DsonDocumentHandle handle, int geomIndex);
DSONPARSER_API double DsonDocument_GetVertexX(DsonDocumentHandle handle, int geomIndex, int vertexIndex);
DSONPARSER_API double DsonDocument_GetVertexY(DsonDocumentHandle handle, int geomIndex, int vertexIndex);
DSONPARSER_API double DsonDocument_GetVertexZ(DsonDocumentHandle handle, int geomIndex, int vertexIndex);

// Polygon face list (polylist)
// Each face: [polygon_group_idx, material_group_idx, v0, v1, v2, v3] — two leading ints.
DSONPARSER_API int    DsonDocument_GetPolylistCount(DsonDocumentHandle handle, int geomIndex);
DSONPARSER_API int    DsonDocument_GetPolylistFaceVertexCount(DsonDocumentHandle handle, int geomIndex, int faceIndex);
DSONPARSER_API int    DsonDocument_GetPolylistFaceVertex(DsonDocumentHandle handle, int geomIndex, int faceIndex, int vertexIndex);
DSONPARSER_API int    DsonDocument_GetPolylistFaceGroupIndex(DsonDocumentHandle handle, int geomIndex, int faceIndex);
DSONPARSER_API int    DsonDocument_GetPolylistFaceMaterialIndex(DsonDocumentHandle handle, int geomIndex, int faceIndex);

// Polygon groups (bone region groups, e.g. l_forearm, head, pelvis)
DSONPARSER_API int         DsonDocument_GetPolygonGroupCount(DsonDocumentHandle handle, int geomIndex);
DSONPARSER_API const char* DsonDocument_GetPolygonGroupName(DsonDocumentHandle handle, int geomIndex, int groupIndex);

// Material groups
DSONPARSER_API int         DsonDocument_GetPolygonMaterialGroupCount(DsonDocumentHandle handle, int geomIndex);
DSONPARSER_API const char* DsonDocument_GetPolygonMaterialGroupName(DsonDocumentHandle handle, int geomIndex, int groupIndex);

// Material groups on material instances (library materials — typically empty for library entries)
DSONPARSER_API int         DsonDocument_GetMaterialGroupCount(DsonDocumentHandle handle, int matIndex);
DSONPARSER_API const char* DsonDocument_GetMaterialGroupName(DsonDocumentHandle handle, int matIndex, int groupIndex);

// Material groups on scene material instances (scene.materials — "groups" maps to polygon_material_groups)
DSONPARSER_API int         DsonDocument_GetSceneMaterialGroupCount(DsonDocumentHandle handle, int matIndex);
DSONPARSER_API const char* DsonDocument_GetSceneMaterialGroupName(DsonDocumentHandle handle, int matIndex, int groupIndex);

// Get modifier info by index
DSONPARSER_API const char* DsonDocument_GetModifierId(DsonDocumentHandle handle, int index);
DSONPARSER_API const char* DsonDocument_GetModifierName(DsonDocumentHandle handle, int index);
DSONPARSER_API const char* DsonDocument_GetModifierType(DsonDocumentHandle handle, int index);
// Skin binding info for a modifier (0 if the modifier has no skin payload)
DSONPARSER_API int DsonDocument_GetModifierSkinVertexCount(DsonDocumentHandle handle, int index);
DSONPARSER_API int DsonDocument_GetModifierSkinJointCount(DsonDocumentHandle handle, int index);

// ---- B. Skeleton / Nodes ----
DSONPARSER_API const char* DsonDocument_GetNodeParent(DsonDocumentHandle handle, int nodeIndex);
DSONPARSER_API double      DsonDocument_GetNodeEndPointX(DsonDocumentHandle handle, int nodeIndex);
DSONPARSER_API double      DsonDocument_GetNodeEndPointY(DsonDocumentHandle handle, int nodeIndex);
DSONPARSER_API double      DsonDocument_GetNodeEndPointZ(DsonDocumentHandle handle, int nodeIndex);
DSONPARSER_API double      DsonDocument_GetNodeOrientationX(DsonDocumentHandle handle, int nodeIndex);
DSONPARSER_API double      DsonDocument_GetNodeOrientationY(DsonDocumentHandle handle, int nodeIndex);
DSONPARSER_API double      DsonDocument_GetNodeOrientationZ(DsonDocumentHandle handle, int nodeIndex);
DSONPARSER_API const char* DsonDocument_GetNodeRotationOrder(DsonDocumentHandle handle, int nodeIndex);
DSONPARSER_API double      DsonDocument_GetNodeTranslationX(DsonDocumentHandle handle, int nodeIndex);
DSONPARSER_API double      DsonDocument_GetNodeTranslationY(DsonDocumentHandle handle, int nodeIndex);
DSONPARSER_API double      DsonDocument_GetNodeTranslationZ(DsonDocumentHandle handle, int nodeIndex);
DSONPARSER_API double      DsonDocument_GetNodeRotationX(DsonDocumentHandle handle, int nodeIndex);
DSONPARSER_API double      DsonDocument_GetNodeRotationY(DsonDocumentHandle handle, int nodeIndex);
DSONPARSER_API double      DsonDocument_GetNodeRotationZ(DsonDocumentHandle handle, int nodeIndex);
DSONPARSER_API double      DsonDocument_GetNodeScaleX(DsonDocumentHandle handle, int nodeIndex);
DSONPARSER_API double      DsonDocument_GetNodeScaleY(DsonDocumentHandle handle, int nodeIndex);
DSONPARSER_API double      DsonDocument_GetNodeScaleZ(DsonDocumentHandle handle, int nodeIndex);
DSONPARSER_API double      DsonDocument_GetNodeGeneralScale(DsonDocumentHandle handle, int nodeIndex);

// ---- C. Skin Weights ----
// modifierIndex is an index into the full modifier library (same as GetModifierId etc.)
DSONPARSER_API int         DsonDocument_GetSkinJointCount(DsonDocumentHandle handle, int modifierIndex);
DSONPARSER_API const char* DsonDocument_GetSkinJointNodeId(DsonDocumentHandle handle, int modifierIndex, int jointIndex);
DSONPARSER_API int         DsonDocument_GetSkinJointWeightCount(DsonDocumentHandle handle, int modifierIndex, int jointIndex);
DSONPARSER_API int         DsonDocument_GetSkinJointWeightVertexIndex(DsonDocumentHandle handle, int modifierIndex, int jointIndex, int weightIndex);
DSONPARSER_API double      DsonDocument_GetSkinJointWeight(DsonDocumentHandle handle, int modifierIndex, int jointIndex, int weightIndex);

// Processed per-vertex skin weight queries (inverted, sorted descending, normalized, capped).
// Call GetVertexInfluenceCount first to size arrays, then GetVertexBoneInfluence for each influence.
// Pass maxInfluences=8 for UE5 FSoftSkinVertex; must be >= 1.
DSONPARSER_API int  DsonDocument_GetVertexInfluenceCount(DsonDocumentHandle handle, int modifierIndex, int vertexIndex, int maxInfluences);

// Returns the bone node id string and normalized weight [0,1] for influence i on vertexIndex.
// Influences are sorted descending by weight; all influences for this vertex sum to 1.0.
// Sets *boneNodeId="" and *weight=0.0 and returns false if influenceIndex is out of range.
DSONPARSER_API bool DsonDocument_GetVertexBoneInfluence(DsonDocumentHandle handle, int modifierIndex, int vertexIndex, int influenceIndex, const char** boneNodeId, double* weight);

// Same as GetVertexBoneInfluence but weights are renormalized over the top
// min(totalInfluences, maxInfluences) influences so they sum to 1.0.
// Use this instead of GetVertexBoneInfluence when building FSoftSkinVertex.
DSONPARSER_API bool DsonDocument_GetVertexBoneInfluenceCapped(DsonDocumentHandle handle, int modifierIndex, int vertexIndex, int influenceIndex, int maxInfluences, const char** boneNodeId, double* weight);

// ---- D. UV Sets (library uv_sets, not scene instances) ----
DSONPARSER_API const char* DsonDocument_GetUVSetId(DsonDocumentHandle handle, int uvSetIndex);
DSONPARSER_API int         DsonDocument_GetUVCount(DsonDocumentHandle handle, int uvSetIndex);
DSONPARSER_API double      DsonDocument_GetUVU(DsonDocumentHandle handle, int uvSetIndex, int uvIndex);
DSONPARSER_API double      DsonDocument_GetUVV(DsonDocumentHandle handle, int uvSetIndex, int uvIndex);
// vertex_count from the uv_set entry — needed for identity-default expansion
// when consumers expand sparse overrides into a flat per-corner array.
DSONPARSER_API int DsonDocument_GetUVSetVertexCount(DsonDocumentHandle handle, int uvSetIndex);

// Sparse UV override entries from polygon_vertex_indices.
// Each entry is [face, corner, uv_index] for a face corner whose UV index
// differs from its vertex index. Consumers expand to a flat per-corner array
// by seeding with identity (uv_index = vertex_index) and applying overrides.
DSONPARSER_API int DsonDocument_GetUVOverrideCount(DsonDocumentHandle handle, int uvSetIndex);
DSONPARSER_API int DsonDocument_GetUVOverrideFace(DsonDocumentHandle handle, int uvSetIndex, int overrideIndex);
DSONPARSER_API int DsonDocument_GetUVOverrideCorner(DsonDocumentHandle handle, int uvSetIndex, int overrideIndex);
DSONPARSER_API int DsonDocument_GetUVOverrideUVIndex(DsonDocumentHandle handle, int uvSetIndex, int overrideIndex);

// Returns 0 for the sparse triplet format used by DAZ uv_set DSFs (the common case).
// Use the GetUVOverride* family above for sparse data.
DSONPARSER_API int         DsonDocument_GetUVPolygonVertexIndexCount(DsonDocumentHandle handle, int uvSetIndex);
DSONPARSER_API int         DsonDocument_GetUVPolygonVertexIndex(DsonDocumentHandle handle, int uvSetIndex, int index);

// ---- E. Materials (library materials by index) ----
//
// Channels are accessed by index in the range [0, ChannelCount-1]. Each channel carries a DAZ
// id string (e.g. "Diffuse Color", "Normal Map") and a DAZ type string (e.g. "float_color",
// "float"). Consumers iterate or search by id to interpret them; the parser stores everything
// in source-file order and drops nothing.
//
DSONPARSER_API const char* DsonDocument_GetMaterialName(DsonDocumentHandle handle, int matIndex);
DSONPARSER_API const char* DsonDocument_GetMaterialGeometryId(DsonDocumentHandle handle, int matIndex);
DSONPARSER_API const char* DsonDocument_GetMaterialUVSetId(DsonDocumentHandle handle, int matIndex);

// Returns the top-level material type field (e.g. "studio/material/iray").
DSONPARSER_API const char* DsonDocument_GetMaterialType(DsonDocumentHandle handle, int matIndex);
// Returns the shader type from the material's extra[] (e.g. "studio/material/uber_iray", "studio/material/pbr_skin").
// Points to parser-owned memory; copy immediately if retention past another API call is needed.
// Returns "" when no matching extra entry exists or matIndex is out of range.
DSONPARSER_API const char* DsonDocument_GetMaterialShaderType(DsonDocumentHandle handle, int matIndex);

// Indexed channel accessors for library materials (matIndex into material_library).
DSONPARSER_API int         DsonDocument_GetMaterialChannelCount(DsonDocumentHandle handle, int matIndex);
DSONPARSER_API const char* DsonDocument_GetMaterialChannelId(DsonDocumentHandle handle, int matIndex, int channelIdx);
DSONPARSER_API const char* DsonDocument_GetMaterialChannelType(DsonDocumentHandle handle, int matIndex, int channelIdx);
DSONPARSER_API double      DsonDocument_GetMaterialChannelValue(DsonDocumentHandle handle, int matIndex, int channelIdx);
DSONPARSER_API double      DsonDocument_GetMaterialChannelColorR(DsonDocumentHandle handle, int matIndex, int channelIdx);
DSONPARSER_API double      DsonDocument_GetMaterialChannelColorG(DsonDocumentHandle handle, int matIndex, int channelIdx);
DSONPARSER_API double      DsonDocument_GetMaterialChannelColorB(DsonDocumentHandle handle, int matIndex, int channelIdx);
DSONPARSER_API bool        DsonDocument_GetMaterialChannelHasColor(DsonDocumentHandle handle, int matIndex, int channelIdx);
DSONPARSER_API const char* DsonDocument_GetMaterialChannelImageUrl(DsonDocumentHandle handle, int matIndex, int channelIdx);
DSONPARSER_API const char* DsonDocument_GetMaterialChannelTexturePath(DsonDocumentHandle handle, int matIndex, int channelIdx);

// Surface-level accessors for scene material instances (scene.materials).
DSONPARSER_API const char* DsonDocument_GetSceneMaterialName(DsonDocumentHandle handle, int sceneMatIndex);
DSONPARSER_API const char* DsonDocument_GetSceneMaterialGeometryId(DsonDocumentHandle handle, int sceneMatIndex);
DSONPARSER_API const char* DsonDocument_GetSceneMaterialUVSetId(DsonDocumentHandle handle, int sceneMatIndex);
DSONPARSER_API const char* DsonDocument_GetSceneMaterialType(DsonDocumentHandle handle, int sceneMatIndex);
DSONPARSER_API const char* DsonDocument_GetSceneMaterialShaderType(DsonDocumentHandle handle, int sceneMatIndex);

// Indexed channel accessors for scene material instances (scene.materials).
DSONPARSER_API int         DsonDocument_GetSceneMaterialChannelCount(DsonDocumentHandle handle, int sceneMatIndex);
DSONPARSER_API const char* DsonDocument_GetSceneMaterialChannelId(DsonDocumentHandle handle, int sceneMatIndex, int channelIdx);
DSONPARSER_API const char* DsonDocument_GetSceneMaterialChannelType(DsonDocumentHandle handle, int sceneMatIndex, int channelIdx);
DSONPARSER_API double      DsonDocument_GetSceneMaterialChannelValue(DsonDocumentHandle handle, int sceneMatIndex, int channelIdx);
DSONPARSER_API double      DsonDocument_GetSceneMaterialChannelColorR(DsonDocumentHandle handle, int sceneMatIndex, int channelIdx);
DSONPARSER_API double      DsonDocument_GetSceneMaterialChannelColorG(DsonDocumentHandle handle, int sceneMatIndex, int channelIdx);
DSONPARSER_API double      DsonDocument_GetSceneMaterialChannelColorB(DsonDocumentHandle handle, int sceneMatIndex, int channelIdx);
DSONPARSER_API bool        DsonDocument_GetSceneMaterialChannelHasColor(DsonDocumentHandle handle, int sceneMatIndex, int channelIdx);
DSONPARSER_API const char* DsonDocument_GetSceneMaterialChannelImageUrl(DsonDocumentHandle handle, int sceneMatIndex, int channelIdx);
DSONPARSER_API const char* DsonDocument_GetSceneMaterialChannelTexturePath(DsonDocumentHandle handle, int sceneMatIndex, int channelIdx);

// ---- F. Morph Targets ----
// morphIndex is an index into the filtered list of modifiers where type == "morph"
DSONPARSER_API int         DsonDocument_GetMorphCount(DsonDocumentHandle handle);
DSONPARSER_API const char* DsonDocument_GetMorphName(DsonDocumentHandle handle, int morphIndex);
DSONPARSER_API const char* DsonDocument_GetMorphLabel(DsonDocumentHandle handle, int morphIndex);
DSONPARSER_API int         DsonDocument_GetMorphDeltaCount(DsonDocumentHandle handle, int morphIndex);
DSONPARSER_API int         DsonDocument_GetMorphDeltaVertexIndex(DsonDocumentHandle handle, int morphIndex, int deltaIndex);
DSONPARSER_API double      DsonDocument_GetMorphDeltaX(DsonDocumentHandle handle, int morphIndex, int deltaIndex);
DSONPARSER_API double      DsonDocument_GetMorphDeltaY(DsonDocumentHandle handle, int morphIndex, int deltaIndex);
DSONPARSER_API double      DsonDocument_GetMorphDeltaZ(DsonDocumentHandle handle, int morphIndex, int deltaIndex);

DSONPARSER_API int         DsonDocument_GetMorphNormalDeltaCount(DsonDocumentHandle handle, int morphIndex);
DSONPARSER_API int         DsonDocument_GetMorphNormalDeltaVertexIndex(DsonDocumentHandle handle, int morphIndex, int deltaIndex);
DSONPARSER_API double      DsonDocument_GetMorphNormalDeltaX(DsonDocumentHandle handle, int morphIndex, int deltaIndex);
DSONPARSER_API double      DsonDocument_GetMorphNormalDeltaY(DsonDocumentHandle handle, int morphIndex, int deltaIndex);
DSONPARSER_API double      DsonDocument_GetMorphNormalDeltaZ(DsonDocumentHandle handle, int morphIndex, int deltaIndex);
// Returns the geometry id fragment from the morph modifier's parent URL (the part after '#').
// e.g. parent = ".../Genesis9.dsf#Genesis9-1" → returns "Genesis9-1". Returns "" if no '#' or out of range.
DSONPARSER_API const char* DsonDocument_GetMorphGeometryId(DsonDocumentHandle handle, int morphIndex);

// Unknown keys diagnostics
DSONPARSER_API int DsonDocument_GetContextCount(DsonDocumentHandle handle);
DSONPARSER_API const char* DsonDocument_GetContextName(DsonDocumentHandle handle, int index);
DSONPARSER_API int DsonDocument_GetUnknownKeyCount(DsonDocumentHandle handle, const char* context);
DSONPARSER_API const char* DsonDocument_GetUnknownKey(DsonDocumentHandle handle, const char* context, int index);

// Clear document
DSONPARSER_API void DsonDocument_Clear(DsonDocumentHandle handle);

// Destroy document
DSONPARSER_API void DsonDocument_Destroy(DsonDocumentHandle handle);

// Get last error message
DSONPARSER_API const char* DsonParser_GetLastError();

#ifdef __cplusplus
}
#endif
