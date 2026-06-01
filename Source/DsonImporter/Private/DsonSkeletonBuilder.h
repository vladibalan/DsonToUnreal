#pragma once
#include "CoreMinimal.h"

struct FDsonImportSettings;
class USkeleton;

class FDsonSkeletonBuilder
{
public:
    // Returns the created USkeleton on success, nullptr on failure.
    // Logs all errors via UE_LOG(LogDsonImporter, Error, ...).
    static USkeleton* Build(const FDsonImportSettings& Settings);

private:
    static bool LoadDsfDocument(const FString& Path, uint64_t& OutHandle);
    static void BuildReferenceSkeletonFromDsf(uint64_t DsfHandle, FReferenceSkeleton& OutRefSkeleton);
    static FTransform MakeBoneTransform(uint64_t DsfHandle, int32 NodeIndex, double UnitScale);
    static USkeleton* CreateSkeletonAsset(const FReferenceSkeleton& RefSkeleton, const FString& AssetName);
};