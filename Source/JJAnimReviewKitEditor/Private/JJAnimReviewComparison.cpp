#include "JJAnimReviewComparison.h"

#include "JJAnimReviewModel.h"

FTransform JJAnimReviewComparison::AtTime(const FJJReviewPoseData& Pose, int32 Bone, double Time)
{
    if (Pose.Samples.IsEmpty() || !Pose.BoneNames.IsValidIndex(Bone))
    {
        return FTransform::Identity;
    }
    int32 Low = 0;
    int32 High = Pose.Samples.Num() - 1;
    while (Low < High)
    {
        const int32 Mid = (Low + High) / 2;
        if (Pose.Samples[Mid].TimeSeconds < Time)
        {
            Low = Mid + 1;
        }
        else
        {
            High = Mid;
        }
    }
    const int32 A = FMath::Max(0, Low - 1);
    const auto& First = Pose.Samples[A];
    const auto& Last = Pose.Samples[Low];
    if (!First.ComponentTransforms.IsValidIndex(Bone) || !Last.ComponentTransforms.IsValidIndex(Bone))
    {
        return FTransform::Identity;
    }
    const double Dt = Last.TimeSeconds - First.TimeSeconds;
    const double Alpha = Dt > 0 ? FMath::Clamp((Time - First.TimeSeconds) / Dt, 0.0, 1.0) : 0;
    FTransform Result;
    Result.Blend(First.ComponentTransforms[Bone].ToTransform(), Last.ComponentTransforms[Bone].ToTransform(), Alpha);
    return Result;
}

FJJReviewComparison JJAnimReviewComparison::Compare(const FJJReviewPoseData& Current,
    const FJJReviewPoseData& Previous, int32 TopCount, double TranslationThreshold, double RotationThreshold)
{
    FJJReviewComparison Result;
    FString Id;
    FString Error;
    if (!JJAnimReviewModel::ComputeContentId(Current, Id, Error)
        || !JJAnimReviewModel::ComputeContentId(Previous, Id, Error))
    {
        Result.Warnings.Add(TEXT("Comparison input invalid: ") + Error);
        return Result;
    }
    if (Current.BoneNames != Previous.BoneNames || Current.ParentIndices != Previous.ParentIndices)
    {
        Result.Warnings.Add(TEXT("Comparison skipped: bone names/order/hierarchy changed."));
        return Result;
    }
    if (!FMath::IsNearlyEqual(Current.DurationSeconds, Previous.DurationSeconds, 1.e-6))
    {
        Result.Warnings.Add(TEXT("Take durations differ; comparison uses overlapping absolute time only."));
    }
    const double Duration = FMath::Min(Current.DurationSeconds, Previous.DurationSeconds);
    const int32 Count = FMath::Min(Current.Samples.Num(), 10000);
    constexpr int32 RangesPerBone = 3;
    constexpr int32 TotalRanges = 50;
    int32 OmittedRanges = 0;
    if (Count < Current.Samples.Num())
    {
        Result.Warnings.Add(TEXT("Take comparison limited to first 10000 current samples."));
    }
    for (int32 B = 0; B < Current.BoneNames.Num(); ++B)
    {
        FJJReviewPoseDifference Max;
        Max.Bone = Current.BoneNames[B];
        double BestScore = 0;
        int32 Range = INDEX_NONE;
        int32 BoneRanges = 0;
        bool bInRange = false;
        for (int32 I = 0; I < Count; ++I)
        {
            const auto& Sample = Current.Samples[I];
            if (Sample.TimeSeconds > Duration)
            {
                break;
            }
            const FTransform A = Sample.ComponentTransforms[B].ToTransform();
            const FTransform Z = AtTime(Previous, B, Sample.TimeSeconds);
            const double Translation = FVector::Distance(A.GetTranslation(), Z.GetTranslation());
            const double Rotation = FMath::RadiansToDegrees(A.GetRotation().AngularDistance(Z.GetRotation()));
            Max.TranslationCm = FMath::Max(Max.TranslationCm, Translation);
            Max.RotationDegrees = FMath::Max(Max.RotationDegrees, Rotation);
            const double Score = FMath::Max(Translation / FMath::Max(TranslationThreshold, 1.e-6),
                Rotation / FMath::Max(RotationThreshold, 1.e-6));
            if (Score > BestScore)
            {
                Max.Frame = Sample.SourceFrame;
                BestScore = Score;
            }
            if (Score > 1)
            {
                if (!bInRange)
                {
                    bInRange = true;
                    if (BoneRanges < RangesPerBone && Result.Ranges.Num() < TotalRanges)
                    {
                        FJJReviewCheckResult Item;
                        Item.Check = TEXT("take_difference");
                        Item.Status = TEXT("warning");
                        Item.Bone = Max.Bone;
                        Item.StartFrame = Sample.SourceFrame;
                        Item.Threshold = 1;
                        Item.Unit = TEXT("ratio");
                        Item.Message = TEXT("Normalized translation/rotation difference; component space, previous take");
                        Range = Result.Ranges.Add(Item);
                        ++BoneRanges;
                    }
                    else
                    {
                        ++OmittedRanges;
                    }
                }
                if (Range != INDEX_NONE)
                {
                    Result.Ranges[Range].EndFrame = Sample.SourceFrame;
                    Result.Ranges[Range].Measured = FMath::Max(Result.Ranges[Range].Measured, Score);
                }
            }
            else
            {
                Range = INDEX_NONE;
                bInRange = false;
            }
        }
        if (BestScore > 1)
        {
            Result.Maxima.Add(Max);
        }
    }
    if (OmittedRanges > 0)
    {
        Result.Warnings.Add(FString::Printf(TEXT("Comparison ranges omitted: %d (per bone %d, total %d)."),
            OmittedRanges, RangesPerBone, TotalRanges));
    }
    Result.Maxima.Sort([&](const auto& A, const auto& B)
    {
        return FMath::Max(A.TranslationCm / FMath::Max(TranslationThreshold, 1.e-6), A.RotationDegrees / FMath::Max(RotationThreshold, 1.e-6))
            > FMath::Max(B.TranslationCm / FMath::Max(TranslationThreshold, 1.e-6), B.RotationDegrees / FMath::Max(RotationThreshold, 1.e-6));
    });
    if (Result.Maxima.Num() > FMath::Max(0, TopCount))
    {
        Result.Maxima.SetNum(FMath::Max(0, TopCount));
    }
    return Result;
}
