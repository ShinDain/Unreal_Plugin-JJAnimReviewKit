#pragma once

#include "JJAnimReviewTypes.h"
#include "Serialization/Archive.h"

// Request-owned file lock. Only this layer mutates bundle files.
class FJJAnimReviewStore
{
public:
    static bool ResolveOutputRoot(const FString& Requested, FString& Output, FString& Error);
    bool Open(const FString& OutputRoot, const FString& Asset, FString& Error);
    bool OpenReadOnly(const FString& OutputRoot, const FString& Asset, FString& Error);
    bool ReadTakes(TArray<FJJReviewBundle>& Takes, FString& Error);
    bool ReadPose(const FJJReviewBundle& Bundle, FJJReviewPoseData& Pose, FString& Error) const;
    bool Save(const FJJReviewBundle& Bundle, const FJJReviewPoseData& Pose, FString& Error);
    void Prune(const TArray<FJJReviewBundle>& Takes, TArray<FString>& Warnings);
    FString GetTakeDirectory(const FString& Take) const;

private:
    bool Recover(const FString& Directory, FString& Error);
    bool IsOwnedTake(const FString& Directory) const;
    FString AssetDirectory;
    FString CanonicalAsset;
    TUniquePtr<FArchive> WriteLock;
    bool bReadOnly = true;
};
