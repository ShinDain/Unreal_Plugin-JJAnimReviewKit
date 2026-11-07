#include "JJAnimReviewTypes.h"
#include "JJAnimReviewProfile.h"

namespace JJAnimReviewTypesPrivate
{
    FJJReviewVector MakeVector(const FVector& Value)
    {
        FJJReviewVector Result;
        Result.X = Value.X;
        Result.Y = Value.Y;
        Result.Z = Value.Z;
        return Result;
    }
}

FJJReviewTransform FJJReviewTransform::FromTransform(const FTransform& Transform)
{
    FJJReviewTransform Result;
    const FRotator Rotation = Transform.Rotator().GetNormalized();
    Result.TranslationCm = JJAnimReviewTypesPrivate::MakeVector(Transform.GetTranslation());
    Result.RotationDegrees = JJAnimReviewTypesPrivate::MakeVector(FVector(Rotation.Roll, Rotation.Pitch, Rotation.Yaw));
    Result.Scale = JJAnimReviewTypesPrivate::MakeVector(Transform.GetScale3D());
    return Result;
}

FTransform FJJReviewTransform::ToTransform() const
{
    return FTransform(
        FRotator(RotationDegrees.Y, RotationDegrees.Z, RotationDegrees.X),
        FVector(TranslationCm.X, TranslationCm.Y, TranslationCm.Z),
        FVector(Scale.X, Scale.Y, Scale.Z));
}

FJJReviewProfileSettings::FJJReviewProfileSettings()
{
    const TCHAR* Keys[] =
    {
        TEXT("foot_sliding"), TEXT("penetration"), TEXT("pop"), TEXT("joint_limits"),
        TEXT("root_discontinuity"), TEXT("loop_mismatch"), TEXT("socket_drift")
    };
    const double Thresholds[] =
    {
        2.0, 1.0, 45.0, 5.0, 10.0, 2.0, 2.0
    };
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Keys); ++Index)
    {
        FJJReviewCheckSettings Setting;
        Setting.Threshold = Thresholds[Index];
        Checks.Add(FName(Keys[Index]), Setting);
    }
}

FJJReviewProfileSettings FJJReviewProfileSettings::Estimate(const TArray<FName>& BoneNames)
{
    FJJReviewProfileSettings Result;
    for (const FName Bone : BoneNames)
    {
        const FString Name = Bone.ToString().ToLower();
        TArray<FString> Tokens;
        FString Tokenized = Name.Replace(TEXT("."), TEXT("_")).Replace(TEXT("-"), TEXT("_"));
        Tokenized.ParseIntoArray(Tokens, TEXT("_"), true);

        const bool bFoot = Tokens.Contains(TEXT("foot")) || Tokens.Contains(TEXT("ball"));
        const bool bHand = Tokens.Contains(TEXT("hand")) || Tokens.Contains(TEXT("palm"));
        const bool bHelper = Tokens.Contains(TEXT("ik")) || Tokens.Contains(TEXT("twist")) || Tokens.Contains(TEXT("ctrl"));
        if (bFoot && !bHelper)
        {
            Result.FootBones.Add(Bone);
        }
        if ((bFoot || bHand) && !bHelper)
        {
            Result.PenetrationBones.Add(Bone);
        }
    }

    return Result;
}
