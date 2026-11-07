#include "JJAnimReviewEditing.h"

#include "JJAnimReviewModel.h"

bool JJAnimReviewEditing::Apply(FJJReviewBundle& Bundle, FJJReviewAnnotation Annotation,
    const FString& Operation, FString& Error)
{
    const int32 Index = Bundle.Annotations.IndexOfByPredicate([&](const FJJReviewAnnotation& Item)
    {
        return Item.AnnotationId == Annotation.AnnotationId;
    });
    if (Operation == TEXT("add"))
    {
        if (Index != INDEX_NONE)
        {
            Error = TEXT("Annotation ID already exists");
            return false;
        }
    }
    else if (Index == INDEX_NONE)
    {
        Error = TEXT("Annotation no longer exists; refresh the tab");
        return false;
    }
    if (Operation == TEXT("delete"))
    {
        Bundle.Annotations.RemoveAt(Index);
        return true;
    }
    if (Operation == TEXT("resolve") || Operation == TEXT("reopen"))
    {
        Bundle.Annotations[Index].Status = Operation == TEXT("resolve") ? TEXT("resolved") : TEXT("open");
        return true;
    }
    if (Operation != TEXT("add") && Operation != TEXT("update"))
    {
        Error = TEXT("Unsupported annotation operation");
        return false;
    }
    const TArray<FString> Directions =
    {
        TEXT(""), TEXT("+X"), TEXT("-X"), TEXT("+Y"), TEXT("-Y"), TEXT("+Z"), TEXT("-Z")
    };
    if (Annotation.Frame < 0 || !Directions.Contains(Annotation.Direction)
        || (!Annotation.Amount.IsEmpty() && Annotation.Amount != TEXT("little") && Annotation.Amount != TEXT("much")))
    {
        Error = TEXT("Invalid frame or direction/amount");
        return false;
    }
    if (Operation == TEXT("add") || Bundle.Annotations[Index].AuthoredContentId != Bundle.ContentId)
    {
        Annotation.AnnotationId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Annotation.AuthoredTake = Bundle.Take;
        Annotation.AuthoredContentId = Bundle.ContentId;
        Annotation.bFromPreviousVersion = false;
        Annotation.Status = Operation == TEXT("add") ? TEXT("open") : Bundle.Annotations[Index].Status;
    }
    else
    {
        Annotation.AuthoredTake = Bundle.Annotations[Index].AuthoredTake;
        Annotation.AuthoredContentId = Bundle.Annotations[Index].AuthoredContentId;
        Annotation.bFromPreviousVersion = Bundle.Annotations[Index].bFromPreviousVersion;
        Annotation.Status = Bundle.Annotations[Index].Status;
    }
    if (Operation == TEXT("add"))
    {
        Bundle.Annotations.Add(Annotation);
    }
    else
    {
        Bundle.Annotations[Index] = Annotation;
    }
    return JJAnimReviewModel::ValidateBundle(Bundle, Error);
}

TArray<FJJReviewPoseDifference> JJAnimReviewEditing::PoseInstructions(const FJJReviewPoseData& Original,
    const FJJReviewPoseSample& Preview, FString& SignedInstructions,
    double TranslationThreshold, double RotationThreshold)
{
    TArray<FJJReviewPoseDifference> Differences;
    SignedInstructions.Reset();
    if (Original.Samples.Num() != 1 || Original.BoneNames.Num() != Preview.ComponentTransforms.Num()
        || Original.ParentIndices.Num() != Original.BoneNames.Num()
        || Original.Samples[0].ComponentTransforms.Num() != Original.BoneNames.Num())
    {
        return Differences;
    }
    for (int32 B = 0; B < Original.BoneNames.Num(); ++B)
    {
        FTransform A = Original.Samples[0].ComponentTransforms[B].ToTransform();
        FTransform Z = Preview.ComponentTransforms[B].ToTransform();
        const int32 P = Original.ParentIndices[B];
        if (P != INDEX_NONE)
        {
            if (!Original.BoneNames.IsValidIndex(P))
            {
                return {};
            }
            A = A.GetRelativeTransform(Original.Samples[0].ComponentTransforms[P].ToTransform());
            Z = Z.GetRelativeTransform(Preview.ComponentTransforms[P].ToTransform());
        }
        if (A.ContainsNaN() || Z.ContainsNaN())
        {
            continue;
        }
        const FVector Translation = Z.GetTranslation() - A.GetTranslation();
        FQuat Delta = (Z.GetRotation() * A.GetRotation().Inverse()).GetNormalized();
        if (Delta.W < 0)
        {
            Delta = Delta * -1.0;
        }
        const double Degrees = FMath::RadiansToDegrees(Delta.GetAngle());
        if (Translation.Size() <= TranslationThreshold && Degrees <= RotationThreshold)
        {
            continue;
        }
        FJJReviewPoseDifference Difference;
        Difference.Bone = Original.BoneNames[B];
        Difference.Frame = Preview.SourceFrame;
        Difference.TranslationCm = Translation.Size();
        Difference.RotationDegrees = Degrees;
        Differences.Add(Difference);
        const FVector Axis = Delta.GetRotationAxis();
        SignedInstructions += FString::Printf(TEXT("\n%s parent-space delta: move(%+.3f,%+.3f,%+.3f)cm; rotate axis(%+.4f,%+.4f,%+.4f) %.3fdeg;"),
            *Difference.Bone.ToString(), Translation.X, Translation.Y, Translation.Z, Axis.X, Axis.Y, Axis.Z, Degrees);
    }
    return Differences;
}
