#pragma once

#include "JJAnimReviewTypes.h"

// Plain inputs: adapters resolve asset metadata, synthetic callers supply it directly.
struct FJJReviewJoint
{
    FName Child;
    FName Parent;
    FTransform ChildFrame = FTransform::Identity;
    FTransform ParentFrame = FTransform::Identity;
    FVector LimitsDegrees = FVector::ZeroVector; // twist, swing1, swing2
    FIntVector Motion = FIntVector::ZeroValue; // 0 free, 1 limited, 2 locked
};

struct FJJReviewSocket
{
    FName Socket;
    FName Parent;
    FName Target;
    FTransform Local = FTransform::Identity;
};

struct FJJReviewCheckContext
{
    bool bLooping = false;
    bool bEstimatedProfile = false;
    TArray<FJJReviewJoint> Joints;
    TArray<FJJReviewSocket> Sockets;
    TArray<FString> Warnings;
};

namespace JJAnimReviewChecks
{
    // Full content identity still uses all frames; expensive checks cap at 10000 samples.
    TArray<FJJReviewCheckResult> Evaluate(const FJJReviewPoseData& Pose,
        const FJJReviewProfileSettings& Profile, const FJJReviewCheckContext& Context);
}
