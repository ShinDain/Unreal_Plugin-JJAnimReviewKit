#pragma once

#include "JJAnimReviewTypes.h"

struct FJJReviewComparison
{
    TArray<FJJReviewPoseDifference> Maxima;
    TArray<FJJReviewCheckResult> Ranges;
    TArray<FString> Warnings;
};

namespace JJAnimReviewComparison
{
    // Component poses, absolute asset time, linear translation / quaternion slerp.
    FTransform AtTime(const FJJReviewPoseData& Pose, int32 Bone, double Time);
    FJJReviewComparison Compare(const FJJReviewPoseData& Current, const FJJReviewPoseData& Previous,
        int32 TopCount = 10, double TranslationThreshold = 0.1, double RotationThreshold = 0.5);
}
