#pragma once

#include "JJAnimReviewTypes.h"
#include "JJAnimReviewCaptureSettings.h"

struct FJJReviewSheetLayout
{
    int32 Columns = 0;
    int32 Rows = 0;
    int32 Cell = 0;
    FIntPoint Size = FIntPoint::ZeroValue;
};

namespace JJAnimReviewSheet
{
    TArray<int32> SelectFrames(const FJJReviewPoseData& Pose, int32 Count, const TArray<int32>& Explicit);
    FJJReviewSheetLayout Layout(int32 Frames, int32 Angles, int32 RequestedCell);
}
