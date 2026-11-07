#pragma once

#include "CoreMinimal.h"
#include "JJAnimReviewProfile.h"
#include "JJAnimReviewTypes.generated.h"

// JSON uses lowerCamelCase property names; distances cm, angles degrees, time seconds.
USTRUCT()
struct JJANIMREVIEWKITEDITOR_API FJJReviewVector
{
    GENERATED_BODY()

    UPROPERTY()
    double X = 0.0;

    UPROPERTY()
    double Y = 0.0;

    UPROPERTY()
    double Z = 0.0;
};

USTRUCT()
struct JJANIMREVIEWKITEDITOR_API FJJReviewTransform
{
    GENERATED_BODY()

    UPROPERTY()
    FJJReviewVector TranslationCm;

    // X = roll, Y = pitch, Z = yaw.
    UPROPERTY()
    FJJReviewVector RotationDegrees;

    UPROPERTY()
    FJJReviewVector Scale;

    static FJJReviewTransform FromTransform(const FTransform& Transform);
    FTransform ToTransform() const;
};

USTRUCT()
struct JJANIMREVIEWKITEDITOR_API FJJReviewPoseSample
{
    GENERATED_BODY()

    UPROPERTY()
    double TimeSeconds = 0.0;

    UPROPERTY()
    int32 SourceFrame = 0;

    UPROPERTY()
    TArray<FJJReviewTransform> ComponentTransforms;

    // Cumulative Raw root change relative to time 0, in the initial root frame.
    UPROPERTY()
    FJJReviewTransform RootMotion;
};

USTRUCT()
struct JJANIMREVIEWKITEDITOR_API FJJReviewPoseData
{
    GENERATED_BODY()

    UPROPERTY()
    int32 SchemaVersion = 1;

    UPROPERTY()
    FString ContentId;

    UPROPERTY()
    FString SamplingPolicy = TEXT("raw_source_frames_v1");

    UPROPERTY()
    double DurationSeconds = 0.0;

    UPROPERTY()
    int32 RateNumerator = 30;

    UPROPERTY()
    int32 RateDenominator = 1;

    UPROPERTY()
    TArray<FName> BoneNames;

    UPROPERTY()
    TArray<int32> ParentIndices;

    UPROPERTY()
    TArray<double> NotifyTimesSeconds;

    UPROPERTY()
    TArray<FJJReviewPoseSample> Samples;
};

USTRUCT()
struct JJANIMREVIEWKITEDITOR_API FJJReviewCheckResult
{
    GENERATED_BODY()

    UPROPERTY()
    FString Check;

    // not_run, passed, warning, failed, skipped
    UPROPERTY()
    FString Status = TEXT("not_run");

    UPROPERTY()
    FName Bone;

    UPROPERTY()
    int32 StartFrame = 0;

    UPROPERTY()
    int32 EndFrame = 0;

    UPROPERTY()
    double Measured = 0.0;

    UPROPERTY()
    double Threshold = 0.0;

    UPROPERTY()
    FString Unit;

    UPROPERTY()
    FString Message;
};

USTRUCT()
struct JJANIMREVIEWKITEDITOR_API FJJReviewPoseDifference
{
    GENERATED_BODY()

    UPROPERTY()
    FName Bone;

    UPROPERTY()
    int32 Frame = 0;

    UPROPERTY()
    double TranslationCm = 0.0;

    UPROPERTY()
    double RotationDegrees = 0.0;
};

USTRUCT()
struct JJANIMREVIEWKITEDITOR_API FJJReviewAnnotation
{
    GENERATED_BODY()

    UPROPERTY()
    FString AnnotationId;

    UPROPERTY()
    FString AuthoredTake;

    UPROPERTY()
    FString AuthoredContentId;

    // open / resolved. Stable ID and origin survive carry-forward.
    UPROPERTY()
    FString Status = TEXT("open");

    UPROPERTY()
    bool bFromPreviousVersion = false;

    UPROPERTY()
    int32 Frame = 0;

    UPROPERTY()
    FName Bone;

    UPROPERTY()
    FString Text;

    // Empty or +X/-X/+Y/-Y/+Z/-Z in world axes; amount little/much.
    UPROPERTY()
    FString Direction;

    UPROPERTY()
    FString Amount;

    UPROPERTY()
    TArray<FJJReviewPoseDifference> PoseInstructions;
};

USTRUCT()
struct JJANIMREVIEWKITEDITOR_API FJJReviewBundle
{
    GENERATED_BODY()

    UPROPERTY()
    int32 SchemaVersion = 1;

    UPROPERTY()
    FString Asset;

    UPROPERTY()
    FString Take;

    UPROPERTY()
    int32 TakeNumber = 0;

    UPROPERTY()
    FString ContentId;

    UPROPERTY()
    FString CreatedUtc;

    UPROPERTY()
    FString UpdatedUtc;

    UPROPERTY()
    FString Profile;

    UPROPERTY()
    FJJReviewProfileSettings ProfileSettings;

    UPROPERTY()
    bool bEstimatedProfile = false;

    UPROPERTY()
    FString CheckStatus = TEXT("not_run");

    UPROPERTY()
    TArray<FString> Warnings;

    UPROPERTY()
    TArray<FJJReviewCheckResult> Checks;

    UPROPERTY()
    TArray<FJJReviewAnnotation> Annotations;

    UPROPERTY()
    TArray<FJJReviewPoseDifference> PoseDifferences;

    UPROPERTY()
    TArray<FString> Images;
};
