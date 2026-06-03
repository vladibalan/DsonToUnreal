#pragma once
#include "CoreMinimal.h"

// Matches typedef void* DsonDocumentHandle in DsonParserFunctions.h — identical typedef, safe to repeat.
typedef void* DsonDocumentHandle;

class UMaterialInstanceConstant;
class UMaterial;
class FDsonTextureImporter;

enum class EDazShaderKind : uint8
{
    Default,
    IrayUber,
    PBRSkin,
};

class FDsonMaterialBuilder
{
public:
    // TextureImporter lifetime is owned by the caller; this class holds a reference only.
    FDsonMaterialBuilder(const TArray<FString>& InContentRoots,
                         FDsonTextureImporter& InTextureImporter);

    // Build one UMaterialInstanceConstant for one scene material index.
    // OutputFolder is the /Game/... package folder where the MIC is saved (caller-derived).
    // Returns nullptr on failure; logs reason. Increments the appropriate per-shader counter
    // before attempting the build so failures are still counted by shader kind.
    UMaterialInstanceConstant* BuildSceneMaterial(
        DsonDocumentHandle Doc,
        int32 SceneMatIdx,
        const FString& OutputFolder);

    // Open DufPath, build all scene materials, and populate OutByGroup keyed by group[0] name.
    void BuildAllSceneMaterials(
        const FString& DufPath,
        const FString& OutputFolder,
        TMap<FString, UMaterialInstanceConstant*>& OutByGroup);

    int32 GetBuiltCount()    const { return BuiltCount;    }
    int32 GetFailureCount()  const { return FailureCount;  }
    int32 GetIrayUberCount() const { return IrayUberCount; }
    int32 GetPBRSkinCount()  const { return PBRSkinCount;  }
    int32 GetDefaultCount()  const { return DefaultCount;  }

private:
    EDazShaderKind DetectShader(const FString& Url, const FString& ShaderType) const;
    UMaterial*     LoadMasterForShader(EDazShaderKind Kind);

    TArray<FString>           ContentRoots;
    FDsonTextureImporter&     TextureImporter;

    TWeakObjectPtr<UMaterial> CachedIrayUberMaster;
    TWeakObjectPtr<UMaterial> CachedPBRSkinMaster;
    TWeakObjectPtr<UMaterial> CachedDefaultMaster;

    int32 BuiltCount    = 0;
    int32 FailureCount  = 0;
    int32 IrayUberCount = 0;
    int32 PBRSkinCount  = 0;
    int32 DefaultCount  = 0;
};