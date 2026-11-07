#include "JJAnimReviewService.h"

#include "JJAnimReviewEngineCompat.h"
#include "JJAnimReviewModel.h"
#include "JJAnimReviewStore.h"
#include "Misc/DateTime.h"
#include "JJAnimReviewEditing.h"
#include "JJAnimReviewComparison.h"

bool JJAnimReviewService::ReadLatest(const FString& Asset, FJJReviewBundle& Bundle,
    TArray<FJJReviewBundle>& Takes, FString& Error)
{
    if (!IsInGameThread())
    {
        Error = TEXT("Game thread required");
        return false;
    }
    FString Canonical;
    FString Root;
    FJJAnimReviewStore Store;
    if (!JJAnimReviewEngineCompat::NormalizeAssetPath(Asset, Canonical, Error)
        || !FJJAnimReviewStore::ResolveOutputRoot(TEXT(""), Root, Error)
        || !Store.OpenReadOnly(Root, Canonical, Error) || !Store.ReadTakes(Takes, Error))
    {
        return false;
    }
    if (Takes.IsEmpty())
    {
        Error = TEXT("Create a review bundle first (default output folder).");
        return false;
    }
    Bundle = Takes.Last();
    return true;
}

bool JJAnimReviewService::ReadTakePose(const FString& Asset, const FString& Take,
    FJJReviewPoseData& Pose, FString& Error)
{
    FJJReviewBundle Latest;
    TArray<FJJReviewBundle> Takes;
    if (!ReadLatest(Asset, Latest, Takes, Error))
    {
        return false;
    }

    const FJJReviewBundle* Found = Takes.FindByPredicate([&Take](const FJJReviewBundle& Candidate)
    {
        return Candidate.Take == Take;
    });
    if (!Found)
    {
        Error = TEXT("Take not found: ") + Take;
        return false;
    }

    FString Canonical;
    FString Root;
    FJJAnimReviewStore Store;
    if (!JJAnimReviewEngineCompat::NormalizeAssetPath(Asset, Canonical, Error)
        || !FJJAnimReviewStore::ResolveOutputRoot(TEXT(""), Root, Error)
        || !Store.OpenReadOnly(Root, Canonical, Error))
    {
        return false;
    }

    return Store.ReadPose(*Found, Pose, Error);
}

bool JJAnimReviewService::EditAnnotation(const FString& Asset, const FString& ExpectedContentId,
    FJJReviewAnnotation Annotation, const FString& Operation, FString& Error)
{
    if (!IsInGameThread())
    {
        Error = TEXT("Game thread required");
        return false;
    }
    FString Canonical;
    FString Root;
    FJJAnimReviewStore Store;
    TArray<FJJReviewBundle> Takes;
    if (!JJAnimReviewEngineCompat::NormalizeAssetPath(Asset, Canonical, Error)
        || !FJJAnimReviewStore::ResolveOutputRoot(TEXT(""), Root, Error)
        || !Store.Open(Root, Canonical, Error) || !Store.ReadTakes(Takes, Error))
    {
        return false;
    }
    if (Takes.IsEmpty() || Takes.Last().ContentId != ExpectedContentId)
    {
        Error = TEXT("Review changed; refresh the annotation tab.");
        return false;
    }
    FJJReviewBundle Bundle = Takes.Last();
    FJJReviewPoseData Pose;
    if (!Store.ReadPose(Bundle, Pose, Error))
    {
        return false;
    }
    // A2 sampler revalidates unsaved edits before attaching notes to a content version.
    UAnimSequence* Animation = JJAnimReviewEngineCompat::LoadAnimation(Canonical, Error);
    TArray<double> Times;
    FJJReviewPoseData Current;
    FString Content;
    if (!Animation || !JJAnimReviewEngineCompat::GetSourceTimes(Animation, Times, Error)
        || !JJAnimReviewEngineCompat::Sample(Animation, Times, Current, Error)
        || !JJAnimReviewModel::ComputeContentId(Current, Content, Error))
    {
        return false;
    }
    if (Content != ExpectedContentId)
    {
        Error = TEXT("Animation content changed. Create a review bundle, then refresh the tab.");
        return false;
    }
    if (!Pose.Samples.IsEmpty() && Annotation.Frame > Pose.Samples.Last().SourceFrame)
    {
        Error = TEXT("Annotation frame exceeds the clip.");
        return false;
    }
    if (!Annotation.Bone.IsNone() && !Pose.BoneNames.Contains(Annotation.Bone))
    {
        Error = TEXT("Annotation references a missing bone.");
        return false;
    }
    if (!JJAnimReviewEditing::Apply(Bundle, Annotation, Operation, Error))
    {
        return false;
    }
    Bundle.UpdatedUtc = FDateTime::UtcNow().ToIso8601();
    if (!Store.Save(Bundle, Pose, Error))
    {
        return false;
    }
    Takes.Last() = Bundle;
    TArray<FString> Warnings;
    Store.Prune(Takes, Warnings);
    return true;
}

FJJAnimReviewResponse JJAnimReviewService::Run(const FJJAnimReviewRequest& Request)
{
    FJJAnimReviewResponse Response;
    if (!IsInGameThread())
    {
        Response.Error = TEXT("JJAnimReview must be called on the game thread");
        return Response;
    }
    FString Asset;
    FString Profile;
    FString OutputRoot;
    if (!JJAnimReviewEngineCompat::NormalizeAssetPath(Request.Asset, Asset, Response.Error)
        || (!Request.Profile.IsEmpty() && !JJAnimReviewEngineCompat::NormalizeAssetPath(Request.Profile, Profile, Response.Error))
        || !FJJAnimReviewStore::ResolveOutputRoot(Request.Output, OutputRoot, Response.Error))
    {
        return Response;
    }

    FJJAnimReviewStore Store;
    if (!Store.Open(OutputRoot, Asset, Response.Error))
    {
        return Response;
    }
    TArray<FJJReviewBundle> Takes;
    if (!Store.ReadTakes(Takes, Response.Error))
    {
        return Response;
    }
    UAnimSequence* Animation = JJAnimReviewEngineCompat::LoadAnimation(Asset, Response.Error);
    if (!Animation)
    {
        return Response;
    }
    FJJReviewBundle Bundle;
    Bundle.Asset = Asset;
    Bundle.Profile = Profile;
    if (!JJAnimReviewEngineCompat::ResolveProfile(Animation, Profile, Bundle.ProfileSettings,
        Bundle.bEstimatedProfile, Response.Error))
    {
        return Response;
    }
    TArray<double> Times;
    FJJReviewPoseData Pose;
    if (!JJAnimReviewEngineCompat::GetSourceTimes(Animation, Times, Response.Error)
        || !JJAnimReviewEngineCompat::Sample(Animation, Times, Pose, Response.Error)
        || !JJAnimReviewModel::ComputeContentId(Pose, Bundle.ContentId, Response.Error))
    {
        return Response;
    }
    Pose.ContentId = Bundle.ContentId;
    JJAnimReviewModel::SelectTake(Bundle, Takes.IsEmpty() ? nullptr : &Takes.Last(), FDateTime::UtcNow().ToIso8601());
    FJJReviewCheckContext Context = JJAnimReviewEngineCompat::CheckContext(Animation, Bundle.ProfileSettings);
    Context.bEstimatedProfile = Bundle.bEstimatedProfile;
    Bundle.Checks = JJAnimReviewChecks::Evaluate(Pose, Bundle.ProfileSettings, Context);
    Bundle.Warnings.Append(Context.Warnings);
    Bundle.CheckStatus = TEXT("passed");
    for (const FJJReviewCheckResult& Result : Bundle.Checks)
    {
        if (Result.Status == TEXT("failed"))
        {
            Bundle.CheckStatus = TEXT("failed");
            break;
        }
        if (Result.Status == TEXT("warning"))
        {
            Bundle.CheckStatus = TEXT("warning");
        }
    }
    if (Bundle.bEstimatedProfile)
    {
        Bundle.Warnings.Add(TEXT("Estimated profile: verify foot/penetration bone names, ground height and thresholds."));
    }
    Bundle.PoseDifferences.Reset();
    const int32 PreviousIndex = Takes.Num() - (Takes.Num() > 0 && Takes.Last().Take == Bundle.Take ? 2 : 1);
    if (Takes.IsValidIndex(PreviousIndex))
    {
        const FJJReviewBundle& Previous = Takes[PreviousIndex];
        if (Previous.ContentId.StartsWith(TEXT("pose_md5_v1:")))
        {
            Bundle.Warnings.Add(TEXT("Previous take uses legacy v1 root/hash policy: comparison and ghost skipped; original history retained. Create two v2 takes to compare."));
        }
        else
        {
            FJJReviewPoseData PreviousPose;
            FString ComparisonError;
            if (!Store.ReadPose(Previous, PreviousPose, ComparisonError))
            {
                Bundle.Warnings.Add(TEXT("Comparison skipped: ") + ComparisonError);
            }
            else
            {
                FJJReviewComparison Comparison = JJAnimReviewComparison::Compare(Pose, PreviousPose);
                Bundle.PoseDifferences = MoveTemp(Comparison.Maxima);
                Bundle.Checks.Append(Comparison.Ranges);
                Bundle.Warnings.Append(Comparison.Warnings);
                Bundle.Warnings.Add(TEXT("Compared with ") + Previous.Take + TEXT(" in component space; includes root motion. Maxima may occur at different frames."));
            }
        }
    }
    if (!Store.Save(Bundle, Pose, Response.Error))
    {
        return Response;
    }
    if (Request.bCapture)
    {
        FString Reason;
        const FString Filename = Store.GetTakeDirectory(Bundle.Take) / TEXT("sheet.png");
        if (JJAnimReviewEngineCompat::CaptureSheet(Animation, Pose, Request.Capture, Filename, Reason))
        {
            Bundle.Images.AddUnique(TEXT("sheet.png"));
        }
        else
        {
            Bundle.Warnings.Add(Reason);
        }
        if (!Store.Save(Bundle, Pose, Response.Error))
        {
            return Response;
        }
    }

    if (!Takes.IsEmpty() && Takes.Last().Take == Bundle.Take)
    {
        Takes.Last() = Bundle;
    }
    else
    {
        Takes.Add(Bundle);
    }
    Store.Prune(Takes, Response.Warnings);
    Response.Warnings.Append(Bundle.Warnings);
    Response.TakeDirectory = Store.GetTakeDirectory(Bundle.Take);
    Response.ExitCode = Bundle.CheckStatus == TEXT("failed") ? 1 : 0;
    return Response;
}
