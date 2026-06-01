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