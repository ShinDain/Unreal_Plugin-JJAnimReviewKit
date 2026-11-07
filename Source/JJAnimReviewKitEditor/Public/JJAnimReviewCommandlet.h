#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "JJAnimReviewCommandlet.generated.h"

UCLASS()
class JJANIMREVIEWKITEDITOR_API UJJAnimReviewCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UJJAnimReviewCommandlet();
    virtual int32 Main(const FString& Params) override;
};
