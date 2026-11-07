#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "JJAnimReviewCaptureSettings.h"
#include "JJAnimReviewLibrary.generated.h"

UCLASS()
class JJANIMREVIEWKITEDITOR_API UJJAnimReviewLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Synchronous, game thread only. OutputFolder is within Saved/JJAnimReview.
    UFUNCTION(BlueprintCallable, Category = "JJAnimReview")
    static int32 CreateReview(const FString& AssetPath, const FString& ProfilePath,
        const FString& OutputFolder, FString& TakeDirectory, FString& Error);

    UFUNCTION(BlueprintCallable, Category = "JJAnimReview")
    static int32 CreateReviewWithSheet(const FString& AssetPath, const FString& ProfilePath,
        const FString& OutputFolder, const FJJReviewCaptureSettings& Settings,
        FString& TakeDirectory, FString& Error);
};
