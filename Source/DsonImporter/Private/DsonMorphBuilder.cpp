#include "DsonMorphBuilder.h"
#include "DsonImporter.h"
#include "DsonImportTypes.h"
#include "DsonParserFunctions.h"
#include "DsonImportUtils.h"
#include "DsonLoadedDocument.h"
#include "DsonContentRoots.h"

#include "MeshDescription.h"
#include "SkeletalMeshAttributes.h"
#include "HAL/FileManager.h"
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
        int32 CorrectiveFiles = 0;
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

    struct FCorrectiveCandidate
    {
        FString Path;
        TArray<FString> GateIds;  // lower-cased mult-stage push-url fragments
    };

    // Extracts the URL-decoded #fragment (minus ?query) from a DSON URL.
    // Returns empty string when no '#' is present.
    static FString ExtractUrlFragmentId(const FString& Url)
    {
        int32 HashIdx = INDEX_NONE;
        if (!Url.FindChar(TEXT('#'), HashIdx))
            return FString();
        const FString Fragment = Url.Mid(HashIdx + 1);
        int32 QueryIdx = INDEX_NONE;
        if (Fragment.FindChar(TEXT('?'), QueryIdx))
            return FDsonContentRoots::UrlDecode(Fragment.Left(QueryIdx));
        return FDsonContentRoots::UrlDecode(Fragment);
    }

    // Returns true iff the formula output URL refers to this modifier's own ?value channel.
    static bool IsModifierSelfValueOutput(const FString& OutputUrl, const FString& ModifierId)
    {
        if (ModifierId.IsEmpty() || !OutputUrl.EndsWith(TEXT("?value"), ESearchCase::CaseSensitive))
            return false;
        return ExtractUrlFragmentId(OutputUrl).Equals(ModifierId, ESearchCase::CaseSensitive);
    }

    // Opens each accepted corrective path (not already in SeenPathKeys) and adds it to the
    // document and handle arrays. Skips duplicates and failed opens (R7 permissive).
    static void OpenCorrectiveFiles(
        const TArray<FString>& Paths,
        TSet<FString>& SeenPathKeys,
        TArray<FDsonLoadedDocument>& ExternalDocuments,
        TArray<uint64_t>& SourceHandles,
        FDsonMorphDiscoveryStats& DiscoveryStats)
    {
        for (const FString& Path : Paths)
        {
            const FString PathKey = NormalizedPathKey(Path);
            if (SeenPathKeys.Contains(PathKey))
                continue;
            SeenPathKeys.Add(PathKey);
            FDsonLoadedDocument Doc;
            if (!Doc.LoadFromFileAsWarning(Path, TEXT("[morph] corrective")))
                continue;
            SourceHandles.Add(Doc.GetHandle64());
            ExternalDocuments.Add(MoveTemp(Doc));
            ++DiscoveryStats.CorrectiveFiles;
        }
    }

    // Scans the figure's Morphs/ subtree for scene-gated joint corrective morphs and adds
    // accepted files to ExternalDocuments/SourceHandles. Two phases: (1) open all *.dsf to build
    // modifier_id->channel.value map and identify corrective candidates (self-?value formula
    // output + deltas), then (2) gate-test each candidate against ReachableModifierIds and the
    // channel-value map. M1: caches accepted paths in Settings.DiscoveredCorrectiveDsfPaths so
    // a second call (recipe builder) skips the scan entirely (R3: holds only accepted handles).
    static void ScanAndEnqueueCorrectives(
        const FDsonImportSettings& Settings,
        TSet<FString>& SeenPathKeys,
        const TSet<FString>& ReachableModifierIds,
        TArray<FDsonLoadedDocument>& ExternalDocuments,
        TArray<uint64_t>& SourceHandles,
        FDsonMorphDiscoveryStats& DiscoveryStats)
    {
        const FString MorphsDir =
            FPaths::GetPath(Settings.ResolvedFigureDsfPath) / TEXT("Morphs");

        // M1 cache hit: accepted paths already known from Apply; just open them
        if (!Settings.DiscoveredCorrectiveDsfPaths.IsEmpty())
        {
            OpenCorrectiveFiles(
                Settings.DiscoveredCorrectiveDsfPaths,
                SeenPathKeys, ExternalDocuments, SourceHandles, DiscoveryStats);
            return;
        }

        if (!IFileManager::Get().DirectoryExists(*MorphsDir))
            return;

        if (!GDsonParser.GetModifierChannelValue)
        {
            UE_LOG(LogDsonImporter, Warning,
                TEXT("[morph] GetModifierChannelValue unavailable; corrective static-gate "
                     "values default to 0 -- base correctives may be excluded"));
        }

        // Phase 1: enumerate all *.dsf, collect modifier_id->channel.value and identify
        // corrective candidates (self-?value formula output + morph deltas).
        TArray<FString> AllDsfPaths;
        IFileManager::Get().FindFilesRecursive(
            AllDsfPaths, *MorphsDir, TEXT("*.dsf"), /*Files=*/true, /*Dirs=*/false);

        TMap<FString, double> ChannelValueMap;
        TArray<FCorrectiveCandidate> Candidates;
        const double ScanStart = FPlatformTime::Seconds();

        for (const FString& DsfPath : AllDsfPaths)
        {
            if (SeenPathKeys.Contains(NormalizedPathKey(DsfPath)))
                continue;

            FDsonLoadedDocument Doc;
            if (!Doc.LoadFromFileAsWarning(DsfPath, TEXT("[morph-scan]")))
                continue;
            const uint64_t H = Doc.GetHandle64();

            // Build morph-id -> morph-index map for delta check
            TMap<FString, int32> MorphIdxMap;
            if (GDsonParser.GetMorphCount && GDsonParser.GetMorphId)
            {
                const int32 MorphCount = GDsonParser.GetMorphCount(H);
                for (int32 mk = 0; mk < MorphCount; ++mk)
                {
                    const FString MId =
                        DsonImportUtils::FromUtf8(GDsonParser.GetMorphId(H, mk));
                    if (!MId.IsEmpty())
                        MorphIdxMap.Add(MId, mk);
                }
            }

            const bool bHaveModIds =
                GDsonParser.GetModifierCount && GDsonParser.GetModifierId;
            const int32 ModCount = bHaveModIds ? GDsonParser.GetModifierCount(H) : 0;

            bool bFileHasCorrective = false;
            TArray<FString> FileGateIds;

            for (int32 mi = 0; mi < ModCount; ++mi)
            {
                const FString ModId =
                    DsonImportUtils::FromUtf8(GDsonParser.GetModifierId(H, mi));
                if (ModId.IsEmpty())
                    continue;

                // Collect channel.value for the gate test (needed for all modifiers,
                // since a gate control in another file's formula may reference this one)
                if (GDsonParser.GetModifierChannelValue)
                    ChannelValueMap.FindOrAdd(ModId.ToLower()) =
                        GDsonParser.GetModifierChannelValue(H, mi);

                // Check corrective signature: self-?value formula output
                if (!GDsonParser.GetModifierFormulaCount ||
                    !GDsonParser.GetModifierFormulaOutput)
                    continue;

                const int32 FCount = GDsonParser.GetModifierFormulaCount(H, mi);
                bool bSelfValue = false;
                for (int32 fi = 0; fi < FCount && !bSelfValue; ++fi)
                {
                    const FString OutUrl = DsonImportUtils::FromUtf8(
                        GDsonParser.GetModifierFormulaOutput(H, mi, fi));
                    if (IsModifierSelfValueOutput(OutUrl, ModId))
                        bSelfValue = true;
                }
                if (!bSelfValue)
                    continue;

                // Check morph deltas (corrective must actually move vertices)
                const int32* MorphIdx = MorphIdxMap.Find(ModId);
                if (!MorphIdx || !GDsonParser.GetMorphDeltaCount ||
                    GDsonParser.GetMorphDeltaCount(H, *MorphIdx) == 0)
                    continue;

                // Confirmed corrective: collect mult-stage gate control IDs
                bFileHasCorrective = true;
                if (GDsonParser.GetModifierFormulaStage &&
                    GDsonParser.GetModifierFormulaOperationCount &&
                    GDsonParser.GetModifierFormulaOperationOp &&
                    GDsonParser.GetModifierFormulaOperationUrl)
                {
                    for (int32 fi = 0; fi < FCount; ++fi)
                    {
                        const FString Stage = DsonImportUtils::FromUtf8(
                            GDsonParser.GetModifierFormulaStage(H, mi, fi));
                        if (!Stage.Equals(TEXT("mult"), ESearchCase::CaseSensitive))
                            continue;
                        const int32 OpCount =
                            GDsonParser.GetModifierFormulaOperationCount(H, mi, fi);
                        for (int32 oi = 0; oi < OpCount; ++oi)
                        {
                            const FString Op = DsonImportUtils::FromUtf8(
                                GDsonParser.GetModifierFormulaOperationOp(H, mi, fi, oi));
                            if (!Op.Equals(TEXT("push"), ESearchCase::CaseSensitive))
                                continue;
                            const FString PushUrl = DsonImportUtils::FromUtf8(
                                GDsonParser.GetModifierFormulaOperationUrl(H, mi, fi, oi));
                            const FString GateId =
                                ExtractUrlFragmentId(PushUrl).ToLower();
                            if (!GateId.IsEmpty())
                                FileGateIds.AddUnique(GateId);
                        }
                    }
                }
            }

            if (bFileHasCorrective)
            {
                FCorrectiveCandidate Cand;
                Cand.Path = DsfPath;
                Cand.GateIds = MoveTemp(FileGateIds);
                Candidates.Add(MoveTemp(Cand));
            }
            // Doc closes (RAII); accepted files are re-opened in phase 3
        }

        UE_LOG(LogDsonImporter, Log,
            TEXT("[morph] corrective scan: %d files in %.2f s, %d candidates"),
            AllDsfPaths.Num(), FPlatformTime::Seconds() - ScanStart, Candidates.Num());

        // Phase 2: gate test with the complete ChannelValueMap.
        // Include a corrective iff every mult-stage gate control is either
        // (a) in the scene-reachable modifier set, or (b) has channel.value > 0 (statically on).
        TArray<FString> AcceptedPaths;
        for (const FCorrectiveCandidate& Cand : Candidates)
        {
            bool bPass = true;
            for (const FString& GateId : Cand.GateIds)
            {
                if (ReachableModifierIds.Contains(GateId))
                    continue;
                const double* Val = ChannelValueMap.Find(GateId);
                if (Val && *Val > 0.0)
                    continue;
                bPass = false;
                break;
            }
            if (bPass)
                AcceptedPaths.Add(Cand.Path);
        }

        // M1: cache accepted paths so the recipe builder's second call skips the scan
        Settings.DiscoveredCorrectiveDsfPaths = AcceptedPaths;

        // Phase 3: open accepted corrective files and add to the reachable set
        OpenCorrectiveFiles(
            AcceptedPaths, SeenPathKeys, ExternalDocuments, SourceHandles, DiscoveryStats);

        UE_LOG(LogDsonImporter, Log,
            TEXT("[morph] corrective gate: %d accepted of %d candidates"),
            DiscoveryStats.CorrectiveFiles, Candidates.Num());
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
        TSet<FString> ReachableModifierIds;

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

            if (GDsonParser.GetSceneModifierId)
            {
                const FString ModId = DsonImportUtils::FromUtf8(
                    GDsonParser.GetSceneModifierId(SceneHandle, i));
                if (!ModId.IsEmpty())
                    ReachableModifierIds.Add(ModId.ToLower());
            }
        }

        for (int32 WorklistIndex = 0; WorklistIndex < Worklist.Num(); ++WorklistIndex)
        {
            FDsonLoadedDocument Document;
            if (!Document.LoadFromFileAsWarning(Worklist[WorklistIndex], TEXT("[morph] external")))
                continue;

            const uint64_t DocHandle = Document.GetHandle64();
            SourceHandles.Add(DocHandle);

            if (GDsonParser.GetModifierCount && GDsonParser.GetModifierId)
            {
                const int32 DocModCount = GDsonParser.GetModifierCount(DocHandle);
                for (int32 mi = 0; mi < DocModCount; ++mi)
                {
                    const FString ModId = DsonImportUtils::FromUtf8(
                        GDsonParser.GetModifierId(DocHandle, mi));
                    if (!ModId.IsEmpty())
                        ReachableModifierIds.Add(ModId.ToLower());
                }
            }

            EnqueueModifierFormulaOutputs(
                DocHandle, Roots, SeenPathKeys, Worklist, DiscoveryStats);
            ExternalDocuments.Add(MoveTemp(Document));
        }

        if (!Settings.ResolvedFigureDsfPath.IsEmpty())
        {
            ScanAndEnqueueCorrectives(
                Settings, SeenPathKeys, ReachableModifierIds,
                ExternalDocuments, SourceHandles, DiscoveryStats);
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

void FDsonMorphBuilder::DiscoverFormulaReachableDocuments(
    const FDsonImportSettings& Settings,
    TArray<FDsonLoadedDocument>& OutDocs,
    TArray<uint64_t>& OutHandles)
{
    FDsonMorphDiscoveryStats DiscoveryStats;
    LoadFormulaReachableMorphDocuments(Settings, OutDocs, OutHandles, DiscoveryStats);
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
        TEXT("[morph] sources=%d created=%d deltas=%d skipped-oob=%d dup-names=%d external-files=%d direct-external=%d formula-external=%d corrective-files=%d"),
        SourceHandles.Num(),
        Stats.Created,
        Stats.Deltas,
        Stats.SkippedOob,
        Stats.DuplicateNames,
        ExternalDocuments.Num(),
        DiscoveryStats.DirectExternalFiles,
        DiscoveryStats.FormulaExternalFiles,
        DiscoveryStats.CorrectiveFiles);
}
