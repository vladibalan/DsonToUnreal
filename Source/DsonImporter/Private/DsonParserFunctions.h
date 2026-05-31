#pragma once

// Opaque handle — mirrors the typedef in DsonParserAPI.h
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
            && GetSceneNodeGeometryUrl   != nullptr;
    }
};

extern FDsonParserAPI GDsonParser;