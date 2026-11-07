#include "JJAnimReviewSheet.h"

TArray<int32> JJAnimReviewSheet::SelectFrames(const FJJReviewPoseData& Pose, int32 Count, const TArray<int32>& Explicit)
{
    TArray<int32> Result;
    if (Pose.Samples.IsEmpty())
    {
        return Result;
    }
    const int32 Last = Pose.Samples.Last().SourceFrame;
    Count = FMath::Clamp(Count, 2, 24);
    if (!Explicit.IsEmpty())
    {
        for (int32 Frame : Explicit)
        {
            Result.AddUnique(FMath::Clamp(Frame, 0, Last));
            if (Result.Num() == 24)
            {
                break;
            }
        }
    }
    else
    {
        Result.Add(0);
        Result.AddUnique(Last);
        for (double Time : Pose.NotifyTimesSeconds)
        {
            if (Result.Num() >= Count)
            {
                break;
            }
            Result.AddUnique(FMath::Clamp(FMath::RoundToInt(Time * Pose.RateNumerator / Pose.RateDenominator), 0, Last));
        }
        // Fill the largest remaining gap; no duplicate frames on short clips.
        while (Result.Num() < Count)
        {
            Result.Sort();
            int32 Gap = 1;
            int32 Candidate = INDEX_NONE;
            for (int32 I = 1; I < Result.Num(); ++I)
            {
                if (Result[I] - Result[I - 1] > Gap)
                {
                    Gap = Result[I] - Result[I - 1];
                    Candidate = Result[I - 1] + Gap / 2;
                }
            }
            if (Candidate == INDEX_NONE)
            {
                break;
            }
            Result.Add(Candidate);
        }
    }
    Result.Sort();
    return Result;
}

FJJReviewSheetLayout JJAnimReviewSheet::Layout(int32 Frames, int32 Angles, int32 RequestedCell)
{
    FJJReviewSheetLayout Result;
    if (Frames <= 0 || Frames > 24 || Angles <= 0 || Angles > 8)
    {
        return Result;
    }
    Result.Columns = Frames;
    Result.Rows = Angles;
    Result.Cell = FMath::Min(FMath::Clamp(RequestedCell, 32, 512), 1568 / FMath::Max(Frames, Angles));
    Result.Size = FIntPoint(Frames * Result.Cell, Angles * Result.Cell);
    return Result;
}
