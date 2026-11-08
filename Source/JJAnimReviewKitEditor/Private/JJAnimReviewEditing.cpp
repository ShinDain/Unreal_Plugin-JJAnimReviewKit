#include "JJAnimReviewEditing.h"

#include "JJAnimReviewModel.h"
#include "JJAnimReviewNotes.h"

namespace JJAnimReviewEditingPrivate
{
    bool RelativeDelta(const FJJReviewPoseData& Original, const FJJReviewPoseSample& Preview,
        int32 Bone, FVector& Translation, FQuat& Delta)
    {
        FTransform A = Original.Samples[0].ComponentTransforms[Bone].ToTransform();
        FTransform Z = Preview.ComponentTransforms[Bone].ToTransform();
        const int32 Parent = Original.ParentIndices[Bone];
        if (Parent != INDEX_NONE)
        {
            A = A.GetRelativeTransform(Original.Samples[0].ComponentTransforms[Parent].ToTransform());
            Z = Z.GetRelativeTransform(Preview.ComponentTransforms[Parent].ToTransform());
        }
        if (A.ContainsNaN() || Z.ContainsNaN())
        {
            return false;
        }
        Translation = Z.GetTranslation() - A.GetTranslation();
        Delta = (Z.GetRotation() * A.GetRotation().Inverse()).GetNormalized();
        if (Delta.W < 0)
        {
            Delta = Delta * -1.0;
        }
        return true;
    }
}

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
    if (!JJAnimReviewNotes::ValidateRange(Annotation, MAX_int32, Error) || !Directions.Contains(Annotation.Direction)
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
    double TranslationThreshold, double RotationThreshold, const TSet<FName>* ModifiedBones)
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
        const int32 Parent = Original.ParentIndices[B];
        if (Parent != INDEX_NONE && !Original.BoneNames.IsValidIndex(Parent))
        {
            return {};
        }
        FVector Translation;
        FQuat Delta;
        if (!JJAnimReviewEditingPrivate::RelativeDelta(Original, Preview, B, Translation, Delta))
        {
            continue;
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
    }

    if (ModifiedBones)
    {
        Differences = JJAnimReviewNotes::FilterPoseInstructions(Differences, *ModifiedBones);
    }
    // Format only the retained bones, so text and structured instructions have identical targets.
    for (const FJJReviewPoseDifference& Difference : Differences)
    {
        const int32 B = Original.BoneNames.IndexOfByKey(Difference.Bone);
        FVector Translation;
        FQuat Delta;
        if (!JJAnimReviewEditingPrivate::RelativeDelta(Original, Preview, B, Translation, Delta))
        {
            continue;
        }
        const FVector Axis = Delta.GetRotationAxis();
        SignedInstructions += FString::Printf(TEXT("\n%s parent-space delta: move(%+.3f,%+.3f,%+.3f)cm; rotate axis(%+.4f,%+.4f,%+.4f) %.3fdeg;"),
            *Difference.Bone.ToString(), Translation.X, Translation.Y, Translation.Z,
            Axis.X, Axis.Y, Axis.Z, Difference.RotationDegrees);
    }
    return Differences;
}
