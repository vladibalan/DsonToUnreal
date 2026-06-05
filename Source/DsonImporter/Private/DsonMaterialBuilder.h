#pragma once
#include "CoreMinimal.h"
#include "DsonParserFunctions.h"  // DsonDocumentHandle

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
    // OutUvSetUrl receives the raw UV set URL (with fragment) from the first scene material that
    // has one; empty if none found.
    void BuildAllSceneMaterials(
        const FString& DufPath,
        const FString& OutputFolder,
        TMap<FString, UMaterialInstanceConstant*>& OutByGroup,
        FString& OutUvSetUrl);

    int32 GetBuiltCount()    const { return BuiltCount;    }
    int32 GetFailureCount()  const { return FailureCount;  }
    int32 GetIrayUberCount() const { return IrayUberCount; }
    int32 GetPBRSkinCount()  const { return PBRSkinCount;  }
    int32 GetDefaultCount()  const { return DefaultCount;  }

private:
    // Chooses the UE master material family from DAZ scene material metadata.
    // URL fragments are preferred because DAZ shader_type can be generic or absent.
    EDazShaderKind DetectShader(const FString& Url, const FString& ShaderType) const;

    // Loads and caches the content master material for the selected shader kind.
    // Parameter names on these assets must match MaterialMastersV1.md and mapping tables.
    UMaterial*     LoadMasterForShader(EDazShaderKind Kind);

    void RecordShaderKind(EDazShaderKind Kind);
    void RecordFailure();

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
