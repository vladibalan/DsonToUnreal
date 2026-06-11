#include "DsonRecipeBuilder.h"
#include "DsonAssetRecipe.h"
#include "DsonAssetUtils.h"
#include "DsonContentRoots.h"
#include "DsonImporter.h"
#include "DsonImportTypes.h"
#include "DsonImportUtils.h"
#include "DsonLoadedDocument.h"
#include "DsonParserFunctions.h"

#include "Animation/MorphTarget.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/Paths.h"

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

// Builds a map from morphId -> sanitized UE morph-target name for every morph in DsonHandle.
// R4: reuses DsonImportUtils::ReadMorphObjectName (same logic the morph builder uses).
static TMap<FString, FString> BuildMorphIdToNameMap(uint64_t DsonHandle)
{
    TMap<FString, FString> IdToName;
    if (!GDsonParser.GetMorphCount || !GDsonParser.GetMorphId)
        return IdToName;

    const int32 MorphCount = GDsonParser.GetMorphCount(DsonHandle);
    IdToName.Reserve(MorphCount);
    for (int32 m = 0; m < MorphCount; ++m)
    {
        // R3: copy id before the next parser call
        const FString MorphId = S(GDsonParser.GetMorphId(DsonHandle, m));
        if (MorphId.IsEmpty())
            continue;
        const FString MorphName = DsonImportUtils::ReadMorphObjectName(DsonHandle, m);
        if (!MorphName.IsEmpty())
            IdToName.Add(MorphId, MorphName);
    }
    return IdToName;
}

// Appends FDsonLieSurface entries from one DUF document (body or companion MAT-preset).
// R4: single loop used for both body and companion figures — do not inline a second copy.
// CompanionSlot: empty for body surfaces; companion slot path for companion-figure surfaces.
// R3: Doc must remain valid for the duration of this call (caller holds the FDsonLoadedDocument).
static void AppendLieSurfaces(
    uint64_t H,
    DsonDocumentHandle Doc,
    const TMap<FString, TSoftObjectPtr<UTexture2D>>& PreBakedComposites,
    const FString& CompanionSlot,
    TArray<FDsonLieSurface>& OutSurfaces)
{
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
                Surface.MaterialGroupName   = GroupName;
                Surface.ChannelId           = ChannelId;
                Surface.SourceCompanionSlot = CompanionSlot;
                Surface.Layers.Reserve(InlineCount);
                for (int32 li = 0; li < InlineCount; ++li)
                    Surface.Layers.Add(ReadChannelLayer(H, mi, ci, li));
                // Inline channel layers are not composited by the importer — no pre-bake marker.
                OutSurfaces.Add(MoveTemp(Surface));
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
            Surface.MaterialGroupName   = GroupName;
            Surface.ChannelId           = ChannelId;
            Surface.SourceCompanionSlot = CompanionSlot;
            Surface.Layers.Reserve(ImgLayerCount);
            for (int32 li = 0; li < ImgLayerCount; ++li)
                Surface.Layers.Add(ReadImageLayer(Doc, ImageIndex, li));

            // R4: do not re-derive the bake decision — record what actually happened (plumbed set).
            // The image id is the URL-decoded #fragment (same key CompositeImageLayers used).
            const FString ImageId = FDsonContentRoots::UrlDecode(ImageUrl.Mid(1));
            if (const TSoftObjectPtr<UTexture2D>* Baked = PreBakedComposites.Find(ImageId))
            {
                Surface.bImporterPreBaked = true;
                Surface.BakedComposite    = *Baked;
            }

            OutSurfaces.Add(MoveTemp(Surface));
        }
    }
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

    // --- Per-surface LIE recipe (body figure + companion figures) ---
    // R4: AppendLieSurfaces handles both; SourceCompanionSlot distinguishes origin.
    AppendLieSurfaces(H, Doc, Result.PreBakedComposites, TEXT(""), Recipe->LieSurfaces);

    for (const FDsonCompanionSource& Companion : Settings.CompanionFigures)
    {
        if (Companion.MatPresetPath.IsEmpty())
            continue;
        // R3: RAII via FDsonLoadedDocument; Doc stays valid for the AppendLieSurfaces call.
        FDsonLoadedDocument CompanionDoc;
        if (!CompanionDoc.LoadFromFileAsWarning(Companion.MatPresetPath, TEXT("[recipe-LIE-companion]")))
        {
            UE_LOG(LogDsonImporter, Warning,
                TEXT("[recipe] '%s': companion '%s' MAT-preset could not be loaded; companion LIE surfaces skipped"),
                *Settings.CharacterName, *Companion.Slot);
            continue;
        }
        AppendLieSurfaces(
            CompanionDoc.GetHandle64(), CompanionDoc.GetHandle(),
            Result.PreBakedComposites, Companion.Slot, Recipe->LieSurfaces);
    }

    // --- Dial weights ---
    // Enumerate scene.modifiers from the DUF. For each URL: URL-decode the fragment id,
    // resolve the referenced DSF, build morphId->name from it (cached by path), and emit
    // a bound FDsonDialWeight only if the name is in the imported UMorphTarget set (so
    // HD morphs and control morphs with no UMorphTarget produce no dangling binding).
    int32 DiagTotalModifiers = 0;
    int32 DiagNonDefault     = 0;
    int32 DiagCorrelated     = 0;
    int32 DiagUncorrelated   = 0;

    const bool bHaveDialAccessors =
        GDsonParser.GetSceneModifierCount &&
        GDsonParser.GetSceneModifierUrl   &&
        GDsonParser.GetSceneModifierChannelValue;

    if (bHaveDialAccessors)
    {
        // R1: UE 5.4 GetMorphTargets() returns const TArray<TObjectPtr<UMorphTarget>>&
        TSet<FString> ImportedTargetNames;
        if (Result.Mesh)
        {
            for (const TObjectPtr<UMorphTarget>& MT : Result.Mesh->GetMorphTargets())
                if (MT)
                    ImportedTargetNames.Add(MT->GetFName().ToString());
        }

        const TArray<FString> ContentRoots = FDsonContentRoots::Detect();
        // Cache: normalized path -> morphId->name; avoids reopening the same DSF per modifier entry.
        TMap<FString, TMap<FString, FString>> PathToMorphIdMap;

        DiagTotalModifiers = GDsonParser.GetSceneModifierCount(H);
        for (int32 si = 0; si < DiagTotalModifiers; ++si)
        {
            // R3: copy URL before next parser call
            const FString Url = S(GDsonParser.GetSceneModifierUrl(H, si));

            const float Value = static_cast<float>(
                GDsonParser.GetSceneModifierChannelValue(H, si));
            const float Min = static_cast<float>(
                GDsonParser.GetSceneModifierChannelMin
                    ? GDsonParser.GetSceneModifierChannelMin(H, si) : 0.0);
            const float Max = static_cast<float>(
                GDsonParser.GetSceneModifierChannelMax
                    ? GDsonParser.GetSceneModifierChannelMax(H, si) : 1.0);
            const bool bClamped =
                GDsonParser.GetSceneModifierChannelClamped
                    ? GDsonParser.GetSceneModifierChannelClamped(H, si) : false;

            if (FMath::Abs(Value) > KINDA_SMALL_NUMBER)
                ++DiagNonDefault;

            // Extract and URL-decode the fragment id (1a: R4: FDsonContentRoots::UrlDecode)
            int32 HashIdx = INDEX_NONE;
            if (!Url.FindChar(TEXT('#'), HashIdx) || HashIdx + 1 >= Url.Len())
            {
                ++DiagUncorrelated;
                continue;
            }
            const FString ModifierId = FDsonContentRoots::UrlDecode(Url.Mid(HashIdx + 1));

            // Resolve the referenced DSF from the modifier URL.
            // R4: ResolveUrl strips fragment and decodes the path internally.
            const FString ResolvedPath = FDsonContentRoots::ResolveUrl(Url, ContentRoots);
            if (ResolvedPath.IsEmpty())
            {
                ++DiagUncorrelated;
                continue;
            }

            // Normalize path key for case-insensitive cache lookup (Windows FS).
            FString PathKey = ResolvedPath;
            FPaths::NormalizeFilename(PathKey);
            PathKey.ToLowerInline();

            if (!PathToMorphIdMap.Contains(PathKey))
            {
                // R3: FDsonLoadedDocument RAII; lives only long enough to build the map.
                FDsonLoadedDocument TempDoc;
                if (TempDoc.LoadFromFileAsWarning(ResolvedPath, TEXT("[recipe-dials]")))
                    PathToMorphIdMap.Add(PathKey, BuildMorphIdToNameMap(TempDoc.GetHandle64()));
                else
                    PathToMorphIdMap.Add(PathKey, TMap<FString, FString>());
            }

            const FString* MorphName = PathToMorphIdMap[PathKey].Find(ModifierId);
            if (!MorphName || MorphName->IsEmpty())
            {
                ++DiagUncorrelated;
                if (Settings.bDumpMaterialDiagnostics)
                {
                    UE_LOG(LogDsonImporter, Log,
                        TEXT("[recipe-shape] dial uncorrelated: url='%s' val=%.4f"),
                        *Url, Value);
                }
                continue;
            }

            // Validate against actually-imported UMorphTargets (HD/control morphs stay uncorrelated)
            if (!ImportedTargetNames.Contains(*MorphName))
            {
                ++DiagUncorrelated;
                continue;
            }

            ++DiagCorrelated;

            FDsonDialWeight DW;
            DW.BoundMorphTargetName = *MorphName;
            DW.SourceUrl            = Url;
            DW.Value                = Value;
            DW.Min                  = Min;
            DW.Max                  = Max;
            DW.bClamped             = bClamped;

            if (Settings.bDumpMaterialDiagnostics)
            {
                UE_LOG(LogDsonImporter, Log,
                    TEXT("[recipe-shape] dial: morph='%s' id='%s' val=%.4f min=%.4f max=%.4f clamped=%d"),
                    *DW.BoundMorphTargetName, *ModifierId, DW.Value, DW.Min, DW.Max,
                    static_cast<int32>(DW.bClamped));
            }

            Recipe->DialWeights.Add(MoveTemp(DW));
        }
    }
    else
    {
        UE_LOG(LogDsonImporter, Verbose,
            TEXT("[recipe] '%s': scene-modifier channel accessors unavailable; dial weights skipped"),
            *Settings.CharacterName);
    }

    // --- Pre-baked marker diagnostic ---
    int32 DiagBaked = 0;
    int32 DiagRaw   = 0;
    for (const FDsonLieSurface& Surface : Recipe->LieSurfaces)
    {
        if (Surface.bImporterPreBaked)
            ++DiagBaked;
        else
            ++DiagRaw;

        if (Settings.bDumpMaterialDiagnostics && Surface.bImporterPreBaked)
        {
            UE_LOG(LogDsonImporter, Log,
                TEXT("[recipe-shape] baked LIE: group='%s' channel='%s' path='%s'"),
                *Surface.MaterialGroupName, *Surface.ChannelId,
                *Surface.BakedComposite.ToString());
        }
    }

    // --- Summary diagnostic (required per step 6) ---
    UE_LOG(LogDsonImporter, Log,
        TEXT("[recipe-shape] '%s': modifiers=%d non-default=%d correlated=%d uncorrelated=%d | LIE baked=%d raw=%d | companions=%d"),
        *Settings.CharacterName,
        DiagTotalModifiers, DiagNonDefault, DiagCorrelated, DiagUncorrelated,
        DiagBaked, DiagRaw,
        Recipe->CompanionSlots.Num());

    UE_LOG(LogDsonImporter, Log,
        TEXT("[recipe] '%s': %d dial weight(s), %d companion slot(s), %d LIE surface(s)"),
        *Settings.CharacterName,
        Recipe->DialWeights.Num(), Recipe->CompanionSlots.Num(), Recipe->LieSurfaces.Num());

    FDsonAssetUtils::SaveAssetPackage(Package, Recipe, PackagePath, TEXT("[recipe]"));
}
