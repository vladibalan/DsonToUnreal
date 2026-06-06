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
#include "ObjectTools.h"

namespace
{
    struct FDsonMorphBuildStats
    {
        int32 Created = 0;
        int32 Deltas = 0;
        int32 SkippedOob = 0;
        int32 DuplicateNames = 0;
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

    static void CollectExternalMorphPaths(
        const FDsonImportSettings& Settings,
        TSet<FString>& OutPaths)
    {
        if (!GDsonParser.GetSceneModifierCount || !GDsonParser.GetSceneModifierUrl)
            return;

        FDsonLoadedDocument SceneDocument;
        if (!SceneDocument.LoadFromFileAsWarning(Settings.DsonFilePath, TEXT("[morph] scene")))
            return;

        const TArray<FString> Roots = FDsonContentRoots::Detect();
        const uint64_t SceneHandle = SceneDocument.GetHandle64();
        const int32 ModifierCount = GDsonParser.GetSceneModifierCount(SceneHandle);
        const FString FigurePathKey = NormalizedPathKey(Settings.ResolvedFigureDsfPath);
        TSet<FString> SeenPathKeys;
        SeenPathKeys.Add(FigurePathKey);

        for (int32 i = 0; i < ModifierCount; ++i)
        {
            const FString Url = DsonImportUtils::FromUtf8(
                GDsonParser.GetSceneModifierUrl(SceneHandle, i));
            if (Url.IsEmpty())
                continue;

            const FString ResolvedPath = FDsonContentRoots::ResolveUrl(Url, Roots);
            if (ResolvedPath.IsEmpty())
                continue;

            const FString PathKey = NormalizedPathKey(ResolvedPath);
            if (SeenPathKeys.Contains(PathKey))
                continue;

            SeenPathKeys.Add(PathKey);
            OutPaths.Add(ResolvedPath);
        }
    }

    static FString ReadMorphObjectName(uint64_t DsonHandle, int32 MorphIdx)
    {
        FString Name = GDsonParser.GetMorphName
            ? DsonImportUtils::FromUtf8(GDsonParser.GetMorphName(DsonHandle, MorphIdx))
            : FString();
        if (Name.IsEmpty() && GDsonParser.GetMorphLabel)
        {
            Name = DsonImportUtils::FromUtf8(
                GDsonParser.GetMorphLabel(DsonHandle, MorphIdx));
        }

        if (Name.IsEmpty())
            return FString();

        const FString SanitizedName = ObjectTools::SanitizeObjectName(Name);
        return SanitizedName.IsEmpty() ? FString() : SanitizedName;
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
            const FString MorphName = ReadMorphObjectName(DsonHandle, m);
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

    TSet<FString> ExternalMorphPaths;
    CollectExternalMorphPaths(Settings, ExternalMorphPaths);

    TArray<FDsonLoadedDocument> ExternalDocuments;
    ExternalDocuments.Reserve(ExternalMorphPaths.Num());
    for (const FString& Path : ExternalMorphPaths)
    {
        FDsonLoadedDocument Document;
        if (!Document.LoadFromFileAsWarning(Path, TEXT("[morph] external")))
            continue;

        SourceHandles.Add(Document.GetHandle64());
        ExternalDocuments.Add(MoveTemp(Document));
    }

    TSet<FString> SeenMorphNames;
    FDsonMorphBuildStats Stats;
    for (const uint64_t SourceHandle : SourceHandles)
    {
        RegisterMorphsFromDocument(
            SourceHandle, SkelAttribs, VertexIDs, SeenMorphNames, Stats);
    }

    UE_LOG(LogDsonImporter, Log,
        TEXT("[morph] sources=%d created=%d deltas=%d skipped-oob=%d dup-names=%d external-files=%d"),
        SourceHandles.Num(),
        Stats.Created,
        Stats.Deltas,
        Stats.SkippedOob,
        Stats.DuplicateNames,
        ExternalDocuments.Num());
}
