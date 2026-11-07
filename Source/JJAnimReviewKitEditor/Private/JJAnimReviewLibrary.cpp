#include "JJAnimReviewLibrary.h"

#include "JJAnimReviewService.h"

int32 UJJAnimReviewLibrary::CreateReviewWithSheet(const FString& AssetPath, const FString& ProfilePath,
    const FString& OutputFolder, const FJJReviewCaptureSettings& Settings, FString& TakeDirectory, FString& Error)
{
    FJJAnimReviewRequest Request;
    Request.Asset = AssetPath;
    Request.Profile = ProfilePath;
    Request.Output = OutputFolder;
    Request.bCapture = true;
    Request.Capture = Settings;
    const FJJAnimReviewResponse Response = JJAnimReviewService::Run(Request);
    TakeDirectory = Response.TakeDirectory;
    Error = Response.Error;
    return Response.ExitCode;
}

int32 UJJAnimReviewLibrary::CreateReview(const FString& AssetPath, const FString& ProfilePath,
    const FString& OutputFolder, FString& TakeDirectory, FString& Error)
{
    FJJAnimReviewRequest Request;
    Request.Asset = AssetPath;
    Request.Profile = ProfilePath;
    Request.Output = OutputFolder;
    const FJJAnimReviewResponse Response = JJAnimReviewService::Run(Request);
    TakeDirectory = Response.TakeDirectory;
    Error = Response.Error;
    return Response.ExitCode;
}
