#include "DsonRecipeBuilder.h"
#include "DsonAssetRecipe.h"
#include "DsonAssetUtils.h"
#include "DsonContentRoots.h"
#include "DsonImporter.h"
#include "DsonImportTypes.h"
#include "DsonImportUtils.h"
#include "DsonLoadedDocument.h"
#include "DsonMorphBuilder.h"
#include "DsonParserFunctions.h"

#include "Animation/MorphTarget.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/Paths.h"

using DsonImportUtils::FromUtf8;

// Reads all 14 compositing fields for one per-image layer (image_library family).
static FDsonLieLayer ReadImageLayer(DsonDocumentHandle Doc, int32 ImageIdx, int32 LayerIdx)
{
    FDsonLieLayer L;
    // R3: each string accessor result copied via FromUtf8() before the next parser call
    L.TexturePath = FromUtf8(GDsonParser.GetImageLayerTexturePath ? GDsonParser.GetImageLayerTexturePath(Doc, ImageIdx, LayerIdx) : nullptr);
    L.BlendMode   = FromUtf8(GDsonParser.GetImageLayerBlendMode   ? GDsonParser.GetImageLayerBlendMode  (Doc, ImageIdx, LayerIdx) : nullptr);
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
    L.TexturePath = FromUtf8(GDsonParser.GetSceneMaterialChannelLayerTexturePath ? GDsonParser.GetSceneMaterialChannelLayerTexturePath(H, MatIdx, ChanIdx, LayerIdx) : nullptr);
    L.BlendMode   = FromUtf8(GDsonParser.GetSceneMaterialChannelLayerBlendMode   ? GDsonParser.GetSceneMaterialChannelLayerBlendMode  (H, MatIdx, ChanIdx, LayerIdx) : nullptr);
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
        const FString MorphId = FromUtf8(GDsonParser.GetMorphId(DsonHandle, m));
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
            ? FromUtf8(GDsonParser.GetSceneMaterialGroupName(H, mi, 0))
            : FString();
        // R3: copy scene mat id before next parser call
        const FString SceneMatId = FromUtf8(GDsonParser.GetSceneMaterialId
            ? GDsonParser.GetSceneMaterialId(H, mi) : nullptr);

        TSet<FString> EmittedChannels;

        const int32 ChanCount = GDsonParser.GetSceneMaterialChannelCount
            ? GDsonParser.GetSceneMaterialChannelCount(H, mi) : 0;

        for (int32 ci = 0; ci < ChanCount; ++ci)
        {
            // R3: copy channel id before next parser call
            const FString ChannelId = FromUtf8(GDsonParser.GetSceneMaterialChannelId
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
                EmittedChannels.Add(ChannelId);
                continue;
            }

            // No inline layers — check for #fragment image URL (Nancy head diffuse/SSS case)
            // R3: copy image URL before next parser call
            const FString ImageUrl = FromUtf8(GDsonParser.GetSceneMaterialChannelImageUrl
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
            EmittedChannels.Add(ChannelId);
        }

        // Anim-bound image LIE scan: handles scene.animations key-0 Leaf=="image" entries
        // (e.g. eye LIE on G9 Eyes companion), which the channel walk does not see.
        const int32 AnimCount = GDsonParser.GetSceneAnimationCount
            ? GDsonParser.GetSceneAnimationCount(Doc) : 0;

        for (int32 ai = 0; ai < AnimCount; ++ai)
        {
            // R3: copy URL before next parser call
            const FString AnimUrl = FromUtf8(GDsonParser.GetSceneAnimationUrl
                ? GDsonParser.GetSceneAnimationUrl(Doc, ai) : nullptr);
            if (AnimUrl.IsEmpty())
                continue;

            FString ParsedMatId, ChannelId, Leaf;
            if (!DsonImportUtils::ParseAnimationUrl(AnimUrl, ParsedMatId, ChannelId, Leaf))
                continue;
            if (Leaf != TEXT("image"))
                continue;

            // matId reconciliation: same as ApplySceneAnimationOverrides
            const FString DecodedParsedMatId = FDsonContentRoots::UrlDecode(ParsedMatId);
            if (DecodedParsedMatId != SceneMatId &&
                DecodedParsedMatId != DsonImportUtils::StripUniquifyingSuffix(SceneMatId))
                continue;

            // Dedup: warn if channel was already emitted by the channel walk (both-paths collision)
            if (EmittedChannels.Contains(ChannelId))
            {
                UE_LOG(LogDsonImporter, Warning,
                    TEXT("[recipe] '%s' ch '%s': anim image and channel-walk both reachable; anim path skipped (dedup)"),
                    *GroupName, *ChannelId);
                continue;
            }

            const int32 ValueKind = GDsonParser.GetSceneAnimationValueKind
                ? GDsonParser.GetSceneAnimationValueKind(Doc, ai) : -1;
            if (ValueKind != 3)
                continue;

            // R3: copy string value before next parser call
            const FString FragmentRef = FromUtf8(GDsonParser.GetSceneAnimationString
                ? GDsonParser.GetSceneAnimationString(Doc, ai) : nullptr);
            if (!FragmentRef.StartsWith(TEXT("#")))
            {
                UE_LOG(LogDsonImporter, Warning,
                    TEXT("[recipe] '%s' ch '%s': anim image value not a #fragment ref (val='%s')"),
                    *GroupName, *ChannelId, *FragmentRef);
                continue;
            }

            // R4: shared helper resolves #fragment -> image_library index
            const int32 AnimImageIndex = DsonImportUtils::FindImageLibraryIndex(Doc, FragmentRef);
            if (AnimImageIndex == INDEX_NONE)
            {
                UE_LOG(LogDsonImporter, Warning,
                    TEXT("[recipe] '%s' ch '%s': anim image_library entry not found (ref='%s')"),
                    *GroupName, *ChannelId, *FragmentRef);
                continue;
            }

            const int32 LayerCount = GDsonParser.GetImageLayerCount
                ? GDsonParser.GetImageLayerCount(Doc, AnimImageIndex) : 0;
            if (LayerCount == 0)
                continue;

            FDsonLieSurface Surface;
            Surface.MaterialGroupName   = GroupName;
            Surface.ChannelId           = ChannelId;
            Surface.SourceCompanionSlot = CompanionSlot;
            Surface.Layers.Reserve(LayerCount);
            for (int32 li = 0; li < LayerCount; ++li)
                Surface.Layers.Add(ReadImageLayer(Doc, AnimImageIndex, li));

            // Pre-bake marker: same key the channel path uses (UrlDecode of #fragment id)
            const FString AnimImageId = FDsonContentRoots::UrlDecode(FragmentRef.Mid(1));
            if (const TSoftObjectPtr<UTexture2D>* Baked = PreBakedComposites.Find(AnimImageId))
            {
                Surface.bImporterPreBaked = true;
                Surface.BakedComposite    = *Baked;
            }

            EmittedChannels.Add(ChannelId);
            OutSurfaces.Add(MoveTemp(Surface));
        }
    }
}

// Classifies a formula output URL by its ?property suffix.
// Mechanical derivation only — no formula evaluation.
static EDsonFormulaTarget ClassifyOutputUrl(const FString& OutputUrl)
{
    int32 QueryIdx = INDEX_NONE;
    if (!OutputUrl.FindChar(TEXT('?'), QueryIdx))
        return EDsonFormulaTarget::Other;
    const FString Prop = OutputUrl.Mid(QueryIdx + 1);
    if (Prop.Equals(TEXT("value"), ESearchCase::CaseSensitive))
        return EDsonFormulaTarget::MorphValue;
    if (Prop.StartsWith(TEXT("center_point"), ESearchCase::CaseSensitive))
        return EDsonFormulaTarget::BoneCenterPoint;
    if (Prop.StartsWith(TEXT("end_point"), ESearchCase::CaseSensitive))
        return EDsonFormulaTarget::BoneEndPoint;
    return EDsonFormulaTarget::Other;
}

// Reads OutputUrl, Stage, OutputTarget, and Operations for one formula index.
// bIsScene selects the scene-modifier vs. modifier-library accessor family (R7: all optional exports
// guarded). Caller fills SourceModifierId, SourceModifierName, bFromSceneModifier,
// BoundMorphTargetName, and SourceValue.
// R3: every const char* copied via FromUtf8() before the next parser call.
static FDsonFormula ReadOneFormula(uint64_t Handle, int32 ModIdx, int32 FormulaIdx, bool bIsScene)
{
    FDsonFormula F;
    if (bIsScene)
    {
        F.OutputUrl = FromUtf8(GDsonParser.GetSceneModifierFormulaOutput
            ? GDsonParser.GetSceneModifierFormulaOutput(Handle, ModIdx, FormulaIdx) : nullptr);
        F.Stage     = FromUtf8(GDsonParser.GetSceneModifierFormulaStage
            ? GDsonParser.GetSceneModifierFormulaStage(Handle, ModIdx, FormulaIdx) : nullptr);
    }
    else
    {
        F.OutputUrl = FromUtf8(GDsonParser.GetModifierFormulaOutput
            ? GDsonParser.GetModifierFormulaOutput(Handle, ModIdx, FormulaIdx) : nullptr);
        F.Stage     = FromUtf8(GDsonParser.GetModifierFormulaStage
            ? GDsonParser.GetModifierFormulaStage(Handle, ModIdx, FormulaIdx) : nullptr);
    }
    F.OutputTarget = ClassifyOutputUrl(F.OutputUrl);

    const int32 OpCount = bIsScene
        ? (GDsonParser.GetSceneModifierFormulaOperationCount
            ? GDsonParser.GetSceneModifierFormulaOperationCount(Handle, ModIdx, FormulaIdx) : 0)
        : (GDsonParser.GetModifierFormulaOperationCount
            ? GDsonParser.GetModifierFormulaOperationCount(Handle, ModIdx, FormulaIdx) : 0);
    F.Operations.Reserve(OpCount);
    for (int32 oi = 0; oi < OpCount; ++oi)
    {
        FDsonFormulaOp Op;
        // R3: copy each string before the next parser call; Val is double (no lifetime concern)
        if (bIsScene)
        {
            Op.Op  = FromUtf8(GDsonParser.GetSceneModifierFormulaOperationOp
                ? GDsonParser.GetSceneModifierFormulaOperationOp (Handle, ModIdx, FormulaIdx, oi) : nullptr);
            Op.Val = GDsonParser.GetSceneModifierFormulaOperationVal
                ? GDsonParser.GetSceneModifierFormulaOperationVal(Handle, ModIdx, FormulaIdx, oi) : 0.0;
            Op.Url = FromUtf8(GDsonParser.GetSceneModifierFormulaOperationUrl
                ? GDsonParser.GetSceneModifierFormulaOperationUrl(Handle, ModIdx, FormulaIdx, oi) : nullptr);
        }
        else
        {
            Op.Op  = FromUtf8(GDsonParser.GetModifierFormulaOperationOp
                ? GDsonParser.GetModifierFormulaOperationOp (Handle, ModIdx, FormulaIdx, oi) : nullptr);
            Op.Val = GDsonParser.GetModifierFormulaOperationVal
                ? GDsonParser.GetModifierFormulaOperationVal(Handle, ModIdx, FormulaIdx, oi) : 0.0;
            Op.Url = FromUtf8(GDsonParser.GetModifierFormulaOperationUrl
                ? GDsonParser.GetModifierFormulaOperationUrl(Handle, ModIdx, FormulaIdx, oi) : nullptr);
        }
        F.Operations.Add(MoveTemp(Op));
    }
    return F;
}

// Linear scan for a node by id in a document's node_library. Returns INDEX_NONE if not found.
// R3: FromUtf8() copies each transient id string before the next GetNodeId call.
static int32 FindNodeByIdLinear(uint64_t DsonHandle, const FString& NodeId)
{
    if (!GDsonParser.GetNodeCount || !GDsonParser.GetNodeId)
        return INDEX_NONE;
    const int32 Count = GDsonParser.GetNodeCount(DsonHandle);
    for (int32 i = 0; i < Count; ++i)
    {
        if (FromUtf8(GDsonParser.GetNodeId(DsonHandle, i)) == NodeId)
            return i;
    }
    return INDEX_NONE;
}

// Walks one document's modifier_library and appends one FDsonFormula per formula.
// bFromSceneModifier: true for externally-referenced DSFs; false for the base figure DSF.
// ModifierIdToDialValue: scene dial value map; only used when bFromSceneModifier==true.
// EmittedKeys: dedup set keyed by "ModifierId|OutputUrl|Stage"; shared across all passes.
// R3: all parser strings copied via FromUtf8() before the next parser call.
// R4: single copy of the modifier_library emit loop — do not inline a second copy.
// R7: missing optional exports return early; open handles are caller-managed (permissive).
static void AppendModifierLibraryFormulas(
    uint64_t Handle,
    const TMap<FString, FString>& MorphIdMap,
    const TSet<FString>& ImportedTargetNames,
    bool bFromSceneModifier,
    const TMap<FString, float>& ModifierIdToDialValue,
    TSet<FString>& EmittedKeys,
    const FDsonImportSettings& Settings,
    TArray<FDsonFormula>& OutFormulas)
{
    if (!GDsonParser.GetModifierCount || !GDsonParser.GetModifierFormulaCount ||
        !GDsonParser.GetModifierFormulaOutput)
        return;

    const int32 ModCount = GDsonParser.GetModifierCount(Handle);
    for (int32 mi = 0; mi < ModCount; ++mi)
    {
        const int32 FormulaCount = GDsonParser.GetModifierFormulaCount(Handle, mi);
        if (FormulaCount <= 0)
            continue;

        // R3: copy id and name before any further parser calls
        const FString ModId   = FromUtf8(GDsonParser.GetModifierId
            ? GDsonParser.GetModifierId  (Handle, mi) : nullptr);
        const FString ModName = FromUtf8(GDsonParser.GetModifierName
            ? GDsonParser.GetModifierName(Handle, mi) : nullptr);

        FString BoundName;
        if (const FString* MorphName = MorphIdMap.Find(ModId))
            if (!MorphName->IsEmpty() && ImportedTargetNames.Contains(*MorphName))
                BoundName = *MorphName;

        const float SourceVal = bFromSceneModifier
            ? ModifierIdToDialValue.FindRef(ModId)
            : 0.0f;

        for (int32 fi = 0; fi < FormulaCount; ++fi)
        {
            FDsonFormula F = ReadOneFormula(Handle, mi, fi, false);

            const FString Key = ModId + TEXT("|") + F.OutputUrl + TEXT("|") + F.Stage;
            if (EmittedKeys.Contains(Key))
                continue;
            EmittedKeys.Add(Key);

            F.SourceModifierId     = ModId;
            F.SourceModifierName   = ModName;
            F.bFromSceneModifier   = bFromSceneModifier;
            F.SourceValue          = SourceVal;
            F.BoundMorphTargetName = BoundName;

            if (Settings.bDumpMaterialDiagnostics)
            {
                UE_LOG(LogDsonImporter, Log,
                    TEXT("[recipe-shape] formula(%s): id='%s' name='%s' output='%s' tag=%d stage='%s' ops=%d bound='%s'"),
                    bFromSceneModifier ? TEXT("external") : TEXT("figure"),
                    *F.SourceModifierId, *F.SourceModifierName, *F.OutputUrl,
                    static_cast<int32>(F.OutputTarget), *F.Stage,
                    F.Operations.Num(), *F.BoundMorphTargetName);
            }
            OutFormulas.Add(MoveTemp(F));
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
    // R3: GetAssetId returns const char*, copy immediately via FromUtf8()
    Recipe->SourceId = FromUtf8(GDsonParser.GetAssetId ? GDsonParser.GetAssetId(Doc) : nullptr);
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

    // --- Dial weights + formula emission ---
    int32 DiagTotalModifiers = 0;
    int32 DiagNonDefault     = 0;
    int32 DiagCorrelated     = 0;
    int32 DiagUncorrelated   = 0;

    // R4: hoisted — shared by dial-weight pass, scene-formula pass, figure-formula pass,
    //     and external-doc formula pass
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
    // modifierId -> scene dial value; populated by the dial pass, consumed by formula passes.
    TMap<FString, float> ModifierIdToDialValue;
    // Dedup across all formula passes: key = "ModifierId|OutputUrl|Stage".
    TSet<FString> EmittedFormulaKeys;

    const bool bHaveDialAccessors =
        GDsonParser.GetSceneModifierCount &&
        GDsonParser.GetSceneModifierUrl   &&
        GDsonParser.GetSceneModifierChannelValue;

    if (bHaveDialAccessors)
    {
        DiagTotalModifiers = GDsonParser.GetSceneModifierCount(H);
        for (int32 si = 0; si < DiagTotalModifiers; ++si)
        {
            // R3: copy URL before next parser call
            const FString Url = FromUtf8(GDsonParser.GetSceneModifierUrl(H, si));

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
            ModifierIdToDialValue.Add(ModifierId, Value);

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

    // --- Scene.modifiers formula emission (second pass; reuses PathToMorphIdMap cache) ---
    // Emits one FDsonFormula per formula on each scene modifier that has any.
    // BoundMorphTargetName reuses the cache built by the dial-weight pass (R4).
    const bool bHaveSceneFormulas =
        GDsonParser.GetSceneModifierCount   &&
        GDsonParser.GetSceneModifierUrl     &&
        GDsonParser.GetSceneModifierFormulaCount &&
        GDsonParser.GetSceneModifierFormulaOutput;

    if (bHaveSceneFormulas)
    {
        const int32 SceneModCount = GDsonParser.GetSceneModifierCount(H);
        for (int32 si = 0; si < SceneModCount; ++si)
        {
            const int32 FormulaCount = GDsonParser.GetSceneModifierFormulaCount(H, si);
            if (FormulaCount <= 0)
                continue;

            // R3: copy URL before next parser call
            const FString Url = FromUtf8(GDsonParser.GetSceneModifierUrl(H, si));

            // Need #fragment for SourceModifierId
            int32 HashIdx = INDEX_NONE;
            if (!Url.FindChar(TEXT('#'), HashIdx) || HashIdx + 1 >= Url.Len())
                continue;
            const FString ModifierId = FDsonContentRoots::UrlDecode(Url.Mid(HashIdx + 1));

            const float SourceValue = static_cast<float>(GDsonParser.GetSceneModifierChannelValue
                ? GDsonParser.GetSceneModifierChannelValue(H, si) : 0.0);

            // BoundMorphTargetName: reuse PathToMorphIdMap (extend if a new DSF is encountered)
            FString BoundName;
            const FString ResolvedPath = FDsonContentRoots::ResolveUrl(Url, ContentRoots);
            if (!ResolvedPath.IsEmpty())
            {
                FString PathKey = ResolvedPath;
                FPaths::NormalizeFilename(PathKey);
                PathKey.ToLowerInline();
                if (!PathToMorphIdMap.Contains(PathKey))
                {
                    FDsonLoadedDocument TempDoc;
                    if (TempDoc.LoadFromFileAsWarning(ResolvedPath, TEXT("[recipe-formulas]")))
                        PathToMorphIdMap.Add(PathKey, BuildMorphIdToNameMap(TempDoc.GetHandle64()));
                    else
                        PathToMorphIdMap.Add(PathKey, TMap<FString, FString>());
                }
                if (const FString* MorphName = PathToMorphIdMap[PathKey].Find(ModifierId))
                    if (!MorphName->IsEmpty() && ImportedTargetNames.Contains(*MorphName))
                        BoundName = *MorphName;
            }

            for (int32 fi = 0; fi < FormulaCount; ++fi)
            {
                FDsonFormula F = ReadOneFormula(H, si, fi, true);
                F.SourceModifierId    = ModifierId;
                F.bFromSceneModifier  = true;
                F.SourceValue         = SourceValue;
                F.BoundMorphTargetName = BoundName;
                // SourceModifierName intentionally empty for scene-modifier formulas

                const FString Key = F.SourceModifierId + TEXT("|") + F.OutputUrl + TEXT("|") + F.Stage;
                if (EmittedFormulaKeys.Contains(Key))
                    continue;
                EmittedFormulaKeys.Add(Key);

                if (Settings.bDumpMaterialDiagnostics)
                {
                    UE_LOG(LogDsonImporter, Log,
                        TEXT("[recipe-shape] formula(scene): id='%s' output='%s' tag=%d stage='%s' ops=%d bound='%s'"),
                        *F.SourceModifierId, *F.OutputUrl, static_cast<int32>(F.OutputTarget),
                        *F.Stage, F.Operations.Num(), *F.BoundMorphTargetName);
                }
                Recipe->Formulas.Add(MoveTemp(F));
            }
        }
    }

    // --- Figure modifier_library formula emission ---
    // Opens the body-figure DSF once and walks all modifier_library entries with formulas.
    // FigureDoc stays in scope for the RigPoints pass that follows.
    FDsonLoadedDocument FigureDoc;
    uint64_t FigHandle = 0;
    if (!Settings.ResolvedFigureDsfPath.IsEmpty())
    {
        if (!FigureDoc.LoadFromFileAsWarning(Settings.ResolvedFigureDsfPath, TEXT("[recipe-formulas-figure]")))
        {
            UE_LOG(LogDsonImporter, Warning,
                TEXT("[recipe] '%s': figure DSF '%s' could not be opened; figure-lib formulas skipped"),
                *Settings.CharacterName, *Settings.ResolvedFigureDsfPath);
        }
        else
        {
            FigHandle = FigureDoc.GetHandle64();
            // R4: reuse BuildMorphIdToNameMap for the figure DSF (same helper used by the dial pass)
            const TMap<FString, FString> FigureMorphIdMap = BuildMorphIdToNameMap(FigHandle);

            // R4: AppendModifierLibraryFormulas is the single copy of the modifier_library walk.
            AppendModifierLibraryFormulas(
                FigHandle, FigureMorphIdMap, ImportedTargetNames, false,
                ModifierIdToDialValue, EmittedFormulaKeys, Settings, Recipe->Formulas);
        }
    }

    // --- External-document formula pass ---
    // Opens all formula-reachable external DSFs (same walk as the morph builder) and
    // emits each document's modifier_library formulas with bFromSceneModifier=true.
    // R4: reuses DiscoverFormulaReachableDocuments (single source of the walk) and
    //     AppendModifierLibraryFormulas (single copy of the emit loop).
    // R7: DiscoverFormulaReachableDocuments is permissive — unresolvable files warn and skip.
    {
        TArray<FDsonLoadedDocument> ExternalDocs;
        TArray<uint64_t> ExternalHandles;
        FDsonMorphBuilder::DiscoverFormulaReachableDocuments(Settings, ExternalDocs, ExternalHandles);
        for (int32 di = 0; di < ExternalDocs.Num(); ++di)
        {
            const TMap<FString, FString> ExtMorphIdMap = BuildMorphIdToNameMap(ExternalHandles[di]);
            AppendModifierLibraryFormulas(
                ExternalHandles[di], ExtMorphIdMap, ImportedTargetNames, true,
                ModifierIdToDialValue, EmittedFormulaKeys, Settings, Recipe->Formulas);
        }
    }

    // --- RigPoints: one entry per unique bone referenced by ERC-follow formulas ---
    // Resolve target node from the formula output URL's #fragment id.
    // Primary: figure DSF (FigHandle); fallback: body DUF (H).
    // Raw DAZ coordinates, not UE-flipped (whole formula block is DAZ-space; R4/R7).
    {
        TSet<FString> EmittedNodeNames;
        for (const FDsonFormula& F : Recipe->Formulas)
        {
            if (F.OutputTarget != EDsonFormulaTarget::BoneCenterPoint &&
                F.OutputTarget != EDsonFormulaTarget::BoneEndPoint)
                continue;

            // Parse node id: content between '#' and '?' in the output URL
            int32 HashIdx = INDEX_NONE;
            int32 QueryIdx = INDEX_NONE;
            if (!F.OutputUrl.FindChar(TEXT('#'), HashIdx) || !F.OutputUrl.FindChar(TEXT('?'), QueryIdx))
                continue;
            if (QueryIdx <= HashIdx + 1)
                continue;
            const FString NodeId = FDsonContentRoots::UrlDecode(
                F.OutputUrl.Mid(HashIdx + 1, QueryIdx - HashIdx - 1));
            if (NodeId.IsEmpty())
                continue;

            // Find node: figure DSF first (owns the rig data), then body DUF fallback
            int32 NodeIdx = (FigHandle != 0) ? FindNodeByIdLinear(FigHandle, NodeId) : INDEX_NONE;
            uint64_t NodeHandle = (NodeIdx != INDEX_NONE) ? FigHandle : 0;
            if (NodeIdx == INDEX_NONE)
            {
                NodeIdx = FindNodeByIdLinear(H, NodeId);
                if (NodeIdx != INDEX_NONE)
                    NodeHandle = H;
            }

            if (NodeIdx == INDEX_NONE)
            {
                UE_LOG(LogDsonImporter, Warning,
                    TEXT("[recipe] '%s': RigPoint node '%s' not found (output='%s'); skipped"),
                    *Settings.CharacterName, *NodeId, *F.OutputUrl);
                continue;
            }

            // R3: copy NodeName before any further parser calls
            const FString NodeName = FromUtf8(GDsonParser.GetNodeName
                ? GDsonParser.GetNodeName(NodeHandle, NodeIdx) : nullptr);
            if (EmittedNodeNames.Contains(NodeName))
                continue;
            EmittedNodeNames.Add(NodeName);

            FDsonNodeRigPoint RP;
            RP.NodeName = NodeName;
            RP.CenterX = static_cast<float>(GDsonParser.GetNodeCenterPointX ? GDsonParser.GetNodeCenterPointX(NodeHandle, NodeIdx) : 0.0);
            RP.CenterY = static_cast<float>(GDsonParser.GetNodeCenterPointY ? GDsonParser.GetNodeCenterPointY(NodeHandle, NodeIdx) : 0.0);
            RP.CenterZ = static_cast<float>(GDsonParser.GetNodeCenterPointZ ? GDsonParser.GetNodeCenterPointZ(NodeHandle, NodeIdx) : 0.0);
            RP.EndX    = static_cast<float>(GDsonParser.GetNodeEndPointX ? GDsonParser.GetNodeEndPointX(NodeHandle, NodeIdx) : 0.0);
            RP.EndY    = static_cast<float>(GDsonParser.GetNodeEndPointY ? GDsonParser.GetNodeEndPointY(NodeHandle, NodeIdx) : 0.0);
            RP.EndZ    = static_cast<float>(GDsonParser.GetNodeEndPointZ ? GDsonParser.GetNodeEndPointZ(NodeHandle, NodeIdx) : 0.0);
            Recipe->RigPoints.Add(MoveTemp(RP));
        }
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
    int32 DiagFMorphVal  = 0;
    int32 DiagFErcCenter = 0;
    int32 DiagFErcEnd    = 0;
    int32 DiagFOther     = 0;
    int32 DiagFBound     = 0;
    for (const FDsonFormula& F : Recipe->Formulas)
    {
        switch (F.OutputTarget)
        {
        case EDsonFormulaTarget::MorphValue:      ++DiagFMorphVal;  break;
        case EDsonFormulaTarget::BoneCenterPoint: ++DiagFErcCenter; break;
        case EDsonFormulaTarget::BoneEndPoint:    ++DiagFErcEnd;    break;
        default:                                  ++DiagFOther;     break;
        }
        if (!F.BoundMorphTargetName.IsEmpty())
            ++DiagFBound;
    }

    UE_LOG(LogDsonImporter, Log,
        TEXT("[recipe-shape] '%s': modifiers=%d non-default=%d correlated=%d uncorrelated=%d | LIE baked=%d raw=%d | companions=%d | formulas=%d (morphval=%d erc-center=%d erc-end=%d other=%d) bound=%d rigpoints=%d"),
        *Settings.CharacterName,
        DiagTotalModifiers, DiagNonDefault, DiagCorrelated, DiagUncorrelated,
        DiagBaked, DiagRaw,
        Recipe->CompanionSlots.Num(),
        Recipe->Formulas.Num(), DiagFMorphVal, DiagFErcCenter, DiagFErcEnd, DiagFOther,
        DiagFBound, Recipe->RigPoints.Num());

    UE_LOG(LogDsonImporter, Log,
        TEXT("[recipe] '%s': %d dial weight(s), %d formula(s), %d rigpoint(s), %d companion slot(s), %d LIE surface(s)"),
        *Settings.CharacterName,
        Recipe->DialWeights.Num(), Recipe->Formulas.Num(), Recipe->RigPoints.Num(),
        Recipe->CompanionSlots.Num(), Recipe->LieSurfaces.Num());

    if (!FDsonAssetUtils::SaveAssetPackage(Package, Recipe, PackagePath, TEXT("[recipe]")))
    {
        UE_LOG(LogDsonImporter, Error,
            TEXT("[recipe] '%s': failed to save recipe asset at '%s' -- downstream consumers will find no recipe for this character"),
            *Settings.CharacterName, *PackagePath);
    }
}
