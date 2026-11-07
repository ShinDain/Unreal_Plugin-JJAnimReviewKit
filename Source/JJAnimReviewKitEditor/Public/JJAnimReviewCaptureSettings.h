#pragma once

#include "CoreMinimal.h"
#include "JJAnimReviewCaptureSettings.generated.h"

USTRUCT(BlueprintType)
struct JJANIMREVIEWKITEDITOR_API FJJReviewCaptureSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Review")
    int32 FrameCount = 6;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Review")
    int32 CellSize = 256;

    // Front assumes mesh +X; use 90 for a +Y-facing mesh.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Review")
    double ForwardYawOffset = 0.0;

    // Yaw relative to ForwardYawOffset; the last angle is always elevated, even with one angle.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Review")
    TArray<double> YawDegrees =
    {
        0, 90, 180, 45
    };

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Review")
    TArray<int32> Frames;
};
