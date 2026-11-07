#include "JJAnimReviewCommandlet.h"

#include "JJAnimReviewService.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogJJAnimReviewCommandlet, Log, All);

UJJAnimReviewCommandlet::UJJAnimReviewCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
    ShowErrorCount = true;
}

int32 UJJAnimReviewCommandlet::Main(const FString& Params)
{
    FJJAnimReviewRequest Request;
    FParse::Value(*Params, TEXT("Asset="), Request.Asset);
    FParse::Value(*Params, TEXT("Profile="), Request.Profile);
    FParse::Value(*Params, TEXT("Output="), Request.Output);
    Request.bCapture = FParse::Param(*Params, TEXT("Sheet"));
    const FJJAnimReviewResponse Response = JJAnimReviewService::Run(Request);
    if (Response.ExitCode == 2)
    {
        UE_LOG(LogJJAnimReviewCommandlet, Error, TEXT("%s"), *Response.Error);
    }
    else
    {
        UE_LOG(LogJJAnimReviewCommandlet, Display, TEXT("Review bundle: %s (exit %d)"), *Response.TakeDirectory, Response.ExitCode);
    }
    for (const FString& Warning : Response.Warnings)
    {
        UE_LOG(LogJJAnimReviewCommandlet, Warning, TEXT("%s"), *Warning);
    }
    return Response.ExitCode;
}
