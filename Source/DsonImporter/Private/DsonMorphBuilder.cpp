#include "DsonMorphBuilder.h"
#include "DsonImporter.h"
#include "DsonImportTypes.h"
#include "DsonParserFunctions.h"
#include "DsonImportUtils.h"
#include "DsonLoadedDocument.h"
#include "DsonContentRoots.h"

#include "MeshDescription.h"
#include "SkeletalMeshAttributes.h"
#include "Misc/Paths.h"

namespace
{
    struct FDsonMorphBuildStats
    {
        int32 Created = 0;
        int32 Deltas = 0;
        int32 SkippedOob = 0;
        int32 DuplicateNames = 0;
    };

    struct FDsonMorphDiscoveryStats
    {
        int32 DirectExternalFiles = 0;
        int32 FormulaExternalFiles = 0;
    };

    static FString NormalizedPathKey(const FString& Path)
    {
        FString Normalized = FPaths::ConvertRelativePathToFull(Path);
        FPaths::NormalizeFilename(Normalized);
        return Normalized.ToLower();
    }

    static bool HasRequiredMorphDeltaExports()
    {
        return GDsonParser.GetMorphDeltaCount &&
            GDsonParser.GetMorphDeltaVertexIndex &&
            GDsonParser.GetMorphDeltaX &&
            GDsonParser.GetMorphDeltaY &&
            GDsonParser.GetMorphDeltaZ;
    }

    static bool StripFormulaValueQuery(const FString& OutputUrl, FString& OutUrl)
    {
        int32 QueryIndex = INDEX_NONE;
        if (!OutputUrl.FindChar(TEXT('?'), QueryIndex))
            return false;

        if (!OutputUrl.Mid(QueryIndex + 1).Equals(TEXT("value"), ESearchCase::CaseSensitive))
            return false;

        OutUrl = OutputUrl.Left(QueryIndex);
        return !OutUrl.IsEmpty();
    }

    static bool EnqueueMorphFileUrl(
        const FString& Url,
        const TArray<FString>& Roots,
        TSet<FString>& SeenPathKeys,
        TArray<FString>& Worklist,
        int32& OutNewFileCount)
    {
        if (Url.IsEmpty())
            return false;

        const FString ResolvedPath = FDsonContentRoots::ResolveUrl(Url, Roots);
        if (ResolvedPath.IsEmpty())
            return false;

        const FString PathKey = NormalizedPathKey(ResolvedPath);
        if (SeenPathKeys.Contains(PathKey))
            return false;

        SeenPathKeys.Add(PathKey);
        Worklist.Add(ResolvedPath);
        ++OutNewFileCount;
        return true;
    }

    static void EnqueueFormulaOutput(
        const FString& OutputUrl,
        const TArray<FString>& Roots,
        TSet<FString>& SeenPathKeys,
        TArray<FString>& Worklist,
        FDsonMorphDiscoveryStats& DiscoveryStats)
    {
        FString FileUrl;
        if (!StripFormulaValueQuery(OutputUrl, FileUrl))
            return;

        EnqueueMorphFileUrl(
            FileUrl,
            Roots,
            SeenPathKeys,
            Worklist,
            DiscoveryStats.FormulaExternalFiles);
    }

    static void EnqueueSceneModifierFormulaOutputs(
        uint64_t SceneHandle,
        int32 SceneModifierIndex,
        const TArray<FString>& Roots,
        TSet<FString>& SeenPathKeys,
        TArray<FString>& Worklist,
        FDsonMorphDiscoveryStats& DiscoveryStats)
    {
        if (!GDsonParser.GetSceneModifierFormulaCount ||
            !GDsonParser.GetSceneModifierFormulaOutput)
        {
            return;
        }

        const int32 FormulaCount =
            GDsonParser.GetSceneModifierFormulaCount(SceneHandle, SceneModifierIndex);
        for (int32 FormulaIndex = 0; FormulaIndex < FormulaCount; ++FormulaIndex)
        {
            const FString OutputUrl = DsonImportUtils::FromUtf8(
                GDsonParser.GetSceneModifierFormulaOutput(
                    SceneHandle, SceneModifierIndex, FormulaIndex));
            EnqueueFormulaOutput(OutputUrl, Roots, SeenPathKeys, Worklist, DiscoveryStats);
        }
    }

    static void EnqueueModifierFormulaOutputs(
        uint64_t DsonHandle,
        const TArray<FString>& Roots,
        TSet<FString>& SeenPathKeys,
        TArray<FString>& Worklist,
        FDsonMorphDiscoveryStats& DiscoveryStats)
    {
        if (!GDsonParser.GetModifierCount ||
            !GDsonParser.GetModifierFormulaCount ||
            !GDsonParser.GetModifierFormulaOutput)
        {
            return;
        }

        const int32 ModifierCount = GDsonParser.GetModifierCount(DsonHandle);
        for (int32 ModifierIndex = 0; ModifierIndex < ModifierCount; ++ModifierIndex)
        {
            const int32 FormulaCount =
                GDsonParser.GetModifierFormulaCount(DsonHandle, ModifierIndex);
            for (int32 FormulaIndex = 0; FormulaIndex < FormulaCount; ++FormulaIndex)
            {
                const FString OutputUrl = DsonImportUtils::FromUtf8(
                    GDsonParser.GetModifierFormulaOutput(
                        DsonHandle, ModifierIndex, FormulaIndex));
                EnqueueFormulaOutput(OutputUrl, Roots, SeenPathKeys, Worklist, DiscoveryStats);
            }
        }
    }

    static void LoadFormulaReachableMorphDocuments(
        const FDsonImportSettings& Settings,
        TArray<FDsonLoadedDocument>& ExternalDocuments,
        TArray<uint64_t>& SourceHandles,
        FDsonMorphDiscoveryStats& DiscoveryStats)
    {
        if (!GDsonParser.GetSceneModifierCount || !GDsonParser.GetSceneModifierUrl)
            return;

        FDsonLoadedDocument SceneDocument;
        if (!SceneDocument.LoadFromFileAsWarning(Settings.DsonFilePath, TEXT("[morph] scene")))
            return;

        const TArray<FString> Roots = FDsonContentRoots::Detect();
        const uint64_t SceneHandle = SceneDocument.GetHandle64();
        const int32 ModifierCount = GDsonParser.GetSceneModifierCount(SceneHandle);
        TSet<FString> SeenPathKeys;
        SeenPathKeys.Add(NormalizedPathKey(Settings.ResolvedFigureDsfPath));

        TArray<FString> Worklist;

        for (int32 i = 0; i < ModifierCount; ++i)
        {
            const FString Url = DsonImportUtils::FromUtf8(
                GDsonParser.GetSceneModifierUrl(SceneHandle, i));

            EnqueueMorphFileUrl(
                Url,
                Roots,
                SeenPathKeys,
                Worklist,
                DiscoveryStats.DirectExternalFiles);
            EnqueueSceneModifierFormulaOutputs(
                SceneHandle, i, Roots, SeenPathKeys, Worklist, DiscoveryStats);
        }

        for (int32 WorklistIndex = 0; WorklistIndex < Worklist.Num(); ++WorklistIndex)
        {
            FDsonLoadedDocument Document;
            if (!Document.LoadFromFileAsWarning(Worklist[WorklistIndex], TEXT("[morph] external")))
                continue;

            SourceHandles.Add(Document.GetHandle64());
            EnqueueModifierFormulaOutputs(
                Document.GetHandle64(), Roots, SeenPathKeys, Worklist, DiscoveryStats);
            ExternalDocuments.Add(MoveTemp(Document));
        }
    }

    static bool IsVertexIndexValid(int32 VertexIdx, int32 NumBaseVertices)
    {
        return VertexIdx >= 0 && VertexIdx < NumBaseVertices;
    }

    static void AddPositionDeltas(
        uint64_t DsonHandle,
        int32 MorphIdx,
        int32 NumBaseVertices,
        TArray<TPair<int32, FVector3f>>& OutDeltas,
        int32& OutSkippedOob)
    {
        const double UnitScale = DsonImportUtils::ReadDazUnitScale(DsonHandle);
        const int32 DeltaCount = GDsonParser.GetMorphDeltaCount(DsonHandle, MorphIdx);

        for (int32 d = 0; d < DeltaCount; ++d)
        {
            const int32 VertexIdx = GDsonParser.GetMorphDeltaVertexIndex(DsonHandle, MorphIdx, d);
            if (!IsVertexIndexValid(VertexIdx, NumBaseVertices))
            {
                ++OutSkippedOob;
                continue;
            }

            OutDeltas.Add(TPair<int32, FVector3f>(
                VertexIdx,
                FVector3f(DsonImportUtils::DazPointToUe(
                    GDsonParser.GetMorphDeltaX(DsonHandle, MorphIdx, d),
                    GDsonParser.GetMorphDeltaY(DsonHandle, MorphIdx, d),
                    GDsonParser.GetMorphDeltaZ(DsonHandle, MorphIdx, d),
                    UnitScale))));
        }
    }

    static void RegisterMorphsFromDocument(
        uint64_t DsonHandle,
        FSkeletalMeshAttributes& SkelAttribs,
        const TArray<FVertexID>& VertexIDs,
        TSet<FString>& SeenMorphNames,
        FDsonMorphBuildStats& Stats)
    {
        const int32 NumBaseVertices = VertexIDs.Num();
        const int32 MorphCount = GDsonParser.GetMorphCount(DsonHandle);
        for (int32 m = 0; m < MorphCount; ++m)
        {
            const FString MorphName = DsonImportUtils::ReadMorphObjectName(DsonHandle, m);
            if (MorphName.IsEmpty())
                continue;

            const FString MorphNameKey = MorphName.ToLower();
            if (SeenMorphNames.Contains(MorphNameKey))
            {
                ++Stats.DuplicateNames;
                continue;
            }
            SeenMorphNames.Add(MorphNameKey);

            TArray<TPair<int32, FVector3f>> LocalDeltas;
            AddPositionDeltas(DsonHandle, m, NumBaseVertices, LocalDeltas, Stats.SkippedOob);
            if (LocalDeltas.IsEmpty())
                continue;

            const FName MorphFName(*MorphName);
            if (!SkelAttribs.RegisterMorphTargetAttribute(
                    MorphFName, /*bIncludeNormals=*/false))
                continue;

            TVertexAttributesRef<FVector3f> PositionDeltas =
                SkelAttribs.GetVertexMorphPositionDelta(MorphFName);

            for (const TPair<int32, FVector3f>& Delta : LocalDeltas)
                PositionDeltas.Set(VertexIDs[Delta.Key], Delta.Value);

            ++Stats.Created;
            Stats.Deltas += LocalDeltas.Num();
        }
    }
}

void FDsonMorphBuilder::Apply(
    const FDsonImportSettings& Settings,
    uint64_t FigureDsfHandle,
    FMeshDescription&,
    FSkeletalMeshAttributes& SkelAttribs,
    const TArray<FVertexID>& VertexIDs)
{
    if (!GDsonParser.GetMorphCount)
    {
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonMorphBuilder: optional morph parser exports are unavailable; skipping morph targets"));
        return;
    }

    if (!HasRequiredMorphDeltaExports())
    {
        UE_LOG(LogDsonImporter, Warning,
            TEXT("DsonMorphBuilder: required morph delta parser exports are unavailable; skipping morph targets"));
        return;
    }

    TArray<uint64_t> SourceHandles;
    SourceHandles.Add(FigureDsfHandle);

    TArray<FDsonLoadedDocument> ExternalDocuments;
    FDsonMorphDiscoveryStats DiscoveryStats;
    LoadFormulaReachableMorphDocuments(
        Settings, ExternalDocuments, SourceHandles, DiscoveryStats);

    TSet<FString> SeenMorphNames;
    FDsonMorphBuildStats Stats;
    for (const uint64_t SourceHandle : SourceHandles)
    {
        RegisterMorphsFromDocument(
            SourceHandle, SkelAttribs, VertexIDs, SeenMorphNames, Stats);
    }

    UE_LOG(LogDsonImporter, Log,
        TEXT("[morph] sources=%d created=%d deltas=%d skipped-oob=%d dup-names=%d external-files=%d direct-external=%d formula-external=%d"),
        SourceHandles.Num(),
        Stats.Created,
        Stats.Deltas,
        Stats.SkippedOob,
        Stats.DuplicateNames,
        ExternalDocuments.Num(),
        DiscoveryStats.DirectExternalFiles,
        DiscoveryStats.FormulaExternalFiles);
}
