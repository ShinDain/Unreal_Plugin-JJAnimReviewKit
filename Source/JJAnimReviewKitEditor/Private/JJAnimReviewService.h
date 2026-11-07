#pragma once

#include "CoreMinimal.h"
#include "JJAnimReviewCaptureSettings.h"
#include "JJAnimReviewTypes.h"

struct FJJAnimReviewRequest
{
    FString Asset;
    FString Profile;
    FString Output;
    bool bCapture = false;
    FJJReviewCaptureSettings Capture;
};

struct FJJAnimReviewResponse
{
    // 0: completed, 1: checks failed (R1), 2: execution error.
    int32 ExitCode = 2;
    FString TakeDirectory;
    FString Error;
    TArray<FString> Warnings;
};

namespace JJAnimReviewService
{
    FJJAnimReviewResponse Run(const FJJAnimReviewRequest& Request);
    bool ReadLatest(const FString& Asset, FJJReviewBundle& Bundle, TArray<FJJReviewBundle>& Takes, FString& Error);
    // Read-only pose of one retained take (default output folder) for the R4 wire ghost.
    bool ReadTakePose(const FString& Asset, const FString& Take, FJJReviewPoseData& Pose, FString& Error);
    bool EditAnnotation(const FString& Asset, const FString& ExpectedContentId,
        FJJReviewAnnotation Annotation, const FString& Operation, FString& Error);
}
