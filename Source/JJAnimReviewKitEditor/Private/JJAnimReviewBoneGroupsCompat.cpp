#include "JJAnimReviewEngineCompat.h"

#include "JJAnimReviewModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace JJAnimReviewBoneGroupsCompatPrivate
{
    FString GroupFile(UAnimSequence* Animation)
    {
        // A19: plugin-relative team data; no path derived from the project layout.
        const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("JJAnimReviewKit"));
        if (!Plugin || !Animation || !Animation->GetSkeleton())
        {
            return FString();
        }
        return Plugin->GetBaseDir() / TEXT("BoneGroups")
            / (JJAnimReviewModel::AssetDirectoryKey(Animation->GetSkeleton()->GetPathName()) + TEXT(".json"));
    }
}

void JJAnimReviewEngineCompat::SkeletonBones(UAnimSequence* Animation, TArray<FName>& Names, TArray<int32>& Parents)
{
    check(IsInGameThread());
    Names.Reset();
    Parents.Reset();
    if (!Animation || !Animation->GetSkeleton())
    {
        return;
    }
    const FReferenceSkeleton& Skeleton = Animation->GetSkeleton()->GetReferenceSkeleton();
    for (int32 Index = 0; Index < Skeleton.GetNum(); ++Index)
    {
        Names.Add(Skeleton.GetBoneName(Index));
        Parents.Add(Skeleton.GetParentIndex(Index));
    }
}

bool JJAnimReviewEngineCompat::LoadBoneGroups(UAnimSequence* Animation, TArray<FJJReviewBoneGroup>& User,
    TArray<FJJReviewBoneGroup>& Resolved, bool& bTemplateMatched, FString& Error)
{
    check(IsInGameThread());
    User.Reset();
    Resolved.Reset();
    bTemplateMatched = false;
    const FString Filename = JJAnimReviewBoneGroupsCompatPrivate::GroupFile(Animation);
    if (Filename.IsEmpty())
    {
        Error = TEXT("No skeleton/plugin for bone groups.");
        return false;
    }
    TArray<FName> Names;
    TArray<int32> Parents;
    SkeletonBones(Animation, Names, Parents);
    const auto Template = JJAnimReviewNotes::MannequinTemplate(Names, Parents, bTemplateMatched);
    if (IFileManager::Get().FileExists(*Filename))
    {
        FString Json;
        if (!FFileHelper::LoadFileToString(Json, *Filename)
            || !JJAnimReviewNotes::GroupsFromJson(Json, Animation->GetSkeleton()->GetPathName(), User, Error))
        {
            Error = TEXT("Cannot load bone groups: ") + Filename + TEXT(" ") + Error;
            return false;
        }
    }
    Resolved = JJAnimReviewNotes::ResolveGroups(Names, Template, User);
    return true;
}

bool JJAnimReviewEngineCompat::SaveBoneGroups(UAnimSequence* Animation, const TArray<FJJReviewBoneGroup>& User, FString& Error)
{
    check(IsInGameThread());
    const FString Filename = JJAnimReviewBoneGroupsCompatPrivate::GroupFile(Animation);
    if (Filename.IsEmpty())
    {
        Error = TEXT("No skeleton/plugin for bone groups.");
        return false;
    }
    // Check existing identity before overwriting a short-hash collision or corrupt file.
    if (IFileManager::Get().FileExists(*Filename))
    {
        FString Existing;
        TArray<FJJReviewBoneGroup> Checked;
        if (!FFileHelper::LoadFileToString(Existing, *Filename)
            || !JJAnimReviewNotes::GroupsFromJson(Existing, Animation->GetSkeleton()->GetPathName(), Checked, Error))
        {
            return false;
        }
    }
    FString Json;
    if (!JJAnimReviewNotes::GroupsToJson(Animation->GetSkeleton()->GetPathName(), User, Json, Error))
    {
        return false;
    }
    const FString Staged = Filename + TEXT(".next");
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true)
        || !FFileHelper::SaveStringToFile(Json, *Staged, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
        || !IFileManager::Get().Move(*Filename, *Staged, true, false))
    {
        Error = TEXT("Cannot save bone groups: ") + Filename;
        return false;
    }
    return true;
}
