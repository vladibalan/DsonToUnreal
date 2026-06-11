#include "DsonRecipeBuilder.h"
#include "DsonAssetRecipe.h"
#include "DsonAssetUtils.h"
#include "DsonContentRoots.h"
#include "DsonImporter.h"
#include "DsonImportTypes.h"
#include "DsonImportUtils.h"
#include "DsonLoadedDocument.h"
#include "DsonParserFunctions.h"

static FString S(const char* Raw) { return DsonImportUtils::FromUtf8(Raw); }

// Reads all 14 compositing fields for one per-image layer (image_library family).
static FDsonLieLayer ReadImageLayer(DsonDocumentHandle Doc, int32 ImageIdx, int32 LayerIdx)
{
    FDsonLieLayer L;
    // R3: each string accessor result copied via S() before the next parser call
    L.TexturePath = S(GDsonParser.GetImageLayerTexturePath ? GDsonParser.GetImageLayerTexturePath(Doc, ImageIdx, LayerIdx) : nullptr);
    L.BlendMode   = S(GDsonParser.GetImageLayerBlendMode   ? GDsonParser.GetImageLayerBlendMode  (Doc, ImageIdx, LayerIdx) : nullptr);
    L.Opacity  = static_cast<float>(GDsonParser.GetImageLayerOpacity  ? GDsonParser.GetImageLayerOpacity (Doc, ImageIdx, LayerIdx) : 1.0);
    L.bActive  = GDsonParser.GetImageLayerActive  ? GDsonParser.GetImageLayerActive (Doc, ImageIdx, LayerIdx) : false;
    L.bInvert  = GDsonParser.GetImageLayerInvert  ? GDsonParser.GetImageLayerInvert (Doc, ImageIdx, LayerIdx) : false;
    L.ColorR   = static_cast<float>(GDsonParser.GetImageLayerColorR   ? GDsonParser.GetImageLayerColorR  (Doc, ImageIdx, LayerIdx) : 1.0);
    L.ColorG   = static_cast<float>(GDsonParser.GetImageLayerColorG   ? GDsonParser.GetImageLayerColorG  (Doc, ImageIdx, LayerIdx) : 1.0);
    L.ColorB   = static_cast<float>(GDsonParser.GetImageLayerColorB   ? GDsonParser.GetImageLayerColorB  (Doc, ImageIdx, LayerIdx) : 1.0);
    L.Rotation = static_cast<float>(GDsonParser.GetImageLayerRotation ? GDsonParser.GetImageLayerRotation(Doc, ImageIdx, LayerIdx) : 0.0);
    L.ScaleX   = static_cast<float>(GDsonParser.GetImageLayerScaleX   ? GDsonParser.GetImageLayerScaleX  (Doc, ImageIdx, LayerIdx) : 1.0);
    L.ScaleY   = static_cast<float>(GDsonParser.GetImageLayerScaleY   ? GDsonParser.GetImageLayerScaleY  (Doc, ImageIdx, LayerIdx) : 1.0);
    L.OffsetX  = static_cast<float>(GDsonParser.GetImageLayerOffsetX  ? GDsonParser.GetImageLayerOffsetX (Doc, ImageIdx, LayerIdx) : 0.0);
    L.OffsetY  = static_cast<float>(GDsonParser.GetImageLayerOffsetY  ? GDsonParser.GetImageLayerOffsetY (Doc, ImageIdx, LayerIdx) : 0.0);
    L.bMirrorX = GDsonParser.GetImageLayerMirrorX ? GDsonParser.GetImageLayerMirrorX(Doc, ImageIdx, LayerIdx) : false;
    L.bMirrorY = GDsonParser.GetImageLayerMirrorY ? GDsonParser.GetImageLayerMirrorY(Doc, ImageIdx, LayerIdx) : false;
    return L;
}

// Reads all 14 compositing fields for one per-channel layer (scene material channel family).
static FDsonLieLayer ReadChannelLayer(uint64_t H, int32 MatIdx, int32 ChanIdx, int32 LayerIdx)
{
    FDsonLieLayer L;
    L.TexturePath = S(GDsonParser.GetSceneMaterialChannelLayerTexturePath ? GDsonParser.GetSceneMaterialChannelLayerTexturePath(H, MatIdx, ChanIdx, LayerIdx) : nullptr);
    L.BlendMode   = S(GDsonParser.GetSceneMaterialChannelLayerBlendMode   ? GDsonParser.GetSceneMaterialChannelLayerBlendMode  (H, MatIdx, ChanIdx, LayerIdx) : nullptr);
    L.Opacity  = static_cast<float>(GDsonParser.GetSceneMaterialChannelLayerOpacity  ? GDsonParser.GetSceneMaterialChannelLayerOpacity (H, MatIdx, ChanIdx, LayerIdx) : 1.0);
    L.bActive  = GDsonParser.GetSceneMaterialChannelLayerActive  ? GDsonParser.GetSceneMaterialChannelLayerActive (H, MatIdx, ChanIdx, LayerIdx) : false;
    L.bInvert  = GDsonParser.GetSceneMaterialChannelLayerInvert  ? GDsonParser.GetSceneMaterialChannelLayerInvert (H, MatIdx, ChanIdx, LayerIdx) : false;
    L.ColorR   = static_cast<float>(GDsonParser.GetSceneMaterialChannelLayerColorR   ? GDsonParser.GetSceneMaterialChannelLayerColorR  (H, MatIdx, ChanIdx, LayerIdx) : 1.0);
    L.ColorG   = static_cast<float>(GDsonParser.GetSceneMaterialChannelLayerColorG   ? GDsonParser.GetSceneMaterialChannelLayerColorG  (H, MatIdx, ChanIdx, LayerIdx) : 1.0);
    L.ColorB   = static_cast<float>(GDsonParser.GetSceneMaterialChannelLayerColorB   ? GDsonParser.GetSceneMaterialChannelLayerColorB  (H, MatIdx, ChanIdx, LayerIdx) : 1.0);
    L.Rotation = static_cast<float>(GDsonParser.GetSceneMaterialChannelLayerRotation ? GDsonParser.GetSceneMaterialChannelLayerRotation(H, MatIdx, ChanIdx, LayerIdx) : 0.0);
    L.ScaleX   = static_cast<float>(GDsonParser.GetSceneMaterialChannelLayerScaleX   ? GDsonParser.GetSceneMaterialChannelLayerScaleX  (H, MatIdx, ChanIdx, LayerIdx) : 1.0);
    L.ScaleY   = static_cast<float>(GDsonParser.GetSceneMaterialChannelLayerScaleY   ? GDsonParser.GetSceneMaterialChannelLayerScaleY  (H, MatIdx, ChanIdx, LayerIdx) : 1.0);
    L.OffsetX  = static_cast<float>(GDsonParser.GetSceneMaterialChannelLayerOffsetX  ? GDsonParser.GetSceneMaterialChannelLayerOffsetX (H, MatIdx, ChanIdx, LayerIdx) : 0.0);
    L.OffsetY  = static_cast<float>(GDsonParser.GetSceneMaterialChannelLayerOffsetY  ? GDsonParser.GetSceneMaterialChannelLayerOffsetY (H, MatIdx, ChanIdx, LayerIdx) : 0.0);
    L.bMirrorX = GDsonParser.GetSceneMaterialChannelLayerMirrorX ? GDsonParser.GetSceneMaterialChannelLayerMirrorX(H, MatIdx, ChanIdx, LayerIdx) : false;
    L.bMirrorY = GDsonParser.GetSceneMaterialChannelLayerMirrorY ? GDsonParser.GetSceneMaterialChannelLayerMirrorY(H, MatIdx, ChanIdx, LayerIdx) : false;
    return L;
}

void FDsonRecipeBuilder::Build(const FDsonImportResult& Result)
{
    const FDsonImportSettings& Settings = Result.Settings;

    // R3: open via FDsonLoadedDocument (no hand-rolled Create/Destroy)
    FDsonLoadedDocument Document;
    if (!Document.LoadFromFileAsWarning(Settings.DsonFilePath, TEXT("[recipe]")))
    {
        UE_LOG(LogDsonImporter, Warning,
            TEXT("[recipe] '%s': could not open DUF; recipe skipped"),
            *Settings.CharacterName);
        return;
    }

    const DsonDocumentHandle Doc = Document.GetHandle();
    const uint64_t H = Document.GetHandle64();

    // Create package + asset (R4: FDsonAssetUtils pattern, R1: NewObject UE 5.4 signature)
    const FString RecipeName = Settings.CharacterName + TEXT("_Recipe");
    const FString PackagePath = FDsonAssetUtils::CharacterRoot(Settings.CharacterName) / RecipeName;

    UPackage* Package = FDsonAssetUtils::CreateLoadedPackage(PackagePath, TEXT("[recipe]"));
    if (!Package)
        return;

    UDsonAssetRecipe* Recipe = NewObject<UDsonAssetRecipe>(Package, *RecipeName, RF_Public | RF_Standalone);

    // --- Manifest ---
    // R3: GetAssetId returns const char*, copy immediately via S()
    Recipe->SourceId = S(GDsonParser.GetAssetId ? GDsonParser.GetAssetId(Doc) : nullptr);
    Recipe->CharacterName = Settings.CharacterName;
    if (Result.Skeleton)
        Recipe->Skeleton = Result.Skeleton;
    if (Result.Mesh)
        Recipe->BodyMesh = Result.Mesh;

    // --- Companion slot tags (CompanionMeshes and CompanionSlots are 1:1 aligned by the pipeline) ---
    for (int32 i = 0; i < Result.CompanionMeshes.Num(); ++i)
    {
        FDsonCompanionSlotEntry Entry;
        Entry.Slot = Result.CompanionSlots[i];
        Entry.Mesh = Result.CompanionMeshes[i];
        Recipe->CompanionSlots.Add(MoveTemp(Entry));
    }

    // --- Per-surface LIE recipe ---
    // For each scene material channel: if it has inline LIE layers, use the per-channel
    // accessors. If it has 0 inline layers but a #fragment image URL, resolve to
    // image_library and use the per-image accessors (the Nancy head diffuse/SSS case).
    const int32 SceneMatCount = GDsonParser.GetSceneMaterialCount
        ? GDsonParser.GetSceneMaterialCount(H) : 0;

    for (int32 mi = 0; mi < SceneMatCount; ++mi)
    {
        const int32 GroupCount = GDsonParser.GetSceneMaterialGroupCount
            ? GDsonParser.GetSceneMaterialGroupCount(H, mi) : 0;
        // R3: copy group name before next parser call
        const FString GroupName = (GroupCount > 0 && GDsonParser.GetSceneMaterialGroupName)
            ? S(GDsonParser.GetSceneMaterialGroupName(H, mi, 0))
            : FString();

        const int32 ChanCount = GDsonParser.GetSceneMaterialChannelCount
            ? GDsonParser.GetSceneMaterialChannelCount(H, mi) : 0;

        for (int32 ci = 0; ci < ChanCount; ++ci)
        {
            // R3: copy channel id before next parser call
            const FString ChannelId = S(GDsonParser.GetSceneMaterialChannelId
                ? GDsonParser.GetSceneMaterialChannelId(H, mi, ci) : nullptr);

            // Bounds-check LayerCount before reading Opacity (sentinel 0.0 / ScaleX 1.0 hazard)
            const int32 InlineCount = GDsonParser.GetSceneMaterialChannelLayerCount
                ? GDsonParser.GetSceneMaterialChannelLayerCount(H, mi, ci) : 0;

            if (InlineCount > 0)
            {
                FDsonLieSurface Surface;
                Surface.MaterialGroupName = GroupName;
                Surface.ChannelId         = ChannelId;
                Surface.Layers.Reserve(InlineCount);
                for (int32 li = 0; li < InlineCount; ++li)
                    Surface.Layers.Add(ReadChannelLayer(H, mi, ci, li));
                Recipe->LieSurfaces.Add(MoveTemp(Surface));
                continue;
            }

            // No inline layers — check for #fragment image URL (Nancy head diffuse/SSS case)
            // R3: copy image URL before next parser call
            const FString ImageUrl = S(GDsonParser.GetSceneMaterialChannelImageUrl
                ? GDsonParser.GetSceneMaterialChannelImageUrl(H, mi, ci) : nullptr);
            if (!ImageUrl.StartsWith(TEXT("#")))
                continue;

            // R4: shared helper resolves #fragment -> image_library index
            const int32 ImageIndex = DsonImportUtils::FindImageLibraryIndex(Doc, ImageUrl);
            if (ImageIndex == INDEX_NONE)
            {
                UE_LOG(LogDsonImporter, Warning,
                    TEXT("[recipe] '%s' ch '%s': image_library entry not found (ref='%s')"),
                    *GroupName, *ChannelId, *ImageUrl);
                continue;
            }

            const int32 ImgLayerCount = GDsonParser.GetImageLayerCount
                ? GDsonParser.GetImageLayerCount(Doc, ImageIndex) : 0;
            if (ImgLayerCount == 0)
                continue;

            FDsonLieSurface Surface;
            Surface.MaterialGroupName = GroupName;
            Surface.ChannelId         = ChannelId;
            Surface.Layers.Reserve(ImgLayerCount);
            for (int32 li = 0; li < ImgLayerCount; ++li)
                Surface.Layers.Add(ReadImageLayer(Doc, ImageIndex, li));
            Recipe->LieSurfaces.Add(MoveTemp(Surface));
        }
    }

    UE_LOG(LogDsonImporter, Log,
        TEXT("[recipe] '%s': %d companion slot(s), %d LIE surface(s)"),
        *Settings.CharacterName, Recipe->CompanionSlots.Num(), Recipe->LieSurfaces.Num());

    FDsonAssetUtils::SaveAssetPackage(Package, Recipe, PackagePath, TEXT("[recipe]"));
}
