#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "JJAnimReviewProfile.generated.h"

class USkeleton;

USTRUCT(BlueprintType)
struct JJANIMREVIEWKITEDITOR_API FJJReviewCheckSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Review")
    bool bEnabled = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Review", meta = (ClampMin = "0"))
    double Threshold = 2.0;
};

USTRUCT(BlueprintType)
struct JJANIMREVIEWKITEDITOR_API FJJReviewSocketBonePair
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Review")
    FName Socket;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Review")
    FName Bone;
};

USTRUCT(BlueprintType)
struct JJANIMREVIEWKITEDITOR_API FJJReviewProfileSettings
{
    GENERATED_BODY()

    FJJReviewProfileSettings();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Review|Sheet")
    double ForwardYawOffset = 90.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Review")
    TArray<FName> FootBones;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Review")
    double GroundHeightCm = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Review", meta = (ClampMin = "0"))
    double ContactHeightCm = 3.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Review", meta = (ClampMin = "0"))
    double ContactSpeedCmPerSecond = 10.0;

    // Estimated profiles only: exclude fast horizontal swing, retaining sliding contacts.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Review", meta = (ClampMin = "0"))
    double EstimatedContactMaxHorizontalSpeedCmPerSecond = 50.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Review")
    TArray<FName> PenetrationBones;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Review")
    TArray<FJJReviewSocketBonePair> SocketBonePairs;

    // Stable keys: foot_sliding, penetration, pop, joint_limits,
    // root_discontinuity, loop_mismatch, socket_drift. Units in PORTING.md.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Review")
    TMap<FName, FJJReviewCheckSettings> Checks;

    static FJJReviewProfileSettings Estimate(const TArray<FName>& BoneNames);
};

UCLASS(BlueprintType)
class JJANIMREVIEWKITEDITOR_API UJJAnimReviewProfile : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Review")
    TSoftObjectPtr<USkeleton> Skeleton;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Review")
    FJJReviewProfileSettings Settings;
};
